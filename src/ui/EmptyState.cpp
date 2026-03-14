/**
 * @file ui/EmptyState.cpp
 * @brief Implementation of empty state rendering component
 */

#include "ui/EmptyState.h"

#include "core/Settings.h"
#include "filters/SizeFilter.h"
#include "filters/TimeFilter.h"
#include "filters/TimeFilterUtils.h"
#include "gui/GuiState.h"
#include "gui/ImGuiUtils.h"
#include "gui/UIActions.h"
#include "imgui.h"
#include "ui/IconsFontAwesome.h"
#include "ui/LayoutConstants.h"
#include "ui/Theme.h"
#include "utils/StringUtils.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

namespace {

  // Show tooltip if item is hovered and text is non-empty (reduces nesting at call site).
  void ShowTooltipIfHovered(std::string_view text) {
    if (!ImGui::IsItemHovered() || text.empty()) {
      return;
    }
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::EndTooltip();
  }
} // anonymous namespace

/**
 * @brief Truncate path display to show meaningful part
 *
 * Truncates very long paths to show just the last part (directory or filename).
 * If path is longer than 20 characters, extracts the meaningful part.
 *
 * @param path Path to truncate
 * @return Truncated path display string
 */
static std::string TruncatePathDisplay(std::string_view path) {
  if (constexpr size_t k_max_path_length = 20; path.length() <= k_max_path_length) {
    return std::string(path);
  }
  // Try to extract meaningful part (e.g., last directory or pattern)
  if (const size_t last_slash = path.find_last_of("/\\");
      last_slash != std::string_view::npos && last_slash + 1 < path.length()) {
    return "..." + std::string(path.substr(last_slash + 1));
  }
  constexpr size_t k_truncated_path_length = 17;  // k_max_path_length - 3 for "..."
  return std::string(path.substr(0, k_truncated_path_length)) + "...";
}

/**
 * @brief Format extensions for display with smart truncation
 *
 * If there are 3 or fewer extensions, shows all.
 * If there are more, shows first 2 and count of remaining.
 *
 * @param extensions Comma-separated extension string (e.g., "cpp,h,hpp")
 * @return Formatted string (e.g., "*.cpp,h,hpp" or "*.cpp,h (+11 more)")
 */
static std::string FormatExtensionsForDisplay(std::string_view extensions) {
  if (extensions.empty()) {
    return "";
  }

  const std::vector<std::string> ext_list = ParseExtensions(extensions, ',');  // ParseExtensions accepts string_view

  if (ext_list.empty()) {
    return "";
  }

  // Helper to join extensions
  auto join_extensions = [](const std::vector<std::string>& list, size_t count) {
    if (list.empty()) {
      return std::string();
    }
    std::string s = "*." + list.at(0);
    for (size_t i = 1; i < count && i < list.size(); ++i) {
      s += "," + list.at(i);
    }
    return s;
  };

  // Show all if 3 or fewer
  if (ext_list.size() <= 3) {
    return join_extensions(ext_list, ext_list.size());
  }

  // Show first 2, then count
  std::string result = join_extensions(ext_list, 2);
  // ext_list.size() > 3 at this point (checked above), so this is always positive
  const size_t remaining = (ext_list.size() >= 2) ? (ext_list.size() - 2) : 0;
  result += " (+" + std::to_string(remaining) + " more)";

  return result;
}

/**
 * @brief Build full tooltip text for a recent search
 *
 * @param recent The saved search to describe
 * @return Formatted tooltip text
 */
static std::string BuildRecentSearchTooltip(const SavedSearch& recent) {
  std::string tooltip;

  if (!recent.extensions.empty()) {
    // Format extensions nicely: *.cpp, *.h, *.hpp
    std::vector<std::string> ext_list = ParseExtensions(recent.extensions, ',');

    if (!ext_list.empty()) {
      tooltip += "Extensions: ";
      for (size_t i = 0; i < ext_list.size(); ++i) {
        if (i > 0) {  // NOSONAR(cpp:S134) - Nested if acceptable: simple separator logic in loop
          tooltip += ", ";
        }
        tooltip += "*." + ext_list.at(i);
      }
      tooltip += "\n";
    }
  }

  if (!recent.filename.empty()) {
    tooltip += "Name: " + recent.filename + "\n";
  }

  if (!recent.path.empty()) {
    tooltip += "Path: " + recent.path + "\n";
  }

  if (recent.foldersOnly) {
    tooltip += "Folders only\n";
  }

  if (recent.caseSensitive) {
    tooltip += "Case sensitive\n";
  }

  if (recent.timeFilter != "None" && !recent.timeFilter.empty()) {
    tooltip += "Time filter: " + recent.timeFilter + "\n";
  }

  if (recent.sizeFilter != "None" && !recent.sizeFilter.empty()) {
    tooltip += "Size filter: " + recent.sizeFilter + "\n";
  }

  // Remove trailing newline if present
  if (!tooltip.empty() && tooltip.back() == '\n') {
    tooltip.pop_back();
  }

  return tooltip;
}

