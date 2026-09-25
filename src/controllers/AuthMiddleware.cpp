#include "AuthMiddleware.h"
#include "../runtime/Runtime.h"
#include "../services/Commerce.h"
#include <drogon/HttpClient.h>
#include <future>
#include <iostream>
#include <jwt-cpp/jwt.h>
#include <nlohmann/json.hpp>
#include <trantor/net/EventLoopThread.h>

using json = nlohmann::json;

namespace
{
json fetchGoogleKeys()
{
    static trantor::EventLoopThread io;
    static const bool started = [] {
        io.run();
        return true;
    }();
    (void)started;
    auto client = drogon::HttpClient::newHttpClient("https://www.googleapis.com", io.getLoop());
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath("/robot/v1/metadata/x509/securetoken@system.gserviceaccount.com");

    auto prom = std::make_shared<std::promise<drogon::HttpResponsePtr>>();
    auto fut = prom->get_future();
    const auto budget = runtime::remaining();
    client->sendRequest(
        req,
        [prom](drogon::ReqResult result, const drogon::HttpResponsePtr &resp) {
            prom->set_value(result == drogon::ReqResult::Ok ? resp : nullptr);
        },
        std::chrono::duration<double>(budget).count());

    if (fut.wait_for(budget) != std::future_status::ready)
        throw DatabaseError(503, "AUTH_UNAVAILABLE");
    auto resp = fut.get();
    if (!resp || resp->getStatusCode() != drogon::k200OK)
    {
        throw DatabaseError(503, "AUTH_UNAVAILABLE");
    }

    return json::parse(resp->getBody());
}
[[noreturn]] void unavailable()
{
    throw ApiError(503, "AUTH_UNAVAILABLE",
                   "Account verification is temporarily unavailable. Please try again.");
}
} // namespace
AuthMiddleware::AuthMiddleware(FirestoreClient &firestore, const std::string &projectId,
                               FirebaseAuth &credentials)
    : AuthMiddleware(firestore, projectId, fetchGoogleKeys,
                     FirestoreClient::httpTransport("https://identitytoolkit.googleapis.com",
                                                    [&credentials] { return credentials.getAccessToken(); }))
{
}
AuthMiddleware::AuthMiddleware(FirestoreClient &firestore, const std::string &projectId, KeyProvider keys,
                               FirestoreClient::Transport accounts,
                               std::function<std::chrono::steady_clock::time_point()> clock)
    : clock_(std::move(clock)), keys_(std::move(keys)), accounts_(std::move(accounts)), firestore_(firestore),
      projectId_(projectId)
{
}
void AuthMiddleware::refreshGoogleKeys()
{
    const auto now = clock_();
    if (now < nextRefresh_)
        throw DatabaseError(503, "AUTH_UNAVAILABLE");
    nextRefresh_ = now + std::chrono::seconds(30);
    try
    {
        const auto parsed = keys_();
        if (!parsed.is_object() || parsed.empty())
            throw std::runtime_error("Invalid keys");
        std::map<std::string, std::string> replacement;
        for (auto it = parsed.begin(); it != parsed.end(); ++it)
        {
            if (!it.value().is_string() || it.value().get<std::string>().empty())
                throw std::runtime_error("Invalid key");
            const auto pem = it.value().get<std::string>();
            (void)jwt::algorithm::rs256(pem, "", "", ""); // Do not cache malformed provider keys.
            replacement[it.key()] = pem;
        }
        googleCerts_ = std::move(replacement);
        certsExpiry_ = clock_() + std::chrono::hours(1);
    }
    catch (...)
    {
        throw DatabaseError(503, "AUTH_UNAVAILABLE");
    }
}
AuthResult AuthMiddleware::verify(const drogon::HttpRequestPtr &req)
{
    AuthResult result;
    result.errorMessage = "Please sign in again";
    const auto header = req->getHeader("Authorization");
    if (header.rfind("Bearer ", 0) != 0 || header.size() <= 7 || header.size() > 16384)
        return result;
    int64_t authTime = 0;
    try
    {
        const auto decoded = jwt::decode(header.substr(7));
        if (!decoded.has_key_id() || decoded.get_algorithm() != "RS256")
            return result;
        const auto claims = json::parse(decoded.get_payload());
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        for (const auto *field : {"exp", "iat", "auth_time"})
            if (!claims.contains(field) || !claims[field].is_number_integer() ||
                claims[field].get<int64_t>() < 0)
                return result;
        if (claims["iat"].get<int64_t>() > now + 60 || claims["auth_time"].get<int64_t>() > now + 60)
            return result;
        if (!claims.contains("sub") || !claims["sub"].is_string())
            return result;
        result.uid = claims["sub"].get<std::string>();
        if (result.uid.empty() || result.uid.size() > 128 || result.uid.find('/') != std::string::npos ||
            result.uid == "." || result.uid == "..")
            return result;
        std::string certificate;
        {
            auto guard = runtime::lock(mutex_);
            bool refreshed = false;
            if (googleCerts_.empty() || clock_() >= certsExpiry_)
            {
                refreshGoogleKeys();
                refreshed = true;
            }
            const auto kid = decoded.get_key_id();
            if (!googleCerts_.count(kid) && !refreshed && clock_() >= nextRefresh_)
                refreshGoogleKeys();
            if (!googleCerts_.count(kid))
                return result;
            certificate = googleCerts_.at(kid);
        }
        const auto algorithm = [&] {
            try
            {
                return jwt::algorithm::rs256(certificate, "", "", "");
            }
            catch (...)
            {
                throw DatabaseError(503, "AUTH_UNAVAILABLE");
            }
        }();
        jwt::verify()
            .allow_algorithm(algorithm)
            .with_issuer("https://securetoken.google.com/" + projectId_)
            .with_audience(projectId_)
            .leeway(60)
            .verify(decoded);
        authTime = claims["auth_time"].get<int64_t>();
        if (claims.contains("email"))
            result.email = claims["email"].get<std::string>();
    }
    catch (const ApiError &)
    {
        throw;
    }
    catch (const DatabaseError &)
    {
        unavailable();
    }
    catch (const std::exception &)
    {
        return result;
    }

    if (runtime::limits)
        runtime::limits->account(result.uid, req->methodString(), req->path());

    // Check the current account on every protected request. Do not cache a positive
    // result: that would extend access after disablement or session revocation.
    try
    {
        const auto response = accounts_(
            "POST", "/v1/projects/" + drogon::utils::urlEncodeComponent(projectId_) + "/accounts:lookup",
            {{"localId", json::array({result.uid})}});
        if (response.status != 200 || !response.body.is_object() || response.body.contains("error"))
            unavailable();
        if (!response.body.contains("users"))
            return result; // Firebase omits users when no account matches.
        const auto &users = response.body["users"];
        if (!users.is_array())
            unavailable();
        if (users.empty())
            return result;
        if (users.size() != 1 || !users[0].is_object())
            unavailable();
        const auto &user = users[0];
        if (!user.contains("localId") || !user["localId"].is_string() || user["localId"] != result.uid)
            unavailable();
        if (user.contains("disabled") && !user["disabled"].is_boolean())
            unavailable();
        if (user.value("disabled", false))
            return result;
        int64_t validSince = 0;
        if (user.contains("validSince"))
        {
            if (!user["validSince"].is_string())
                unavailable();
            const auto value = user["validSince"].get<std::string>();
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
                unavailable();
            validSince = std::stoll(value);
        }
        // Compare the original authentication time, not the refreshed token's iat.
        if (authTime < validSince)
            return result;
    }
    catch (const std::exception &)
    {
        unavailable();
    }

    const auto userDoc = commerce::ensureProfile(firestore_, result.uid, result.email);
    result.isAdmin = userDoc.value("role", "customer") == "admin";
    result.valid = true;
    result.errorMessage.clear();
    return result;
}
