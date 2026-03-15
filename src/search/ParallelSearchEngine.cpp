#include "search/ParallelSearchEngine.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <string_view>
#include <thread>
#include <utility>

#include "index/ISearchableIndex.h"
#include "path/PathStorage.h"
#include "search/SearchContext.h"
#include "search/SearchPatternUtils.h"
#include "search/SearchStatisticsCollector.h"
#include "search/SearchThreadPool.h"
#include "utils/HashMapAliases.h"
#include "utils/LoadBalancingStrategy.h"
#include "utils/Logger.h"
#include "utils/StringSearch.h"
#include "utils/StringUtils.h"
#include "utils/ThreadUtils.h"

// Constructor - thread_pool_ initialized in initializer list
// NOLINTNEXTLINE(readability-identifier-naming,cppcoreguidelines-pro-type-member-init,hicpp-member-init) - threadPool matches API; member-init: thread_pool_ set below
ParallelSearchEngine::ParallelSearchEngine(std::shared_ptr<SearchThreadPool> threadPool)
    : thread_pool_(std::move(threadPool)) {
  if (!thread_pool_) {
    LOG_ERROR_BUILD("ParallelSearchEngine: thread pool is null");
  }
}

// SearchAsync implementation
std::vector<std::future<std::vector<uint64_t>>>
ParallelSearchEngine::SearchAsync(const ISearchableIndex& index,
                                  [[maybe_unused]] std::string_view query,
                                  int thread_count,
                                  const SearchContext& context,
                                  SearchStats* stats) const {
  // Record start time for duration calculation
  [[maybe_unused]] auto start_time = std::chrono::steady_clock::now();  // NOSONAR(cpp:S1854) - Reserved for future duration calculation

  // CRITICAL: Acquire shared_lock to ensure:
  // 1. Vector sizes remain stable when calculating chunk ranges
  // 2. Proper memory visibility when creating futures
  // 3. Each worker thread acquires its own shared_lock before accessing arrays.
  // Do not perform I/O or heavy work while holding this lock (see LOCK_ORDERING doc).
  const std::shared_lock lock(index.GetMutex());

  std::vector<std::future<std::vector<uint64_t>>> futures;
  const size_t total_items = index.GetTotalItems();
  if (total_items == 0) {
    // Update stats even for empty index
    if (stats != nullptr) {
      stats->num_threads_used_ = 0;
      stats->total_items_scanned_ = 0;
      stats->total_matches_found_ = 0;
      stats->duration_milliseconds_ = 0.0;
    }
    return futures;
  }

  // Determine optimal thread count
  const size_t total_bytes = index.GetStorageSize();
  thread_count = DetermineThreadCount(thread_count, total_bytes,
                                       context.search_thread_pool_size);
  thread_count = (std::min)(thread_count, static_cast<int>(total_items));  // NOSONAR(cpp:S1905) - Required cast: std::min needs same types, convert size_t to int
  thread_count = (std::max)(thread_count, 1);

  const size_t chunk_size = (total_items + thread_count - 1) / thread_count;

  // Phase 2: Pre-compile PathPattern patterns once before threads start
  // (This is already done in SearchContext if patterns were pre-compiled)
  // We'll use the patterns from context if available

  // Phase 3: Launch parallel search threads and return futures immediately
  futures.reserve(thread_count);

  if (!thread_pool_) {
    LOG_ERROR_BUILD("SearchAsync: Thread pool is null - cannot execute search");
    if (stats != nullptr) {
      stats->num_threads_used_ = 0;
      stats->total_items_scanned_ = total_items;
      stats->total_matches_found_ = 0;
      stats->duration_milliseconds_ = 0.0;
    }
    return futures;
  }

  // Note: Reference is non-const because Enqueue() is a non-const method that modifies
  // the thread pool's internal state (adds tasks to queue). This is intentional and
  // necessary for the thread pool to function correctly.
  SearchThreadPool& pool = *thread_pool_;
  if (pool.GetThreadCount() == 0) {  // NOSONAR(cpp:S6004) - Variable used after if block (line 235 and later)
    LOG_ERROR_BUILD("SearchAsync: Thread pool has 0 threads - cannot execute search");
    if (stats != nullptr) {
      stats->num_threads_used_ = 0;
      stats->total_items_scanned_ = total_items;
      stats->total_matches_found_ = 0;
      stats->duration_milliseconds_ = 0.0;
    }
    return futures;
  }

  for (int t = 0; t < thread_count; ++t) {
    const size_t start_index = t * chunk_size;
    const size_t end_index = (std::min)(start_index + chunk_size, total_items);

    if (start_index >= end_index) {
      break;
    }

    // Launch task via thread pool for reuse and better control
    futures.push_back(pool.Enqueue(
        [&index, start_index, end_index, context]() {  // NOLINT(bugprone-exception-escape) - exceptions propagate to std::future
          // CRITICAL: Acquire shared_lock in worker thread to prevent race
          // conditions. This ensures Insert/Remove operations block while this
          // worker is reading arrays
          const std::shared_lock lock(index.GetMutex());

          // Get SoA view after acquiring lock (pointers are only valid while lock is held)
          const PathStorage::SoAView soaView = index.GetSearchableView();
          const size_t storage_size = index.GetStorageSize();

          std::vector<uint64_t> local_results;
          // Heuristic: assume up to ~20% of items may match in typical queries
          // Use a minimum to avoid reserve(0) and keep reallocations low.
          const size_t items_in_chunk = end_index - start_index;
          const size_t initial_capacity = (std::max)(items_in_chunk / 5, size_t{16}); // 20% hit rate heuristic, minimum 16
          local_results.reserve(initial_capacity);

          // Create pattern matchers once per thread (optimization)
          auto matchers = CreatePatternMatchers(context);

          // Use optimized overload that accepts pre-created matchers
          ProcessChunkRangeIds(soaView, start_index, end_index, local_results,
                               context, storage_size, matchers.filename_matcher, matchers.path_matcher);

          return local_results;
        }));
  }

  // NOTE: The shared_lock acquired at the start of this function is released
  // when this function returns, but each worker thread lambda acquires its own
  // shared_lock before calling ProcessChunkRangeIds. This ensures:
  // 1. Each worker thread holds a lock while reading arrays
  // 2. Insert/Remove operations (which require unique_lock) will block until
  //    all worker threads release their shared_locks
  // 3. This prevents race conditions where arrays could be modified while
  //    workers are still reading them

  // Update stats if provided
  // Note: total_matches_found_ and duration_milliseconds_ will be populated by the caller
  // after collecting results from futures (see FileIndex::SearchAsync)
  if (stats != nullptr) {
    stats->num_threads_used_ = thread_count;
    stats->total_items_scanned_ = total_items;
    // total_matches_found_ and duration_milliseconds_ are populated by caller after collecting results
  }

  return futures;
}

