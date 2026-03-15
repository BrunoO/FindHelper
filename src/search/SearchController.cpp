/**
 * @file SearchController.cpp
 * @brief Implementation of search orchestration and coordination logic
 *
 * This file implements the SearchController class, which centralizes all
 * high-level search orchestration on the UI side. It acts as a mediator
 * between GuiState, SearchWorker, and UsnMonitor, coordinating when searches
 * should be triggered and how results should be handled.
 *
 * RESPONSIBILITIES:
 * - Debounced instant search: Triggers searches as user types (with 400ms delay)
 * - Manual search: Immediate search triggered by button/Enter key
 * - Auto-refresh: Automatically re-runs search when index changes
 * - Result polling: Pulls completed results from SearchWorker and updates UI
 * - Search coordination: Prevents searches during index building
 *
 * ARCHITECTURE:
 * - Called once per frame from application_logic::Update()
 * - Non-blocking: All operations are asynchronous via SearchWorker
 * - State-driven: Decisions based on GuiState flags and timestamps
 * - Thread-safe: Coordinates between UI thread and SearchWorker thread
 *
 * SEARCH TRIGGERING:
 * 1. Debounced instant search: User types -> wait 400ms -> trigger if idle
 * 2. Manual search: Button/Enter -> immediate trigger (bypasses debounce)
 * 3. Auto-refresh: Index size changes -> trigger if enabled and idle
 *
 * RESULT HANDLING:
 * - Polls SearchWorker for new results each frame
 * - Updates GuiState with results when available
 * - Avoids unnecessary UI updates when results are unchanged
 * - Handles empty result sets gracefully
 *
 * PATH POOL LIFECYCLE (searchResultPathPool):
 * - SearchResult.fullPath is a std::string_view into GuiState::searchResultPathPool.
 * - Invariant: Never clear or reallocate the pool while any SearchResult in
 *   searchResults, partialResults, filteredResults, or sizeFilteredResults
 *   references it (or use-after-free / SIGSEGV in ResultsTable::Render).
 * - When clearing: clear result vectors and filter caches first, then clear the pool.
 * - When replacing (non-streaming PollResults): clear result vectors and caches,
 *   then clear pool, then MergeAndConvertToSearchResults, then assign new results.
 * - When appending (streaming): if reserve would reallocate, migrate to a new pool
 *   and update all partialResults[].fullPath to point into the new pool before
 *   appending (see ConsumePendingStreamingBatches).
 *
 * @see SearchController.h for class interface
 * @see SearchWorker.h for background search execution
 * @see ApplicationLogic.cpp for Update() call site
 * @see GuiState.h for state management
 */

#include "search/SearchController.h"
#include "search/StreamingResultsCollector.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iterator>
#include <numeric>

#include "core/Settings.h"
#include "index/FileIndex.h"
#include "search/FolderSizeAggregator.h"

// CompareFileTime is now in FileTimeTypes.h
#ifndef _WIN32
#include "utils/FileTimeTypes.h"
#endif  // _WIN32

#include "index/LazyValue.h"  // For kFileSizeNotLoaded, kFileSizeFailed
#include "search/SearchResultUtils.h"  // For CleanupAttributeLoadingFutures
#include "usn/UsnMonitor.h"
#include "utils/FileSystemUtils.h"
#include "utils/HashMapAliases.h"
#include "utils/Logger.h"

// Forward declarations for helper functions
static bool CompareSearchResults(const std::vector<SearchResult> &current_results,
                                 const std::vector<SearchResult> &new_results);
static inline bool HasFileSizeChanged(uint64_t current_size, uint64_t new_size);
static inline bool HasModificationTimeChanged(const FILETIME& current_time, const FILETIME& new_time);

