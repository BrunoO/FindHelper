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
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
#include <numeric>
#include <string_view>
#include <thread>
#include <unordered_map>

// CompareFileTime is now in FileTimeTypes.h
#ifndef _WIN32
#include "utils/FileTimeTypes.h"
#else
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#endif  // _WIN32

#include "filters/SizeFilterUtils.h"
#include "filters/TimeFilterUtils.h"
#include "search/SearchResultsService.h"
#include "utils/AsyncUtils.h"
#include "utils/FileAttributeConstants.h"
#include "utils/FileSystemUtils.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"
#include "utils/ThreadPool.h"

std::vector<SearchResult> MergeAndConvertToSearchResults(  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - free function, PascalCase API
    std::vector<char>& pool,
    const std::vector<SearchResultData>& data,
    const FileIndex& file_index) {
  std::vector<SearchResult> results;
  results.reserve(data.size());

  size_t pool_bytes_needed = 0;
  for (const SearchResultData& datum : data) {
    pool_bytes_needed += datum.fullPath.size() + 1;
  }
  pool.reserve(pool.size() + pool_bytes_needed);

  std::vector<uint64_t> file_ids;
  std::vector<size_t> file_result_indices;
  file_ids.reserve(data.size());
  file_result_indices.reserve(data.size());

  size_t pool_offset = pool.size();
  for (const SearchResultData& datum : data) {
    // datum.fullPath is an owned copy (taken under index shared_lock); safe to read here.
    pool.insert(pool.end(), datum.fullPath.begin(), datum.fullPath.end());
    pool.push_back('\0');
    const size_t path_len = datum.fullPath.size();
    const char* path_start = pool.data() + pool_offset;
    pool_offset += path_len + 1;

    SearchResult result;
    result.fileId = datum.id;
    result.isDirectory = datum.isDirectory;
    result.fullPath = std::string_view(path_start, path_len);
    result.filename_offset = datum.filename_start;
    result.extension_offset =
        (datum.extension_start == SIZE_MAX) ? std::string_view::npos : datum.extension_start;

    if (datum.isDirectory) {
      result.fileSize = kFileSizeNotLoaded;
      result.lastModificationTime = kFileTimeNotLoaded;
      result.folderFileCount = kFolderFileCountNotLoaded;
    } else {
      result.fileSize = kFileSizeNotLoaded;
      result.lastModificationTime = kFileTimeNotLoaded;
      file_ids.push_back(datum.id);
      file_result_indices.push_back(results.size());
    }
    result.fileSizeDisplay = "";
    result.lastModificationDisplay = "";
    results.push_back(std::move(result));
  }

  if (!file_ids.empty()) {
    std::vector<std::optional<FileIndex::EntryAttributeSnapshot>> snapshots(file_ids.size());
    file_index.FillCachedAttributeSnapshots(file_ids, snapshots);
    for (size_t i = 0; i < snapshots.size(); ++i) {
      SearchResult& result = results.at(file_result_indices.at(i));
      if (const std::optional<FileIndex::EntryAttributeSnapshot>& attrs = snapshots.at(i); attrs.has_value()) {
        result.fileSize = attrs->file_size;
        result.lastModificationTime = attrs->last_modification_time;
      }
    }
  }
  return results;
}