std::vector<std::future<std::vector<SearchResultData>>>
ParallelSearchEngine::SearchAsyncWithData(const ISearchableIndex& index,
                                          [[maybe_unused]] std::string_view query,
                                          int thread_count,
                                          const SearchContext& context,
                                          std::vector<ThreadTiming>* thread_timings,
                                          [[maybe_unused]] const std::atomic<bool>* cancel_flag) const {
  // CRITICAL: Acquire shared_lock (same rules as SearchAsync); no I/O under lock (see LOCK_ORDERING doc).
  const std::shared_lock lock(index.GetMutex());

  std::vector<std::future<std::vector<SearchResultData>>> futures;
  const size_t total_items = index.GetTotalItems();
  if (total_items == 0) {
    return futures;
  }

  // Determine optimal thread count
  const size_t total_bytes = index.GetStorageSize();
  thread_count = DetermineThreadCount(thread_count, total_bytes,
                                       context.search_thread_pool_size);
  thread_count = (std::min)(thread_count, static_cast<int>(total_items));  // NOSONAR(cpp:S1905) - Required cast: std::min needs same types, convert size_t to int
  thread_count = (std::max)(thread_count, 1);

  LOG_INFO_BUILD(
      "Using load balancing strategy: "
      << context.load_balancing_strategy
      << (context.load_balancing_strategy != "static"
              ? (", dynamic chunk size: " + std::to_string(context.dynamic_chunk_size))
              : ""));

  // Prepare thread timings vector if requested
  // Pre-size to thread_count so threads can write to their index safely
  if (thread_timings != nullptr) {
    thread_timings->clear();
    thread_timings->resize(thread_count); // Pre-size for safe indexing
  }

  // Launch parallel search threads with selected load balancing strategy
  auto strategy_ptr = CreateLoadBalancingStrategy(context.load_balancing_strategy);
  if (!strategy_ptr) {
    LOG_ERROR_BUILD(
        "SearchAsyncWithData: CreateLoadBalancingStrategy returned null");
    return futures; // Return empty futures
  }

  // Verify thread pool is ready before launching tasks
  if (!thread_pool_) {
    LOG_ERROR_BUILD("SearchAsyncWithData: Thread pool is null - cannot execute search");
    return futures;
  }

  // Check thread pool is ready (GetThreadCount() is const, so pool can be const reference)
  const SearchThreadPool& pool = *thread_pool_;
  if (pool.GetThreadCount() == 0) {  // NOSONAR(cpp:S6004) - Variable used after if block (line 235 and later)
    LOG_ERROR_BUILD("SearchAsyncWithData: Thread pool has 0 threads - cannot execute search");
    return futures;
  }

  // LoadBalancingStrategy now uses ISearchableIndex and ISearchExecutor
  // ParallelSearchEngine implements ISearchExecutor, so *this can be passed
  futures = strategy_ptr->LaunchSearchTasks(
      index, *this, total_items, thread_count, context, thread_timings);

  // Verify futures were created
  if (futures.empty() && thread_count > 0 && total_items > 0) {
    LOG_ERROR_BUILD(
        "SearchAsyncWithData: LaunchSearchTasks returned empty futures vector "
        << "(thread_count=" << thread_count << ", total_items=" << total_items
        << ")");
  }

  // NOTE: The shared_lock acquired at the start of this function is released
  // when this function returns, but each worker thread lambda acquires its own
  // shared_lock before calling ProcessChunkRange. This ensures:
  // 1. Each worker thread holds a lock while reading arrays
  // 2. Insert/Remove operations (which require unique_lock) will block until
  //    all worker threads release their shared_locks
  // 3. This prevents race conditions where arrays could be modified while
  //    workers are still reading them

  return futures;
}