namespace {

/**
 * @brief Wait for and cleanup a single attribute loading future
 *
 * Waits for the future to complete if needed, then calls .get() to clean up resources.
 * This prevents memory leaks from abandoned futures.
 *
 * @param future Future to cleanup
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - This is a function in anonymous namespace, not a global variable (clang-tidy false positive), naming follows project convention
void WaitAndCleanupFuture(std::future<void>& future) {
  if (future.valid()) {
    if (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      // Future is still running - wait for it to complete to avoid accessing invalid memory
      future.wait();
    }
    // CRITICAL: Call .get() to clean up the future and release associated resources
    // This prevents memory leaks from abandoned futures
    try {
      future.get();
    } catch (const std::exception& e) {  // NOSONAR(cpp:S2738) - Cleanup best-effort: log and swallow; future.get() must not propagate into UI loop
      LOG_ERROR_BUILD("Attribute loading future threw during cleanup: " << e.what());
    } catch (...) {  // NOSONAR(cpp:S2738, cpp:S2486) - Unknown exception; log generic message and swallow to guarantee cleanup
      LOG_ERROR_BUILD("Attribute loading future threw unknown exception during cleanup");
    }
  }
}

/**
 * @brief Invalidate filter caches and clear filtered result vectors
 *
 * Must be called whenever searchResults is replaced or cleared so that
 * GetDisplayResults does not return stale filtered data.
 *
 * @param state GUI state whose filter caches should be invalidated
 */
inline void InvalidateFilterCaches(GuiState& state) {
  state.InvalidateFilterCacheFlags();
  state.filteredResults.clear();
  state.sizeFilteredResults.clear();
}

/**
 * @brief Wait for and cleanup all attribute loading futures
 *
 * Cleans up all pending attribute loading futures before replacing results.
 * This prevents memory corruption: futures capture references to SearchResult objects
 * which would be invalid after results are replaced.
 *
 * @param state GUI state containing attribute loading futures
 */
void WaitForAllAttributeLoadingFutures(GuiState& state) {
  for (auto& future : state.attributeLoadingFutures) {
    WaitAndCleanupFuture(future);
  }
  state.attributeLoadingFutures.clear();
  state.loadingAttributes = false;

  // Also clean up cloud file loading futures
  for (auto& future : state.cloudFileLoadingFutures) {
    WaitAndCleanupFuture(future);
  }
  state.cloudFileLoadingFutures.clear();
  state.deferredCloudFiles.clear();
}

/**
 * @brief Determine if search results should be updated
 *
 * For partial results (incremental updates), always update to show progress.
 * For complete results, compare to avoid unnecessary updates (prevents blink on auto-refresh).
 *
 * @param is_complete Whether the search is complete
 * @param new_results New search results
 * @param current_results Current search results
 * @return True if results should be updated
 */
[[nodiscard]] bool ShouldUpdateResults(bool is_complete,
                                       const std::vector<SearchResult>& new_results,
                                       const std::vector<SearchResult>& current_results) {
  if (!is_complete) {
    // Partial results: always update (they're incremental by nature)
    return true;
  }

  // Complete results: only update if they actually changed
  // This prevents the "blink" effect when auto-refresh triggers but results are identical
  if (constexpr size_t kLargeResultSetThreshold = 100000; new_results.size() > kLargeResultSetThreshold) {  // NOLINT(readability-identifier-naming) - Constant follows project convention
    // OPTIMIZATION: Skip comparison for very large result sets (expensive)
    // For large sets, assume results changed (user will see update anyway)
    LOG_INFO("Large result set (" + std::to_string(new_results.size()) +
             " items), skipping comparison");
    return true;
  }

  const bool results_changed = CompareSearchResults(current_results, new_results);
  LOG_INFO("CompareSearchResults returned: " +
           std::string(results_changed ? "true (changed)" : "false (identical)"));
  return results_changed;
}

/**
 * @brief Update search results in GUI state
 *
 * Replaces current results with new results, handling cleanup of attribute loading futures.
 *
 * @param state GUI state to update
 * @param new_results New search results to set
 * @param is_complete Whether the search is complete
 */
void UpdateSearchResults(GuiState& state,
                         std::vector<SearchResult>&& new_results,  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved) - moved into state.searchResults
                         bool is_complete) {
  WaitForAllAttributeLoadingFutures(state);
  state.searchResults = std::move(new_results);
  // CRITICAL: Invalidate filter caches whenever searchResults is replaced.
  // GetDisplayResults checks valid flags and falls back to searchResults when invalid.
  // Clear the vectors so we do not hold stale filter data.
  InvalidateFilterCaches(state);
  state.InvalidateFolderStats();
  // Only set resultsUpdated when search is complete
  // This prevents auto re-sorting during incremental updates
  // Results are shown incrementally (unsorted) until complete, then sorted
  state.resultsUpdated = is_complete;
  // Defer filter cache rebuild for one frame when results first arrive
  // This prevents blocking the UI thread with expensive file attribute loading
  // on Windows (especially for cloud files like OneDrive)
  if (is_complete && !state.searchResults.empty()) {  // NOLINT(bugprone-use-after-move,hicpp-invalid-access-moved) - Check is_complete and empty() before move, state.searchResults is set from moved new_results
    state.deferFilterCacheRebuild = true;
  }
  if (!state.searchResultPathPool.empty()) {
    const auto pool_size = state.searchResultPathPool.size();
    assert(std::all_of(state.searchResults.begin(), state.searchResults.end(),  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
                       [&state, pool_size](const SearchResult& r) {
                         const auto* const pool_start = state.searchResultPathPool.data();
                         const auto pool_index =
                             static_cast<size_t>(r.fullPath.data() - pool_start);
                         return pool_index < pool_size;
                       }));
  }
  LOG_INFO("UI received search results: " +
           std::to_string(state.searchResults.size()) + " items" +
           (is_complete ? " (complete)" : " (partial)"));
}

