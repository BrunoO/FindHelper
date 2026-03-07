/**
 * @file SearchResultUtils.cpp
 * @brief Implementation of SearchResult utility functions
 *
 * This module implements:
 * - Lazy loading and formatting of file size and modification time
 * - Time filter cache maintenance with efficient filtering
 * - Sorting with pre-fetching for Size and Last Modified columns
 *
 * The sorting implementation uses a thread pool (passed as parameter) to pre-fetch file
 * metadata (size and modification time) before sorting, avoiding UI thread
 * blocking during sort operations.
 */

#include "search/SearchResultUtils.h"

#include "index/LazyAttributeLoader.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <future>
#include <numeric>
#include <stdexcept>  // For std::future_error
#include <string_view>
#include <unordered_map>

// CompareFileTime is now in FileTimeTypes.h
#ifndef _WIN32
#include "utils/FileTimeTypes.h"
#else
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#endif  // _WIN32

#include "filters/SizeFilterUtils.h"
#include "filters/TimeFilterUtils.h"
#include "utils/AsyncUtils.h"
#include "utils/FileAttributeConstants.h"
#include "utils/FileSystemUtils.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"
#include "utils/ThreadPool.h"

namespace {

// Maximum wait time for cancelled tasks to finish (milliseconds)
constexpr int kMaxWaitForCancelledTasksMs = 10;

/**
 * @brief Check if all attribute loading futures are complete
 *
 * Performs a non-blocking check to see if all futures are ready.
 *
 * @param futures Vector of futures to check
 * @return True if all futures are ready, false otherwise
 */
bool AreAllFuturesComplete(const std::vector<std::future<void>>& futures) {
  return std::all_of(futures.begin(), futures.end(), [](const auto& f) {
    return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
  });
}

/**
 * @brief Wait briefly for cancelled tasks to finish
 *
 * Gives cancelled tasks time to see the cancellation flag and exit,
 * preventing use-after-free if results are replaced while tasks are running.
 *
 * @param futures Vector of futures to wait for
 * @param max_wait_ms Maximum time to wait in milliseconds
 */
void WaitBrieflyForCancelledTasks(const std::vector<std::future<void>>& futures, int max_wait_ms) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(max_wait_ms);
  for (const auto& f : futures) {
    if (f.valid()) {
      auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
      if (remaining.count() > 0) {
        // Wait briefly for cancelled task to finish (non-blocking)
        f.wait_for(remaining);
      }
      // If future is not ready, it's cancelled and will be cleaned up later
      // We can't block here, so we'll rely on the cancellation token to prevent work
    }
  }
}

/**
 * @brief Check if sort data is ready and handle completion
 *
 * Checks if sort data is ready (either via flag or futures complete).
 * If ready, cleans up futures and returns true. Otherwise returns false.
 *
 * @param state GUI state (modified)
 * @param sort_generation Expected sort generation
 * @return True if data is ready, false if still loading
 */
bool CheckAndHandleSortDataReady(GuiState& state, SortGeneration sort_generation) {
  // Check if sort data is ready (all attributes already loaded, no async needed)
  if (state.sortDataReady) {
    return true;
  }

  // If no futures or if this is for an outdated sort operation, do nothing
  if (state.attributeLoadingFutures.empty() || state.sort_generation_ != sort_generation) {
    return false;
  }

  // Check if all futures are complete (non-blocking check)
  if (!AreAllFuturesComplete(state.attributeLoadingFutures)) {
    return false;  // Still loading
  }

  // All futures are complete - get results and format display strings
  // CRITICAL: Must call .get() on each future to clean up resources and prevent memory leaks
  // All futures are ready at this point (we checked above), so use blocking mode
  CleanupAttributeLoadingFutures(state, true);

  // If a newer sort has been started, don't apply the results of this one
  if (state.sort_generation_ != sort_generation) {
    CleanupAttributeLoadingFutures(state, false);
    return false;
  }

  return true;  // Data is ready
}

struct FolderStatsForSort {
  size_t file_count = 0;
  uint64_t total_size_bytes = 0;
};

using FolderStatsForSortMap = std::unordered_map<std::string_view, FolderStatsForSort>;

void AccumulateFolderStatsForResult(FolderStatsForSortMap& stats_by_path,
                                    const SearchResult& result) {
  if (result.isDirectory) {
    return;
  }

  const uint64_t file_size = result.fileSize;
  if (file_size == kFileSizeNotLoaded || file_size == kFileSizeFailed) {
    return;
  }

  std::string_view path = result.fullPath;
  while (!path.empty()) {
    const size_t last_separator = path.find_last_of("\\/");
    if (last_separator == std::string_view::npos) {
      break;
    }
    path = path.substr(0, last_separator);
    FolderStatsForSort& stats = stats_by_path[path];
    ++stats.file_count;
    stats.total_size_bytes += file_size;
  }
}

FolderStatsForSortMap BuildFolderStatsForSort(const std::vector<SearchResult>& results) {
  FolderStatsForSortMap stats_by_path;
  for (const auto& result : results) {
    AccumulateFolderStatsForResult(stats_by_path, result);
  }
  return stats_by_path;
}

