#include "FirestoreClient.h"
#include <atomic>
#include <future>
#include <iostream>
using json = nlohmann::json;
void check(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
int main()
{
    try
    {
        auto now = runtime::Clock::now();
        CatalogCache cache([&] { return now; });
        int loads = 0;
        auto load = [&] { return json::array({++loads}); };
        check(cache.get(load) == cache.get(load) && loads == 1, "Cache did not reuse snapshot");
        now += std::chrono::seconds(30);
        check(cache.get(load)[0] == 2, "TTL did not refresh");
        cache.invalidate();
        check(cache.get(load)[0] == 3, "Invalidation did not refresh");
        std::cout << "PASS cache: reuse, expiry and invalidation\n";

        CatalogCache concurrent;
        std::promise<void> started, release;
        auto gate = release.get_future().share();
        std::atomic<int> count{0};
        auto slow = [&] {
            if (++count == 1)
                started.set_value();
            gate.wait();
            return json::array({1});
        };
        auto first = std::async(std::launch::async, [&] { return concurrent.get(slow); });
        started.get_future().wait();
        std::vector<std::future<json>> others;
        for (int i = 0; i < 6; ++i)
            others.push_back(std::async(std::launch::async, [&] { return concurrent.get(slow); }));
        bool timedOut = false;
        try
        {
            runtime::Deadline deadline(runtime::Clock::now() - std::chrono::seconds(1));
            concurrent.get(slow);
        }
        catch (const ApiError &e)
        {
            timedOut = e.code == "REQUEST_TIMEOUT";
        }
        release.set_value();
        first.get();
        for (auto &f : others)
            f.get();
        check(count == 1 && timedOut, "Concurrent refresh/deadline guard failed");
        std::cout << "PASS cache: shared refresh and bounded waiting\n";

        CatalogCache racing;
        std::promise<void> reading, finish;
        auto finishGate = finish.get_future().share();
        auto old = std::async(std::launch::async, [&] {
            return racing.get([&] {
                reading.set_value();
                finishGate.wait();
                return json::array({"old"});
            });
        });
        reading.get_future().wait();
        racing.invalidate();
        finish.set_value();
        old.get();
        check(racing.get([] { return json::array({"new"}); })[0] == "new",
              "Old read repopulated invalidated cache");
        racing.invalidate();
        bool failed = false;
        try
        {
            racing.get([]() -> json { throw std::runtime_error("upstream"); });
        }
        catch (...)
        {
            failed = true;
        }
        check(failed && racing.get([] { return json::array({"recovered"}); })[0] == "recovered",
              "Refresh failure poisoned cache");
        std::cout << "PASS cache: invalidation race and error recovery\n";

        CatalogCache large;
        int largeLoads = 0;
        auto oversized = [&] {
            ++largeLoads;
            return json::array({std::string(4 * 1024 * 1024, 'x')});
        };
        large.get(oversized);
        large.get(oversized);
        check(largeLoads == 2, "Oversized result retained");
        std::cout << "PASS cache: retained size bound\n";

        int reads = 0, live = 0;
        bool failCommit = false;
        const std::string root = "projects/test/databases/(default)/documents/";
        FirestoreClient db("test", [&](const std::string &method, const std::string &path, const json &) {
            if (method == "GET" && path.find("?pageSize") != std::string::npos)
            {
                ++reads;
                return FirestoreClient::Response{
                    200,
                    {{"documents",
                      json::array({{{"name", root + "products/p"},
                                    {"fields", FirestoreClient::toFields({{"stock", live}})}}})}}};
            }
            if (method == "GET")
                return FirestoreClient::Response{200,
                                                 {{"name", root + "products/p"},
                                                  {"fields", FirestoreClient::toFields({{"stock", live}})}}};
            if (path.find(":beginTransaction") != std::string::npos)
                return FirestoreClient::Response{200, {{"transaction", "t"}}};
            if (path.find(":commit") != std::string::npos)
            {
                if (failCommit)
                    return FirestoreClient::Response{503, {{"error", {{"status", "UNAVAILABLE"}}}}};
                return FirestoreClient::Response{200, {{"writeResults", json::array({json::object()})}}};
            }
            return FirestoreClient::Response{200, json::object()};
        });
        db.catalogProducts();
        db.catalogProducts();
        check(reads == 1, "Catalog endpoint not cached");
        live = 1;
        check(db.getDocument("products", "p")["stock"] == 1 && db.catalogProducts()[0]["stock"] == 0,
              "Live reads used cache");
        db.setDocument("users", "u", {{"role", "customer"}});
        db.catalogProducts();
        check(reads == 1, "Unrelated write invalidated catalog");
        db.setDocument("products", "p", {{"stock", 1}});
        db.catalogProducts();
        check(reads == 2, "Product write failed to invalidate");
        db.transact([](const auto &, auto &writes) -> json {
            writes.push_back({"products", "p", {{"stock", 2}}, {"stock"}});
            return true;
        });
        db.catalogProducts();
        check(reads == 3, "Transactional inventory write failed to invalidate");
        failCommit = true;
        try
        {
            db.setDocument("products", "p", {{"stock", 3}});
        }
        catch (const DatabaseError &)
        {
        }
        db.catalogProducts();
        check(reads == 4, "Uncertain commit retained cached snapshot");
        db.deleteDocument("products", "p");
        db.catalogProducts();
        check(reads == 5, "Deletion failed to invalidate");
        std::cout
            << "PASS cache: write integration and live-read isolation\n5 catalog cache scenarios passed\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