// Get thread pool
SearchThreadPool& ParallelSearchEngine::GetThreadPool() const {
  if (!thread_pool_) {
    LOG_ERROR_BUILD("ParallelSearchEngine::GetThreadPool: thread_pool_ is null");
    throw std::runtime_error("ParallelSearchEngine: thread pool is null");  // NOSONAR(cpp:S112) - std::runtime_error is appropriate for internal state validation errors
  }
  return *thread_pool_;
}

// Calculate bytes processed for a chunk range
// Implementation moved to SearchStatisticsCollector (now inline in header)

// Record thread timing information
// Implementation moved to SearchStatisticsCollector (now inline in header)

// Helper to determine optimal thread count
int ParallelSearchEngine::DetermineThreadCount(int thread_count, size_t total_bytes,
                                               int search_thread_pool_size_from_context) {
  if (thread_count <= 0) {
    if (search_thread_pool_size_from_context > 0) {
      thread_count = search_thread_pool_size_from_context;
    } else {
      thread_count = static_cast<int>(GetLogicalProcessorCount());
    }
  }

  // Limit by data size (minimum chunk size)
  constexpr size_t kMinChunkBytes = static_cast<size_t>(64) * 1024U;
  if (auto max_threads_by_bytes = static_cast<int>((total_bytes + kMinChunkBytes - 1) / kMinChunkBytes); max_threads_by_bytes > 0 && thread_count > max_threads_by_bytes) {
    thread_count = max_threads_by_bytes;
  }

  return thread_count;
}

// Create pattern matchers from search context
ParallelSearchEngine::PatternMatchers
ParallelSearchEngine::CreatePatternMatchers(const SearchContext& context) {
  PatternMatchers matchers;

  // Set up filename matcher if not in extension-only mode
  if (!context.extension_only_mode) {
    if (context.HasFilenameQuery()) {
      // Use pre-compiled pattern if available (faster), otherwise compile on-the-fly
      if (context.filename_pattern) {
        matchers.filename_matcher = search_pattern_utils::CreateFilenameMatcher(
            context.filename_pattern);
      } else {
        matchers.filename_matcher = search_pattern_utils::CreateFilenameMatcher(
            context.filename_query, context.case_sensitive, context.filename_query_lower);
      }
    }
    // Set up path matcher if path query is specified
    if (context.HasPathQuery()) {
      // Use pre-compiled pattern if available (faster), otherwise compile on-the-fly
      if (context.path_pattern) {
        matchers.path_matcher = search_pattern_utils::CreatePathMatcher(
            context.path_pattern);
      } else {
        matchers.path_matcher = search_pattern_utils::CreatePathMatcher(
            context.path_query, context.case_sensitive, context.path_query_lower);
      }
    }
  }

  return matchers;
}

// ============================================================================
// Helper functions for ProcessChunkRange
// ============================================================================