[[nodiscard]] uint64_t GetFolderSortMetric(const SearchResult& result,
                                           int column_index,
                                           const FolderStatsForSortMap& stats_by_path) {
  constexpr uint64_t kZero = 0;

  if (column_index == ResultColumn::FolderFileCount) {
    if (!result.isDirectory) {
      // For individual files, "Total Files" is displayed as 1 in the UI, so use 1 here
      // to keep sort order consistent with the rendered values.
      return 1;
    }
    const auto it = stats_by_path.find(result.fullPath);
    if (it == stats_by_path.end()) {
      return kZero;
    }
    return it->second.file_count;
  }

  if (column_index == ResultColumn::FolderTotalSize) {
    if (result.isDirectory) {
      const auto it = stats_by_path.find(result.fullPath);
      if (it == stats_by_path.end()) {
        return kZero;
      }
      return it->second.total_size_bytes;
    }

    const uint64_t file_size = result.fileSize;
    if (file_size == kFileSizeNotLoaded || file_size == kFileSizeFailed) {
      return kZero;
    }
    return file_size;
  }

  return kZero;
}

}  // namespace

void EnsureDisplayStringsPopulated(const SearchResult& result, const FileIndex& file_index,
                                  bool allow_sync_load) {
  // Early return optimization: if everything is already populated, skip all checks
  // This avoids repeated checks every frame for already-loaded results
  if (result.isDirectory) {
    // Directories don't need loading - they already have size=0 and time={0,0}
    return;
  }

  // Check if both display strings are already populated (fastest path)
  if (!result.fileSizeDisplay.empty() && !result.lastModificationDisplay.empty()) {
    return;  // Everything already loaded and formatted
  }

  // During streaming, skip synchronous disk I/O to keep UI responsive.
  // Cloud files (OneDrive, SharePoint) can take 100-500ms per file; loading
  // for many visible rows blocks the main thread and triggers OS "not responding" spinner.
  if (!allow_sync_load) {
    return;  // Size/date columns will show "..." until search completes
  }

  // Load size if not yet loaded
  if (result.fileSize == kFileSizeNotLoaded) {
    result.fileSize = file_index.GetFileSizeById(result.fileId);
  }

  // Format size display string if needed
  // This handles both: (1) newly loaded values, and (2) values that were loaded
  // but not formatted
  if (result.fileSize != kFileSizeNotLoaded && result.fileSize != kFileSizeFailed &&
      result.fileSizeDisplay.empty()) {
    result.fileSizeDisplay = FormatFileSize(result.fileSize);
  }

  // Load modification time if not yet loaded
  if (IsSentinelTime(result.lastModificationTime)) {
    result.lastModificationTime = file_index.GetFileModificationTimeById(result.fileId);
  }

  // Format modification time display string if needed
  // This handles both: (1) newly loaded values, and (2) values that were loaded
  // but not formatted
  if (!IsSentinelTime(result.lastModificationTime) && !IsFailedTime(result.lastModificationTime) &&
      result.lastModificationDisplay.empty()) {
    result.lastModificationDisplay = FormatFileTime(result.lastModificationTime);
  }
}

void EnsureModTimeLoaded(const SearchResult& result, const FileIndex& file_index) {
  if (!result.isDirectory && IsSentinelTime(result.lastModificationTime)) {
    result.lastModificationTime = file_index.GetFileModificationTimeById(result.fileId);
  }
}

/**
 * @brief Helper function to apply time filter to search results
 *
 * Filters search results based on modification time relative to a cutoff:
 * - Today/ThisWeek/ThisMonth/ThisYear: Files newer than or equal to cutoff
 * - Older: Files older than cutoff
 *
 * This function performs lazy loading of modification times for each result
 * to avoid unnecessary I/O. Directories and files with failed time loads are
 * skipped.
 *
 * @param results Full search results to filter
 * @param filter TimeFilter enum value
 * @param file_index File index for loading modification times
 * @return Filtered vector of SearchResult (may be empty)
 */

// Helper to initialize filtered results vector with conservative capacity (eliminates duplicate
// code)
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming)
// - This is a static function, not a global variable (clang-tidy false positive)
static void InitializeFilteredResults(std::vector<SearchResult>& filtered, size_t source_size) {
  filtered.clear();
  filtered.reserve(source_size / 2);  // Conservative estimate
}

// Helper: apply time filter comparison once modification time is loaded
static bool MatchesTimeFilter(const FILETIME& last_mod_time, const FILETIME& cutoff_time,
                              TimeFilter filter) {
  const int comparison = CompareFileTime(
    &last_mod_time, &cutoff_time);  // NOLINT(cppcoreguidelines-init-variables) - Variable
                                    // initialized by CompareFileTime return value
  return (filter == TimeFilter::Older) ? (comparison < 0) : (comparison >= 0);
}

