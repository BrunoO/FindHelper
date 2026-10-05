#pragma once

/**
 * @file ui/StatusBarLogic.h
 * @brief Pure presentation logic for status bar state and text formatting
 *
 * Decoupled from Dear ImGui rendering to enable deterministic unit testing.
 */

#include <chrono>
#include <string>
#include <string_view>

#include "filters/SizeFilterUtils.h"
#include "filters/TimeFilterUtils.h"
#include "gui/GuiState.h"
#include "search/SearchResultUtils.h"
#include "search/SearchWorker.h"

// Forward declaration
class UsnMonitor;

#ifdef _WIN32
#include "usn/UsnMonitor.h"
#endif

namespace ui::status_bar_detail {

inline constexpr int kExportNotificationDurationSeconds = 5;

// Progressive total-size summation (SearchResultUtils::UpdateDisplayedTotalSizeIfNeeded); RenderCenterGroup
// calls the updater each frame, so the busy bar cannot stick when this is true.
[[nodiscard]] inline bool IsDisplayedTotalSizeSummationPending(const GuiState& state) {
  if (state.filter_caches.total_size.valid || !state.search_pipeline.results_complete ||
      state.async_sort_.IsLoading()) {
    return false;
  }
  return !state.result_pool_->Results().empty();
}

[[nodiscard]] inline bool IsCloudFileAttributeLoadActive(const GuiState& state) {
  return !state.cloud_files.loading_futures.empty();
}

// Returns true if export notification/error should still be shown in the status bar.
[[nodiscard]] inline bool IsExportNotificationActive(const GuiState& state) {
  const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - state.export_workflow.notification_time).count();
  return elapsed < kExportNotificationDurationSeconds;
}

// Returns status text for the right group (status bar).
[[nodiscard]] inline std::string GetStatusText(const GuiState& state, const SearchWorker& search_worker,
                                               const UsnMonitor* monitor, bool is_trigram_building) {
  if (state.index_build.in_progress) {
    return state.index_build.status_text.empty() ? "Status: Indexing..."
                                                 : state.index_build.status_text;
  }
  if (state.index_build.failed) {
    return state.index_build.status_text.empty() ? "Status: Index build failed"
                                                 : state.index_build.status_text;
  }
  if (search_worker.IsBusy()) {
    return is_trigram_building ? "Status: Searching (full scan)..." : "Status: Searching...";
  }
  if (state.async_sort_.IsLoading()) {
    return "Status: Loading attributes...";
  }
  if (state.computingFolderSizes) {
    return "Status: Computing folder sizes...";
  }
  if (IsCloudFileAttributeLoadActive(state)) {
    return "Status: Loading cloud file attributes...";
  }
  if (IsDisplayedTotalSizeSummationPending(state)) {
    return "Status: Summing file sizes...";
  }
  if (state.gemini.api_call_in_progress) {
    const std::string& provider_name = state.gemini.api_provider_display_name;
    return "Status: " + (provider_name.empty() ? "AI" : provider_name) + " API...";
  }
  if (IsExportNotificationActive(state)) {
    if (!state.export_workflow.error_message.empty()) {
      return "Error: " + state.export_workflow.error_message;
    }
    if (!state.export_workflow.notification.empty()) {
      return state.export_workflow.notification;
    }
  }
#ifdef _WIN32
  if (monitor != nullptr && monitor->IsIndexIntegrityCompromised()) {
    return "Status: Index may be stale — restart recommended";
  }
#else
  static_cast<void>(monitor);
#endif  // _WIN32
  if (is_trigram_building) {
    return "Status: Optimizing search index...";
  }
  return "Status: Idle";
}

// Returns true when the status bar should show the animated busy progress bar (indexing, searching,
// loading attributes, folder sizes, cloud attribute futures, progressive total-size sum, AI provider API,
// background trigram index rebuild).
[[nodiscard]] inline bool IsStatusBarBusy(const GuiState& state, const SearchWorker& search_worker,
                                          bool is_trigram_building) {
  return state.index_build.in_progress || search_worker.IsBusy() ||
         state.async_sort_.IsLoading() || state.computingFolderSizes ||
         IsCloudFileAttributeLoadActive(state) || IsDisplayedTotalSizeSummationPending(state) ||
         state.gemini.api_call_in_progress || is_trigram_building;
}

}  // namespace ui::status_bar_detail
