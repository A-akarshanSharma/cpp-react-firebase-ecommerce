#include "FirebaseAuth.h"
#include "Errors.h"
#include "runtime/Runtime.h"
#include <drogon/HttpClient.h>
#include <drogon/drogon.h>
#include <fstream>
#include <future>
#include <jwt-cpp/jwt.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <trantor/net/EventLoopThread.h>

using json = nlohmann::json;

FirebaseAuth::FirebaseAuth(const std::string &serviceAccountPath)
{
    std::ifstream file(serviceAccountPath);
    if (!file.is_open())
        throw std::runtime_error("Could not open service account file: " + serviceAccountPath);

    json sa;
    file >> sa;

    clientEmail_ = sa.at("client_email").get<std::string>();
    privateKey_ = sa.at("private_key").get<std::string>();
    tokenUri_ = sa.value("token_uri", "https://oauth2.googleapis.com/token");
}

std::string FirebaseAuth::signedJwt()
{
    auto now = std::chrono::system_clock::now();
    auto exp = now + std::chrono::hours(1);

    // Firestore plus read-only account lookup (IAM still governs permissions).
    std::string token =
        jwt::create()
            .set_issuer(clientEmail_)
            .set_subject(clientEmail_)
            .set_audience(tokenUri_)
            .set_issued_at(now)
            .set_expires_at(exp)
            .set_payload_claim("scope",
                               jwt::claim(std::string("https://www.googleapis.com/auth/datastore "
                                                      "https://www.googleapis.com/auth/identitytoolkit")))
            .sign(jwt::algorithm::rs256("", privateKey_, "", ""));

    return token;
}

void FirebaseAuth::refreshAccessToken()
{
    std::string assertion = signedJwt();

    static trantor::EventLoopThread io;
    static const bool started = [] {
        io.run();
        return true;
    }();
    (void)started;
    auto client = drogon::HttpClient::newHttpClient("https://oauth2.googleapis.com", io.getLoop());
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Post);
    req->setPath("/token");
    req->setContentTypeCode(drogon::CT_APPLICATION_X_FORM);

    std::string body = "grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer&assertion=" + assertion;
    req->setBody(body);

    auto prom = std::make_shared<std::promise<drogon::HttpResponsePtr>>();
    auto fut = prom->get_future();

    const auto budget = runtime::remaining();
    client->sendRequest(
        req,
        [prom](drogon::ReqResult result, const drogon::HttpResponsePtr &resp) {
            if (result == drogon::ReqResult::Ok)
                prom->set_value(resp);
            else
                prom->set_value(nullptr);
        },
        std::chrono::duration<double>(budget).count());

    if (fut.wait_for(budget) != std::future_status::ready)
        throw DatabaseError(503, "AUTH_UNAVAILABLE");
    auto resp = fut.get();
    if (!resp)
        throw DatabaseError(503, "AUTH_UNAVAILABLE");

    auto body_str = std::string(resp->getBody());
    json respJson = json::parse(body_str, nullptr, false);

    if (respJson.is_discarded() || !respJson.contains("access_token"))
        throw DatabaseError(503, "AUTH_UNAVAILABLE");

    cachedToken_ = respJson.at("access_token").get<std::string>();
    int expiresIn = respJson.value("expires_in", 3600);
    // refresh 60s before actual expiry to be safe
    tokenExpiry_ = std::chrono::steady_clock::now() + std::chrono::seconds(expiresIn - 60);
}

std::string FirebaseAuth::getAccessToken()
{
    auto guard = runtime::lock(mutex_);
    if (cachedToken_.empty() || std::chrono::steady_clock::now() >= tokenExpiry_)
        refreshAccessToken();

    return cachedToken_;
}
