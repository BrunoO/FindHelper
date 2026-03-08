/**
 * @file ui/FilterPanel.cpp
 * @brief Implementation of filter panel rendering component
 */

#include "ui/FilterPanel.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>

#include "core/Settings.h"
#include "filters/SizeFilterUtils.h"
#include "filters/TimeFilterUtils.h"
#include "gui/GuiState.h"
#include "gui/ImGuiUtils.h"
#include "gui/UIActions.h"
#include "path/PathUtils.h"
#include "search/SearchInputField.h"
#include "search/SearchTypes.h"
#include "ui/IconsFontAwesome.h"
#include "ui/LayoutConstants.h"
#include "ui/Theme.h"
#include "usn/UsnMonitor.h"
#include "utils/StringUtils.h"

#include "imgui.h"

namespace ui {

// ============================================================================
// Constants
// ============================================================================

/**
 * @brief Default extensions string (empty, meaning no extension filter)
 */
static constexpr const char* kDefaultExtensions = "";

// Use Theme colors for active filter buttons
// Note: We access them via inline getters/definitions in Render functions to ensure initialized
// state but we can also use Theme::Colors constants directly as they are constexpr. Except text
// color might need high contrast against Accent background. Theme::Colors::Text is light, Accent is
// medium-dark, usually good.

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Sets a quick filter by populating extension input and clearing filename
 *
 * Used by quick filter buttons (Documents, Executables, Videos, Pictures) to
 * quickly set extension filters without manual typing.
 *
 * @param extensions Semicolon-separated list of extensions (e.g., "pdf;doc;docx")
 * @param extension_input Buffer for extension input field
 * @param extension_input_size Size of extension input buffer
 * @param filename_input Buffer for filename input field
 * @param filename_input_size Size of filename input buffer
 * @param folders_only Reference to folders-only checkbox state (set to false)
 * @param mark_input_changed Callback to mark input as changed (triggers search)
 */
static void SetQuickFilter(const char* extensions, SearchInputField& extension_input,
  SearchInputField& filename_input, bool& folders_only,
  const std::function<void()>& mark_input_changed) {  // NOSONAR(cpp:S5213) - std::function type erasure for callbacks; template would break API
  extension_input.SetValue(extensions);
  filename_input.Clear();
  folders_only = false;
  mark_input_changed();
}

/**
 * @brief Quick filter preset definitions
 *
 * Maps quick filter button names to their extension strings.
 * Used for detecting which quick filter is currently active.
 */
struct QuickFilterPreset {
  const char* name;
  const char* extensions;
};

static constexpr std::array<QuickFilterPreset, 4> kQuickFilterPresets = {
  {{"Documents", "rtf;pdf;doc;docx;xls;xlsx;ppt;pptx;odt;ods;odp"},
   {"Executables", "exe;bat;cmd;ps1;msi;com"},
   {"Videos", "mp4;avi;mkv;mov;wmv;flv;webm;m4v"},
   {"Pictures", "jpg;jpeg;png;gif;bmp;tif;tiff;svg;webp;heic"}}};

/**
 * @brief Checks if the current extension input matches a quick filter preset
 *
 * Uses static cache to avoid string allocation and comparison loop when extension
 * input hasn't changed between frames.
 *
 * @param extension_input Current extension input field
 * @return Pointer to matching preset name, or nullptr if no match
 */
static const char* GetActiveQuickFilterName(const SearchInputField& extension_input) {
  // Cache the last extension value and result to avoid repeated allocations
  static std::string last_extension_value;
  static const char* cached_result = nullptr;

  // Get current extension value
  const std::string current_extensions = extension_input.AsString();

  // Return cached result if extension hasn't changed
  if (current_extensions == last_extension_value) {
    return cached_result;
  }

  // Extension changed - update cache and find matching preset
  last_extension_value = current_extensions;
  cached_result = nullptr;

  for (const auto& preset : kQuickFilterPresets) {
    if (current_extensions == preset.extensions) {
      cached_result = preset.name;
      return cached_result;
    }
  }

  return nullptr;
}

/**
 * @brief Checks if a filter name matches the active filter name
 *
 * Uses std::string_view for safe, efficient string comparison.
 * Avoids C-style string functions while maintaining performance.
 *
 * @param active_name Currently active filter name (may be nullptr)
 * @param filter_name Filter name to check against
 * @return true if active_name matches filter_name, false otherwise
 */
static bool IsActiveFilter(const char* active_name, const char* filter_name) {
  if (active_name == nullptr || filter_name == nullptr) {
    return false;
  }
  return std::string_view(active_name) == std::string_view(filter_name);
}

/**
 * @brief Renders a button with active state highlighting if applicable
 *
 * If the button represents an active filter, it will be highlighted with a
 * blue color scheme similar to filter badges.
 *
 * @param label Button label text
 * @param is_active Whether this button represents an active filter
 * @param callback Function to call when button is clicked
 */
static void RenderQuickFilterButton(
  const char* label, bool is_active,
  const std::function<void()>& callback) {  // NOSONAR(cpp:S1238, cpp:S5213) - std::function provides type
                                            // erasure for callbacks, template would break API
  if (is_active) {
    // Highlight active button with Accent color
    ImGui::PushStyleColor(ImGuiCol_Button, Theme::Colors::Accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Colors::AccentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Colors::AccentActive);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextOnAccent);  // Dark text on accent
  }

