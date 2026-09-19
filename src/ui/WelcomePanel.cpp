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
  constexpr float kHistoryRowSubtitleFontScale = 0.9F;
  constexpr float kHistoryTooltipWrapWidthChars = 40.0F;
  constexpr const char* kHistoryPanelHint =
      "Searches you run show up here. Click one to run it again.";
  constexpr const char* kHistoryPanelEmpty =
      "Run a search and it will appear here. Pin one to keep it.";

  [[nodiscard]] std::string FormatTitleWithCount(std::string_view label, size_t count) {
    return std::string(label) + " (" + std::to_string(count) + ")";
  }

  void RenderWelcomePanelTitle(std::string_view title) {
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextStrong);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kPanelHeaderFontScale);
    ImGui::TextUnformatted(title.data(), title.data() + title.size());
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::Spacing();
  }

  void RenderWelcomeSectionTitle(std::string_view label, size_t count,
                                 bool with_separator = true) {
    const std::string text = FormatTitleWithCount(label, count);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDim);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    if (with_separator) {
      ImGui::Separator();
    } else {
      ImGui::Spacing();
    }
  }

  // Single-line text at cursor with ellipsis when wider than max_width (ImGui has no public TextEllipsis).
  void TextEllipsisSingleLine(const char* text_begin, const char* text_end, float max_width) {
    const float line_h = ImGui::GetTextLineHeight();
    const ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float layout_w = max_width > 0.0F ? max_width : 1.0F;
    if (window == nullptr) {
      ImGui::Dummy(ImVec2(layout_w, line_h));
      return;
    }
    const ImVec2 pos_min = ImGui::GetCursorScreenPos();
    const ImVec2 pos_max(pos_min.x + layout_w, pos_min.y + line_h);
    ImGui::RenderTextEllipsis(window->DrawList, pos_min, pos_max, pos_max.x, text_begin, text_end, nullptr);
    ImGui::Dummy(ImVec2(layout_w, line_h));
  }

  // Render a compact icon button with a hover tooltip. Does not call SameLine — caller spaces
  // multiple buttons explicitly so the strip stays one aligned row without trailing SameLine bugs.
  [[nodiscard]] bool RenderIconButtonWithTooltip(const char* icon_text,
                                                 std::string_view tooltip) {
    const bool clicked = ImGui::SmallButton(icon_text);
    if (ImGui::IsItemHovered() && !tooltip.empty()) {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted(tooltip.data(), tooltip.data() + tooltip.size());
      ImGui::EndTooltip();
    }
    return clicked;
  }

  // Width of a SmallButton for the widest of the given Font Awesome icon labels (frame padding
  // included). Used so reserved layout matches actual buttons and the timestamp column.
  [[nodiscard]] float MaxSmallIconButtonWidth(std::initializer_list<const char*> icons) {
    const ImGuiStyle& style = ImGui::GetStyle();
    float widest = 0.0F;
    for (const char* icon : icons) {
      widest = (std::max)(widest, ImGui::CalcTextSize(icon).x + (style.FramePadding.x * 2.0F));
    }
    return widest;
  }

  // Vertical Y (window pos) for history-row icon buttons: same band as line-1 headline/timestamp,
  // not vertically centered across the two-line row (which overlapped the timestamp column).
  [[nodiscard]] float HistoryRowActionsCursorY(float row_start_y) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float line_h = ImGui::GetTextLineHeight();
    const float frame_h = ImGui::GetFrameHeight();
    return row_start_y + style.FramePadding.y + ((line_h - frame_h) * 0.5F);
  }

  // ImGui 1.89+: moving the cursor past CursorMaxPos must be followed by an item (Dummy) so
  // parent content bounds update (#5548). Overlay widgets run above the row bottom; restore the
  // layout cursor to the Y recorded right after Selectable, then submit a zero-size Dummy.
  void HistoryRowAdvanceLayoutAfterOverlays(float row_start_x, float cursor_y_after_selectable) {
    ImGui::SetCursorPos(ImVec2(row_start_x, cursor_y_after_selectable));
    ImGui::Dummy(ImVec2(0.0F, 0.0F));
  }
} // anonymous namespace

