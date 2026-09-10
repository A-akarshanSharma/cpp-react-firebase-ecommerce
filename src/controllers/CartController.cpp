#include "CartController.h"
#include "AuthMiddleware.h"
#include "../FirestoreClient.h"
#include "../models/CartItem.h"
#include <nlohmann/json.hpp>
#include <vector>

extern FirestoreClient *g_firestoreClient;
extern AuthMiddleware *g_authMiddleware;

using json = nlohmann::json;

// Shared helper: verifies the request has a valid (any) logged-in user.
// Returns the uid on success; on failure, has already sent the error response and returns "".
static std::string requireAuth(const drogon::HttpRequestPtr &req,
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
        return "";
    }
    return auth.uid;
}

// Looks up a product's live data (name, price, stock, imageUrl) from Firestore.
// Returns an empty json if the product no longer exists (e.g. admin deleted it).
static json fetchProductInfo(const std::string &productId)
{
    auto doc = g_firestoreClient->getDocument("products", productId);
    return doc; // empty json if not found
}

// Loads the caller's cart as a vector of CartItem (empty vector if no cart exists yet).
static std::vector<CartItem> loadCartItems(const std::string &uid)
{
    std::vector<CartItem> items;
    auto doc = g_firestoreClient->getDocument("carts", uid);
    if (doc.contains("items") && doc["items"].is_array())
        for (const auto &item : doc["items"])
            items.push_back(CartItem::fromJson(item));
    return items;
}
static bool saveCartItems(const std::string &uid, const std::vector<CartItem> &items)
{
    json itemsArray = json::array();
    for (const auto &item : items)
        itemsArray.push_back(item.toJson());

    json fields = {{"items", itemsArray}};
    return g_firestoreClient->setDocument("carts", uid, fields);
}

static Json::Value cartToJsonValue(const std::vector<CartItem> &items)
{
    Json::Value result(Json::arrayValue);
    for (const auto &item : items)
    {
        Json::Value v;
        v["productId"] = item.productId;
        v["quantity"] = item.quantity;
        result.append(v);
    }
    return result;
}

void CartController::getCart(const drogon::HttpRequestPtr &req,
                              std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    std::string uid = requireAuth(req, callback);
    if (uid.empty())
        return;

    auto items = loadCartItems(uid);

    // Enrich each item with live product data rather than trusting whatever
    // was stored when it was added - price/stock may have changed since.
    Json::Value result(Json::arrayValue);
    for (const auto &item : items)
    {
        auto product = fetchProductInfo(item.productId);

        Json::Value v;
        v["productId"] = item.productId;
        v["quantity"] = item.quantity;

        if (product.empty())
        {
            // Product was deleted since being added to the cart.
            v["available"] = false;
            v["name"] = "No longer available";
            v["price"] = 0;
            v["stock"] = 0;
        }
        else
        {
            int stock = product.value("stock", 0);
            v["name"] = product.value("name", "");
            v["price"] = product.value("price", 0.0);
            v["imageUrl"] = product.value("imageUrl", "");
            v["stock"] = stock;
            // Flags the frontend can act on directly - e.g. show "only 2 left, reduce quantity".
            v["available"] = stock >= item.quantity;
        }

        result.append(v);
    }

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

void CartController::addItem(const drogon::HttpRequestPtr &req,
                              std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    std::string uid = requireAuth(req, callback);
    if (uid.empty())
        return;

    auto bodyJson = json::parse(req->getBody(), nullptr, false);
    if (bodyJson.is_discarded() || !bodyJson.contains("productId"))
    {
        Json::Value err;
        err["error"] = "Body must include \"productId\" (and optionally \"quantity\", defaults to 1)";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    std::string productId = bodyJson.at("productId").get<std::string>();
    int quantityToAdd = bodyJson.value("quantity", 1);

    auto product = fetchProductInfo(productId);
    if (product.empty())
    {
        Json::Value err;
        err["error"] = "Product not found";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k404NotFound);
        callback(resp);
        return;
    }
    int availableStock = product.value("stock", 0);

    auto items = loadCartItems(uid);
    bool found = false;
    int existingQuantity = 0;
    for (const auto &item : items)
        if (item.productId == productId)
            existingQuantity = item.quantity;

    int newTotal = existingQuantity + quantityToAdd;
    if (newTotal > availableStock)
    {
        Json::Value err;
        err["error"] = "Not enough stock available";
        err["requested"] = newTotal;
        err["available"] = availableStock;
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k409Conflict);
        callback(resp);
        return;
    }

    for (auto &item : items)
    {
        if (item.productId == productId)
        {
            item.quantity = newTotal;
            found = true;
            break;
        }
    }
    if (!found)
        items.push_back({productId, quantityToAdd});

    bool ok = saveCartItems(uid, items);

    Json::Value result;
    result["success"] = ok;
    result["cart"] = cartToJsonValue(items);
    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    if (!ok)
        resp->setStatusCode(drogon::k500InternalServerError);
    callback(resp);
}

void CartController::updateItem(const drogon::HttpRequestPtr &req,
                                 std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    std::string uid = requireAuth(req, callback);
    if (uid.empty())
        return;

    auto bodyJson = json::parse(req->getBody(), nullptr, false);
    if (bodyJson.is_discarded() || !bodyJson.contains("productId") || !bodyJson.contains("quantity"))
    {
        Json::Value err;
        err["error"] = "Body must include \"productId\" and \"quantity\"";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    std::string productId = bodyJson.at("productId").get<std::string>();
    int newQuantity = bodyJson.at("quantity").get<int>();

    if (newQuantity > 0)
    {
        auto product = fetchProductInfo(productId);
        if (product.empty())
        {
            Json::Value err;
            err["error"] = "Product not found";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
            return;
        }
        int availableStock = product.value("stock", 0);
        if (newQuantity > availableStock)
        {
            Json::Value err;
            err["error"] = "Not enough stock available";
            err["requested"] = newQuantity;
            err["available"] = availableStock;
            auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(drogon::k409Conflict);
            callback(resp);
            return;
        }
    }

    auto items = loadCartItems(uid);
    std::vector<CartItem> updated;
    for (auto &item : items)
    {
        if (item.productId == productId)
        {
            if (newQuantity > 0)
            {
                item.quantity = newQuantity;
                updated.push_back(item);
            }
            // quantity <= 0 means drop it - simply don't add to `updated`
        }
        else
        {
            updated.push_back(item);
        }
    }

    bool ok = saveCartItems(uid, updated);

    Json::Value result;
    result["success"] = ok;
    result["cart"] = cartToJsonValue(updated);
    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    if (!ok)
        resp->setStatusCode(drogon::k500InternalServerError);
    callback(resp);
}

void CartController::removeItem(const drogon::HttpRequestPtr &req,
                                 std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    std::string uid = requireAuth(req, callback);
    if (uid.empty())
        return;

    auto bodyJson = json::parse(req->getBody(), nullptr, false);
    if (bodyJson.is_discarded() || !bodyJson.contains("productId"))
    {
        Json::Value err;
        err["error"] = "Body must include \"productId\"";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    std::string productId = bodyJson.at("productId").get<std::string>();

    auto items = loadCartItems(uid);
    std::vector<CartItem> updated;
    for (const auto &item : items)
        if (item.productId != productId)
            updated.push_back(item);

    bool ok = saveCartItems(uid, updated);

    Json::Value result;
    result["success"] = ok;
    result["cart"] = cartToJsonValue(updated);
    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    if (!ok)
        resp->setStatusCode(drogon::k500InternalServerError);
    callback(resp);
}