// Streaming poll helpers (extracted to reduce PollResults cognitive complexity; cpp:S3776).

void ApplyStreamingErrorToState(GuiState& state,
                                 const StreamingResultsCollector& collector) {
  state.searchError = std::string(collector.GetError());
  if (!state.partialResults.empty()) {
    WaitForAllAttributeLoadingFutures(state);
    state.searchResults = std::move(state.partialResults);
    state.partialResults.clear();
  }
  InvalidateFilterCaches(state);
  state.resultsComplete = true;
  state.showingPartialResults = false;
  state.searchActive = false;
  // NOLINTNEXTLINE(readability-simplify-boolean-expr) - Combined assert keeps invariants explicit
  assert(state.resultsComplete && !state.showingPartialResults && !state.searchActive &&
         !state.searchError.empty());
}

void ConsumePendingStreamingBatches(GuiState& state,
                                    StreamingResultsCollector& collector,
                                    [[maybe_unused]] const FileIndex& file_index) {
  const std::vector<SearchResultData> pending_batches =
      collector.GetPendingBatchesUpTo(streaming_results_collector_constants::kMaxResultsPerFrame);
  if (pending_batches.empty()) {
    return;
  }
  if (!state.showingPartialResults) {
    state.partialResults.clear();
    state.searchResultPathPool.clear();
    state.showingPartialResults = true;
    state.resultsComplete = false;
    state.searchError = "";
    // Pre-reserve a reasonable number of slots to avoid repeated reallocation as partialResults
    // grows. kInitialPartialResultsReserve (100k) covers most searches without over-allocating.
    constexpr size_t kInitialPartialResultsReserve =
        streaming_results_collector_constants::kMaxResultsPerFrame * 20;  // NOLINT(readability-magic-numbers) - 20 × 5000 = 100k; documented inline
    state.partialResults.reserve(kInitialPartialResultsReserve);
  }

  // Compute bytes needed for the new batch (same formula as MergeAndConvertToSearchResults).
  const size_t pool_bytes_needed = std::accumulate(
      pending_batches.begin(), pending_batches.end(), size_t{0},
      [](size_t acc, const SearchResultData& datum) {
        return acc + datum.fullPath.size() + 1;
      });

  // If appending would reallocate the pool, existing partialResults[].fullPath would become
  // dangling (they point into the current buffer). Migrate to a new pool so we never invalidate.
  // Use exponential doubling (max of 2x capacity vs current+2x batch) to prevent migrations every
  // other frame: without this, reserve_target = size + 2*batch gives exactly one batch of headroom,
  // so the check fires again next frame at O(N) cost.
  const size_t reserve_target =
      (std::max)(state.searchResultPathPool.size() + (pool_bytes_needed * 2),
                 (state.searchResultPathPool.capacity() * 2));
  if (std::vector<char>& pool = state.searchResultPathPool;
      !pool.empty() && pool.capacity() < pool.size() + pool_bytes_needed) {
    const char* old_data = pool.data();
    std::vector<char> new_pool;
    new_pool.reserve(reserve_target);
    new_pool.assign(pool.begin(), pool.end());
    for (SearchResult& r : state.partialResults) {
      const auto offset = std::distance(old_data, r.fullPath.data());  // NOLINT(bugprone-suspicious-stringview-data-usage) - not used as C string; only for pointer offset
      r.fullPath = std::string_view(std::next(new_pool.data(), offset), r.fullPath.size());
    }
    pool = std::move(new_pool);
  }

  std::vector<SearchResult> converted =
      MergeAndConvertToSearchResults(state.searchResultPathPool, pending_batches, file_index);
  state.partialResults.insert(state.partialResults.end(),
                              std::make_move_iterator(converted.begin()),
                              std::make_move_iterator(converted.end()));

  state.resultsBatchNumber++;
  state.resultsUpdated = true;
}

