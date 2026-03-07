#pragma once

#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "index/ISearchableIndex.h"
#include "search/ISearchExecutor.h"
#include "search/SearchContext.h"
#include "search/SearchTypes.h"
#include "utils/LightweightCallable.h"

// Forward declarations
class ParallelSearchEngine;

/**
 * Strategy Pattern for Load Balancing
 *
 * Base class for implementing different load balancing strategies for parallel
 * file search operations. Allows switching between different strategies at
 * runtime via settings parameter, enabling easy experimentation with different
 * approaches.
 *
 * Each strategy implements how work is distributed across threads:
 * - Static: Fixed chunks assigned upfront (original implementation)
 * - Dynamic: Small chunks assigned dynamically as threads finish
 * - Hybrid: Initial large chunks + dynamic small chunks (recommended)
 * - Interleaved: Threads process items in an interleaved manner
 *
 * The strategy pattern allows the search algorithm to be decoupled from the
 * specific load balancing approach, making it easy to add new strategies or
 * switch between them based on performance characteristics.
 *
 * @see StaticChunkingStrategy
 * @see HybridStrategy
 * @see DynamicStrategy
 * @see InterleavedChunkingStrategy
 */
class LoadBalancingStrategy {
 public:
  virtual ~LoadBalancingStrategy() = default;

 protected:
  // Protected default constructor (interface class)
  LoadBalancingStrategy() = default;

 public:
  // Non-copyable, non-movable (interface class)
  LoadBalancingStrategy(const LoadBalancingStrategy&) = delete;
  LoadBalancingStrategy& operator=(const LoadBalancingStrategy&) = delete;
  LoadBalancingStrategy(LoadBalancingStrategy&&) = delete;
  LoadBalancingStrategy& operator=(LoadBalancingStrategy&&) = delete;

  /**
   * Launch parallel search tasks using this strategy.
   *
   * Creates and enqueues search tasks to the thread pool according to the
   * specific load balancing strategy. Each task processes a chunk of the
   * file index and returns matching search results.
   *
   * @param index Reference to ISearchableIndex containing the data to search
   * @param executor Reference to ISearchExecutor for thread pool access
   * @param total_items Total number of items in the file index to process
   * @param thread_count Number of worker threads to use for parallel execution
   * @param context SearchContext containing all search parameters (queries, filters, options)
   * @param thread_timings Optional output parameter for per-thread timing statistics
   *
   * @return Vector of futures that will yield search results from each worker thread.
   *         The caller should wait on these futures and aggregate the results.
   *         Returns empty vector if thread pool is invalid or has 0 threads.
   *
   * @note Thread safety: Each worker thread acquires a shared_lock on index
   *       to prevent race conditions with Insert/Remove operations.
   * @note Exception handling: Exceptions in worker threads are caught and logged,
   *       but do not propagate to the caller.
   * @note Static methods (CreatePatternMatchers, ProcessChunkRange, etc.) are
   *       accessed directly via ParallelSearchEngine to avoid virtual call overhead.
   */
  virtual std::vector<std::future<std::vector<SearchResultData>>>
  LaunchSearchTasks(  // NOSONAR(cpp:S1206) - Function signature duplication is required for C++
                      // virtual function overrides; no default args (google-default-arguments)
    const ISearchableIndex& index, const ISearchExecutor& executor, size_t total_items,
    int thread_count, const SearchContext& context,
    std::vector<ThreadTiming>* thread_timings) const = 0;

  /**
   * Get the name of this strategy (for logging/debugging).
   */
  [[nodiscard]] virtual std::string GetName() const = 0;

  /**
   * Get a description of this strategy (for UI tooltips).
   */
  [[nodiscard]] virtual std::string GetDescription() const = 0;
};

/**
 * @brief Work Stealing Strategy (requires Boost)
 *
 * Uses distributed queues (one per thread) and work stealing to minimize contention.
 * Ideal for high core counts where single atomic counter becomes a bottleneck.
 */
class WorkStealingStrategy : public LoadBalancingStrategy {
 public:
  std::vector<std::future<std::vector<SearchResultData>>> LaunchSearchTasks(
    const ISearchableIndex& index, const ISearchExecutor& executor, size_t total_items,
    int thread_count, const SearchContext& context,
    std::vector<ThreadTiming>* thread_timings) const override;

