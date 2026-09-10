#pragma once
#include <drogon/HttpController.h>

// All routes require a valid Firebase token - operate only on the calling user's own cart.
// No admin check needed here (every user manages their own cart).
class CartController : public drogon::HttpController<CartController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(CartController::getCart, "/cart", drogon::Get);
    ADD_METHOD_TO(CartController::addItem, "/cart/add", drogon::Post);
    ADD_METHOD_TO(CartController::updateItem, "/cart/update", drogon::Post);
    ADD_METHOD_TO(CartController::removeItem, "/cart/remove", drogon::Post);
    METHOD_LIST_END

    void getCart(const drogon::HttpRequestPtr &req,
                 std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // Body: {"productId": "...", "quantity": 1} - adds, or increments if already in cart.
    void addItem(const drogon::HttpRequestPtr &req,
                 std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // Body: {"productId": "...", "quantity": 3} - sets the exact quantity (0 removes it).
    void updateItem(const drogon::HttpRequestPtr &req,
                     std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // Body: {"productId": "..."} - removes the item entirely.
    void removeItem(const drogon::HttpRequestPtr &req,
                     std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};
