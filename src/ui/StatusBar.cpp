/**
 * @file ui/StatusBar.cpp
 * @brief Implementation of status bar rendering component
 */

#include "ui/StatusBar.h"

#include "filters/SizeFilterUtils.h"
#include "filters/TimeFilterUtils.h"
#include "gui/GuiState.h"
#include "index/FileIndex.h"
#include "search/SearchResultUtils.h"
#include "search/SearchWorker.h"
#include "ui/AboutSectionHelpers.h"
#include "ui/IconsFontAwesome.h"
#include "ui/LayoutConstants.h"
#include "ui/Theme.h"
#include "usn/UsnMonitor.h"
#include "utils/StringUtils.h"

#include "imgui.h"

#include <chrono>
#include <string>
#include <string_view>
#include <tuple>

namespace ui {

namespace {
constexpr int kExportNotificationDurationSeconds = 5;
}

// Returns true if export notification/error should still be shown in the status bar.
static bool IsExportNotificationActive(const GuiState& state) {
  const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - state.exportNotificationTime).count();
  return elapsed < kExportNotificationDurationSeconds;
}

// Returns status text for the right group (status bar).
static std::string GetStatusText(const GuiState& state, const SearchWorker& search_worker) {
  if (state.index_build_in_progress) {
    return state.index_build_status_text.empty() ? "Status: Indexing..."
                                                 : state.index_build_status_text;
  }
  if (state.index_build_failed) {
    return state.index_build_status_text.empty() ? "Status: Index build failed"
                                                 : state.index_build_status_text;
  }
  if (search_worker.IsBusy()) {
    return "Status: Searching...";
  }
  if (!state.attributeLoadingFutures.empty() || state.loadingAttributes) {
    return "Status: Loading attributes...";
  }
  if (IsExportNotificationActive(state)) {
    if (!state.exportErrorMessage.empty()) {
      return "Error: " + state.exportErrorMessage;
    }
    if (!state.exportNotification.empty()) {
      return state.exportNotification;
    }
  }
  return "Status: Idle";
}

// Forward declarations for helper functions
static void RenderLeftGroup(const UsnMonitor *monitor, [[maybe_unused]] const FileIndex &file_index,
                           std::string_view monitored_volume);
static void RenderCenterGroup(GuiState &state,
                              const UsnMonitor *monitor,
                              const FileIndex &file_index,
                              const SearchWorker &search_worker);
static void RenderRightGroup(const GuiState &state, const SearchWorker &search_worker,
                             const std::string &status_text, const std::string &memory_text);

