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
 *   result_pool_->Results(), filter_caches.time.results, or filter_caches.size.results references it
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

#include "ctrack.hpp"
#include "search/SearchControllerDetail.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <memory>
#include <thread>

#include "core/Settings.h"
#include "index/FileIndex.h"
#include "search/FolderSizeAggregator.h"
#include "search/SearchWorker.h"

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
                       const auto* const ptr = r.fullPath.data();
                       const auto pool_start_addr =
                           reinterpret_cast<std::uintptr_t>(pool_start);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) - debug assert: pointer range check needs integer addresses
                       const auto ptr_addr =
                           reinterpret_cast<std::uintptr_t>(ptr);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) - debug assert: pointer range check needs integer addresses
                       if (ptr_addr < pool_start_addr) {
                         return false;
                       }
                       const auto offset = static_cast<size_t>(ptr_addr - pool_start_addr);
                       if (offset > pool_size) {
                         return false;
                       }
                       const auto full_path_size = r.fullPath.size();
                       return full_path_size <= (pool_size - offset);
                     }));
}

/**
 * @brief Invalidate filter caches and clear filtered result vectors
 *
 * Must be called whenever searchResults is replaced or cleared so that
 * GetDisplayResults does not return stale filtered data.
 *
 * @param state GUI state whose filter caches should be invalidated
 */