void RefreshStaleAttributesForResult(SearchResult& r, const FileIndex& file_index) {
  if (r.isDirectory) {
    return;
  }
  const bool size_stale = (r.fileSize == kFileSizeNotLoaded);
  const bool time_stale = IsSentinelTime(r.lastModificationTime);
  if (!size_stale && !time_stale) {
    return;
  }
  const FileEntry* const entry = file_index.GetEntry(r.fileId);
  if (entry == nullptr) {
    return;
  }
  if (size_stale) {
    r.fileSize = entry->fileSize.IsNotLoaded() ? kFileSizeNotLoaded
                                               : entry->fileSize.GetValue();
  }
  if (time_stale) {
    r.lastModificationTime = entry->lastModificationTime.IsNotLoaded()
                                 ? kFileTimeNotLoaded
                                 : entry->lastModificationTime.GetValue();
  }
}

void FinalizeStreamingSearchComplete(GuiState& state, const FileIndex& file_index) {
  // All async attribute loading must complete before we read final values below.
  WaitForAllAttributeLoadingFutures(state);

  if (!state.partialResults.empty()) {
    // Refresh attributes that were sentinel (not yet loaded) when each batch was first
    // converted during streaming. Async loading is now complete after the Wait above.
    for (SearchResult& r : state.partialResults) {
      RefreshStaleAttributesForResult(r, file_index);
    }
    // Promote partialResults → searchResults directly. The searchResultPathPool is already
    // fully built during streaming (ConsumePendingStreamingBatches appended every batch);
    // all string_views in partialResults remain valid. This avoids re-running
    // MergeAndConvertToSearchResults (O(N) string copies + pool rebuild) at finalization.
    UpdateSearchResults(state, std::move(state.partialResults), true);
    state.partialResults.clear();
    // Debug invariant: every SearchResult.fullPath must point into the current pool.
    if (!state.searchResultPathPool.empty()) {
      const auto pool_size = state.searchResultPathPool.size();
      assert(std::all_of(state.searchResults.begin(), state.searchResults.end(),  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
                         [&state, pool_size](const SearchResult& r) {
                           const auto* const pool_start = state.searchResultPathPool.data();
                           const auto pool_index =
                               static_cast<size_t>(r.fullPath.data() - pool_start);
                           return pool_index < pool_size;
                         }));
    }
    LOG_INFO("Streaming search complete, total results: " +
             std::to_string(state.searchResults.size()));
  } else if (!state.resultsComplete) {
    state.searchResults.clear();
    state.partialResults.clear();
    InvalidateFilterCaches(state);
    state.searchResultPathPool.clear();
    state.resultsUpdated = true;
  }
  state.resultsComplete = true;
  state.showingPartialResults = false;
  state.searchActive = false;
  // Clear sortDataReady to invalidate any pending async sort from partial results.
  // Preserve lastSortColumn/lastSortDirection so ProcessImGuiSort re-applies the user's
  // sort when resultsUpdated is true (matches non-streaming behavior).
  state.sortDataReady = false;
  // NOLINTNEXTLINE(readability-simplify-boolean-expr) - Combined assert keeps invariants explicit
  assert(state.resultsComplete && !state.showingPartialResults && !state.searchActive);
}

