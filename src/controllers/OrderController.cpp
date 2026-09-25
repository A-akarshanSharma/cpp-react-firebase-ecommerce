#include "OrderController.h"
#include "../services/Ordering.h"
#include "ControllerSupport.h"
using namespace controller;
void OrderController::createOrder(const drogon::HttpRequestPtr &req,
                                  std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    respond(callback, [=] {
        auto user = auth(req);
        return commerce::checkout(*g_firestoreClient, user.uid, req->getHeader("Idempotency-Key"),
                                  body(req, true));
    });
}
void OrderController::getMyOrders(const drogon::HttpRequestPtr &req,
                                  std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    respond(callback, [=] {
        auto user = auth(req);
        auto page = historyPage(req, "orders", "userId", user.uid);
        for (auto &o : page["items"])
            o = validation::orderView(o, o.at("id"));
        return page;
    });
}
void OrderController::getAllOrders(const drogon::HttpRequestPtr &req,
                                   std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    respond(callback, [=] {
        auth(req, true);
        json result = json::array();
        for (const auto &o : g_firestoreClient->listDocuments("orders"))
            result.push_back(validation::orderView(o, o.at("id")));
        return result;
    });
}
void OrderController::updateOrderStatus(const drogon::HttpRequestPtr &req,
                                        std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                        std::string id)
{
    respond(callback, [=] {
        auto user = auth(req, true);
        auto order = commerce::transitionOrder(*g_firestoreClient, id, user.uid, true, body(req));
        return json{{"success", true}, {"order", order}};
    });
}
void OrderController::getOrder(const drogon::HttpRequestPtr &req,
                               std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                               std::string id)
{
    respond(callback, [=] {
        const auto user = auth(req);
        return commerce::orderDetail(*g_firestoreClient, id, user.uid, user.isAdmin);
    });
}
void OrderController::cancelOrder(const drogon::HttpRequestPtr &req,
                                  std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                  std::string id)
{
    respond(callback, [=] {
        const auto user = auth(req);
        auto input = body(req, true);
        input["status"] = "cancelled";
        return commerce::transitionOrder(*g_firestoreClient, id, user.uid, user.isAdmin, input);
    });
}
void OrderController::addAddress(const drogon::HttpRequestPtr &req,
                                 std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                 std::string id)
{
    respond(callback, [=] {
        const auto user = auth(req, true);
        return commerce::setOrderAddress(*g_firestoreClient, id, user.uid, body(req));
    });
}
void OrderController::inventoryHistory(const drogon::HttpRequestPtr &req,
                                       std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    respond(callback, [=] {
        auth(req, true);
        const auto productId = req->getParameter("productId");
        if (!productId.empty())
            validation::id(productId);
        return historyPage(req, "inventoryMovements", productId.empty() ? "" : "productId", productId);
    });
}