void InvalidateFilterCaches(GuiState& state) {
  state.InvalidateFilterCacheFlags();
  state.filter_caches.time.results.clear();
  state.filter_caches.size.results.clear();
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
  {
    // Instrumented: spin-wait duration = how long the UI thread stalls waiting for
    // in-flight/cancelled sort-attribute tasks to drain (suspect in PollResults spikes).
    CTRACK_NAME("SearchController::WaitSortTasksDrain");  // NOLINT(misc-const-correctness)
    while (state.async_sort_.counter &&
           state.async_sort_.counter->load(std::memory_order_acquire) > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  // Hard-reset async sort state whenever results are being replaced/cancelled.
  // Capture generation before Reset() since generation_ is not zeroed by Reset().
  state.completed_sort_generation_ = state.async_sort_.generation_;
  state.async_sort_.Reset();  // Sets sort_ready_state_ = Idle.

  {
    // Instrumented: future::get() blocks for the full remaining I/O of cloud-file
    // loads (suspect in PollResults spikes).
    CTRACK_NAME("SearchController::CleanupCloudFutures");  // NOLINT(misc-const-correctness)
  }
  state.cloud_files.WaitAndDrain();
}


/**
 * @brief Clear FolderSizeAggregator results_ and bump its generation.
 *
 * Updates GuiState baseline fields. Optionally clears Size / Descendant Files*
 * already written into the current result rows so the next render re-requests.
 */
void ResetFolderSizeAggregatorCache(GuiState& state,
                                     FolderSizeAggregator& folder_aggregator,
                                     size_t index_size_baseline,
                                     bool invalidate_written_result_fields) {
  folder_aggregator.Reset();
  if (invalidate_written_result_fields) {
    for (auto& result : state.result_pool_->Results()) {
      if (!result.isDirectory) {
        continue;
      }
      result.fileSize = kFileSizeNotLoaded;
      result.fileSizeDisplay = "...";
      result.folderFileCount = kFolderFileCountNotLoaded;
      result.folderFileCountDisplay = "...";
    }
    // The rows were re-pended in place without bumping the results version, and
    // Reset() emptied the queue (HasPendingWork()==false) — an armed flush
    // fast-path would skip the rescan and strand these rows at "..." until the
    // next commit. Force one full scan; the scan re-arms the fast-path itself.
    state.search_pipeline.folder_stats_flush_had_pending = true;
  }
  state.search_pipeline.last_folder_aggregator_index_size = index_size_baseline;
  state.search_pipeline.pending_folder_aggregator_index_size = index_size_baseline;
  state.search_pipeline.last_folder_aggregator_reset_time = std::chrono::steady_clock::now();
  state.search_pipeline.folder_aggregator_cache_stale = false;
}

// Minimum time between FolderSizeAggregator full recomputes while the cache is stale.
// No index-stability wait: bulk USN may still be running; we just cap scan frequency.
constexpr auto kFolderAggregatorMinRefreshInterval = std::chrono::minutes(5);

[[nodiscard]] bool FolderAggregatorRefreshIntervalElapsed(const GuiState& state) {
  const auto now = std::chrono::steady_clock::now();
  return now - state.search_pipeline.last_folder_aggregator_reset_time >= kFolderAggregatorMinRefreshInterval;
}

/**
 * @brief On new-search handoff: Reset aggregator at most every refresh interval if stale.
 *
 * CancelPending keeps results_ so folders that reappear skip recomputation. After inserts
 * or deletes the cache is wrong, but Reset on every auto-refresh re-scans the whole index.
 * Throttle to kFolderAggregatorMinRefreshInterval (live Sync uses the same gate).
 */
void CancelOrResetFolderSizeAggregator(GuiState& state,
                                       FolderSizeAggregator* folder_aggregator) {
  if (folder_aggregator == nullptr) {
    return;
  }
  if (state.search_pipeline.folder_aggregator_cache_stale && FolderAggregatorRefreshIntervalElapsed(state)) {
    // Results are about to be replaced or cleared; no need to scrub written fields.
    ResetFolderSizeAggregatorCache(state, *folder_aggregator,
                                   state.search_pipeline.pending_folder_aggregator_index_size,
                                   /*invalidate_written_result_fields=*/false);
    return;
  }
  folder_aggregator->CancelPending();
}

/**
 * @brief Track index membership changes for FolderSizeAggregator invalidation.
 *
 * Marks the cache stale when the index grows/shrinks. Resets at most once per
 * kFolderAggregatorMinRefreshInterval — no requirement that the index stop changing.
 */
void SyncFolderSizeAggregatorWithIndex(GuiState& state,
                                       FolderSizeAggregator* folder_aggregator,
                                       size_t current_index_size) {
  if (folder_aggregator == nullptr) {
    return;
  }

  if (!state.search_pipeline.folder_aggregator_index_size_initialized) {
    state.search_pipeline.last_folder_aggregator_index_size = current_index_size;
    state.search_pipeline.pending_folder_aggregator_index_size = current_index_size;
    state.search_pipeline.folder_aggregator_index_size_initialized = true;
    // Treat baseline as a fresh cache epoch so the first stale refresh waits a full interval.
    state.search_pipeline.last_folder_aggregator_reset_time = std::chrono::steady_clock::now();
    return;
  }

  if (current_index_size != state.search_pipeline.pending_folder_aggregator_index_size) {
    state.search_pipeline.pending_folder_aggregator_index_size = current_index_size;
  }

  if (current_index_size != state.search_pipeline.last_folder_aggregator_index_size) {
    state.search_pipeline.folder_aggregator_cache_stale = true;
  }

  if (!state.search_pipeline.folder_aggregator_cache_stale || !FolderAggregatorRefreshIntervalElapsed(state)) {
    return;
  }

  ResetFolderSizeAggregatorCache(state, *folder_aggregator, current_index_size,
                                 /*invalidate_written_result_fields=*/true);
}

void PrepareResultsStateForUpdate(GuiState& state, FolderSizeAggregator* folder_aggregator) {
  {
    // Instrumented: PrepareResultsStateForUpdate = sort-task drain + aggregator cancel/reset
    // (blocking UI-thread work on the results-commit path).
    CTRACK_NAME("SearchController::PrepareResultsForUpdate");  // NOLINT(misc-const-correctness)
    WaitForAllAttributeLoadingFutures(state);
    CancelOrResetFolderSizeAggregator(state, folder_aggregator);
  }
  state.search_pipeline.search_active = false;
  state.search_pipeline.results_complete = true;
  state.search_pipeline.results_updated = true;
  // Phase 3: results content changed — any captured filter cache versions stale.
  state.BumpResultsVersion();
}

/**
 * @brief Commit new search results and path pool into GuiState.
 *
 * Replaces current results with new results, handling cleanup of attribute loading futures,
 * folder size aggregator cancellation/reset, selection remapping, filter cache invalidation,
 * batch number bumping, and logging. Always replaces the path pool (empty pool clears prior
 * query path storage when the result set is empty).
 */
void CommitNewSearchResults(
    GuiState& state,
    std::vector<SearchResult>&& new_results,
    std::vector<char>&& new_pool,
    FolderSizeAggregator* folder_aggregator,
    [[maybe_unused]] std::string_view log_label) {  // unused in Release (LOG_INFO compiled out)
  // Remap selection BEFORE replacing searchResults: string_views in the old display
  // reference the current path pool, which is invalidated after the move below.
  // Entity-based remap: items still present in new_results stay selected at their new
  // positions; items that disappeared are deselected. This handles both manual new
  // searches (old paths absent → selection clears) and auto-refresh (surviving files
  // tracked to their new row indices). PollResults sets resultsPresortedOnCommit so
  // HandleTableSorting can skip a redundant sync-column re-sort/re-remap; Size/Last
  // Modified still re-enter the async sort path when resultsUpdated is set.
  PrepareResultsStateForUpdate(state, folder_aggregator);
  if (!state.selection.GetSelectedRows().empty()) {
    // Snapshot old order as path views before the pool is replaced below.
    state.selection.RemapSelectionAfterDisplayResultsChange(
        SnapshotDisplayPaths(*search::SearchResultsService::GetDisplayResults(state)), new_results);
  }

  // Reclaim excess results capacity when replacing the result set to keep memory
  // tightly bounded (see below for why the path pool itself is never shrunk).
  const size_t prev_results_cap = state.result_pool_->Results().capacity();

  state.result_pool_->Results() = std::move(new_results);
  // Always replace the path pool (including empty). An empty back-buffer pool means
  // zero results; skipping the move would retain the previous query's path bytes.
  state.result_pool_->Pool() = std::move(new_pool);

  // Shrink to fit if new size is significantly smaller than previous capacity to release excess heap back to OS/CRT.
  // Safe for Results(): SearchResult.fullPath views point into Pool(), not into
  // the results vector, so moving SearchResult objects is harmless.
  if (state.result_pool_->Results().size() < prev_results_cap / 2) {
    state.result_pool_->Results().shrink_to_fit();
  }
  // Never shrink Pool() here: every SearchResult.fullPath committed above is a
  // string_view into this buffer, so any reallocation is a use-after-free in
  // ResultsTable::Render (masked in practice only by allocator address reuse).
  // The back-buffer pool is already sized by reserve(pool_bytes_needed) and is
  // wholesale-replaced on the next commit, so the slack (bounded by
  // quarantined-hit bytes) is not worth reclaiming.
  // CRITICAL: Invalidate filter caches whenever searchResults is replaced.
  // GetDisplayResults checks valid flags and falls back to searchResults when invalid.
  // Clear the vectors so we do not hold stale filter data.
  InvalidateFilterCaches(state);
  // Increment so IncrementalSearchState::CheckBatchNumber detects the change and clears
  // its filtered_results_ cache, which holds SearchResult copies whose fullPath views
  // point into the (now replaced) path pool. Without this, those views dangle if the
  // pool reallocates, causing a UAF when the render loop reads result.fullPath.
  state.result_pool_->BumpBatchNumber();
  // Defer filter-cache rebuild for one frame only when an active filter would
  // need lazy attribute I/O (unloaded sizes/times). Skips the unfiltered flash
  // when filters are off or attributes are already cached.
  state.search_pipeline.defer_filter_cache_rebuild = ShouldDeferFilterCacheRebuild(state);
  AssertSearchResultsFullPathsInPathPool(state);
  LOG_INFO("UI received " + std::string(log_label) + ": " +
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
  PrepareResultsStateForUpdate(state, folder_aggregator);
  state.search_pipeline.search_session_id++;
  ClearResultPool(state);
  // Clear marks for new searches (ClearSearchResults is called when a new search starts)
  state.selection.markedFileIds.clear();
  // Reset sort state so a later CheckAndCompleteAsyncSort does not run a sort on new
  // results without loading Size/Time attributes (would sort by kFileSizeNotLoaded).
  state.async_sort_.sort_ready_state_ = SortReadyState::Idle;
  LOG_INFO("UI cleared search results: " + std::string(reason));
}

void NoteCompletedManualSearchForHistory(GuiState& state) {
  if (state.search_pipeline.search_was_manual && !state.search_pipeline.search_active) {
    state.search_pipeline.pending_manual_history_record = true;
  }
}

} // namespace

SearchController::SearchController() = default;

void SearchController::Update(GuiState &state, SearchWorker &search_worker,  // NOLINT(readability-identifier-naming) - Parameter name follows project convention
                              FolderSizeAggregator* folder_aggregator,
                              IndexFrameState frame,
                              const AppSettings& settings, const FileIndex& file_index) const {
  CTRACK_DEV_NAME("SearchController::Update");  // NOLINT(misc-const-correctness)
  // When user explicitly clears (Clear All or Escape), discard worker's cached results
  // so PollResults does not re-apply stale results on the next frame.
  // Only fully reset when worker is idle. If busy, cancel to finish faster; keep the
  // flag so drain + discard run on the first idle frame.
  if (state.search_pipeline.clear_results_requested) {
    if (!search_worker.IsBusy()) {
      search_worker.DiscardResults();
      ClearSearchResults(state, folder_aggregator, "user clear requested");
      state.search_pipeline.clear_results_requested = false;
    } else {
      search_worker.CancelSearch();
    }
  }

  // Don't allow searches while index is being built or finalizing
  // This prevents race condition where search uses offsets that become invalid
  // when FinalizeInitialPopulation() clears and rebuilds path_storage_
  // Caller must pass Application::IsIndexBuilding() (includes monitor population).
  if (frame.is_index_building) {
    PollResults(state, search_worker, folder_aggregator, file_index,
                settings.showPathHierarchyIndentation);
    NoteCompletedManualSearchForHistory(state);
    return;
  }

  // Index membership changed (USN/crawler inserts/deletes): drop stale folder aggregates
  // before polling/applying results so Descendant Files* / folder Size recompute.
  SyncFolderSizeAggregatorWithIndex(state, folder_aggregator, frame.index_size);

  // Consume completed results BEFORE starting a new search this frame.
  // If HandleAutoRefresh / debounce runs first, StartSearch makes IsBusy() true and
  // PollResults skips HasNewResults — dropping a finished generation until the next
  // search completes (or forever if that search wedges).
  PollResults(state, search_worker, folder_aggregator, file_index,
              settings.showPathHierarchyIndentation);
  NoteCompletedManualSearchForHistory(state);

  // Handle debounced instant search (search-as-you-type)
  if (ShouldTriggerDebouncedSearch(state, search_worker)) {
    const size_t previous_results_size = state.result_pool_->Results().size();
    // Keep previous completed results visible until PollResults replaces them
    // (same §9 semantics as HandleAutoRefresh; see double-buffering spec §10).
    search_controller_detail::BeginInFlightSearchKeepingVisibleResults(state);
    // Phase 2: snapshot criteria at trigger time; the pure BuildParams() runs on
    // the snapshot, so same-frame criteria edits cannot drift into this search.
    const SearchCriteria criteria_snapshot = state.searchCriteria;
    search_worker.StartSearch(criteria_snapshot.BuildParams(), &settings);
    assert(state.result_pool_->Results().size() == previous_results_size);
  }

  // Handle auto-refresh when index changes
  HandleAutoRefresh(state, search_worker, frame.index_mutation, settings);
}

void SearchController::TriggerManualSearch(GuiState &state,
                                           SearchWorker &search_worker,
                                           FolderSizeAggregator* folder_aggregator,
                                           const AppSettings& settings) const {  // NOLINT(readability-identifier-naming) - Parameter name follows project convention
  CTRACK_NAME("SearchController::TriggerManualSearch");  // NOLINT(misc-const-correctness)
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
  state.input_debounce.input_changed = false; // Reset debounce state when manually searching
  state.search_pipeline.search_active = true;
  state.search_pipeline.results_complete = false;
  state.search_pipeline.search_was_manual = true; // Manual search: record in history on completion
  // Phase 2: criteria snapshot at trigger time (see SearchCriteria.h contract).
  const SearchCriteria criteria_snapshot = state.searchCriteria;
  search_worker.StartSearch(criteria_snapshot.BuildParams(), &settings);
}

void SearchController::HandleAutoRefresh(GuiState &state,
                                         SearchWorker &search_worker,  // NOLINT(readability-identifier-naming) - Parameter name follows project convention
                                         uint64_t current_index_mutation,
                                         const AppSettings& settings) const {
  // Auto-refresh logic (triggered by index changes, not user input).
  // NOTE:
  // - We no longer require state.search_pipeline.search_active to be true here.
  //   Previously, searchActive was cleared as soon as a search completed,
  //   which prevented auto-refresh from ever triggering after the first search.
  // - Instead, we require that we have existing search results, or query text
  //   worth watching. A typed query with empty results still arms refresh so
  //   rare/transient files (e.g. hex temp names) appear without a manual
  //   re-search; a fully empty query box stays unarmed to avoid background
  //   burn on no-match-everything states.
  if (!state.searchCriteria.auto_refresh) {
    return;
  }

  // Require an existing search result set, or typed query text, so we know
  // what to auto-refresh.
  if (state.result_pool_->Results().empty() && !state.searchCriteria.HasSearchText()) {
    return;
  }

  // Only trigger when the index mutation version has moved since the last
  // auto-refresh baseline. Version-based (not size-based) so heals and
  // renames — which keep Size() — refresh visible results too. This baseline
  // is updated each time we trigger.
  // Out of scope: attribute-only changes (size/mtime) do not bump the
  // version, so size/time-filtered or sorted views may stay stale across an
  // attribute threshold crossing until manual re-search. Live attributes
  // would need a separate attr version + re-filter/re-sort path.
  if (current_index_mutation == state.search_pipeline.last_index_mutation) {
    return;
  }

  // Only trigger auto-refresh if the worker is not busy
  if (search_worker.IsBusy()) {
    return;
  }

  // Enforce a cooldown between auto-refresh triggers. Under heavy USN activity (bulk file
  // operations), the index version changes on nearly every frame. Without this guard the app enters
  // a continuous search-retrigger loop that keeps search_worker busy indefinitely, prevents the
  // idle throttle from engaging, and saturates the main thread with result processing work.
  constexpr auto kAutoRefreshCooldown = std::chrono::milliseconds(500);
  const auto now = std::chrono::steady_clock::now();
  if (now - state.search_pipeline.last_auto_refresh_time < kAutoRefreshCooldown) {
    return;
  }

  const size_t previous_results_size = state.result_pool_->Results().size();

  state.search_pipeline.last_index_mutation = current_index_mutation;
  state.search_pipeline.last_auto_refresh_time = now;
  // Keep previous completed results visible until PollResults replaces them
  // (see 2026-02-24_RESULTS_AUTO_REFRESH_DOUBLE_BUFFERING_SPEC.md §9).
  search_controller_detail::BeginInFlightSearchKeepingVisibleResults(state);
  // Phase 2: criteria snapshot at trigger time (same contract as debounced path).
  const SearchCriteria criteria_snapshot = state.searchCriteria;
  search_worker.StartSearch(criteria_snapshot.BuildParams(), &settings);
  assert(state.result_pool_->Results().size() == previous_results_size);
}

void SearchController::PollResults(GuiState &state,
                                    SearchWorker &search_worker,  // NOLINT(readability-identifier-naming) - Parameter name follows project convention
                                    FolderSizeAggregator* folder_aggregator,
                                    const FileIndex &file_index,
                                    bool show_path_hierarchy_indentation) const {
  CTRACK_DEV_NAME("SearchController::PollResults");  // NOLINT(misc-const-correctness)
  // Single guard for both no-work cases: no fresh results to consume, or the search
  // was already superseded (auto-refresh / debounced search started this frame) —
  // skip consume; the next run will replace results_data_ when it completes.
  if (!search_worker.HasNewResults() || search_worker.IsBusy()) {
    return;
  }

  std::vector<SearchResultBatch> data;
  {
    // Instrumented: move results data out of the worker (worker mutex held briefly).
    CTRACK_NAME("SearchController::GetResultsData");  // NOLINT(misc-const-correctness)
    data = search_worker.GetResultsData();
  }

  // Construct back-buffer result pool off-screen without clearing visible front results
  ResultPoolOwner back_buffer;
  std::vector<SearchResult> back_results;
  {
    // Instrumented: pool build + batch cached-attribute snapshot lookup (index shared
    // lock; contention suspect vs crawler exclusive InsertPaths locks).
    CTRACK_NAME("SearchController::MergeAndConvertResults");  // NOLINT(misc-const-correctness)
    back_results = MergeAndConvertToSearchResults(back_buffer.Pool(), data, file_index);
  }

  // Preserve already-computed directory stats from front-buffer results before pre-sorting
  {
    // Instrumented: front/back directory-attribute reconciliation.
    CTRACK_NAME("SearchController::ReconcileComputedAttrs");  // NOLINT(misc-const-correctness)
    search_controller_detail::ReconcileComputedDirectoryAttributes(
        back_results, state.result_pool_->Results());
  }

  // Pre-sort back-buffer results according to active UI sort settings.
  // If an async sort operation is currently in-flight (for Size or Last Modified),
  // back_results still contains sentinel values for un-fetched attributes. Pre-sorting
  // by Size or Last Modified during an in-flight async sort would produce invalid ordering.
  int sort_col = state.async_sort_.last_sort_column;
  ImGuiSortDirection sort_dir = state.async_sort_.last_sort_direction;
  const bool sort_in_flight = state.async_sort_.HasPendingSort();
  // Identical fallback for both conditions: async Size/LastModified sorts must not
  // pre-sort on sentinel values, and an unset column (-1) defaults to Filename.
  if ((sort_in_flight && (sort_col == ResultColumn::Size || sort_col == ResultColumn::LastModified)) ||
      sort_col == -1) {
    sort_col = ResultColumn::Filename;
    sort_dir = ImGuiSortDirection_Ascending;
  }
  // #3 fix: O(N) unordered set-equality pre-check before the O(N log N) pre-sort.
  // Auto-refresh polls with an unchanged result set paid a full string-compare sort
  // only for AreSearchResultsEqual to hit skip-swap afterwards. When the sets match,
  // the front buffer already shows this set in this order (pre-sorted on commit,
  // maintained by HandleTableSorting), so both the sort and the commit are skipped.
  // Bypassed while an async Size/Date sort is in flight to preserve today's
  // cancel-and-restart commit semantics in that short-lived window.
  if (!sort_in_flight) {
    bool sets_equal = false;
    {
      // Instrumented (prod-level, like the other commit-path events): O(N) hash scan
      // vs O(N log N) string-compare sort. In ctrack output a small
      // CompareFrontBufferUnordered total with a reduced PreSortBackBuffer call count
      // confirms the skip fires; PollResults max should drop toward the merge cost.
      CTRACK_NAME("SearchController::CompareFrontBufferUnordered");  // NOLINT(misc-const-correctness)
      sets_equal = search_controller_detail::AreSearchResultSetsEqualUnordered(
          back_results, state.result_pool_->Results());
    }
    if (sets_equal) {
      state.search_pipeline.search_active = false;
      state.search_pipeline.results_complete = true;
      LOG_INFO("UI search results unchanged after refresh (" +
               std::to_string(back_results.size()) + " items), skipping double-buffer swap");
      return;
    }
  }
  {
    // Instrumented: pre-sort of the back buffer (O(N log N) compare-based sort).
    CTRACK_NAME("SearchController::PreSortBackBuffer");  // NOLINT(misc-const-correctness)
    std::sort(back_results.begin(), back_results.end(),
              CreateSearchResultComparator(sort_col, sort_dir,
                                           show_path_hierarchy_indentation));
  }

  // Diff / Equality check: if pre-sorted back-buffer matches current front-buffer,
  // skip swap to avoid unnecessary UI redraw or selection/scroll reset.
  {
    // Instrumented: O(N) front/back equality scan (skip-swap fast path).
    CTRACK_NAME("SearchController::CompareFrontBuffer");  // NOLINT(misc-const-correctness)
    if (search_controller_detail::AreSearchResultsEqual(back_results, state.result_pool_->Results())) {
      state.search_pipeline.search_active = false;
      state.search_pipeline.results_complete = true;
      LOG_INFO("UI search results unchanged after refresh (" +
               std::to_string(back_results.size()) + " items), skipping double-buffer swap");
      return;
    }
  }

  // Results changed: commit new double-buffered back-buffer results and pool
  {
    // Instrumented: sort-task drain + aggregator reset + selection remap + pool move.
    CTRACK_NAME("SearchController::CommitNewResults");  // NOLINT(misc-const-correctness)
    CommitNewSearchResults(state, std::move(back_results), std::move(back_buffer.Pool()),
                           folder_aggregator, "double-buffered search results");
  }
  // Back buffer was sorted above with the active UI column/direction/hierarchy unless
  // an async sort was in-flight (in which case it fell back to Filename).
  // Let HandleTableSorting skip clone+re-sort for sync columns only when not in-flight.
  state.search_pipeline.results_presorted_on_commit = !sort_in_flight;
}

bool SearchController::ShouldTriggerDebouncedSearch(
    const GuiState &state, const SearchWorker &search_worker) const {  // NOLINT(readability-identifier-naming) - Parameter name follows project convention
  // Instant search: triggers when user types (with debounce)
  // Separate from auto-refresh which triggers on index changes
  if (!state.searchCriteria.instant_search || !state.input_debounce.input_changed) {
    return false;
  }

  const auto now = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - state.input_debounce.last_input_time)
                           .count();

  return (elapsed >= SearchController::kDebounceDelayMs && !search_worker.IsBusy());
}