/**
 * @brief Clear search results in GUI state
 *
 * Clears current results, handling cleanup of attribute loading futures.
 *
 * @param state GUI state to update
 * @param reason Reason for clearing (for logging)
 */
void ClearSearchResults(GuiState& state, FolderSizeAggregator* folder_aggregator, [[maybe_unused]] std::string_view reason) {
  WaitForAllAttributeLoadingFutures(state);
  // Clear result vectors BEFORE clearing the pool so no SearchResult.fullPath
  // (string_view into the pool) is left dangling. See path pool lifecycle in file header.
  state.searchResults.clear();
  state.partialResults.clear();
  InvalidateFilterCaches(state);
  state.InvalidateFolderStats();
  if (folder_aggregator != nullptr) {
    folder_aggregator->CancelPending();
  }
  state.searchResultPathPool.clear();
  // Clear marks for new searches (ClearSearchResults is called when a new search starts)
  state.markedFileIds.clear();
  // Reset sort state so a later CheckAndCompleteAsyncSort does not run a sort on new
  // results without loading Size/Time attributes (would sort by kFileSizeNotLoaded).
  state.sortDataReady = false;
  state.resultsUpdated = true;
  LOG_INFO("UI cleared search results: " + std::string(reason));
}

// Non-streaming poll: apply new results, clear, or update; reduces PollResults complexity (cpp:S3776).
void ApplyNonStreamingSearchResults(GuiState& state,
                                    std::vector<SearchResult>&& new_results,  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved) - forwarded to UpdateSearchResults with std::move
                                    bool is_complete) {
  if (is_complete) {
    state.searchActive = false;
    state.resultsComplete = true;
    state.showingPartialResults = false;
  }
  if (state.searchResults.empty() && !new_results.empty()) {
    UpdateSearchResults(state, std::move(new_results), is_complete);
    LOG_INFO("UI received search results (empty->populated): " +
             std::to_string(state.searchResults.size()) + " items" +
             (is_complete ? " (complete)" : " (partial)"));
    return;
  }
  if (is_complete && new_results.empty() && !state.searchResults.empty()) {
    ClearSearchResults(state, nullptr, "new search completed with 0 results (early return)");
    return;
  }
  if (const bool should_update = ShouldUpdateResults(is_complete, new_results, state.searchResults);
      should_update) {
    if (is_complete || !new_results.empty() || state.searchResults.empty()) {
      UpdateSearchResults(state, std::move(new_results), is_complete);
    } else {
      LOG_INFO("Skipping update: new partial results are empty but current results are not (waiting for complete)");
    }
    return;
  }
  if (is_complete) {
    if (new_results.empty() && !state.searchResults.empty()) {
      ClearSearchResults(state, nullptr, "search complete with 0 results (via else-if path)");
    } else {
      state.resultsUpdated = true;
      LOG_INFO("Search complete, results unchanged (" +
               std::to_string(state.searchResults.size()) + " items)");
    }
  }
}

} // namespace

SearchController::SearchController() = default;

void SearchController::Update(GuiState &state, SearchWorker &search_worker,  // NOLINT(readability-identifier-naming) - Parameter name follows project convention
                              FolderSizeAggregator* folder_aggregator,
                              const UsnMonitor *monitor, bool is_index_building,
                              const AppSettings& settings, const FileIndex& file_index) const {
  // When user explicitly clears (Clear All or Escape), discard worker's cached results
  // so PollResults does not re-apply from the streaming collector on the next frame.
  // Only discard when worker is idle to avoid use-after-free. If busy, cancel to finish
  // faster; keep flag so we discard and skip streaming path on next frame.
  if (state.clearResultsRequested) {
    if (!search_worker.IsBusy()) {
      search_worker.DiscardResults();
      state.clearResultsRequested = false;
    } else {
      search_worker.CancelSearch();
    }
  }

  // Don't allow searches while index is being built or finalizing
  // This prevents race condition where search uses offsets that become invalid
  // when FinalizeInitialPopulation() clears and rebuilds path_storage_
  if (is_index_building || (monitor != nullptr && monitor->IsPopulatingIndex())) {
    PollResults(state, search_worker, folder_aggregator, file_index);
    return;
  }

  // Handle debounced instant search (search-as-you-type)
  if (ShouldTriggerDebouncedSearch(state, search_worker)) {
    ClearSearchResults(state, folder_aggregator, "debounced search started");
    assert(state.searchResults.empty());
    state.inputChanged = false;
    state.searchActive = true;
    state.partialResults.clear();
    state.resultsComplete = false;
    state.showingPartialResults = false;
    state.searchWasManual = false; // Instant search is not manual
    search_worker.SetStreamPartialResults(settings.streamPartialResults);
    search_worker.StartSearch(state.BuildCurrentSearchParams(), &settings);
  }

  // Handle auto-refresh when index changes
  const size_t current_index_size = (monitor != nullptr) ? monitor->GetIndexedFileCount() : 0;
  HandleAutoRefresh(state, search_worker, current_index_size, settings);

  // Poll for results
  PollResults(state, search_worker, folder_aggregator, file_index);
}

