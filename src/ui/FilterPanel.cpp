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

#include "filters/SizeFilterUtils.h"
#include "gui/GuiState.h"
#include "gui/ImGuiUtils.h"
#include "gui/UIActions.h"
#include "path/PathUtils.h"
#include "search/SearchInputField.h"
#include "search/SearchTypes.h"
#include "ui/IconsFontAwesome.h"
#include "ui/LayoutConstants.h"
#include "ui/Theme.h"
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
    Theme::PushAccentButtonStyle();
  }

  if (ImGui::Button(label)) {
    callback();
  }

  if (is_active) {
    Theme::PopAccentButtonStyle();
  }
}

// ============================================================================
// Public Methods
// ============================================================================

void FilterPanel::RenderQuickFilters(GuiState& state) {
  ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kBlockPadding));
  ImGui::AlignTextToFramePadding();
  ImGui::Text(ICON_FA_FILTER " Quick Filters:");
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
  ImGui::Text("Last Modified:");
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
  // intentionally
  ImGui::Text("File Size:");
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

  Theme::PushAccentButtonStyle();

  // Use snprintf to avoid string allocation (temporary formatting buffer, not performance-critical)
  std::string label_with_x(256, '\0');
  if (const int written = std::snprintf(label_with_x.data(), label_with_x.size(), "%s ×", label);
      written > 0 && static_cast<size_t>(written) < label_with_x.size()) {  // NOLINT(bugprone-branch-clone) - first branch: exact length; else branch: truncation
    label_with_x.resize(static_cast<size_t>(written));
  } else if (written >= 0) {
    label_with_x.resize(label_with_x.size() - 1);  // Truncation occurred
  }
  if (ImGui::SmallButton(label_with_x.c_str())) {
    on_clear();
  }

  Theme::PopAccentButtonStyle();
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
    ImGui::TextDisabled("Active Filters:");
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

}  // namespace ui
