#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

// Forward declarations: only references are held and the merge scratch is
// heap-owned (see merge_scratch_). Definitions live in the .cpp, which
// includes gui/GuiState.h and search/SearchResultUtils.h.
class FileIndex;
class FolderSizeAggregator;
class GuiState;
class SearchWorker;
struct AppSettings;
struct SearchResultMergeScratch;

// Search Controller class
// Centralizes all high-level search orchestration on the UI side:
// - Debounces "instant search" as the user types
// - Triggers manual searches (button / Enter key)
// - Triggers auto-refresh when the index changes
// - Polls the background SearchWorker and updates GuiState with results

// Read-only index observations for one Update frame, produced by Application
// from the live index. Groups the four frame inputs (Sonar cpp:S107: Update
// would otherwise take 8 parameters); collaborators (state, worker,
// aggregator, settings, index) stay separate parameters.
struct IndexFrameState {
  size_t index_size = 0;
  uint64_t index_mutation = 0;
  bool is_index_building = false;
  // USN journal backlog depth (0 off-Windows: stub monitor reports 0).
  // Consumed by auto-refresh suspension below.
  size_t usn_queue_depth = 0;
};

class SearchController {
public:
  SearchController();
  // Out-of-line: merge_scratch_ is an incomplete type here (unique_ptr needs
  // the complete type at destruction).
  ~SearchController();

  // Non-copyable, non-movable (manages search state)
  SearchController(const SearchController&) = delete;
  SearchController& operator=(const SearchController&) = delete;
  SearchController(SearchController&&) = delete;
  SearchController& operator=(SearchController&&) = delete;

  // Main entry point for the controller.
  // Called once per GUI frame (from the main loop).
  // - Clears / cancels on clearResultsRequested (drain attrs when idle).
  // - PollResults() before debounce/auto-refresh so a finished generation is not
  //   skipped when StartSearch makes IsBusy() true in the same frame.
  // - Decides when to start a debounced "instant" search based on user input.
  // - Kicks off auto-refresh searches when the index mutation version changes.
  // monitor: Optional pointer used to query current index size and population state.
  // frame: index observations for this frame (size, mutation version, building
  // flag). Same predicate as Application::IsIndexBuilding() (builder active,
  // finalizing_population, or USN monitor populating). Do not pass a narrower
  // flag (e.g. IndexBuildState::active only).
  // file_index: Used to convert SearchResultBatch hits to SearchResult with path pool.
  void Update(GuiState &state, SearchWorker &search_worker, FolderSizeAggregator* folder_aggregator, IndexFrameState frame, const AppSettings& settings, const FileIndex& file_index) const;

  // Trigger a manual search immediately (bypasses debounce).
  // Called directly from UI event handlers when the user:
  // - Clicks the "Search" button
  // - Presses Enter in a search input field
  // Uses current values from GuiState to build SearchParams and start the worker.
  void TriggerManualSearch(GuiState &state, SearchWorker &search_worker, FolderSizeAggregator* folder_aggregator, const AppSettings& settings) const;

  // Handle auto-refresh when the underlying index changes.
  // Called from Update() once per frame, after we know the current index
  // mutation version. If auto-refresh is enabled and the version differs
  // from the last refresh, starts a new background search (unless the worker
  // is currently busy). Version-based (not size-based) so heals and renames
  // refresh visible results too.
  // Suspended while the USN journal backlog is deep (usn_queue_depth at or
  // above kAutoRefreshSuspendQueueDepth): each refresh holds the index
  // shared_lock for the whole search, stalling the USN processor's per-buffer
  // unique_lock behind it. Baselines are left untouched while suspended so
  // the pending mutation triggers exactly one refresh when the backlog
  // drains.
  void HandleAutoRefresh(GuiState &state, SearchWorker &search_worker,
                         uint64_t current_index_mutation, size_t usn_queue_depth,
                         const AppSettings& settings) const;

  // Poll the SearchWorker for newly completed results and update GuiState.
  // Called from Update() once per frame. Uses file_index for path-pool conversion.
  // show_path_hierarchy_indentation must match ResultsTable / HandleTableSorting so
  // back-buffer pre-sort order matches the visible front buffer (skip-swap correctness).
  void PollResults(GuiState &state, SearchWorker &search_worker,
                   FolderSizeAggregator* folder_aggregator, const FileIndex& file_index,
                   bool show_path_hierarchy_indentation) const;

private:
  // Decide whether a debounced "instant search" should be triggered this frame.
  // Called from Update().
  // Returns true when:
  // - Instant search is enabled,
  // - User input has changed,
  // - The debounce delay has elapsed,
  // - And the SearchWorker is not currently busy.
  [[nodiscard]] bool ShouldTriggerDebouncedSearch(const GuiState &state,
                                     const SearchWorker &search_worker) const;

  // Constants
  static constexpr int kDebounceDelayMs = 200;     // 200ms debounce for filename/ext fields
  static constexpr int kPathDebounceDelayMs = 350; // longer debounce for path field (heavier scan)
  // Maximum quiet time before a debounced search fires even when the path looks
  // incomplete: trailing '\' / '/' / ':', or a plain path (no pp:/rs:/fz: prefix,
  // no glob) shorter than kMinPathLengthToSearch. Prefixed/glob paths are exempt
  // from the length guard and keep the normal kPathDebounceDelayMs debounce.
  // The bound is the safety mechanism against a one-way skip: input_changed is
  // only cleared when a search actually starts, so an unbounded skip would block
  // instant search forever for a stable value like "d:" or "c:\dev\".
  static constexpr int kIncompletePathMaxDelayMs = 1000;
  // Plain paths shorter than this (e.g. "d", "/a") match nearly the whole index.
  // Note: "c:" and "c:\" are NOT reached by this guard — they are already caught
  // by the trailing-separator guard in ShouldTriggerDebouncedSearch; this only
  // covers 1-3 char paths that do not end in '\' / '/' / ':'.
  static constexpr size_t kMinPathLengthToSearch = 4;
  // USN backlog depth at or above which auto-refresh is suspended (drain first,
  // refresh once after). Mirrors usn_monitor_constants::kQueueDepthBand1000
  // (Windows-only header): the codebase's deep-backlog band; keep in sync.
  static constexpr size_t kAutoRefreshSuspendQueueDepth = 1000;

  // Reusable scratch buffers for MergeAndConvertToSearchResults (hoisted across searches to eliminate heap allocations).
  // Heap-owned so this header does not drag in SearchResultUtils.h (and its
  // FileIndex.h chain); allocated in the constructor. Mutable because
  // PollResults is const (called from const Update); UI-thread confined, never touched off the main thread.
  mutable std::unique_ptr<SearchResultMergeScratch> merge_scratch_;  // NOSONAR(cpp:S8379) - UI-thread confined: only PollResults touches this, once per frame on the main thread; no second thread exists to guard against, so a mutex would add overhead for zero protection
};
