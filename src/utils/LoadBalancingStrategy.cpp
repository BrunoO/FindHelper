/**
 * @file LoadBalancingStrategy.cpp
 * @brief Implementation of load balancing strategies for parallel search
 *
 * This file implements multiple load balancing strategies for distributing
 * search work across multiple threads. Each strategy has different
 * characteristics suited for different workloads and performance requirements.
 *
 * STRATEGIES IMPLEMENTED:
 * 1. Static: Fixed-size chunks assigned upfront (simple, predictable)
 * 2. Dynamic: Work-stealing with dynamic chunk sizing (best load balance)
 * 3. Hybrid: Combines static initial work with dynamic stealing
 * 4. Interleaved: Round-robin assignment for cache-friendly access
 *
 * STRATEGY SELECTION:
 * - Selected via settings.loadBalancingStrategy
 * - Default: Hybrid (good balance of performance and load distribution)
 * - Can be changed at runtime via settings UI
 *
 * PERFORMANCE CONSIDERATIONS:
 * - Static: Lowest overhead, best for uniform workloads
 * - Dynamic: Best load balance, higher overhead
 * - Hybrid: Balanced approach, good for mixed workloads
 * - Interleaved: Best cache locality, good for sequential access patterns
 *
 * THREAD SAFETY:
 * - All strategies use SearchThreadPool for thread management
 * - Atomic operations for cancellation and progress tracking
 * - Shared locks for FileIndex access during search
 *
 * @see LoadBalancingStrategy.h for base class and factory function
 * @see FileIndex.cpp for strategy usage in search operations
 * @see Settings.h for strategy configuration
 */

#include "utils/LoadBalancingStrategy.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <future>
#include <memory>
#include <new>
#include <stdexcept>
#ifdef FAST_LIBS_BOOST
#include <boost/lockfree/queue.hpp>
#endif  // FAST_LIBS_BOOST

#include "core/Settings.h"
#include "index/ISearchableIndex.h"
#include "path/PathPatternMatcher.h"
#include "search/ParallelSearchEngine.h"
#include "search/SearchPatternUtils.h"
#include "search/SearchThreadPool.h"
#include "search/SearchTypes.h"
#include "utils/LightweightCallable.h"
#include "utils/Logger.h"

// ============================================================================
// Helper for Exception Handling (DRY - eliminates duplication)
// ============================================================================

