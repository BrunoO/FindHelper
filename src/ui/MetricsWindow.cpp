/**
 * @file ui/MetricsWindow.cpp
 * @brief Implementation of metrics window rendering component
 */

#include <chrono>
#include <cstdarg>

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#endif  // _WIN32

#include "core/Version.h"
#include "imgui.h"
#include "index/FileIndex.h"
#include "search/SearchWorker.h"
#include "ui/IconsFontAwesome.h"
#include "ui/MetricsWindow.h"
#include "ui/Theme.h"
#include "ui/UiStyleGuards.h"
#include "usn/UsnMonitor.h"

namespace ui {

namespace {
constexpr float kMetricsWindowPivot = 0.5F;
constexpr int kMetricsWindowDefaultWidth = 500;
constexpr int kMetricsWindowDefaultHeight = 700;
}  // namespace

// Note: Format string (fmt) is always a compile-time constant literal, never user input.
// All callers pass hardcoded format strings like "Buffers Read: %zu", so this is safe from format string injection.
void MetricsWindow::RenderMetricText(const char *tooltip, const char *fmt, ...) {  // NOLINT(cert-dcl50-cpp,cppcoreguidelines-pro-type-vararg,hicpp-vararg) NOSONAR(cpp:S5281) - ImGui::TextV requires C varargs; format string validated: all callers pass hardcoded literals
  va_list args;  // NOLINT(cppcoreguidelines-init-variables,cppcoreguidelines-pro-type-vararg,hicpp-vararg) - va_list initialized by va_start() below; required for ImGui::TextV
  va_start(args, fmt);
  ImGui::TextV(fmt, args);  // NOSONAR(cpp:S5281) - Format string is validated: all callers pass hardcoded string literals (see function comment above)
  va_end(args);

  if (tooltip != nullptr && *tooltip != '\0' && ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(tooltip);
    ImGui::EndTooltip();
  }
}

void MetricsWindow::Render(bool *p_open,
                           [[maybe_unused]] UsnMonitor *monitor,
                           SearchWorker *search_worker,
                           FileIndex &file_index) {
  if (p_open == nullptr || !*p_open) {
    return;
  }

  const detail::StyleColorGuard window_bg_guard(ImGuiCol_WindowBg, Theme::Colors::WindowBg);

  const char *metrics_title = METRICS_WINDOW_TITLE_STR;

  // Make window resizable and allow it to float outside main window
  // Set window position and size - use FirstUseEver so position is only set once, allowing user to move it
  const ImGuiViewport *main_viewport = ImGui::GetMainViewport();
  const float center_x = main_viewport->WorkPos.x + (main_viewport->WorkSize.x * kMetricsWindowPivot);
  const float center_y = main_viewport->WorkPos.y + (main_viewport->WorkSize.y * kMetricsWindowPivot);
  const ImVec2 center(center_x, center_y);
  ImGui::SetNextWindowPos(center, ImGuiCond_FirstUseEver, ImVec2(kMetricsWindowPivot, kMetricsWindowPivot));
  ImGui::SetNextWindowSize(ImVec2(static_cast<float>(kMetricsWindowDefaultWidth), static_cast<float>(kMetricsWindowDefaultHeight)), ImGuiCond_FirstUseEver);
  
  const detail::WindowGuard window_guard(metrics_title, p_open, ImGuiWindowFlags_None);
  if (window_guard.ShowContent()) {
#ifdef _WIN32
    if (monitor) {
      if (ImGui::Button("Reset Metrics")) {
        monitor->ResetMetrics();
      }
      ImGui::SameLine();
    }
#else
    // macOS: No monitoring metrics available (monitor is always nullptr)
#endif  // _WIN32
    if (ImGui::Button(ICON_FA_XMARK " Close")) {
      *p_open = false;
    }

    ImGui::Separator();

#ifdef _WIN32
    RenderMonitorMetricsSection(monitor, file_index);
#endif  // _WIN32

    RenderSearchPerformanceSection(search_worker);
    RenderSystemPerformanceSection(file_index);
  }
}

#ifdef _WIN32
// Windows-only helpers: static in ui so MSVC/LTCG resolves symbols (anonymous namespace caused LNK2001).
// Defined in outer namespace ui (no nested namespace) to avoid MSVC symbol/LTCG issues.
static void RenderMonitorProcessingStatistics(
    const UsnMonitorMetrics::Snapshot &snapshot,
    const FileIndex &file_index) {
  MetricsWindow::RenderMetricText("Total buffers read from USN journal",  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "Buffers Read: %zu", snapshot.buffers_read);
  MetricsWindow::RenderMetricText("Total buffers that completed processing",  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "Buffers Processed: %zu", snapshot.buffers_processed);
  MetricsWindow::RenderMetricText("Total USN records processed (matching interesting "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "reasons)",
                   "Records Processed: %zu", snapshot.records_processed);
  {
    const auto stats = file_index.GetMaintenanceStats();
    MetricsWindow::RenderMetricText("Index maintenance statistics",  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                     "Buffer Rebuilds: %zu, Deleted Entries: %zu/%zu",
                     stats.rebuild_count, stats.deleted_count,
                     stats.total_entries);
    if (stats.remove_not_in_index_count > 0 ||
        stats.remove_duplicate_count > 0 ||
        stats.remove_inconsistency_count > 0) {
      MetricsWindow::RenderMetricText("Remove() diagnostics: not_in_index=%zu, "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                       "duplicate=%zu, inconsistency=%zu",
                       "Remove() Diagnostics: not_in_index=%zu, "
                       "duplicate=%zu, inconsistency=%zu",
                       stats.remove_not_in_index_count,
                       stats.remove_duplicate_count,
                       stats.remove_inconsistency_count);
    }
  }
  if (snapshot.buffers_processed > 0) {
    const double avg_records_per_buffer =
        static_cast<double>(snapshot.records_processed) /
        static_cast<double>(snapshot.buffers_processed);
    MetricsWindow::RenderMetricText("Average number of USN records processed per buffer "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                     "(records_processed / buffers_processed)",
                     "Avg Records/Buffer: %.2f", avg_records_per_buffer);
  }
}

static void RenderMonitorFileOperations(
    const UsnMonitorMetrics::Snapshot &snapshot) {
  MetricsWindow::RenderMetricText("Number of file creation events observed in the USN "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "journal",
                   "Files Created: %zu", snapshot.files_created);
  MetricsWindow::RenderMetricText("Number of file deletion events observed in the USN "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "journal",
                   "Files Deleted: %zu", snapshot.files_deleted);
  MetricsWindow::RenderMetricText("Number of rename operations (counted once per "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "RENAME_NEW_NAME event)",
                   "Files Renamed: %zu", snapshot.files_renamed);
  MetricsWindow::RenderMetricText("Number of file modifications that changed file size "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "(DATA_EXTEND/TRUNCATION/OVERWRITE)",
                   "Files Modified: %zu", snapshot.files_modified);
  const size_t total_ops = snapshot.files_created + snapshot.files_deleted +
                           snapshot.files_renamed + snapshot.files_modified;
  MetricsWindow::RenderMetricText("Total number of file operations observed "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "(created + deleted + renamed + modified)",
                   "Total Operations: %zu", total_ops);
}

static void RenderMonitorQueueStatistics(
    const UsnMonitorMetrics::Snapshot &snapshot) {
  MetricsWindow::RenderMetricText("Current number of USN buffers waiting in the queue",  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "Current Queue Depth: %zu",
                   snapshot.current_queue_depth);
  MetricsWindow::RenderMetricText("Maximum queue depth reached since metrics were reset",  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "Max Queue Depth: %zu", snapshot.max_queue_depth);
  MetricsWindow::RenderMetricText(  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      "Total number of USN buffers dropped because the queue "
      "was full",
      "Buffers Dropped: %zu", snapshot.buffers_dropped);
  if (snapshot.buffers_dropped > 0) {
    ImGui::TextColored(Theme::Colors::Warning,
                      "Warning: %zu buffers dropped (queue full)",
                      snapshot.buffers_dropped);
  }
  if (snapshot.current_queue_depth > 10) {
    ImGui::TextColored(Theme::Colors::Warning,
                      "Queue backlog: %zu buffers",
                      snapshot.current_queue_depth);
  }
}

static void RenderMonitorErrorStatistics(
    const UsnMonitorMetrics::Snapshot &snapshot) {
  MetricsWindow::RenderMetricText("Total number of errors returned by USN journal reads",  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "Total Errors: %zu", snapshot.errors_encountered);
  MetricsWindow::RenderMetricText("Number of journal wrap-around events "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "(ERROR_JOURNAL_ENTRY_DELETED)",
                   "Journal Wrap Errors: %zu",
                   snapshot.journal_wrap_errors);
  MetricsWindow::RenderMetricText("Number of invalid parameter errors "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "(ERROR_INVALID_PARAMETER) from USN reads",
                   "Invalid Param Errors: %zu",
                   snapshot.invalid_param_errors);
  MetricsWindow::RenderMetricText("Number of other error types from USN reads",  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "Other Errors: %zu", snapshot.other_errors);
  MetricsWindow::RenderMetricText("Current streak of consecutive read errors",  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "Consecutive Errors: %zu",
                   snapshot.consecutive_errors);
  MetricsWindow::RenderMetricText("Maximum streak of consecutive read errors observed "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "since metrics were reset",
                   "Max Consecutive Errors: %zu",
                   snapshot.max_consecutive_errors);
  if (snapshot.errors_encountered > 0) {
    ImGui::TextColored(Theme::Colors::Warning,
                      "Errors detected - check logs for details");
  }
  if (snapshot.consecutive_errors > 0) {
    ImGui::TextColored(Theme::Colors::Error,
                      "Warning: %zu consecutive errors",
                      snapshot.consecutive_errors);
  }
}

static void RenderMonitorTimingStatistics(
    const UsnMonitorMetrics::Snapshot &snapshot) {
  MetricsWindow::RenderMetricText("Cumulative time spent in USN read calls "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "since metrics were reset",
                   "Total Read Time: %llu ms",
                   snapshot.total_read_time_ms);
  MetricsWindow::RenderMetricText("Cumulative time spent processing USN buffers "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "since metrics were reset",
                   "Total Process Time: %llu ms",
                   snapshot.total_process_time_ms);
  if (snapshot.buffers_read > 0) {
    const double avg_read_time =
        static_cast<double>(snapshot.total_read_time_ms) /
        static_cast<double>(snapshot.buffers_read);
    MetricsWindow::RenderMetricText(  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
        "Average time spent reading each buffer from the USN "
        "journal (total_read_time_ms / buffers_read)",
        "Avg Read Time/Buffer: %.2f ms", avg_read_time);
  }
  if (snapshot.buffers_processed > 0) {
    const double avg_process_time =
        static_cast<double>(snapshot.total_process_time_ms) /
        static_cast<double>(snapshot.buffers_processed);
    MetricsWindow::RenderMetricText("Average time spent processing each USN buffer "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                     "(total_process_time_ms / buffers_processed)",
                     "Avg Process Time/Buffer: %.2f ms",
                     avg_process_time);
  }
  if (snapshot.last_update_time_ms > 0) {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    const uint64_t time_since_update = now - snapshot.last_update_time_ms;
    MetricsWindow::RenderMetricText("Elapsed time since the last metrics update "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                     "(now - last_update_time_ms)",
                     "Last Update: %llu ms ago", time_since_update);
  }
}

static void RenderMonitorPerformanceSummary(
    const UsnMonitorMetrics::Snapshot &snapshot) {
  if (snapshot.buffers_processed > 0 &&
      snapshot.total_process_time_ms > 0) {
    const double buffers_per_sec =
        (static_cast<double>(snapshot.buffers_processed) * 1000.0) /
        static_cast<double>(snapshot.total_process_time_ms);
    MetricsWindow::RenderMetricText(  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
        "Average buffer processing throughput in buffers per "
        "second (buffers_processed / total_process_time_ms)",
        "Processing Rate: %.2f buffers/sec", buffers_per_sec);
  }
  if (snapshot.records_processed > 0 &&
      snapshot.total_process_time_ms > 0) {
    const double records_per_sec =
        (static_cast<double>(snapshot.records_processed) * 1000.0) /
        static_cast<double>(snapshot.total_process_time_ms);
    MetricsWindow::RenderMetricText(  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
        "Average record processing throughput in records per "
        "second (records_processed / total_process_time_ms)",
        "Record Processing Rate: %.2f records/sec", records_per_sec);
  }
  const size_t total_ops = snapshot.files_created + snapshot.files_deleted +
                           snapshot.files_renamed + snapshot.files_modified;
  if (total_ops > 0 && snapshot.total_process_time_ms > 0) {
    const double ops_per_sec = (static_cast<double>(total_ops) * 1000.0) /
                               static_cast<double>(snapshot.total_process_time_ms);
    MetricsWindow::RenderMetricText(  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
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
  if (ImGui::CollapsingHeader("Processing Statistics",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    RenderMonitorProcessingStatistics(snapshot, file_index);
  }
  if (ImGui::CollapsingHeader("File Operations",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    RenderMonitorFileOperations(snapshot);
  }
  if (ImGui::CollapsingHeader("Queue Statistics",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    RenderMonitorQueueStatistics(snapshot);
  }
  if (ImGui::CollapsingHeader("Error Statistics")) {
    RenderMonitorErrorStatistics(snapshot);
  }
  if (ImGui::CollapsingHeader("Timing Statistics")) {
    RenderMonitorTimingStatistics(snapshot);
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

void MetricsWindow::RenderSearchPerformanceSection(SearchWorker *search_worker) {
  if (search_worker == nullptr ||
      !ImGui::CollapsingHeader("Search Performance",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }
  const auto search_snapshot = search_worker->GetMetricsSnapshot();

    // Search Statistics
    MetricsWindow::RenderMetricText("Number of searches executed since search metrics were "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                     "reset",
                     "Total Searches: %zu", search_snapshot.total_searches_);
    MetricsWindow::RenderMetricText("Total results returned across all searches "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                     "since metrics were reset",
                     "Total Results Found: %zu",
                     search_snapshot.total_results_found_);

    if (search_snapshot.total_searches_ > 0) {
      const double avg_results =
          static_cast<double>(search_snapshot.total_results_found_) /
          static_cast<double>(search_snapshot.total_searches_);
      MetricsWindow::RenderMetricText("Average number of results returned per search "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                       "(total_results_found_ / total_searches_)",
                       "Avg Results/Search: %.1f", avg_results);
    }

    ImGui::Separator();

    // Timing Statistics
    MetricsWindow::RenderMetricText("Cumulative time spent in the parallel search phase "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                     "across all searches",
                     "Total Search Time: %llu ms",
                     search_snapshot.total_search_time_ms_);
    MetricsWindow::RenderMetricText("Cumulative time spent in the post-processing phase "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
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
      MetricsWindow::RenderMetricText("Average time spent in the search phase per search "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                       "(total_search_time_ms_ / total_searches_)",
                       "Avg Search Time: %.2f ms", avg_search_time);
      MetricsWindow::RenderMetricText("Average time spent in the post-processing phase per "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                       "search (total_postprocess_time_ms_ / total_searches_)",
                       "Avg Post-Process Time: %.2f ms",
                       avg_postprocess_time);
      const double avg_total_time = avg_search_time + avg_postprocess_time;
      MetricsWindow::RenderMetricText("Average total latency per search "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                       "(Avg Search Time + Avg Post-Process Time)",
                       "Avg Total Time: %.2f ms", avg_total_time);
    }

    ImGui::Separator();
    MetricsWindow::RenderMetricText("Longest observed search phase duration among all "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                     "searches",
                     "Max Search Time: %llu ms",
                     search_snapshot.max_search_time_ms_);
    MetricsWindow::RenderMetricText("Longest observed post-processing phase duration among "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                     "all searches",
                     "Max Post-Process Time: %llu ms",
                     search_snapshot.max_postprocess_time_ms_);
    MetricsWindow::RenderMetricText("Largest number of results returned by a single search",  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                     "Max Results in Single Search: %zu",
                     search_snapshot.max_results_count_);

    ImGui::Separator();

    // Last Search
    MetricsWindow::RenderMetricText("Summary of the most recently completed search",  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                     "Last Search:");
    MetricsWindow::RenderMetricText("Number of results returned by the most recent search",  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                     "  Results: %zu", search_snapshot.last_results_count_);
    MetricsWindow::RenderMetricText("Time spent in the search phase for the most recent "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                     "search",
                     "  Search Time: %llu ms",
                     search_snapshot.last_search_time_ms_);
    MetricsWindow::RenderMetricText("Time spent in the post-processing phase for the most "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                     "recent search",
                     "  Post-Process Time: %llu ms",
                     search_snapshot.last_postprocess_time_ms_);
    const uint64_t last_total_time = search_snapshot.last_search_time_ms_ +
                               search_snapshot.last_postprocess_time_ms_;
    MetricsWindow::RenderMetricText("Total latency of the most recent search "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                     "(Search Time + Post-Process Time)",
                     "  Total Time: %llu ms", last_total_time);

    if (search_snapshot.last_results_count_ > 0 && last_total_time > 0) {
      const double results_per_ms =
          static_cast<double>(search_snapshot.last_results_count_) /
          static_cast<double>(last_total_time);
      MetricsWindow::RenderMetricText("Throughput of the most recent search in results per "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
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
        MetricsWindow::RenderMetricText("Average number of searches completed per second "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
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

  ImGui::Separator();
  if (ImGui::Button("Reset Search Metrics")) {
    search_worker->ResetMetrics();
  }
}

void MetricsWindow::RenderSystemPerformanceSection(FileIndex &file_index) {
  if (!ImGui::CollapsingHeader("System Performance",
                               ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }
  // FPS
  const ImGuiIO& io = ImGui::GetIO();
  MetricsWindow::RenderMetricText("Frames per second (UI rendering performance)",  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "FPS: %.1f", io.Framerate);

  // Thread count
  const size_t thread_count = file_index.GetSearchThreadPoolCount();
  MetricsWindow::RenderMetricText("Number of threads in search thread pool",  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "Search Threads: %zu", thread_count);
}

void MetricsWindow::RenderResultThroughputMetric(uint64_t total_results_found,
                                                  uint64_t total_time_ms) {
  if (total_time_ms == 0) {
    return;
  }
  const double results_per_sec =
      (static_cast<double>(total_results_found) * 1000.0) / static_cast<double>(total_time_ms);
  MetricsWindow::RenderMetricText("Average result throughput in results per second "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
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
  MetricsWindow::RenderMetricText(  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      "Breakdown of total search time between search and "
      "post-processing phases",
      "Time Distribution:");
  MetricsWindow::RenderMetricText("Percentage of total time spent in the search phase",  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "  Search: %.1f%%", search_percent);
  MetricsWindow::RenderMetricText("Percentage of total time spent in the "  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
                   "post-processing phase",
                   "  Post-Process: %.1f%%", postprocess_percent);
}

} // namespace ui

