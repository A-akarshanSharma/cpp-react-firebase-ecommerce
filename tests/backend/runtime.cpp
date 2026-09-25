#include "controllers/ControllerSupport.h"
#include <atomic>
#include <future>
#include <iostream>
FirestoreClient *g_firestoreClient = nullptr;
AuthMiddleware *g_authMiddleware = nullptr;
void check(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejected(F action, int status)
{
    try
    {
        action();
    }
    catch (const ApiError &e)
    {
        check(e.status == status, "Wrong limit status");
        return;
    }
    throw std::runtime_error("Expected rejection");
}
int main()
{
    try
    {
        auto now = runtime::Clock::now();
        runtime::Limits limits(2, [&] { return now; });
        limits.take("a", 2, 60);
        limits.take("a", 2, 60);
        rejected([&] { limits.take("a", 2, 60); }, 429);
        limits.take("b", 2, 60);
        rejected([&] { limits.take("c", 2, 60); }, 503);
        now += std::chrono::seconds(1);
        limits.take("a", 2, 60);
        now += std::chrono::minutes(11);
        limits.take("c", 2, 60);
        std::cout << "PASS token refill, key isolation, bounded storage and idle cleanup\n";
        runtime::Limits concurrent;
        std::atomic<int> accepted{0};
        std::vector<std::thread> clients;
        for (int i = 0; i < 100; ++i)
            clients.emplace_back([&] {
                try
                {
                    concurrent.take("shared", 10, .001);
                    ++accepted;
                }
                catch (const ApiError &)
                {
                }
            });
        for (auto &client : clients)
            client.join();
        check(accepted == 10, "Concurrent limits overspent tokens");
        std::cout << "PASS concurrent admission is atomic\n";
        runtime::Limits account;
        for (int i = 0; i < 3; ++i)
            account.account("a", "POST", "/orders");
        rejected([&] { account.account("a", "POST", "/orders"); }, 429);
        account.account("b", "POST", "/orders");
        account.account("a", "GET", "/orders");
        for (int i = 0; i < 3; ++i)
            account.account("a", "POST", "/admin/uploads");
        rejected([&] { account.account("a", "POST", "/admin/uploads"); }, 429);
        std::cout << "PASS checkout/upload limits preserve other users and reads\n";
        {
            runtime::Deadline scope(runtime::Clock::now() - std::chrono::seconds(1));
            rejected([] { runtime::remaining(); }, 503);
        }
        check(runtime::remaining().count() > 0, "Deadline leaked across jobs");
        std::cout << "PASS deadline exhaustion and restoration\n";
        std::promise<void> release, started;
        auto gate = release.get_future().share();
        std::atomic<int> jobs{0};
        {
            runtime::WorkerPool pool(1, 2);
            check(pool.submit([&] {
                started.set_value();
                gate.wait();
                ++jobs;
            }),
                  "First job rejected");
            started.get_future().wait();
            check(pool.submit([&] { ++jobs; }) && pool.submit([&] { ++jobs; }), "Queue capacity incorrect");
            check(!pool.submit([&] { ++jobs; }), "Queue unbounded");
            release.set_value();
        }
        check(jobs == 3, "Worker shutdown lost accepted jobs");
        std::cout << "PASS bounded queue, worker saturation and graceful drain\n";
        runtime::requestSeconds = 0;
        bool ran = false;
        drogon::HttpResponsePtr reply;
        controller::respond([&](const auto &r) { reply = r; },
                            [&] {
                                ran = true;
                                return nlohmann::json::object();
                            });
        check(!ran && reply->getStatusCode() == 503, "Expired request executed work");
        runtime::requestSeconds = 15;
        std::cout << "PASS expired requests cannot start business work\n6 runtime scenarios passed\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