// Helper: handle cloud-file time loading and optimistic inclusion.
// Returns true if the result was handled (included/queued) and the caller
// should skip further processing for this item.
static bool HandleCloudFileTimeLoading(
  const SearchResult& result, const FileIndex& file_index, GuiState* state,
  ThreadPool* thread_pool,  // NOSONAR(cpp:S995) - Parameter is used to invoke non-const operations
                            // (enqueue); pointer-to-const would be incorrect
  std::vector<SearchResult>& filtered, std::vector<uint64_t>& cloud_files_to_load) {
  const std::string_view path_view = file_index.GetPathAccessor().GetPathView(result.fileId);
  if (path_view.empty()) {
    return true;  // Invalid path - skip this file
  }

  const std::string path(path_view);
  if (const bool is_cloud_file = IsLikelyCloudFile(path); !is_cloud_file) {
    return false;  // Let caller handle synchronous load for local files
  }

  if (state == nullptr || thread_pool == nullptr) {
    return true;  // Cloud file without async context: skip instead of blocking UI
  }

  // Cloud file with unloaded time - include optimistically and defer loading
  // Use insert().second to check if insertion happened (more efficient than find + insert)
  if (state->deferredCloudFiles.insert(result.fileId).second) {
    cloud_files_to_load.push_back(result.fileId);
  }
  filtered.push_back(result);
  return true;
}

static std::vector<SearchResult> ApplyTimeFilter(const std::vector<SearchResult>& results,
                                                 TimeFilter filter, const FileIndex& file_index,
                                                 GuiState* state = nullptr,
                                                 ThreadPool* thread_pool = nullptr) {
  if (filter == TimeFilter::None) {
    return results;  // No filtering
  }

  const FILETIME cutoff_time = CalculateCutoffTime(filter);
  std::vector<SearchResult> filtered;
  InitializeFilteredResults(filtered, results.size());

  // Track which cloud files need background loading
  std::vector<uint64_t> cloud_files_to_load;

  for (const auto& result : results) {
    // Skip directories
    if (result.isDirectory) {
      continue;
    }

    // Check if modification time is already loaded
    bool time_loaded =
      !IsSentinelTime(result.lastModificationTime) && !IsFailedTime(result.lastModificationTime);

    if (!time_loaded) {
      // Time not loaded - check if it's a cloud file
      if (HandleCloudFileTimeLoading(result, file_index, state, thread_pool, filtered,
                                     cloud_files_to_load)) {
        continue;
      }

      // Not a cloud file - try to load synchronously (fast for local files)
      EnsureModTimeLoaded(result, file_index);
      time_loaded =
        !IsSentinelTime(result.lastModificationTime) && !IsFailedTime(result.lastModificationTime);
    }

    // Skip if time couldn't be loaded (and it's not a deferred cloud file)
    if (!time_loaded) {
      continue;
    }

    // Apply filter
    if (MatchesTimeFilter(result.lastModificationTime, cutoff_time, filter)) {
      filtered.push_back(result);
    }
  }

  // Start background loading for deferred cloud files
  if (!cloud_files_to_load.empty() && state != nullptr && thread_pool != nullptr) {
    for (const uint64_t file_id : cloud_files_to_load) {
      state->cloudFileLoadingFutures.emplace_back(thread_pool->enqueue([file_id, &file_index]() {
        // Load modification time in background (result intentionally ignored - triggers lazy
        // loading)
        (void)file_index.GetFileModificationTimeById(file_id);
      }));
    }
  }

  return filtered;
}

// Helper to update filter cache for empty state (eliminates duplicate code)
template <typename FilterType>
static void UpdateFilterCacheForEmptyState(std::vector<SearchResult>& filtered_results,
                                           size_t& filtered_count, FilterType& cached_filter,
                                           bool& cache_valid, FilterType current_filter) {
  filtered_results.clear();
  filtered_count = 0;
  cached_filter = current_filter;
  cache_valid = true;
}

// Helper to update filter cache when filter is None (eliminates duplicate code)
template <typename FilterType>
static void UpdateFilterCacheForNoFilter(std::vector<SearchResult>& filtered_results,
                                         size_t& filtered_count, FilterType& cached_filter,
                                         bool& cache_valid, FilterType current_filter,
                                         size_t source_count) {
  filtered_results.clear();
  filtered_count = source_count;
  cached_filter = current_filter;
  cache_valid = true;
}

// Helper: clean up completed cloud-file futures, returning true if any completed.
// Always runs the cleanup loop to avoid suppressing the cloud_files_completed signal.
// Removed function-static throttling to avoid global shared state and cross-state
// interference; invalidation runs when cloud files complete.
static bool CleanUpCloudFutures(GuiState& state) {
  bool cloud_files_completed = false;
  if (state.cloudFileLoadingFutures.empty()) {
    return cloud_files_completed;
  }

  // Always check and clean ready futures - never skip to avoid suppressing the signal.
  auto it = state.cloudFileLoadingFutures.begin();
  while (it != state.cloudFileLoadingFutures.end()) {
    if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      async_utils::ExecuteWithLogCatch([&it]() { it->get(); }, "Error during future cleanup");
      it = state.cloudFileLoadingFutures.erase(it);
      cloud_files_completed = true;
    } else {
      ++it;
    }
  }

  if (cloud_files_completed) {
    state.timeFilterCacheValid = false;
    state.InvalidateDisplayedTotalSize();
  }

  return cloud_files_completed;
}

