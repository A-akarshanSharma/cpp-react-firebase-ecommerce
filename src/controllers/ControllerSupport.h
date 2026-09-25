#pragma once
#include "../Validation.h"
#include "../runtime/Runtime.h"
#include "AuthMiddleware.h"
#include <charconv>
#include <drogon/drogon.h>
extern FirestoreClient *g_firestoreClient;
extern AuthMiddleware *g_authMiddleware;
namespace controller
{
using json = nlohmann::json;
inline json body(const drogon::HttpRequestPtr &req, bool optional = false)
{
    if (req->getBody().size() > validation::MaxBodyBytes)
        throw ApiError(413, "BODY_TOO_LARGE", "Request body is too large");
    if (optional && req->getBody().empty())
        return json::object();
    auto parsed = json::parse(req->getBody(), nullptr, false);
    validation::require(!parsed.is_discarded() && parsed.is_object(), "Body must be a valid JSON object");
    return parsed;
}
inline AuthResult auth(const drogon::HttpRequestPtr &req, bool admin = false)
{
    auto user = g_authMiddleware->verify(req);
    if (!user.valid)
        throw ApiError(401, "UNAUTHENTICATED", "Please sign in again");
    if (admin && !user.isAdmin)
        throw ApiError(403, "FORBIDDEN", "Admin access required");
    return user;
}
inline json historyPage(const drogon::HttpRequestPtr &req, const std::string &collection,
                        const std::string &field = "", const std::string &value = "")
{
    int limit = 25;
    const auto raw = req->getParameter("limit");
    if (!raw.empty())
    {
        const auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), limit);
        validation::require(parsed.ec == std::errc{} && parsed.ptr == raw.data() + raw.size(),
                            "Invalid page size");
    }
    return g_firestoreClient->historyPage(collection, limit, req->getParameter("cursor"), field, value);
}
template <class Work>
void respond(const std::function<void(const drogon::HttpResponsePtr &)> &callback, Work work)
{
    const auto until = runtime::Clock::now() + std::chrono::seconds(runtime::requestSeconds);
    auto run = [callback, work = std::move(work), until]() mutable {
        runtime::Deadline scope(until);
        int status = 200;
        json result;
        try
        {
            runtime::remaining();
            result = work();
        }
        catch (const ApiError &e)
        {
            status = e.status;
            result = e.details;
            result["error"] = e.what();
            result["code"] = e.code;
        }
        catch (const DatabaseError &e)
        {
            LOG_ERROR << "Database failure status=" << e.status << " code=" << e.code;
            status = 503;
            result = {
                {"error", "The store database could not confirm this request. Please try again safely."},
                {"code", "DATABASE_UNAVAILABLE"}};
        }
        catch (const std::exception &)
        {
            LOG_ERROR << "Unexpected request failure";
            status = 500;
            result = {{"error", "The store could not complete this request"}, {"code", "INTERNAL_ERROR"}};
        }
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setStatusCode(static_cast<drogon::HttpStatusCode>(status));
        response->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        if (result.contains("retryAfter"))
            response->addHeader("Retry-After", std::to_string(result["retryAfter"].get<int>()));
        response->setBody(result.dump());
        callback(response);
    };
    if (!runtime::workers)
    {
        run();
        return;
    }
    if (!runtime::workers->submit(std::move(run)))
    {
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setStatusCode(drogon::k503ServiceUnavailable);
        response->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        response->addHeader("Retry-After", "1");
        response->setBody(R"({"code":"SERVER_BUSY","error":"The store is busy. Please retry shortly."})");
        callback(response);
    }
}
} // namespace controller
