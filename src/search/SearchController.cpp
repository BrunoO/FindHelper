/**
 * @file SearchController.cpp
 * @brief Implementation of search orchestration and coordination logic
 *
 * This file implements the SearchController class, which centralizes all
 * high-level search orchestration on the UI side. It acts as a mediator
 * between GuiState and SearchWorker, coordinating when searches
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
 * PATH POOL LIFECYCLE (result_pool_):
 * - SearchResult.fullPath is a std::string_view into GuiState::result_pool_->Pool().
 * - Invariant: Never clear or reallocate the pool while any SearchResult in
 *   result_pool_->Results(), filteredResults, or sizeFilteredResults references it
 *   (or use-after-free / SIGSEGV in ResultsTable::Render).
 * - When clearing: use result_pool_->Clear() which enforces results-before-pool ordering.
 * - When replacing (PollResults): use ClearResultPool() then
 *   MergeAndConvertToSearchResults, then assign new results via result_pool_->Results().
 *
 * @see SearchController.h for class interface
 * @see SearchWorker.h for background search execution
 * @see ApplicationLogic.cpp for Update() call site
 * @see GuiState.h for state management
 */

#include "search/SearchController.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <iterator>
#include <memory>
#include <thread>

#include "core/Settings.h"
#include "index/FileIndex.h"
#include "search/FolderSizeAggregator.h"

// CompareFileTime is now in FileTimeTypes.h
#ifndef _WIN32
#include "utils/FileTimeTypes.h"
#endif  // _WIN32

#include "index/LazyValue.h"  // For kFileSizeNotLoaded, kFileSizeFailed
#include "search/SearchResultUtils.h"
#include "search/SearchResultsService.h"  // For GetDisplayResults (selection remap)
#include "utils/FileSystemUtils.h"
#include "utils/Logger.h"