void StatusBar::Render(GuiState &state,
                       const SearchWorker &search_worker,
                       const UsnMonitor *monitor,
                       const FileIndex &file_index,
                       std::string_view monitored_volume) {
  // 1 px top border so status bar reads as a distinct footer (LayoutConstants)
  const float border_y = ImGui::GetCursorScreenPos().y;
  const float border_w = ImGui::GetWindowSize().x;
  const ImVec2 border_min(ImGui::GetWindowPos().x, border_y);
  const ImVec2 border_max(ImGui::GetWindowPos().x + border_w, border_y + LayoutConstants::kStatusBarTopBorderHeight);
  ImGui::GetWindowDrawList()->AddRectFilled(border_min, border_max,
                                            ImGui::ColorConvertFloat4ToU32(Theme::Colors::Border));
  ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kStatusBarTopBorderHeight));

  ImGui::Separator();

  // Left group: Version, build type, monitoring status
  RenderLeftGroup(monitor, file_index, monitored_volume);

  // Spacing between groups
  ImGui::SameLine();
  const float left_group_end = ImGui::GetCursorPosX();
  constexpr float kGroupSpacing = 30.0F;
  ImGui::SetCursorPosX(left_group_end + kGroupSpacing);

  // Center group: File counts, search time
  RenderCenterGroup(state, monitor, file_index, search_worker);

  // Right group: Status, memory (aligned to right)
  // Calculate actual widths dynamically to prevent truncation
  ImGui::SameLine();

  // Get the status text that will be displayed
  const std::string status_text = GetStatusText(state, search_worker);

  // Get the memory text that will be displayed (same formatting as Help About section via FormatMemoryOrNa)
  // Cache memory text since memory_bytes_ only updates every 10 seconds
  static size_t last_memory_bytes = 0;
  static std::string cached_memory_text;
  std::string memory_text;
  if (state.memory_bytes_ == last_memory_bytes && !cached_memory_text.empty()) {
    memory_text = cached_memory_text;
  } else {
    memory_text = "Memory: " + FormatMemoryOrNa(state.memory_bytes_);
    cached_memory_text = memory_text;
    last_memory_bytes = state.memory_bytes_;
  }

  // Calculate actual text widths using ImGui
  const ImVec2 status_size = ImGui::CalcTextSize(status_text.c_str());
  const ImVec2 memory_size = ImGui::CalcTextSize(memory_text.c_str());
  // Cache separator size (never changes, calculated once)
  static const float kSeparatorWidth = []() {
    return ImGui::CalcTextSize("|").x;
  }();

  // Get spacing between items (ImGui's ItemInnerSpacing)
  const float item_spacing = ImGui::GetStyle().ItemInnerSpacing.x;

  // Calculate total width: status + separator + memory + spacing between elements
  // Format: [status] [spacing] [|] [spacing] [memory]
  const float right_group_width = status_size.x + item_spacing + kSeparatorWidth +
                                  item_spacing + memory_size.x;
  const float window_width = ImGui::GetWindowWidth();
  // Add extra margin after memory information to prevent it from being too close to window border
  constexpr float kMemoryRightMargin = 10.0F;
  const float right_margin = ImGui::GetStyle().WindowPadding.x + kMemoryRightMargin;

  // Position the right group, ensuring it doesn't go off-screen
  const float right_group_x = window_width - right_group_width - right_margin;
  const float current_x = ImGui::GetCursorPosX();  // NOSONAR(cpp:S6004) - Variable used after if block (line 99)

  // Only position if we have enough space and it's to the right of current position
  // Add a small buffer to ensure we don't overlap
  constexpr float kRightGroupBuffer = 5.0F;
  if (right_group_x > current_x + kRightGroupBuffer && right_group_x > 0) {  // NOSONAR(cpp:S6004) - current_x used in condition
    ImGui::SetCursorPosX(right_group_x);
  } else {
    // If not enough space, add spacing and continue on same line
    // This prevents overlap but may cause wrapping on very narrow windows
    ImGui::SameLine();
    ImGui::Spacing();
  }

  RenderRightGroup(state, search_worker, status_text, memory_text);
}

// Left group: Version, build type, monitoring status
static void RenderLeftGroup(const UsnMonitor *monitor, [[maybe_unused]] const FileIndex &file_index,
                           std::string_view monitored_volume) {
  // Version, build type, and PGO (shared with Help About section via AboutSectionHelpers)
  ImGui::TextDisabled("v-%s", GetAboutAppVersion());
  ImGui::SameLine();
  ImGui::TextDisabled("%s", GetAboutBuildTypeLabel());
  if (const char pgo_mode = GetAboutPgoMode(); pgo_mode != '\0') {
    ImGui::SameLine();
    ImGui::TextDisabled("[%c]", pgo_mode);
    if (ImGui::IsItemHovered() && GetAboutPgoTooltip(pgo_mode) != nullptr) {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted(GetAboutPgoTooltip(pgo_mode));
      ImGui::EndTooltip();
    }
  }

  ImGui::SameLine();
  ImGui::Text("|");
  ImGui::SameLine();

  // Monitoring status with colored dot icon (Phase 2)
  if (monitor != nullptr) {
    ImVec4 dot_color;
    const char* tooltip_text = nullptr;

    if (monitor->IsPopulatingIndex()) {
      dot_color = Theme::Colors::Warning;  // Building index
      tooltip_text = "Building Index...";
    } else {
      // Helper function to get monitoring status (reduces nesting depth)
      auto get_monitoring_status = [&monitor]() -> std::pair<ImVec4, const char*> {
#ifdef _WIN32
        if (!monitor->IsActive()) {
          return {Theme::Colors::Error, "Monitoring Inactive"};
        }
        if (auto metrics = monitor->GetMetricsSnapshot(); metrics.buffers_dropped > 0) {  // Use init-statement pattern
          return {Theme::Colors::Error, "Monitoring Active (buffers dropped)"};
        }
        return {Theme::Colors::Success, "Monitoring Active"};
#else
        // Non-Windows: monitor exists but may not be active (capture used on Windows path only)
        static_cast<void>(monitor);
        return {Theme::Colors::TextDim, "Monitoring (non-Windows)"};
#endif  // _WIN32
      };
      std::tie(dot_color, tooltip_text) = get_monitoring_status();
    }

    // Render colored dot using FontAwesome circle icon
    ImGui::PushStyleColor(ImGuiCol_Text, dot_color);
    ImGui::Text(ICON_FA_CIRCLE);
    ImGui::PopStyleColor();

    // Show tooltip on hover with full status
    if (tooltip_text != nullptr && ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted(tooltip_text);
      if (!monitored_volume.empty()) {
        ImGui::Text("Volume: %.*s", static_cast<int>(monitored_volume.size()), monitored_volume.data());
      }
      ImGui::EndTooltip();
    }
#ifdef _WIN32
    if (!monitored_volume.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("(%.*s)", static_cast<int>(monitored_volume.size()), monitored_volume.data());
    }
#endif  // _WIN32
  } else {
    // No USN monitor: show platform label (shared with Help About section)
    ImGui::TextColored(Theme::Colors::TextDim, "%s", GetAboutPlatformMonitoringLabel());
    if (!monitored_volume.empty()) {
#ifdef _WIN32
      ImGui::SameLine();
      ImGui::TextDisabled("(%.*s)", static_cast<int>(monitored_volume.size()), monitored_volume.data());
#endif  // _WIN32
    }
  }
}

