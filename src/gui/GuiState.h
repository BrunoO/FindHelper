#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

#include "api/GeminiApiUtils.h"
#include "gui/AsyncSortState.h"
#include "gui/ResultFilterCaches.h"
#include "gui/ResultPoolOwner.h"
#include "gui/SearchCriteria.h"
#include "gui/SearchPipelineState.h"
#include "gui/SelectionState.h"
#include "gui/UIThreadOwned.h"
#include "gui/WorkflowStates.h"
#include "search/SearchTypes.h"

// Forward declaration to avoid circular include with SearchTypes.h
struct SearchResult;

// GUI State class to encapsulate all UI state.  // NOSONAR(cpp:S125) - S125 reports this comment block starting here, but it plus the ownership-map table below are authoritative docs, not commented-out code (see 5813e033)
// NOSONAR - Intentional, kept: FIELD OWNERSHIP MAP below documents each mutable
// field cluster and its single writer (markdown table rows below look like
// code to the S125 detector, but are authoritative documentation).
//
// FIELD OWNERSHIP MAP (Phase 0 of the god-object decomposition plan; see
// internal-docs/plans/2026-09-11_GUISTATE_GOD_OBJECT_DECOMPOSITION_PLAN.md).
// Before adding a write site for any field below, declare it here. Every
// future substate extraction must keep each struct single-writer.
//
// | Domain (planned substate)     | Fields                                  | Writers (sole unless noted)              |
// |-------------------------------|-----------------------------------------|------------------------------------------|
// | SearchCriteria (Phase 2)      | searchCriteria (SearchCriteria.h,       | user input events; presets via           |
// |                               | snake_case fields): extension_input,    | ApplySearchConfig()/ApplyShowAllPreset() |
// |                               | filename_input, path_input, folders_    |                                          |
// |                               | only, case_sensitive, time_filter,      |                                          |
// |                               | size_filter, instant_search, auto_      |                                          |
// |                               | refresh                                 |                                          |
// | Result caches (Phase 3)       | result_pool_, filter_caches (time/size   | SearchController (+ ResultsTable via     |
// |                               | slices + total_size progress; validity   | SearchResultUtils update helpers only)   |
// |                               | = valid && captured_version ==           | (BumpResultsVersion on results change)   |
// |                               | GetResultsVersion())                     |                                          |
// | Search pipeline (Phase 4)     | search_pipeline (SearchPipelineState     | SearchController (triggers, PollResults, |
// |                               | struct, snake_case): search_active,      | ClearInputs, check helpers in            |
// |                               | results_complete, search_session_id,     | SearchControllerDetail.h). Documented    |
// |                               | search_error, search_was_manual,         | co-writer: Application::UpdateSearchState|
// |                               | pending_manual_history_record,           | consumes pending_manual_history_record   |
// |                               | results_updated, results_presorted_      | (ConsumeManualHistoryRecord) and clears  |
// |                               | on_commit, clear_results_requested,      | search_was_manual after the history      |
// |                               | defer_filter_cache_rebuild,              | check                                    |
// |                               | last_index_mutation,                     |                                          |
// |                               | last_auto_refresh_time,                 |                                          |
// |                               | folder-aggregator staleness cluster:     |                                          |
// |                               | last_folder_aggregator_index_size,       |                                          |
// |                               | folder_aggregator_cache_stale,           |                                          |
// |                               | pending_folder_aggregator_index_size,    |                                          |
// |                               | folder_aggregator_index_size_initialized,|                                          |
// |                               | last_folder_aggregator_reset_time,      |                                          |
// |                               | flush fast-path:                        | FlushAggregatorFolderStats (UI thread,   |
// |                               | last_folder_stats_flush_version,        | via ResultsTable render)                 |
// |                               | folder_stats_flush_had_pending          |                                          |
// | Cloud-file loading (Ph. 6)    | cloud_files (CloudFileWorkflowState):    | SearchController/SearchResultUtils       |
// |                               | deferred_ids, loading_futures (enqueue   | (TryEnqueue/queue futures/CleanUpCloud-  |
// |                               | + teardown only via the struct methods)  | Futures); teardown: ClearInputs +        |
// |                               |                                          | commits (DrainInBackground, non-         |
// |                               |                                          | blocking); shutdown (WaitAndDrain)      |
// | Async sort                    | async_sort_, lastSortColumn,            | ResultsTable (sort trigger + sort spec), |
// |                               | lastSortDirection, completed_sort_,     | SearchController (BeginNewSort/drain)    |
// |                               | computingFolderSizes                    | FolderSizeAggregator client side         |
// | Selection / deletion          | selection                               | ResultsTable (keyboard + menus)          |
// | Export workflow (Phase 1)     | export_workflow: notification,          | export service + ExportCsvPopup          |
// |                               | error_message, notification_time,       |                                          |
// |                               | show_popup, success, file_path,         |                                          |
// |                               | result_count                            |                                          |
// | Gemini workflow (Phase 1)     | gemini: description_input,              | AI-assisted search panel                 |
// |                               | api_call_in_progress, api_future,       |                                          |
// |                               | error_message, error_display_time,      |                                          |
// |                               | message_severity, copied_notice_time,   |                                          |
// |                               | api_provider_display_name              |                                          |
// | Index build progress (Ph. 1)  | index_build: in_progress + 9 fields     | Application::UpdateIndexBuildState       |
// | History interaction (Ph. 1)   | history: selected_id,                   | SearchHistoryWindow + popups             |
// |                               | pending_rename_id, pending_delete_id    |                                          |
// | UI visibility (Phase 8)        | ui_visibility (UiVisibilityState, snake_case):   | respective widgets (Render*)             |
// |                               | manual_search_expanded, ai_search_       |                                          |
// |                               | expanded, show_quick_filters, show_help_ |                                          |
// |                               | window, show_search_help_window,         |                                          |
// |                               | open_regex_generator_popup*,             |                                          |
// |                               | context_menu_open, last_context_menu_    |                                          |
// |                               | time, incremental_search_active          |                                          |
// | Input debounce (Ph. 8)        | input_debounce (InputDebounceState,      | input events (SearchInputs widget);      |
// |                               | snake_case): instant_search,             | presets via ApplySearchConfig etc.       |
// |                               | auto_refresh, last_input_time,           |                                          |
// |                               | input_changed, focus_filename_input      |                                          |

