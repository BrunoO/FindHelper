#pragma once


#include "path/PathStorage.h"
#include "search/SearchContext.h"
#include "search/SearchPatternUtils.h"  // For search_pattern_utils::ExtensionMatches (extension filter)
#include "search/SearchTypes.h"
#include "utils/Logger.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <future>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

class ISearchableIndex;
class ThreadPool;

// Forward declaration: defined in ParallelSearchEngine.cpp. The engine's
// filter-path helper takes it by const reference only.
namespace parallel_search_detail {
struct CandidateQuery;
}  // namespace parallel_search_detail

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
 * - Launching chunked parallel search tasks
 * - Processing search chunks in parallel
 * - Aggregating results from multiple threads
 */
class ParallelSearchEngine {
public:
  /**
   * Constructor
   *
   * @param threadPool Shared pointer to ThreadPool for executing tasks
   */
  explicit ParallelSearchEngine(std::shared_ptr<ThreadPool> threadPool);  // NOLINT(readability-identifier-naming) - threadPool matches API/caller convention

  /**
   * Perform parallel search and return futures with extracted data
   *
   * This method coordinates a parallel search across multiple threads,
   * returning futures that will yield per-task SearchResultBatch objects
   * (arena-owned path bytes plus offset hits; no per-hit heap strings).
   * Callers convert batches with AppendBatchToResultData (single-threaded).
   * This eliminates the need for FileEntry lookups in post-processing.
   *
   * @param index Reference to searchable index (implements ISearchableIndex)
   * @param query Filename query (searches in filename part of path)
   * @param thread_count Number of threads to use (-1 = auto)
   * @param context SearchContext containing all search parameters
   * @param thread_timings Optional output parameter for per-thread timing
   * @param cancel_flag Optional cancellation flag. Duplicates
   *   context.cancel_flag (FileIndex builds the context from the same flag);
   *   when only the parameter is supplied it is backfilled into the effective
   *   context, and an already-set flag short-circuits before any task launches.
   *
   * @return Vector of futures that will yield SearchResultBatch objects
   */
  std::vector<std::future<SearchResultBatch>>
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
  static void ProcessChunkRange(  // NOSONAR(cpp:S107) - Internal parallel search chunk processing takes full context
      const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
      size_t chunk_start,
      size_t chunk_end,
      ResultsContainer& local_results,
      const SearchContext& context,
      size_t storage_size,
      const search_pattern_utils::PatternMatcher& filename_matcher,
      const search_pattern_utils::PatternMatcher& path_matcher,
      bool path_first = false);

  /**
   * Get the thread pool used by this engine
   *
   * @return Reference to the ThreadPool
   */
  [[nodiscard]] ThreadPool& GetThreadPool() const;

  /**
   * Calculate bytes processed for a chunk range.
   *
   * Calculates the total size of paths in the specified chunk range for load balancing
   * and performance metrics.
   *
   * @param soaView Read-only view of SoA arrays
   * @param start_index Start index of chunk (inclusive)
   * @param end_index End index of chunk (exclusive)
   * @param storage_size Total storage size in bytes (for last entry calculation)
   * @return Total bytes in the chunk range
   */
  static size_t CalculateChunkBytes(
      const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
      size_t start_index,
      size_t end_index,
      size_t storage_size) {
    if (soaView.size == 0 || soaView.path_offsets == nullptr ||
        start_index >= end_index || start_index >= soaView.size) {
      return 0;
    }

    const size_t clamped_end = (std::min)(end_index, soaView.size);
    const size_t start_offset = soaView.path_offsets[start_index];
    const size_t end_offset =
        (clamped_end < soaView.size) ? soaView.path_offsets[clamped_end] : storage_size;
    const size_t item_count = clamped_end - start_index;

    if (end_offset < start_offset + item_count) {
      return 0;
    }
    return end_offset - start_offset - item_count;
  }