// Helper: check whether time filter cache needs rebuild.
static bool ShouldRebuildTimeFilterCache(const GuiState& state, bool cloud_files_completed) {
  const bool filter_changed = (state.cachedTimeFilter != state.timeFilter);
  const bool need_rebuild =
    !state.timeFilterCacheValid || filter_changed || state.resultsUpdated || cloud_files_completed;
  return need_rebuild;
}

void UpdateTimeFilterCacheIfNeeded(GuiState& state, const FileIndex& file_index,
                                   ThreadPool* thread_pool) {
  // If search is not active AND no results, keep cache trivial
  // But if we have results, keep them visible even if search is not active
  if (!state.searchActive && state.searchResults.empty()) {
    UpdateFilterCacheForEmptyState(state.filteredResults, state.filteredCount,
                                   state.cachedTimeFilter, state.timeFilterCacheValid,
                                   state.timeFilter);
    return;
  }

  // If no time filter, filtered results == full results
  if (state.timeFilter == TimeFilter::None) {
    UpdateFilterCacheForNoFilter(state.filteredResults, state.filteredCount, state.cachedTimeFilter,
                                 state.timeFilterCacheValid, state.timeFilter,
                                 state.searchResults.size());
    return;
  }

  // PERFORMANCE: Defer filter cache rebuild for one frame when results first arrive
  // This prevents blocking the UI thread with expensive file attribute loading
  // on Windows (especially for cloud files like OneDrive which require IShellItem2)
  // The first frame shows unfiltered results, then filters are applied on next frame
  if (state.deferFilterCacheRebuild) {
    // Show unfiltered results for this frame, rebuild cache on next frame
    // Clear the filtered results to show all results temporarily
    state.filteredResults.clear();
    state.filteredCount = state.searchResults.size();
    return;  // Don't clear defer flag here - let UpdateSizeFilterCacheIfNeeded handle it
  }

  // Defer cloud file cleanup while displayed total size is being computed progressively.
  // For broad searches (e.g. "pp:**") with many cloud files, each completion would invalidate
  // and reset the computation; we would never finish. Let the current computation complete
  // first, then process cloud files on the next frame.
  // Starvation guard: use time-based budget (frame-rate independent) to allow cleanup
  // periodically without long stalls on high-FPS systems.
  static auto s_last_cleanup = std::chrono::steady_clock::now();
  if (const bool total_size_in_progress =
          !state.displayedTotalSizeValid && state.displayedTotalSizeComputationIndex > 0;
      total_size_in_progress) {
    constexpr auto kMinCleanupInterval = std::chrono::milliseconds(250);  // Run cleanup at least 4x/sec
    const auto now = std::chrono::steady_clock::now();
    if (now - s_last_cleanup < kMinCleanupInterval) {
      return;  // Defer this frame
    }
    s_last_cleanup = now;  // Allow cleanup this frame
  } else {
    s_last_cleanup = std::chrono::steady_clock::now();
  }

  // Check if any deferred cloud files have finished loading; if so, invalidate
  // cache so it can be rebuilt with updated data.
  if (const bool cloud_files_completed = CleanUpCloudFutures(state);
      !ShouldRebuildTimeFilterCache(state, cloud_files_completed)) {
    return;  // Cache is still valid
  }

  // Reset progress before mutating filteredResults to avoid transient idx > results.size()
  state.InvalidateDisplayedTotalSize();
  state.filteredResults =
    ApplyTimeFilter(state.searchResults, state.timeFilter, file_index, &state, thread_pool);
  state.filteredCount = state.filteredResults.size();
  state.cachedTimeFilter = state.timeFilter;
  state.timeFilterCacheValid = true;
}

/**
 * @brief Helper to ensure file size is loaded for a result
 *
 * This is a lightweight helper that only loads size, avoiding unnecessary
 * modification time loading when filtering by size.
 *
 * @param result SearchResult to populate (fileSize may be modified)
 * @param file_index File index for loading file size
 */
static void EnsureSizeLoaded(const SearchResult& result, const FileIndex& file_index) {
  if (!result.isDirectory && result.fileSize == kFileSizeNotLoaded) {
    result.fileSize = file_index.GetFileSizeById(result.fileId);
  }
}

/**
 * @brief Helper function to apply size filter to search results
 *
 * Filters search results based on file size ranges:
 * - Empty: 0 bytes
 * - Tiny: < 1 KB
 * - Small: 1 KB - 100 KB
 * - Medium: 100 KB - 10 MB
 * - Large: 10 MB - 100 MB
 * - Huge: 100 MB - 1 GB
 * - Massive: > 1 GB
 *
 * This function performs lazy loading of file sizes for each result
 * to avoid unnecessary I/O. Directories are skipped.
 *
 * @param results Full search results to filter
 * @param filter SizeFilter enum value
 * @param file_index File index for loading file sizes
 * @return Filtered vector of SearchResult (may be empty)
 */
