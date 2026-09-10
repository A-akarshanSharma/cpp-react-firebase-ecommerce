#pragma once
#include <drogon/HttpController.h>

class OrderController : public drogon::HttpController<OrderController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(OrderController::createOrder, "/orders", drogon::Post);
    ADD_METHOD_TO(OrderController::getMyOrders, "/orders", drogon::Get);
    ADD_METHOD_TO(OrderController::getAllOrders, "/admin/orders", drogon::Get);
    ADD_METHOD_TO(OrderController::updateOrderStatus, "/admin/orders/{id}/status", drogon::Put);
    METHOD_LIST_END

    // Turns the caller's current cart into an order. Validates stock, decrements it,
    // snapshots prices, clears the cart. See the NOTE in the .cpp about the
    // known race-condition simplification made here.
    void createOrder(const drogon::HttpRequestPtr &req,
                      std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // Caller's own order history.
    void getMyOrders(const drogon::HttpRequestPtr &req,
                      std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // Admin-only - every order from every user.
    void getAllOrders(const drogon::HttpRequestPtr &req,
                       std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // Admin-only. Body: {"status": "shipped"} etc.
    void updateOrderStatus(const drogon::HttpRequestPtr &req,
                            std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                            std::string id);
};
