#include "OrderController.h"
#include "AuthMiddleware.h"
#include "../FirestoreClient.h"
#include "../models/Order.h"
#include "../models/CartItem.h"
#include <nlohmann/json.hpp>
#include <chrono>

extern FirestoreClient *g_firestoreClient;
extern AuthMiddleware *g_authMiddleware;

using json = nlohmann::json;

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

static bool requireAdmin(const drogon::HttpRequestPtr &req,
                          const std::function<void(const drogon::HttpResponsePtr &)> &callback,
                          std::string &uidOut)
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
    uidOut = auth.uid;
    return true;
}

static Json::Value orderToJsonValue(const Order &o)
{
    Json::Value v;
    v["id"] = o.id;
    v["userId"] = o.userId;
    v["total"] = o.total;
    v["status"] = o.status;
    v["createdAt"] = static_cast<Json::Int64>(o.createdAt);

    Json::Value items(Json::arrayValue);
    for (const auto &item : o.items)
    {
        Json::Value iv;
        iv["productId"] = item.productId;
        iv["name"] = item.name;
        iv["price"] = item.price;
        iv["quantity"] = item.quantity;
        items.append(iv);
    }
    v["items"] = items;
    return v;
}

void OrderController::createOrder(const drogon::HttpRequestPtr &req,
                                   std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    std::string uid = requireAuth(req, callback);
    if (uid.empty())
        return;

    // Load the caller's cart.
    auto cartDoc = g_firestoreClient->getDocument("carts", uid);
    std::vector<CartItem> cartItems;
    if (cartDoc.contains("items") && cartDoc["items"].is_array())
        for (const auto &item : cartDoc["items"])
            cartItems.push_back(CartItem::fromJson(item));

    if (cartItems.empty())
    {
        Json::Value err;
        err["error"] = "Cart is empty";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    // NOTE - known simplification, not a fully-safe production pattern:
    // this checks stock, then later writes new stock, as two separate steps.
    // Between the check and the write, another request could also pass its own check
    // for the same product, and both could then decrement - a race condition.
    // At this project's traffic scale, that's an accepted, low-probability risk for now.
    // A production-hardened version would use Firestore's transaction API
    // (beginTransaction/commit) to make check+decrement one atomic operation.
    // Revisit this before running any high-traffic promotion or flash sale.

    // Step 1: validate stock for every item BEFORE writing anything.
    std::vector<OrderItem> orderItems;
    double total = 0.0;
    for (const auto &cartItem : cartItems)
    {
        auto product = g_firestoreClient->getDocument("products", cartItem.productId);
        if (product.empty())
        {
            Json::Value err;
            err["error"] = "Product no longer available: " + cartItem.productId;
            auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(drogon::k409Conflict);
            callback(resp);
            return;
        }

        int stock = product.value("stock", 0);
        if (cartItem.quantity > stock)
        {
            Json::Value err;
            err["error"] = "Not enough stock for " + product.value("name", cartItem.productId);
            err["available"] = stock;
            err["requested"] = cartItem.quantity;
            auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(drogon::k409Conflict);
            callback(resp);
            return;
        }

        OrderItem oi;
        oi.productId = cartItem.productId;
        oi.name = product.value("name", "");
        oi.price = product.value("price", 0.0); // snapshot - order total won't change if price changes later
        oi.quantity = cartItem.quantity;
        orderItems.push_back(oi);
        total += oi.price * oi.quantity;
    }

    // Step 2: all validated - now decrement stock for real.
    for (const auto &cartItem : cartItems)
    {
        auto product = g_firestoreClient->getDocument("products", cartItem.productId);
        int currentStock = product.value("stock", 0);
        json updatedFields = product; // keep other fields (name, price, etc) unchanged
        updatedFields["stock"] = currentStock - cartItem.quantity;
        g_firestoreClient->setDocument("products", cartItem.productId, updatedFields);
    }

    // Step 3: create the order record.
    Order order;
    order.userId = uid;
    order.items = orderItems;
    order.total = total;
    order.status = "pending"; // will become "paid" once Phase 3 payment integration is wired in
    order.createdAt = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

    std::string orderId = g_firestoreClient->addDocument("orders", order.toFields());
    if (orderId.empty())
    {
        Json::Value err;
        err["error"] = "Failed to create order (stock was already decremented - check Firestore manually)";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
        return;
    }

    // Step 4: clear the cart now that it's been turned into an order.
    json emptyCart = {{"items", json::array()}};
    g_firestoreClient->setDocument("carts", uid, emptyCart);

    order.id = orderId;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(orderToJsonValue(order));
    callback(resp);
}

void OrderController::getMyOrders(const drogon::HttpRequestPtr &req,
                                   std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    std::string uid = requireAuth(req, callback);
    if (uid.empty())
        return;

    auto allOrders = g_firestoreClient->listDocuments("orders"); // nlohmann::json array

    Json::Value result(Json::arrayValue);
    for (const auto &doc : allOrders)
    {
        if (doc.value("userId", "") == uid)
        {
            Order o = Order::fromJson(doc);
            result.append(orderToJsonValue(o));
        }
    }

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

void OrderController::getAllOrders(const drogon::HttpRequestPtr &req,
                                    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    std::string adminUid;
    if (!requireAdmin(req, callback, adminUid))
        return;

    auto allOrders = g_firestoreClient->listDocuments("orders");

    Json::Value result(Json::arrayValue);
    for (const auto &doc : allOrders)
    {
        Order o = Order::fromJson(doc);
        result.append(orderToJsonValue(o));
    }

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

void OrderController::updateOrderStatus(const drogon::HttpRequestPtr &req,
                                         std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                         std::string id)
{
    std::string adminUid;
    if (!requireAdmin(req, callback, adminUid))
        return;

    auto bodyJson = json::parse(req->getBody(), nullptr, false);
    if (bodyJson.is_discarded() || !bodyJson.contains("status"))
    {
        Json::Value err;
        err["error"] = "Body must include \"status\"";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    // Read the existing order so we only change status, not overwrite items/total/userId.
    auto existing = g_firestoreClient->getDocument("orders", id);
    if (existing.empty())
    {
        Json::Value err;
        err["error"] = "Order not found";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k404NotFound);
        callback(resp);
        return;
    }

    existing["status"] = bodyJson.at("status").get<std::string>();
    bool ok = g_firestoreClient->setDocument("orders", id, existing);

    Json::Value result;
    result["success"] = ok;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    if (!ok)
        resp->setStatusCode(drogon::k500InternalServerError);
    callback(resp);
}