void SearchController::TriggerManualSearch(GuiState &state,
                                           SearchWorker &search_worker,
                                           FolderSizeAggregator* folder_aggregator,
                                           const AppSettings& settings) const {  // NOLINT(readability-identifier-naming) - Parameter name follows project convention
  // Don't allow manual search if index is still being built
  // (This is a safety check - the UI should disable the button, but this
  // prevents
  //  any edge cases where the button might still be clickable)
  // Note: monitor parameter not available here, but Update() already handles
  // this
  // Clear previous results and filter caches so we don't show stale count
  // (otherwise with a filter active the UI shows old filtered N, then "drops" to
  // first partial batch size when streaming starts)
  ClearSearchResults(state, folder_aggregator, "manual search started");
  assert(state.searchResults.empty());
  state.inputChanged = false; // Reset debounce state when manually searching
  state.searchActive = true;
  state.partialResults.clear();
  state.resultsComplete = false;
  state.showingPartialResults = false;
  state.searchWasManual = true; // Mark as manually triggered
  search_worker.SetStreamPartialResults(settings.streamPartialResults);
  search_worker.StartSearch(state.BuildCurrentSearchParams(), &settings);
}

void SearchController::HandleAutoRefresh(GuiState &state,
                                         SearchWorker &search_worker,  // NOLINT(readability-identifier-naming) - Parameter name follows project convention
                                         size_t current_index_size,
                                         const AppSettings& settings) const {
  // Auto-refresh logic (triggered by index changes, not user input).
  // NOTE:
  // - We no longer require state.searchActive to be true here.
  //   Previously, searchActive was cleared as soon as a search completed,
  //   which prevented auto-refresh from ever triggering after the first search.
  // - Instead, we require that we have existing search results. This ensures
  //   auto-refresh is only active after at least one successful search, and
  //   uses the last search parameters for refreshes.
  if (!state.autoRefresh) {
    return;
  }

  // Require an existing search result set so we know what to auto-refresh.
  if (state.searchResults.empty()) {
    return;
  }

  // Only trigger when the indexed file count has changed since the last
  // auto-refresh baseline. This baseline is updated each time we trigger.
  if (current_index_size == state.lastIndexSize) {
    return;
  }

  // Only trigger auto-refresh if the worker is not busy
  if (search_worker.IsBusy()) {
    return;
  }

  const size_t previous_results_size = state.searchResults.size();

  state.lastIndexSize = current_index_size;
  // NOTE: Do not clear results up front for auto-refresh. Keeping existing searchResults
  // visible until new data arrives avoids a transient 0-row frame (see
  // 2026-02-24_RESULTS_AUTO_REFRESH_DOUBLE_BUFFERING_SPEC.md §9).
  state.inputChanged = false;      // Reset debounce state for auto-refresh
  state.searchActive = true;
  // Keep previous completed results in searchResults to avoid a 0-row frame,
  // but clear any stale streaming partials so a new stream starts from a clean slate.
  state.partialResults.clear();
  state.resultsComplete = false;
  state.showingPartialResults = false;
  state.searchWasManual = false;   // Auto-refresh is not manual
  search_worker.SetStreamPartialResults(settings.streamPartialResults);
  search_worker.StartSearch(state.BuildCurrentSearchParams(), &settings);
  assert(state.searchResults.size() == previous_results_size);
}

