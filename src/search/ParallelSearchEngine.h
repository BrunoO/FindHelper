#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <shared_mutex>

#include "index/FileIndexStorage.h"
#include "index/ISearchableIndex.h"
#include "path/PathStorage.h"
#include "path/PathUtils.h"
#include "search/ISearchExecutor.h"
#include "search/SearchContext.h"
#include "search/SearchPatternUtils.h"  // For search_pattern_utils::ExtensionMatches (extension filter)
#include "search/SearchStatisticsCollector.h"
#include "search/SearchTypes.h"
#include "utils/HashMapAliases.h"
#include "utils/LightweightCallable.h"
#include "utils/Logger.h"
#include <string>
#include <string_view>
#include <vector>

// Forward declarations
class SearchThreadPool;

// Check cancellation every N items to avoid polling overhead in the hot loop.
constexpr size_t kCancellationCheckInterval = 128;  // NOLINT(readability-magic-numbers) - Balance between responsiveness and hot-path cost
constexpr size_t kCancellationCheckMask = kCancellationCheckInterval - 1;
static_assert((kCancellationCheckInterval & kCancellationCheckMask) == 0U,
              "kCancellationCheckInterval must be a power of two for bitmask optimization");

/**
 * @file ParallelSearchEngine.h
 * @brief Coordinates parallel search operations using load balancing strategies
 *
 * This class encapsulates the parallel search orchestration logic, eliminating
 * the need for friend classes in FileIndex. It uses the ISearchableIndex
 * interface to access FileIndex data without requiring direct access to
 * private members.
 *
 * DESIGN PRINCIPLES:
 * - Eliminates friend classes (uses interface instead)
 * - Separates search orchestration from data storage
 * - Works with any implementation of ISearchableIndex
 * - Testable with mock implementations
 *
 * THREAD SAFETY:
 * - All methods are thread-safe
 * - Uses shared_mutex from ISearchableIndex for synchronization
 * - Worker threads acquire shared locks before accessing data
 */

/**
 * Parallel search engine that coordinates search operations
 *
 * This class handles the orchestration of parallel search operations,
 * including:
 * - Creating pattern matchers from search queries
 * - Coordinating with load balancing strategies
 * - Processing search chunks in parallel
 * - Aggregating results from multiple threads
 *
 * Implements ISearchExecutor to decouple load balancing strategies from
 * this concrete implementation, enabling better testability and maintainability.
 */
class ParallelSearchEngine : public ISearchExecutor {
public:
  /**
   * Constructor
   *
   * @param threadPool Shared pointer to SearchThreadPool for executing tasks
   */
  explicit ParallelSearchEngine(std::shared_ptr<SearchThreadPool> threadPool);  // NOLINT(readability-identifier-naming) - threadPool matches API/caller convention

  /**
   * Perform parallel search and return futures with file IDs
   *
   * This method coordinates a parallel search across multiple threads,
   * returning futures that will yield vectors of matching file IDs.
   *
   * @param index Reference to searchable index (implements ISearchableIndex)
   * @param query Filename query (searches in filename part of path)
   * @param thread_count Number of threads to use (-1 = auto)
   * @param context SearchContext containing all search parameters
   * @param stats Optional output parameter for search statistics
   *
   * @return Vector of futures that will yield vectors of matching file IDs
   */
  std::vector<std::future<std::vector<uint64_t>>>
  SearchAsync(const ISearchableIndex& index,
              std::string_view query,
              int thread_count,
              const SearchContext& context,
              SearchStats* stats = nullptr) const;

  /**
   * Perform parallel search and return futures with extracted data
   *
   * This method coordinates a parallel search across multiple threads,
   * returning futures that will yield vectors of SearchResultData.
   * This eliminates the need for FileEntry lookups in post-processing.
   *
   * @param index Reference to searchable index (implements ISearchableIndex)
   * @param query Filename query (searches in filename part of path)
   * @param thread_count Number of threads to use (-1 = auto)
   * @param context SearchContext containing all search parameters
   * @param thread_timings Optional output parameter for per-thread timing
   * @param cancel_flag Optional cancellation flag
   *
   * @return Vector of futures that will yield vectors of SearchResultData
   */
  std::vector<std::future<std::vector<SearchResultData>>>
  SearchAsyncWithData(const ISearchableIndex& index,
                      std::string_view query,
                      int thread_count,
                      const SearchContext& context,
                      std::vector<ThreadTiming>* thread_timings = nullptr,
                      const std::atomic<bool>* cancel_flag = nullptr) const;

