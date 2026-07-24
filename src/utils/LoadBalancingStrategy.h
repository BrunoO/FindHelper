#pragma once

#include <future>
#include <vector>

#include "index/ISearchableIndex.h"
#include "search/ISearchExecutor.h"
#include "search/SearchContext.h"
#include "search/SearchTypes.h"

/**
 * Hybrid Load Balancing Strategy (Initial Chunks + Dynamic Small Chunks).
 *
 * Assigns initial large chunks upfront per thread, then uses dynamic small
 * chunks for the remaining work. Provides good cache locality for the initial
 * work while maintaining excellent load balance for the remainder.
 */
class HybridStrategy {
 public:
  HybridStrategy() = default;
  ~HybridStrategy() = default;

  // Non-copyable, non-movable
  HybridStrategy(const HybridStrategy&) = delete;
  HybridStrategy& operator=(const HybridStrategy&) = delete;
  HybridStrategy(HybridStrategy&&) = delete;
  HybridStrategy& operator=(HybridStrategy&&) = delete;

  /**
   * Launch parallel search tasks.
   *
   * Creates and enqueues search tasks to the thread pool. Each task processes
   * a chunk of the file index and returns matching search results.
   *
   * @param index Reference to ISearchableIndex containing the data to search
   * @param executor Reference to ISearchExecutor for thread pool access
   * @param total_items Total number of items in the file index to process
   * @param thread_count Number of worker threads to use for parallel execution
   * @param context SearchContext containing all search parameters
   * @param thread_timings Optional output parameter for per-thread timing statistics
   *
   * @return Vector of futures that will yield search results from each worker thread.
   *         Returns empty vector if thread pool is invalid or has 0 threads.
   *
   * @note Thread safety: Each worker thread acquires a shared_lock on index
   *       to prevent race conditions with Insert/Remove operations.
   */
  [[nodiscard]] std::vector<std::future<std::vector<SearchResultData>>> LaunchSearchTasks(
    const ISearchableIndex& index, const ISearchExecutor& executor, size_t total_items,
    int thread_count, const SearchContext& context,
    std::vector<ThreadTiming>* thread_timings) const;
};