static std::vector<SearchResult> ApplySizeFilter(const std::vector<SearchResult>& results,
                                                 SizeFilter filter, const FileIndex& file_index) {
  if (filter == SizeFilter::None) {
    return results;  // No filtering
  }

  std::vector<SearchResult> filtered;
  InitializeFilteredResults(filtered, results.size());

  for (const auto& result : results) {
    // Skip directories - they don't have meaningful sizes
    if (result.isDirectory) {
      continue;
    }

    // Ensure file size is loaded (mutable fields can be modified through const ref)
    EnsureSizeLoaded(result, file_index);

    // Skip if size couldn't be loaded
    if (result.fileSize == kFileSizeNotLoaded || result.fileSize == kFileSizeFailed) {
      continue;
    }

    // Apply filter
    if (MatchesSizeFilter(result.fileSize, filter)) {
      filtered.push_back(result);
    }
  }

  return filtered;
}

void UpdateSizeFilterCacheIfNeeded(GuiState& state, const FileIndex& file_index) {
  // If search is not active AND no results, keep cache trivial
  if (!state.searchActive && state.searchResults.empty()) {
    UpdateFilterCacheForEmptyState(state.sizeFilteredResults, state.sizeFilteredCount,
                                   state.cachedSizeFilter, state.sizeFilterCacheValid,
                                   state.sizeFilter);
    return;
  }

  // Determine the source results (either time-filtered or raw search results)
  const std::vector<SearchResult>* source_results = nullptr;
  size_t source_count = 0;

  if (state.timeFilter != TimeFilter::None && !state.filteredResults.empty()) {
    // Use time-filtered results as the source
    source_results = &state.filteredResults;
    source_count = state.filteredCount;
  } else {
    // Use raw search results as the source
    source_results = &state.searchResults;
    source_count = state.searchResults.size();
  }

  // If no size filter, size-filtered results == source results
  if (state.sizeFilter == SizeFilter::None) {
    UpdateFilterCacheForNoFilter(state.sizeFilteredResults, state.sizeFilteredCount,
                                 state.cachedSizeFilter, state.sizeFilterCacheValid,
                                 state.sizeFilter, source_count);
    // Clear defer flag after both filters have been processed
    // This ensures filters are deferred together on first frame, then both rebuild on second frame
    state.deferFilterCacheRebuild = false;
    return;
  }

  // PERFORMANCE: Defer filter cache rebuild if time filter cache is being deferred
  // This ensures both filters are deferred together for consistency
  if (state.deferFilterCacheRebuild) {
    // Show unfiltered results for this frame, rebuild cache on next frame
    state.sizeFilteredResults.clear();
    state.sizeFilteredCount = source_count;
    // Clear defer flag after both filters have been processed
    // This ensures filters are deferred together on first frame, then both rebuild on second frame
    state.deferFilterCacheRebuild = false;
    return;
  }

  const bool filter_changed = (state.cachedSizeFilter != state.sizeFilter);
  const bool need_rebuild = !state.sizeFilterCacheValid || filter_changed || state.resultsUpdated ||
                            !state.timeFilterCacheValid;
  if (!need_rebuild) {  // NOSONAR(cpp:S6004) - Variable used after if block (line 499 and later)
    return;             // Cache is still valid
  }

  state.sizeFilteredResults = ApplySizeFilter(*source_results, state.sizeFilter, file_index);
  state.sizeFilteredCount = state.sizeFilteredResults.size();
  state.cachedSizeFilter = state.sizeFilter;
  state.sizeFilterCacheValid = true;

  // Piggyback: accumulate total size during size filter pass (sizes already loaded)
  uint64_t total_bytes = 0;
  for (const auto& r : state.sizeFilteredResults) {
    if (!r.isDirectory && r.fileSize != kFileSizeNotLoaded && r.fileSize != kFileSizeFailed) {
      total_bytes += r.fileSize;
    }
  }
  state.displayedTotalSizeBytes = total_bytes;
  state.displayedTotalSizeValid = true;
}

namespace {
/** Max file sizes to load per frame when computing displayed total; keeps UI responsive on first search. */
constexpr size_t kDisplayedTotalSizeLoadsPerFrame = 100;
/** Max items to scan per frame regardless of loading; prevents long loops on already-loaded data. */
constexpr size_t kDisplayedTotalSizeScansPerFrame = 10000;
}  // namespace