// Renders "Displayed: N" and optional size/filtered label (streaming, filtered, or plain).
static void RenderDisplayedCountAndSize(GuiState& state, const FileIndex& file_index) {
  if (const bool streaming = !state.resultsComplete && state.showingPartialResults; streaming) {
    const size_t count = state.partialResults.size();
    ImGui::TextColored(Theme::Colors::Warning, "Displayed: %zu", count);
    ImGui::SameLine();
    ImGui::TextDisabled("(no filters, no sort yet)");
    return;
  }
  if (state.searchResults.empty()) {
    ImGui::TextDisabled("(no results)");
    return;
  }
  // Ensure total size is computed when we have complete results (may not run if ResultsTable
  // wasn't rendered, e.g. empty state; also gives extra progress when both run).
  if (state.resultsComplete && !state.showingPartialResults) {
    UpdateDisplayedTotalSizeIfNeeded(state, file_index);
  }
  const bool has_time_filter = state.timeFilter != TimeFilter::None;
  const bool has_size_filter = state.sizeFilter != SizeFilter::None;
  if (has_time_filter || has_size_filter) {  // NOSONAR(cpp:S6004) - Both flags needed in condition and displayed_count
    UpdateTimeFilterCacheIfNeeded(state, file_index);
    UpdateSizeFilterCacheIfNeeded(state, file_index);
    const size_t displayed_count = has_size_filter ? state.sizeFilteredCount : state.filteredCount;
    ImGui::Text("Displayed: %zu", displayed_count);
    if (state.displayedTotalSizeValid) {
      ImGui::SameLine();
      ImGui::TextDisabled("(file size: %s)", FormatMemory(state.displayedTotalSizeBytes).c_str());
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(filtered from %zu)", state.searchResults.size());
    return;
  }
  ImGui::Text("Displayed: %zu", state.searchResults.size());
  if (state.displayedTotalSizeValid) {
    ImGui::SameLine();
    ImGui::TextDisabled("(file size: %s)", FormatMemory(state.displayedTotalSizeBytes).c_str());
  }
}

// Renders queue size and/or search time with separators (reduces RenderCenterGroup complexity).
static void RenderQueueAndSearchTime(const UsnMonitor* monitor, const SearchWorker& search_worker) {
  const bool has_queue_info = (monitor != nullptr) && monitor->IsPopulatingIndex() && monitor->GetQueueSize() > 0;
  bool has_search_time = false;
  if (!search_worker.IsBusy()) {
    const auto search_metrics = search_worker.GetMetricsSnapshot();
    has_search_time = (search_metrics.total_searches_ > 0);
  }
  if (!has_queue_info && !has_search_time) {
    return;
  }
  ImGui::SameLine();
  ImGui::Text("|");
  ImGui::SameLine();
  if (has_queue_info) {
    const size_t queue_size = monitor->GetQueueSize();
    ImGui::Text("Queue: %zu", queue_size);
    if (has_search_time) {
      ImGui::SameLine();
      ImGui::Text("|");
      ImGui::SameLine();
    }
  }
  if (has_search_time) {
    const auto search_metrics = search_worker.GetMetricsSnapshot();
    const uint64_t last_total_time = search_metrics.last_search_time_ms_ +
                                     search_metrics.last_postprocess_time_ms_;
    constexpr uint64_t kMillisecondsPerSecond = 1000;
    if (last_total_time < kMillisecondsPerSecond) {
      ImGui::Text("Search: %llums", static_cast<unsigned long long>(last_total_time));
    } else {
      constexpr double kSecondsPerMillisecond = 0.001;
      ImGui::Text("Search: %.2fs", static_cast<double>(last_total_time) * kSecondsPerMillisecond);
    }
  }
}

// Renders index build timing (current elapsed if in progress, last duration if completed).
static void RenderIndexBuildTiming(const GuiState& state) {
  if (!state.index_build_has_timing) {
    return;
  }

  uint64_t elapsed_ms = 0U;

  if (state.index_build_in_progress) {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - state.index_build_start_time);
    elapsed_ms = static_cast<uint64_t>(elapsed.count());
  } else if (state.index_build_last_duration_ms > 0U) {
    elapsed_ms = state.index_build_last_duration_ms;
  } else {
    return;
  }

  constexpr uint64_t kMillisecondsPerSecond = 1000;
  if (elapsed_ms < kMillisecondsPerSecond) {
    ImGui::Text("Index: %llums", static_cast<unsigned long long>(elapsed_ms));
  } else {
    constexpr double kSecondsPerMillisecond = 0.001;
    ImGui::Text("Index: %.2fs", static_cast<double>(elapsed_ms) * kSecondsPerMillisecond);
  }
}

