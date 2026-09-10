#include "FirebaseAuth.h"
#include <drogon/HttpClient.h>
#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <jwt-cpp/jwt.h>
#include <fstream>
#include <sstream>
#include <future>
#include <stdexcept>

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

    // Scope needed for Firestore access. Add more scopes here later
    // (e.g. Firebase Auth admin scope) if you need to verify user tokens
    // server-side using Google's admin APIs instead of manual JWK verification.
    std::string token = jwt::create()
        .set_issuer(clientEmail_)
        .set_subject(clientEmail_)
        .set_audience(tokenUri_)
        .set_issued_at(now)
        .set_expires_at(exp)
        .set_payload_claim("scope", jwt::claim(std::string("https://www.googleapis.com/auth/datastore")))
        .sign(jwt::algorithm::rs256("", privateKey_, "", ""));

    return token;
}

void FirebaseAuth::refreshAccessToken()
{
    std::string assertion = signedJwt();

    auto client = drogon::HttpClient::newHttpClient("https://oauth2.googleapis.com");
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Post);
    req->setPath("/token");
    req->setContentTypeCode(drogon::CT_APPLICATION_X_FORM);

    std::string body = "grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer&assertion=" + assertion;
    req->setBody(body);

    auto prom = std::make_shared<std::promise<drogon::HttpResponsePtr>>();
    auto fut = prom->get_future();

    client->sendRequest(req, [prom](drogon::ReqResult result, const drogon::HttpResponsePtr &resp) {
        if (result == drogon::ReqResult::Ok)
            prom->set_value(resp);
        else
            prom->set_value(nullptr);
    });

    auto resp = fut.get();
    if (!resp)
        throw std::runtime_error("Token exchange request failed (network error)");

    auto body_str = std::string(resp->getBody());
    json respJson = json::parse(body_str, nullptr, false);

    if (respJson.is_discarded() || !respJson.contains("access_token"))
        throw std::runtime_error("Token exchange failed, response: " + body_str);

    cachedToken_ = respJson.at("access_token").get<std::string>();
    int expiresIn = respJson.value("expires_in", 3600);
    // refresh 60s before actual expiry to be safe
    tokenExpiry_ = std::chrono::steady_clock::now() + std::chrono::seconds(expiresIn - 60);
}

std::string FirebaseAuth::getAccessToken()
{
    if (cachedToken_.empty() || std::chrono::steady_clock::now() >= tokenExpiry_)
        refreshAccessToken();

    return cachedToken_;
}