namespace {

void AssertSearchResultsFullPathsInPathPool(const GuiState& state) {
  if (state.result_pool_->Pool().empty()) {
    return;
  }
  const auto pool_size = state.result_pool_->Pool().size();
  assert(std::all_of(state.result_pool_->Results().begin(), state.result_pool_->Results().end(),  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
                     [&state, pool_size](const SearchResult& r) {
                       const auto* const pool_start = state.result_pool_->Pool().data();
                       const auto pool_index =
                           static_cast<size_t>(r.fullPath.data() - pool_start);
                       return pool_index < pool_size;
                     }));
}

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
 * @brief Clear the result path pool and enforce all associated invariants.
 *
 * Call this instead of state.result_pool_->Pool().clear() directly. It
 * atomically clears the results vector, clears the pool (results-before-pool
 * ordering is enforced by ResultPoolOwner::Clear()), invalidates filter caches
 * (which hold SearchResult copies with fullPath string_views into the pool), and
 * increments the batch number so IncrementalSearchState::CheckBatchNumber
 * fires on the next frame and drops its own stale copies.
 */
inline void ClearResultPool(GuiState& state) {
  InvalidateFilterCaches(state);
  state.result_pool_->Clear();
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
  // Cancel first so queued tasks skip I/O and decrement the counter immediately,
  // keeping the spin-wait below from blocking for the full I/O duration.
  state.async_sort_.token.Cancel();
  // Spin-wait for all attribute loading tasks to decrement the counter to zero.
  while (state.async_sort_.counter &&
         state.async_sort_.counter->load(std::memory_order_acquire) > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  // Hard-reset async sort state whenever results are being replaced/cancelled.
  // Capture generation before Reset() since generation_ is not zeroed by Reset().
  state.completed_sort_generation_ = state.async_sort_.generation_;
  state.async_sort_.Reset();  // Sets sort_ready_state_ = Idle.

  // Also clean up cloud file loading futures
  for (auto& future : state.cloudFileLoadingFutures) {
    WaitAndCleanupFuture(future);
  }
  state.cloudFileLoadingFutures.clear();
  state.deferredCloudFiles.clear();
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
void UpdateSearchResults(
    GuiState& state,
    std::vector<SearchResult>&& new_results,  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved) - moved into state.result_pool_->Results()
    FolderSizeAggregator* folder_aggregator) {
  WaitForAllAttributeLoadingFutures(state);
  // Cancel stale folder-size requests before replacing results to avoid overlap
  // between old queued jobs and the new result handoff.
  if (folder_aggregator != nullptr) {
    folder_aggregator->CancelPending();
  }
  // Remap selection BEFORE replacing searchResults: string_views in the old display
  // reference the current path pool, which is invalidated after the move below.
  // Entity-based remap: items still present in new_results stay selected at their new
  // positions; items that disappeared are deselected. This handles both manual new
  // searches (old paths absent → selection clears) and auto-refresh (surviving files
  // tracked to their new row indices). The sort path (HandleTableSorting with
  // resultsUpdated=true) handles the active-sort case and re-remaps after sorting.
  if (!state.selection.GetSelectedRows().empty()) {
    state.selection.RemapSelectionAfterDisplayResultsChange(
        *search::SearchResultsService::GetDisplayResults(state), new_results);
  }
  state.result_pool_->Results() = std::move(new_results);
  // CRITICAL: Invalidate filter caches whenever searchResults is replaced.
  // GetDisplayResults checks valid flags and falls back to searchResults when invalid.
  // Clear the vectors so we do not hold stale filter data.
  InvalidateFilterCaches(state);
  // Increment so IncrementalSearchState::CheckBatchNumber detects the change and clears
  // its filtered_results_ cache, which holds SearchResult copies whose fullPath views
  // point into the (now replaced) path pool. Without this, those views dangle if the
  // pool reallocates, causing a UAF when the render loop reads result.fullPath.
  state.result_pool_->BumpBatchNumber();
  state.resultsUpdated = true;
  // Defer filter cache rebuild for one frame when results first arrive to avoid
  // blocking the UI thread with expensive file attribute loading (e.g. OneDrive).
  if (!state.result_pool_->Results().empty()) {  // NOLINT(bugprone-use-after-move,hicpp-invalid-access-moved) - empty() checked after move into Results(); pool data is still valid
    state.deferFilterCacheRebuild = true;
  }
  AssertSearchResultsFullPathsInPathPool(state);
  LOG_INFO("UI received search results: " +
           std::to_string(state.result_pool_->Results().size()) + " items");
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
  state.searchSessionId++;
  if (folder_aggregator != nullptr) {
    folder_aggregator->CancelPending();
  }
  ClearResultPool(state);
  // Clear marks for new searches (ClearSearchResults is called when a new search starts)
  state.selection.markedFileIds.clear();
  // Reset sort state so a later CheckAndCompleteAsyncSort does not run a sort on new
  // results without loading Size/Time attributes (would sort by kFileSizeNotLoaded).
  state.async_sort_.sort_ready_state_ = SortReadyState::Idle;
  state.resultsUpdated = true;
  LOG_INFO("UI cleared search results: " + std::string(reason));
}

// Poll helper: apply new results or mark complete with zero results.
// Called from PollResults after ClearResultPool, so state.result_pool_->Results() is always empty.
void ApplyNonStreamingSearchResults(
    GuiState& state,
    std::vector<SearchResult>&& new_results,  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved) - forwarded to UpdateSearchResults with std::move
    FolderSizeAggregator* folder_aggregator) {
  state.searchActive = false;
  state.resultsComplete = true;
  if (!new_results.empty()) {
    UpdateSearchResults(state, std::move(new_results), folder_aggregator);
    return;
  }
  // Search completed with zero matches; pool already cleared by ClearResultPool.
  state.resultsUpdated = true;
  LOG_INFO("Search complete, 0 results");
}

} // namespace

SearchController::SearchController() = default;

