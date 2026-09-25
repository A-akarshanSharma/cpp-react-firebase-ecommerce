#include "ProductController.h"
#include "../services/Operations.h"
#include "ControllerSupport.h"
using namespace controller;
void ProductController::adjustStock(const drogon::HttpRequestPtr &req,
                                    std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                    std::string id)
{
    respond(callback, [=] {
        auto user = auth(req, true);
        return commerce::adjustStock(*g_firestoreClient, id, body(req), user.uid);
    });
}
void ProductController::listProducts(const drogon::HttpRequestPtr &,
                                     std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    respond(callback, [] { return commerce::catalogView(g_firestoreClient->catalogProducts()); });
}
void ProductController::getProduct(const drogon::HttpRequestPtr &,
                                   std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                   std::string id)
{
    respond(callback, [=] {
        validation::id(id);
        auto p = g_firestoreClient->getDocument("products", id);
        if (!commerce::sellable(*g_firestoreClient, p))
            throw ApiError(404, "PRODUCT_NOT_FOUND", "Product not found");
        return validation::productView(p, id);
    });
}
void ProductController::createProduct(const drogon::HttpRequestPtr &req,
                                      std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    respond(callback, [=] {
        auto user = auth(req, true);
        return commerce::saveProduct(*g_firestoreClient, "", body(req), true, user.uid);
    });
}
void ProductController::updateProduct(const drogon::HttpRequestPtr &req,
                                      std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                      std::string id)
{
    respond(callback, [=] {
        auto user = auth(req, true);
        return commerce::saveProduct(*g_firestoreClient, id, body(req), false, user.uid);
    });
}
void ProductController::deleteProduct(const drogon::HttpRequestPtr &req,
                                      std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                      std::string id)
{
    respond(callback, [=] {
        auto user = auth(req, true);
        validation::id(id);
        return commerce::archiveProduct(*g_firestoreClient, id, user.uid);
    });
}