// Example search configuration used for empty state guidance
// Category identifiers for grouping in the "Explore search types" panel
enum class ExampleCategory : std::uint8_t { ByFilename, ByPattern, ByFileType, ByFilters };

struct ExampleSearch {
  const char* label;        // Compact label for pill button  // NOLINT(readability-identifier-naming)
  const char* filename;     // Filename input pattern  // NOLINT(readability-identifier-naming)
  const char* extension;    // Extension input  // NOLINT(readability-identifier-naming)
  const char* path;         // Path input (can be empty)  // NOLINT(readability-identifier-naming)
  bool folders_only;        // Folders-only flag  // NOLINT(readability-identifier-naming)
  bool case_sensitive;      // Case sensitivity flag  // NOLINT(readability-identifier-naming)
  TimeFilter time_filter;   // Time-based filter  // NOLINT(readability-identifier-naming)
  SizeFilter size_filter;   // Size-based filter  // NOLINT(readability-identifier-naming)
  ExampleCategory category;  // NOLINT(readability-identifier-naming)
};

static constexpr std::array<ExampleSearch, 10> kExampleSearches = {  // NOSONAR(cpp:S5945) - Using std::array instead of C-style array
  ExampleSearch{"*.cpp", "*.cpp", "", "", false, false, TimeFilter::None, SizeFilter::None, ExampleCategory::ByFilename},
  ExampleSearch{"Makefile", "Makefile", "", "", false, false, TimeFilter::None, SizeFilter::None, ExampleCategory::ByFilename},
  ExampleSearch{"^main", "^main", "", "", false, false, TimeFilter::None, SizeFilter::None, ExampleCategory::ByPattern},
  ExampleSearch{"*.h", "", "h", "", false, false, TimeFilter::None, SizeFilter::None, ExampleCategory::ByFileType},
  ExampleSearch{"*.py", "*.py", "", "", false, false, TimeFilter::None, SizeFilter::None, ExampleCategory::ByFileType},
  ExampleSearch{"*.json", "*.json", "", "", false, false, TimeFilter::None, SizeFilter::None, ExampleCategory::ByFileType},
  ExampleSearch{"Folders only", "", "", "", true, false, TimeFilter::None, SizeFilter::None, ExampleCategory::ByFilters},
  ExampleSearch{"Modified today", "", "", "", false, false, TimeFilter::Today, SizeFilter::None, ExampleCategory::ByFilters},
  ExampleSearch{"Empty files", "", "", "", false, false, TimeFilter::None, SizeFilter::Empty, ExampleCategory::ByFilters},
  ExampleSearch{"Large files (>10MB)", "", "", "", false, false, TimeFilter::None, SizeFilter::Large, ExampleCategory::ByFilters},
};

/**
 * @brief Apply an example search to GuiState (caller should trigger search to match recent-search behavior)
 */
static void ApplyExampleSearch(const ExampleSearch& ex, GuiState& state) {
  // Do NOT call state.ClearInputs() here as it sets clearResultsRequested = true,
  // which causes the subsequent TriggerManualSearch to be cancelled in the same frame.
  // Instead, overwrite all relevant search fields directly.
  state.searchCriteria.filename_input.SetValue(ex.filename);
  state.searchCriteria.extension_input.SetValue(ex.extension);
  state.searchCriteria.path_input.SetValue(ex.path);
  state.searchCriteria.folders_only = ex.folders_only;
  state.searchCriteria.case_sensitive = ex.case_sensitive;
  state.searchCriteria.time_filter = ex.time_filter;
  state.searchCriteria.size_filter = ex.size_filter;

  // Clear AI search description as examples are manual searches
  state.gemini.description_input[0] = '\0';  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - Fixed-size buffer, index 0 is always valid

  state.MarkInputChanged();
}