  /**
   * Process a chunk range for search
   *
   * This method processes a range of items in the SoA arrays, searching
   * for matches and adding them to the results container. It's used by
   * load balancing strategies to process chunks in parallel.
   *
   * Template method that works with any container type that supports:
   * - push_back() method
   * - size() method
   *
   * Accepts pre-created pattern matchers. Callers must create matchers once per thread
   * using CreatePatternMatchers() and reuse them across multiple chunks.
   *
   * @param soaView Read-only view of SoA arrays
   * @param chunk_start Start index of chunk (inclusive)
   * @param chunk_end End index of chunk (exclusive)
   * @param local_results Results container to add matches to
   * @param context SearchContext containing search parameters
   * @param storage_size Total size of path storage in bytes (for extension length calculation)
   * @param filename_matcher Pre-created filename matcher
   * @param path_matcher Pre-created path matcher
   *
   * @note Template implementation is in ParallelSearchEngine.h (inline)
   */
  template<typename ResultsContainer>
  static void ProcessChunkRange(
      const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
      size_t chunk_start,
      size_t chunk_end,
      ResultsContainer& local_results,
      const SearchContext& context,
      size_t storage_size,
      const lightweight_callable::LightweightCallable<bool, const char*>& filename_matcher,
      const lightweight_callable::LightweightCallable<bool, std::string_view>& path_matcher);

  /**
   * Process a chunk range for search (IDs only version)
   *
   * Simplified version that only returns file IDs, not full SearchResultData.
   * Used by SearchAsync (not SearchAsyncWithData).
   *
   * Accepts pre-created pattern matchers. Callers must create matchers once per thread
   * using CreatePatternMatchers() and reuse them across multiple chunks.
   *
   * @param soaView Read-only view of SoA arrays
   * @param chunk_start Start index of chunk (inclusive)
   * @param chunk_end End index of chunk (exclusive)
   * @param local_results Vector to add matching IDs to
   * @param context SearchContext containing search parameters
   * @param storage_size Total size of path storage in bytes (for extension length calculation)
   * @param filename_matcher Pre-created filename matcher
   * @param path_matcher Pre-created path matcher
   */
  static void ProcessChunkRangeIds(
      const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
      size_t chunk_start,
      size_t chunk_end,
      std::vector<uint64_t>& local_results,
      const SearchContext& context,
      size_t storage_size,
      const lightweight_callable::LightweightCallable<bool, const char*>& filename_matcher,
      const lightweight_callable::LightweightCallable<bool, std::string_view>& path_matcher);

  /**
   * Get the thread pool used by this engine
   *
   * @return Reference to the SearchThreadPool
   *
   * @note Implements ISearchExecutor interface
   */
  [[nodiscard]] SearchThreadPool& GetThreadPool() const override;

  /**
   * Calculate bytes processed for a chunk range.
   *
   * Convenience forwarding helper for load balancing strategies to calculate
   * the total number of bytes in a chunk range for timing/metrics purposes.
   * Delegates to SearchStatisticsCollector::CalculateChunkBytes() so all
   * callers share a single implementation.
   *
   * @param soaView Read-only view of SoA arrays
   * @param start_index_ Start index of chunk (inclusive)
   * @param end_index_ End index of chunk (exclusive)
   * @param storage_size Total storage size in bytes (for last entry calculation)
   * @return Total bytes in the chunk range
   */
  static size_t CalculateChunkBytes(
      const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
      size_t start_index_,   // NOLINT(readability-identifier-naming) - matches SearchStatisticsCollector API
      size_t end_index_,     // NOLINT(readability-identifier-naming) - matches SearchStatisticsCollector API
      size_t storage_size) {
    return SearchStatisticsCollector::CalculateChunkBytes(soaView, start_index_, end_index_, storage_size);
  }

