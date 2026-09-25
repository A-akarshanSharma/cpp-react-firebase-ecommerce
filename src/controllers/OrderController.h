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
    ADD_METHOD_TO(OrderController::getOrder, "/orders/{id}", drogon::Get);
    ADD_METHOD_TO(OrderController::cancelOrder, "/orders/{id}/cancel", drogon::Post);
    ADD_METHOD_TO(OrderController::addAddress, "/admin/orders/{id}/address", drogon::Put);
    ADD_METHOD_TO(OrderController::inventoryHistory, "/admin/inventory", drogon::Get);
    METHOD_LIST_END

    // Atomic checkout with immutable snapshots and inventory history.
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
                           std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string id);
    void getOrder(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr &)> &&,
                  std::string);
    void cancelOrder(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr &)> &&,
                     std::string);
    void addAddress(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr &)> &&,
                    std::string);
    void inventoryHistory(const drogon::HttpRequestPtr &,
                          std::function<void(const drogon::HttpResponsePtr &)> &&);
};
