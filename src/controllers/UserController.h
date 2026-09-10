#pragma once
#include <drogon/HttpController.h>

// All routes here are admin-only, except /me.
class UserController : public drogon::HttpController<UserController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(UserController::listUsers, "/admin/users", drogon::Get);
    ADD_METHOD_TO(UserController::setUserRole, "/admin/users/{uid}/role", drogon::Put);
    ADD_METHOD_TO(UserController::me, "/me", drogon::Get);
    METHOD_LIST_END

    void listUsers(const drogon::HttpRequestPtr &req,
                    std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // Body: {"role": "admin"} or {"role": "customer"}
    void setUserRole(const drogon::HttpRequestPtr &req,
                      std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                      std::string uid);

    // Any logged-in user - returns their own uid/email/isAdmin.
    // This is how the frontend knows whether to show the admin panel.
    void me(const drogon::HttpRequestPtr &req,
            std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};