//
// Naming: Public members use camelCase (not snake_case_) by design. This matches
// common UI/widget naming (e.g. ImGui, JavaScript UI state) and keeps access
// at call sites readable (e.g. state.selected_row_, state.searchCriteria.time_filter). A few
// internal members use snake_case_ with trailing underscore per project
// convention (e.g. async_sort_, gemini.description_input).
// See docs/standards/CXX17_NAMING_CONVENTIONS.md; this is the documented
// exception for UI state aggregates.
//
class GuiState {
 public:
  // Search criteria inputs — see SearchCriteria.h (god-object Phase 2).
  // Single writer: user-input widgets and presets
  // (ApplySearchConfig / ApplyShowAllPreset); SearchController snapshots this
  // struct at search-trigger time and builds SearchParams from the snapshot.
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  SearchCriteria searchCriteria;

  // Search state
  // result_pool_: bundles the path-pool vector, the results vector that
  // holds string_views into it, and the batch-change counter.
  // Use result_pool_->Clear() (not direct field access) to clear both — it
  // enforces results-before-pool ordering to prevent dangling string_views.
  // UIThreadOwned enforces that mutations only occur on the UI thread (assert in
  // debug builds). Const access (render functions, read-only helpers) uses the
  // const overload and is never asserted. See UIThreadOwned<T> in UIThreadOwned.h.
  //
  // Batch-number versioning protocol (result_pool_->BatchNumber()):
  //   Incrementors: SearchController::ClearResultPool (on search start / discard) and
  //     SearchController::PollResults (once when double-buffered search completes and swaps).
  //     BumpBatchNumber() is the only mutating call.
  //   Consumers:
  //     • IncrementalSearchState::CheckBatchNumber — detects a new result set and drops
  //       its filtered_results_ cache (which holds SearchResult copies with string_views
  //       into the old pool; stale views dangle after pool reallocation).
  //     • ResultsTable folder-stats rebuild predicate — compares batch number against
  //       the value captured at last rebuild; mismatches force a cache refresh.
  //   Contract: consumers test (current != captured), not a specific absolute value, so
  //   double-increments (e.g., clear then apply in the same frame) are harmless.
  //   Never add a new consumer that relies on a specific count value — use != only.
  // NOLINTNEXTLINE(readability-identifier-naming)
  UIThreadOwned<ResultPoolOwner> result_pool_;
  // Cached results after applying time filter (to avoid re-filtering every frame)
  // Phase 3: see ResultFilterCaches; a slice is valid iff
  // valid && captured_version == GetResultsVersion() && cached_filter == current filter.
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  ResultFilterCaches filter_caches;

  // Monotonic version of the current result set. Bumped by BumpResultsVersion()
  // whenever result content changes (commit, cloud attr completion...). Filter
  // cache slices capture it when (re)built; mismatch ⇒ stale.
  ResultsVersion results_version_ = 1;  // NOLINT(readability-identifier-naming) - snake_case_ private member
  /** Monotonic version for filter caches: bump whenever results change. */
  void BumpResultsVersion() { ++results_version_; }
  /** Current results version (captured by caches when rebuilt). */
  [[nodiscard]] ResultsVersion GetResultsVersion() const { return results_version_; }

