/**
 * @file LoadBalancingStrategy.cpp
 * @brief Implementation of the hybrid load balancing strategy for parallel search
 *
 * Implements HybridStrategy: assigns initial large chunks upfront per thread,
 * then uses dynamic small chunks for remaining work. Provides good cache
 * locality for the initial work while maintaining excellent load balance.
 *
 * THREAD SAFETY:
 * - Uses SearchThreadPool for thread management
 * - Atomic operations for cancellation and progress tracking
 * - Shared locks for FileIndex access during search
 *
 * @see LoadBalancingStrategy.h for HybridStrategy declaration
 * @see FileIndex.cpp for strategy usage in search operations
 */

#include "utils/LoadBalancingStrategy.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <future>
#include <memory>
#include <new>
#include <shared_mutex>
#include <stdexcept>

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
 * - Reuses pre-built pattern matchers from search startup
 *
 * This helper eliminates code duplication across all load balancing strategies.
 * Note: The lock must be acquired before calling this function.
 *
 * @param index The searchable index (lock must already be held)
 * @param matchers Pre-built pattern matchers (one per search, shared across workers)
 * @return ThreadSetupAfterLock struct with initialized data
 */
[[nodiscard]] inline ThreadSetupAfterLock SetupThreadWorkAfterLock(
  const ISearchableIndex& index,
  const ParallelSearchEngine::PatternMatchers& matchers) {
  ThreadSetupAfterLock setup;

  // Get SoA view after acquiring lock (pointers are only valid while lock is held)
  setup.soa_view_ = index.GetSearchableView();
  setup.storage_size_ = index.GetStorageSize();

  // Reuse matchers built once per search (shared across all workers).
  setup.matchers_ = matchers;

  return setup;
}

// Reserve before any index lock (matches pre-refactor worker order; avoids heap work under lock).
[[nodiscard]] inline std::vector<SearchResultData> MakeReservedWorkerResultBuffer(
  size_t chunk_start, size_t chunk_end) {
  std::vector<SearchResultData> local_results;
  constexpr size_t k_match_rate_heuristic = 20;
  local_results.reserve((chunk_end - chunk_start) / k_match_rate_heuristic);
  return local_results;
}

/**
 * RAII: worker-thread timing start, result buffer, index shared_lock, and post-lock setup.
 *
 * Member order + init list order matter: local_results must be fully reserved before index_lock
 * so we do not allocate while holding the FileIndex shared mutex.
 */
struct WorkerSearchChunkContext {
  std::chrono::high_resolution_clock::time_point start_time_{std::chrono::high_resolution_clock::now()}; // NOLINT(readability-identifier-naming)
  std::vector<SearchResultData> local_results; // NOLINT(readability-identifier-naming)
  std::shared_lock<std::shared_mutex> index_lock; // NOLINT(readability-identifier-naming)
  ThreadSetupAfterLock setup; // NOLINT(readability-identifier-naming)

  WorkerSearchChunkContext(const ISearchableIndex& index,
                           const ParallelSearchEngine::PatternMatchers& matchers,
                           size_t chunk_start, size_t chunk_end)
      : local_results(MakeReservedWorkerResultBuffer(chunk_start, chunk_end)),
        index_lock(index.GetMutex()),
        setup(SetupThreadWorkAfterLock(index, matchers)) {}
};

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
  const ThreadSetupAfterLock& setup; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members,readability-identifier-naming)
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
  const ThreadSetupAfterLock& setup; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members,readability-identifier-naming)
  size_t total_items_ = 0; // NOLINT(readability-identifier-naming)
  size_t initial_chunks_end_ = 0; // NOLINT(readability-identifier-naming)
  std::atomic<size_t>* next_chunk_start_ = nullptr; // NOLINT(readability-identifier-naming)
  const SearchContext& context; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members,readability-identifier-naming)
  std::vector<SearchResultData>& local_results; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members,readability-identifier-naming)
  int thread_idx_ = 0; // NOLINT(readability-identifier-naming)
  const char* strategy_name_ = nullptr; // NOLINT(readability-identifier-naming)
  int thread_count_ = 0; // NOLINT(readability-identifier-naming)
  size_t min_chunk_size_ = 0; // NOLINT(readability-identifier-naming)
};

