#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <tuple>
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

    // Fire-and-forget task submission without future overhead.
    // Use when completion is tracked externally (e.g. via atomic countdown counter).
    // NOLINTNEXTLINE(readability-identifier-naming) - public API method name submit
    void submit(std::function<void()> f) {
        {
            const std::unique_lock lock(queue_mutex_);
            if (stop_) {
                throw std::runtime_error("submit on stopped ThreadPool");  // NOSONAR(cpp:S112) - std::runtime_error is appropriate for invalid operation errors
            }
            tasks_.emplace(std::move(f));
        }
        condition_.notify_one();
    }

private:
    void WorkerLoop(size_t worker_index);

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;

    std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_ = false;
};

template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    using ReturnType = std::invoke_result_t<F, Args...>;

    // Bind arguments eagerly into a tuple and std::apply on the worker.
    // Replaces std::bind (extra indirection, deprecated style) while keeping
    // perfect forwarding for move-only callables.
    auto bound_call = std::make_tuple(std::forward<F>(f), std::forward<Args>(args)...);
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        // NOLINTNEXTLINE(readability-redundant-lambda-parameter-list) - C++17 requires () with mutable; paren-less mutable lambdas are C++23
        [bound_call = std::move(bound_call)]() mutable -> ReturnType {
          return std::apply(
              [](auto&& bound_fn, auto&&... unpacked_args) -> ReturnType {
                return std::invoke(
                    std::forward<decltype(bound_fn)>(bound_fn),
                    std::forward<decltype(unpacked_args)>(unpacked_args)...);
              },
              std::move(bound_call));
        });

    std::future<ReturnType> res = task->get_future();
    {
        const std::unique_lock lock(queue_mutex_);

        if (stop_) {
            throw std::runtime_error("enqueue on stopped ThreadPool");  // NOSONAR(cpp:S112) - std::runtime_error is appropriate for invalid operation errors
        }

        tasks_.emplace([task]{ (*task)(); });
    }
    condition_.notify_one();
    return res;
}