/**
 * @brief Get category subheader label for display
 */
static const char* GetCategoryLabel(ExampleCategory cat) {
  switch (cat) {
    case ExampleCategory::ByFilename:
      return "By Name";
    case ExampleCategory::ByPattern:
      return "By Pattern";
    case ExampleCategory::ByFileType:
      return "By File Type";
    case ExampleCategory::ByFilters:
      return "By Filters";
    default:
      return "";
  }
}

[[nodiscard]] static size_t CountExamplesInCategory(ExampleCategory cat) {
  size_t n = 0;
  for (const ExampleSearch& ex : kExampleSearches) {  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
    if (ex.category == cat) {
      ++n;
    }
  }
  return n;
}

/**
 * @brief Render left panel: Explore search types with categorized examples.
 * @param actions Used to trigger manual search on example click; must be non-const.
 */
static void RenderExamplesPanel(GuiState& state, UIActions* actions, float panel_width,
                               float panel_height, bool is_index_building,
                               size_t indexed_file_count) {
  ImGui::BeginChild("empty_state_examples", ImVec2(panel_width, panel_height), ImGuiChildFlags_Borders);

  const ImGuiStyle& style = ImGui::GetStyle();
  RenderWelcomePanelTitle(FormatTitleWithCount("Explore Search Types", kExampleSearches.size()));

  const bool can_show_all =
      actions != nullptr && !is_index_building && indexed_file_count > 0U;
  if (!can_show_all) {
    ImGui::BeginDisabled();
  }
  if (ImGui::SmallButton("all indexed items") && actions != nullptr) {
    state.ApplyShowAllPreset();
    actions->TriggerManualSearch(state);
  }
  if (!can_show_all) {
    ImGui::EndDisabled();
  }
  ImGui::Spacing();

  if (is_index_building) {
    ImGui::BeginDisabled();
  }

  // Compute content width once while cursor is at the start of a fresh line.
  // GetContentRegionAvail().x is only accurate when the cursor is at line-start
  // (ImGui resets cursor X after every widget, so querying mid-row gives full width).
  const float content_width = ImGui::GetContentRegionAvail().x;

  ExampleCategory current_category = ExampleCategory::ByFilename;
  bool first_in_category = true;
  bool first_in_row = true;
  float line_x = 0.0F;  // Manually-tracked accumulated width on the current row

  for (size_t i = 0; i < kExampleSearches.size(); ++i) {
    const ExampleSearch& ex = kExampleSearches.at(i);
    if (ex.category != current_category) {
      ImGui::NewLine();  // End previous category's button row
      current_category = ex.category;
      first_in_category = true;
      ImGui::Spacing();
      first_in_row = true;
      line_x = 0.0F;
    }
    if (first_in_category) {
      RenderWelcomeSectionTitle(GetCategoryLabel(current_category),
                                CountExamplesInCategory(current_category),
                                /*with_separator=*/false);
      first_in_category = false;
      first_in_row = true;
      line_x = 0.0F;
    }

    const float button_width =
        ImGui::CalcTextSize(ex.label).x + (style.FramePadding.x * 2.0F);

    if (!first_in_row) {
      if (line_x + LayoutConstants::kExampleButtonSpacing + button_width <= content_width) {
        ImGui::SameLine(0.0F, LayoutConstants::kExampleButtonSpacing);
        line_x += LayoutConstants::kExampleButtonSpacing;
      } else {
        // Don't call SameLine — ImGui already advanced to the next line after
        // the previous widget, so omitting SameLine is sufficient to wrap.
        line_x = 0.0F;
      }
    }

    ImGui::PushID(static_cast<int>(i));
    if (const bool clicked = ImGui::SmallButton(ex.label); clicked && actions != nullptr) {
      ApplyExampleSearch(ex, state);
      actions->TriggerManualSearch(state);
    }
    ImGui::PopID();
    line_x += button_width;
    first_in_row = false;
  }

  if (is_index_building) {
    ImGui::EndDisabled();
  }

  ImGui::EndChild();
}

