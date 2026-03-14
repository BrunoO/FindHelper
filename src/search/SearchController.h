#pragma once

#include "gui/GuiState.h"
#include "index/FileIndex.h"
#include "search/SearchWorker.h"
#include <atomic>
#include <chrono>
#include <functional>

// Forward declarations
class UsnMonitor;
struct AppSettings;

// Search Controller class
// Centralizes all high-level search orchestration on the UI side:
// - Debounces "instant search" as the user types
// - Triggers manual searches (button / Enter key)
// - Triggers auto-refresh when the index changes
// - Polls the background SearchWorker and updates GuiState with results
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
  // - Decides when to start a debounced "instant" search based on user input.
  // - Kicks off auto-refresh searches when the index size changes.
  // - Always calls PollResults() to pull any completed results from the worker.
  // monitor: Optional pointer used to query current index size and population state.
  // is_index_building: True if index is building or finalizing (prevents search race condition).
  // file_index: Used to convert SearchResultData to SearchResult with path pool.
  void Update(GuiState &state, SearchWorker &search_worker, const UsnMonitor* monitor, bool is_index_building, const AppSettings& settings, const FileIndex& file_index) const;

  // Trigger a manual search immediately (bypasses debounce).
  // Called directly from UI event handlers when the user:
  // - Clicks the "Search" button
  // - Presses Enter in a search input field
  // Uses current values from GuiState to build SearchParams and start the worker.
  void TriggerManualSearch(GuiState &state, SearchWorker &search_worker, const AppSettings& settings) const;

  // Handle auto-refresh when the underlying index changes.
  // Called from Update() once per frame, after we know the current index size.
  // If auto-refresh is enabled and the index size differs from the last search,
  // starts a new background search (unless the worker is currently busy).
  void HandleAutoRefresh(GuiState &state, SearchWorker &search_worker,
                         size_t current_index_size, const AppSettings& settings) const;

  // Poll the SearchWorker for newly completed results and update GuiState.
  // Called from Update() once per frame. Uses file_index for path-pool conversion.
  void PollResults(GuiState &state, SearchWorker &search_worker, const FileIndex& file_index) const;

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
  static constexpr int kDebounceDelayMs = 400; // 400ms debounce delay
};
