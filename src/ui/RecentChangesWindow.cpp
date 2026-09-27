/**
 * @file ui/RecentChangesWindow.cpp
 * @brief Implementation of recent changes window rendering component
 */

#include "ui/RecentChangesWindow.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "imgui.h"
#include "ui/CenteredToolWindow.h"
#include "ui/IconsFontAwesome.h"
#include "ui/UiStyleGuards.h"
#include "usn/UsnActivityTracker.h"
#include "usn/UsnMonitor.h"

namespace ui {

namespace {

constexpr float kRecentChangesDefaultWidth = 750.0F;
constexpr float kRecentChangesDefaultHeight = 500.0F;

// USN Journal monitoring is Windows-only: the state struct and rendering helpers are
// referenced exclusively from the _WIN32 body of RecentChangesWindow::Render and are
// compiled only there, so non-Windows builds see no unused definitions.
#ifdef _WIN32

struct PersistentWindowState {
  bool show_created = true;
  bool show_deleted = true;
  bool show_renamed = true;
  bool show_modified = true;
  bool paused = false;
  std::array<char, 128> filter_buffer{};  // Null-terminated ImGui InputText buffer
  std::vector<UsnChangeEntry> paused_snapshot;
};

std::string FormatTimestamp(const std::chrono::system_clock::time_point& tp) {
  const auto time_t_val = std::chrono::system_clock::to_time_t(tp);
  std::tm tm_buf{};
#ifdef _WIN32
  localtime_s(&tm_buf, &time_t_val);
#else
  localtime_r(&time_t_val, &tm_buf);
#endif  // _WIN32

  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      tp.time_since_epoch()) % 1000;

  std::ostringstream oss;
  oss << std::setfill('0')
      << std::setw(2) << tm_buf.tm_hour << ":"
      << std::setw(2) << tm_buf.tm_min << ":"
      << std::setw(2) << tm_buf.tm_sec << "."
      << std::setw(3) << ms.count();
  return oss.str();
}

const char* GetChangeTypeName(UsnChangeType type) {
  switch (type) {
    case UsnChangeType::Created:
      return "CREATED";
    case UsnChangeType::Deleted:
      return "DELETED";
    case UsnChangeType::Renamed:
      return "RENAMED";
    case UsnChangeType::Modified:
      return "MODIFIED";
    default:
      return "OTHER";
  }
}

ImVec4 GetChangeTypeColor(UsnChangeType type) {
  switch (type) {
    case UsnChangeType::Created:
      return ImVec4(0.2F, 0.8F, 0.2F, 1.0F);  // Green
    case UsnChangeType::Deleted:
      return ImVec4(0.9F, 0.3F, 0.3F, 1.0F);  // Red
    case UsnChangeType::Renamed:
      return ImVec4(0.9F, 0.7F, 0.1F, 1.0F);  // Yellow/Orange
    case UsnChangeType::Modified:
      return ImVec4(0.3F, 0.6F, 0.9F, 1.0F);  // Blue
    default:
      return ImVec4(0.7F, 0.7F, 0.7F, 1.0F);  // Gray
  }
}

bool MatchesFilter(const UsnChangeEntry& entry, const PersistentWindowState& state) {
  switch (entry.change_type) {
    case UsnChangeType::Created:
      if (!state.show_created) return false;
      break;
    case UsnChangeType::Deleted:
      if (!state.show_deleted) return false;
      break;
    case UsnChangeType::Renamed:
      if (!state.show_renamed) return false;
      break;
    case UsnChangeType::Modified:
      if (!state.show_modified) return false;
      break;
    default:
      break;
  }

  if (state.filter_buffer[0] != '\0') {
    const std::string filter_str(state.filter_buffer.data());
    if (const auto it_path = std::search(
            entry.path.begin(), entry.path.end(),
            filter_str.begin(), filter_str.end(),
            [](char a, char b) { return ::tolower(a) == ::tolower(b); });
        it_path != entry.path.end()) {
      return true;
    }
    if (!entry.old_path.empty()) {
      if (const auto it_old = std::search(
              entry.old_path.begin(), entry.old_path.end(),
              filter_str.begin(), filter_str.end(),
              [](char a, char b) { return ::tolower(a) == ::tolower(b); });
          it_old != entry.old_path.end()) {
        return true;
      }
    }
    return false;
  }

  return true;
}

#endif  // _WIN32

}  // namespace