  if (ImGui::Button(label)) {
    callback();
  }

  if (is_active) {
    ImGui::PopStyleColor(4);
  }
}

// --- Helpers for RenderApplicationControls (reduce cognitive complexity) ---

static const char* GetUIModeButtonLabel(AppSettings::UIMode mode) {
  if (mode == AppSettings::UIMode::Simplified) {
    return ICON_FA_EXPAND " UI: Simplified";
  }
  if (mode == AppSettings::UIMode::Minimalistic) {
    return ICON_FA_EXPAND " UI: Minimalistic";
  }
  return ICON_FA_EXPAND " UI: Full";
}

static void CycleUIModeAndSave(AppSettings& settings) {
  if (settings.uiMode == AppSettings::UIMode::Full) {
    settings.uiMode = AppSettings::UIMode::Simplified;
  } else if (settings.uiMode == AppSettings::UIMode::Simplified) {
    settings.uiMode = AppSettings::UIMode::Minimalistic;
  } else {
    settings.uiMode = AppSettings::UIMode::Full;
  }
  SaveSettings(settings);
}

static float ComputeApplicationControlsTotalWidth(
  const char* settings_label, const char* mode_label, float help_width,
  bool metrics_available, bool show_metrics_val,
  const bool* show_test_engine_window,  // non-null when Test Engine build enabled (used only to decide width)
  const ImGuiStyle& style) {
  const float settings_width = ComputeButtonWidth(settings_label);
  const float mode_width = ComputeButtonWidth(mode_label);
  float total = settings_width + mode_width + help_width + (style.ItemSpacing.x * 2.0F);
  if (show_test_engine_window != nullptr) {
    total += ComputeButtonWidth("Test Engine") + style.ItemSpacing.x;
  }
  if (metrics_available) {
    const char* metrics_label =
      show_metrics_val ? ICON_FA_CHART_BAR " Hide Metrics" : ICON_FA_CHART_BAR " Metrics";
    total += ComputeButtonWidth(metrics_label) + style.ItemSpacing.x;
  }
  return total;
}

static void ApplyRightAlignPosition(float total_group_width) {
  const ImGuiStyle& style = ImGui::GetStyle();
  const float window_right_edge = ImGui::GetWindowContentRegionMax().x;
  const float right_margin = style.WindowPadding.x;
  const float target_x = window_right_edge - total_group_width - right_margin;
  if ((target_x + total_group_width) <= (window_right_edge - right_margin)) {
    ImGui::SetCursorPosX(target_x);
  }
}

