#include "UserController.h"
#include "AuthMiddleware.h"
#include "../FirestoreClient.h"
#include <nlohmann/json.hpp>

extern FirestoreClient *g_firestoreClient;
extern AuthMiddleware *g_authMiddleware;

static bool requireAdmin(const drogon::HttpRequestPtr &req,
                          const std::function<void(const drogon::HttpResponsePtr &)> &callback)
{
    AuthResult auth = g_authMiddleware->verify(req);

    if (!auth.valid)
    {
        Json::Value err;
        err["error"] = "Not authenticated: " + auth.errorMessage;
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k401Unauthorized);
        callback(resp);
        return false;
    }

    if (!auth.isAdmin)
    {
        Json::Value err;
        err["error"] = "Admin access required";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k403Forbidden);
        callback(resp);
        return false;
    }

    return true;
}

void UserController::listUsers(const drogon::HttpRequestPtr &req,
                                std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    if (!requireAdmin(req, callback))
        return;

    auto docs = g_firestoreClient->listDocuments("users"); // nlohmann::json array

    Json::Value result(Json::arrayValue);
    for (const auto &doc : docs)
    {
        Json::Value u;
        u["uid"] = doc.value("id", "");
        u["email"] = doc.value("email", "");
        u["role"] = doc.value("role", "customer");
        result.append(u);
    }

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

void UserController::setUserRole(const drogon::HttpRequestPtr &req,
                                  std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                  std::string uid)
{
    if (!requireAdmin(req, callback))
        return;

    auto bodyJson = nlohmann::json::parse(req->getBody(), nullptr, false);
    if (bodyJson.is_discarded() || !bodyJson.contains("role"))
    {
        Json::Value err;
        err["error"] = "Body must include \"role\"";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    std::string newRole = bodyJson.at("role").get<std::string>();
    if (newRole != "admin" && newRole != "customer")
    {
        Json::Value err;
        err["error"] = "role must be \"admin\" or \"customer\"";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    // Read the existing doc first so we don't wipe out the email field on update.
    auto existing = g_firestoreClient->getDocument("users", uid);
    nlohmann::json updated;
    updated["email"] = existing.value("email", "");
    updated["role"] = newRole;

    bool ok = g_firestoreClient->setDocument("users", uid, updated);

    Json::Value result;
    result["success"] = ok;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    if (!ok)
        resp->setStatusCode(drogon::k500InternalServerError);
    callback(resp);
}

void UserController::me(const drogon::HttpRequestPtr &req,
                         std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    AuthResult auth = g_authMiddleware->verify(req);

    if (!auth.valid)
    {
        Json::Value err;
        err["error"] = "Not authenticated: " + auth.errorMessage;
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k401Unauthorized);
        callback(resp);
        return;
    }

    Json::Value result;
    result["uid"] = auth.uid;
    result["email"] = auth.email;
    result["isAdmin"] = auth.isAdmin;

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}