/**
 * @brief Dynamic chunks loop output parameters
 */
struct DynamicChunksLoopOutput {
  size_t& dynamic_chunks_count; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members,readability-identifier-naming)
  size_t& dynamic_items_total; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members,readability-identifier-naming)
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
    if (params.context.cancel_flag != nullptr && params.context.cancel_flag->load()) {
      should_continue = false;
      continue;
    }

    // Calculate chunk size (guided scheduling if thread_count > 0, otherwise use
    // context.dynamic_chunk_size)
    size_t current_chunk_size = params.context.dynamic_chunk_size;
    if (params.thread_count_ > 0 && params.min_chunk_size_ > 0) {
      // Guided Scheduling: Calculate chunk size dynamically based on remaining items.
      // Formula: remaining / (guided_scheduling_divisor * thread_count), floored at min_chunk_size.
      const size_t current_processed = params.next_chunk_start_->load();
      const size_t remaining =
        (params.total_items_ > current_processed) ? (params.total_items_ - current_processed) : 0;
      const auto gsd = static_cast<size_t>(params.context.guided_scheduling_divisor);
      const size_t divisor = gsd * static_cast<size_t>(params.thread_count_);
      const size_t calculated_size = remaining / divisor;
      current_chunk_size = (std::max)(params.min_chunk_size_, calculated_size);
    }

    // Atomically claim the next chunk with the calculated size
    const size_t chunk_start =
      params.next_chunk_start_->fetch_add(current_chunk_size);

    // Stop if we've claimed beyond the end of available work
    if (chunk_start >= params.total_items_) {
      should_continue = false;
      continue;
    }

    // Calculate chunk end, ensuring we don't exceed live SoA size (total_items may be
    // a stale pre-compaction sample; setup.soa_view_ is authoritative under this lock).
    const size_t live_size = params.setup.soa_view_.size;
    const size_t chunk_end =
        (std::min)({chunk_start + current_chunk_size, params.total_items_, live_size});

    // Process the dynamic chunk
    if (chunk_end <= chunk_start || chunk_start >= live_size) {
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
 * @brief Hybrid strategy task parameters
 */
struct HybridStrategyTaskParams {
  const ISearchableIndex& index; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members,readability-identifier-naming)
  size_t start_index_ = 0; // NOLINT(readability-identifier-naming)
  size_t end_index_ = 0; // NOLINT(readability-identifier-naming)
  int thread_idx_ = 0; // NOLINT(readability-identifier-naming)
  size_t total_items_ = 0; // NOLINT(readability-identifier-naming)
  size_t initial_chunks_end_ = 0; // NOLINT(readability-identifier-naming)
  const std::shared_ptr<std::atomic<size_t>>& next_dynamic_chunk; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members,readability-identifier-naming)
  const SearchContext& context; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members,readability-identifier-naming)
  const std::shared_ptr<const ParallelSearchEngine::PatternMatchers>& shared_matchers_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
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
  WorkerSearchChunkContext worker(params.index, *params.shared_matchers_, params.start_index_,
                                  params.end_index_);

  // total_items was sampled under a short lock before enqueue; Maintain/RebuildPathBuffer
  // may have compacted the SoA since then. Clamp all work bounds to the live view size
  // so ValidateChunkRange does not reject whole chunks (silent result loss).
  const size_t live_size = worker.setup.soa_view_.size;
  const size_t effective_start = (std::min)(params.start_index_, live_size);
  const size_t effective_end = (std::min)(params.end_index_, live_size);
  const size_t effective_total = (std::min)(params.total_items_, live_size);
  const size_t effective_initial_end = (std::min)(params.initial_chunks_end_, live_size);

  if (effective_start < effective_end) {
    load_balancing_detail::ProcessChunkWithExceptionHandling(
      {worker.setup, effective_start, effective_end, params.thread_idx_, "HybridStrategy",
       "initial chunk"},
      worker.local_results, params.context);
  }

  // Track statistics for timing information
  const size_t initial_items = effective_end - effective_start;
  size_t dynamic_chunks_count = 0;
  size_t dynamic_items_total = 0;

  // Phase 2: Process remaining work using dynamic chunk allocation
  DynamicChunksLoopOutput dynamic_output{dynamic_chunks_count, dynamic_items_total};  // NOLINT(misc-const-correctness) - passed by non-const ref to ProcessDynamicChunksLoop
  load_balancing_detail::ProcessDynamicChunksLoop(
    {worker.setup, effective_total, effective_initial_end, params.next_dynamic_chunk.get(),
     params.context, worker.local_results, params.thread_idx_, "HybridStrategy", params.thread_count_,
     params.min_chunk_size_},
    dynamic_output);

  load_balancing_detail::RecordThreadTimingIfRequested(
    params.thread_timings_, params.thread_idx_, worker.start_time_,
    {effective_start, effective_end, initial_items, dynamic_chunks_count,
     initial_items + dynamic_items_total, 0, worker.local_results.size(), &worker.setup.soa_view_,
     worker.setup.storage_size_, effective_start, effective_end});

  return worker.local_results;
}

