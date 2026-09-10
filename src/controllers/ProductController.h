#pragma once
#include <drogon/HttpController.h>

class ProductController : public drogon::HttpController<ProductController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ProductController::listProducts, "/products", drogon::Get);
    ADD_METHOD_TO(ProductController::getProduct, "/products/{id}", drogon::Get);
    ADD_METHOD_TO(ProductController::createProduct, "/products", drogon::Post);
    ADD_METHOD_TO(ProductController::updateProduct, "/products/{id}", drogon::Put);
    ADD_METHOD_TO(ProductController::deleteProduct, "/products/{id}", drogon::Delete);
    METHOD_LIST_END

    // Public - anyone can browse products.
    void listProducts(const drogon::HttpRequestPtr &req,
                       std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    void getProduct(const drogon::HttpRequestPtr &req,
                     std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                     std::string id);

    // Admin-only from here down.
    void createProduct(const drogon::HttpRequestPtr &req,
                        std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    void updateProduct(const drogon::HttpRequestPtr &req,
                        std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        std::string id);

    void deleteProduct(const drogon::HttpRequestPtr &req,
                        std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        std::string id);
};
