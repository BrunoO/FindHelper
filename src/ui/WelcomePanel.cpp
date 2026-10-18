/**
 * @file ui/WelcomePanel.cpp
 * @brief Implementation of welcome panel rendering component
 */

#include "ui/WelcomePanel.h"

#include "core/Settings.h"
#include "filters/SizeFilter.h"
#include "filters/TimeFilter.h"
#include "gui/GuiState.h"
#include "gui/ImGuiUtils.h"
#include "gui/UIActions.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "search/SearchHistory.h"
#include "ui/IconsFontAwesome.h"
#include "ui/LayoutConstants.h"
#include "ui/SearchHistoryCard.h"
#include "ui/Theme.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace ui {

namespace {

constexpr float kPanelHeaderFontScale = 1.15F;
constexpr float kHistoryRowSubtitleFontScale = 0.88F;
constexpr float kHistoryTooltipWrapWidthChars = 40.0F;

void RenderWelcomePanelTitle(std::string_view title, std::string_view subtitle = {}) {
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextStrong);
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kPanelHeaderFontScale);
  ImGui::TextUnformatted(title.data(), title.data() + title.size());
  ImGui::PopFont();
  ImGui::PopStyleColor();

  if (!subtitle.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDim);
    ImGui::TextUnformatted(subtitle.data(), subtitle.data() + subtitle.size());
    ImGui::PopStyleColor();
  }
  ImGui::Spacing();
}

// Vertical Y for history-row icon buttons
[[nodiscard]] float HistoryRowActionsCursorY(float row_start_y) {
  const ImGuiStyle& style = ImGui::GetStyle();
  const float line_h = ImGui::GetTextLineHeight();
  const float frame_h = ImGui::GetFrameHeight();
  return row_start_y + style.FramePadding.y + ((line_h - frame_h) * 0.5F);
}

void HistoryRowAdvanceLayoutAfterOverlays(float row_start_x, float cursor_y_after_selectable) {
  ImGui::SetCursorPos(ImVec2(row_start_x, cursor_y_after_selectable));
  ImGui::Dummy(ImVec2(0.0F, 0.0F));
}

struct HistoryRowActionsConfig {
  const char* primary_icon;
  const char* primary_tooltip;
  const char* primary_menu_label;
  bool is_pin_action;
};

void ApplyHistoryRowPrimaryAction(const SearchHistoryEntry& entry,
                                  const HistoryRowActionsConfig& config, std::int64_t now_ms,
                                  AppSettings& settings, GuiState& state, UIActions* actions) {
  // Orchestration lives in search/SearchHistory.cpp (shared verbatim with the
  // Quick Access card rows; keeps the headline-before-toggle invariant in one place).
  ToggleHistoryPinWithFeedback(entry, config.is_pin_action, settings, now_ms, actions, state);
}

/**
 * @brief Render a Recent History item in a timeline list view.
 */