/**
 * Runs hybrid worker body with top-level exception translation (keeps Enqueue lambda short; Sonar S1188).
 * Takes HybridStrategyTaskParams (Sonar S107: max 7 parameters per function).
 */
[[nodiscard]] inline std::vector<SearchResultData> ExecuteHybridStrategyTaskWithEnqueueExceptions(
    const HybridStrategyTaskParams& params) {
  try {
    return ExecuteHybridStrategyTask(params);
  } catch (const std::bad_alloc& e) {
    (void)e;
    LOG_ERROR_BUILD("HybridStrategy: Memory allocation failure in worker thread "
                    << params.thread_idx_ << ", range [" << params.start_index_ << "-" << params.end_index_
                    << "]");
    return std::vector<SearchResultData>{};
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - catch-all for setup exceptions (WorkerSearchChunkContext ctor, CreatePatternMatchers)
    LOG_ERROR_BUILD("HybridStrategy: Exception in worker thread "
                    << params.thread_idx_ << ", range [" << params.start_index_ << "-" << params.end_index_
                    << "]: " << e.what());
    return std::vector<SearchResultData>{};
  } catch (...) {  // NOSONAR(cpp:S2738) - unknown exceptions must not escape worker lambdas
    LOG_ERROR_BUILD("HybridStrategy: Unknown exception in worker thread "
                    << params.thread_idx_ << ", range [" << params.start_index_ << "-" << params.end_index_
                    << "]");
    return std::vector<SearchResultData>{};
  }
}

}  // namespace load_balancing_detail

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
 * - Atomic fetch_add for dynamic chunk claims (lock-free work stealing)
 *
 * Best for:
 * - General-purpose use
 * - Scenarios with mixed work distribution
 * - When you want both performance and load balance
 */
[[nodiscard]] std::vector<std::future<std::vector<SearchResultData>>>
HybridStrategy::LaunchSearchTasks(  // NOLINT(readability-function-cognitive-complexity) - try-catch in per-thread lambda adds nesting; further splitting would obscure lambda capture intent
    const ISearchableIndex& index, const ISearchExecutor& executor,
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

  // Single shared copy for all workers (same lifetime/duplication rationale as SearchAsync).
  const std::shared_ptr<const SearchContext> shared_context =
      std::make_shared<SearchContext>(context);
  const std::shared_ptr<const ParallelSearchEngine::PatternMatchers> shared_matchers =
      std::make_shared<ParallelSearchEngine::PatternMatchers>(
          ParallelSearchEngine::CreatePatternMatchers(context));

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

    futures.push_back(thread_pool.Enqueue([&index, start_index, end_index, thread_idx = t,  // NOLINT(bugprone-exception-escape) - task exceptions caught below; residual from LOG stream ops in catch blocks (OOM scenario)
                                           total_items, initial_chunks_end, next_dynamic_chunk,
                                           shared_context, shared_matchers, thread_timings, thread_count]() {
      const load_balancing_detail::HybridStrategyTaskParams params{
          index, start_index, end_index, thread_idx, total_items, initial_chunks_end, next_dynamic_chunk,
          *shared_context, shared_matchers, thread_timings, thread_count, shared_context->dynamic_chunk_size};
      return load_balancing_detail::ExecuteHybridStrategyTaskWithEnqueueExceptions(params);
    }));
  }

  return futures;
}