static void RenderModeCycleButton(AppSettings& settings) {
  const char* mode_label = GetUIModeButtonLabel(settings.uiMode);
  ImGui::SetNextItemAllowOverlap();
  if (ImGui::SmallButton(mode_label)) {
    CycleUIModeAndSave(settings);
  }
  if (ImGui::IsItemHovered()) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API uses varargs
    ImGui::SetTooltip("Cycle UI Mode (Full -> Simplified -> Minimalistic)");
  }
}

// Stable ID ##toolbar_settings for tests. See ImGuiTestEngineTests settings_window_open.
static void RenderSettingsButton(bool show_settings_val, std::atomic<bool>& show_settings) {
  const char* settings_label =
    show_settings_val ? ICON_FA_GEAR " Hide Settings##toolbar_settings"
                     : ICON_FA_GEAR " Settings##toolbar_settings";
  ImGui::SameLine();
  ImGui::SetNextItemAllowOverlap();
  if (ImGui::SmallButton(settings_label)) {
    show_settings.store(!show_settings.load());
  }
}

// Stable ID ##toolbar_help for tests; avoids relying on visible label (icons/localization). See ImGuiTestEngineTests help_window_open.
static void RenderHelpButton(GuiState& state) {
  const char* help_label =
    state.showHelpWindow ? ICON_FA_BOOK_OPEN " Hide Help##toolbar_help"
                         : ICON_FA_BOOK_OPEN " Help##toolbar_help";
  ImGui::SameLine();
  ImGui::SetNextItemAllowOverlap();
  if (ImGui::SmallButton(help_label)) {
    state.showHelpWindow = !state.showHelpWindow;
  }
  if (ImGui::IsItemHovered()) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API uses varargs
    ImGui::SetTooltip("Keyboard shortcuts");
  }
}

static void RenderTestEngineButton(bool* show_test_engine_window) {
  ImGui::SameLine();
  ImGui::SetNextItemAllowOverlap();
  if (ImGui::SmallButton("Test Engine")) {
    *show_test_engine_window = true;
  }
  if (ImGui::IsItemHovered()) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API uses varargs
    ImGui::SetTooltip("Show ImGui Test Engine window (run UI tests)");
  }
}

static void RenderMetricsButton(std::atomic<bool>& show_metrics, bool metrics_available) {
  if (!metrics_available) {
    return;
  }
  const bool show_metrics_val = show_metrics.load();
  const char* metrics_label =
    show_metrics_val ? ICON_FA_CHART_BAR " Hide Metrics" : ICON_FA_CHART_BAR " Metrics";
  ImGui::SameLine();
  ImGui::SetNextItemAllowOverlap();
  if (ImGui::SmallButton(metrics_label)) {
    show_metrics.store(!show_metrics.load());
  }
}

// ============================================================================
// Public Methods
// ============================================================================

void FilterPanel::RenderApplicationControls(GuiState& state, std::atomic<bool>& show_settings,  // NOLINT(readability-identifier-naming) - PascalCase member function
                                            std::atomic<bool>& show_metrics, bool metrics_available,
                                            AppSettings& settings,
                                            bool* show_test_engine_window) {
  const bool show_settings_val = show_settings.load();
  const char* settings_label =
    show_settings_val ? ICON_FA_GEAR " Hide Settings" : ICON_FA_GEAR " Settings";
  const char* mode_label = GetUIModeButtonLabel(settings.uiMode);
  const float help_width =
    ComputeButtonWidth(state.showHelpWindow ? ICON_FA_BOOK_OPEN " Hide Help"
                                            : ICON_FA_BOOK_OPEN " Help");
  const bool show_metrics_val = metrics_available ? show_metrics.load() : false;
  const ImGuiStyle& style = ImGui::GetStyle();

  const float total_group_width = ComputeApplicationControlsTotalWidth(
    settings_label, mode_label, help_width, metrics_available, show_metrics_val,
    show_test_engine_window, style);
  ApplyRightAlignPosition(total_group_width);

  ImGui::PushStyleColor(ImGuiCol_Button, Theme::Colors::Surface);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Colors::SurfaceHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Colors::SurfaceActive);
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDim);

  RenderModeCycleButton(settings);
  RenderSettingsButton(show_settings_val, show_settings);
  RenderHelpButton(state);

  if (show_test_engine_window != nullptr) {
    RenderTestEngineButton(show_test_engine_window);
  }
  RenderMetricsButton(show_metrics, metrics_available);

  ImGui::PopStyleColor(4);
}

