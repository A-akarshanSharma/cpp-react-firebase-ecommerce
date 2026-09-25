#include "FirebaseAuth.h"
#include "FirestoreClient.h"
#include "controllers/AuthMiddleware.h"
#include "controllers/ControllerSupport.h"
#include "services/Ordering.h"
#include "runtime/Proxy.h"
#include <atomic>
#include <cstdlib>
#include <drogon/drogon.h>
#include <fstream>
#include <map>
#include <sstream>

// Shared instances used by controllers (see ProductController.cpp).
// Simple approach for phase 1/2 - fine at this scale.
std::string g_uploadDirectory = "../uploads";
FirebaseAuth *g_firebaseAuth = nullptr;
FirestoreClient *g_firestoreClient = nullptr;
AuthMiddleware *g_authMiddleware = nullptr;

// Very small .env loader so you don't need a separate library for phase 1.
static std::map<std::string, std::string> loadEnv(const std::string &path)
{
    std::map<std::string, std::string> env;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line))
    {
        auto pos = line.find('=');
        if (pos == std::string::npos || line.empty() || line[0] == '#')
            continue;
        env[line.substr(0, pos)] = line.substr(pos + 1);
    }
    return env;
}

int main()
{
    const auto envFile = std::getenv("APP_ENV_FILE");
    auto env = loadEnv(envFile ? envFile : "../.env");
    // Process environment takes precedence; deployment needs no working-directory-dependent file.
    for (const auto key : {"FIREBASE_PROJECT_ID", "SERVICE_ACCOUNT_PATH", "SERVER_HOST", "SERVER_PORT",
                           "UPLOAD_DIRECTORY", "ALLOWED_ORIGIN", "TRUSTED_PROXY_IP"})
        if (const auto value = std::getenv(key))
            env[key] = value;
    const auto trustedProxy = env.count("TRUSTED_PROXY_IP") ? env["TRUSTED_PROXY_IP"] : "";
    const auto trustedIp = runtime::canonicalIp(trustedProxy);
    if (!trustedProxy.empty() && trustedIp.empty())
    {
        LOG_ERROR << "TRUSTED_PROXY_IP must be one exact IPv4 or IPv6 address";
        return 1;
    }

    std::string projectId = env.count("FIREBASE_PROJECT_ID") ? env["FIREBASE_PROJECT_ID"] : "";
    std::string saPath =
        env.count("SERVICE_ACCOUNT_PATH") ? env["SERVICE_ACCOUNT_PATH"] : "../service-account.json";
    int port = env.count("SERVER_PORT") ? std::stoi(env["SERVER_PORT"]) : 8080;

    if (projectId.empty())
    {
        LOG_ERROR << "FIREBASE_PROJECT_ID is required";
        return 1;
    }

    static FirebaseAuth auth(saPath);
    static FirestoreClient firestore(projectId, auth);
    static AuthMiddleware authMiddleware(firestore, projectId, auth);

    runtime::Limits limits;
    runtime::WorkerPool requestWorkers(4, 16);
    runtime::workers = &requestWorkers;
    runtime::limits = &limits;
    g_firebaseAuth = &auth;
    g_firestoreClient = &firestore;
    g_authMiddleware = &authMiddleware;

    if (env.count("UPLOAD_DIRECTORY"))
        g_uploadDirectory = env["UPLOAD_DIRECTORY"];

    std::string allowedOrigin = env.count("ALLOWED_ORIGIN") ? env["ALLOWED_ORIGIN"] : "*";

    // CORS - without this, browsers block every request from the frontend (different
    // origin/port) even though curl works fine, since curl doesn't enforce CORS.
    // Handles the OPTIONS preflight browsers send before POST/PUT/DELETE requests,
    // and tags every actual response with the allowed origin.
    drogon::app().registerPreRoutingAdvice([allowedOrigin, trustedIp](const drogon::HttpRequestPtr &req,
                                                           drogon::AdviceCallback &&callback,
                                                           drogon::AdviceChainCallback &&next) {
        try
        {
            // Only the configured proxy may supply its overwritten X-Real-IP header.
            const auto peer = runtime::clientIp(req->peerAddr().toIp(), trustedIp, req->getHeader("X-Real-IP"));
            runtime::limits->take("ip:" + peer, 120, 240);
            if (req->method() == drogon::Post &&
                (req->path() == "/orders" || req->path() == "/admin/uploads"))
                runtime::limits->take("sensitive-ip:" + peer, 10, 30);
        }
        catch (const ApiError &e)
        {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(static_cast<drogon::HttpStatusCode>(e.status));
            resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
            if (e.status == 429 || e.status == 503)
                resp->addHeader("Retry-After", std::to_string(e.details.value("retryAfter", 1)));
            resp->setBody(nlohmann::json{{"code", e.code}, {"error", e.what()}}.dump());
            callback(resp);
            return;
        }
        if (req->method() == drogon::Options)
        {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->addHeader("Access-Control-Allow-Origin", allowedOrigin);
            resp->addHeader("Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS");
            resp->addHeader("Access-Control-Allow-Headers", "Content-Type,Authorization,Idempotency-Key");
            callback(resp);
            return;
        }
        next();
    });

    // Router-generated errors (including 404) bypass post-handling advice.
    // Apply CORS just before sending so browsers can read those responses too.
    drogon::app().registerPreSendingAdvice(
        [allowedOrigin](const drogon::HttpRequestPtr &req, const drogon::HttpResponsePtr &resp) {
            resp->addHeader("Access-Control-Allow-Origin", allowedOrigin);
            resp->addHeader("Access-Control-Expose-Headers", "Retry-After");
        });

    LOG_INFO << "Starting server on port " << port << " for project " << projectId;

    drogon::app().registerHandler(
        "/health",
        [](const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            auto response = drogon::HttpResponse::newHttpResponse();
            response->setContentTypeCode(drogon::CT_APPLICATION_JSON);
            response->setBody(R"({"status":"ok"})");
            callback(response);
        },
        {drogon::Get});
    std::atomic<bool> sweepRunning{false};
    nlohmann::json expiryCursor = nullptr;
    runtime::WorkerPool maintenance(1, 1);
    const auto expiryTimer = drogon::app().getLoop()->runEvery(30.0, [&] {
        if (sweepRunning.exchange(true))
            return;
        if (!maintenance.submit([&] {
                struct Reset
                {
                    std::atomic<bool> &flag;
                    ~Reset()
                    {
                        flag = false;
                    }
                } reset{sweepRunning};
                runtime::Deadline deadline(runtime::Clock::now() + std::chrono::seconds(25));
                try
                {
                    const auto due = firestore.dueOrders(commerce::nowSeconds(), expiryCursor);
                    if (due.empty())
                        expiryCursor = nullptr;
                    for (const auto &order : due)
                    {
                        runtime::remaining();
                        expiryCursor = order;
                        try
                        {
                            commerce::expireOrder(firestore, order.at("id"));
                        }
                        catch (const std::exception &)
                        {
                            LOG_ERROR << "Order expiry failed for " << order.at("id").get<std::string>();
                        }
                    }
                }
                catch (const std::exception &)
                {
                    LOG_ERROR << "Order expiry sweep failed; will retry";
                }
            }))
            sweepRunning = false;
    });
    drogon::app().setThreadNum(2);
    drogon::app().setClientMaxBodySize(4 * 1024 * 1024);
    drogon::app().addListener(env.count("SERVER_HOST") ? env["SERVER_HOST"] : "0.0.0.0", port);
    drogon::app().run();
    drogon::app().getLoop()->invalidateTimer(expiryTimer);

    return 0;
}
