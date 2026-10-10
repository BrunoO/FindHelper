/**
 * @file ThreadPool.cpp
 * @brief Implementation of a general-purpose thread pool for async task execution
 *
 * This file implements the ThreadPool class, which provides a reusable pool of
 * worker threads for executing asynchronous tasks. The thread pool eliminates
 * the overhead of creating and destroying threads for each task by maintaining
 * a fixed set of worker threads that process tasks from a queue.
 *
 * ARCHITECTURE:
 * - Worker threads are created at construction time and run until destruction
 * - Tasks are enqueued via enqueue() and executed by available workers
 * - Thread-safe: Multiple threads can enqueue tasks concurrently
 * - Graceful shutdown: Destructor waits for all tasks to complete
 *
 * USAGE:
 * - Owned by Application class for general-purpose async operations
 * - Primary use: Pre-fetching file attributes (size, modification time) during
 *   UI sorting operations (see SearchResultUtils.cpp)
 * - Thread count: Uses settings.searchThreadPoolSize or hardware_concurrency()
 *
 * THREAD NAMING:
 * - Worker threads are named "ThreadPool-{i}" for profiling/debugging
 * - Names are visible in profilers (Instruments, Visual Studio Profiler, etc.)
 *
 * PERFORMANCE:
 * - Reduces thread creation overhead (1-10ms per thread)
 * - Enables efficient parallel execution of I/O-bound tasks
 * - Task execution happens outside locks to minimize contention
 *
 * @see ThreadPool.h for class interface
 * @see SearchThreadPool.h for search-specific thread pool
 * @see Application.cpp for ThreadPool ownership and usage
 * @see SearchResultUtils.cpp for attribute loading usage
 */

#include "utils/Logger.h"
#include "utils/ThreadPool.h"
#include "utils/ThreadUtils.h"
#include <exception>
#include <string>

ThreadPool::ThreadPool(size_t threads)  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - workers_, queue_mutex_, condition_ are std types with default constructors that initialize themselves
{
    for (size_t i = 0; i < threads; ++i) {
        workers_.emplace_back([this, i] {
            WorkerLoop(i);
        });
    }
}

void ThreadPool::WorkerLoop(size_t worker_index)
{
    // Set thread name for profiling/debugging
    const std::string thread_name = "ThreadPool-" + std::to_string(worker_index);
    SetThreadName(thread_name.c_str());

    for (;;) {
        std::function<void()> task;

        {
            std::unique_lock lock(queue_mutex_);
            condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            if (stop_ && tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }

        // Never let an exception escape the worker: it would call
        // std::terminate and take down the app (parity with
        // SearchThreadPool::ExecuteTaskWithExceptionHandling).
        try {
            task();
        } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all safety net for worker threads
            (void)e;
            LOG_ERROR_BUILD("ThreadPool: Exception in worker thread "
                            << worker_index << ": " << e.what());
        } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all required for worker threads
            LOG_ERROR_BUILD("ThreadPool: Unknown exception in worker thread "
                            << worker_index);
        }
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock lock(queue_mutex_);  // NOLINT(misc-const-correctness) - unique_lock has non-const unlock()
        stop_ = true;
    }
    condition_.notify_all();
    for (std::thread& worker : workers_) {
        worker.join();
    }
}

