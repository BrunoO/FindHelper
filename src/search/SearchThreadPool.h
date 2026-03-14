#pragma once

#include "utils/Logger.h"
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

/**
 * Simple thread pool for search operations.
 *
 * Reuses threads instead of creating new ones for each search, which:
 * 1. Eliminates thread creation overhead (1-10ms per thread)
 * 2. Makes strategy comparisons fairer (consistent baseline)
 * 3. Improves performance for frequent searches
 *
 * Thread-safe: Can be used from multiple threads concurrently.
 */
class SearchThreadPool {
public:
  /**
   * Create a thread pool with the specified number of threads.
   *
   * @param num_threads Number of worker threads (default: hardware concurrency)
   */
  explicit SearchThreadPool(size_t num_threads = 0);

  /**
   * Destructor: Shuts down the thread pool and waits for all threads to finish.
   */
  ~SearchThreadPool();

  // Delete copy constructor and assignment (thread pool is not copyable)
  SearchThreadPool(const SearchThreadPool &) = delete;
  SearchThreadPool &operator=(const SearchThreadPool &) = delete;
  // Delete move constructor and assignment (thread pool manages threads, not movable)
  SearchThreadPool(SearchThreadPool &&) = delete;
  SearchThreadPool &operator=(SearchThreadPool &&) = delete;

  /**
   * Enqueue a task to be executed by a worker thread.
   *
   * @param f Callable object (function, lambda, etc.)
   * @return Future that will contain the result when the task completes
   */
  template<typename F>
  auto Enqueue(F &&f) -> std::future<decltype(f())> {  // NOLINT(cppcoreguidelines-missing-std-forward) - f forwarded to packaged_task ctor
    // Create a packaged_task to wrap the callable
    auto task = std::make_shared<std::packaged_task<decltype(f())()>>(
        std::forward<F>(f));

    // Get future for the result
    std::future<decltype(f())> result = task->get_future();

    {
      const std::scoped_lock lock(mutex_);
      if (shutdown_) {
        // Thread pool is shutting down, log warning and return invalid future
        // This can happen during application shutdown if searches are still active
        LOG_WARNING_BUILD("SearchThreadPool: Task rejected during shutdown (thread pool is shutting down)");
        return result;
      }

      // Enqueue the task
      tasks_.emplace([task]() { (*task)(); });
    }

    // Notify one waiting thread
    cv_.notify_one();
    return result;
  }

  /**
   * Get the number of worker threads in the pool.
   */
  [[nodiscard]] size_t GetThreadCount() const { return threads_.size(); }

  /**
   * Get the number of pending tasks in the queue.
   */
  [[nodiscard]] size_t GetPendingTaskCount() const {
    const std::scoped_lock lock(mutex_);
    return tasks_.size();
  }

private:
  // Helper for constructor: runs the worker loop (reduces constructor cognitive complexity, cpp:S3776)
  static void RunWorker(SearchThreadPool* pool, size_t worker_index);

  // Worker threads
  std::vector<std::thread> threads_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // Task queue
  std::queue<std::function<void()>> tasks_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // Synchronization
  mutable std::mutex mutex_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::condition_variable cv_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  bool shutdown_ = false;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
};
