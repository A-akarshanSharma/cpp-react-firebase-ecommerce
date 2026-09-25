#pragma once
#include <drogon/HttpController.h>

class TestController : public drogon::HttpController<TestController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(TestController::testWrite, "/test-write", drogon::Get);
    ADD_METHOD_TO(TestController::testRead, "/test-read", drogon::Get);
    METHOD_LIST_END
 
    void testWrite(const drogon::HttpRequestPtr &req,
                    std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    void testRead(const drogon::HttpRequestPtr &req,
                   std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};
