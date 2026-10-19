#pragma once

#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(size_t threads = 0, std::string_view name_prefix = "ThreadPool");
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

    // Convenience PascalCase alias matching Enqueue() API used by search callers
    template<class F, class... Args>
    auto Enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
      return enqueue(std::forward<F>(f), std::forward<Args>(args)...);
    }

    // Fire-and-forget task submission without future overhead.
    // Use when completion is tracked externally (e.g. via atomic countdown counter).
    // NOLINTNEXTLINE(readability-identifier-naming) - public API method name submit
    void submit(std::function<void()> f);

    [[nodiscard]] size_t GetThreadCount() const noexcept { return workers_.size(); }

private:
    void WorkerLoop(size_t worker_index, const std::string& name_prefix);

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

    {
        const std::scoped_lock lock(queue_mutex_);
        if (stop_) {
            std::promise<ReturnType> rejected;
            rejected.set_exception(std::make_exception_ptr(
                std::runtime_error("ThreadPool: task rejected during shutdown")));
            return rejected.get_future();
        }
    }

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
        const std::scoped_lock lock(queue_mutex_);

        if (stop_) {
            std::promise<ReturnType> rejected;
            rejected.set_exception(std::make_exception_ptr(
                std::runtime_error("ThreadPool: task rejected during shutdown")));
            return rejected.get_future();
        }

        tasks_.emplace([task]{ (*task)(); });
    }
    condition_.notify_one();
    return res;
}