/**
 * @brief Build label from parts with " + " separator
 *
 * Combines multiple parts into a single label string with " + " separators.
 *
 * @param parts Vector of label parts to combine
 * @return Combined label string, or "search" if parts is empty
 */
static std::string BuildRecentSearchLabel(const std::vector<std::string>& parts) {
  if (parts.empty()) {
    return "search";
  }
  std::string label;
  for (size_t j = 0; j < parts.size(); ++j) {
    if (j > 0) {
      label += " + ";
    }
    label += parts.at(j);
  }
  return label;
}

/**
 * @brief Truncate label based on pixel width, preserving important parts
 *
 * Truncates labels that exceed max pixel width, trying to preserve the first part
 * if it contains multiple parts separated by " + ".
 *
 * @param label Label to truncate
 * @param max_width Maximum pixel width (including button padding)
 * @return Truncated label
 */
static std::string TruncateLabelByWidth(std::string_view label, float max_width) {
  std::string label_str(label);
  if (const ImVec2 text_size = ImGui::CalcTextSize(label_str.c_str()); text_size.x <= max_width) {
    return label_str;
  }

  // Account for button padding (FramePadding on both sides)
  const float button_padding = ImGui::GetStyle().FramePadding.x * 2.0F;
  const float available_text_width = max_width - button_padding;

  // If we have multiple parts, try to keep the first part and truncate the rest
  if (const size_t first_sep = label_str.find(" + "); first_sep != std::string::npos) {
    const std::string first_part = label_str.substr(0, first_sep);
    const ImVec2 first_part_size = ImGui::CalcTextSize(first_part.c_str());

    // Check if first part + " + ..." fits
    if (const ImVec2 ellipsis_size = ImGui::CalcTextSize(" + ...");
        first_part_size.x + ellipsis_size.x <= available_text_width) {
      return first_part + " + ...";
    }
  }

  // Binary search for the maximum length that fits
  size_t low = 0;
  size_t high = label_str.length();
  size_t best = 0;
  const std::string ellipsis = "...";
  // Note: ellipsis_size was calculated but never used; removed unused variable

  while (low <= high) {
    const size_t mid = (low + high) / 2;
    const std::string test = label_str.substr(0, mid) + ellipsis;
    const ImVec2 test_size = ImGui::CalcTextSize(test.c_str());

    if (test_size.x <= available_text_width) {
      best = mid;
      low = mid + 1;
    } else {
      high = mid - 1;
    }
  }

  if (best > 0) {
    return label_str.substr(0, best) + ellipsis;
  }
  return ellipsis;  // NOLINT(performance-no-automatic-move) - Fallback if even ellipsis doesn't fit, const string literal is fine here
}

// FormatNumberWithSeparators REMOVED (moved to StringUtils.h)

/**
 * @brief Build label for a recent search from its components
 *
 * Combines path, filename, extensions, and folders-only flag into a display label.
 *
 * @param recent The saved search to build label for
 * @param max_button_width Maximum button width for truncation
 * @param available_width Available width for buttons
 * @param button_padding Button padding to account for
 * @return Label string and its calculated width
 */
static inline std::pair<std::string, float> BuildRecentSearchLabelAndWidth(const SavedSearch& recent,
                                                                           float max_button_width,
                                                                           float available_width,
                                                                           float button_padding) {
  // Build label by combining available fields in order: path, filename, extensions
  std::vector<std::string> parts;

  // Add path first (if present, truncate long paths)
  if (!recent.path.empty()) {
    parts.push_back(TruncatePathDisplay(recent.path));
  }

  // Add filename second (if present)
  if (!recent.filename.empty()) {
    parts.push_back(recent.filename);
  }

  // Add extensions third (with smart truncation for long lists)
  if (!recent.extensions.empty()) {
    parts.push_back(FormatExtensionsForDisplay(recent.extensions));
  }

  // Add folders-only indicator last
  if (recent.foldersOnly) {
    parts.emplace_back("folders");
  }

  // Combine parts with " + " separator
  std::string label = BuildRecentSearchLabel(parts);

  // Truncate based on maximum button width
  const float effective_max_width = (std::min)(max_button_width, available_width - button_padding);
  label = TruncateLabelByWidth(label, effective_max_width);

  // Calculate actual button width (text + padding)
  const ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
  const float button_width = text_size.x + button_padding;

  return {label, button_width};
}