namespace load_balancing_detail {

/**
 * @brief Thread setup data structure (after lock acquisition)
 *
 * Holds setup data that can be extracted after the lock is acquired.
 * The lock itself must remain in the lambda scope for proper RAII.
 */
struct ThreadSetupAfterLock {
  PathStorage::SoAView soa_view_; // NOLINT(readability-identifier-naming)
  size_t storage_size_ = 0; // NOLINT(readability-identifier-naming)
  ParallelSearchEngine::PatternMatchers matchers_; // NOLINT(readability-identifier-naming)
};

/**
 * @brief Initialize thread setup after lock acquisition
 *
 * Performs common initialization steps after the lock is acquired:
 * - Gets SoA view and storage size
 * - Creates pattern matchers
 *
 * This helper eliminates code duplication across all load balancing strategies.
 * Note: The lock must be acquired before calling this function.
 *
 * @param index The searchable index (lock must already be held)
 * @param context The search context
 * @return ThreadSetupAfterLock struct with initialized data
 */
[[nodiscard]] inline ThreadSetupAfterLock SetupThreadWorkAfterLock(
  const ISearchableIndex& index,
  const SearchContext& context) {
  ThreadSetupAfterLock setup;

  // Get SoA view after acquiring lock (pointers are only valid while lock is held)
  setup.soa_view_ = index.GetSearchableView();
  setup.storage_size_ = index.GetStorageSize();

  // Create pattern matchers once per thread (optimization)
  setup.matchers_ = ParallelSearchEngine::CreatePatternMatchers(context);

  return setup;
}

/**
 * @brief Parameters for thread timing recording
 */
struct ThreadTimingParams {
  size_t start_index_ = 0; // NOLINT(readability-identifier-naming)
  size_t end_index_ = 0; // NOLINT(readability-identifier-naming)
  size_t initial_items_ = 0; // NOLINT(readability-identifier-naming)
  size_t dynamic_chunks_count_ = 0; // NOLINT(readability-identifier-naming)
  size_t total_items_processed_ = 0; // NOLINT(readability-identifier-naming)
  size_t bytes_processed_ = 0; // NOLINT(readability-identifier-naming)
  size_t results_count_ = 0; // NOLINT(readability-identifier-naming)
  const PathStorage::SoAView* soa_view_ = nullptr; // NOLINT(readability-identifier-naming)
  size_t storage_size_ = 0; // NOLINT(readability-identifier-naming)
  size_t chunk_start_ = 0; // NOLINT(readability-identifier-naming)
  size_t chunk_end_ = 0; // NOLINT(readability-identifier-naming)
};

/**
 * @brief Record thread timing information if requested
 *
 * Records thread timing statistics for performance monitoring.
 * This helper eliminates code duplication across all load balancing strategies.
 *
 * @param thread_timings Optional pointer to thread timings vector (nullptr = skip recording)
 * @param thread_idx Thread index
 * @param start_time Start time of thread work
 * @param params Timing parameters (chunk ranges, item counts, bytes info, etc.)
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - Static function; PascalCase for function name
inline void RecordThreadTimingIfRequested(
  std::vector<ThreadTiming>* thread_timings, int thread_idx,
  const std::chrono::high_resolution_clock::time_point& start_time,
  const ThreadTimingParams& params) {
  if (thread_timings == nullptr) {
    return;
  }

  const auto end_time = std::chrono::high_resolution_clock::now();
  const auto elapsed =
    std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

  // Calculate bytes if not already provided
  size_t total_bytes = params.bytes_processed_;
  if (total_bytes == 0 && params.soa_view_ != nullptr) {
    total_bytes = ParallelSearchEngine::CalculateChunkBytes(*params.soa_view_, params.chunk_start_,
                                                            params.chunk_end_, params.storage_size_);
  }

  ThreadTiming timing;
  ParallelSearchEngine::RecordThreadTiming(timing, thread_idx, params.start_index_, params.end_index_,
                                           params.initial_items_, params.dynamic_chunks_count_,
                                           params.total_items_processed_, total_bytes,
                                           params.results_count_, elapsed);

  if (thread_idx < static_cast<int>(thread_timings->size())) {
    (*thread_timings)[thread_idx] = timing;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - bounds checked by the enclosing if condition
  }
}

/**
 * @brief Validate thread pool has threads available
 *
 * Validates that the thread pool has at least one thread available for execution.
 * This helper eliminates code duplication across all load balancing strategies.
 *
 * @param executor The search executor containing the thread pool
 * @param strategy_name Strategy name for error logging
 * @return true if thread pool is valid (has threads), false otherwise
 */
[[nodiscard]] inline bool ValidateThreadPool(const ISearchExecutor& executor,
                               const char* strategy_name) {
  const SearchThreadPool& thread_pool = executor.GetThreadPool();
  if (const size_t pool_thread_count = thread_pool.GetThreadCount();
      pool_thread_count == 0) {
    LOG_ERROR_BUILD(strategy_name << ": Thread pool has 0 threads - cannot execute search");
    return false;
  }
  return true;
}

/**
 * @brief Chunk processing parameters
 */
struct ChunkProcessingParams {
  const ThreadSetupAfterLock& setup; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  size_t chunk_start_ = 0; // NOLINT(readability-identifier-naming)
  size_t chunk_end_ = 0; // NOLINT(readability-identifier-naming)
  int thread_idx_ = 0; // NOLINT(readability-identifier-naming)
  const char* strategy_name_ = nullptr; // NOLINT(readability-identifier-naming)
  const char* chunk_type_ = nullptr; // NOLINT(readability-identifier-naming)
};

/**
 * @brief Process chunk with exception handling
 *
 * Processes a chunk of work using ProcessChunkRange, with comprehensive exception handling.
 * This helper eliminates code duplication across all load balancing strategies.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - Static function
inline void ProcessChunkWithExceptionHandling(
  const ChunkProcessingParams& params, std::vector<SearchResultData>& local_results,
  const SearchContext& context) {
  try {
    // Use optimized overload that accepts pre-created matchers
    ParallelSearchEngine::ProcessChunkRange(
      params.setup.soa_view_, params.chunk_start_, params.chunk_end_, local_results, context,
      params.setup.storage_size_, params.setup.matchers_.filename_matcher,
      params.setup.matchers_.path_matcher);
  } catch (const std::bad_alloc& e) {
    (void)e;
    LOG_ERROR_BUILD("Memory allocation failure in "
                    << params.strategy_name_ << " (" << params.chunk_type_ << ", thread "
                    << params.thread_idx_ << ", range [" << params.chunk_start_ << "-"
                    << params.chunk_end_ << "])");
  } catch (const std::runtime_error& e) {  // NOSONAR(cpp:S1181) - Catch-all for runtime errors in worker thread
    (void)e;
    LOG_ERROR_BUILD("Runtime error in " << params.strategy_name_ << " (" << params.chunk_type_
                                        << ", thread " << params.thread_idx_ << ", range ["
                                        << params.chunk_start_ << "-" << params.chunk_end_
                                        << "]): " << e.what());
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all for standard exceptions in worker thread
    (void)e;
    LOG_ERROR_BUILD("Exception in " << params.strategy_name_ << " (" << params.chunk_type_
                                    << ", thread " << params.thread_idx_ << ", range ["
                                    << params.chunk_start_ << "-" << params.chunk_end_
                                    << "]): " << e.what());
  }
}

/**
 * @brief Dynamic chunks loop input parameters
 */
struct DynamicChunksLoopParams {
  const ThreadSetupAfterLock& setup; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  size_t total_items_ = 0; // NOLINT(readability-identifier-naming)
  size_t initial_chunks_end_ = 0; // NOLINT(readability-identifier-naming)
  std::atomic<size_t>* next_chunk_start_ = nullptr; // NOLINT(readability-identifier-naming)
  const SearchContext& context; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  std::vector<SearchResultData>& local_results; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  int thread_idx_ = 0; // NOLINT(readability-identifier-naming)
  const char* strategy_name_ = nullptr; // NOLINT(readability-identifier-naming)
  int thread_count_ = 0; // NOLINT(readability-identifier-naming)
  size_t min_chunk_size_ = 0; // NOLINT(readability-identifier-naming)
};

/**
 * @brief Dynamic chunks loop output parameters
 */
struct DynamicChunksLoopOutput {
  size_t& dynamic_chunks_count; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  size_t& dynamic_items_total; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  size_t* total_bytes_processed_ = nullptr; // NOLINT(readability-identifier-naming)
  size_t* total_items_processed_ = nullptr; // NOLINT(readability-identifier-naming)
};

/**
 * @brief Process dynamic chunks in a loop to reduce nesting depth
 *
 * Processes remaining work using dynamic chunk allocation with cancellation support.
 * This helper reduces nesting depth by extracting the while loop logic.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - Static function
inline void ProcessDynamicChunksLoop(const DynamicChunksLoopParams& params,
                                     DynamicChunksLoopOutput& output) {
  if (params.initial_chunks_end_ >= params.total_items_) {
    return;  // No remaining work
  }

  bool should_continue = true;
  while (should_continue) {
    // Check for cancellation before claiming each chunk
    if (params.context.cancel_flag != nullptr && params.context.cancel_flag->load(std::memory_order_acquire)) {
      should_continue = false;
      continue;
    }

    // Calculate chunk size (guided scheduling if thread_count > 0, otherwise use
    // context.dynamic_chunk_size)
    size_t current_chunk_size = params.context.dynamic_chunk_size;
    if (params.thread_count_ > 0 && params.min_chunk_size_ > 0) {
      // Guided Scheduling: Calculate chunk size dynamically based on remaining items
      const size_t current_processed = params.next_chunk_start_->load(std::memory_order_relaxed);
      const size_t remaining =
        (params.total_items_ > current_processed) ? (params.total_items_ - current_processed) : 0;
      const size_t divisor = 2 * static_cast<size_t>(params.thread_count_);
      const size_t calculated_size = remaining / divisor;
      current_chunk_size = (std::max)(params.min_chunk_size_, calculated_size);
    }

    // Atomically claim the next chunk with the calculated size
    const size_t chunk_start =
      params.next_chunk_start_->fetch_add(current_chunk_size, std::memory_order_relaxed);

    // Stop if we've claimed beyond the end of available work
    if (chunk_start >= params.total_items_) {
      should_continue = false;
      continue;
    }

    // Calculate chunk end, ensuring we don't exceed total_items
    const size_t chunk_end = (std::min)(chunk_start + current_chunk_size, params.total_items_);

    // Process the dynamic chunk
    if (chunk_end <= chunk_start) {
      should_continue = false;
      continue;
    }

    ProcessChunkWithExceptionHandling({params.setup, chunk_start, chunk_end, params.thread_idx_,
                                       params.strategy_name_, "dynamic chunk"},
                                      params.local_results, params.context);

    output.dynamic_chunks_count++;
    output.dynamic_items_total += (chunk_end - chunk_start);

    if (output.total_bytes_processed_ != nullptr) {
      *output.total_bytes_processed_ += ParallelSearchEngine::CalculateChunkBytes(
        params.setup.soa_view_, chunk_start, chunk_end, params.setup.storage_size_);
    }
    if (output.total_items_processed_ != nullptr) {
      *output.total_items_processed_ += (chunk_end - chunk_start);
    }
  }
}

/**
 * @brief Static strategy task parameters
 */
struct StaticStrategyTaskParams {
  const ISearchableIndex& index; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  size_t start_index_ = 0; // NOLINT(readability-identifier-naming)
  size_t end_index_ = 0; // NOLINT(readability-identifier-naming)
  int thread_idx_ = 0; // NOLINT(readability-identifier-naming)
  const SearchContext& context; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  std::vector<ThreadTiming>* thread_timings_ = nullptr; // NOLINT(readability-identifier-naming)
};

/**
 * @brief Execute StaticChunkingStrategy task for a single thread
 *
 * Processes a single fixed chunk. Extracted from lambda to reduce complexity
 * and improve maintainability; matches pattern of Hybrid/Dynamic/Interleaved.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - Static function
inline std::vector<SearchResultData> ExecuteStaticStrategyTask(
  const StaticStrategyTaskParams& params) {
  const auto start_time = std::chrono::high_resolution_clock::now();
  std::vector<SearchResultData> local_results;
  constexpr size_t k_match_rate_heuristic = 20;
  local_results.reserve((params.end_index_ - params.start_index_) / k_match_rate_heuristic);

  // CRITICAL: Acquire shared_lock in worker thread to prevent race conditions
  const std::shared_lock lock(params.index.GetMutex());

  // Get SoA view, storage size, and pattern matchers after acquiring lock
  const auto setup = load_balancing_detail::SetupThreadWorkAfterLock(params.index, params.context);

  // Process chunk with exception handling
  load_balancing_detail::ProcessChunkWithExceptionHandling(
    {setup, params.start_index_, params.end_index_, params.thread_idx_, "StaticChunkingStrategy",
     "chunk"},
    local_results, params.context);

  load_balancing_detail::RecordThreadTimingIfRequested(
    params.thread_timings_, params.thread_idx_, start_time,
    {params.start_index_, params.end_index_, params.end_index_ - params.start_index_, 0,
     params.end_index_ - params.start_index_, 0, local_results.size(), &setup.soa_view_,
     setup.storage_size_, params.start_index_, params.end_index_});

  return local_results;
}

/**
 * @brief Hybrid strategy task parameters
 */
struct HybridStrategyTaskParams {
  const ISearchableIndex& index; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  size_t start_index_ = 0; // NOLINT(readability-identifier-naming)
  size_t end_index_ = 0; // NOLINT(readability-identifier-naming)
  int thread_idx_ = 0; // NOLINT(readability-identifier-naming)
  size_t total_items_ = 0; // NOLINT(readability-identifier-naming)
  size_t initial_chunks_end_ = 0; // NOLINT(readability-identifier-naming)
  const std::shared_ptr<std::atomic<size_t>>& next_dynamic_chunk; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  const SearchContext& context; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  std::vector<ThreadTiming>* thread_timings_ = nullptr; // NOLINT(readability-identifier-naming)
  int thread_count_ = 0; // NOLINT(readability-identifier-naming)
  size_t min_chunk_size_ = 0; // NOLINT(readability-identifier-naming)
};

/**
 * @brief Execute HybridStrategy task for a single thread
 *
 * Processes initial chunk and then competes for dynamic chunks.
 * Extracted from lambda to reduce complexity and improve maintainability.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - Static function
inline std::vector<SearchResultData> ExecuteHybridStrategyTask(
  const HybridStrategyTaskParams& params) {
  const auto start_time = std::chrono::high_resolution_clock::now();
  std::vector<SearchResultData> local_results;
  constexpr size_t k_match_rate_heuristic = 20;
  local_results.reserve((params.end_index_ - params.start_index_) / k_match_rate_heuristic);

  // CRITICAL: Acquire shared_lock in worker thread to prevent race conditions
  const std::shared_lock lock(params.index.GetMutex());

  // Get SoA view, storage size, and pattern matchers after acquiring lock
  const auto setup = load_balancing_detail::SetupThreadWorkAfterLock(params.index, params.context);

  // Process initial chunk with exception handling
  load_balancing_detail::ProcessChunkWithExceptionHandling(
    {setup, params.start_index_, params.end_index_, params.thread_idx_, "HybridStrategy",
     "initial chunk"},
    local_results, params.context);

  // Track statistics for timing information
  const size_t initial_items = params.end_index_ - params.start_index_;
  size_t dynamic_chunks_count = 0;
  size_t dynamic_items_total = 0;

  // Phase 2: Process remaining work using dynamic chunk allocation
  DynamicChunksLoopOutput dynamic_output{dynamic_chunks_count, dynamic_items_total};  // NOLINT(misc-const-correctness) - passed by non-const ref to ProcessDynamicChunksLoop
  load_balancing_detail::ProcessDynamicChunksLoop(
    {setup, params.total_items_, params.initial_chunks_end_, params.next_dynamic_chunk.get(),
     params.context, local_results, params.thread_idx_, "HybridStrategy", params.thread_count_,
     params.min_chunk_size_},
    dynamic_output);

  load_balancing_detail::RecordThreadTimingIfRequested(
    params.thread_timings_, params.thread_idx_, start_time,
    {params.start_index_, params.end_index_, initial_items, dynamic_chunks_count,
     initial_items + dynamic_items_total, 0, local_results.size(), &setup.soa_view_,
     setup.storage_size_, params.start_index_, params.end_index_});

  return local_results;
}

/**
 * @brief Dynamic strategy task parameters
 */
struct DynamicStrategyTaskParams {
  const ISearchableIndex& index; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  int thread_idx_ = 0; // NOLINT(readability-identifier-naming)
  size_t total_items_ = 0; // NOLINT(readability-identifier-naming)
  size_t initial_chunks_end_ = 0; // NOLINT(readability-identifier-naming)
  size_t my_initial_start_ = 0; // NOLINT(readability-identifier-naming)
  size_t my_initial_end_ = 0; // NOLINT(readability-identifier-naming)
  const std::shared_ptr<std::atomic<size_t>>& next_chunk_start; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  size_t min_chunk_size_ = 0; // NOLINT(readability-identifier-naming)
  const SearchContext& context; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  std::vector<ThreadTiming>* thread_timings_ = nullptr; // NOLINT(readability-identifier-naming)
  int thread_count_ = 0; // NOLINT(readability-identifier-naming)
};

/**
 * @brief Execute DynamicStrategy task for a single thread
 *
 * Processes initial chunk and then competes for dynamic chunks with guided scheduling.
 * Extracted from lambda to reduce complexity and improve maintainability.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - Static function
inline std::vector<SearchResultData> ExecuteDynamicStrategyTask(
  const DynamicStrategyTaskParams& params) {
  const auto start_time = std::chrono::high_resolution_clock::now();
  std::vector<SearchResultData> local_results;
  // Reserve space assuming ~5% match rate (heuristic for performance)
  constexpr size_t k_match_rate_heuristic = 20;
  const size_t estimated_matches = (std::max)(
    (params.my_initial_end_ - params.my_initial_start_) / k_match_rate_heuristic,
    params.min_chunk_size_);
  local_results.reserve(estimated_matches);

  // CRITICAL: Acquire shared_lock in worker thread to prevent race conditions
  const std::shared_lock lock(params.index.GetMutex());

  // Get SoA view, storage size, and pattern matchers after acquiring lock
  const auto setup = load_balancing_detail::SetupThreadWorkAfterLock(params.index, params.context);

  // Track statistics for timing information
  size_t initial_items = 0;
  size_t dynamic_chunks_count = 0;
  size_t total_items_processed = 0;
  size_t total_bytes_processed = 0;

  // ========== Phase 1: Process Initial Chunk (no contention) ==========
  if (params.my_initial_start_ < params.my_initial_end_) {
    load_balancing_detail::ProcessChunkWithExceptionHandling(
      {setup, params.my_initial_start_, params.my_initial_end_, params.thread_idx_, "DynamicStrategy",
       "initial chunk"},
      local_results, params.context);

    initial_items = params.my_initial_end_ - params.my_initial_start_;
    total_items_processed += initial_items;
    total_bytes_processed += ParallelSearchEngine::CalculateChunkBytes(
      setup.soa_view_, params.my_initial_start_, params.my_initial_end_, setup.storage_size_);
  }

  // ========== Phase 2: Dynamic Scheduling for Remaining Work ==========
  size_t dynamic_items_total = 0;
  DynamicChunksLoopOutput dynamic_output{dynamic_chunks_count, dynamic_items_total,  // NOLINT(misc-const-correctness) - passed by non-const ref to ProcessDynamicChunksLoop
                                         &total_bytes_processed, &total_items_processed};
  load_balancing_detail::ProcessDynamicChunksLoop(
    {setup, params.total_items_, params.initial_chunks_end_, params.next_chunk_start.get(),
     params.context, local_results, params.thread_idx_, "DynamicStrategy", params.thread_count_,
     params.min_chunk_size_},
    dynamic_output);

  load_balancing_detail::RecordThreadTimingIfRequested(
    params.thread_timings_, params.thread_idx_, start_time,
    {params.my_initial_start_, params.my_initial_end_, initial_items, dynamic_chunks_count,
     total_items_processed, total_bytes_processed, local_results.size(), &setup.soa_view_,
     setup.storage_size_, params.my_initial_start_, params.my_initial_end_});

  return local_results;
}

/**
 * @brief Interleaved strategy task parameters
 */
struct InterleavedStrategyTaskParams {
  const ISearchableIndex& index; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  int thread_idx_ = 0; // NOLINT(readability-identifier-naming)
  size_t total_items_ = 0; // NOLINT(readability-identifier-naming)
  int thread_count_ = 0; // NOLINT(readability-identifier-naming)
  size_t sub_chunk_size_ = 0; // NOLINT(readability-identifier-naming)
  size_t num_sub_chunks_ = 0; // NOLINT(readability-identifier-naming)
  const SearchContext& context; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  std::vector<ThreadTiming>* thread_timings_ = nullptr; // NOLINT(readability-identifier-naming)
};

/**
 * @brief Execute InterleavedChunkingStrategy task for a single thread
 *
 * Processes chunks in interleaved pattern (thread t gets chunks t, t+N, t+2N, ...).
 * Extracted from lambda to reduce complexity and improve maintainability.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - Static function
inline std::vector<SearchResultData> ExecuteInterleavedStrategyTask(
  const InterleavedStrategyTaskParams& params) {
  const auto start_time = std::chrono::high_resolution_clock::now();
  std::vector<SearchResultData> local_results;
  // Heuristic: assume up to ~20% of items may match for typical queries.
  // Use per-thread share of total_items to size the initial capacity and
  // apply a minimum to avoid reserve(0) to reduce reallocations.
  const size_t items_per_thread =
    (params.thread_count_ > 0) ? (params.total_items_ / static_cast<size_t>(params.thread_count_))
                              : params.total_items_;
  constexpr size_t k_hit_rate_heuristic_divisor = 5;
  constexpr size_t k_min_capacity = 16;
  const size_t initial_capacity = (std::max)(items_per_thread / k_hit_rate_heuristic_divisor, k_min_capacity);
  local_results.reserve(initial_capacity);

  // CRITICAL: Acquire shared_lock in worker thread to prevent race conditions
  const std::shared_lock lock(params.index.GetMutex());

  // Get SoA view, storage size, and pattern matchers after acquiring lock
  const auto setup = load_balancing_detail::SetupThreadWorkAfterLock(params.index, params.context);

  // Track statistics for timing information
  size_t total_items_processed = 0;
  size_t chunks_processed = 0;
  size_t total_bytes_processed = 0;

  // Process chunks in interleaved pattern: thread t gets chunks t, t+N, t+2N, ...
  // where N is thread_count
  for (auto chunk_idx = static_cast<size_t>(params.thread_idx_); chunk_idx < params.num_sub_chunks_;
       chunk_idx += static_cast<size_t>(params.thread_count_)) {
    // Check for cancellation before processing each chunk
    if (params.context.cancel_flag != nullptr && params.context.cancel_flag->load(std::memory_order_acquire)) {
      break;
    }
    // Calculate chunk boundaries
    const auto chunk_start = chunk_idx * params.sub_chunk_size_;
    const auto chunk_end = (std::min)(chunk_start + params.sub_chunk_size_,
                                 params.total_items_);

    // Skip empty chunks (shouldn't happen, but be safe)
    if (chunk_start >= chunk_end) {
      continue;
    }

    // Note: SoA view and matchers are still valid (lock is held for entire lambda)
    // Process chunk with exception handling
    load_balancing_detail::ProcessChunkWithExceptionHandling(
      {setup, chunk_start, chunk_end, params.thread_idx_, "InterleavedChunkingStrategy", "chunk"},
      local_results, params.context);

    // Update statistics
    const size_t items_in_chunk = chunk_end - chunk_start;
    total_items_processed += items_in_chunk;
    total_bytes_processed += ParallelSearchEngine::CalculateChunkBytes(
      setup.soa_view_, chunk_start, chunk_end, setup.storage_size_);
    chunks_processed++;
  }

  // Record timing information if requested
  // For interleaved strategy, we process multiple chunks per thread
  // Record the first chunk start and last chunk end for better visibility
  const size_t first_chunk_start =
    (static_cast<size_t>(params.thread_idx_) < params.num_sub_chunks_) ? (static_cast<size_t>(params.thread_idx_) * params.sub_chunk_size_) : 0;
  size_t last_chunk_end = 0;
  if (chunks_processed > 0) {
    // Calculate the last chunk index this thread processed
    // Formula: last_chunk_idx = t + (chunks_processed - 1) * thread_count
    const size_t last_chunk_idx = static_cast<size_t>(params.thread_idx_) + ((chunks_processed - 1) * static_cast<size_t>(params.thread_count_));
    if (last_chunk_idx < params.num_sub_chunks_) {
      last_chunk_end = (std::min)((last_chunk_idx + 1) * params.sub_chunk_size_, params.total_items_);
    } else {
      last_chunk_end = params.total_items_;
    }
  }

  load_balancing_detail::RecordThreadTimingIfRequested(
    params.thread_timings_, params.thread_idx_, start_time,
    {first_chunk_start, last_chunk_end, 0, chunks_processed, total_items_processed,
     total_bytes_processed, local_results.size(), &setup.soa_view_, setup.storage_size_,
     first_chunk_start, last_chunk_end});

  return local_results;
}

}  // namespace load_balancing_detail

// ============================================================================
// Static Chunking Strategy (Original Implementation)
// ============================================================================

/**
 * Static Chunking Strategy
 *
 * The original load balancing implementation that divides work into fixed,
 * equal-sized chunks assigned upfront to each thread. This is the simplest
 * strategy with minimal overhead.
 *
 * Algorithm:
 * 1. Calculate chunk_size = ceil(total_items / thread_count)
 * 2. Assign chunk [start_index, end_index) to each thread
 * 3. Each thread processes its entire chunk sequentially
 *
 * Characteristics:
 * - Pros: Simple, predictable, low overhead, good cache locality
 * - Cons: Poor load balance if work items have varying processing times
 *
 * Best for:
 * - Uniform work distribution (similar processing time per item)
 * - When cache locality is important
 * - Simple scenarios where load balance is not critical
 *
 * @note This strategy ignores dynamic_chunk_size and hybrid_initial_percent
 *       parameters as they are not applicable to static chunking.
 */
// StaticChunkingStrategy, HybridStrategy, and DynamicStrategy are defined in this file
// (not in header to reduce compile-time dependencies)

class StaticChunkingStrategy : public LoadBalancingStrategy {
 public:
  [[nodiscard]] std::vector<std::future<std::vector<SearchResultData>>> LaunchSearchTasks(
    const ISearchableIndex& index, const ISearchExecutor& executor, size_t total_items,
    int thread_count, const SearchContext& context,
    std::vector<ThreadTiming>* thread_timings) const override;