void FilterPanel::RenderQuickFilters(GuiState& state, [[maybe_unused]] const UsnMonitor* monitor) {
  ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kBlockPadding));
  ImGui::AlignTextToFramePadding();
  ImGui::Text(ICON_FA_FILTER " Quick Filters:");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
  ImGui::SameLine();

  auto mark_input_changed = [&state]() { state.MarkInputChanged(); };

  // Check which quick filter (if any) is currently active
  const char* active_filter_name = GetActiveQuickFilterName(state.extensionInput);

  // File type quick filters with active state highlighting
  RenderQuickFilterButton(
    "Documents", IsActiveFilter(active_filter_name, "Documents"),
    [&state, &mark_input_changed]() {  // NOSONAR(cpp:S3608) - Already explicit: captures state and
                                       // mark_input_changed
      SetQuickFilter("rtf;pdf;doc;docx;xls;xlsx;ppt;pptx;odt;ods;odp", state.extensionInput,
                     state.filenameInput, state.foldersOnly, mark_input_changed);
    });
  ImGui::SameLine();

  RenderQuickFilterButton(
    "Executables", IsActiveFilter(active_filter_name, "Executables"),
    [&state, &mark_input_changed]() {  // NOSONAR(cpp:S3608) - Already explicit: captures state and
                                       // mark_input_changed
      SetQuickFilter("exe;bat;cmd;ps1;msi;com", state.extensionInput, state.filenameInput,
                     state.foldersOnly, mark_input_changed);
    });
  ImGui::SameLine();

  RenderQuickFilterButton(
    "Videos", IsActiveFilter(active_filter_name, "Videos"),
    [&state, &mark_input_changed]() {  // NOSONAR(cpp:S3608) - Already explicit: captures state and
                                       // mark_input_changed
      SetQuickFilter("mp4;avi;mkv;mov;wmv;flv;webm;m4v", state.extensionInput, state.filenameInput,
                     state.foldersOnly, mark_input_changed);
    });
  ImGui::SameLine();

  RenderQuickFilterButton(
    "Pictures", IsActiveFilter(active_filter_name, "Pictures"),
    [&state, &mark_input_changed]() {  // NOSONAR(cpp:S3608) - Already explicit: captures state and
                                       // mark_input_changed
      SetQuickFilter("jpg;jpeg;png;gif;bmp;tif;tiff;svg;webp;heic", state.extensionInput,
                     state.filenameInput, state.foldersOnly, mark_input_changed);
    });
  ImGui::SameLine();

  // Location shortcuts (no active state - they set path, not extensions)
  if (ImGui::Button("Current User")) {
    const std::string user_home = path_utils::GetUserHomePath();
    state.pathInput.SetValue(user_home);
    mark_input_changed();
  }

  ImGui::SameLine();

  if (ImGui::Button("Desktop")) {
    const std::string desktop_path = path_utils::GetDesktopPath();
    state.pathInput.SetValue(desktop_path);
    mark_input_changed();
  }

  ImGui::SameLine();

  if (ImGui::Button("Downloads")) {
    const std::string downloads_path = path_utils::GetDownloadsPath();
    state.pathInput.SetValue(downloads_path);
    mark_input_changed();
  }

  ImGui::SameLine();

  if (ImGui::Button("Recycle Bin")) {
    const std::string trash_path = path_utils::GetTrashPath();
    state.pathInput.SetValue(trash_path);
    mark_input_changed();
  }

  ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kBlockPadding));
}