void RenderRecentHistoryRow(const SearchHistoryEntry* entry, GuiState& state,
                            AppSettings& settings, UIActions* actions, std::int64_t now_ms) {
  constexpr HistoryRowActionsConfig kRecentActions{
    ICON_FA_PIN,
    "Pin to Quick Access",
    ICON_FA_PIN " Pin to Quick Access",
    true,
  };

  const HistoryEntryDisplayInfo info = BuildHistoryEntryDisplayInfo(*entry);
  const std::string timestamp = FormatRelativeTime(entry->last_used_at_unix_ms, now_ms);

  ImGui::PushID(entry->id.c_str());

  const bool selected = (state.history.selected_id == entry->id);
  const float row_height = ImGui::GetTextLineHeightWithSpacing() * 2.1F;
  const float row_start_y = ImGui::GetCursorPosY();
  const float row_start_x = ImGui::GetCursorPosX();
  const ImVec2 row_screen_pos = ImGui::GetCursorScreenPos();
  const float avail_w = ImGui::GetContentRegionAvail().x;

  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float timestamp_w = ImGui::CalcTextSize(timestamp.c_str()).x;
  const float button_w =
    MaxSmallIconButtonWidth({kRecentActions.primary_icon, ICON_FA_PEN, ICON_FA_TRASH});
  const float total_buttons_w = (button_w * 3.0F) + (spacing * 2.0F);
  const float reserved_right_w = (std::max)(timestamp_w, total_buttons_w) + spacing + spacing;

  const ImVec2 mouse_pos = ImGui::GetIO().MousePos;
  const bool in_action_zone = mouse_pos.x >= (row_screen_pos.x + avail_w - reserved_right_w);

  if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowOverlap,
                        ImVec2(avail_w, row_height)) &&
      !in_action_zone && actions != nullptr) {
    state.history.selected_id = entry->id;
    ApplyAndRunHistoryEntry(*entry, state, actions);
  }
  const float cursor_y_after_selectable = ImGui::GetCursorPosY();
  ImGui::OpenPopupOnItemClick("##ctx", ImGuiPopupFlags_MouseButtonRight);

  if (selected) {
    constexpr float kAccentBarW = 3.0F;
    const ImU32 accent_col = ImGui::ColorConvertFloat4ToU32(Theme::Colors::Accent);
    ImGui::GetWindowDrawList()->AddRectFilled(
      row_screen_pos, ImVec2(row_screen_pos.x + kAccentBarW, row_screen_pos.y + row_height),
      accent_col);
  }

  ImGui::SetCursorPosY(row_start_y + ImGui::GetStyle().FramePadding.y);
  ImGui::Indent(ImGui::GetStyle().ItemSpacing.x);

  const bool hovered = ImGui::IsMouseHoveringRect(
    row_screen_pos, ImVec2(row_screen_pos.x + avail_w, row_screen_pos.y + row_height));
  const bool show_actions = hovered || state.history.pending_rename_id == entry->id ||
                            state.history.pending_delete_id == entry->id;

  if (hovered && !in_action_zone) {
    const std::string tooltip = BuildHistoryEntryTooltip(*entry);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * kHistoryTooltipWrapWidthChars);
    ImGui::TextUnformatted(tooltip.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }

  // History icon prefix for recent searches timeline
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDim);
  ImGui::TextUnformatted(ICON_FA_HISTORY);
  ImGui::PopStyleColor();
  ImGui::SameLine(0.0F, 6.0F);

  // Line 1: Headline + Timestamp / Actions
  const float headline_max_w =
    ImGui::GetWindowContentRegionMax().x - reserved_right_w - ImGui::GetCursorPosX();
  TextEllipsisSingleLine(info.headline.c_str(), info.headline.c_str() + info.headline.size(),
                         headline_max_w);

  if (!show_actions) {
    ImGui::SameLine();
    const float timestamp_x = ImGui::GetWindowContentRegionMax().x - timestamp_w - spacing;
    ImGui::SetCursorPosX(timestamp_x);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDim);
    ImGui::TextUnformatted(timestamp.c_str());
    ImGui::PopStyleColor();
  }

  // Line 2: Subtitle
  ImGui::SetCursorPosY(row_start_y + ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y +
                       2.0F);
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDim);
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kHistoryRowSubtitleFontScale);
  const float subtitle_max_w =
    ImGui::GetWindowContentRegionMax().x - reserved_right_w - ImGui::GetCursorPosX();
  if (info.subtitle.empty()) {
    ImGui::TextUnformatted("\xe2\x80\x94");
  } else {
    TextEllipsisSingleLine(info.subtitle.c_str(), info.subtitle.c_str() + info.subtitle.size(),
                           subtitle_max_w);
  }
  ImGui::PopFont();
  ImGui::PopStyleColor();
  ImGui::Unindent(ImGui::GetStyle().ItemSpacing.x);

  if (show_actions) {
    ImGui::SetCursorPosY(HistoryRowActionsCursorY(row_start_y));
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - total_buttons_w - spacing);

    if (RenderIconButtonWithTooltip(kRecentActions.primary_icon, kRecentActions.primary_tooltip)) {
      ApplyHistoryRowPrimaryAction(*entry, kRecentActions, now_ms, settings, state, actions);
    }
    ImGui::SameLine(0.0F, spacing);
    if (RenderIconButtonWithTooltip(ICON_FA_PEN, "Rename")) {
      state.history.pending_rename_id = entry->id;
    }
    ImGui::SameLine(0.0F, spacing);
    constexpr float kDeleteButtonHoverAlpha = 0.75F;
    constexpr float kDeleteButtonActiveAlpha = 1.0F;
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(Theme::Colors::Error.x, Theme::Colors::Error.y,
                                 Theme::Colors::Error.z, kDeleteButtonHoverAlpha));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(Theme::Colors::Error.x, Theme::Colors::Error.y,
                                 Theme::Colors::Error.z, kDeleteButtonActiveAlpha));
    if (RenderIconButtonWithTooltip(ICON_FA_TRASH, "Delete")) {
      state.history.pending_delete_id = entry->id;
    }
    ImGui::PopStyleColor(2);
  }

  if (ImGui::BeginPopup("##ctx")) {
    if (ImGui::MenuItem(kRecentActions.primary_menu_label)) {
      ApplyHistoryRowPrimaryAction(*entry, kRecentActions, now_ms, settings, state, actions);
    }
    if (ImGui::MenuItem(ICON_FA_PEN " Rename")) {
      state.history.pending_rename_id = entry->id;
    }
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_TRASH " Delete")) {
      state.history.pending_delete_id = entry->id;
    }
    ImGui::EndPopup();
  }

  HistoryRowAdvanceLayoutAfterOverlays(row_start_x, cursor_y_after_selectable);
  ImGui::PopID();
}

