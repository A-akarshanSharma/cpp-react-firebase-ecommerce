#pragma once
#include "../Errors.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace runtime
{
using Clock = std::chrono::steady_clock;
inline thread_local Clock::time_point deadline = Clock::time_point::max();
struct Deadline
{
    Clock::time_point previous;
    explicit Deadline(Clock::time_point until) : previous(deadline)
    {
        deadline = until;
    }
    ~Deadline()
    {
        deadline = previous;
    }
};
inline std::chrono::milliseconds remaining()
{
    if (deadline == Clock::time_point::max())
        return std::chrono::seconds(10);
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    if (left.count() <= 0)
        throw ApiError(503, "REQUEST_TIMEOUT", "Request deadline reached. Check the result before retrying.");
    return std::min(left, std::chrono::milliseconds(10000));
}
inline std::unique_lock<std::timed_mutex> lock(std::timed_mutex &mutex)
{
    std::unique_lock<std::timed_mutex> guard(mutex, std::defer_lock);
    if (!guard.try_lock_for(remaining()))
        throw ApiError(503, "REQUEST_TIMEOUT", "Request deadline reached. Please retry safely.");
    return guard;
}
class WorkerPool
{
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::function<void()>> queue;
    std::vector<std::thread> threads;
    size_t capacity;
    bool stopping = false;

  public:
    WorkerPool(size_t workers, size_t queued) : capacity(queued)
    {
        if (!workers || !queued)
            throw std::invalid_argument("Invalid worker capacity");
        for (size_t i = 0; i < workers; ++i)
            threads.emplace_back([this] {
                for (;;)
                {
                    std::function<void()> work;
                    {
                        std::unique_lock<std::mutex> guard(mutex);
                        ready.wait(guard, [&] { return stopping || !queue.empty(); });
                        if (queue.empty() && stopping)
                            return;
                        work = std::move(queue.front());
                        queue.pop_front();
                    }
                    try
                    {
                        work();
                    }
                    catch (...)
                    { /* Each request reports its own failure. */
                    }
                }
            });
    }
    ~WorkerPool()
    {
        {
            std::lock_guard<std::mutex> guard(mutex);
            stopping = true;
        }
        ready.notify_all();
        for (auto &thread : threads)
            thread.join();
    }
    bool submit(std::function<void()> work)
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (stopping || queue.size() >= capacity)
            return false;
        queue.push_back(std::move(work));
        ready.notify_one();
        return true;
    }
};
class Limits
{
    struct Bucket
    {
        double tokens;
        Clock::time_point updated;
    };
    std::mutex mutex;
    std::map<std::string, Bucket> buckets;
    size_t capacity;
    Clock::time_point nextCleanup{};
    std::function<Clock::time_point()> now;

  public:
    explicit Limits(size_t capacity = 20000, std::function<Clock::time_point()> now = Clock::now)
        : capacity(capacity), now(std::move(now))
    {
    }
    void take(const std::string &key, double burst, double perMinute)
    {
        std::lock_guard<std::mutex> guard(mutex);
        auto time = now();
        auto found = buckets.find(key);
        if (found == buckets.end())
        {
            if (buckets.size() >= capacity)
            {
                if (time >= nextCleanup)
                {
                    nextCleanup = time + std::chrono::minutes(1);
                    for (auto it = buckets.begin(); it != buckets.end();)
                        if (time - it->second.updated > std::chrono::minutes(10))
                            it = buckets.erase(it);
                        else
                            ++it;
                }
                if (buckets.size() >= capacity)
                    throw ApiError(503, "SERVER_BUSY", "The store is busy. Please retry shortly.",
                                   {{"retryAfter", 10}});
            }
            found = buckets.emplace(key, Bucket{burst, time}).first;
        }
        auto &bucket = found->second;
        bucket.tokens =
            std::min(burst, bucket.tokens + std::chrono::duration<double>(time - bucket.updated).count() *
                                                perMinute / 60.0);
        bucket.updated = time;
        if (bucket.tokens < 1)
            throw ApiError(
                429, "RATE_LIMITED", "Too many requests. Please wait before retrying.",
                {{"retryAfter", std::max(1, int(std::ceil((1 - bucket.tokens) * 60 / perMinute)))}});
        bucket.tokens -= 1;
    }
    void account(const std::string &uid, const std::string &method, const std::string &path)
    {
        take("user:" + uid, 60, 120);
        if (method != "GET")
            take("mutation:" + uid, 20, 60);
        if (method == "POST" && path == "/orders")
            take("checkout:" + uid, 3, 10);
        if (method == "POST" && path == "/admin/uploads")
            take("upload:" + uid, 3, 10);
    }
};
inline WorkerPool *workers = nullptr;
inline Limits *limits = nullptr;
inline int requestSeconds = 15;
} // namespace runtime