/**
 * @brief Configuration for shared pinned/recent history row rendering.
 */
struct HistoryRowActionsConfig {
  const char* primary_icon;  // NOLINT(readability-identifier-naming)
  const char* primary_tooltip;  // NOLINT(readability-identifier-naming)
  const char* primary_menu_label;  // NOLINT(readability-identifier-naming)
  bool is_pin_action;  // NOLINT(readability-identifier-naming)
};

static void ApplyHistoryRowPrimaryAction(const SearchHistoryEntry& entry,
                                         const HistoryRowActionsConfig& config,
                                         std::int64_t now_ms,
                                         AppSettings& settings,
                                         UIActions* actions) {
  if (config.is_pin_action) {
    PinHistoryEntry(entry.id, now_ms, settings);
  } else {
    UnpinHistoryEntry(entry.id, now_ms, settings);
  }
  if (actions == nullptr || !actions->PersistSettings()) {
    LOG_ERROR("Failed to persist search history pin/unpin change");
  }
}

static void RenderHistoryRow(const SearchHistoryEntry* entry,
                             const HistoryRowActionsConfig& actions_config,
                             GuiState& state,
                             AppSettings& settings,
                             UIActions* actions,
                             std::int64_t now_ms) {
  const HistoryEntryDisplayInfo info = BuildHistoryEntryDisplayInfo(*entry);
  const std::string timestamp = FormatRelativeTime(entry->last_used_at_unix_ms, now_ms);

  ImGui::PushID(entry->id.c_str());

  const bool selected = (state.history.selected_id == entry->id);
  const float row_height = ImGui::GetTextLineHeightWithSpacing() * 2.1F;
  const float row_start_y = ImGui::GetCursorPosY();
  const float row_start_x = ImGui::GetCursorPosX();
  const ImVec2 row_screen_pos = ImGui::GetCursorScreenPos();
  const float avail_w = ImGui::GetContentRegionAvail().x;

  // Full-width selectable with item overlap allowed for buttons
  if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowOverlap,
                        ImVec2(avail_w, row_height)) &&
      actions != nullptr) {
    state.history.selected_id = entry->id;
    ApplyHistoryEntryToGuiState(*entry, state);
    actions->TriggerManualSearch(state);
  }
  const float cursor_y_after_selectable = ImGui::GetCursorPosY();
  // Register context-menu trigger on the selectable (full row area) immediately after it,
  // before any overlay rendering that would change g.LastItemData.ID.
  ImGui::OpenPopupOnItemClick("##ctx", ImGuiPopupFlags_MouseButtonRight);

  // Left-edge accent bar for the selected row.
  if (selected) {
    constexpr float k_accent_bar_w = 3.0F;
    const ImU32 accent_col = ImGui::ColorConvertFloat4ToU32(Theme::Colors::Accent);
    ImGui::GetWindowDrawList()->AddRectFilled(
        row_screen_pos,
        ImVec2(row_screen_pos.x + k_accent_bar_w, row_screen_pos.y + row_height),
        accent_col);
  }

  // Render text on top of selectable
  ImGui::SetCursorPosY(row_start_y + ImGui::GetStyle().FramePadding.y);
  ImGui::Indent(ImGui::GetStyle().ItemSpacing.x);

  // Show actions whenever the mouse is anywhere over the full two-line row.
  const bool hovered = ImGui::IsMouseHoveringRect(
      row_screen_pos, ImVec2(row_screen_pos.x + avail_w, row_screen_pos.y + row_height));
  const bool show_actions = hovered || state.history.pending_rename_id == entry->id || state.history.pending_delete_id == entry->id;

  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float timestamp_w = ImGui::CalcTextSize(timestamp.c_str()).x;
  const float button_w = MaxSmallIconButtonWidth(
      {actions_config.primary_icon, ICON_FA_PEN, ICON_FA_TRASH});
  const float total_buttons_w = (button_w * 3.0F) + (spacing * 2.0F);

  // Reserve the wider of timestamp vs action strip so line-1 text never intrudes on the right.
  const float reserved_right_w = (std::max)(timestamp_w, total_buttons_w) + spacing + spacing;

  const ImVec2 mouse_pos = ImGui::GetIO().MousePos;
  if (const bool in_action_zone =
          mouse_pos.x >= (row_screen_pos.x + avail_w - reserved_right_w);
      hovered && !in_action_zone) {
    const std::string tooltip = BuildHistoryEntryTooltip(*entry);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * kHistoryTooltipWrapWidthChars);
    ImGui::TextUnformatted(tooltip.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }

  // Line 1: Headline (with ellipsis) + Timestamp or Actions
  const float headline_max_w = ImGui::GetWindowContentRegionMax().x - reserved_right_w - ImGui::GetCursorPosX();
  TextEllipsisSingleLine(info.headline.c_str(), info.headline.c_str() + info.headline.size(), headline_max_w);

  // Timestamp: only at rest — buttons replace it on hover
  if (!show_actions) {
    ImGui::SameLine();
    const float timestamp_x = ImGui::GetWindowContentRegionMax().x - timestamp_w - spacing;
    ImGui::SetCursorPosX(timestamp_x);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDim);
    ImGui::TextUnformatted(timestamp.c_str());
    ImGui::PopStyleColor();
  }

  // Line 2: Subtitle — clipped to reserved_right_w so it never runs under the button zone
  ImGui::SetCursorPosY(row_start_y + ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y +
                       2.0F);
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDim);
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kHistoryRowSubtitleFontScale);
  const float subtitle_max_w = ImGui::GetWindowContentRegionMax().x - reserved_right_w - ImGui::GetCursorPosX();
  if (info.subtitle.empty()) {
    ImGui::TextUnformatted("\xe2\x80\x94");  // em dash — quieter than "no filters"
  } else {
    TextEllipsisSingleLine(info.subtitle.c_str(), info.subtitle.c_str() + info.subtitle.size(), subtitle_max_w);
  }
  ImGui::PopFont();
  ImGui::PopStyleColor();
  ImGui::Unindent(ImGui::GetStyle().ItemSpacing.x);

  if (show_actions) {
    // Line up with headline/timestamp row — avoids overlapping the timestamp band and keeps icons
    // on one horizontal baseline.
    ImGui::SetCursorPosY(HistoryRowActionsCursorY(row_start_y));
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - total_buttons_w - spacing);

    if (RenderIconButtonWithTooltip(actions_config.primary_icon,
                                    actions_config.primary_tooltip)) {
      ApplyHistoryRowPrimaryAction(*entry, actions_config, now_ms, settings, actions);
    }
    ImGui::SameLine(0.0F, spacing);
    if (RenderIconButtonWithTooltip(ICON_FA_PEN, "Rename")) {
      state.history.pending_rename_id = entry->id;
    }
    ImGui::SameLine(0.0F, spacing);
    constexpr float kDeleteButtonHoverAlpha = 0.75F;
    constexpr float kDeleteButtonActiveAlpha = 1.0F;
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(Theme::Colors::Error.x, Theme::Colors::Error.y, Theme::Colors::Error.z, kDeleteButtonHoverAlpha));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(Theme::Colors::Error.x, Theme::Colors::Error.y, Theme::Colors::Error.z, kDeleteButtonActiveAlpha));
    if (RenderIconButtonWithTooltip(ICON_FA_TRASH, "Delete")) {
      state.history.pending_delete_id = entry->id;
    }
    ImGui::PopStyleColor(2);
  }

  if (ImGui::BeginPopup("##ctx")) {
    if (ImGui::MenuItem(actions_config.primary_menu_label)) {
      ApplyHistoryRowPrimaryAction(*entry, actions_config, now_ms, settings, actions);
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

/**
 * @brief Render a single pinned history row with its context menu.
 */
static void RenderPinnedHistoryRow(const SearchHistoryEntry* entry, GuiState& state,
                                   AppSettings& settings, UIActions* actions, std::int64_t now_ms) {
  constexpr HistoryRowActionsConfig k_pinned_actions{
      ICON_FA_XMARK, "Unpin", ICON_FA_XMARK " Unpin", false,
  };
  RenderHistoryRow(entry, k_pinned_actions, state, settings, actions, now_ms);
}

/**
 * @brief Render a single recent (unpinned) history row with its context menu.
 */
static void RenderRecentHistoryRow(const SearchHistoryEntry* entry, GuiState& state,
                                   AppSettings& settings, UIActions* actions, std::int64_t now_ms) {
  constexpr HistoryRowActionsConfig k_recent_actions{
      ICON_FA_PIN, "Pin", ICON_FA_PIN " Pin", true,
  };
  RenderHistoryRow(entry, k_recent_actions, state, settings, actions, now_ms);
}


/**
 * @brief Render the unified Search History panel (pinned + recent sections).
 *
 * Replaces RenderRecentPanel. Renders all history entries sorted by
 * GetSortedHistoryView: pinned entries first (collapsed when none), then recents.
 * Per-row actions: run, pin/unpin, rename (pinned only), delete.
 */
static void RenderSearchHistoryPanel(GuiState& state, AppSettings& settings, UIActions* actions,
                                     float panel_width, float panel_height,
                                     bool is_index_building) {
  ImGui::BeginChild("empty_state_history", ImVec2(panel_width, panel_height),
                    ImGuiChildFlags_Borders);

  const auto view = GetSortedHistoryView(settings);

  RenderWelcomePanelTitle(FormatTitleWithCount("Search History", view.size()));

  if (view.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDisabled);
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(kHistoryPanelEmpty);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::EndChild();
    return;
  }

  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDim);
  ImGui::PushTextWrapPos(0.0F);
  ImGui::TextUnformatted(kHistoryPanelHint);
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
  ImGui::Spacing();

  // Validate selection each frame — clear if the entry was deleted or reordered away.
  if (!state.history.selected_id.empty()) {
    const bool still_present = std::any_of(  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
        view.begin(), view.end(),
        [&state](const SearchHistoryEntry* e) { return e->id == state.history.selected_id; });
    if (!still_present) {
      state.history.selected_id.clear();
    }
  }

  const auto now_ms = static_cast<std::int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());

  if (is_index_building) {
    ImGui::BeginDisabled();
  }

  // Count entries per section up front for the badge labels.
  const auto pinned_count = static_cast<size_t>(std::count_if(  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
      view.begin(), view.end(), [](const SearchHistoryEntry* e) { return e->is_pinned; }));
  const size_t recent_count = view.size() - pinned_count;

  // ----- Pinned subsection -----
  if (pinned_count > 0) {
    RenderWelcomeSectionTitle("Pinned", pinned_count);
    for (const SearchHistoryEntry* entry : view) {
      if (entry->is_pinned) {
        RenderPinnedHistoryRow(entry, state, settings, actions, now_ms);
      }
    }
    ImGui::Spacing();
  }

  // ----- Recent subsection -----
  if (recent_count > 0) {
    RenderWelcomeSectionTitle("Recent", recent_count);
    for (const SearchHistoryEntry* entry : view) {
      if (!entry->is_pinned) {
        RenderRecentHistoryRow(entry, state, settings, actions, now_ms);
      }
    }
  }

  if (is_index_building) {
    ImGui::EndDisabled();
  }

  ImGui::EndChild();
}