  [[nodiscard]] std::string GetName() const override {
    return "static";
  }
  [[nodiscard]] std::string GetDescription() const override {
    return "Static chunking: Fixed chunks assigned upfront (original implementation)";
  }
};

[[nodiscard]] std::vector<std::future<std::vector<SearchResultData>>>
StaticChunkingStrategy::LaunchSearchTasks(
  const ISearchableIndex& index, const ISearchExecutor& executor, size_t total_items,
  int thread_count, const SearchContext& context,
  std::vector<ThreadTiming>* thread_timings) const {
  std::vector<std::future<std::vector<SearchResultData>>> futures;
  futures.reserve(static_cast<size_t>(thread_count));

  // Calculate chunk size using ceiling division: ceil(total_items / thread_count)
  // This ensures all items are covered, with the last chunk potentially smaller
  const size_t chunk_size = (total_items + static_cast<size_t>(thread_count) - 1) / static_cast<size_t>(thread_count);
  if (!load_balancing_detail::ValidateThreadPool(executor, "StaticChunkingStrategy")) {
    return futures;  // Return empty futures vector
  }
  SearchThreadPool& thread_pool = executor.GetThreadPool();

  // Assign fixed chunks to each thread
  for (int t = 0; t < thread_count; ++t) {
    const size_t start_index = static_cast<size_t>(t) * chunk_size;
    // Ensure end_index doesn't exceed total_items (important for last chunk)
    const size_t end_index = (std::min)(start_index + chunk_size, total_items);

    // Skip empty chunks (can happen if thread_count > total_items)
    if (start_index >= end_index) {
      break;
    }

    futures.push_back(thread_pool.Enqueue([&index, start_index, end_index, t, context, thread_timings]() {  // NOLINT(bugprone-exception-escape) - exceptions propagate to std::future
      return load_balancing_detail::ExecuteStaticStrategyTask(
        {index, start_index, end_index, t, context, thread_timings});
    }));
  }

  return futures;
}

// ============================================================================
// Hybrid Strategy (Initial Chunks + Dynamic Small Chunks)
// ============================================================================

/**
 * Hybrid Strategy
 *
 * Combines the benefits of static and dynamic strategies by assigning
 * initial large chunks upfront, then using dynamic small chunks for the
 * remaining work. This provides good cache locality for the initial work
 * while maintaining excellent load balance for the remainder.
 *
 * Algorithm:
 * 1. Calculate initial_work_items = total_items * hybrid_initial_percent / 100
 * 2. Divide initial work into large chunks (one per thread)
 * 3. Each thread processes its initial chunk
 * 4. Remaining work is divided into small dynamic chunks
 * 5. Threads compete for dynamic chunks using atomic fetch_add
 *
 * Characteristics:
 * - Pros: Good cache locality (initial chunks) + excellent load balance (dynamic)
 * - Cons: Slightly more complex than static, requires atomic operations
 *
 * Best for:
 * - General-purpose use (recommended default)
 * - Scenarios with mixed work distribution
 * - When you want both performance and load balance
 *
 * @note This is the default strategy due to its balanced performance
 *       characteristics across different workloads.
 */
class HybridStrategy : public LoadBalancingStrategy {
 public:
  [[nodiscard]] std::vector<std::future<std::vector<SearchResultData>>> LaunchSearchTasks(
    const ISearchableIndex& index, const ISearchExecutor& executor, size_t total_items,
    int thread_count, const SearchContext& context,
    std::vector<ThreadTiming>* thread_timings) const override;

