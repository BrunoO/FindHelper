/**
 * @file SearchThreadPool.cpp
 * @brief Implementation of a thread pool dedicated to search operations
 *
 * This file implements the SearchThreadPool class, which provides a specialized
 * thread pool for parallel file search operations. Unlike the general-purpose
 * ThreadPool, this pool is optimized specifically for search tasks and is
 * managed as a static singleton by FileIndex.
 *
 * ARCHITECTURE:
 * - Worker threads are created at construction and persist until destruction
 * - Tasks are enqueued via Enqueue() and executed by available workers
 * - Thread-safe: Multiple threads can enqueue tasks concurrently
 * - Exception handling: Worker threads catch and log exceptions to prevent crashes
 * - Graceful shutdown: Destructor waits for all tasks to complete
 *
 * USAGE:
 * - Managed as static singleton by FileIndex (see FileIndex::GetThreadPool())
 * - Used exclusively for parallel search operations (FileIndex::SearchAsyncWithData)
 * - Thread count: Uses settings.searchThreadPoolSize or hardware_concurrency()
 * - Created lazily on first search operation
 *
 * THREAD NAMING:
 * - Worker threads are named "SearchPool-{i}" for profiling/debugging
 * - Names are visible in profilers (Instruments, Visual Studio Profiler, etc.)
 *
 * PERFORMANCE:
 * - Eliminates thread creation overhead (1-10ms per thread) for frequent searches
 * - Enables fair strategy comparisons (consistent baseline)
 * - Improves performance for repeated search operations
 * - Task execution happens outside locks to minimize contention
 *
 * DIFFERENCES FROM ThreadPool:
 * - Specialized for search operations (owned by FileIndex, not Application)
 * - Includes exception handling in worker threads
 * - Uses different naming convention (SearchPool vs ThreadPool)
 *
 * @see SearchThreadPool.h for class interface
 * @see ThreadPool.h for general-purpose thread pool
 * @see FileIndex.cpp for SearchThreadPool ownership and usage
 * @see LoadBalancingStrategy.cpp for search task execution
 */

#include "search/SearchThreadPool.h"

#include <exception>
#include <thread>

#include "utils/Logger.h"
#include "utils/ThreadUtils.h"

namespace {

// Returns effective thread count for the pool (reduces constructor complexity).
size_t ComputeThreadCount(size_t num_threads) {
  if (num_threads != 0) {
    return num_threads;
  }
  num_threads = std::thread::hardware_concurrency();
  return num_threads != 0 ? num_threads : 2;
}

// Execute one task with exception handling (reduces RunWorker cognitive complexity).
void ExecuteTaskWithExceptionHandling(const std::function<void()>& task, size_t worker_index) {
  if (!task) {
    return;
  }
  try {
    task();
  } catch (const std::bad_alloc& e) {
    (void)e;
    LOG_ERROR_BUILD("SearchThreadPool: Memory allocation failure in worker thread "
                    << worker_index);
  } catch (const std::runtime_error& e) {  // NOSONAR(cpp:S1181) - Part of exception hierarchy
    (void)e;
    LOG_ERROR_BUILD("SearchThreadPool: Runtime error in worker thread "
                    << worker_index << ": " << e.what());
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all safety net
    (void)e;
    LOG_ERROR_BUILD("SearchThreadPool: Exception in worker thread "
                    << worker_index << ": " << e.what());
  } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all required for worker threads
    LOG_ERROR_BUILD("SearchThreadPool: Unknown exception in worker thread "
                    << worker_index);
  }
}

}  // namespace

SearchThreadPool::SearchThreadPool(size_t num_threads)  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - threads_, mutex_, cv_ are std types with default constructors
{
  const size_t n = ComputeThreadCount(num_threads);
  threads_.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    threads_.emplace_back([this, i]() { RunWorker(this, i); });
  }
}

void SearchThreadPool::RunWorker(SearchThreadPool* pool, size_t worker_index) {
  const std::string thread_name = "SearchPool-" + std::to_string(worker_index);
  SetThreadName(thread_name.c_str());

  while (true) {
    std::function<void()> task;
    {
      std::unique_lock lock(pool->mutex_);
      pool->cv_.wait(lock,
                     [pool] { return !pool->tasks_.empty() || pool->shutdown_; });
      if (pool->shutdown_ && pool->tasks_.empty()) {
        break;
      }
      if (!pool->tasks_.empty()) {
        task = std::move(pool->tasks_.front());
        pool->tasks_.pop();
      }
    }
    ExecuteTaskWithExceptionHandling(task, worker_index);
  }
}

SearchThreadPool::~SearchThreadPool() {
  {
    const std::scoped_lock lock(mutex_);
    shutdown_ = true;
  }
  
  // Notify all threads to wake up and check shutdown flag
  cv_.notify_all();
  
  // Wait for all threads to finish
  for (auto &thread : threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
}