// Example search configuration used for empty state guidance
// Category identifiers for grouping in the "Explore search types" panel
enum class ExampleCategory : std::uint8_t { ByFilename, ByPattern, ByFileType, ByFilters };

struct ExampleSearch {
  const char* label;        // Compact label for pill button
  const char* filename;     // Filename input pattern
  const char* extension;    // Extension input
  const char* path;         // Path input (can be empty)
  bool folders_only;        // Folders-only flag
  bool case_sensitive;      // Case sensitivity flag
  TimeFilter time_filter;   // Time-based filter
  SizeFilter size_filter;   // Size-based filter
  ExampleCategory category;
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
  state.filenameInput.SetValue(ex.filename);
  state.extensionInput.SetValue(ex.extension);
  state.pathInput.SetValue(ex.path);
  state.foldersOnly = ex.folders_only;
  state.caseSensitive = ex.case_sensitive;
  state.timeFilter = ex.time_filter;
  state.sizeFilter = ex.size_filter;

  // Clear AI search description as examples are manual searches
  state.gemini_description_input_[0] = '\0';  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - Fixed-size buffer, index 0 is always valid

  state.MarkInputChanged();
}

/**
 * @brief Get category subheader label for display
 */
static const char* GetCategoryLabel(ExampleCategory cat) {
  switch (cat) {
    case ExampleCategory::ByFilename:
      return "By name";
    case ExampleCategory::ByPattern:
      return "By pattern";
    case ExampleCategory::ByFileType:
      return "By file type";
    case ExampleCategory::ByFilters:
      return "By filters";
    default:
      return "";
  }
}

/**
 * @brief Render left panel: Explore search types with categorized examples.
 * @param actions Used to trigger manual search on example click; must be non-const.
 */
static void RenderExamplesPanel(GuiState& state, UIActions* actions, float panel_width,
                               float panel_height, bool is_index_building) {
  ImGui::BeginChild("empty_state_examples", ImVec2(panel_width, panel_height), ImGuiChildFlags_Borders);

  const ImGuiStyle& style = ImGui::GetStyle();
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDim);
  ImGui::TextUnformatted("Explore search types");
  ImGui::PopStyleColor();
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
      ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDim);
      ImGui::TextUnformatted(GetCategoryLabel(current_category));
      ImGui::PopStyleColor();
      ImGui::Spacing();
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
 * @brief Build a short filters summary string for recent search display
 */
static std::string BuildFiltersSummary(const SavedSearch& recent) {
  std::vector<std::string> parts;
  if (recent.foldersOnly) {
    parts.emplace_back("folders");
  }
  if (recent.caseSensitive) {
    parts.emplace_back("case");
  }
  if (!recent.timeFilter.empty() && recent.timeFilter != "None") {
    parts.push_back(recent.timeFilter);
  }
  if (!recent.sizeFilter.empty() && recent.sizeFilter != "None") {
    parts.push_back(recent.sizeFilter);
  }
  if (parts.empty()) {
    return "-";
  }
  std::string result;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      result += ", ";
    }
    result += parts.at(i);
  }
  return result;
}

/**
 * @brief Render right panel: Recent searches as a compact table
 *
 * Displays Name, Path, Filters columns. Click row to re-run search.
 * Panel has fixed height so a vertical scrollbar appears when there are many rows.
 */
