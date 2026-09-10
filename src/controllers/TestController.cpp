#include "TestController.h"
#include "../FirestoreClient.h"
#include <nlohmann/json.hpp>

// Set up in main.cpp - simplest way to share one client instance for phase 1.
// Revisit with proper dependency injection once the project grows.
extern FirestoreClient *g_firestoreClient;

void TestController::testWrite(const drogon::HttpRequestPtr &req,
                                std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    nlohmann::json fields = {
        {"message", "hello from drogon"},
        {"count", 1},
        {"active", true}};

    bool ok = g_firestoreClient->setDocument("phase1_test", "hello_doc", fields);

    Json::Value result;
    result["success"] = ok;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

void TestController::testRead(const drogon::HttpRequestPtr &req,
                               std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    auto doc = g_firestoreClient->getDocument("phase1_test", "hello_doc");

    Json::Value result;
    result["found"] = !doc.empty();
    result["data"] = doc.dump();

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}