namespace parallel_search_detail {

// Helper function to check filename and path matchers (inline for hot-path inlining).
// Returns true if all matchers pass (or are not set), false if any matcher fails.
// Filename matcher always receives the filename only (last path segment).
// Path matcher always receives the full path. Extracted to reduce nesting depth (cpp:S134).
inline bool CheckMatchers(const char* path, [[maybe_unused]] size_t path_len, size_t filename_offset,
                   const lightweight_callable::LightweightCallable<bool, const char*>& filename_matcher,
                   const lightweight_callable::LightweightCallable<bool, std::string_view>& path_matcher) {
  if (filename_matcher) {
    const char* filename = path + filename_offset;
    if (!filename_matcher(filename)) {
      return false;
    }
  }
  if (path_matcher) {
    const std::string_view full_path(path, path_len);
    if (!path_matcher(full_path)) {
      return false;
    }
  }
  return true;
}

} // namespace parallel_search_detail

// Helper to validate chunk range for ProcessChunkRangeIds (inline for single call site).
// Returns true if valid, false otherwise.
static inline bool ValidateChunkRangeIds(size_t array_size, size_t chunk_start, size_t chunk_end) {
  if (array_size == 0 || chunk_end > array_size) {
    return false;
  }
  if (chunk_start >= array_size) {
    LOG_WARNING_BUILD("ProcessChunkRangeIds: chunk_start ("
                      << chunk_start << ") >= array_size (" << array_size << ")");
    return false;
  }
  if (chunk_end <= chunk_start) {
    LOG_WARNING_BUILD("ProcessChunkRangeIds: Invalid range ["
                      << chunk_start << "-" << chunk_end << "]");
    return false;
  }
  return true;
}

// ProcessChunkRangeIds implementation
// Performance-critical hot path (millions of iterations per search).
// Complexity from necessary conditional logic in tight loop. Already optimized with inline helpers.
// Extracting loop body would add function call overhead, degrading performance.
void ParallelSearchEngine::ProcessChunkRangeIds(  // NOSONAR(cpp:S3776, cpp:S107) - Hot path with explicit parameter list; further splitting would introduce call overhead or additional indirection
    const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
    size_t chunk_start,
    size_t chunk_end,
    std::vector<uint64_t>& local_results,
    const SearchContext& context,
    size_t storage_size,
    const lightweight_callable::LightweightCallable<bool, const char*>& filename_matcher,
    const lightweight_callable::LightweightCallable<bool, std::string_view>& path_matcher) {
  // NOLINTNEXTLINE(readability-simplify-boolean-expr) - Combined assert documents full chunk range invariant in a single place
  assert(chunk_start <= chunk_end && chunk_end <= soaView.size);
  if (const size_t array_size = soaView.size; !ValidateChunkRangeIds(array_size, chunk_start, chunk_end)) {
    return;
  }

  // Use pre-created matchers (no overhead from CreatePatternMatchers)
  // Implementation is a simplified version of ProcessChunkRange,
  // returning only IDs.

  // OPTIMIZATION: Hoist cancellation flag and extension filter check outside loop
  const bool has_cancel_flag = (context.cancel_flag != nullptr);
  const bool has_extension_filter = context.HasExtensionFilter();

  for (size_t i = chunk_start; i < chunk_end; ++i) {
    // Check for cancellation periodically (every kCancellationCheckInterval items)
    // NOLINTNEXTLINE(readability-simplify-boolean-expr) - Explicit conjunction keeps cancellation mask check and atomic flag load clearly separated in hot loop
    if (has_cancel_flag && ((i & kCancellationCheckMask) == 0U) &&  // NOSONAR(cpp:S1066) - Merge if: separate conditions for readability
        context.cancel_flag->load(std::memory_order_acquire)) {
      return;
    }

    if (soaView.is_deleted[i] != 0) {
      continue;
    }
    if (context.folders_only && soaView.is_directory[i] == 0) {
      continue;
    }

    const size_t path_len =
        parallel_search_detail::GetPathLength(soaView, i, storage_size);

    // Extension filter (optimized: uses GetExtensionView, avoids strlen())
    if (!parallel_search_detail::MatchesExtensionFilter(soaView, i, path_len, has_extension_filter, context)) {
      continue;
    }

    if (context.extension_only_mode) {
      // Extension filter already checked above, proceed to add result
    } else {
      const char* path = soaView.path_storage + soaView.path_offsets[i];
      const size_t filename_offset = soaView.filename_start[i];

      // Check filename/path matchers with early continue to reduce nesting
      if (!parallel_search_detail::CheckMatchers(path, path_len, filename_offset,
                                                  filename_matcher, path_matcher)) {
        continue;
      }
    }

    local_results.push_back(soaView.path_ids[i]);
  }
}