void FilterPanel::RenderTimeQuickFilters(GuiState& state) {
  ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kBlockPadding));
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Last Modified:");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
  ImGui::SameLine();

  RenderQuickFilterButton("Today", state.timeFilter == TimeFilter::Today, [&state]() {
    state.timeFilter = TimeFilter::Today;
  });  // NOSONAR(cpp:S3608) - Already explicit: captures state
  ImGui::SameLine();

  RenderQuickFilterButton("This Week", state.timeFilter == TimeFilter::ThisWeek, [&state]() {
    state.timeFilter = TimeFilter::ThisWeek;
  });  // NOSONAR(cpp:S3608) - Already explicit: captures state
  ImGui::SameLine();

  RenderQuickFilterButton("This Month", state.timeFilter == TimeFilter::ThisMonth, [&state]() {
    state.timeFilter = TimeFilter::ThisMonth;
  });  // NOSONAR(cpp:S3608) - Already explicit: captures state
  ImGui::SameLine();

  RenderQuickFilterButton("This Year", state.timeFilter == TimeFilter::ThisYear, [&state]() {
    state.timeFilter = TimeFilter::ThisYear;
  });  // NOSONAR(cpp:S3608) - Already explicit: captures state
  ImGui::SameLine();

  RenderQuickFilterButton("Older", state.timeFilter == TimeFilter::Older, [&state]() {
    state.timeFilter = TimeFilter::Older;
  });  // NOSONAR(cpp:S3608) - Already explicit: captures state
}

void FilterPanel::RenderSizeQuickFilters(GuiState& state) {
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API uses varargs
  // intentionally
  ImGui::Text("File Size:");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
  ImGui::SameLine();

  RenderQuickFilterButton("Empty", state.sizeFilter == SizeFilter::Empty,
                          [&state]() { state.sizeFilter = SizeFilter::Empty; });
  ImGui::SameLine();

  RenderQuickFilterButton("Tiny", state.sizeFilter == SizeFilter::Tiny,
                          [&state]() { state.sizeFilter = SizeFilter::Tiny; });
  ImGui::SameLine();

  RenderQuickFilterButton("Small", state.sizeFilter == SizeFilter::Small,
                          [&state]() { state.sizeFilter = SizeFilter::Small; });
  ImGui::SameLine();

  RenderQuickFilterButton("Medium", state.sizeFilter == SizeFilter::Medium,
                          [&state]() { state.sizeFilter = SizeFilter::Medium; });
  ImGui::SameLine();

  RenderQuickFilterButton("Large", state.sizeFilter == SizeFilter::Large,
                          [&state]() { state.sizeFilter = SizeFilter::Large; });
  ImGui::SameLine();

  RenderQuickFilterButton("Huge", state.sizeFilter == SizeFilter::Huge,
                          [&state]() { state.sizeFilter = SizeFilter::Huge; });
  ImGui::SameLine();

  RenderQuickFilterButton("Massive", state.sizeFilter == SizeFilter::Massive,
                          [&state]() { state.sizeFilter = SizeFilter::Massive; });

  ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kBlockPadding));
}

void FilterPanel::RenderFilterBadge(
  const char* label, bool is_active,
  const std::function<void()>& on_clear) {  // NOSONAR(cpp:S1238, cpp:S5213) - std::function provides type
                                             // erasure for callbacks, template would break API
  if (!is_active) {
    return;
  }

  ImGui::PushStyleColor(ImGuiCol_Button, Theme::Colors::Accent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Colors::AccentHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Colors::AccentActive);
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextOnAccent);

  // Use snprintf to avoid string allocation (temporary formatting buffer, not performance-critical)
  std::string label_with_x(256, '\0');
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - snprintf for fixed buffer formatting
  if (const int written = std::snprintf(label_with_x.data(), label_with_x.size(), "%s ×", label);  // NOLINT(cppcoreguidelines-init-variables) - initialized by snprintf return
      written > 0 && static_cast<size_t>(written) < label_with_x.size()) {  // NOLINT(bugprone-branch-clone) - first branch: exact length; else branch: truncation
    label_with_x.resize(static_cast<size_t>(written));
  } else if (written >= 0) {
    label_with_x.resize(label_with_x.size() - 1);  // Truncation occurred
  }
  if (ImGui::SmallButton(label_with_x.c_str())) {
    on_clear();
  }

  ImGui::PopStyleColor(4);
  ImGui::SameLine();
}

