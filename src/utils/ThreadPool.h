#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(size_t threads);
    ~ThreadPool();

    // Non-copyable, non-movable (manages threads)
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template<class F, class... Args>
    // NOLINTNEXTLINE(readability-identifier-naming) - public API method name enqueue
    auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop = false;
};

template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    using ReturnType = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<ReturnType> res = task->get_future();
    {
        const std::unique_lock lock(queue_mutex);

        if (stop) {
            throw std::runtime_error("enqueue on stopped ThreadPool");  // NOSONAR(cpp:S112) - std::runtime_error is appropriate for invalid operation errors
        }

        tasks.emplace([task](){ (*task)(); });
    }
    condition.notify_one();
    return res;
}