// =========================================================================
// SECTION 1: PINNED SEARCHES (QUICK ACCESS CARDS)
// =========================================================================
void RenderPinnedSection(const std::vector<const SearchHistoryEntry*>& view,
                         size_t pinned_count,
                         GuiState& state,
                         AppSettings& settings,
                         UIActions* actions,
                         std::int64_t now_ms) {
  RenderWelcomePanelTitle(
    "Pinned Searches (" + std::to_string(pinned_count) + ")",
    "Quick access templates — click any card to run, or pin recent searches");

  if (pinned_count == 0) {
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDim);
    ImGui::TextUnformatted("No pinned searches. Pin recent searches or restore default templates.");
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0F, 12.0F);
    if (ImGui::SmallButton("Restore Default Templates")) {
      SeedDefaultPinnedSearchesIfNeeded(settings);
      if (actions != nullptr) {
        (void)actions->PersistSettings();
      }
    }
    ImGui::Spacing();
  } else {
    constexpr float kMinCardW = 230.0F;
    constexpr float kCardHeight = 62.0F;
    constexpr float kGridSpacing = 10.0F;

    const float grid_avail_w = ImGui::GetContentRegionAvail().x;
    const int cols = (std::max)(1, (std::min)(4, static_cast<int>(grid_avail_w / kMinCardW)));
    const float card_w =
      (grid_avail_w - (kGridSpacing * static_cast<float>(cols - 1))) / static_cast<float>(cols);

    const SearchHistoryCardConfig card_config{card_w, kCardHeight, true};

    const ImVec2 grid_start_pos = ImGui::GetCursorPos();
    int card_idx = 0;
    for (const SearchHistoryEntry* entry : view) {
      if (!entry->is_pinned) {
        continue;
      }

      const int col = card_idx % cols;
      const int row = card_idx / cols;
      const float card_x = grid_start_pos.x + (static_cast<float>(col) * (card_w + kGridSpacing));
      const float card_y =
        grid_start_pos.y + (static_cast<float>(row) * (kCardHeight + kGridSpacing));

      ImGui::SetCursorPos(ImVec2(card_x, card_y));
      SearchHistoryCard::Render(*entry, state, settings, actions, now_ms, card_config);

      card_idx++;
    }

    if (card_idx > 0) {
      const int total_rows = (card_idx + cols - 1) / cols;
      ImGui::SetCursorPos(
        ImVec2(grid_start_pos.x,
               grid_start_pos.y + (static_cast<float>(total_rows) * (kCardHeight + kGridSpacing))));
    }
    ImGui::Spacing();
  }
}