namespace {

// Maximum wait time for cancelled tasks to finish (milliseconds)
constexpr int kMaxWaitForCancelledTasksMs = 10;

/**
 * @brief Wait briefly for cancelled tasks to finish
 *
 * Gives cancelled tasks time to see the cancellation flag and exit,
 * preventing use-after-free if results are replaced while tasks are running.
 *
 * @param counter Atomic countdown counter (decremented by each task on completion)
 * @param max_wait_ms Maximum time to wait in milliseconds
 */
void WaitBrieflyForCancelledTasks(const std::shared_ptr<std::atomic<int>>& counter,
                                   int max_wait_ms) {
  if (!counter) {
    return;
  }
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(max_wait_ms);
  while (counter->load(std::memory_order_acquire) > 0 &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
}

/**
 * @brief Wait until all cancelled tasks have fully drained
 *
 * The brief wait above is useful to keep UI responsive, but for the sort-attribute
 * loading pipeline we must not reuse the same results buffer while old tasks may
 * still write into it. This function enforces strict quiescence.
 *
 * @param counter Atomic countdown counter for the cancelled batch
 */
void WaitForCancelledTasksFully(const std::shared_ptr<std::atomic<int>>& counter) {
  if (!counter) {
    return;
  }
  while (counter->load(std::memory_order_acquire) > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
  if (state.async_sort_.sort_ready_state_ == SortReadyState::Ready) {
    return true;
  }

  // No counter or outdated sort operation — nothing to check
  if (!state.async_sort_.counter || state.async_sort_.generation_ != sort_generation) {
    return false;
  }

  // Non-blocking check: counter reaches 0 when all tasks have decremented it
  if (state.async_sort_.counter->load(std::memory_order_acquire) > 0) {
    return false;  // Still loading
  }

  // All tasks done — reset counter and transition to Ready.
  state.async_sort_.counter.reset();
  state.async_sort_.sort_ready_state_ = SortReadyState::Ready;
  return true;
}

}  // namespace


void EnsureDisplayStringsPopulated(const SearchResult& result, const FileIndex& file_index) {
  // Early return optimization: if everything is already populated, skip all checks
  // This avoids repeated checks every frame for already-loaded results
  // Check if both display strings are already populated (fastest path)
  if (!result.fileSizeDisplay.empty() && !result.lastModificationDisplay.empty()) {
    return;  // Everything already loaded and formatted
  }

  // Load size if not yet loaded (files only; directories use FolderSizeAggregator).
  // Retry kFileSizeFailed: SearchResult snapshots can be stale after RecomputeAllPaths
  // resets FileEntry state when corrected paths are inserted.
  if (!result.isDirectory &&
      (result.fileSize == kFileSizeNotLoaded || result.fileSize == kFileSizeFailed)) {
    result.fileSize = file_index.GetFileSizeById(result.fileId);
  }

  // Format size display string if needed
  // This handles both: (1) newly loaded values, and (2) values that were loaded
  // but not formatted
  if (result.fileSize != kFileSizeNotLoaded && result.fileSize != kFileSizeFailed &&
      result.fileSizeDisplay.empty()) {
    result.fileSizeDisplay = FormatFileSize(result.fileSize);
  }
  // For directories whose aggregate size hasn't been computed yet, provide a
  // placeholder so callers (e.g. CSV export) never see an empty fileSizeDisplay.
  if (result.isDirectory && result.fileSize == kFileSizeNotLoaded &&
      result.fileSizeDisplay.empty()) {
    result.fileSizeDisplay = "...";
  }

  // For directories whose file count hasn't been computed yet, provide a placeholder
  // so callers (e.g. CSV export) never see an empty folderFileCountDisplay.
  if (result.isDirectory && result.folderFileCount == kFolderFileCountNotLoaded &&
      result.folderFileCountDisplay.empty()) {
    result.folderFileCountDisplay = "...";
  }

  // Invariant: "..." must only appear when the size is still unknown.
  // If fileSizeDisplay is "..." but fileSize has a real value, the per-row
  // code failed to overwrite the placeholder when the aggregator result arrived.
  assert(result.fileSizeDisplay != "..." || result.fileSize == kFileSizeNotLoaded);  // NOSONAR(cpp:S2583) - Intentional debug invariant; Sonar sees the preceding if as making this trivially true, but this assertion catches cross-call violations where a previous invocation set "..." and a subsequent code path wrote a real size without updating the display

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
  if (IsSentinelTime(result.lastModificationTime)) {
    result.lastModificationTime = file_index.GetFileModificationTimeById(result.fileId);
  }
}

/**
 * @brief Helper to ensure file size is loaded for a result (size only; no mod-time I/O).
 */
static void EnsureSizeLoaded(const SearchResult& result, const FileIndex& file_index) {
  if (!result.isDirectory &&
      (result.fileSize == kFileSizeNotLoaded || result.fileSize == kFileSizeFailed)) {
    result.fileSize = file_index.GetFileSizeById(result.fileId);
  }
}

namespace {
/** Max attribute loads per frame when rebuilding size/time filter caches (keeps FPS responsive). */
constexpr size_t kFilterAttributeLoadsPerFrame = 100;

[[nodiscard]] bool ResultsNeedSizeLoads(const std::vector<SearchResult>& results) {
  return std::any_of(  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
      results.begin(), results.end(), [](const SearchResult& result) {
        return !result.isDirectory &&
               (result.fileSize == kFileSizeNotLoaded || result.fileSize == kFileSizeFailed);
      });
}

// Load up to budget sizes into result snapshots. Returns true if more loads remain.
[[nodiscard]] bool LoadSizeAttributesWithBudget(const std::vector<SearchResult>& results,
                                                const FileIndex& file_index, size_t budget) {
  size_t loads = 0;
  for (const auto& result : results) {
    if (loads >= budget) {
      return ResultsNeedSizeLoads(results);
    }
    if (!result.isDirectory &&
        (result.fileSize == kFileSizeNotLoaded || result.fileSize == kFileSizeFailed)) {
      EnsureSizeLoaded(result, file_index);
      ++loads;
    }
  }
  return false;
}

[[nodiscard]] bool ResultsNeedLocalTimeLoads(const std::vector<SearchResult>& results,
                                             const FileIndex& file_index) {
  return std::any_of(  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
      results.begin(), results.end(),
      [&file_index](const SearchResult& result) {
        if (!IsSentinelTime(result.lastModificationTime)) {
          return false;
        }
        const std::string path = file_index.GetPathAccessor().GetPathCopy(result.fileId);
        if (path.empty()) {
          return false;
        }
        return !IsLikelyCloudFile(path);  // Cloud times deferred in ApplyTimeFilter
      });
}

// Load up to budget local (non-cloud) mod times. Returns true if more local loads remain.
[[nodiscard]] bool LoadLocalTimeAttributesWithBudget(const std::vector<SearchResult>& results,
                                                     const FileIndex& file_index, size_t budget) {
  size_t loads = 0;
  for (const auto& result : results) {
    if (loads >= budget) {
      return ResultsNeedLocalTimeLoads(results, file_index);
    }
    if (!IsSentinelTime(result.lastModificationTime)) {
      continue;
    }
    if (const std::string path = file_index.GetPathAccessor().GetPathCopy(result.fileId);
        path.empty() || IsLikelyCloudFile(path)) {
      continue;
    }
    EnsureModTimeLoaded(result, file_index);
    ++loads;
  }
  return false;
}
}  // namespace

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
    &last_mod_time, &cutoff_time);
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
  const std::string path = file_index.GetPathAccessor().GetPathCopy(result.fileId);
  if (path.empty()) {
    return true;  // Invalid path - skip this file
  }

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

// Helper to update filter cache when no search is active (eliminates duplicate code)
template <typename FilterType>
static void UpdateFilterCacheForWelcomePanel(std::vector<SearchResult>& filtered_results,
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

bool ShouldDeferFilterCacheRebuild(const GuiState& state) {
  const bool time_filter_active = state.timeFilter != TimeFilter::None;
  const bool size_filter_active = state.sizeFilter != SizeFilter::None;
  if (!time_filter_active && !size_filter_active) {
    return false;
  }

  const auto& results = state.result_pool_->Results();
  return std::any_of(  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
      results.begin(), results.end(),
      [time_filter_active, size_filter_active](const SearchResult& result) {
        if (time_filter_active && IsSentinelTime(result.lastModificationTime)) {
          return true;
        }
        // Size filter skips directories; only unloaded/failed file sizes force I/O.
        return size_filter_active && !result.isDirectory &&
               (result.fileSize == kFileSizeNotLoaded || result.fileSize == kFileSizeFailed);
      });
}

void UpdateTimeFilterCacheIfNeeded(GuiState& state, const FileIndex& file_index,
                                   ThreadPool* thread_pool) {
  // If search is not active AND no results, keep cache trivial
  // But if we have results, keep them visible even if search is not active
  if (!state.searchActive && state.result_pool_->Results().empty()) {
    UpdateFilterCacheForWelcomePanel(state.filteredResults, state.filteredCount,
                                   state.cachedTimeFilter, state.timeFilterCacheValid,
                                   state.timeFilter);
    return;
  }

  // If no time filter, filtered results == full results
  if (state.timeFilter == TimeFilter::None) {
    UpdateFilterCacheForNoFilter(state.filteredResults, state.filteredCount, state.cachedTimeFilter,
                                 state.timeFilterCacheValid, state.timeFilter,
                                 state.result_pool_->Results().size());
    return;
  }

  // PERFORMANCE: Defer one frame only when an active filter still needs attribute I/O
  // (see ShouldDeferFilterCacheRebuild). Avoids unfiltered flash when filters are off
  // or attributes are already cached; still protects UI from OneDrive/IShellItem2 stalls.
  if (state.deferFilterCacheRebuild) {
    if (!ShouldDeferFilterCacheRebuild(state)) {
      state.deferFilterCacheRebuild = false;
    } else {
      // Show unfiltered results for this frame, rebuild cache on next frame
      state.filteredResults.clear();
      state.filteredCount = state.result_pool_->Results().size();
      return;  // Don't clear defer flag here - let UpdateSizeFilterCacheIfNeeded handle it
    }
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

  state.InvalidateDisplayedTotalSize();

  // Cap local (non-cloud) mod-time I/O per frame so filter rebuild cannot stall FPS.
  if (LoadLocalTimeAttributesWithBudget(state.result_pool_->Results(), file_index,
                                        kFilterAttributeLoadsPerFrame)) {
    state.filteredResults.clear();
    state.filteredCount = state.result_pool_->Results().size();
    state.timeFilterCacheValid = false;
    return;
  }

  state.filteredResults =
    ApplyTimeFilter(state.result_pool_->Results(), state.timeFilter, file_index, &state, thread_pool);
  state.filteredCount = state.filteredResults.size();
  state.cachedTimeFilter = state.timeFilter;
  state.timeFilterCacheValid = true;
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
  if (!state.searchActive && state.result_pool_->Results().empty()) {
    UpdateFilterCacheForWelcomePanel(state.sizeFilteredResults, state.sizeFilteredCount,
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
    source_results = &state.result_pool_->Results();
    source_count = state.result_pool_->Results().size();
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

  // PERFORMANCE: Defer one frame only when an active filter still needs attribute I/O.
  // Keep time+size deferral consistent when both are active.
  if (state.deferFilterCacheRebuild) {
    if (!ShouldDeferFilterCacheRebuild(state)) {
      state.deferFilterCacheRebuild = false;
    } else {
      // Show unfiltered results for this frame, rebuild cache on next frame
      state.sizeFilteredResults.clear();
      state.sizeFilteredCount = source_count;
      state.deferFilterCacheRebuild = false;
      return;
    }
  }

  const bool filter_changed = (state.cachedSizeFilter != state.sizeFilter);
  const bool need_rebuild = !state.sizeFilterCacheValid || filter_changed || state.resultsUpdated ||
                            !state.timeFilterCacheValid;
  if (!need_rebuild) {  // NOSONAR(cpp:S6004) - Variable used after if block (line 499 and later)
    return;             // Cache is still valid
  }

  // Cap size I/O per frame so filter rebuild cannot stall FPS under disk pressure.
  if (LoadSizeAttributesWithBudget(*source_results, file_index, kFilterAttributeLoadsPerFrame)) {
    state.sizeFilteredResults.clear();
    state.sizeFilteredCount = source_count;
    state.sizeFilterCacheValid = false;
    return;
  }

  state.sizeFilteredResults = ApplySizeFilter(*source_results, state.sizeFilter, file_index);
  state.sizeFilteredCount = state.sizeFilteredResults.size();
  state.cachedSizeFilter = state.sizeFilter;
  state.sizeFilterCacheValid = true;

  // Piggyback: accumulate total size during size filter pass (sizes already loaded)
  state.displayedTotalSizeBytes = ComputeTotalFileBytes(state.sizeFilteredResults);
  state.displayedTotalSizeValid = true;
}

namespace {
/** Max file sizes to load per frame when computing displayed total; keeps UI responsive on first search. */
constexpr size_t kDisplayedTotalSizeLoadsPerFrame = 100;
/** Max items to scan per frame regardless of loading; prevents long loops on already-loaded data. */
constexpr size_t kDisplayedTotalSizeScansPerFrame = 10000;
}  // namespace

void UpdateDisplayedTotalSizeIfNeeded(GuiState& state, const FileIndex& file_index) {
  if (!state.resultsComplete) {
    return;
  }
  if (state.displayedTotalSizeValid) {
    return;  // Already computed (e.g. by size filter piggyback)
  }
  if (state.async_sort_.IsLoading()) {
    // Background tasks (StartSortAttributeLoading) are loading sizes into the FileIndex cache.
    // Defer: once loading completes, sizes will be cached and this function
    // will complete without any stat() calls on the render thread.
    return;
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
    display_results_ptr = &state.result_pool_->Results();
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
    const SearchResult& result = results[idx];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - idx < results.size() is the while-loop guard
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
constexpr std::byte kPendingSortFlagSizeLoaded{1};
constexpr std::byte kPendingSortFlagTimeLoaded{2};

static void PreparePendingSortAttributeBuffers(AsyncSortState& staging, size_t result_count,
                                               SortGeneration generation) {
  const std::scoped_lock lock(staging.mutex_);
  staging.sizes_.assign(result_count, kFileSizeNotLoaded);
  staging.times_.assign(result_count, kFileTimeNotLoaded);
  staging.flags_.assign(result_count, std::byte{0});
  staging.staging_generation_ = generation;
}

static void ApplyPendingSortAttributeUpdates(AsyncSortState& staging,
                                             std::vector<SearchResult>& results,
                                             SortGeneration sort_generation) {
  const std::scoped_lock lock(staging.mutex_);
  if (staging.staging_generation_ != sort_generation || staging.flags_.empty()) {
    return;
  }

  const size_t update_count = (std::min)(results.size(), staging.flags_.size());
  for (size_t i = 0; i < update_count; ++i) {
    const std::byte flags =
      staging.flags_[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < update_count <= flags_.size()
    if ((flags & kPendingSortFlagSizeLoaded) != std::byte{0}) {
      results[i].fileSize =  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < update_count <= results.size()
        staging.sizes_[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < update_count <= both vector sizes
    }
    if ((flags & kPendingSortFlagTimeLoaded) != std::byte{0}) {
      results[i].lastModificationTime =  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < update_count <= results.size()
        staging.times_[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < update_count <= both vector sizes
    }
  }
}

// Context bundle for sort-attribute task lambdas.
// IMPORTANT: always capture by VALUE in lambdas (`[..., ctx]` not `[..., &ctx]`).
// This struct is created as a stack-local in StartAttributeLoadingAsync and is destroyed
// when that function returns — before the thread-pool tasks execute. Capturing by reference
// would leave every ctx.* access in the task body as a use-after-free.
// Raw pointers (file_index, token, thread_pool) are safe because they point to GuiState
// members / application-lifetime objects that outlive all tasks. The shared_ptr counter
// keeps the atomic counter alive for the task's full duration.
struct SortAttributeEnqueueContext {
  const FileIndex* file_index_ = nullptr;
  const SortCancellationToken* token_ = nullptr;
  ThreadPool* thread_pool_ = nullptr;
  std::shared_ptr<std::atomic<int>> counter_;
};
// Verify that value-capture in lambdas is safe: all fields must be trivially copyable or
// reference-counted (shared_ptr). std::shared_ptr satisfies trivially copyable in some
// implementations but not all — check the members directly instead.
static_assert(std::is_trivially_copyable_v<const FileIndex*>);
static_assert(std::is_trivially_copyable_v<const SortCancellationToken*>);
static_assert(std::is_trivially_copyable_v<ThreadPool*>);

struct SortAttributeLoadItem {
  size_t index_ = 0;
  uint64_t file_id_ = 0;
  bool need_size_ = false;
  bool need_time_ = false;
};

constexpr size_t kSortAttributeLoadBatchSize = 64;

// Staging write helpers (Fix B): acquire staging.mutex_, bounds-check under the lock,
// then write. The bounds check guards against ResetPendingSortStaging racing between a task's
// second IsCancelled() check and this lock acquisition: if the UI cleared the staging vectors
// in that window, i >= size() and we silently discard (the generation was already reset so
// ApplyPendingSortAttributeUpdates will ignore any stale values anyway).
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - every write is guarded by the i < size() check immediately above it
static void WriteStagingBatch(AsyncSortState& staging,
                              const std::vector<SortAttributeLoadItem>& batch,
                              const std::vector<uint64_t>& loaded_sizes,
                              const std::vector<FILETIME>& loaded_times) {
  const std::scoped_lock lock(staging.mutex_);
  for (size_t b = 0; b < batch.size(); ++b) {
    const SortAttributeLoadItem& item = batch[b];
    if (item.index_ >= staging.flags_.size()) {
      continue;
    }
    if (item.need_size_) {
      staging.sizes_[item.index_] = loaded_sizes[b];
      staging.flags_[item.index_] |= kPendingSortFlagSizeLoaded;
    }
    if (item.need_time_) {
      staging.times_[item.index_] = loaded_times[b];
      staging.flags_[item.index_] |= kPendingSortFlagTimeLoaded;
    }
  }
}
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

static void RunAttributeLoadBatchTask(std::vector<SortAttributeLoadItem> batch,
                                      AsyncSortState& staging,
                                      const SortAttributeEnqueueContext& ctx) {
  if (ctx.token_->IsCancelled()) {
    ctx.counter_->fetch_sub(1, std::memory_order_acq_rel);
    return;
  }

  std::vector<uint64_t> loaded_sizes(batch.size(), kFileSizeNotLoaded);
  std::vector<FILETIME> loaded_times(batch.size(), kFileTimeNotLoaded);

  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - b < batch.size(); loaded_* sized to batch.size()  // NOSONAR(cpp:S125) - clang-tidy directive, not commented-out code
  for (size_t b = 0; b < batch.size(); ++b) {
    if (ctx.token_->IsCancelled()) {
      ctx.counter_->fetch_sub(1, std::memory_order_acq_rel);
      return;
    }
    const SortAttributeLoadItem& item = batch[b];
    if (item.need_size_ && item.need_time_) {
      loaded_sizes[b] = ctx.file_index_->GetFileSizeById(item.file_id_);
      loaded_times[b] = ctx.file_index_->GetFileModificationTimeById(item.file_id_);
    } else if (item.need_size_) {
      loaded_sizes[b] = ctx.file_index_->GetFileSizeById(item.file_id_);
    } else if (item.need_time_) {
      loaded_times[b] = ctx.file_index_->GetFileModificationTimeById(item.file_id_);
    }
  }
  // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

  if (!ctx.token_->IsCancelled()) {
    WriteStagingBatch(staging, batch, loaded_sizes, loaded_times);
  }
  ctx.counter_->fetch_sub(1, std::memory_order_acq_rel);
}

static void EnqueueAttributeLoadBatch(std::vector<SortAttributeLoadItem> batch,
                                      AsyncSortState& staging,
                                      const SortAttributeEnqueueContext& ctx) {
  if (batch.empty()) {
    return;
  }

  ctx.thread_pool_->submit([batch = std::move(batch), &staging, ctx]() mutable {
    RunAttributeLoadBatchTask(std::move(batch), staging, ctx);
  });
}

/**
 * @brief Start async loading of file attributes for sorting (non-blocking)
 *
 * Enqueues all size and modification time fetch tasks to the thread pool without
 * waiting for completion. Returns an atomic countdown counter initialised to the
 * number of tasks; each task decrements it on completion (including on cancellation).
 * Returns nullptr when there is nothing to load.
 *
 * @param state GUI state staging buffers used by background tasks
 * @param results Search results used for task discovery and file IDs
 * @param file_index File index for loading metadata
 * @param thread_pool Thread pool for parallel pre-fetching
 * @param token Cancellation token; tasks check it before performing I/O
 * @return Shared atomic counter (null if no tasks were enqueued)
 */
static std::shared_ptr<std::atomic<int>> StartAttributeLoadingAsync(
  AsyncSortState& staging, std::vector<SearchResult>& results, const FileIndex& file_index,
  ThreadPool& thread_pool, const SortCancellationToken& token, SortGeneration sort_generation) {
  if (results.empty()) {
    return nullptr;
  }

  // Collect rows needing attribute I/O, then enqueue batched tasks.
  std::vector<SortAttributeLoadItem> load_items;
  load_items.reserve(results.size());
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i bounded by results.size()
  for (size_t i = 0; i < results.size(); ++i) {
    const auto& result = results[i];
    const bool need_size = !result.isDirectory && result.fileSize == kFileSizeNotLoaded;
    const bool need_time = IsSentinelTime(result.lastModificationTime);
    if (!need_size && !need_time) {
      continue;
    }
    load_items.push_back(SortAttributeLoadItem{i, result.fileId, need_size, need_time});
  }
  // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

  if (load_items.empty()) {
    return nullptr;
  }

  const auto task_count = static_cast<int>(
      (load_items.size() + kSortAttributeLoadBatchSize - 1) / kSortAttributeLoadBatchSize);

  PreparePendingSortAttributeBuffers(staging, results.size(), sort_generation);
  auto counter = std::make_shared<std::atomic<int>>(task_count);
  const SortAttributeEnqueueContext enqueue_ctx{&file_index, &token, &thread_pool, counter};

  for (size_t batch_start = 0; batch_start < load_items.size();
       batch_start += kSortAttributeLoadBatchSize) {
    const size_t batch_end =
        (std::min)(batch_start + kSortAttributeLoadBatchSize, load_items.size());
    std::vector<SortAttributeLoadItem> batch(load_items.begin() + static_cast<std::ptrdiff_t>(batch_start),
                                             load_items.begin() + static_cast<std::ptrdiff_t>(batch_end));
    EnqueueAttributeLoadBatch(std::move(batch), staging, enqueue_ctx);
  }

  return counter;
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
    // Format size display string if needed (files only — directories show "Folder" in the UI).
    if (!result.isDirectory && result.fileSize != kFileSizeNotLoaded &&
        result.fileSize != kFileSizeFailed && result.fileSizeDisplay.empty()) {
      result.fileSizeDisplay = FormatFileSize(result.fileSize);
    }
    // Format modification time display string if needed (files and directories).
    if (!IsSentinelTime(result.lastModificationTime) &&
        !IsFailedTime(result.lastModificationTime) && result.lastModificationDisplay.empty()) {
      result.lastModificationDisplay = FormatFileTime(result.lastModificationTime);
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
  AsyncSortState local_staging{};

  // Blocking spin-wait until all tasks have decremented the counter to zero.
  if (const auto counter = StartAttributeLoadingAsync(local_staging, results, file_index,
                                                      thread_pool, dummy_token, 1);
      counter) {
    while (counter->load(std::memory_order_acquire) > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  // Apply staged attribute values from local_staging back into results.
  // StartAttributeLoadingAsync writes loaded sizes/times into local_staging
  // (the staging buffer design), not directly into results[i]. Without this apply step,
  // results[i].fileSize would remain kFileSizeNotLoaded and FormatDisplayStrings would
  // produce empty display strings; the subsequent sort would also order incorrectly.
  ApplyPendingSortAttributeUpdates(local_staging, results, 1);

  // Format the display strings (now that we have the data).
  FormatDisplayStrings(results);

  // Log lazy loading statistics after sorting (if any lazy loading occurred)
  // Must be called AFTER lazy loading completes to see actual statistics
  LazyAttributeLoader::LogStatistics();
}

void SortSearchResults(std::vector<SearchResult>& results, int column_index,
                       ImGuiSortDirection direction, FileIndex& file_index,
                       ThreadPool& thread_pool) {
  if (results.empty()) {
    return;
  }

  // Pre-fetch all necessary data for sorting using the thread pool.
  // This avoids blocking the UI thread on file I/O inside the sort lambda.
  if (column_index == ResultColumn::Size || column_index == ResultColumn::LastModified) {
    PrefetchAndFormatSortDataBlocking(results, file_index, thread_pool);
  }

  std::sort(results.begin(), results.end(),
            CreateSearchResultComparator(column_index, direction));
}

void StartSortAttributeLoading(GuiState& state, std::vector<SearchResult>& results,
                               int column_index, const FileIndex& file_index,
                               ThreadPool& thread_pool) {
  // Cancel any previously running sort operations.
  state.async_sort_.token.Cancel();

  // Brief non-blocking wait for cancelled tasks to finish (max 10ms).
  // Gives cancelled tasks time to see the cancellation flag and exit,
  // preventing use-after-free if results are replaced while tasks are running.
  WaitBrieflyForCancelledTasks(state.async_sort_.counter, kMaxWaitForCancelledTasksMs);

  // Hard safety barrier: do not reuse the results buffer for a new attribute-loading
  // batch until the previous cancelled batch has fully drained.
  WaitForCancelledTasksFully(state.async_sort_.counter);

  // Clear all in-flight state — counter, loading flag, and staging buffers.
  // Tasks still running hold their own shared_ptr copy of the counter and will
  // decrement safely; after the full wait above the old counter is already 0.
  state.async_sort_.Reset();

  // Only start async loading for Size or Last Modified columns
  if (column_index != ResultColumn::Size && column_index != ResultColumn::LastModified) {
    return;
  }

  if (results.empty()) {
    return;
  }

  // Install a fresh token and bump the generation counter atomically.
  // BeginNewSort() cancels (idempotent here) then replaces the token, so the
  // enqueue context below always captures a pointer to the current (uncancelled) token.
  const SortGeneration new_gen = state.async_sort_.BeginNewSort();
  assert(state.completed_sort_generation_ <= new_gen);

  // Start async attribute loading with the new generation.
  state.async_sort_.counter = StartAttributeLoadingAsync(
    state.async_sort_, results, file_index, thread_pool,
    state.async_sort_.token, new_gen);

  // Show "Loading attributes..." for the entire sort-by-Size/LastModified flow
  // (from now until sort completes). This ensures the status bar message is visible
  // even when no futures were created (e.g. all sizes/times already in results from MFT
  // or previous load), so the user sees that a sort-by-attributes operation is in progress.
  state.async_sort_.sort_ready_state_ = SortReadyState::Loading;

  // If no futures were created (all attributes already loaded), transition to
  // Ready immediately so the sort completes on the next frame.
  if (!state.async_sort_.counter) {
    state.async_sort_.sort_ready_state_ = SortReadyState::Ready;
  }
}

bool CheckSortAttributeLoadingAndSort(GuiState& state, std::vector<SearchResult>& results,
                                      int column_index, ImGuiSortDirection direction,
                                      SortGeneration sort_generation) {
  // Check if sort data is ready and handle completion
  if (!CheckAndHandleSortDataReady(state, sort_generation)) {
    return false;  // Still loading
  }

  // Transition back to Idle now that we're processing the ready state.
  // Reset() below will also set Idle, but clearing here is explicit.
  state.async_sort_.sort_ready_state_ = SortReadyState::Idle;

  // Apply staged updates on the UI thread to avoid any cross-thread writes to results.
  ApplyPendingSortAttributeUpdates(state.async_sort_, results, sort_generation);

  // Format display strings now that all data is loaded
  FormatDisplayStrings(results);

  // All attributes loaded - perform the sort
  std::sort(results.begin(), results.end(), CreateSearchResultComparator(column_index, direction));

  // Reset all in-flight state (counter, loading, staging buffers).
  state.async_sort_.Reset();
  state.completed_sort_generation_ = sort_generation;
  assert(state.completed_sort_generation_ <= state.async_sort_.generation_);

  // Log lazy loading statistics after async sorting completes
  // Must be called AFTER lazy loading completes to see actual statistics
  LazyAttributeLoader::LogStatistics();

  return true;  // Sort completed
}

uint64_t ComputeTotalFileBytes(const std::vector<SearchResult>& results) {
  uint64_t total = 0;
  for (const auto& r : results) {
    if (!r.isDirectory && r.fileSize != kFileSizeNotLoaded && r.fileSize != kFileSizeFailed) {
      total += r.fileSize;
    }
  }
  return total;
}