// Helper structure to compare SearchResult metadata efficiently
struct ResultMetadata {
  uint64_t fileSize = 0;  // NOLINT(readability-identifier-naming,readability-redundant-member-init) - explicit default for clarity
  FILETIME lastModificationTime{};  // NOLINT(readability-identifier-naming,readability-redundant-member-init)
  std::string filename{};  // NOLINT(readability-redundant-member-init)
  std::string fullPath{};  // NOLINT(readability-identifier-naming,readability-redundant-member-init)

  bool operator==(const ResultMetadata &other) const {  // NOSONAR(cpp:S2807) - Member operator is acceptable; could be refactored to hidden friend later if needed
    return fileSize == other.fileSize &&
           CompareFileTime(&lastModificationTime,
                           &other.lastModificationTime) == 0 &&
           filename == other.filename && fullPath == other.fullPath;
  }
};

// Helper function to check if file size changed (with smart handling for loaded/unloaded states)
// Returns true if size actually changed, false if difference is just loaded->unloaded downgrade
static inline bool HasFileSizeChanged(uint64_t current_size, uint64_t new_size) {
  if (current_size == new_size) {
    return false;
  }
  const bool current_loaded = (current_size != kFileSizeNotLoaded && current_size != kFileSizeFailed);
  const bool new_unloaded = (new_size == kFileSizeNotLoaded);
  // Ignore downgrade from loaded to unloaded (UI will preserve loaded value)
  return !(current_loaded && new_unloaded);
}

// Helper function to check if modification time changed (with smart handling for loaded/unloaded states)
// Returns true if time actually changed, false if difference is just loaded->unloaded downgrade
static inline bool HasModificationTimeChanged(const FILETIME& current_time, const FILETIME& new_time) {
  if (CompareFileTime(&current_time, &new_time) == 0) {
    return false;
  }
  const bool current_loaded = !IsSentinelTime(current_time) && !IsFailedTime(current_time);
  const bool new_unloaded = IsSentinelTime(new_time);
  // Ignore downgrade from loaded to unloaded (UI will preserve loaded value)
  return !(current_loaded && new_unloaded);
}

// Compare new search results with current results to detect if they actually
// changed Returns true if results changed (different files or metadata), false
// if identical
static bool CompareSearchResults(const std::vector<SearchResult> &current_results,
                     const std::vector<SearchResult> &new_results) {
  const ScopedTimer timer("SearchController - Compare Results");

  // Quick size check
  if (new_results.size() != current_results.size()) {
    return true; // Results changed (files added/removed)
  }

  // CRITICAL: If new results are empty but current are not, this is a change
  // (results were cleared/removed)
  if (new_results.empty()) {
    // If current results are also empty, no change
    // If current results are not empty, this is a change (results were cleared)
    return !current_results.empty();
  }

  // CRITICAL: If current results are empty but new are not, this is a change
  if (current_results.empty()) {
    return true; // Results were populated
  }

  // Build metadata map for current results (indexed by fileId for O(1) lookup)
  hash_map_t<uint64_t, ResultMetadata> current_metadata;
  current_metadata.reserve(current_results.size());
  for (const auto &result : current_results) {
    ResultMetadata meta;
    meta.fileSize = result.fileSize;
    meta.lastModificationTime = result.lastModificationTime;
    meta.filename = std::string(result.GetFilename());
    meta.fullPath = std::string(result.fullPath);
    current_metadata[result.fileId] = std::move(meta);
  }

  // Compare new results with current metadata
  for (const auto &new_result : new_results) {
    auto it = current_metadata.find(new_result.fileId);

    // If file ID not found, results changed (file added/removed)
    if (it == current_metadata.end()) {
      return true;
    }

    // Compare metadata for this file
    const ResultMetadata &current_meta = it->second;

    // Check basic immutable fields (Filename, FullPath) - Strict check
    if (current_meta.filename != new_result.GetFilename() ||
        current_meta.fullPath != new_result.fullPath) {
      return true;
    }

    // Check mutable fields (Size, Time) - Smart check
    // We want to avoid returning "true" (changed) if the only difference is
    // that the current result has loaded data and the new result is "Not
    // Loaded". This happens because new results from SearchWorker always start
    // as "Not Loaded", while the UI may have lazily loaded the data.

    // 1. File Size
    if (HasFileSizeChanged(current_meta.fileSize, new_result.fileSize)) {
      return true; // Real change (e.g., size actually changed)
    }

    // 2. Modification Time
    if (HasModificationTimeChanged(current_meta.lastModificationTime, new_result.lastModificationTime)) {
      return true; // Real change
    }
  }

  return false; // Results are identical (or compatible)
}