  /**
   * Record thread timing information.
   *
   * Convenience forwarding helper for load balancing strategies to fill in
   * thread timing statistics for performance monitoring.
   * Delegates to SearchStatisticsCollector::RecordThreadTiming() so that
   * all timing aggregation logic remains centralized.
   *
   * @param timing ThreadTiming struct to fill in
   * @param thread_idx Thread index
   * @param start_index_ Start index of chunk
   * @param end_index_ End index of chunk
   * @param initial_items Number of items in initial chunk
   * @param dynamic_chunks_count Number of dynamic chunks processed
   * @param total_items_processed_ Total items processed
   * @param bytes_processed_ Total bytes processed
   * @param results_count_ Number of matching results
   * @param elapsed_ms_ Elapsed time in milliseconds
   */
  static void RecordThreadTiming(  // NOSONAR(cpp:S107) - Convenience helper forwarding to SearchStatisticsCollector; explicit parameter list kept for clarity and to match existing API
      ThreadTiming& timing,
      uint64_t thread_idx,
      size_t start_index_,    // NOLINT(readability-identifier-naming) - matches SearchStatisticsCollector API
      size_t end_index_,      // NOLINT(readability-identifier-naming) - matches SearchStatisticsCollector API
      size_t initial_items,
      size_t dynamic_chunks_count,
      size_t total_items_processed_,  // NOLINT(readability-identifier-naming) - matches SearchStatisticsCollector API
      size_t bytes_processed_,        // NOLINT(readability-identifier-naming) - matches SearchStatisticsCollector API
      size_t results_count_,          // NOLINT(readability-identifier-naming) - matches SearchStatisticsCollector API
      int64_t elapsed_ms_) {          // NOLINT(readability-identifier-naming) - matches SearchStatisticsCollector API
    SearchStatisticsCollector::RecordThreadTiming(timing, thread_idx, start_index_, end_index_,
                                                   initial_items, dynamic_chunks_count,
                                                   total_items_processed_, bytes_processed_,
                                                   results_count_, elapsed_ms_);
  }

  /**
   * Determine optimal thread count
   *
   * Determines the optimal number of threads to use based on:
   * - Explicit thread_count parameter
   * - search_thread_pool_size_from_context (from SearchContext, 0 = auto)
   * - Hardware concurrency
   * - Data size (bytes per thread)
   *
   * @param thread_count Requested thread count (-1 = auto)
   * @param total_bytes Total bytes in path storage
   * @param search_thread_pool_size_from_context Thread pool size from context (0 = auto)
   * @return Optimal thread count
   */
  static int DetermineThreadCount(int thread_count, size_t total_bytes,
                                  int search_thread_pool_size_from_context = 0);

  /**
   * Structure to hold pattern matchers for filename and path matching.
   * Used instead of std::pair to avoid size limitations with LightweightCallable.
   */
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - both members default-constructed; path_matcher set by CreatePatternMatchers
  struct PatternMatchers {
    lightweight_callable::LightweightCallable<bool, const char*> filename_matcher;
    lightweight_callable::LightweightCallable<bool, std::string_view> path_matcher;
  };

  /**
   * Create pattern matchers from search context
   *
   * Creates lightweight callable pattern matchers for filename and path
   * queries based on the search context (regex, glob, case sensitivity, etc.)
   *
   * @param context SearchContext containing query strings and options
   * @return PatternMatchers struct with initialized matchers
   */
  static PatternMatchers CreatePatternMatchers(const SearchContext& context);

private:
  std::shared_ptr<SearchThreadPool> thread_pool_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
};

// ============================================================================
// Template Implementation (must be in header)
// ============================================================================

