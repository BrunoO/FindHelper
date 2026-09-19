#pragma once

#include "gui/GuiState.h"

#include <atomic>
#include <chrono>
#include <functional>

// Forward declarations
class FileIndex;
class FolderSizeAggregator;
class SearchWorker;
struct AppSettings;

// Search Controller class
// Centralizes all high-level search orchestration on the UI side:
// - Debounces "instant search" as the user types
// - Triggers manual searches (button / Enter key)
// - Triggers auto-refresh when the index changes
// - Polls the background SearchWorker and updates GuiState with results

// Read-only index observations for one Update frame, produced by Application
// from the live index. Groups the three frame inputs (Sonar cpp:S107: Update
// would otherwise take 8 parameters); collaborators (state, worker,
// aggregator, settings, index) stay separate parameters.
struct IndexFrameState {
  size_t index_size = 0;
  uint64_t index_mutation = 0;
  bool is_index_building = false;
};

class SearchController {
public:
  SearchController();
  ~SearchController() = default;

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
  void HandleAutoRefresh(GuiState &state, SearchWorker &search_worker,
                         uint64_t current_index_mutation, const AppSettings& settings) const;

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
  static constexpr int kDebounceDelayMs = 200; // 200ms debounce; search ~35ms so 400ms was idle wait
};