void RenderRecentSection(const std::vector<const SearchHistoryEntry*>& view,
                         size_t recent_count,
                         GuiState& state,
                         AppSettings& settings,
                         UIActions* actions,
                         std::int64_t now_ms) {
  RenderWelcomePanelTitle(
    "Recent Searches (" + std::to_string(recent_count) + ")",
    "Searches you run automatically appear here in your activity timeline");

  if (recent_count == 0) {
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDisabled);
    ImGui::TextUnformatted("No recent searches yet. Run a search to log it in your timeline.");
    ImGui::PopStyleColor();
  } else {
    for (const SearchHistoryEntry* entry : view) {
      if (!entry->is_pinned) {
        RenderRecentHistoryRow(entry, state, settings, actions, now_ms);
      }
    }
  }
}

/**
 * @brief Render the Welcome Search Dashboard containing Pinned & Recent sections.
 */
void RenderSearchDashboard(GuiState& state, AppSettings& settings, UIActions* actions,
                           float panel_width, float panel_height, bool is_index_building,
                           size_t indexed_file_count) {
  ImGui::BeginChild("empty_state_dashboard", ImVec2(panel_width, panel_height),
                    ImGuiChildFlags_Borders);

  const auto view = GetSortedHistoryView(settings);

  // Top Dashboard Toolbar: Title + Show All preset
  const bool can_show_all = actions != nullptr && !is_index_building && indexed_file_count > 0U;
  const float avail_w = ImGui::GetContentRegionAvail().x;

  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextStrong);
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kPanelHeaderFontScale);
  ImGui::TextUnformatted("Search Dashboard");
  ImGui::PopFont();
  ImGui::PopStyleColor();

  ImGui::SameLine();
  const float btn_w = ImGui::CalcTextSize("Show All Indexed Items").x + 16.0F;
  ImGui::SetCursorPosX(avail_w - btn_w);
  if (!can_show_all) {
    ImGui::BeginDisabled();
  }
  if (ImGui::SmallButton("Show All Indexed Items") && actions != nullptr) {
    state.ApplyShowAllPreset();
    actions->TriggerManualSearch(state);
  }
  if (!can_show_all) {
    ImGui::EndDisabled();
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Validate selection each frame
  if (!state.history.selected_id.empty() &&
      FindHistoryEntryById(settings.searchHistory, state.history.selected_id) == nullptr) {
    state.history.selected_id.clear();
  }

  const auto now_ms =
    static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count());

  if (is_index_building) {
    ImGui::BeginDisabled();
  }

  // Partition entries
  const auto pinned_count = static_cast<size_t>(
    std::count_if(view.begin(), view.end(), [](const SearchHistoryEntry* e) { return e->is_pinned; }));
  const size_t recent_count = view.size() - pinned_count;

  if (const bool is_wide = panel_width >= LayoutConstants::kWelcomeDashboardWideBreakpoint;
      is_wide) {
    const float gutter = LayoutConstants::kWelcomeDashboardSplitGutter;
    const float left_w = (panel_width - gutter) * LayoutConstants::kWelcomeDashboardLeftWingRatio;
    const float right_w = panel_width - left_w - gutter;

    ImGui::BeginChild("dashboard_pinned_wing", ImVec2(left_w, 0.0F), ImGuiChildFlags_None);
    RenderPinnedSection(view, pinned_count, state, settings, actions, now_ms);
    ImGui::EndChild();

    ImGui::SameLine(0.0F, gutter * 0.5F);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0.0F, gutter * 0.5F);

    ImGui::BeginChild("dashboard_recent_wing", ImVec2(right_w, 0.0F), ImGuiChildFlags_None);
    RenderRecentSection(view, recent_count, state, settings, actions, now_ms);
    ImGui::EndChild();
  } else {
    RenderPinnedSection(view, pinned_count, state, settings, actions, now_ms);
    ImGui::Separator();
    ImGui::Spacing();
    RenderRecentSection(view, recent_count, state, settings, actions, now_ms);
  }

  if (is_index_building) {
    ImGui::EndDisabled();
  }

  ImGui::EndChild();
}

}  // anonymous namespace