  /** Resets progressive computation state (index, accumulator). */
  void ResetDisplayedTotalSizeProgress() {
    filter_caches.total_size.ResetProgress();
  }

  /** Invalidates the time/size filter cache slices and the displayed total size.
   *  Does NOT clear the cached vectors — use after an in-place sort where
   *  rebuilding reuses the vectors; stores capture GetResultsVersion() again. */
  void InvalidateFilterCacheFlags() {
    filter_caches.time.valid = false;
    filter_caches.size.valid = false;
    InvalidateDisplayedTotalSize();
  }

  /** Invalidates displayed total size cache and resets progressive computation state. */
  void InvalidateDisplayedTotalSize() {
    filter_caches.total_size.Invalidate();
  }

  // Phase 4: search-pipeline state (see SearchPipelineState struct).
  // Sole writer: SearchController (+ workflow helpers it calls); render code
  // and status bar read-only.
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  SearchPipelineState search_pipeline;
  // Deferred-cloud-file loading workflow (Phase 6): see CloudFileWorkflowState.
  // Single writer: SearchController/SearchResultUtils (enqueue + reset paths);
  // futures are drained by CleanUpCloudFutures and teardown (WaitAndDrain).
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  CloudFileWorkflowState cloud_files;
  // UI visibility/latch flags and input-debounce state (Phase 8) —
  // see UiVisibilityState / InputDebounceState; per-field writers stay with
  // the corresponding widgets (see FIELD OWNERSHIP MAP rows).
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  UiVisibilityState ui_visibility;
  InputDebounceState input_debounce;

  // Export workflow state (export notification banner + export CSV modal popup).
  // See ExportWorkflowState; clear must not blank an open export modal.
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  ExportWorkflowState export_workflow;
  // Gemini API integration state (see GeminiWorkflowState for invariants).
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  GeminiWorkflowState gemini;

  // Memory usage tracking (updated every 10 seconds to avoid costly system calls)
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  size_t memory_bytes_ = 0;  // Current memory usage in bytes
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  std::chrono::steady_clock::time_point last_memory_update_time_ =
    std::chrono::steady_clock::now();  // Last time memory was updated

  // Index build progress (shared across all platforms) — single writer:
  // Application::UpdateIndexBuildState. See IndexBuildProgressState.
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  IndexBuildProgressState index_build;

  // Attribute loading state (for status bar display and async loading)
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, camelCase is intentional
  bool computingFolderSizes =
    false;  // True when FolderSizeAggregator has pending background work
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  SortGeneration completed_sort_generation_ = 0;  // Generation of the last completed sort
  // All async-sort in-flight state: state machine, counter, token, generation, staging buffers.
  // See AsyncSortState / SortReadyState for invariants and thread-ownership rules.
  // NOLINTNEXTLINE(readability-identifier-naming) - POD-like struct for UI state, snake_case with trailing underscore is intentional
  AsyncSortState async_sort_;

  // Selection and Deletion state
  // Encapsulated in SelectionState to address large-aggregate complexity and improve cohesion.
  // NOLINTNEXTLINE(readability-identifier-naming)
  SelectionState selection;

  // Search History panel interaction state (see HistoryInteractionState).
  // NOLINTNEXTLINE(readability-identifier-naming) - GuiState UI aggregate camelCase exception
  HistoryInteractionState history;

  void MarkInputChanged();
  void ClearInputs();

  /**
   * Build SearchParams from current GuiState values.
   *
   * Converts GuiState input fields into SearchParams for the SearchWorker
   * (filename, path, extensions, foldersOnly, caseSensitive, etc.).
   *
   * @return SearchParams struct ready for SearchWorker::StartSearch()
   */
  [[nodiscard]] SearchParams BuildCurrentSearchParams() const;

  /**
   * Apply a search configuration from JSON (typically from Gemini API).
   *
   * This method applies a SearchConfig to the GuiState, updating all relevant
   * fields. Missing fields in the config use defaults (empty strings, false, None).
   *
   * @param config Search configuration to apply
   */
  void ApplySearchConfig(const gemini_api_utils::SearchConfig& config);

  /**
   * Apply a built-in "Show all indexed files" preset.
   *
   * Clears filename, extension, time, and size filters, then sets the path
   * input to a catch-all path pattern (`pp:**`) and marks input as changed
   * so the next manual search will return all indexed entries.
   */
  void ApplyShowAllPreset();
};