  [[nodiscard]] std::string GetName() const override {
    return "hybrid";
  }
  [[nodiscard]] std::string GetDescription() const override {
    return "Hybrid: Initial large chunks + dynamic small chunks (recommended)";
  }
};

[[nodiscard]] std::vector<std::future<std::vector<SearchResultData>>>
HybridStrategy::LaunchSearchTasks(const ISearchableIndex& index, const ISearchExecutor& executor,
                                  size_t total_items, int thread_count,
                                  const SearchContext& context,
                                  std::vector<ThreadTiming>* thread_timings) const {
  std::vector<std::future<std::vector<SearchResultData>>> futures;
  futures.reserve(static_cast<size_t>(thread_count));

  // Phase 1: Calculate initial chunk distribution
  // Convert percentage to absolute number of items
  const size_t initial_work_items = (total_items * static_cast<size_t>(context.hybrid_initial_percent)) / 100;
  // Divide initial work equally among threads
  const size_t initial_chunk_size = (initial_work_items + static_cast<size_t>(thread_count) - 1) / static_cast<size_t>(thread_count);
  // Calculate where initial chunks end (may exceed total_items, so clamp)
  const size_t initial_chunks_end = (std::min)(initial_chunk_size * static_cast<size_t>(thread_count), total_items);

  // Phase 2: Set up dynamic chunk allocation
  // Use shared_ptr to ensure atomic counter lives as long as the lambdas need it
  // Start dynamic chunks after initial chunks are complete
  auto next_dynamic_chunk = std::make_shared<std::atomic<size_t>>(initial_chunks_end);

  if (!load_balancing_detail::ValidateThreadPool(executor, "HybridStrategy")) {
    return futures;  // Return empty futures vector
  }
  SearchThreadPool& thread_pool = executor.GetThreadPool();

  // Launch one task per thread, each processing an initial chunk then competing for dynamic chunks
  for (int t = 0; t < thread_count; ++t) {
    // Calculate initial chunk boundaries for this thread
    const size_t start_index = static_cast<size_t>(t) * initial_chunk_size;
    // Last thread gets any remainder to ensure we don't exceed total_items
    const size_t end_index =
      (t == thread_count - 1) ? initial_chunks_end : start_index + initial_chunk_size;

    // Skip threads with empty initial chunks (shouldn't happen, but be safe)
    if (start_index >= end_index) {
      continue;
    }

    futures.push_back(thread_pool.Enqueue([&index, start_index, end_index, thread_idx = t,  // NOLINT(bugprone-exception-escape) - exceptions propagate to std::future
                                           total_items, initial_chunks_end, next_dynamic_chunk,
                                           context, thread_timings, thread_count]() {
      // Use dynamic_chunk_size as min chunk size for guided scheduling
      const size_t min_chunk_size = context.dynamic_chunk_size;
      return load_balancing_detail::ExecuteHybridStrategyTask(
        {index, start_index, end_index, thread_idx, total_items, initial_chunks_end,
         next_dynamic_chunk, context, thread_timings, thread_count, min_chunk_size});
    }));
  }

  return futures;
}

// ============================================================================
// Dynamic Strategy (Initial Chunks + Guided Dynamic Scheduling)
// ============================================================================

/**
 * Dynamic Strategy with Initial Chunks
 *
 * Combines the benefits of Static and Dynamic strategies:
 * 1. Each thread gets an initial chunk (reduces contention, improves cache locality)
 * 2. Remaining work is distributed dynamically with guided scheduling
 *
 * Algorithm:
 * Phase 1 - Initial Chunks:
 *   1. Divide work into initial chunks (one per thread, ~50% of total work)
 *   2. Each thread processes its assigned initial chunk (no contention)
 *
 * Phase 2 - Dynamic Scheduling:
 *   3. Remaining work is claimed dynamically via atomic fetch_add
 *   4. Guided scheduling: chunk size decreases as work depletes
 *   5. Minimum chunk size = dynamic_chunk_size from settings
 *
 * Characteristics:
 * - Pros: Excellent load balance + good cache locality for initial work
 * - Cons: Slightly more complex than pure dynamic
 *
 * Best for:
 * - General-purpose use (recommended default)
 * - Datasets with potential clustering (e.g., files sorted by directory)
 * - When you want both performance and load balance
 *
 * @note Initial chunk size is ~50% of work / thread_count.
 *       This is a fixed ratio optimized for typical file search workloads.
 */
class DynamicStrategy : public LoadBalancingStrategy {
 public:
  [[nodiscard]] std::vector<std::future<std::vector<SearchResultData>>> LaunchSearchTasks(
    const ISearchableIndex& index, const ISearchExecutor& executor, size_t total_items,
    int thread_count, const SearchContext& context,
    std::vector<ThreadTiming>* thread_timings) const override;

