#include "CartController.h"
#include "../services/Commerce.h"
#include "ControllerSupport.h"
using namespace controller;
void CartController::getCart(const drogon::HttpRequestPtr &req,
                             std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    respond(callback, [=] {
        auto user = auth(req);
        return commerce::getCart(*g_firestoreClient, user.uid);
    });
}
void CartController::addItem(const drogon::HttpRequestPtr &req,
                             std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    respond(callback, [=] {
        auto user = auth(req);
        return commerce::mutateCart(*g_firestoreClient, user.uid, "add", body(req));
    });
}
void CartController::updateItem(const drogon::HttpRequestPtr &req,
                                std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    respond(callback, [=] {
        auto user = auth(req);
        return commerce::mutateCart(*g_firestoreClient, user.uid, "update", body(req));
    });
}
void CartController::removeItem(const drogon::HttpRequestPtr &req,
                                std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    respond(callback, [=] {
        auto user = auth(req);
        return commerce::mutateCart(*g_firestoreClient, user.uid, "remove", body(req));
    });
}