void FilterPanel::RenderActiveFilterIndicators(GuiState& state) {
  auto mark_input_changed = [&state]() { state.MarkInputChanged(); };

  // Helper function to wrap filter-clearing actions with mark_input_changed
  auto clear_and_update = [&mark_input_changed](
      const std::function<void()>& clear_action) {  // NOSONAR(cpp:S5213) - std::function type erasure; template would break API
    return [&mark_input_changed, clear_action]() {  // capture by value so closure outlives parameter
      clear_action();
      mark_input_changed();
    };
  };

  // Check if any filters are active
  const bool has_active_filters = IsExtensionFilterActive(state) || IsFilenameFilterActive(state) ||
                                  IsPathFilterActive(state) || state.foldersOnly ||
                                  state.timeFilter != TimeFilter::None ||
                                  state.sizeFilter != SizeFilter::None;

  if (has_active_filters) {
    ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kBlockPadding));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Active Filters:");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
    ImGui::SameLine();

    // Extension filter badge
    if (IsExtensionFilterActive(state)) {
      RenderFilterBadge("Extensions", true, clear_and_update([&state]() {
                          state.extensionInput.SetValue(kDefaultExtensions);
                        }));
    }

    // Name filter badge (applies to files and folders)
    if (IsFilenameFilterActive(state)) {
      RenderFilterBadge("Name", true,
                        clear_and_update([&state]() { state.filenameInput.Clear(); }));
    }

    // Path filter badge
    if (IsPathFilterActive(state)) {
      RenderFilterBadge("Path", true, clear_and_update([&state]() { state.pathInput.Clear(); }));
    }

    // Folders Only badge
    if (state.foldersOnly) {
      RenderFilterBadge("Folders Only", true,
                        clear_and_update([&state]() { state.foldersOnly = false; }));
    }

    // Time filter badge
    if (state.timeFilter != TimeFilter::None) {
      static constexpr std::array<const char*, 5> time_filter_labels = {  // NOLINT(readability-identifier-naming) - snake_case for local constant
        "Last Modified: Today", "Last Modified: This Week", "Last Modified: This Month",
        "Last Modified: This Year", "Last Modified: Older"};
      const int filter_index = static_cast<int>(state.timeFilter) - 1;  // -1 because None is 0
      if (filter_index >= 0 && filter_index < static_cast<int>(time_filter_labels.size())) {
        // Use static const char* for label (no runtime formatting needed)
        const char* label = time_filter_labels.at(static_cast<size_t>(filter_index));
        RenderFilterBadge(label, true, [&state]() { state.timeFilter = TimeFilter::None; });
      }
    }

    // Size filter badge
    if (state.sizeFilter != SizeFilter::None) {
      const char* size_label = SizeFilterDisplayLabel(state.sizeFilter);
      // SizeFilterDisplayLabel returns static const char*, no formatting needed
      RenderFilterBadge(size_label, true, [&state]() { state.sizeFilter = SizeFilter::None; });
    }

    ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kBlockPadding));
  }
}