// Center group: File counts, search time
static void RenderCenterGroup(GuiState& state,
                              const UsnMonitor* monitor,
                              const FileIndex& file_index,
                              const SearchWorker& search_worker) {
  const size_t total_items = (monitor != nullptr) ? monitor->GetIndexedFileCount() : file_index.Size();
  ImGui::Text("Total: %zu", total_items);
  ImGui::SameLine();
  ImGui::Text("|");
  ImGui::SameLine();

  RenderDisplayedCountAndSize(state, file_index);

  // Index build timing (initial index, manual crawl, auto-crawl, or periodic recrawl)
  if (state.index_build_has_timing &&
      (state.index_build_in_progress || state.index_build_last_duration_ms > 0U)) {
    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();
    RenderIndexBuildTiming(state);
  }

  RenderQueueAndSearchTime(monitor, search_worker);
}

// Right group: Status, memory
static void RenderRightGroup(const GuiState &state, const SearchWorker &search_worker,
                             const std::string &status_text, const std::string &memory_text) {
  // Use pre-built status text (passed from Render to avoid duplicate building).
  // Determine color from status type, then single render call (avoids bugprone-branch-clone).
  const ImVec4* status_color = nullptr;
  if (const bool is_loading_attributes =
          !state.attributeLoadingFutures.empty() || state.loadingAttributes;
      state.index_build_in_progress || is_loading_attributes) {
    status_color = &Theme::Colors::Warning;
  } else if (state.index_build_failed) {
    status_color = &Theme::Colors::Error;
  } else if (search_worker.IsBusy()) {
    status_color = &Theme::Colors::Accent;
  } else if (IsExportNotificationActive(state)) {
    if (!state.exportErrorMessage.empty()) {
      status_color = &Theme::Colors::Error;
    } else if (!state.exportNotification.empty()) {
      status_color = &Theme::Colors::Success;
    }
  }
  if (status_color != nullptr) {
    ImGui::TextColored(*status_color, "%s", status_text.c_str());
  } else {
    ImGui::Text("%s", status_text.c_str());
  }
  ImGui::SameLine();
  ImGui::Text("|");
  ImGui::SameLine();

  // Use pre-built memory text (passed from Render to avoid duplicate formatting)
  if (state.memory_bytes_ > 0) {
    ImGui::Text("%s", memory_text.c_str());
  } else {
    ImGui::TextDisabled("Memory: N/A");
  }
}

} // namespace ui
