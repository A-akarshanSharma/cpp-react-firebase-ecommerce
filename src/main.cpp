#include <drogon/drogon.h>
#include "FirebaseAuth.h"
#include "FirestoreClient.h"
#include "controllers/AuthMiddleware.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <map>

// Shared instances used by controllers (see TestController.cpp, ProductController.cpp).
// Simple approach for phase 1/2 - fine at this scale.
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
    auto env = loadEnv("../.env");

    std::string projectId = env.count("FIREBASE_PROJECT_ID") ? env["FIREBASE_PROJECT_ID"] : "";
    std::string saPath = env.count("SERVICE_ACCOUNT_PATH") ? env["SERVICE_ACCOUNT_PATH"] : "../service-account.json";
    int port = env.count("SERVER_PORT") ? std::stoi(env["SERVER_PORT"]) : 8080;

    if (projectId.empty())
    {
        LOG_ERROR << "FIREBASE_PROJECT_ID not set in .env - check your config";
        return 1;
    }

    static FirebaseAuth auth(saPath);
    static FirestoreClient firestore(projectId, auth);
    static AuthMiddleware authMiddleware(firestore, projectId);

    g_firebaseAuth = &auth;
    g_firestoreClient = &firestore;
    g_authMiddleware = &authMiddleware;

    std::string allowedOrigin = env.count("ALLOWED_ORIGIN") ? env["ALLOWED_ORIGIN"] : "*";

    // CORS - without this, browsers block every request from the frontend (different
    // origin/port) even though curl works fine, since curl doesn't enforce CORS.
    // Handles the OPTIONS preflight browsers send before POST/PUT/DELETE requests,
    // and tags every actual response with the allowed origin.
    drogon::app().registerPreRoutingAdvice(
        [allowedOrigin](const drogon::HttpRequestPtr &req,
                         drogon::AdviceCallback &&callback,
                         drogon::AdviceChainCallback &&next) {
            if (req->method() == drogon::Options)
            {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->addHeader("Access-Control-Allow-Origin", allowedOrigin);
                resp->addHeader("Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS");
                resp->addHeader("Access-Control-Allow-Headers", "Content-Type,Authorization");
                callback(resp);
                return;
            }
            next();
        });

    drogon::app().registerPostHandlingAdvice(
        [allowedOrigin](const drogon::HttpRequestPtr &req, const drogon::HttpResponsePtr &resp) {
            resp->addHeader("Access-Control-Allow-Origin", allowedOrigin);
        });

    LOG_INFO << "Starting server on port " << port << " for project " << projectId;

    drogon::app().addListener("0.0.0.0", port);
    drogon::app().run();

    return 0;
}