void UpdateDisplayedTotalSizeIfNeeded(GuiState& state, const FileIndex& file_index) {
  if (!state.resultsComplete || state.showingPartialResults) {
    return;  // Streaming: omit size
  }
  if (state.displayedTotalSizeValid) {
    return;  // Already computed (e.g. by size filter piggyback)
  }

  // Determine which vector to sum (same logic as GetDisplayResults)
  const std::vector<SearchResult>* display_results_ptr = nullptr;
  if (!state.deferFilterCacheRebuild) {
    if (state.sizeFilter != SizeFilter::None && state.sizeFilterCacheValid) {
      display_results_ptr = &state.sizeFilteredResults;
    } else if (state.timeFilter != TimeFilter::None && state.timeFilterCacheValid) {
      display_results_ptr = &state.filteredResults;
    }
  }
  if (display_results_ptr == nullptr) {
    display_results_ptr = &state.searchResults;
  }

  const std::vector<SearchResult>& results = *display_results_ptr;
  size_t& idx = state.displayedTotalSizeComputationIndex;
  uint64_t& acc = state.displayedTotalSizeComputationBytes;

  // Reset when idx is out of bounds (e.g. after invalidation or source vector replaced).
  // Source changes are handled by InvalidateDisplayedTotalSize when filteredResults is rebuilt.
  // Use >= so idx==results.size() after exact-bound progress also triggers reset when invalidated
  // (e.g. results vector was replaced with smaller set).
  if (idx >= results.size()) {
    state.ResetDisplayedTotalSizeProgress();
  }

  // Progressive loading: only count actual I/O loads towards the budget.
  // This ensures that if attributes are already in memory (from MFT or previous load),
  // we finish the computation much faster.
  size_t loads_this_frame = 0;
  size_t scans_this_frame = 0;
  while (idx < results.size() &&
         loads_this_frame < kDisplayedTotalSizeLoadsPerFrame &&
         scans_this_frame < kDisplayedTotalSizeScansPerFrame) {
    const SearchResult& result = results[idx];
    ++idx;
    ++scans_this_frame;

    if (result.isDirectory) {
      continue;
    }

    // Only increment loads_this_frame if we actually attempt a load
    if (result.fileSize == kFileSizeNotLoaded) {
      EnsureSizeLoaded(result, file_index);
      ++loads_this_frame;
    }

    if (result.fileSize != kFileSizeNotLoaded && result.fileSize != kFileSizeFailed) {
      acc += result.fileSize;
    }
  }

  if (idx >= results.size()) {
    state.displayedTotalSizeBytes = acc;
    state.displayedTotalSizeValid = true;
    state.ResetDisplayedTotalSizeProgress();
  }
}

/**
 * @brief Validate index and check cancellation token
 *
 * Helper function to validate that an index is within bounds and the operation
 * hasn't been cancelled. Used by async attribute loading tasks.
 *
 * @param token Cancellation token to check
 * @param results Results vector to validate index against
 * @param i Index to validate
 * @return true if index is valid and operation not cancelled, false otherwise
 */
static bool ValidateIndexAndCancellation(const SortCancellationToken& token,
                                         const std::vector<SearchResult>& results, size_t i) {
  // Defensive cancellation check at start
  if (token.IsCancelled()) {
    return false;
  }

  // Validate index is still valid (defensive check)
  if (i >= results.size()) {
    return false;
  }

  // Check cancellation again before accessing result
  if (token.IsCancelled()) {
    return false;
  }

  return true;
}

/**
 * @brief Start async loading of file attributes for sorting (non-blocking)
 *
 * This function enqueues all size and modification time fetch tasks to the thread pool
 * without waiting for completion. The futures are returned for later checking.
 *
 * @param results Search results to pre-fetch data for (will be modified by futures)
 * @param file_index File index for loading metadata
 * @param thread_pool Thread pool for parallel pre-fetching
 * @return Vector of futures that will complete when attributes are loaded
 */
static std::vector<std::future<void>> StartAttributeLoadingAsync(
  std::vector<SearchResult>& results, const FileIndex& file_index, ThreadPool& thread_pool,
  const SortCancellationToken& token) {
  std::vector<std::future<void>> futures;

  if (results.empty()) {
    return futures;
  }

  // We might need to fetch size and mod time for each result.
  futures.reserve(results.size() * 2);

  // Enqueue all I/O tasks using index-based access for safety.
  // This prevents use-after-free if the results vector is replaced while tasks are running.
  for (size_t i = 0; i < results.size(); ++i) {
    const auto& result = results[i];
    if (result.isDirectory) {
      continue;
    }
    if (result.fileSize == kFileSizeNotLoaded) {
      futures.emplace_back(thread_pool.enqueue([i, &results, &file_index, &token] {
        auto& r = results[i];
        r.fileSize = ValidateIndexAndCancellation(token, results, i)
                         ? file_index.GetFileSizeById(r.fileId)
                         : r.fileSize;
      }));
    }
    if (IsSentinelTime(result.lastModificationTime)) {
      futures.emplace_back(thread_pool.enqueue([i, &results, &file_index, &token] {
        auto& r = results[i];
        r.lastModificationTime =
            ValidateIndexAndCancellation(token, results, i)
                ? file_index.GetFileModificationTimeById(r.fileId)
                : r.lastModificationTime;
      }));
    }
  }

  return futures;
}

