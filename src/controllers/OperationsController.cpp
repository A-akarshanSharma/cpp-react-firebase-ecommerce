#include "OperationsController.h"
#include "../services/Uploads.h"
#include "ControllerSupport.h"
#include <filesystem>
extern std::string g_uploadDirectory;
using namespace controller;
void OperationsController::shipping(const Request &, Callback &&callback)
{
    respond(callback, [=] {
        auto settings = commerce::shippingSettings(*g_firestoreClient);
        json result = json::array();
        for (const auto &method : settings.at("methods"))
            if (method.at("active").get<bool>())
                result.push_back(method);
        return result;
    });
}
void OperationsController::adminShipping(const Request &req, Callback &&callback)
{
    respond(callback, [=] {
        auth(req, true);
        return commerce::shippingSettings(*g_firestoreClient);
    });
}
void OperationsController::updateShipping(const Request &req, Callback &&callback)
{
    respond(callback, [=] {
        auto user = auth(req, true);
        return commerce::saveShipping(*g_firestoreClient, user.uid, body(req));
    });
}
void OperationsController::shipment(const Request &req, Callback &&callback, std::string id)
{
    respond(callback, [=] {
        auto user = auth(req, true);
        return commerce::saveShipment(*g_firestoreClient, id, user.uid, body(req));
    });
}
void OperationsController::notifications(const Request &req, Callback &&callback)
{
    respond(callback, [=] {
        auto user = auth(req);
        return historyPage(req, "notifications", "userId", user.uid);
    });
}
void OperationsController::readNotification(const Request &req, Callback &&callback, std::string id)
{
    respond(callback, [=] {
        auto user = auth(req);
        return commerce::markNotificationRead(*g_firestoreClient, id, user.uid);
    });
}
void OperationsController::audits(const Request &req, Callback &&callback)
{
    respond(callback, [=] {
        auth(req, true);
        return historyPage(req, "auditLogs");
    });
}
void OperationsController::getVariants(const Request &, Callback &&callback, std::string id)
{
    respond(callback, [=] { return commerce::variants(*g_firestoreClient, id); });
}
void OperationsController::newVariant(const Request &req, Callback &&callback, std::string id)
{
    respond(callback, [=] {
        auto user = auth(req, true);
        return commerce::createVariant(*g_firestoreClient, id, user.uid, body(req));
    });
}
void OperationsController::upload(const Request &req, Callback &&callback)
{
    respond(callback, [=] {
        auto user = auth(req, true);
        return commerce::uploadImage(*g_firestoreClient, user.uid, std::string(req->getBody()),
                                     g_uploadDirectory);
    });
}
void OperationsController::media(const Request &req, Callback &&callback, std::string name)
{
    try
    {
        const auto path = commerce::mediaFile(g_uploadDirectory, name);
        if (!std::filesystem::is_regular_file(path))
            throw ApiError(404, "IMAGE_NOT_FOUND", "Image not found");
        auto response = drogon::HttpResponse::newFileResponse(path, "", drogon::CT_IMAGE_PNG, "", req);
        response->addHeader("X-Content-Type-Options", "nosniff");
        response->addHeader("Cache-Control", "public, max-age=31536000, immutable");
        callback(response);
    }
    catch (...)
    {
        respond(callback, []() -> json { throw ApiError(404, "IMAGE_NOT_FOUND", "Image not found"); });
    }
}
