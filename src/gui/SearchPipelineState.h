#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "gui/ResultFilterCaches.h"

// Search-lifecycle state (god-object Phase 4). Sole writer: SearchController
// (plus the workflow helpers it calls); render code and status bar read-only.
// snake_case public members per the substate naming rule.
struct SearchPipelineState {
  // True while a search is running (used to gate triggers and skip re-polls).
  bool search_active = false;
  // True when the user is typing (debounce input) so SearchController discards in-flight results.
  bool results_complete = true;
  uint64_t search_session_id = 0;
  std::string search_error;

  // True if the current search was manually triggered (button/Enter); false for debounced/auto-refresh.
  // Used by Application to decide whether to record a search history entry.
  bool search_was_manual = false;
  // Set when PollResults completes a manual search. Survives auto-refresh/debounce
  // in the same frame, which clears search_was_manual before Application records history.
  // Application::UpdateSearchState consumes it (sole consumer) via ConsumeManualHistoryRecord().
  bool pending_manual_history_record = false;
  // One-shot consume (set-once/clear-once state-machine style, mirroring AsyncSortState
  // helpers): returns the previous value and clears the flag (result optional;
  // the primary use here is the clear side effect).
  bool ConsumeManualHistoryRecord() {
    return std::exchange(pending_manual_history_record, false);
  }

  // Index mutation version at the last auto-refresh trigger (baseline).
  // Version-based (not size-based) so heals and renames — which keep Size()
  // — also refresh visible results.
  uint64_t last_index_mutation = 0;
  // Timestamp of the last auto-refresh trigger; enforces a cooldown between
  // refreshes so rapid USN activity (bulk ops) does not re-trigger every frame.
  std::chrono::steady_clock::time_point last_auto_refresh_time;
  // Throttle timestamp for deferred cloud-file cleanup while displayed-total
  // computation is in progress (see UpdateTimeFilterCacheIfNeeded). Per-state
  // (not a function static) so concurrent states/tests do not share a budget.
  std::chrono::steady_clock::time_point last_cloud_cleanup_time;

  // One-shot commit flags: set by PollResults / folder-stats; consumed per frame.
  bool results_updated = false;
  // Set by PollResults after committing a pre-sorted back buffer. Consumed by
  // HandleTableSorting so sync columns skip a redundant clone+re-sort+re-remap.
  bool results_presorted_on_commit = false;
  // Set by ClearInputs; consumed by SearchController::Update to call
  // SearchWorker::DiscardResults so PollResults does not re-apply stale results.
  bool clear_results_requested = false;
  // One-frame defer when active filters need unloaded attrs
  // (see SearchResultUtils::ShouldDeferFilterCacheRebuild).
  bool defer_filter_cache_rebuild = false;

  // FolderSizeAggregator cache staleness cluster (see struct comment above).
  size_t last_folder_aggregator_index_size = 0;
  bool folder_aggregator_cache_stale = false;
  size_t pending_folder_aggregator_index_size = 0;
  bool folder_aggregator_index_size_initialized = false;
  std::chrono::steady_clock::time_point last_folder_aggregator_reset_time;

  // FlushAggregatorFolderStats idle fast-path (perf): results version at the last
  // full pending-dir scan + whether that scan found any pending directories.
  // Lets steady-state frames (same results, aggregator idle, nothing pending) skip
  // the O(N) scan. Sole writer: FlushAggregatorFolderStats itself (UI thread).
  ResultsVersion last_folder_stats_flush_version = 0;
  bool folder_stats_flush_had_pending = false;
};
