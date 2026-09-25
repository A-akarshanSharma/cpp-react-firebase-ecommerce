// Test-only HTTP harness: never compiled into phase1_server and no external services.
#include "controllers/ControllerSupport.h"
#include <atomic>
#include <thread>
FirestoreClient *g_firestoreClient = nullptr;
AuthMiddleware *g_authMiddleware = nullptr;
int main(int argc, char **argv)
{
    if (argc != 2)
        return 1;
    std::atomic<int> active{0};
    runtime::WorkerPool pool(2, 2);
    runtime::workers = &pool;
    drogon::app().registerHandler(
        "/slow",
        [&](const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            controller::respond(callback, [&] {
                ++active;
                std::this_thread::sleep_for(std::chrono::milliseconds(700));
                --active;
                return nlohmann::json{{"ok", true}};
            });
        },
        {drogon::Get});
    drogon::app().registerHandler(
        "/health",
        [&](const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            auto reply = drogon::HttpResponse::newHttpResponse();
            reply->setBody(std::to_string(active.load()));
            callback(reply);
        },
        {drogon::Get});
    drogon::app().addListener("127.0.0.1", std::stoi(argv[1])).setThreadNum(1).run();
}