  /**
   * Record thread timing information.
   *
   * Fills in thread timing statistics for performance monitoring.
   */
  // NOLINTNEXTLINE(readability-function-size)
  static void RecordThreadTiming(  // NOSONAR(cpp:S107) - explicit parameter list mirrors ThreadTiming fields
      ThreadTiming& timing,
      uint64_t thread_idx,
      size_t start_index,
      size_t end_index,
      size_t initial_items,
      size_t dynamic_chunks_count,
      size_t total_items_processed,
      size_t bytes_processed,
      size_t results_count,
      int64_t elapsed_ms) {
    timing.thread_index_ = thread_idx;
    timing.start_index_ = start_index;
    timing.end_index_ = end_index;
    timing.items_processed_ = initial_items;
    timing.dynamic_chunks_processed_ = dynamic_chunks_count;
    timing.total_items_processed_ = total_items_processed;
    timing.bytes_processed_ = bytes_processed;
    timing.results_count_ = results_count;
    timing.elapsed_ms_ = (elapsed_ms >= 0) ? static_cast<uint64_t>(elapsed_ms) : 0ULL;
  }

  /**
   * Determine optimal thread count
   *
   * Determines the optimal number of threads to use based on:
   * - Explicit thread_count parameter (respected as-is when > 0)
   * - search_thread_pool_size_from_context (user setting; respected as-is when > 0)
   * - Physical core count (auto-detect; prefers physical over HT logical — bandwidth-bound)
   * - Data size cap (auto-detect only; ~8 MB/thread to avoid over-threading small indexes)
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
   */
  struct PatternMatchers {
    search_pattern_utils::PatternMatcher filename_matcher;
    search_pattern_utils::PatternMatcher path_matcher;
    // When true, evaluate the path matcher before the filename matcher per item.
    // Set once by CreatePatternMatchers when only the path side has a cheap
    // reject (see PreferPathMatcherFirst); constant for the whole search.
    bool path_first = false;
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
  std::shared_ptr<ThreadPool> thread_pool_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members