// Forward declaration for helper functions
namespace parallel_search_detail {

/**
 * Compute full path length from SoA offsets without strlen()
 *
 * @param soaView Read-only view of SoA arrays
 * @param index Index of the path entry
 * @param storage_size Total size of path storage (for last entry calculation)
 * @return Path length excluding null terminator
 */
inline size_t GetPathLength(const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
                            size_t index,
                            size_t storage_size) {
  // Precondition: index must be within the SoA bounds.
  assert(index < soaView.size && "Path index must be within SoA bounds");
  if (index + 1 < soaView.size) {
    // Not last entry: path length = (next offset - current offset - 1)
    // The -1 accounts for the null terminator
    return soaView.path_offsets[index + 1] - soaView.path_offsets[index] - 1;
  }

  // Last entry: path length = (storage end - current offset - 1)
  return storage_size - soaView.path_offsets[index] - 1;
}

/**
 * Get extension view from SoA arrays without using strlen()
 *
 * OPTIMIZED: Calculates extension length from path length and offsets instead of
 * calling strlen(), eliminating O(n) string scans in hot loops.
 *
 * @param soaView Read-only view of SoA arrays
 * @param index Index of the path entry
 * @param path_len Full path length (excluding null terminator)
 * @return Extension string_view (empty if no extension)
 */
inline std::string_view GetExtensionView(
    const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
    size_t index,
    size_t path_len) {
  if (soaView.extension_start[index] == SIZE_MAX) {
    return {};
  }

  const char* path = soaView.path_storage + soaView.path_offsets[index];
  const size_t ext_off = soaView.extension_start[index];
  // Postcondition: extension must start within the path (otherwise ext_len underflows).
  assert(ext_off <= path_len && "Extension offset must be within path bounds");
  const char* ext_start = path + ext_off;

  // Extension length = path length - extension start offset
  const size_t ext_len = path_len - ext_off;

  return {ext_start, ext_len};
}

/**
 * Validate chunk range parameters and SoA view
 *
 * @param soaView Read-only view of SoA arrays
 * @param chunk_start Start index of chunk (inclusive)
 * @param chunk_end End index of chunk (exclusive) - may be modified
 * @param array_size Size of the array
 * @return true if valid, false otherwise
 */
inline bool ValidateChunkRange(const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
                               size_t chunk_start,
                               size_t& chunk_end,
                               size_t array_size) {
  if (array_size == 0) {
    LOG_WARNING_BUILD("ProcessChunkRange: Array is empty");
    return false;
  }

  if (chunk_start >= array_size) {
    LOG_WARNING_BUILD("ProcessChunkRange: chunk_start ("
                      << chunk_start << ") >= array_size (" << array_size << ")");
    return false;
  }
  if (chunk_end > array_size) {
    LOG_WARNING_BUILD("ProcessChunkRange: chunk_end ("
                      << chunk_end << ") > array_size (" << array_size
                      << "), clamping to " << array_size);
    chunk_end = array_size;
  }
  if (chunk_end <= chunk_start) {
    LOG_WARNING_BUILD("ProcessChunkRange: Invalid range ["
                      << chunk_start << "-" << chunk_end << "]");
    return false;
  }

  // Validate view is valid (critical for preventing crashes)
  if (soaView.path_storage == nullptr || soaView.path_offsets == nullptr || soaView.path_ids == nullptr ||
      soaView.is_deleted == nullptr || soaView.is_directory == nullptr || soaView.filename_start == nullptr ||
      soaView.extension_start == nullptr) {
    LOG_ERROR_BUILD("ProcessChunkRange: Invalid SoAView");
    return false;
  }

  return true;
}

/**
 * Check if item at index should be skipped (deleted, folders_only filter)
 *
 * @param soaView Read-only view of SoA arrays
 * @param index Item index
 * @param folders_only If true, only return directories
 * @return true if item should be skipped, false otherwise
 */
inline bool ShouldSkipItem(const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
                           size_t index,
                           bool folders_only) {
  if (soaView.is_deleted[index] != 0) {
    return true;
  }

  if (folders_only && soaView.is_directory[index] == 0) {
    return true;
  }

  return false;
}

/**
 * Check if item matches extension filter
 *
 * @param soaView Read-only view of SoA arrays
 * @param index Item index
 * @param path_len Full path length (excluding null terminator)
 * @param has_extension_filter Cached result of context.HasExtensionFilter() (hoisted from hot loop)
 * @param context SearchContext containing extension set and case sensitivity
 * @return true if item matches extension filter (or no filter), false otherwise
 */
inline bool MatchesExtensionFilter(const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
                                   size_t index,
                                   size_t path_len,
                                   bool has_extension_filter,
                                   const SearchContext& context) {
  if (!has_extension_filter) {
    return true;  // No filter, always matches
  }

  const std::string_view ext_view = GetExtensionView(soaView, index, path_len);
  return search_pattern_utils::ExtensionMatches(ext_view, context.extension_set, context.case_sensitive);
}

/**
 * Check if item matches pattern matchers (filename and path)
 * Path matcher receives the full path so path patterns (e.g. under a folder) match
 * the full path including filename, not just the directory part.
 *
 * @param soaView Read-only view of SoA arrays
 * @param index Item index
 * @param path_len Full path length (excluding null terminator)
 * @param context SearchContext containing search parameters
 * @param filename_matcher Pre-created filename matcher (may be empty)
 * @param path_matcher Pre-created path matcher (may be empty)
 * @return true if item matches patterns (or no patterns), false otherwise
 */
inline bool MatchesPatterns(const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
                            size_t index,
                            size_t path_len,
                            [[maybe_unused]] const SearchContext& context,
                            const lightweight_callable::LightweightCallable<bool, const char*>& filename_matcher,
                            const lightweight_callable::LightweightCallable<bool, std::string_view>& path_matcher) {
  const char* path = soaView.path_storage + soaView.path_offsets[index];
  const size_t filename_offset = soaView.filename_start[index];

  if (const char* filename = path + filename_offset;
      filename_matcher && !filename_matcher(filename)) {
    return false;
  }

  if (path_matcher && !path_matcher(std::string_view(path, path_len))) {
    return false;
  }

  return true;
}

/**
 * Create SearchResultData from SoA arrays (zero-copy: fullPath is string_view into SoA).
 *
 * @param soaView Read-only view of SoA arrays
 * @param index Item index
 * @param path_len Path length excluding null terminator (from GetPathLength)
 * @return SearchResultData for the item
 */
template<typename ResultsContainer>
inline typename ResultsContainer::value_type CreateResultData(
    const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
    size_t index,
    size_t path_len) {
  const char* path = soaView.path_storage + soaView.path_offsets[index];
  typename ResultsContainer::value_type data;
  data.id = soaView.path_ids[index];
  data.isDirectory = (soaView.is_directory[index] == 1);
  data.fullPath = std::string_view(path, path_len);
  return data;
}
} // namespace parallel_search_detail