  [[nodiscard]] std::string GetName() const override {
    return "work_stealing";
  }
  [[nodiscard]] std::string GetDescription() const override {
#if defined(FAST_LIBS_BOOST)
    return "Work Stealing: Distributed queues with stealing (High Contention optimized)";
#else
    return "Work Stealing: [Unavailable - Build with FAST_LIBS_BOOST=ON]";
#endif  // defined(FAST_LIBS_BOOST)
  }
};

/**
 * Factory function to create a load balancing strategy based on name.
 *
 * Creates a new instance of the specified load balancing strategy. If the
 * strategy name is unknown, returns the default strategy (hybrid) and logs
 * a warning.
 *
 * @param strategy_name Name of the strategy ("static", "dynamic", "hybrid", "interleaved")
 * @return Unique pointer to the strategy instance. Never returns nullptr.
 *         Returns hybrid strategy (default) if name is invalid.
 *
 * @see GetAvailableStrategyNames() for list of valid strategy names
 */
std::unique_ptr<LoadBalancingStrategy> CreateLoadBalancingStrategy(std::string_view strategy_name);

// ============================================================================
// Interleaved Chunking Strategy
// ============================================================================

/**
 * Interleaved Chunking Strategy
 *
 * Distributes work by dividing the total items into small sub-chunks and
 * assigning them to threads in an interleaved pattern. Each thread processes
 * every Nth chunk (where N is the thread count), ensuring better load
 * distribution when work items have varying processing times.
 *
 * Example with 3 threads and 9 chunks:
 * - Thread 0: chunks 0, 3, 6
 * - Thread 1: chunks 1, 4, 7
 * - Thread 2: chunks 2, 5, 8
 *
 * This strategy is particularly effective when:
 * - Work items have unpredictable processing times
 * - Cache locality is less important than load balance
 * - You want deterministic chunk assignment (no atomic operations needed)
 *
 * @note Uses dynamic_chunk_size parameter to determine sub-chunk size.
 *       Falls back to 256 if dynamic_chunk_size < 100.
 */
class InterleavedChunkingStrategy : public LoadBalancingStrategy {
 public:
  std::vector<std::future<std::vector<SearchResultData>>>
  LaunchSearchTasks(  // NOSONAR(cpp:S1206) - Function signature duplication is required for C++
                      // virtual function overrides; no default args (google-default-arguments)
    const ISearchableIndex& index, const ISearchExecutor& executor, size_t total_items,
    int thread_count, const SearchContext& context,
    std::vector<ThreadTiming>* thread_timings) const override;

  [[nodiscard]] std::string GetName() const override {
    return "interleaved";
  }
  [[nodiscard]] std::string GetDescription() const override {
    return "Interleaved chunking: Threads process items in an interleaved "
           "manner";
  }
};

/**
 * Get list of all available strategy names.
 *
 * Returns a vector containing the names of all load balancing strategies
 * that can be created via CreateLoadBalancingStrategy().
 *
 * @return Vector of strategy names: {"static", "hybrid", "dynamic", "interleaved"}
 *
 * @see CreateLoadBalancingStrategy()
 */
std::vector<std::string> GetAvailableStrategyNames();

/**
 * Get default strategy name.
 *
 * Returns the name of the default load balancing strategy used when an
 * unknown strategy name is provided or when no strategy is specified.
 *
 * @return Default strategy name: "hybrid"
 *
 * @see CreateLoadBalancingStrategy()
 */
std::string GetDefaultStrategyName();

/**
 * Validate and normalize strategy name.
 *
 * Validates that the strategy name is one of the supported values.
 * If invalid, logs a warning and returns the default strategy name.
 * This function serves as the single source of truth for strategy validation,
 * eliminating duplication between different parts of the codebase.
 *
 * @param strategy_name Strategy name to validate
 * @return Validated strategy name (normalized) or default ("hybrid") if invalid
 *
 * @note This function centralizes validation logic to prevent duplication.
 *       Used by both GetLoadBalancingStrategy() and CreateLoadBalancingStrategy().
 */
std::string ValidateAndNormalizeStrategyName(std::string_view strategy_name);