/**
 * @brief Format display strings for search results
 *
 * Formats file size and modification time display strings for all non-directory
 * results that have loaded data but not yet formatted display strings.
 *
 * This is a performance-safe helper that eliminates duplicate formatting loops.
 * Called from multiple locations after attribute loading is complete.
 *
 * @param results Search results to format display strings for (modified in place)
 */
static void FormatDisplayStrings(std::vector<SearchResult>& results) {
  for (auto& result : results) {
    if (!result.isDirectory) {
      // Format size display string if needed.
      if (result.fileSize != kFileSizeNotLoaded && result.fileSize != kFileSizeFailed &&
          result.fileSizeDisplay.empty()) {
        result.fileSizeDisplay = FormatFileSize(result.fileSize);
      }
      // Format modification time display string if needed.
      if (!IsSentinelTime(result.lastModificationTime) &&
          !IsFailedTime(result.lastModificationTime) && result.lastModificationDisplay.empty()) {
        result.lastModificationDisplay = FormatFileTime(result.lastModificationTime);
      }
    }
  }
}

/**
 * @brief Helper to pre-fetch and pre-format file size and modification time for sorting (blocking
 * version)
 *
 * This is the synchronous version that blocks until all attributes are loaded.
 * Used when we need to sort immediately (e.g., when results are first displayed).
 *
 * @param results Search results to pre-fetch data for (modified in place)
 * @param file_index File index for loading metadata
 * @param thread_pool Thread pool for parallel pre-fetching
 */
static void PrefetchAndFormatSortDataBlocking(
  std::vector<SearchResult>& results,
  FileIndex& file_index,      // NOSONAR(cpp:S995) - FileIndex is modified during attribute loading;
                              // non-const reference is required
  ThreadPool& thread_pool) {  // NOSONAR(cpp:S995) - ThreadPool is used to schedule work; non-const
                              // reference is required
  if (results.empty()) {
    return;
  }

  const ScopedTimer timer("PrefetchAndFormatSortData with thread pool");

  // For blocking calls, we don't need a real cancellation token, but the
  // function signature requires one.
  const SortCancellationToken dummy_token;
  auto futures = StartAttributeLoadingAsync(results, file_index, thread_pool, dummy_token);

  // Wait for all I/O tasks to complete.
  for (auto& f : futures) {
    f.get();
  }

  // Format the display strings (now that we have the data).
  FormatDisplayStrings(results);

  // Log lazy loading statistics after sorting (if any lazy loading occurred)
  // Must be called AFTER lazy loading completes (futures.get()) to see actual statistics
  LazyAttributeLoader::LogStatistics();
}

void SortSearchResults(std::vector<SearchResult>& results, int column_index,
                       ImGuiSortDirection direction, FileIndex& file_index,
                       ThreadPool& thread_pool) {
  // Validate sort parameters (debug-only)
  // Note: Mark column (0) is not sortable in the UI, so we only allow Filename and above.
  const bool in_valid_range =
      (column_index >= ResultColumn::Filename &&
       column_index <= ResultColumn::FolderTotalSize);
  assert(in_valid_range);
  if (!in_valid_range) {
    LOG_WARNING_BUILD("SortSearchResults: invalid column_index=" << column_index
                                                                 << ", clamping to Filename");
    column_index = ResultColumn::Filename;
  }

  if (results.empty()) {
    return;
  }

  // Pre-fetch all necessary data for sorting using the thread pool.
  // This avoids blocking the UI thread on file I/O inside the sort lambda.
  if (column_index == ResultColumn::Size || column_index == ResultColumn::LastModified) {
    PrefetchAndFormatSortDataBlocking(results, file_index, thread_pool);
  }

  if (column_index == ResultColumn::FolderFileCount ||
      column_index == ResultColumn::FolderTotalSize) {
    const FolderStatsForSortMap stats_by_path = BuildFolderStatsForSort(results);
    const size_t n = results.size();
    std::vector<uint64_t> keys;
    keys.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      const SearchResult& r = results[i];
      const uint64_t metric = GetFolderSortMetric(r, column_index, stats_by_path);
      keys.push_back(metric);
    }
    std::vector<size_t> indices(n);
    std::iota(indices.begin(), indices.end(), 0);
    const auto compare = [&keys, &results, direction](size_t i, size_t j) {
      if (keys[i] != keys[j]) {
        return direction == ImGuiSortDirection_Ascending ? keys[i] < keys[j]
                                                        : keys[i] > keys[j];
      }
      return results[i].fullPath < results[j].fullPath;
    };
    std::sort(indices.begin(), indices.end(), compare);
    std::vector<SearchResult> sorted;
    sorted.reserve(n);
    for (const size_t idx : indices) {
      sorted.push_back(results[idx]);
    }
    results = std::move(sorted);
    return;
  }

  std::sort(results.begin(), results.end(),
            CreateSearchResultComparator(column_index, direction));
}