void WelcomePanel::Render(GuiState& state, UIActions* actions, AppSettings& settings,
                          bool is_index_building) {
  // Show placeholder message with prominent vertical space
  AddVerticalSpacing(LayoutConstants::kWelcomePanelHeroSpacingCount);

  const float window_width = ImGui::GetContentRegionAvail().x;

  // Show different message if index is empty
  const size_t indexed_file_count = (actions != nullptr) ? actions->GetIndexedFileCount() : 0;
  if (indexed_file_count == 0) {
    const char* getting_started_title = "Getting Started";
    const float title_width = ImGui::CalcTextSize(getting_started_title).x;
    ImGui::SetCursorPosX((window_width - title_width) * 0.5F);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::Warning);
    ImGui::Text("%s", getting_started_title);
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Spacing();

    const char* instructions =
      "No files indexed yet. Go to Settings > Index Configuration\nto select a folder to index.";

    const std::string instructions_str(instructions);
    if (const size_t newline_pos = instructions_str.find('\n'); newline_pos != std::string::npos) {
      const std::string line1 = instructions_str.substr(0, newline_pos);
      const std::string line2 = instructions_str.substr(newline_pos + 1);

      const float line1_width = ImGui::CalcTextSize(line1.c_str()).x;
      ImGui::SetCursorPosX((window_width - line1_width) * 0.5F);
      ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDim);
      ImGui::Text("%s", line1.c_str());

      const float line2_width = ImGui::CalcTextSize(line2.c_str()).x;
      ImGui::SetCursorPosX((window_width - line2_width) * 0.5F);
      ImGui::Text("%s", line2.c_str());
      ImGui::PopStyleColor();
    } else {
      const float text_width_val = ImGui::CalcTextSize(instructions).x;
      ImGui::SetCursorPosX((window_width - text_width_val) * 0.5F);
      ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDim);
      ImGui::Text("%s", instructions);
      ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();

    const char* hint = "Tip: Use the Settings button to open Settings";
    const float hint_width = ImGui::CalcTextSize(hint).x;
    ImGui::SetCursorPosX((window_width - hint_width) * 0.5F);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDisabled);
    ImGui::Text("%s", hint);
    ImGui::PopStyleColor();

    return;
  }

  // Main status message
  std::string main_message;
  ImVec4 main_color;
  bool use_larger_font = false;

  if (is_index_building) {
    main_message = "Please wait... indexing in progress";

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - state.index_build.start_time);
    const double elapsed_seconds = static_cast<double>(elapsed.count()) / 1000.0;

    main_message += " (";
    std::array<char, 32> time_buf{};
    if (const int snprintf_result =
          snprintf(time_buf.data(), time_buf.size(), "%.1fs", elapsed_seconds);
        snprintf_result > 0) {
      main_message += time_buf.data();
    }

    if (indexed_file_count > 0U) {
      main_message += ' ';
      main_message += FormatNumberWithSeparators(indexed_file_count);
      main_message += " items indexed";
    }
    main_message += ')';
    main_color = Theme::Colors::Accent;
    use_larger_font = true;
  } else if (!state.search_pipeline.pattern_error.empty()) {
    main_message = state.search_pipeline.pattern_error;
    main_color = Theme::Colors::Warning;
  } else {
    main_message = "No results to display";
    main_color = Theme::Colors::TextDim;
  }

  if (use_larger_font) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.0F);
  }

  const float main_width = ImGui::CalcTextSize(main_message.c_str()).x;
  ImGui::SetCursorPosX((window_width - main_width) * 0.5F);
  ImGui::PushStyleColor(ImGuiCol_Text, main_color);
  ImGui::TextUnformatted(main_message.c_str());
  ImGui::PopStyleColor();

  if (use_larger_font) {
    ImGui::PopFont();
  }

  ImGui::Spacing();
  ImGui::Separator();
  AddVerticalSpacing(LayoutConstants::kWelcomePanelPanelSpacingCount);

  // Full-width Search Dashboard: top = Pinned Quick Access Cards, bottom = Recent Searches Timeline
  const float panel_height = ImGui::GetContentRegionAvail().y;
  RenderSearchDashboard(state, settings, actions, window_width, panel_height, is_index_building,
                        indexed_file_count);
}

}  // namespace ui
