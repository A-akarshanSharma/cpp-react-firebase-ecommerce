#include "UserController.h"
#include "../services/Operations.h"
#include "ControllerSupport.h"
using namespace controller;
void UserController::listUsers(const drogon::HttpRequestPtr &req,
                               std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    respond(callback, [=] {
        auth(req, true);
        json result = json::array();
        for (const auto &u : g_firestoreClient->listDocuments("users"))
            result.push_back({{"uid", u.at("id")},
                              {"email", u.value("email", "")},
                              {"role", u.value("role", "customer")}});
        return result;
    });
}
void UserController::setUserRole(const drogon::HttpRequestPtr &req,
                                 std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                 std::string uid)
{
    respond(callback, [=] {
        auto user = auth(req, true);
        return commerce::changeRole(*g_firestoreClient, uid, user.uid, body(req));
    });
}
void UserController::me(const drogon::HttpRequestPtr &req,
                        std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    respond(callback, [=] {
        auto user = auth(req);
        return json{{"uid", user.uid}, {"email", user.email}, {"isAdmin", user.isAdmin}};
    });
}