  [[nodiscard]] std::string GetName() const override {
    return "dynamic";
  }
  [[nodiscard]] std::string GetDescription() const override {
    return "Dynamic: Initial chunks + guided dynamic scheduling (recommended)";
  }
};

[[nodiscard]] std::vector<std::future<std::vector<SearchResultData>>>
DynamicStrategy::LaunchSearchTasks(const ISearchableIndex& index, const ISearchExecutor& executor,
                                   size_t total_items, int thread_count,
                                   const SearchContext& context,
                                   std::vector<ThreadTiming>* thread_timings) const {
  std::vector<std::future<std::vector<SearchResultData>>> futures;
  futures.reserve(static_cast<size_t>(thread_count));

  // ========== Phase 1: Calculate Initial Chunk Distribution ==========
  // Give each thread ~50% of the work upfront (reduces initial contention)
  // The remaining 50% is handled dynamically for load balancing
  constexpr int k_initial_work_percent = 50;
  const size_t initial_work_items = (total_items * k_initial_work_percent) / 100;
  const size_t initial_chunk_size = (initial_work_items + static_cast<size_t>(thread_count) - 1) / static_cast<size_t>(thread_count);

  // Calculate where initial chunks end (dynamic chunks start here)
  const size_t initial_chunks_end = initial_chunk_size * static_cast<size_t>(thread_count);
  const size_t effective_initial_chunks_end = (std::min)(initial_chunks_end, total_items);

  // ========== Phase 2: Setup Dynamic Chunk Allocation ==========
  // dynamic_chunk_size acts as the minimum chunk size for guided scheduling
  const size_t min_chunk_size = context.dynamic_chunk_size;
  // Atomic counter starts AFTER initial chunks (threads compete for remaining work)
  auto next_chunk_start = std::make_shared<std::atomic<size_t>>(effective_initial_chunks_end);

  if (!load_balancing_detail::ValidateThreadPool(executor, "DynamicStrategy")) {
    return futures;  // Return empty futures vector
  }
  SearchThreadPool& thread_pool = executor.GetThreadPool();

  for (int t = 0; t < thread_count; ++t) {
    // Calculate initial chunk boundaries for this thread
    size_t my_initial_start = static_cast<size_t>(t) * initial_chunk_size;
    size_t my_initial_end =
      (t == thread_count - 1) ? effective_initial_chunks_end : my_initial_start + initial_chunk_size;

    // Clamp to valid range
    my_initial_start = (std::min)(my_initial_start, total_items);
    my_initial_end = (std::min)(my_initial_end, total_items);

    futures.push_back(thread_pool.Enqueue([&index, t, total_items, effective_initial_chunks_end,  // NOLINT(bugprone-exception-escape) - exceptions propagate to std::future
                                           my_initial_start, my_initial_end, next_chunk_start,
                                           min_chunk_size, context, thread_timings, thread_count]() {
      return load_balancing_detail::ExecuteDynamicStrategyTask(
        {index, t, total_items, effective_initial_chunks_end, my_initial_start, my_initial_end,
         next_chunk_start, min_chunk_size, context, thread_timings, thread_count});
    }));
  }

  return futures;
}

// ============================================================================
// Interleaved Chunking Strategy
// ============================================================================

/**
 * Interleaved Chunking Strategy Implementation
 *
 * Distributes work by dividing items into small sub-chunks and assigning
 * them to threads in a round-robin interleaved pattern. This provides
 * deterministic chunk assignment without requiring atomic operations.
 *
 * Algorithm:
 * 1. Divide total_items into sub_chunks of size dynamic_chunk_size
 * 2. Thread t processes chunks: t, t+thread_count, t+2*thread_count, ...
 * 3. Each thread processes its assigned chunks sequentially
 *
 * Example with 3 threads and 9 chunks:
 * - Thread 0: chunks 0, 3, 6
 * - Thread 1: chunks 1, 4, 7
 * - Thread 2: chunks 2, 5, 8
 *
 * Characteristics:
 * - Pros: Deterministic, no atomic operations, good load balance
 * - Cons: Less cache locality than static (chunks are spread out)
 *
 * Best for:
 * - When you want deterministic behavior without atomics
 * - Scenarios with moderate work distribution variance
 * - When sub-chunk size can be tuned for optimal performance
 *
 * @note Uses dynamic_chunk_size for sub-chunk size, with fallback to 256
 *       if dynamic_chunk_size < 100.
 */
[[nodiscard]] std::vector<std::future<std::vector<SearchResultData>>>
InterleavedChunkingStrategy::LaunchSearchTasks(
  const ISearchableIndex& index, const ISearchExecutor& executor, size_t total_items,
  int thread_count, const SearchContext& context,
  std::vector<ThreadTiming>* thread_timings) const {
  std::vector<std::future<std::vector<SearchResultData>>> futures;
  futures.reserve(static_cast<size_t>(thread_count));

  // Validate thread pool before proceeding
  if (!load_balancing_detail::ValidateThreadPool(executor, "InterleavedChunkingStrategy")) {
    return futures;  // Return empty futures vector
  }
  SearchThreadPool& thread_pool = executor.GetThreadPool();

  // Calculate sub-chunk size: use dynamic_chunk_size if reasonable, otherwise default
  // Small chunks (< 100) would create too many chunks and overhead
  constexpr size_t k_min_reasonable_chunk_size = 100;
  constexpr size_t k_default_sub_chunk_size = 256;
  const size_t sub_chunk_size = (context.dynamic_chunk_size >= k_min_reasonable_chunk_size) ? context.dynamic_chunk_size : k_default_sub_chunk_size;
  // Calculate number of sub-chunks using ceiling division
  const size_t num_sub_chunks = (total_items + sub_chunk_size - 1) / sub_chunk_size;

  for (int t = 0; t < thread_count; ++t) {
    futures.push_back(thread_pool.Enqueue([&index, t, total_items, thread_count, sub_chunk_size,  // NOLINT(bugprone-exception-escape) - exceptions propagate to std::future
                                           num_sub_chunks, context, thread_timings]() {
      return load_balancing_detail::ExecuteInterleavedStrategyTask(
        {index, t, total_items, thread_count, sub_chunk_size, num_sub_chunks, context,
         thread_timings});
    }));
  }

  return futures;
}

// ============================================================================
// Factory Implementation
// ============================================================================

// ValidateAndNormalizeStrategyName implementation (moved out of namespace for public access)
std::string ValidateAndNormalizeStrategyName(std::string_view strategy_name) {
  if (strategy_name == "static" || strategy_name == "hybrid" || strategy_name == "dynamic" ||
      strategy_name == "interleaved") {
    return std::string(strategy_name);
  }
#ifdef FAST_LIBS_BOOST
  if (strategy_name == "work_stealing") {
    return std::string(strategy_name);
  }
#endif  // FAST_LIBS_BOOST

  // Invalid value: log warning and return default (avoid #ifdef inside macro - SonarQube)
#ifdef FAST_LIBS_BOOST
  LOG_WARNING_BUILD("Invalid load balancing strategy: '"
                    << strategy_name
                    << "'. Valid options are: 'static', 'hybrid', 'dynamic', "
                       "'interleaved', 'work_stealing'. Using default (hybrid).");
#else
  LOG_WARNING_BUILD("Invalid load balancing strategy: '"
                    << strategy_name
                    << "'. Valid options are: 'static', 'hybrid', 'dynamic', "
                       "'interleaved'. Using default (hybrid).");
#endif  // FAST_LIBS_BOOST
  return GetDefaultStrategyName();
}

/**
 * Factory function to create a load balancing strategy instance.
 *
 * Creates a new instance of the specified strategy. If the strategy name is
 * unknown, logs a warning and returns the default strategy (hybrid).
 *
 * @param strategy_name Name of the strategy ("static", "hybrid", "dynamic", "interleaved")
 * @return Unique pointer to the strategy instance. Never returns nullptr.
 *         Returns hybrid strategy (default) if name is invalid.
 *
 * @see GetAvailableStrategyNames() for list of valid strategy names
 * @see ValidateAndNormalizeStrategyName() for validation logic
 */
std::unique_ptr<LoadBalancingStrategy> CreateLoadBalancingStrategy(std::string_view strategy_name) {
  // Use centralized validation to eliminate duplication
  const std::string validated_name = ValidateAndNormalizeStrategyName(strategy_name);

  // Create strategy based on validated name
  if (validated_name == "static") {  // NOSONAR(cpp:S6004) - validated_name is used multiple times in this if-else chain; init-statement would not be appropriate
    return std::make_unique<StaticChunkingStrategy>();
  }
  if (validated_name == "hybrid") {
    return std::make_unique<HybridStrategy>();
  }
  if (validated_name == "dynamic") {
    return std::make_unique<DynamicStrategy>();
  }
  if (validated_name == "work_stealing") {
    return std::make_unique<WorkStealingStrategy>();
  }
  if (validated_name == "interleaved") {
    return std::make_unique<InterleavedChunkingStrategy>();
  }

  // Should never reach here (ValidateAndNormalizeStrategyName ensures valid name),
  // but provide fallback for safety
  return std::make_unique<HybridStrategy>();
}

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
std::vector<std::string> GetAvailableStrategyNames() {
#ifdef FAST_LIBS_BOOST
  return {"static", "hybrid", "dynamic", "interleaved", "work_stealing"};
#else
  return {"static", "hybrid", "dynamic", "interleaved"};
#endif  // FAST_LIBS_BOOST
}

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
std::string GetDefaultStrategyName() {
  return "hybrid";
}

// ============================================================================
// Work Stealing Strategy (Requires Boost)
// ============================================================================

#ifdef FAST_LIBS_BOOST

namespace work_stealing_detail {

// Task representing a chunk of work
struct WorkChunk {
  size_t start_index;
  size_t end_index;
};

// Queue capacity - power of 2 for lockfree queue
constexpr size_t kQueueCapacity = 1024;

// Per-thread work queue type
using WorkQueue = boost::lockfree::queue<WorkChunk, boost::lockfree::capacity<kQueueCapacity>>;

/**
 * @brief Thread-local worker for Work Stealing Strategy
 */
inline std::vector<SearchResultData> ExecuteWorkStealingTask(
  const ISearchableIndex& index, int thread_idx, int thread_count,
  const std::vector<std::unique_ptr<WorkQueue>>& all_queues,  // NOLINT(hicpp-named-parameter,readability-named-parameter) - used via indexing
  const SearchContext& context,
  std::vector<ThreadTiming>* thread_timings) {
  const auto start_time = std::chrono::high_resolution_clock::now();
  std::vector<SearchResultData> local_results;
  constexpr size_t k_heuristic_capacity = 1024;
  local_results.reserve(k_heuristic_capacity);

  // CRITICAL: Acquire shared_lock
  const std::shared_lock lock(index.GetMutex());

  // Setup thread work (SoA view, matchers)
  const auto setup = load_balancing_detail::SetupThreadWorkAfterLock(index, context);

  // Statistics
  size_t items_processed = 0;
  size_t chunks_processed = 0;
  size_t bytes_processed = 0;

  WorkQueue* my_queue = all_queues[static_cast<size_t>(thread_idx)].get();
  WorkChunk chunk{};

  // 1. Process local queue first
  while (my_queue->pop(chunk)) {
    load_balancing_detail::ProcessChunkWithExceptionHandling(
      {setup, chunk.start_index, chunk.end_index, thread_idx, "WorkStealingStrategy", "local"},
      local_results, context);

    items_processed += (chunk.end_index - chunk.start_index);
    chunks_processed++;
    if (thread_timings != nullptr) {
      bytes_processed += ParallelSearchEngine::CalculateChunkBytes(
        setup.soa_view_, chunk.start_index, chunk.end_index, setup.storage_size_);
    }
  }

  // 2. Steal from others
  // Simple random stealing or round-robin
  for (int i = 1; i < thread_count; ++i) {
    const int victim_idx = (thread_idx + i) % thread_count;
    WorkQueue* victim_queue = all_queues[static_cast<size_t>(victim_idx)].get();

    // Try to steal until empty
    while (victim_queue->pop(chunk)) {
      load_balancing_detail::ProcessChunkWithExceptionHandling(
        {setup, chunk.start_index, chunk.end_index, thread_idx, "WorkStealingStrategy", "stolen"},
        local_results, context);

      items_processed += (chunk.end_index - chunk.start_index);
      chunks_processed++;
      if (thread_timings != nullptr) {
        bytes_processed += ParallelSearchEngine::CalculateChunkBytes(
          setup.soa_view_, chunk.start_index, chunk.end_index, setup.storage_size_);
      }
    }
  }

  load_balancing_detail::RecordThreadTimingIfRequested(
    thread_timings, thread_idx, start_time,
    {0, 0, 0, chunks_processed, items_processed, bytes_processed, local_results.size(),
     &setup.soa_view_, setup.storage_size_, 0, 0});

  return local_results;
}

}  // namespace work_stealing_detail

[[nodiscard]] std::vector<std::future<std::vector<SearchResultData>>>
WorkStealingStrategy::LaunchSearchTasks(
  const ISearchableIndex& index, const ISearchExecutor& executor, size_t total_items,
  int thread_count, const SearchContext& context,
  std::vector<ThreadTiming>* thread_timings) const {
  std::vector<std::future<std::vector<SearchResultData>>> futures;

  if (!load_balancing_detail::ValidateThreadPool(executor, "WorkStealingStrategy")) {
    return futures;
  }

  SearchThreadPool& thread_pool = executor.GetThreadPool();
  futures.reserve(static_cast<size_t>(thread_count));

  // Create queues
  // Note: Vector of unique_ptrs to avoid move/copy issues with lockfree::queue
  // and ensure pointer stability
  auto all_queues =
    std::make_shared<std::vector<std::unique_ptr<work_stealing_detail::WorkQueue>>>();
  all_queues->reserve(static_cast<size_t>(thread_count));
  for (int i = 0; i < thread_count; ++i) {
    all_queues->push_back(std::make_unique<work_stealing_detail::WorkQueue>());
  }

  // Distribute work chunks (Round Robin)
  // Use relatively small chunks to allow for stealing fine-granularity
  constexpr size_t k_default_chunk_size = 1024;
  const size_t chunk_size = context.dynamic_chunk_size > 0 ? context.dynamic_chunk_size : k_default_chunk_size;
  size_t current_idx = 0;
  int queue_idx = 0;

  while (current_idx < total_items) {
    const size_t end = (std::min)(current_idx + chunk_size, total_items);
    all_queues->at(static_cast<size_t>(queue_idx))->push({current_idx, end});
    current_idx = end;
    queue_idx = (queue_idx + 1) % thread_count;
  }

  // Launch threads
  for (int t = 0; t < thread_count; ++t) {
    futures.push_back(thread_pool.Enqueue(
      [&index, thread_idx = t, thread_count, all_queues, context, thread_timings]() {
        return work_stealing_detail::ExecuteWorkStealingTask(index, thread_idx, thread_count,
                                                             *all_queues, context, thread_timings);
      }));
  }

  return futures;
}

#else

// Fallback implementation when Boost is not available
[[nodiscard]] std::vector<std::future<std::vector<SearchResultData>>>
WorkStealingStrategy::LaunchSearchTasks(const ISearchableIndex& /*unused*/, const ISearchExecutor& /*unused*/, size_t /*unused*/,
                                        int /*unused*/, const SearchContext& /*unused*/,
                                        std::vector<ThreadTiming>* /*unused*/) const {
  LOG_ERROR("WorkStealingStrategy selected but application was built without FAST_LIBS_BOOST.");
  return {};  // Should not happen if factory logic works correctly
}

#endif  // FAST_LIBS_BOOST
