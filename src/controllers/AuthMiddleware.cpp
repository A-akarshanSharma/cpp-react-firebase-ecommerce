#include "AuthMiddleware.h"
#include <drogon/HttpClient.h>
#include <jwt-cpp/jwt.h>
#include <nlohmann/json.hpp>
#include <future>
#include <iostream>

using json = nlohmann::json;

AuthMiddleware::AuthMiddleware(FirestoreClient &firestore, const std::string &projectId)
    : firestore_(firestore), projectId_(projectId)
{
}

// Google publishes the public certs used to sign Firebase ID tokens at this URL.
// We cache them and refetch periodically (their Cache-Control header says how long
// they're valid for, but refetching every hour is a simple safe default).
void AuthMiddleware::refreshGoogleKeys()
{
    auto client = drogon::HttpClient::newHttpClient("https://www.googleapis.com");
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath("/robot/v1/metadata/x509/securetoken@system.gserviceaccount.com");

    auto prom = std::make_shared<std::promise<drogon::HttpResponsePtr>>();
    auto fut = prom->get_future();
    client->sendRequest(req, [prom](drogon::ReqResult result, const drogon::HttpResponsePtr &resp) {
        prom->set_value(result == drogon::ReqResult::Ok ? resp : nullptr);
    });

    auto resp = fut.get();
    if (!resp || resp->getStatusCode() != drogon::k200OK)
    {
        std::cerr << "Failed to fetch Google public keys\n";
        return;
    }

    json parsed = json::parse(resp->getBody(), nullptr, false);
    if (parsed.is_discarded())
        return;

    googleCerts_.clear();
    for (auto it = parsed.begin(); it != parsed.end(); ++it)
        googleCerts_[it.key()] = it.value().get<std::string>();

    certsExpiry_ = std::chrono::steady_clock::now() + std::chrono::hours(1);
}

AuthResult AuthMiddleware::verify(const drogon::HttpRequestPtr &req)
{
    AuthResult result;

    std::string authHeader = req->getHeader("Authorization");
    const std::string prefix = "Bearer ";
    if (authHeader.rfind(prefix, 0) != 0)
    {
        result.errorMessage = "Missing or malformed Authorization header";
        return result;
    }
    std::string token = authHeader.substr(prefix.size());

    // Make sure we have current keys before attempting verification.
    if (googleCerts_.empty() || std::chrono::steady_clock::now() >= certsExpiry_)
        refreshGoogleKeys();

    try
    {
        auto decoded = jwt::decode(token);

        if (!decoded.has_key_id())
        {
            result.errorMessage = "Token missing key id";
            return result;
        }
        std::string kid = decoded.get_key_id();

        auto certIt = googleCerts_.find(kid);
        if (certIt == googleCerts_.end())
        {
            // Key might have rotated - refresh once and retry before giving up.
            refreshGoogleKeys();
            certIt = googleCerts_.find(kid);
            if (certIt == googleCerts_.end())
            {
                result.errorMessage = "Unknown signing key";
                return result;
            }
        }

        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::rs256(certIt->second, "", "", ""))
            .with_issuer("https://securetoken.google.com/" + projectId_)
            .with_audience(projectId_)
            .leeway(60); // small clock-skew tolerance

        verifier.verify(decoded); // throws on any failure (bad signature, expired, wrong issuer/audience)

        result.uid = decoded.get_payload_claim("sub").as_string();
        if (decoded.has_payload_claim("email"))
            result.email = decoded.get_payload_claim("email").as_string();

        result.valid = true;
    }
    catch (const std::exception &e)
    {
        result.errorMessage = std::string("Token verification failed: ") + e.what();
        return result;
    }

    // Look up (or create) this user's role doc in Firestore.
    auto userDoc = firestore_.getDocument("users", result.uid);
    if (userDoc.empty())
    {
        // First time we've seen this user - create their profile with a default role.
        json newUser = {{"email", result.email}, {"role", "customer"}};
        firestore_.setDocument("users", result.uid, newUser);
        result.isAdmin = false;
    }
    else
    {
        result.isAdmin = (userDoc.value("role", "customer") == "admin");
    }

    return result;
}