/**
 * @brief Render split panel: left = examples, right = search history
 */
static void RenderSplitPanel(GuiState& state, AppSettings& settings, UIActions* actions,
                             float total_width, bool is_index_building,
                             size_t indexed_file_count) {
  constexpr float k_left_ratio = 0.33F;  // Left panel 33%, right 67%
  const float left_width = (total_width * k_left_ratio) - (LayoutConstants::kWelcomePanelSplitPanelGutter * 0.5F);
  const float right_width = (total_width * (1.0F - k_left_ratio)) - (LayoutConstants::kWelcomePanelSplitPanelGutter * 0.5F);
  const float panel_height = ImGui::GetContentRegionAvail().y;

  RenderExamplesPanel(state, actions, left_width, panel_height, is_index_building,
                      indexed_file_count);
  ImGui::SameLine(0.0F, LayoutConstants::kWelcomePanelSplitPanelGutter);
  RenderSearchHistoryPanel(state, settings, actions, right_width, panel_height, is_index_building);
}

void WelcomePanel::Render(GuiState& state,
                        UIActions* actions,
                        AppSettings& settings,
                        bool is_index_building) {
  // Show placeholder message with prominent vertical space
  AddVerticalSpacing(LayoutConstants::kWelcomePanelHeroSpacingCount);

  // Center the text horizontally
  const float window_width = ImGui::GetContentRegionAvail().x;

  // Show different message if index is empty (no files indexed yet)
  const size_t indexed_file_count =
      (actions != nullptr) ? actions->GetIndexedFileCount() : 0;
  if (indexed_file_count == 0) {
    // Getting started message for empty index
    const char* getting_started_title = "Getting Started";
    const float title_width = ImGui::CalcTextSize(getting_started_title).x;
    ImGui::SetCursorPosX((window_width - title_width) * 0.5F);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::Warning);
    ImGui::Text("%s", getting_started_title);
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Spacing();

    // Instructions for getting started (same message on all platforms)
    const char* instructions = "No files indexed yet. Go to Settings > Index Configuration\nto select a folder to index.";

    // Split instructions into lines for proper centering
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

    // Helpful hint: Settings button opens Settings (no keyboard shortcut implemented)
    const char* hint = "Tip: Use the Settings button to open Settings";
    const float hint_width = ImGui::CalcTextSize(hint).x;
    ImGui::SetCursorPosX((window_width - hint_width) * 0.5F);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDisabled);
    ImGui::Text("%s", hint);
    ImGui::PopStyleColor();

    return;  // Don't show example searches when index is empty
  }

  // Main message: either indexing-in-progress hint or standard "no results" label
  std::string main_message;
  ImVec4 main_color;
  bool use_larger_font = false;

  if (is_index_building) {
    // Prominent indexing-in-progress message with optional indexed file count
    main_message = "Please wait... indexing in progress";

    // Calculate elapsed time since indexing started
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - state.index_build.start_time);
    const double elapsed_seconds = static_cast<double>(elapsed.count()) / 1000.0;

    main_message += " (";
    // Format elapsed time (e.g., 1.2s)
    std::array<char, 32> time_buf{};
    if (const int snprintf_result =
            snprintf(time_buf.data(), time_buf.size(), "%.1fs", elapsed_seconds);
        snprintf_result > 0) {
      main_message += time_buf.data();
    }

    if (indexed_file_count > 0U) {
      main_message += ',';
      main_message += FormatNumberWithSeparators(indexed_file_count);
      main_message += " items indexed";
    }
    main_message += ')';
    main_color = Theme::Colors::Accent;
    use_larger_font = true;
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

  // Split panel: left = example searches (by category), right = recent searches (table)
  RenderSplitPanel(state, settings, actions, window_width, is_index_building,
                   indexed_file_count);
}

}  // namespace ui
