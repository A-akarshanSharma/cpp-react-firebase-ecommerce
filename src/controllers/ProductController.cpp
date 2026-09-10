#include "ProductController.h"
#include "AuthMiddleware.h"
#include "../FirestoreClient.h"
#include "../models/Product.h"
#include <nlohmann/json.hpp>

extern FirestoreClient *g_firestoreClient;
extern AuthMiddleware *g_authMiddleware;

static Json::Value productToJsonValue(const Product &p)
{
    Json::Value v;
    v["id"] = p.id;
    v["name"] = p.name;
    v["price"] = p.price;
    v["description"] = p.description;
    v["imageUrl"] = p.imageUrl;
    v["stock"] = p.stock;
    v["category"] = p.category;
    return v;
}

// Shared helper: verifies the request is from a logged-in admin.
// Returns true if allowed to proceed; if false, has already sent the error response.
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

void ProductController::listProducts(const drogon::HttpRequestPtr &req,
                                      std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    auto docs = g_firestoreClient->listDocuments("products");

    Json::Value result(Json::arrayValue);
    for (const auto &doc : docs)
    {
        Product p = Product::fromJson(doc);
        result.append(productToJsonValue(p));
    }

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

void ProductController::getProduct(const drogon::HttpRequestPtr &req,
                                    std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                    std::string id)
{
    auto doc = g_firestoreClient->getDocument("products", id);

    if (doc.empty())
    {
        Json::Value err;
        err["error"] = "Product not found";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k404NotFound);
        callback(resp);
        return;
    }

    doc["id"] = id;
    Product p = Product::fromJson(doc);

    auto resp = drogon::HttpResponse::newHttpJsonResponse(productToJsonValue(p));
    callback(resp);
}

void ProductController::createProduct(const drogon::HttpRequestPtr &req,
                                       std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    if (!requireAdmin(req, callback))
        return;

    auto bodyJson = nlohmann::json::parse(req->getBody(), nullptr, false);
    if (bodyJson.is_discarded())
    {
        Json::Value err;
        err["error"] = "Invalid JSON body";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    Product p = Product::fromJson(bodyJson);
    std::string newId = g_firestoreClient->addDocument("products", p.toFields());

    if (newId.empty())
    {
        Json::Value err;
        err["error"] = "Failed to create product";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
        return;
    }

    Json::Value result;
    result["id"] = newId;
    result["success"] = true;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

void ProductController::updateProduct(const drogon::HttpRequestPtr &req,
                                       std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                       std::string id)
{
    if (!requireAdmin(req, callback))
        return;

    auto bodyJson = nlohmann::json::parse(req->getBody(), nullptr, false);
    if (bodyJson.is_discarded())
    {
        Json::Value err;
        err["error"] = "Invalid JSON body";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    Product p = Product::fromJson(bodyJson);
    bool ok = g_firestoreClient->setDocument("products", id, p.toFields());

    Json::Value result;
    result["success"] = ok;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    if (!ok)
        resp->setStatusCode(drogon::k500InternalServerError);
    callback(resp);
}

void ProductController::deleteProduct(const drogon::HttpRequestPtr &req,
                                       std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                       std::string id)
{
    if (!requireAdmin(req, callback))
        return;

    bool ok = g_firestoreClient->deleteDocument("products", id);

    Json::Value result;
    result["success"] = ok;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    if (!ok)
        resp->setStatusCode(drogon::k500InternalServerError);
    callback(resp);
}
