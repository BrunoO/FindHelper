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

#include "utils/ThreadPool.h"
#include "utils/ThreadUtils.h"
#include <string>

ThreadPool::ThreadPool(size_t threads)  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - workers, queue_mutex, condition are std types with default constructors that initialize themselves
{
    for (size_t i = 0; i < threads; ++i) {
        workers.emplace_back(
            [this, i]
            {
                // Set thread name for profiling/debugging
                const std::string thread_name = "ThreadPool-" + std::to_string(i);
                SetThreadName(thread_name.c_str());

                for(;;)
                {
                    std::function<void()> task;

                    {
                        std::unique_lock lock(this->queue_mutex);
                        this->condition.wait(lock,
                            [this] { return this->stop || !this->tasks.empty(); });
                        if (this->stop && this->tasks.empty()) {
                            return;
                        }
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }

                    task();
                }
            }
        );
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock lock(queue_mutex);  // NOLINT(misc-const-correctness) - unique_lock has non-const unlock()
        stop = true;
    }
    condition.notify_all();
    for (std::thread& worker : workers) {
        worker.join();
    }
}

