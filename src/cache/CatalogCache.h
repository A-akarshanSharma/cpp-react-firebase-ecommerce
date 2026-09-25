#pragma once
#include "../runtime/Runtime.h"
#include <nlohmann/json.hpp>

// One public-catalog snapshot per database client. Never used for checkout reads.
class CatalogCache
{
    using Json = nlohmann::json;
    std::mutex mutex;
    std::condition_variable ready;
    Json value;
    runtime::Clock::time_point expires{};
    uint64_t generation = 0;
    bool loading = false;
    std::function<runtime::Clock::time_point()> now;

  public:
    explicit CatalogCache(std::function<runtime::Clock::time_point()> clock = runtime::Clock::now)
        : now(std::move(clock))
    {
    }
    void invalidate()
    {
        std::lock_guard<std::mutex> guard(mutex);
        ++generation;
        value = nullptr;
    }
    Json get(const std::function<Json()> &load)
    {
        std::unique_lock<std::mutex> guard(mutex);
        while (loading)
            if (!ready.wait_for(guard, runtime::remaining(), [&] { return !loading; }))
                throw ApiError(503, "REQUEST_TIMEOUT", "Catalog refresh timed out. Please retry.");
        runtime::remaining();
        if (!value.is_null() && now() < expires)
            return value;
        loading = true;
        const auto version = generation;
        guard.unlock();
        try
        {
            auto result = load();
            // Keep retained data bounded; an oversized catalog still works uncached.
            const bool fits = result.dump().size() <= 4 * 1024 * 1024;
            guard.lock();
            if (generation == version && fits)
            {
                value = result;
                expires = now() + std::chrono::seconds(30);
            }
            loading = false;
            guard.unlock();
            ready.notify_all();
            return result;
        }
        catch (...)
        {
            if (!guard.owns_lock())
                guard.lock();
            loading = false;
            guard.unlock();
            ready.notify_all();
            throw;
        }
    }
};
