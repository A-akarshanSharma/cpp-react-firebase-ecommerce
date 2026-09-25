#pragma once
#include <drogon/HttpController.h>
class OperationsController : public drogon::HttpController<OperationsController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(OperationsController::shipping, "/shipping-methods", drogon::Get);
    ADD_METHOD_TO(OperationsController::adminShipping, "/admin/shipping", drogon::Get);
    ADD_METHOD_TO(OperationsController::updateShipping, "/admin/shipping", drogon::Put);
    ADD_METHOD_TO(OperationsController::shipment, "/admin/orders/{id}/shipment", drogon::Put);
    ADD_METHOD_TO(OperationsController::notifications, "/notifications", drogon::Get);
    ADD_METHOD_TO(OperationsController::readNotification, "/notifications/{id}/read", drogon::Post);
    ADD_METHOD_TO(OperationsController::audits, "/admin/audit-logs", drogon::Get);
    ADD_METHOD_TO(OperationsController::getVariants, "/products/{id}/variants", drogon::Get);
    ADD_METHOD_TO(OperationsController::newVariant, "/admin/products/{id}/variants", drogon::Post);
    ADD_METHOD_TO(OperationsController::upload, "/admin/uploads", drogon::Post);
    ADD_METHOD_TO(OperationsController::media, "/media/{name}", drogon::Get);
    METHOD_LIST_END
    using Request = drogon::HttpRequestPtr;
    using Callback = std::function<void(const drogon::HttpResponsePtr &)>;
    void shipping(const Request &, Callback &&);
    void adminShipping(const Request &, Callback &&);
    void updateShipping(const Request &, Callback &&);
    void shipment(const Request &, Callback &&, std::string);
    void notifications(const Request &, Callback &&);
    void readNotification(const Request &, Callback &&, std::string);
    void audits(const Request &, Callback &&);
    void getVariants(const Request &, Callback &&, std::string);
    void newVariant(const Request &, Callback &&, std::string);
    void upload(const Request &, Callback &&);
    void media(const Request &, Callback &&, std::string);
};