void RecentChangesWindow::Render(bool* p_open, [[maybe_unused]] UsnMonitor* monitor) {
  if (p_open == nullptr || !*p_open) {
    return;
  }

  detail::SetupCenteredToolWindow(kRecentChangesDefaultWidth, kRecentChangesDefaultHeight);

  // Same guard pattern as SettingsWindow: the WindowGuard owns the ImGui::Begin/End pair
  // and stays alive for the whole render body, so the body renders inside the
  // ShowContent() block instead of early-returning past it (early returns below still
  // unwind through the guard destructor, keeping Begin/End balanced).
  // Init-statement form keeps the guard scoped to exactly the window body (cpp:S6004).
  if (const detail::WindowGuard window_guard(
          ICON_FA_HISTORY " Recent Drive Activity (USN Journal)###RecentChangesWindow",
          p_open,
          ImGuiWindowFlags_NoCollapse);
      window_guard.ShowContent()) {
#ifndef _WIN32
    ImGui::TextUnformatted("USN Journal real-time change tracking is only available on Windows.");
    detail::RenderToolWindowCloseButton(p_open);
#else
    // Function-local static: state persists across frames exactly like the former
    // namespace-scope global, without introducing mutable global storage (cpp:S5421).
    static PersistentWindowState s_window_state;

    if (monitor == nullptr || !monitor->IsActive()) {
      ImGui::TextUnformatted("USN Journal monitoring is currently inactive.");
      detail::RenderToolWindowCloseButton(p_open);
      return;
    }

    // Header controls
    ImGui::Checkbox("Created", &s_window_state.show_created);
    ImGui::SameLine();
    ImGui::Checkbox("Deleted", &s_window_state.show_deleted);
    ImGui::SameLine();
    ImGui::Checkbox("Renamed", &s_window_state.show_renamed);
    ImGui::SameLine();
    ImGui::Checkbox("Modified", &s_window_state.show_modified);

    ImGui::SameLine();
    if (s_window_state.paused) {
      if (ImGui::Button(ICON_FA_PLAY " Resume")) {
        s_window_state.paused = false;
        s_window_state.paused_snapshot.clear();
      }
    } else {
      if (ImGui::Button(ICON_FA_PAUSE " Pause")) {
        s_window_state.paused = true;
        s_window_state.paused_snapshot = monitor->GetRecentChangesSnapshot();
      }
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TRASH " Clear")) {
      monitor->ClearRecentChanges();
      if (s_window_state.paused) {
        s_window_state.paused_snapshot.clear();
      }
    }

    ImGui::SetNextItemWidth(250.0F);
    ImGui::InputTextWithHint("##PathFilter", "Filter by path...", s_window_state.filter_buffer.data(),
                             s_window_state.filter_buffer.size());

    ImGui::Separator();

    // Get data snapshot
    std::vector<UsnChangeEntry> entries;
    if (s_window_state.paused) {
      entries = s_window_state.paused_snapshot;
    } else {
      entries = monitor->GetRecentChangesSnapshot();
    }

    // Filter entries
    std::vector<const UsnChangeEntry*> filtered;
    filtered.reserve(entries.size());
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
      if (MatchesFilter(*it, s_window_state)) {
        filtered.push_back(&(*it));
      }
    }

    // Table setup
    constexpr ImGuiTableFlags table_flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

    const float footer_height = ImGui::GetFrameHeightWithSpacing() * 2.0F;
    const float table_height = ImGui::GetContentRegionAvail().y - footer_height;

    if (ImGui::BeginTable("##RecentChangesTable", 3, table_flags, ImVec2(0.0F, table_height))) {
      ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 90.0F);
      ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0F);
      ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();

      ImGuiListClipper clipper;
      clipper.Begin(static_cast<int>(filtered.size()));

      while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
          const auto* entry = filtered.at(i);
          ImGui::TableNextRow();

          // Time
          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted(FormatTimestamp(entry->timestamp).c_str());

          // Type
          ImGui::TableSetColumnIndex(1);
          ImGui::PushStyleColor(ImGuiCol_Text, GetChangeTypeColor(entry->change_type));
          ImGui::TextUnformatted(GetChangeTypeName(entry->change_type));
          ImGui::PopStyleColor();

          // Path
          ImGui::TableSetColumnIndex(2);
          std::string display_path = entry->path;
          if (entry->change_type == UsnChangeType::Renamed && !entry->old_path.empty()) {
            display_path += " (was: " + entry->old_path + ")";
          }
          ImGui::TextUnformatted(display_path.c_str());

          // Context menu
          if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Copy Path")) {
              ImGui::SetClipboardText(entry->path.c_str());
            }
            if (!entry->old_path.empty() && ImGui::MenuItem("Copy Old Path")) {
              ImGui::SetClipboardText(entry->old_path.c_str());
            }
            if (ImGui::MenuItem("Copy Line")) {
              std::string line = FormatTimestamp(entry->timestamp) + "\t" +
                                 GetChangeTypeName(entry->change_type) + "\t" + display_path;
              ImGui::SetClipboardText(line.c_str());
            }
            ImGui::EndPopup();
          }
        }
      }

      ImGui::EndTable();
    }

    detail::RenderToolWindowCloseButton(p_open);
#endif  // _WIN32
  }
}

}  // namespace ui