// ProcessChunkRange template implementation
// Performance-critical hot path (millions of iterations per search).
// Complexity from necessary conditional logic in tight loop. Already optimized with inline helpers.
// Extracting loop body would add function call overhead, degrading performance.
template<typename ResultsContainer>
inline void ParallelSearchEngine::ProcessChunkRange(  // NOSONAR(cpp:S3776) NOLINT(readability-function-cognitive-complexity) - Performance-critical hot path; extracting loop body would add call overhead
    const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
    size_t chunk_start,
    size_t chunk_end,
    ResultsContainer& local_results,
    const SearchContext& context,
    size_t storage_size,
    const lightweight_callable::LightweightCallable<bool, const char*>& filename_matcher,
    const lightweight_callable::LightweightCallable<bool, std::string_view>& path_matcher) {
  const size_t array_size = soaView.size;
  size_t validated_chunk_end = chunk_end;

  // Validate chunk range and SoA view
  if (!parallel_search_detail::ValidateChunkRange(soaView, chunk_start, validated_chunk_end, array_size)) {
    return;
  }

  size_t items_checked = 0;
  size_t items_matched = 0;

  // OPTIMIZATION (Phase 1): Cache frequently-accessed context values
  // These are loaded once before the loop to avoid repeated dereferences in the hot path
  const bool has_cancel_flag = (context.cancel_flag != nullptr);
  const bool extension_only_mode = context.extension_only_mode;
  const bool folders_only = context.folders_only;
  const bool has_extension_filter = context.HasExtensionFilter();

  for (size_t i = chunk_start; i < validated_chunk_end; ++i) {
    // Check for cancellation periodically (every kCancellationCheckInterval items)
    if (has_cancel_flag && ((items_checked & kCancellationCheckMask) == 0U) &&  // NOSONAR(cpp:S1066) - Merge if: separate conditions for readability
        context.cancel_flag->load(std::memory_order_acquire)) {
      LOG_INFO_BUILD("ProcessChunkRange: Cancelled at item " << i);
      return;
    }

    items_checked++;

    // Defensive check: the SoA array must not shrink during iteration.
    // validated_chunk_end guarantees this at chunk-start time; assert enforces it at each step.
    assert(i < array_size && "SoA array size must not shrink during search iteration");

    // Load is_deleted once per iteration; skip deleted items immediately
    // Quick early-exit for deleted items (most common case) before any other processing
    if (const uint8_t is_deleted_value = soaView.is_deleted[i]; is_deleted_value != 0) {
      continue;  // Skip deleted items immediately, no further processing
    }

    // folders_only filter (only check after confirming not deleted)
    if (folders_only && soaView.is_directory[i] == 0) {
      continue;
    }

    const size_t path_len =
        parallel_search_detail::GetPathLength(soaView, i, storage_size);

    // Extension filter
    if (!parallel_search_detail::MatchesExtensionFilter(soaView, i, path_len, has_extension_filter, context)) {
      continue;
    }

    // Pattern matching: skip entirely if extension_only_mode (no function call overhead)
    if (!extension_only_mode && !parallel_search_detail::MatchesPatterns(soaView, i, path_len, context, filename_matcher, path_matcher)) {
      continue;
    }

    // Add matching item to results
    local_results.push_back(
        parallel_search_detail::CreateResultData<ResultsContainer>(soaView, i, path_len));
    items_matched++;
  }

  // Unified logging for both modes
  LOG_INFO_BUILD("ProcessChunkRange: "
                 << (extension_only_mode ? "extension_only_mode" : "normal mode")
                 << ", range [" << chunk_start << "-" << validated_chunk_end << "], checked="
                 << items_checked << ", matched=" << items_matched
                 << ", results=" << local_results.size());
}