void StartSortAttributeLoading(GuiState& state, std::vector<SearchResult>& results,
                               int column_index, const FileIndex& file_index,
                               ThreadPool& thread_pool) {
  // Cancel any previously running sort operations
  state.sort_cancellation_token_.Cancel();

  // Brief non-blocking wait for cancelled tasks to finish (max 10ms)
  // This gives cancelled tasks time to see the cancellation flag and exit,
  // preventing use-after-free if results are replaced while tasks are running
  WaitBrieflyForCancelledTasks(state.attributeLoadingFutures, kMaxWaitForCancelledTasksMs);

  // Now safe to clear futures - CRITICAL: Must clear before incrementing generation
  // to ensure old futures are fully cleaned up
  // Use non-blocking cleanup since we're in a cancellation path
  CleanupAttributeLoadingFutures(state, false);

  // Only start async loading for Size or Last Modified columns
  if (column_index != ResultColumn::Size && column_index != ResultColumn::LastModified) {
    return;
  }

  if (results.empty()) {
    return;
  }

  // Create a new cancellation token and increment generation BEFORE starting new futures.
  // This ensures the generation matches the futures we're about to create.
  state.sort_cancellation_token_ = SortCancellationToken();
  state.sort_generation_++;
  assert(state.completed_sort_generation_ <= state.sort_generation_);

  // Start async attribute loading with the new generation
  state.attributeLoadingFutures =
    StartAttributeLoadingAsync(results, file_index, thread_pool, state.sort_cancellation_token_);

  // Show "Loading attributes..." for the entire sort-by-Size/LastModified flow
  // (from now until sort completes). This ensures the status bar message is visible
  // even when no futures were created (e.g. all sizes/times already in results from MFT
  // or previous load), so the user sees that a sort-by-attributes operation is in progress.
  state.loadingAttributes = true;

  // If no futures were created (all attributes already loaded), we need to trigger
  // an immediate sort. Set a flag so CheckSortAttributeLoadingAndSort knows to sort
  // on the next frame.
  // CRITICAL FIX: If no futures were created, all data is already loaded and we
  // should mark the sort as ready to complete immediately. We don't sort here
  // because we want to keep the sort logic in CheckSortAttributeLoadingAndSort
  // to maintain a consistent code path. Setting completed_sort_generation_ to
  // sort_generation_ - 1 ensures the next check will trigger the sort.
  if (state.attributeLoadingFutures.empty()) {
    state.sortDataReady = true;
  }
}

bool CheckSortAttributeLoadingAndSort(GuiState& state, std::vector<SearchResult>& results,
                                      int column_index, ImGuiSortDirection direction,
                                      SortGeneration sort_generation) {
  // Check if sort data is ready and handle completion
  if (!CheckAndHandleSortDataReady(state, sort_generation)) {
    return false;  // Still loading
  }

  // Clear the sortDataReady flag now that we're processing it
  state.sortDataReady = false;

  // Format display strings now that all data is loaded
  FormatDisplayStrings(results);

  // All attributes loaded - perform the sort
  std::sort(results.begin(), results.end(), CreateSearchResultComparator(column_index, direction));

  // Clear futures and loading flag (futures have already been consumed by .get())
  CleanupAttributeLoadingFutures(state, false);
  state.completed_sort_generation_ = sort_generation;
  assert(state.completed_sort_generation_ <= state.sort_generation_);

  // Log lazy loading statistics after async sorting completes
  // Must be called AFTER lazy loading completes to see actual statistics
  LazyAttributeLoader::LogStatistics();

  return true;  // Sort completed
}

// Helper function to cleanup a single future
// Extracted to reduce nesting depth in CleanupAttributeLoadingFutures()
static void CleanupSingleFuture(std::future<void>& f, bool blocking) {
  try {
    if (blocking) {
      // Blocking mode: wait for future if not ready, then get it
      if (f.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        f.wait();
      }
      f.get();
    } else {
      // Non-blocking mode: only clean up if future is ready
      if (f.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        f.get();
      }
    }
  } catch (const std::future_error& e) {
    // Future cleanup must not throw - log error but don't propagate
    LOG_ERROR_BUILD("Future error during cleanup: " << e.what());
    // Explicitly ignore to prevent propagation (destructor safety)
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Generic catch needed for destructor
                                       // safety, specific catch above handles future_error
    // Future cleanup must not throw - log error but don't propagate
    LOG_ERROR_BUILD("Exception during future cleanup: " << e.what());
    // Explicitly ignore to prevent propagation (destructor safety)
  } catch (...) {  // NOSONAR(cpp:S2738) - Final safety net in cleanup path: must catch any
                   // non-standard exceptions to prevent crashes during asynchronous future cleanup
    // Future cleanup must not throw - log unknown error but don't propagate
    LOG_ERROR_BUILD("Unknown exception during future cleanup");
    // Explicitly ignore to prevent propagation (destructor safety)
  }
}

void CleanupAttributeLoadingFutures(GuiState& state, bool blocking) {
  for (auto& f : state.attributeLoadingFutures) {
    if (f.valid()) {
      CleanupSingleFuture(f, blocking);
    }
  }
  state.attributeLoadingFutures.clear();
  state.loadingAttributes = false;
}