static void RenderRecentPanel(GuiState& state, const AppSettings& settings, UIActions* actions,
                              float panel_width, float panel_height, bool is_index_building) {
  ImGui::BeginChild("empty_state_recent", ImVec2(panel_width, panel_height), ImGuiChildFlags_Borders);

  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDim);
  ImGui::TextUnformatted("Recent searches");
  ImGui::PopStyleColor();
  ImGui::Spacing();

  const size_t recent_count =
      settings.recentSearches.size() < settings_defaults::kMaxRecentSearches
          ? settings.recentSearches.size()
          : settings_defaults::kMaxRecentSearches;

  if (recent_count == 0) {
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDisabled);
    ImGui::TextUnformatted("No recent searches yet.");
    ImGui::PopStyleColor();
    ImGui::EndChild();
    return;
  }

  if (is_index_building) {
    ImGui::BeginDisabled();
  }

  const float button_padding = ImGui::GetStyle().FramePadding.x * 2.0F;
  constexpr float k_max_button_width = 180.0F;
  const float available_width = (panel_width * 0.45F) - 20.0F;

  if (const auto k_table_flags = static_cast<int>(
          static_cast<unsigned int>(ImGuiTableFlags_BordersH) |
          static_cast<unsigned int>(ImGuiTableFlags_BordersInnerV) |
          static_cast<unsigned int>(ImGuiTableFlags_RowBg) |
          static_cast<unsigned int>(ImGuiTableFlags_SizingStretchProp));
      ImGui::BeginTable("recent_searches_table", 3, k_table_flags)) {
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.50F);
    ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, 0.30F);
    ImGui::TableSetupColumn("Filters", ImGuiTableColumnFlags_WidthStretch, 0.20F);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < recent_count; ++i) {
      const SavedSearch& recent = settings.recentSearches.at(i);
      const auto [label, button_width] =
          BuildRecentSearchLabelAndWidth(recent, k_max_button_width, available_width, button_padding);
      const std::string filters_str = BuildFiltersSummary(recent);
      const std::string path_display =
          recent.path.empty() ? "-" : TruncatePathDisplay(recent.path);

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(i));

      if (const bool clicked = ImGui::Selectable(label.c_str(), false, 0, ImVec2(0, 0));
          clicked && actions != nullptr) {
        actions->ApplySavedSearch(state, recent);
        actions->TriggerManualSearch(state);
      }
      if (ImGui::IsItemHovered()) {
        ShowTooltipIfHovered(BuildRecentSearchTooltip(recent));
      }
      ImGui::TableNextColumn();
      ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDim);
      ImGui::TextUnformatted(path_display.c_str());
      ImGui::PopStyleColor();

      ImGui::TableNextColumn();
      ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDisabled);
      ImGui::TextUnformatted(filters_str.c_str());
      ImGui::PopStyleColor();

      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  if (is_index_building) {
    ImGui::EndDisabled();
  }

  ImGui::EndChild();
}

/**
 * @brief Render split panel: left = examples, right = recent searches
 */
static void RenderSplitPanel(GuiState& state, const AppSettings& settings, UIActions* actions,
                             float total_width, bool is_index_building) {
  constexpr float k_left_ratio = 0.33F;  // Left panel 33%, right 67%
  const float left_width = (total_width * k_left_ratio) - (LayoutConstants::kEmptyStateSplitPanelGutter * 0.5F);
  const float right_width = (total_width * (1.0F - k_left_ratio)) - (LayoutConstants::kEmptyStateSplitPanelGutter * 0.5F);
  const float panel_height = ImGui::GetContentRegionAvail().y;

  RenderExamplesPanel(state, actions, left_width, panel_height, is_index_building);
  ImGui::SameLine(0.0F, LayoutConstants::kEmptyStateSplitPanelGutter);
  RenderRecentPanel(state, settings, actions, right_width, panel_height, is_index_building);
}

void EmptyState::Render(GuiState& state,
                        UIActions* actions,
                        const AppSettings& settings,
                        bool is_index_building) {
  // Show placeholder message with prominent vertical space
  AddVerticalSpacing(LayoutConstants::kEmptyStateHeroSpacingCount);

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
        std::chrono::duration_cast<std::chrono::milliseconds>(now - state.index_build_start_time);
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
      main_message += ", ";
      main_message += FormatNumberWithSeparators(indexed_file_count);
      main_message += " items indexed";
    }
    main_message += ")";
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

  if (!is_index_building && actions != nullptr && indexed_file_count > 0U) {
    const char* show_all_label = ICON_FA_LIST " Show all indexed items";
    const ImVec2 button_size = ImGui::CalcTextSize(show_all_label);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float button_width =
      button_size.x + (style.FramePadding.x * 2.0F);
    ImGui::SetCursorPosX((window_width - button_width) * 0.5F);
    Theme::PushAccentButtonStyle();
    if (ImGui::Button(show_all_label)) {
      state.ApplyShowAllPreset();
      actions->TriggerManualSearch(state);
      Theme::PopAccentButtonStyle();
      return;
    }
    Theme::PopAccentButtonStyle();
    ImGui::Spacing();
  }

  ImGui::Separator();
  AddVerticalSpacing(LayoutConstants::kEmptyStatePanelSpacingCount);

  // Split panel: left = example searches (by category), right = recent searches (table)
  RenderSplitPanel(state, settings, actions, window_width, is_index_building);
}

}  // namespace ui