void SearchController::PollResults(GuiState &state,  // NOLINT(readability-function-cognitive-complexity) - Streaming vs non-streaming dispatch with error/clear guards; extraction would require many shared parameters
                                    SearchWorker &search_worker,  // NOLINT(readability-identifier-naming) - Parameter name follows project convention
                                    [[maybe_unused]] FolderSizeAggregator* folder_aggregator,
                                    const FileIndex &file_index) const {
  // Check for streaming results first
  if (auto* collector = search_worker.GetStreamingCollector()) {
    assert(!(state.showingPartialResults && state.resultsComplete));

    // When user cleared (Clear All during active search), skip streaming path so we
    // don't repopulate the UI. Discard will happen when worker becomes idle.
    if (state.clearResultsRequested) {
      return;
    }

    if (!collector->HasNewBatch() && !collector->IsSearchComplete()) {
      return;
    }
    if (collector->HasError()) {
      ApplyStreamingErrorToState(state, *collector);
      if (!search_worker.IsBusy()) {
        search_worker.DiscardResults();
      }
      return;
    }
    if (collector->IsSearchComplete()) {
      // Search is done — drain all remaining batches this frame so results appear immediately.
      // The per-frame cap (kMaxResultsPerFrame) exists to keep the UI responsive while the
      // search is still running. Once complete there is no benefit to spreading across frames;
      // draining in a loop gives the same total O(N) work but collapses the 20-frame wait
      // (100k results at 5000/frame) into a single frame.
      while (collector->HasNewBatch()) {
        ConsumePendingStreamingBatches(state, *collector, file_index);
      }
      FinalizeStreamingSearchComplete(state, file_index);
      // Discard collector so we don't re-apply every frame. Without this, sort does not
      // persist: each frame we would overwrite searchResults and reset lastSortColumn.
      // Only discard when worker is idle to avoid use-after-free (worker may still be
      // in ProcessStreamingSearchFutures).
      if (!search_worker.IsBusy()) {
        search_worker.DiscardResults();
        assert(search_worker.GetStreamingCollector() == nullptr);
      }
    }
    return;
  }

  // NON-STREAMING PATH (Standard)
  if (!search_worker.HasNewResults()) {
    return;
  }
  // Do not apply results from a search that was already superseded (e.g. auto-refresh
  // or debounced search started this frame after the worker completed). Skip consume;
  // the next run will replace results_data_ when it completes.
  if (search_worker.IsBusy()) {
    return;
  }

  WaitForAllAttributeLoadingFutures(state);

  const std::vector<SearchResultData> data = search_worker.GetResultsData();
  const bool is_complete = search_worker.IsSearchComplete();

  // Clear result vectors and caches BEFORE clearing the pool so no SearchResult.fullPath
  // (string_view into the pool) is left dangling. See path pool lifecycle in file header.
  state.searchResults.clear();
  InvalidateFilterCaches(state);
  state.searchResultPathPool.clear();

  std::vector<SearchResult> new_results =
      MergeAndConvertToSearchResults(state.searchResultPathPool, data, file_index);
  ApplyNonStreamingSearchResults(state, std::move(new_results), is_complete);
}

bool SearchController::ShouldTriggerDebouncedSearch(
    const GuiState &state, const SearchWorker &search_worker) const {  // NOLINT(readability-identifier-naming) - Parameter name follows project convention
  // Instant search: triggers when user types (with debounce)
  // Separate from auto-refresh which triggers on index changes
  if (!state.instantSearch || !state.inputChanged) {
    return false;
  }

  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     now - state.lastInputTime)
                     .count();

  return (elapsed >= SearchController::kDebounceDelayMs && !search_worker.IsBusy());
}