void FilterPanel::RenderSavedSearches(GuiState& state, const AppSettings& settings_state,
                                      UIActions* actions, bool is_index_building) {
  ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kBlockPadding));
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("Saved Searches:");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
  ImGui::SameLine();

  static int selected_saved_search = -1;
  if (!settings_state.savedSearches.empty()) {
    const auto& saved_list = settings_state.savedSearches;
    if (selected_saved_search >= static_cast<int>(saved_list.size()) || selected_saved_search < 0) {
      selected_saved_search = -1;
    }

    // NOLINTNEXTLINE(readability-magic-numbers) - Combo width in pixels is self-explanatory
    ImGui::SetNextItemWidth(260.0F);
    RenderSavedSearchCombo(saved_list, selected_saved_search, state, actions, is_index_building);
  } else {
    ImGui::BeginDisabled();
    // NOLINTNEXTLINE(readability-magic-numbers) - Combo width in pixels is self-explanatory
    ImGui::SetNextItemWidth(160.0F);
    int dummy_index = -1;
    ImGui::Combo("##SavedSearches", &dummy_index, static_cast<const char* const*>(nullptr), 0);
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  if (ImGui::SmallButton(ICON_FA_SEARCH_PLUS " Save Search")) {
    state.openSaveSearchPopup = true;
  }

  ImGui::SameLine();
  if (const bool can_delete_saved = !settings_state.savedSearches.empty() && selected_saved_search >= 0 &&
        selected_saved_search < static_cast<int>(settings_state.savedSearches.size());
      !can_delete_saved) {
    ImGui::BeginDisabled();
    if (ImGui::SmallButton(ICON_FA_TRASH " Delete")) {
      state.deleteSavedSearchIndex = selected_saved_search;
      ImGui::OpenPopup("DeleteSavedSearchPopup");
    }
    ImGui::EndDisabled();
  } else if (ImGui::SmallButton(ICON_FA_TRASH " Delete")) {
    state.deleteSavedSearchIndex = selected_saved_search;
    ImGui::OpenPopup("DeleteSavedSearchPopup");
  }

  ImGui::SameLine();
  const size_t indexed_file_count =
    (actions != nullptr) ? actions->GetIndexedFileCount() : 0U;
  const bool can_show_all =
    actions != nullptr && !is_index_building && indexed_file_count > 0U;
  if (!can_show_all) {
    ImGui::BeginDisabled();
  }
  if (ImGui::SmallButton(ICON_FA_LIST " Show all indexed items") && actions != nullptr) {
    state.ApplyShowAllPreset();
    actions->TriggerManualSearch(state);
  }
  if (!can_show_all) {
    ImGui::EndDisabled();
  }

  ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kBlockPadding));
}

// Generic helper function to check if a SearchInputField is active
// Optionally checks against a default value (for extension filter)
static bool IsInputFieldActive(const SearchInputField& field, const char* default_value = nullptr) {
  if (field.IsEmpty()) {
    return false;
  }
  if (default_value != nullptr) {
    return field.AsString() != default_value;
  }
  return true;
}

[[nodiscard]] bool FilterPanel::IsExtensionFilterActive(const GuiState& state) {
  return IsInputFieldActive(state.extensionInput, kDefaultExtensions);
}

[[nodiscard]] bool FilterPanel::IsFilenameFilterActive(const GuiState& state) {
  return IsInputFieldActive(state.filenameInput);
}

[[nodiscard]] bool FilterPanel::IsPathFilterActive(const GuiState& state) {
  return IsInputFieldActive(state.pathInput);
}

namespace {
// Helper function to handle saved search selection (reduces nesting in RenderSavedSearchCombo)
void HandleSavedSearchSelection(const SavedSearch& saved_search, GuiState& state,
                                UIActions* actions, bool is_index_building) {
  if (actions != nullptr) {
    actions->ApplySavedSearch(state, saved_search);
    // Trigger search if index is not building
    if (!is_index_building) {
      actions->TriggerManualSearch(state);
    }
  }
}
}  // namespace

void FilterPanel::RenderSavedSearchCombo(const std::vector<SavedSearch>& saved_list,
                                         int& selected_saved_search, GuiState& state,
                                         UIActions* actions, bool is_index_building) {
  const char* current_label =
    (selected_saved_search >= 0)
      ? saved_list[static_cast<std::size_t>(selected_saved_search)].name.c_str()
      : "<Select or save search>";

  if (ImGui::BeginCombo("##SavedSearches", current_label)) {
    for (int i = 0; i < static_cast<int>(saved_list.size()); ++i) {
      const bool is_selected = (i == selected_saved_search);
      if (ImGui::Selectable(saved_list[static_cast<std::size_t>(i)].name.c_str(), is_selected)) {
        selected_saved_search = i;
        HandleSavedSearchSelection(saved_list[static_cast<std::size_t>(i)], state, actions,
                                   is_index_building);
      }
      if (is_selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
}

}  // namespace ui