  /**
   * Launch parallel search tasks across the thread pool.
   *
   * Assigns initial chunks per thread, then uses dynamic chunks for remaining work.
   */
  [[nodiscard]] std::vector<std::future<SearchResultBatch>> LaunchSearchTasks(
      const ISearchableIndex& index, size_t total_items, int thread_count,
      const SearchContext& context,
      std::vector<ThreadTiming>* thread_timings) const;
  // Single-threaded candidate-filter attempt shared by SearchAsyncWithData.
  // Returns a taken result (possibly an empty batch vector on known-empty),
  // or nullopt to fall through to the parallel scan. Extracted to keep
  // SearchAsyncWithData under the cognitive-complexity threshold.
  [[nodiscard]] std::optional<std::vector<std::future<SearchResultBatch>>> TryCandidateFilterPath(
      const ISearchableIndex& index, const parallel_search_detail::CandidateQuery& query,
      const SearchContext& context, int scan_threads, size_t total_items,
      std::vector<ThreadTiming>* thread_timings) const;
  // Rate-bounded cost-model mispredict log (see .cpp for the detector rationale).
  // Const: only touches the mutable atomic trip/take counters.
  // used_extension attributes the take to the extension side (spec §7.3):
  // a slow-filter event with used_extension=1 implicates the combined or
  // extension-only path; with 0 it is trigram-only.
  void MaybeLogTrigramMispredict(uint64_t filter_us, uint64_t cardinality,
                                 size_t total_items, int scan_threads,
                                 bool used_extension) const;
  mutable std::atomic<uint64_t> trigram_mispredict_count_{0};  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  mutable std::atomic<uint64_t> extension_filter_take_count_{0};  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
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
                            size_t storage_size) noexcept {
  return soaView.GetPathLength(index, storage_size);
}

/**
 * Cost-model constants shared by ShouldUseTrigramFilter and the mispredict
 * detector in SearchAsyncWithData. Filtering costs ~0.9µs/candidate while
 * a full scan costs ~1.3µs/row spread over the estimated scan threads
 * (measured Release/arm64 on backtracking matcher shapes; see
 * internal-docs/benchmarks/2026-10-03_TRIGRAM_GATE_PP_PREFILTER.md).
 * Single source of truth: the gate and the detector must use the same
 * estimates, or the detector cannot tell model error from timer noise.
 */
inline constexpr double kTrigramFilterUsPerCandidate = 0.9;
inline constexpr double kFullScanUsPerRow = 1.3;

[[nodiscard]] inline double PredictedFilterCostUs(std::uint64_t cardinality) noexcept {
  return static_cast<double>(cardinality) * kTrigramFilterUsPerCandidate;
}

[[nodiscard]] inline double PredictedScanCostUs(size_t total_items, int thread_count) noexcept {
  const int threads = thread_count > 0 ? thread_count : 1;
  return static_cast<double>(total_items) * kFullScanUsPerRow / static_cast<double>(threads);
}

/**
 * Decide whether trigram candidates should be filtered single-threaded or the
 * index should be scanned in parallel (see cost constants above).
 * The hard cap bounds transient arena memory
 * (cardinality * 64B) when the estimate itself is unreliable.
 */
[[nodiscard]] bool ShouldUseTrigramFilter(std::uint64_t cardinality, size_t total_items,
                                          int estimated_thread_count) noexcept;

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
    size_t path_len) noexcept {
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
 * @param item_type_filter Item filesystem type filter (All, FilesOnly, FoldersOnly)
 * @return true if item should be skipped, false otherwise
 */
inline bool ShouldSkipItem(const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
                           size_t index,
                           ItemTypeFilter item_type_filter) noexcept {
  if (soaView.is_deleted[index] != 0) {
    return true;
  }

  if (item_type_filter == ItemTypeFilter::FoldersOnly && soaView.is_directory[index] == 0) {
    return true;
  }

  if (item_type_filter == ItemTypeFilter::FilesOnly && soaView.is_directory[index] != 0) {
    return true;
  }

  return false;
}

/**
 * Check if item at index should be skipped (deleted, folders_only filter)
 * Kept for backward compatibility.
 */
inline bool ShouldSkipItem(const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
                           size_t index,
                           bool folders_only) noexcept {
  return ShouldSkipItem(soaView, index,
                        folders_only ? ItemTypeFilter::FoldersOnly : ItemTypeFilter::All);
}

/**
 * Fast check whether a path represents a system file/folder (starts with '$' or has a '$\...' folder).
 */
[[nodiscard]] inline bool IsSystemPath(const char* path, size_t filename_offset) noexcept {
  assert(path != nullptr);
  if (path[filename_offset] == '$') {
    return true;
  }
  const std::string_view parent_dir(path, filename_offset);
  if (parent_dir.find('$') == std::string_view::npos) {
    return false;
  }
  return parent_dir.find("\\$") != std::string_view::npos ||
         parent_dir.find("/$") != std::string_view::npos;
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
                                   const SearchContext& context) noexcept {
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
 * @param filename_matcher Pre-created filename matcher (may be empty); receives bounded last-segment view
 * @param path_matcher Pre-created path matcher (may be empty)
 * @param path_first When true, evaluate the path matcher before the filename matcher
 * @return true if item matches patterns (or no patterns), false otherwise
 */
inline bool MatchesPatterns(const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
                            size_t index,
                            size_t path_len,
                            [[maybe_unused]] const SearchContext& context,
                            const search_pattern_utils::PatternMatcher& filename_matcher,
                            const search_pattern_utils::PatternMatcher& path_matcher,
                            bool path_first) {
  const char* path = soaView.path_storage + soaView.path_offsets[index];
  const size_t filename_offset = soaView.filename_start[index];
  assert(filename_offset <= path_len && "filename_start must not exceed path length");

  // Matcher input is an indexed name or path; see AssertSingleLineMatcherInput
  // (search/SearchPatternUtils.h). The path matcher runs first or last depending
  // on path_first, so assert the path text once here (a no-op in release builds).
  search_pattern_utils::search_pattern_utils_detail::AssertSingleLineMatcherInput(
      std::string_view(path, path_len));

  if (path_first && path_matcher && !path_matcher(std::string_view(path, path_len))) {
    return false;
  }

  if (filename_matcher) {
    const std::string_view filename(path + filename_offset, path_len - filename_offset);
    search_pattern_utils::search_pattern_utils_detail::AssertSingleLineMatcherInput(filename);
    if (!filename_matcher(filename)) {
      return false;
    }
  }

  if (!path_first && path_matcher && !path_matcher(std::string_view(path, path_len))) {
    return false;
  }

  return true;
}

/**
 * Create SearchResultData from SoA arrays.
 * fullPath is copied while the caller holds the index shared_lock (ProcessChunkRange),
 * so the owned string remains valid after the lock is released.
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
  data.fullPath.assign(path, path_len);  // copy while shared_lock held; safe after unlock
  data.filename_start = soaView.filename_start[index];
  data.extension_start = soaView.extension_start[index];
  return data;
}

// Trait selecting the arena-batch append path in ProcessChunkRange.
// Only SearchResultBatch takes this path; every other container keeps the
// owned-string CreateResultData above (used by tests calling the template
// directly with std::vector<SearchResultData>).
template<typename ResultsContainer>
struct IsResultBatch : std::false_type {};
template<>
struct IsResultBatch<SearchResultBatch> : std::true_type {};

/**
 * Append one hit to a SearchResultBatch: path bytes go to the task arena
 * (amortized allocation), only offsets are recorded. No per-hit heap string.
 */
inline void AppendHitToBatch(SearchResultBatch& batch,
                             const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
                             size_t index,
                             size_t path_len) {
  const char* path = soaView.path_storage + soaView.path_offsets[index];
  batch.AppendHit(soaView.path_ids[index], path, path_len,
                  soaView.is_directory[index] == 1,
                  soaView.filename_start[index],
                  soaView.extension_start[index]);
}

/**
 * Result count for the ProcessChunkRange log line. Overload resolves for
 * SearchResultBatch (PascalCase Size per naming rules); the template covers
 * vector-like containers.
 */
template<typename ResultsContainer>
inline size_t ResultContainerSize(const ResultsContainer& container) {
  return std::size(container);
}
inline size_t ResultContainerSize(const SearchResultBatch& batch) {
  return batch.Size();
}

/**
 * Emit a contiguous slice of non-deleted path items into a SearchResultBatch.
 * Copies the entire slice of null-terminated path strings via a single arena insert
 * and appends corresponding SearchResultHit structures.
 */
inline void EmitAllPassSlice(
    const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
    size_t slice_start,
    size_t slice_end,
    size_t storage_size,
    SearchResultBatch& local_results) {
  const size_t slice_start_byte = soaView.path_offsets[slice_start];
  const size_t slice_end_byte =
      (slice_end < soaView.size) ? soaView.path_offsets[slice_end] : storage_size;
  assert(slice_end_byte >= slice_start_byte && "Slice end byte must be >= start byte");

  // Copy the entire slice of contiguous null-terminated path strings in one bulk operation
  const size_t arena_base = local_results.arena.size();
  local_results.arena.insert(
      local_results.arena.end(),
      soaView.path_storage + slice_start_byte,
      soaView.path_storage + slice_end_byte);

  for (size_t i = slice_start; i < slice_end; ++i) {
    const size_t item_offset = soaView.path_offsets[i];
    const size_t next_offset =
        (i + 1 < soaView.size) ? soaView.path_offsets[i + 1] : storage_size;
    assert(next_offset > item_offset && "Next offset must be greater than current offset");
    const size_t path_len = next_offset - item_offset - 1;

    local_results.hits.push_back(SearchResultHit{
        soaView.path_ids[i],
        arena_base + (item_offset - slice_start_byte),
        path_len,
        soaView.is_directory[i] == 1,
        soaView.filename_start[i],
        soaView.extension_start[i],
    });
  }
}

/**
 * Bulk-emit contiguous non-deleted slices for all-pass queries directly into SearchResultBatch.
 * Skips tombstones, copies contiguous path string byte ranges via bulk arena insert (single memcpy),
 * and computes hit offsets directly from SoA arrays.
 */
inline void ProcessAllPassChunkRangeBatch(
    const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - soaView matches SoA convention
    size_t chunk_start,
    size_t chunk_end,
    SearchResultBatch& local_results,
    size_t storage_size,
    const std::atomic<bool>* cancel_flag) {
  constexpr size_t kMaxBulkSliceItems = 2048;

  size_t curr = chunk_start;

  while (curr < chunk_end) {
    if (cancel_flag != nullptr && cancel_flag->load()) {
      LOG_INFO_BUILD("ProcessAllPassChunkRangeBatch: Cancelled at item " << curr);
      return;
    }

    // Skip tombstones (deleted items)
    while (curr < chunk_end && soaView.is_deleted[curr] != 0) {
      ++curr;
    }
    if (curr >= chunk_end) {
      break;
    }

    const size_t slice_start = curr;
    const size_t max_slice_end = (std::min)(chunk_end, slice_start + kMaxBulkSliceItems);

    // Find end of contiguous non-deleted slice
    while (curr < max_slice_end && soaView.is_deleted[curr] == 0) {
      ++curr;
    }

    EmitAllPassSlice(soaView, slice_start, curr, storage_size, local_results);
  }
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
    const search_pattern_utils::PatternMatcher& filename_matcher,
    const search_pattern_utils::PatternMatcher& path_matcher,
    bool path_first) {
  const size_t array_size = soaView.size;
  size_t validated_chunk_end = chunk_end;

  // Validate chunk range and SoA view
  if (!parallel_search_detail::ValidateChunkRange(soaView, chunk_start, validated_chunk_end, array_size)) {
    return;
  }

  size_t items_checked = 0;
  // [[maybe_unused]]: only read by LOG_DEBUG_BUILD below, which compiles to
  // ((void)0) under NDEBUG (Release) — without it, -Wunused-but-set-variable fires.
  [[maybe_unused]] size_t items_matched = 0;

  // OPTIMIZATION (Phase 1): Cache frequently-accessed context values
  // These are loaded once before the loop to avoid repeated dereferences in the hot path
  const bool has_cancel_flag = (context.cancel_flag != nullptr);
  const bool extension_only_mode = context.extension_only_mode;
  const ItemTypeFilter item_type_filter = context.GetEffectiveItemTypeFilter();
  const bool has_extension_filter = context.HasExtensionFilter();
  const bool filter_system_files = context.ShouldFilterSystemFiles();
  const bool has_pattern_matchers = static_cast<bool>(filename_matcher) || static_cast<bool>(path_matcher);

  // OPTIMIZATION: All-pass bulk slice emission for SearchResultBatch.
  // When no filters or pattern matchers are present, emit contiguous non-deleted
  // slices via single bulk memcpy operations instead of per-item inserts.
  if constexpr (parallel_search_detail::IsResultBatch<ResultsContainer>::value) {
    if (!has_pattern_matchers && !has_extension_filter &&
        item_type_filter == ItemTypeFilter::All && !extension_only_mode && !filter_system_files) {
      parallel_search_detail::ProcessAllPassChunkRangeBatch(
          soaView, chunk_start, validated_chunk_end, local_results, storage_size, context.cancel_flag);
      LOG_DEBUG_BUILD("ProcessChunkRange: all_pass bulk mode, range ["
                      << chunk_start << "-" << validated_chunk_end << "], results="
                      << local_results.Size());
      return;
    }
  }

  for (size_t i = chunk_start; i < validated_chunk_end; ++i) {
    // Check for cancellation periodically (every kCancellationCheckInterval items)
    if (has_cancel_flag && ((items_checked & kCancellationCheckMask) == 0U) &&  // NOSONAR(cpp:S1066) - Merge if: separate conditions for readability
        context.cancel_flag->load()) {
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

    // Item type filter (only check after confirming not deleted)
    if (item_type_filter == ItemTypeFilter::FoldersOnly && soaView.is_directory[i] == 0) {
      continue;
    }
    if (item_type_filter == ItemTypeFilter::FilesOnly && soaView.is_directory[i] != 0) {
      continue;
    }

    const size_t path_len =
        parallel_search_detail::GetPathLength(soaView, i, storage_size);

    // Extension filter
    if (!parallel_search_detail::MatchesExtensionFilter(soaView, i, path_len, has_extension_filter, context)) {
      continue;
    }

    // Pattern matching: skip entirely if extension_only_mode or no active patterns (no function call overhead)
    if (!extension_only_mode && has_pattern_matchers &&
        !parallel_search_detail::MatchesPatterns(soaView, i, path_len, context, filename_matcher, path_matcher, path_first)) {
      continue;
    }

    if (filter_system_files) {
      const char* const path = soaView.path_storage + soaView.path_offsets[i];
      if (const size_t filename_offset = soaView.filename_start[i];
          parallel_search_detail::IsSystemPath(path, filename_offset)) {
        continue;
      }
    }

    // Add matching item to results. Batches record arena offsets (no per-hit
    // allocation); other containers keep owned-string rows. Resolved at compile
    // time so the inactive path costs nothing.
    if constexpr (parallel_search_detail::IsResultBatch<ResultsContainer>::value) {
      parallel_search_detail::AppendHitToBatch(local_results, soaView, i, path_len);
    } else {
      local_results.push_back(
          parallel_search_detail::CreateResultData<ResultsContainer>(soaView, i, path_len));
    }
    items_matched++;
  }

  // Unified logging for both modes
  LOG_DEBUG_BUILD("ProcessChunkRange: "
                 << (extension_only_mode ? "extension_only_mode" : "normal mode")
                 << ", range [" << chunk_start << "-" << validated_chunk_end << "], checked="
                 << items_checked << ", matched=" << items_matched
                 << ", results=" << parallel_search_detail::ResultContainerSize(local_results));
}