void SearchController::Update(GuiState &state, SearchWorker &search_worker,  // NOLINT(readability-identifier-naming) - Parameter name follows project convention
                              FolderSizeAggregator* folder_aggregator,
                              size_t current_index_size, bool is_index_building,
                              const AppSettings& settings, const FileIndex& file_index) const {
  // When user explicitly clears (Clear All or Escape), discard worker's cached results
  // so PollResults does not re-apply stale results on the next frame.
  // Only discard when worker is idle to avoid use-after-free. If busy, cancel to finish
  // faster; keep flag so we discard on the next frame.
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
  // Caller must pass Application::IsIndexBuilding() (includes monitor population).
  if (is_index_building) {
    PollResults(state, search_worker, folder_aggregator, file_index);
    return;
  }

  // Handle debounced instant search (search-as-you-type)
  if (ShouldTriggerDebouncedSearch(state, search_worker)) {
    ClearSearchResults(state, folder_aggregator, "debounced search started");
    assert(state.result_pool_->Results().empty());
    state.inputChanged = false;
    state.searchActive = true;
    state.resultsComplete = false;
    state.searchWasManual = false; // Instant search is not manual
    search_worker.StartSearch(state.BuildCurrentSearchParams(), &settings);
  }

  // Handle auto-refresh when index changes
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
  // Clear previous results and filter caches so stale counts are not shown.
  // ClearSearchResults → WaitForAllAttributeLoadingFutures cancels, drains, and resets
  // the attribute loading counter in one step. Do not call CleanupAttributeLoadingFutures
  // here first: it would drop the counter shared_ptr, making the subsequent spin-wait a
  // no-op and leaving tasks that are mid-I/O free to write into the results we are about
  // to replace — a use-after-free risk even with Fix B's bounds guard.
  state.async_sort_.sort_ready_state_ = SortReadyState::Idle;
  ClearSearchResults(state, folder_aggregator, "manual search started");
  assert(state.result_pool_->Results().empty());
  state.inputChanged = false; // Reset debounce state when manually searching
  state.searchActive = true;
  state.resultsComplete = false;
  state.searchWasManual = true; // Manual search: record in history on completion
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
  if (state.result_pool_->Results().empty()) {
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

  // Enforce a cooldown between auto-refresh triggers. Under heavy USN activity (bulk file
  // operations), the index size changes on nearly every frame. Without this guard the app enters
  // a continuous search-retrigger loop that keeps search_worker busy indefinitely, prevents the
  // idle throttle from engaging, and saturates the main thread with result processing work.
  constexpr auto kAutoRefreshCooldown = std::chrono::milliseconds(500);
  const auto now = std::chrono::steady_clock::now();
  if (now - state.lastAutoRefreshTime < kAutoRefreshCooldown) {
    return;
  }

  const size_t previous_results_size = state.result_pool_->Results().size();

  state.lastIndexSize = current_index_size;
  state.lastAutoRefreshTime = now;
  // Signal sort tasks to skip I/O and exit. Do NOT call CleanupAttributeLoadingFutures here:
  // that would drop the counter shared_ptr and make WaitForAllAttributeLoadingFutures (called
  // in UpdateSearchResults when the new search completes) a no-op spin-wait. With the counter
  // alive, the finalization path can drain any in-flight task that slipped past the cancellation
  // check before replacing searchResults — the critical safety barrier against use-after-free
  // in the staging write path.
  state.async_sort_.token.Cancel();
  state.async_sort_.sort_ready_state_ = SortReadyState::Idle;
  state.inputChanged = false;      // Reset debounce state for auto-refresh
  state.searchActive = true;
  // Keep previous completed results in searchResults to avoid a 0-row frame
  // until new results arrive (see 2026-02-24_RESULTS_AUTO_REFRESH_DOUBLE_BUFFERING_SPEC.md §9).
  state.resultsComplete = false;
  state.searchWasManual = false;   // Auto-refresh is not manual: do not record in history
  search_worker.StartSearch(state.BuildCurrentSearchParams(), &settings);
  assert(state.result_pool_->Results().size() == previous_results_size);
}

void SearchController::PollResults(GuiState &state,
                                    SearchWorker &search_worker,  // NOLINT(readability-identifier-naming) - Parameter name follows project convention
                                    FolderSizeAggregator* folder_aggregator,
                                    const FileIndex &file_index) const {
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

  // ClearResultPool clears vectors, invalidates caches, clears the pool, and bumps
  // batch_number_ atomically. ApplyNonStreamingSearchResults (end-of-search handoff)
  // → UpdateSearchResults bumps batch_number_ once more for the incoming rows; the double-increment
  // is harmless since IncrementalSearchState::CheckBatchNumber tests != not a specific value.
  ClearResultPool(state);

  std::vector<SearchResult> new_results =
      MergeAndConvertToSearchResults(state.result_pool_->Pool(), data, file_index);
  ApplyNonStreamingSearchResults(state, std::move(new_results), folder_aggregator);
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
