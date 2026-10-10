/**
 * @file ui/MetricsWindow.cpp
 * @brief Implementation of metrics window rendering component
 */

#include <chrono>
#include <cstdarg>

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#endif  // _WIN32

#include "core/AppIdentity.h"
#include "gui/GuiState.h"
#include "imgui.h"
#include "index/FileIndex.h"
#include "search/SearchWorker.h"
#include "ui/CenteredToolWindow.h"
#include "ui/IconsFontAwesome.h"
#include "ui/LayoutConstants.h"
#include "ui/MetricsWindow.h"
#include "ui/Theme.h"
#include "ui/UiStyleGuards.h"
#include "usn/UsnMonitor.h"
#include "usn/UsnThroughput.h"
#include "utils/StringUtils.h"

namespace ui {

namespace {
constexpr float kMetricsWindowPivot = 0.5F;
constexpr int kMetricsWindowDefaultWidth = 500;
constexpr int kMetricsWindowDefaultHeight = 700;
/** Tooltip wrap width in em so long descriptions stay readable. */
constexpr float kMetricsTooltipWrapWidthInEm = 35.0F;

[[nodiscard]] float MetricsWindowFooterReservedHeight() {
  const ImGuiStyle& style = ImGui::GetStyle();
  // Matches RenderToolWindowFooterSeparator: Spacing + Separator + Spacing + button row.
  return (style.ItemSpacing.y * 2.0F) + 1.0F + ImGui::GetFrameHeightWithSpacing();
}

void RenderMetricsToolWindowFooter(const UsnMonitor* monitor, SearchWorker* search_worker,
                                   bool* p_open) {
  detail::RenderToolWindowFooterSeparator();

  bool drew_left_button = false;
#ifdef _WIN32
  if (monitor != nullptr) {
    if (ImGui::Button(ICON_FA_REFRESH " Reset Metrics")) {
      monitor->ResetMetrics();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Reset USN journal monitoring counters to zero");
    }
    drew_left_button = true;
  }
#else
  (void)monitor;
#endif  // _WIN32

  if (search_worker != nullptr) {
    if (drew_left_button) {
      ImGui::SameLine();
    }
    if (ImGui::Button(ICON_FA_REFRESH " Reset Search Metrics")) {
      search_worker->ResetMetrics();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Reset search performance counters to zero");
    }
    drew_left_button = true;
  }

  const float close_width = LayoutConstants::kSecondaryButtonWidth;
  if (drew_left_button) {
    const float close_x = ImGui::GetWindowContentRegionMax().x - close_width;
    ImGui::SameLine(close_x);
  } else {
    detail::AlignToolWindowFooterButtons(1, close_width);
  }
  if (ImGui::Button(ICON_FA_XMARK " Close", ImVec2(close_width, 0))) {
    *p_open = false;
  }
}
}  // namespace

// Note: Format string (fmt) is always a compile-time constant literal, never user input.
// All callers pass hardcoded format strings like "Buffers Read: %zu", so this is safe from format string injection.
// tooltip must be plain text (no printf placeholders): it is drawn with TextUnformatted.
void MetricsWindow::RenderMetricText(const char *tooltip, const char *fmt, ...) {  // NOLINT(cert-dcl50-cpp,modernize-avoid-variadic-functions) NOSONAR(cpp:S5281) - ImGui::TextWrappedV requires C varargs; format string validated: all callers pass hardcoded literals
  va_list args;
  va_start(args, fmt);
  // TextWrappedV so long metric lines wrap inside the fixed-width Metrics window.
  ImGui::TextWrappedV(fmt, args);  // NOSONAR(cpp:S5281) - Format string is validated: all callers pass hardcoded string literals (see function comment above)
  va_end(args);

  if (tooltip != nullptr && *tooltip != '\0' && ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * kMetricsTooltipWrapWidthInEm);
    ImGui::TextUnformatted(tooltip);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

void MetricsWindow::Render(bool *p_open,
                           [[maybe_unused]] const UsnMonitor *monitor,
                           SearchWorker *search_worker,
                           FileIndex &file_index,
                           const GuiState &state) {
  // Make window resizable and allow it to float outside main window
  // Set window position and size - use FirstUseEver so position is only set once, allowing user to move it
  const ImGuiViewport *main_viewport = ImGui::GetMainViewport();
  const float center_x = main_viewport->WorkPos.x + (main_viewport->WorkSize.x * kMetricsWindowPivot);
  const float center_y = main_viewport->WorkPos.y + (main_viewport->WorkSize.y * kMetricsWindowPivot);
  const ImVec2 center(center_x, center_y);
  ImGui::SetNextWindowPos(center, ImGuiCond_FirstUseEver, ImVec2(kMetricsWindowPivot, kMetricsWindowPivot));
  ImGui::SetNextWindowSize(ImVec2(static_cast<float>(kMetricsWindowDefaultWidth), static_cast<float>(kMetricsWindowDefaultHeight)), ImGuiCond_FirstUseEver);

  detail::RenderToolWindow(METRICS_WINDOW_TITLE_STR, p_open, Theme::Colors::WindowBg,
                           [monitor, search_worker, &file_index, &state, p_open] {
    MetricsWindow::RenderMetricsToolWindowBody(monitor, search_worker, file_index, state, p_open);
  });
}

void MetricsWindow::RenderMetricsToolWindowBody(const UsnMonitor *monitor,
                                                SearchWorker *search_worker,
                                                FileIndex &file_index,
                                                const GuiState &state,
                                                bool *p_open) {
  const float footer_reserved_height = MetricsWindowFooterReservedHeight();
  ImGui::BeginChild("MetricsContent", ImVec2(0.0F, -footer_reserved_height), ImGuiChildFlags_None,
                    ImGuiWindowFlags_AlwaysVerticalScrollbar);
  if (ImGui::IsWindowAppearing()) {
    ImGui::SetScrollY(0.0F);
  }

#ifdef _WIN32
  RenderMonitorMetricsSection(monitor, file_index);
#endif  // _WIN32

  RenderSearchPerformanceSection(search_worker);
  RenderSystemPerformanceSection(state, file_index);

  ImGui::EndChild();

  RenderMetricsToolWindowFooter(monitor, search_worker, p_open);
}

#ifdef _WIN32
// Windows-only helpers: static in ui so MSVC/LTCG resolves symbols (anonymous namespace caused LNK2001).
// Defined in outer namespace ui (no nested namespace) to avoid MSVC symbol/LTCG issues.
// Live windowed throughput (read-path only, zero hot-path cost): complements
// the cumulative averages (which hide stalls) and Max Buffer Process
// Time (which hides sustained rate). Throttled to one sample per ~500ms so
// 60fps ImGui rendering does not produce noisy per-frame deltas.
static void RenderMonitorLiveThroughput(const UsnMonitorMetrics::Snapshot &snapshot) {
  using usn_throughput::ThroughputSample;
  constexpr auto kLiveWindow = std::chrono::milliseconds(500);
  static ThroughputSample prev_sample{};
  static auto prev_time = std::chrono::steady_clock::time_point{};
  static usn_throughput::ThroughputRates cached{};
  static bool have_baseline = false;
  const ThroughputSample cur_sample{snapshot.buffers_processed,
                                    snapshot.records_processed,
                                    snapshot.current_queue_depth};
  const auto now = std::chrono::steady_clock::now();
  const bool counters_reset = have_baseline &&
                              (cur_sample.buffers_processed < prev_sample.buffers_processed ||
                               cur_sample.records_processed < prev_sample.records_processed);
  if (!have_baseline || counters_reset) {
    prev_sample = cur_sample;
    prev_time = now;
    cached = usn_throughput::ThroughputRates{};
    have_baseline = true;
  } else if (now - prev_time >= kLiveWindow) {
    const double elapsed_sec =
        std::chrono::duration<double>(now - prev_time).count();
    cached = usn_throughput::ComputeThroughput(prev_sample, cur_sample,
                                               elapsed_sec);
    prev_sample = cur_sample;
    prev_time = now;
  }
  MetricsWindow::RenderMetricText(
      "Live buffer throughput over the last ~500ms window "
      "(delta buffers_processed / delta time); stalls show 0.00 here while "
      "the cumulative average stays flat",
      "Live Rate: %.2f buffers/sec", cached.buffers_per_sec);
  MetricsWindow::RenderMetricText(
      "Live record throughput over the last ~500ms window "
      "(delta records_processed / delta time)",
      "Live Record Rate: %.1f records/sec", cached.records_per_sec);
  if (snapshot.current_queue_depth == 0) {
    MetricsWindow::RenderMetricText(
        "Estimated time to drain the current queue at the live rate",
        "Drain ETA: drained");
  } else if (cached.has_eta) {
    MetricsWindow::RenderMetricText(
        "Estimated time to drain the current queue at the live rate "
        "(queue_depth / live buffers_per_sec)",
        "Drain ETA: %s (%zu buffers)",
        usn_throughput::FormatDrainEta(cached.drain_eta_sec).c_str(),
        snapshot.current_queue_depth);
  } else {
    MetricsWindow::RenderMetricText(
        "Estimated time to drain the current queue at the live rate",
        "Drain ETA: stalled (0.00 buffers/sec)");
  }
}

// Single place for everything about USN buffer handling:
// counts -> queue -> read/process timing -> lock contention -> throughput.
static void RenderMonitorUsnBuffers(
    const UsnMonitorMetrics::Snapshot &snapshot) {
  // Counts
  MetricsWindow::RenderMetricText("Total buffers read from USN journal",
                   "Buffers Read: %zu", snapshot.buffers_read);
  MetricsWindow::RenderMetricText("Total buffers that completed processing",
                   "Buffers Processed: %zu", snapshot.buffers_processed);
  if (snapshot.buffers_processed > 0) {
    const double avg_records_per_buffer =
        static_cast<double>(snapshot.records_processed) /
        static_cast<double>(snapshot.buffers_processed);
    MetricsWindow::RenderMetricText("Average number of USN records processed per buffer "
                     "(records_processed / buffers_processed)",
                     "Avg Records/Buffer: %.2f", avg_records_per_buffer);
  }

  // Queue
  ImGui::SeparatorText("Queue");
  MetricsWindow::RenderMetricText("Current number of USN buffers waiting in the queue",
                   "Current Queue Depth: %zu",
                   snapshot.current_queue_depth);
  MetricsWindow::RenderMetricText("Maximum queue depth reached since metrics were reset",
                   "Max Queue Depth: %zu", snapshot.max_queue_depth);
  MetricsWindow::RenderMetricText(
      "Total number of USN buffers dropped because the queue "
      "was full",
      "Buffers Dropped: %zu", snapshot.buffers_dropped);
  ImGui::TextColored(snapshot.buffers_dropped > 0 ? Theme::Colors::Warning
                                                  : Theme::Colors::TextDim,
                     "Buffers dropped (queue full): %zu",
                     snapshot.buffers_dropped);
  ImGui::TextColored(snapshot.current_queue_depth > 10 ? Theme::Colors::Warning
                                                       : Theme::Colors::TextDim,
                     "Queue backlog: %zu buffers",
                     snapshot.current_queue_depth);

  // Timing
  ImGui::SeparatorText("Timing");
  MetricsWindow::RenderMetricText("Cumulative time spent in USN read calls "
                   "since metrics were reset",
                   "Total Read Time: %llu ms",
                   snapshot.total_read_time_ms);
  MetricsWindow::RenderMetricText("Cumulative time spent processing USN buffers "
                   "since metrics were reset",
                   "Total Process Time: %llu ms",
                   snapshot.total_process_time_ms);
  if (snapshot.buffers_read > 0) {
    const double avg_read_time =
        static_cast<double>(snapshot.total_read_time_ms) /
        static_cast<double>(snapshot.buffers_read);
    MetricsWindow::RenderMetricText(
        "Average time spent reading each buffer from the USN "
        "journal (total_read_time_ms / buffers_read)",
        "Avg Read Time/Buffer: %.2f ms", avg_read_time);
  }
  if (snapshot.buffers_processed > 0) {
    const double avg_process_time =
        static_cast<double>(snapshot.total_process_time_ms) /
        static_cast<double>(snapshot.buffers_processed);
    MetricsWindow::RenderMetricText("Average time spent processing each USN buffer "
                     "(total_process_time_ms / buffers_processed)",
                     "Avg Process Time/Buffer: %.2f ms",
                     avg_process_time);
  }
  MetricsWindow::RenderMetricText(
      "Worst single-buffer USN apply time; spikes identify "
      "placeholder-churn bottlenecks that the average alone hides",
      "Max Buffer Process Time: %llums", snapshot.max_buffer_process_time_ms);
  if (snapshot.last_update_time_ms > 0) {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    const uint64_t time_since_update = now - snapshot.last_update_time_ms;
    MetricsWindow::RenderMetricText("Elapsed time since the last metrics update "
                     "(now - last_update_time_ms)",
                     "Last Update: %llu ms ago", time_since_update);
  }

  // Lock contention
  ImGui::SeparatorText("Index Lock Contention");
  MetricsWindow::RenderMetricText("Cumulative time spent blocked acquiring the "
                   "per-buffer FileIndex unique_lock since metrics were reset "
                   "(subset of Total Process Time above)",
                   "Total Lock Wait Time: %llu ms",
                   snapshot.lock_wait.total_ms);
  MetricsWindow::RenderMetricText("Worst single-buffer wait for the FileIndex "
                   "unique_lock; close to Max Buffer Process Time means slow "
                   "buffers are search contention, not CPU",
                   "Max Lock Wait Time: %llu ms",
                   snapshot.lock_wait.max_ms);
  if (snapshot.total_process_time_ms > 0) {
    const double lock_wait_share =
        (static_cast<double>(snapshot.lock_wait.total_ms) * 100.0) /
        static_cast<double>(snapshot.total_process_time_ms);
    MetricsWindow::RenderMetricText("Share of Total Process Time spent waiting "
                     "for the index lock (lock_wait / total_process_time); "
                     "high share = contention, low share = CPU-bound apply",
                     "Lock Wait Share: %.1f%%", lock_wait_share);
  }

  // Throughput
  ImGui::SeparatorText("Throughput");
  if (snapshot.buffers_processed > 0 &&
      snapshot.total_process_time_ms > 0) {
    const double buffers_per_sec =
        (static_cast<double>(snapshot.buffers_processed) * 1000.0) /
        static_cast<double>(snapshot.total_process_time_ms);
    MetricsWindow::RenderMetricText(
        "Average buffer processing throughput in buffers per "
        "second (buffers_processed / total_process_time_ms)",
        "Processing Rate: %.2f buffers/sec", buffers_per_sec);
  }
  RenderMonitorLiveThroughput(snapshot);
}

static void RenderMonitorProcessingStatistics(
    const UsnMonitorMetrics::Snapshot &snapshot,
    const FileIndex &file_index) {
  MetricsWindow::RenderMetricText("Total USN records processed (matching interesting "
                   "reasons)",
                   "Records Processed: %zu", snapshot.records_processed);
  // Never block the UI on index writers (USN Remove / Maintain rebuild).
  // try_lock + short TTL cache: when the unique_lock is held, keep showing the
  // last snapshot instead of waiting — waiting here contended with bulk
  // soft-deletes and crashed / wedged the app when Metrics had focus.
  constexpr auto kMaintenanceStatsCacheTtl = std::chrono::milliseconds(250);
  static FileIndex::MaintenanceStats cached_stats{};
  static auto last_fetch = std::chrono::steady_clock::time_point{};
  static bool have_cache = false;
  if (const auto now = std::chrono::steady_clock::now();
      !have_cache || now - last_fetch >= kMaintenanceStatsCacheTtl) {
    if (const auto fresh = file_index.TryGetMaintenanceStats(); fresh.has_value()) {
      cached_stats = *fresh;
      last_fetch = now;
      have_cache = true;
    }
  }
  if (!have_cache) {
    return;
  }
  const auto &stats = cached_stats;
  MetricsWindow::RenderMetricText("Index maintenance statistics",
                   "Index Rebuilds: %zu, Deleted Entries: %zu/%zu",
                   stats.rebuild_count, stats.deleted_count,
                   stats.total_entries);
  MetricsWindow::RenderMetricText(
      "Remove() calls for entries that were never present in the index",
      "Remove() Not In Index: %zu", stats.remove_not_in_index_count);
  MetricsWindow::RenderMetricText(
      "Duplicate Remove() attempts for the same entry",
      "Remove() Duplicates: %zu", stats.remove_duplicate_count);
  MetricsWindow::RenderMetricText(
      "Remove() inconsistency detections (index state mismatch)",
      "Remove() Inconsistencies: %zu", stats.remove_inconsistency_count);
}

static void RenderMonitorFileOperations(
    const UsnMonitorMetrics::Snapshot &snapshot) {
  MetricsWindow::RenderMetricText("Number of file creation events observed in the USN "
                   "journal",
                   "Files Created: %zu", snapshot.files_created);
  MetricsWindow::RenderMetricText("Number of file deletion events observed in the USN "
                   "journal",
                   "Files Deleted: %zu", snapshot.files_deleted);
  MetricsWindow::RenderMetricText("Number of rename operations (counted once per "
                   "RENAME_NEW_NAME event)",
                   "Files Renamed: %zu", snapshot.files_renamed);
  MetricsWindow::RenderMetricText("Number of file modifications that changed file size "
                   "(DATA_EXTEND/TRUNCATION/OVERWRITE)",
                   "Files Modified: %zu", snapshot.files_modified);
  const size_t total_ops = snapshot.files_created + snapshot.files_deleted +
                           snapshot.files_renamed + snapshot.files_modified;
  MetricsWindow::RenderMetricText("Total number of file operations observed "
                   "(created + deleted + renamed + modified)",
                   "Total Operations: %zu", total_ops);
}

// Index entries waiting on a missing parent (not buffer-related).
static void RenderMonitorPlaceholderStatistics(
    const UsnMonitorMetrics::Snapshot &snapshot) {
  MetricsWindow::RenderMetricText(
      "Index entries waiting on a missing parent (bare-name placeholders; "
      "normally 0 outside bursts)",
      "Awaiting: %zu", snapshot.awaiting_count);
  MetricsWindow::RenderMetricText(
      "Age of the longest-waiting placeholder; old values identify "
      "never-arriving parents (filtered, dropped, deleted)",
      "Awaiting Oldest Age: %llums", snapshot.awaiting_oldest_age_ms);
  MetricsWindow::RenderMetricText(
      "Placeholders evicted by the never-arriving sweep (never-arriving parents)",
      "Never-Arriving Evicted: %zu", snapshot.never_arriving_evicted);
  MetricsWindow::RenderMetricText(
      "Placeholders re-resolved when a missing parent arrived (heal path); "
      "with unresolved + evicted completes the awaiting flow equation",
      "Healed Awaiting: %zu", snapshot.healed_awaiting_total);
}

static void RenderMonitorErrorStatistics(
    const UsnMonitorMetrics::Snapshot &snapshot) {
  MetricsWindow::RenderMetricText("Total number of errors returned by USN journal reads",
                   "Total Errors: %zu", snapshot.errors.errors_encountered);
  MetricsWindow::RenderMetricText("Number of journal wrap-around events "
                   "(ERROR_JOURNAL_ENTRY_DELETED)",
                   "Journal Wrap Errors: %zu",
                   snapshot.errors.journal_wrap_errors);
  MetricsWindow::RenderMetricText("Number of invalid parameter errors "
                   "(ERROR_INVALID_PARAMETER) from USN reads",
                   "Invalid Param Errors: %zu",
                   snapshot.errors.invalid_param_errors);
  MetricsWindow::RenderMetricText("Number of other error types from USN reads",
                   "Other Errors: %zu", snapshot.errors.other_errors);
  MetricsWindow::RenderMetricText("Current streak of consecutive read errors",
                   "Consecutive Errors: %zu",
                   snapshot.errors.consecutive_errors);
  MetricsWindow::RenderMetricText("Maximum streak of consecutive read errors observed "
                   "since metrics were reset",
                   "Max Consecutive Errors: %zu",
                   snapshot.errors.max_consecutive_errors);
  if (snapshot.errors.errors_encountered > 0) {
    ImGui::TextColored(Theme::Colors::Warning,
                      "Errors detected - check logs for details");
  }
  if (snapshot.errors.consecutive_errors > 0) {
    ImGui::TextColored(Theme::Colors::Error,
                      "Warning: %zu consecutive errors",
                      snapshot.errors.consecutive_errors);
  }
}

// Record / file-operation throughput (buffer throughput lives in "USN Buffers").
static void RenderMonitorPerformanceSummary(
    const UsnMonitorMetrics::Snapshot &snapshot) {
  if (snapshot.records_processed > 0 &&
      snapshot.total_process_time_ms > 0) {
    const double records_per_sec =
        (static_cast<double>(snapshot.records_processed) * 1000.0) /
        static_cast<double>(snapshot.total_process_time_ms);
    MetricsWindow::RenderMetricText(
        "Average record processing throughput in records per "
        "second (records_processed / total_process_time_ms)",
        "Record Processing Rate: %.2f records/sec", records_per_sec);
  }
  const size_t total_ops = snapshot.files_created + snapshot.files_deleted +
                           snapshot.files_renamed + snapshot.files_modified;
  if (total_ops > 0 && snapshot.total_process_time_ms > 0) {
    const double ops_per_sec = (static_cast<double>(total_ops) * 1000.0) /
                               static_cast<double>(snapshot.total_process_time_ms);
    MetricsWindow::RenderMetricText(
        "Average file operation throughput in operations per "
        "second ((created + deleted + renamed + modified) / "
        "total_process_time_ms)",
        "File Operation Rate: %.2f ops/sec", ops_per_sec);
  }
}

void MetricsWindow::RenderMonitorMetricsSection(const UsnMonitor *monitor,
                                                const FileIndex &file_index) {
  if (monitor == nullptr) {
    return;
  }
  const auto snapshot = monitor->GetMetricsSnapshot();
  if (ImGui::CollapsingHeader("USN Buffers",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    RenderMonitorUsnBuffers(snapshot);
  }
  if (ImGui::CollapsingHeader("Processing Statistics",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    RenderMonitorProcessingStatistics(snapshot, file_index);
  }
  if (ImGui::CollapsingHeader("File Operations",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    RenderMonitorFileOperations(snapshot);
  }
  if (ImGui::CollapsingHeader("Parent Placeholders",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    RenderMonitorPlaceholderStatistics(snapshot);
  }
  if (ImGui::CollapsingHeader("Error Statistics")) {
    RenderMonitorErrorStatistics(snapshot);
  }
  if (ImGui::CollapsingHeader("Performance Summary")) {
    RenderMonitorPerformanceSummary(snapshot);
  }
}

#else
// Stub for non-Windows: USN monitor metrics not available (Windows-only feature).
void MetricsWindow::RenderMonitorMetricsSection(const UsnMonitor * /*monitor*/,
                                               const FileIndex & /*file_index*/) {
  /* Stub: USN monitor metrics only available on Windows. */
}
#endif  // _WIN32

void MetricsWindow::RenderSearchPerformanceSection(const SearchWorker* search_worker) {
  if (search_worker == nullptr ||
      !ImGui::CollapsingHeader("Search Performance",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }
  const auto search_snapshot = search_worker->GetMetricsSnapshot();

    // Search Statistics
    MetricsWindow::RenderMetricText("Number of searches executed since search metrics were "
                     "reset",
                     "Total Searches: %zu", search_snapshot.total_searches_);
    MetricsWindow::RenderMetricText("Total results returned across all searches "
                     "since metrics were reset",
                     "Total Results Found: %zu",
                     search_snapshot.total_results_found_);

    if (search_snapshot.total_searches_ > 0) {
      const double avg_results =
          static_cast<double>(search_snapshot.total_results_found_) /
          static_cast<double>(search_snapshot.total_searches_);
      MetricsWindow::RenderMetricText("Average number of results returned per search "
                       "(total_results_found_ / total_searches_)",
                       "Avg Results/Search: %.1f", avg_results);
    }

    ImGui::Separator();

    // Timing Statistics
    MetricsWindow::RenderMetricText("Cumulative time spent in the parallel search phase "
                     "across all searches",
                     "Total Search Time: %llu ms",
                     search_snapshot.total_search_time_ms_);
    MetricsWindow::RenderMetricText("Cumulative time spent in the post-processing phase "
                     "(materializing results, paths, filters) across all "
                     "searches",
                     "Total Post-Process Time: %llu ms",
                     search_snapshot.total_postprocess_time_ms_);

    if (search_snapshot.total_searches_ > 0) {
      const double avg_search_time =
          static_cast<double>(search_snapshot.total_search_time_ms_) /
          static_cast<double>(search_snapshot.total_searches_);
      const double avg_postprocess_time =
          static_cast<double>(search_snapshot.total_postprocess_time_ms_) /
          static_cast<double>(search_snapshot.total_searches_);
      MetricsWindow::RenderMetricText("Average time spent in the search phase per search "
                       "(total_search_time_ms_ / total_searches_)",
                       "Avg Search Time: %.2f ms", avg_search_time);
      MetricsWindow::RenderMetricText("Average time spent in the post-processing phase per "
                       "search (total_postprocess_time_ms_ / total_searches_)",
                       "Avg Post-Process Time: %.2f ms",
                       avg_postprocess_time);
      const double avg_total_time = avg_search_time + avg_postprocess_time;
      MetricsWindow::RenderMetricText("Average total latency per search "
                       "(Avg Search Time + Avg Post-Process Time)",
                       "Avg Total Time: %.2f ms", avg_total_time);
    }

    ImGui::Separator();
    MetricsWindow::RenderMetricText("Longest observed search phase duration among all "
                     "searches",
                     "Max Search Time: %llu ms",
                     search_snapshot.max_search_time_ms_);
    MetricsWindow::RenderMetricText("Longest observed post-processing phase duration among "
                     "all searches",
                     "Max Post-Process Time: %llu ms",
                     search_snapshot.max_postprocess_time_ms_);
    MetricsWindow::RenderMetricText("Largest number of results returned by a single search",
                     "Max Results in Single Search: %zu",
                     search_snapshot.max_results_count_);

    ImGui::Separator();

    // Last Search
    MetricsWindow::RenderMetricText("Summary of the most recently completed search",
                     "Last Search:");
    MetricsWindow::RenderMetricText("Number of results returned by the most recent search",
                     "  Results: %zu", search_snapshot.last_results_count_);
    const uint64_t last_total_time_us = search_snapshot.last_search_time_us_ +
                                        search_snapshot.last_postprocess_time_us_;
    constexpr uint64_t kTenMillisecondsUs = 10000;
    constexpr double kMillisecondsPerMicrosecond = 0.001;

    if (search_snapshot.last_search_time_us_ < kTenMillisecondsUs) {
      MetricsWindow::RenderMetricText("Time spent in the search phase for the most recent "
                       "search",
                       "  Search Time: %.2f ms",
                       static_cast<double>(search_snapshot.last_search_time_us_) * kMillisecondsPerMicrosecond);
    } else {
      MetricsWindow::RenderMetricText("Time spent in the search phase for the most recent "
                       "search",
                       "  Search Time: %llu ms",
                       search_snapshot.last_search_time_ms_);
    }

    if (search_snapshot.last_postprocess_time_us_ < kTenMillisecondsUs) {
      MetricsWindow::RenderMetricText("Time spent in the post-processing phase for the most "
                       "recent search",
                       "  Post-Process Time: %.2f ms",
                       static_cast<double>(search_snapshot.last_postprocess_time_us_) * kMillisecondsPerMicrosecond);
    } else {
      MetricsWindow::RenderMetricText("Time spent in the post-processing phase for the most "
                       "recent search",
                       "  Post-Process Time: %llu ms",
                       search_snapshot.last_postprocess_time_ms_);
    }

    if (last_total_time_us < kTenMillisecondsUs) {
      MetricsWindow::RenderMetricText("Total latency of the most recent search "
                       "(Search Time + Post-Process Time)",
                       "  Total Time: %.2f ms",
                       static_cast<double>(last_total_time_us) * kMillisecondsPerMicrosecond);
    } else {
      const uint64_t last_total_time = search_snapshot.last_search_time_ms_ +
                                 search_snapshot.last_postprocess_time_ms_;
      MetricsWindow::RenderMetricText("Total latency of the most recent search "
                       "(Search Time + Post-Process Time)",
                       "  Total Time: %llu ms", last_total_time);
    }

    if (search_snapshot.last_results_count_ > 0 && last_total_time_us > 0) {
      const double results_per_ms =
          (static_cast<double>(search_snapshot.last_results_count_) * 1000.0) /
          static_cast<double>(last_total_time_us);
      MetricsWindow::RenderMetricText("Throughput of the most recent search in results per "
                       "millisecond (last_results_count_ / total_time)",
                       "  Processing Rate: %.1f results/ms", results_per_ms);
    }

    ImGui::Separator();

    // Performance Summary
    if (search_snapshot.total_searches_ > 0) {
      if (const uint64_t total_time = search_snapshot.total_search_time_ms_ +
                            search_snapshot.total_postprocess_time_ms_; total_time > 0) {
        const double searches_per_sec =
            (static_cast<double>(search_snapshot.total_searches_) * 1000.0) /
            static_cast<double>(total_time);
        MetricsWindow::RenderMetricText("Average number of searches completed per second "
                         "(total_searches_ / total_time)",
                         "Search Rate: %.2f searches/sec", searches_per_sec);
      }

      if (search_snapshot.total_results_found_ > 0) {
        const uint64_t total_time = search_snapshot.total_search_time_ms_ +
                              search_snapshot.total_postprocess_time_ms_;
        RenderResultThroughputMetric(search_snapshot.total_results_found_, total_time);
      }

      // Time distribution
      if (search_snapshot.total_search_time_ms_ > 0 ||
          search_snapshot.total_postprocess_time_ms_ > 0) {
        RenderTimeDistributionMetrics(search_snapshot.total_search_time_ms_,
                                     search_snapshot.total_postprocess_time_ms_);
      }
    }

}

// Renders index build timing (current elapsed if in progress, last duration if completed).
static void RenderIndexBuildTiming(const GuiState &state) {
  if (!state.index_build.has_timing) {
    return;
  }

  uint64_t elapsed_ms = 0U;
  if (state.index_build.in_progress) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - state.index_build.start_time);
    elapsed_ms = static_cast<uint64_t>(elapsed.count());
  } else if (state.index_build.last_duration_ms > 0U) {
    elapsed_ms = state.index_build.last_duration_ms;
  } else {
    return;
  }

  constexpr const char *kTooltip =
      "Duration of the index build (elapsed time if in progress, otherwise the last build)";
  constexpr uint64_t kMillisecondsPerSecond = 1000;
  constexpr double kSecondsPerMillisecond = 0.001;
  if (elapsed_ms < kMillisecondsPerSecond) {
    MetricsWindow::RenderMetricText(kTooltip, "Index Build: %zums",
                                    static_cast<size_t>(elapsed_ms));
  } else {
    MetricsWindow::RenderMetricText(kTooltip, "Index Build: %.2fs",
                                    static_cast<double>(elapsed_ms) * kSecondsPerMillisecond);
  }
}

void MetricsWindow::RenderSystemPerformanceSection(const GuiState &state, FileIndex &file_index) {
  if (!ImGui::CollapsingHeader("System Performance",
                               ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }
  // FPS
  const ImGuiIO& io = ImGui::GetIO();
  MetricsWindow::RenderMetricText("Frames per second (UI rendering performance)",
                   "FPS: %.1f", io.Framerate);

  // Thread count
  const size_t thread_count = file_index.GetSearchThreadPoolCount();
  MetricsWindow::RenderMetricText("Number of threads in search thread pool",
                   "Search Threads: %zu", thread_count);

  RenderIndexBuildTiming(state);

  // Trigram index memory (non-blocking try-lock with short TTL cache to avoid UI stalls)
  constexpr auto kIndexMetricsCacheTtl = std::chrono::milliseconds(250);
  static size_t cached_trigram_bytes = 0;
  static auto last_fetch = std::chrono::steady_clock::time_point{};
  static bool have_cache = false;
  if (const auto now = std::chrono::steady_clock::now();
      !have_cache || now - last_fetch >= kIndexMetricsCacheTtl) {
    if (const auto fresh = file_index.TryGetTrigramIndexMemoryBytes(); fresh.has_value()) {
      cached_trigram_bytes = *fresh;
      last_fetch = now;
      have_cache = true;
    }
  }
  if (have_cache) {
    MetricsWindow::RenderMetricText(
        "Memory consumed by the inverted filename trigram index (Roaring Bitmaps postings)",
        "Trigram Index Memory: %s", FormatMemory(cached_trigram_bytes).c_str());
    const bool trigram_building = file_index.IsTrigramIndexBuilding();
    MetricsWindow::RenderMetricText(
        "Current lifecycle state of the inverted filename trigram acceleration index",
        "Trigram Index Status: %s", trigram_building ? "Rebuilding (optimizing)..." : "Ready");
  }

  // Extension index memory (same non-blocking TTL pattern as above)
  static size_t cached_extension_bytes = 0;
  static auto last_extension_fetch = std::chrono::steady_clock::time_point{};
  static bool have_extension_cache = false;
  if (const auto now = std::chrono::steady_clock::now();
      !have_extension_cache || now - last_extension_fetch >= kIndexMetricsCacheTtl) {
    if (const auto fresh = file_index.TryGetExtensionIndexMemoryBytes(); fresh.has_value()) {
      cached_extension_bytes = *fresh;
      last_extension_fetch = now;
      have_extension_cache = true;
    }
  }
  if (have_cache && have_extension_cache) {
    MetricsWindow::RenderMetricText(
        "Memory consumed by the inverted file-extension index (Roaring Bitmaps postings)",
        "Extension Index Memory: %s", FormatMemory(cached_extension_bytes).c_str());
    const bool extension_building = file_index.IsExtensionIndexBuilding();
    MetricsWindow::RenderMetricText(
        "Current lifecycle state of the inverted file-extension acceleration index",
        "Extension Index Status: %s", extension_building ? "Rebuilding (optimizing)..." : "Ready");
    MetricsWindow::RenderMetricText(
        "Combined memory of both candidate-filter acceleration indexes",
        "Combined Index Memory: %s",
        FormatMemory(cached_trigram_bytes + cached_extension_bytes).c_str());
  }
}

void MetricsWindow::RenderResultThroughputMetric(uint64_t total_results_found,
                                                  uint64_t total_time_ms) {
  if (total_time_ms == 0) {
    return;
  }
  const double results_per_sec =
      (static_cast<double>(total_results_found) * 1000.0) / static_cast<double>(total_time_ms);
  MetricsWindow::RenderMetricText("Average result throughput in results per second "
                   "(total_results_found / total_time_ms)",
                   "Result Processing Rate: %.1f results/sec", results_per_sec);
}

void MetricsWindow::RenderTimeDistributionMetrics(uint64_t total_search_time_ms,
                                                   uint64_t total_postprocess_time_ms) {
  const uint64_t total_time = total_search_time_ms + total_postprocess_time_ms;
  if (total_time == 0) {
    return;
  }
  const double search_percent =
      (static_cast<double>(total_search_time_ms) * 100.0) / static_cast<double>(total_time);
  const double postprocess_percent =
      (static_cast<double>(total_postprocess_time_ms) * 100.0) / static_cast<double>(total_time);
  MetricsWindow::RenderMetricText(
      "Breakdown of total search time between search and "
      "post-processing phases",
      "Time Distribution:");
  MetricsWindow::RenderMetricText("Percentage of total time spent in the search phase",
                   "  Search: %.1f%%", search_percent);
  MetricsWindow::RenderMetricText("Percentage of total time spent in the "
                   "post-processing phase",
                   "  Post-Process: %.1f%%", postprocess_percent);
}

} // namespace ui

