/**
 * @file UIRenderer.cpp
 * @brief Implementation of UIRenderer coordinator for UI rendering
 */

#include "ui/UIRenderer.h"

#include <GLFW/glfw3.h>
#include <string>

#include "api/GeminiApiUtils.h"
#include "core/Settings.h"
#include "core/Version.h"
#include "filters/TimeFilterUtils.h"
#include "gui/GuiState.h"
#include "gui/ImGuiUtils.h"
#include "gui/UIActions.h"
#include "imgui.h"
#include "index/FileIndex.h"
#include "search/SearchWorker.h"
#include "ui/EmptyState.h"
#include "ui/FilterPanel.h"
#include "ui/HelpWindow.h"
#include "ui/IconsFontAwesome.h"
#include "ui/LayoutConstants.h"
#include "ui/MetricsWindow.h"
#include "ui/Popups.h"
#include "ui/ResultsTable.h"
#include "ui/SearchControls.h"
#include "ui/SearchHelpWindow.h"
#include "ui/SearchInputs.h"
#include "ui/SettingsWindow.h"
#include "ui/StatusBar.h"
#include "ui/StoppingState.h"
#include "ui/Theme.h"
#include "usn/UsnMonitor.h"
#include "utils/ThreadPool.h"

namespace ui {

void UIRenderer::RenderMainWindow(const RenderMainWindowContext& context) {
  // Extract parameters from context for readability
  GuiState& state = context.state;
  ui::UIActions* actions = context.actions;
  AppSettings& settings = context.settings;
  FileIndex& file_index = context.file_index;
  ThreadPool& thread_pool = context.thread_pool;
  const UsnMonitor* monitor = context.monitor;
  const SearchWorker& search_worker = context.search_worker;
  NativeWindowHandle native_window = context.native_window;
  GLFWwindow* glfw_window = context.glfw_window;
  std::atomic<bool>& show_settings = context.show_settings;
  std::atomic<bool>& show_metrics = context.show_metrics;
  const bool is_index_building = context.is_index_building;
  const bool metrics_available = context.metrics_available;
  // Main window setup
  const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(main_viewport->WorkPos);
  ImGui::SetNextWindowSize(main_viewport->WorkSize);
  ImGui::SetNextWindowViewport(main_viewport->ID);
  const auto main_flags = static_cast<int>(
    static_cast<unsigned>(ImGuiWindowFlags_NoTitleBar) |
    static_cast<unsigned>(ImGuiWindowFlags_NoResize) |
    static_cast<unsigned>(ImGuiWindowFlags_NoMove) |
    static_cast<unsigned>(ImGuiWindowFlags_NoCollapse) |
    static_cast<unsigned>(ImGuiWindowFlags_NoScrollbar));
  ImGui::Begin(APP_DISPLAY_NAME, nullptr, main_flags);

  // SECTION 0: Application Controls (UI Mode, Settings, Metrics, Help)
  ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kToolbarVerticalPadding));
  FilterPanel::RenderApplicationControls(state, show_settings, show_metrics, metrics_available, settings, context.show_test_engine_window);
  ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kToolbarVerticalPadding));

  // Reserve space at the bottom for the non-scrollable footer area:
  // - Saved Searches row (label + combo + buttons)
  // - Status bar row
  // Using a child window with a negative height keeps this footer area always visible
  // even when a vertical scrollbar appears for the main content.
  const float footer_reserved_height =
    (settings.uiMode == AppSettings::UIMode::Minimalistic)
      ? ImGui::GetFrameHeightWithSpacing() * LayoutConstants::kFooterHeightMultiplierStatusOnly
      : ImGui::GetFrameHeightWithSpacing() * LayoutConstants::kFooterHeightMultiplierWithSavedSearches;
  ImGui::BeginChild("MainContentRegion", ImVec2(0.0F, -footer_reserved_height),
                    ImGuiChildFlags_None, ImGuiWindowFlags_None);

  // Gap between toolbar and first section; when busy, draw progress bar overlapping this space.
  const ImVec2 gap_min = ImGui::GetCursorScreenPos();
  const float gap_w = ImGui::GetWindowSize().x;
  ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kSectionSpacing));
  const ImVec2 gap_max(gap_min.x + gap_w, gap_min.y + LayoutConstants::kSectionSpacing);
  StatusBar::RenderBusyProgressBarInRect(state, search_worker, gap_min, gap_max);

  if (settings.uiMode == AppSettings::UIMode::Minimalistic) {
    // MINIMALISTIC UI: Only path search input
    SearchInputs::Render(state, actions, is_index_building, settings, glfw_window);
  } else {
    // SECTION 1: Manual Search (collapsible - Quick Filters + Search inputs)
    bool is_expanded = false;
    RenderManualSearchHeader(state, is_expanded);
    RenderManualSearchContent(state, actions, monitor, glfw_window, is_expanded, is_index_building,
                              settings);

    ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kSectionSpacing));

    // SECTION 2: AI-Assisted Search (collapsible)
    RenderAISearchSection(context);

    ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kSectionSpacing));

    // SECTION 3: Active Filter Indicators
    FilterPanel::RenderActiveFilterIndicators(state);
    ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kSectionSpacing));
  }

  // Show stopping state if window is closing
  if (glfwWindowShouldClose(glfw_window) != 0) {
    StoppingState::Render();
  } else if (!state.searchActive && state.searchResults.empty()) {
    // Show placeholder when ResultsTable::Render won't render the table
    EmptyState::Render(state, actions, settings, is_index_building);
  } else {
    // Always call ResultsTable::Render - it handles its own conditions internally
    ResultsTable::Render(state, native_window, glfw_window, thread_pool, file_index,
                         context.aggregator,
                         settings.showPathHierarchyIndentation);
  }

  ImGui::EndChild();

  // Saved Searches section (fixed at bottom, above status bar - not in scrollable area)
  // This is a utility/control feature that should always be visible, similar to the status bar
  if (settings.uiMode != AppSettings::UIMode::Minimalistic) {
    ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kSectionSpacing));
    ImGui::Separator();
    FilterPanel::RenderSavedSearches(state, settings, actions, is_index_building);
  }

  // Render popups (modal dialogs, can be rendered outside scrollable area)
  // Popup state management is handled inside each Render function
  Popups::RenderRegexGeneratorPopup(state.pathInput.Data(), SearchInputField::MaxLength(), state);
  Popups::RenderRegexGeneratorPopupFilename(state.filenameInput.Data(),
                                            SearchInputField::MaxLength(), state);
  Popups::RenderSavedSearchPopups(state, settings);

  // Render status bar
  StatusBar::Render(state, search_worker, monitor, file_index,
#ifdef _WIN32
                    settings.monitoredVolume
#else
                    std::string_view{}
#endif  // _WIN32
  );

  ImGui::End();
}

void UIRenderer::RenderFloatingWindows(const RenderFloatingWindowsContext& context) {
  // Extract parameters from context for readability
  GuiState& state = context.state;
  ui::UIActions* actions = context.actions;
  AppSettings& settings = context.settings;
  FileIndex& file_index = context.file_index;
  UsnMonitor* const monitor = context.monitor;
  SearchWorker& search_worker = context.search_worker;
  GLFWwindow* glfw_window = context.glfw_window;
  std::atomic<bool>& show_settings = context.show_settings;
  std::atomic<bool>& show_metrics = context.show_metrics;
  const bool metrics_available = context.metrics_available;
  // Render floating windows
  // Use local bools for compatibility with SettingsWindow/MetricsWindow which expect bool*
  bool show_settings_val = show_settings.load();
  bool show_metrics_val = show_metrics.load();
  if (show_settings_val) {
    SettingsWindow::Render(&show_settings_val, settings, file_index, actions, glfw_window);
    show_settings.store(show_settings_val);
  }
  if (metrics_available && show_metrics_val) {
    MetricsWindow::Render(&show_metrics_val, monitor, &search_worker, file_index);
    show_metrics.store(show_metrics_val);
  }
  if (state.showHelpWindow) {
    HelpWindow::Render(&state.showHelpWindow, &state.memory_bytes_);
  }
  if (state.showSearchHelpWindow) {
    SearchHelpWindow::Render(&state.showSearchHelpWindow);
  }
}

namespace {

// Draws an accent bar on the left edge of the last item (CollapsingHeader).
// Must be called after CollapsingHeader() so the bar is drawn on top and visible.
// Uses 4px width for visibility across DPI scales.
void DrawSectionHeaderAccentBar() {
  constexpr float kSectionBarWidth = 4.0F;
  const ImVec2 rect_min = ImGui::GetItemRectMin();
  const ImVec2 rect_max = ImGui::GetItemRectMax();
  const ImVec2 bar_max(rect_min.x + kSectionBarWidth, rect_max.y);
  ImGui::GetWindowDrawList()->AddRectFilled(
      rect_min,
      bar_max,
      ImGui::ColorConvertFloat4ToU32(Theme::Colors::Accent));
}

}  // namespace

int UIRenderer::CountActiveFilters(const GuiState& state) {
  int count = 0;
  if (FilterPanel::IsExtensionFilterActive(state)) {
    count++;
  }
  if (FilterPanel::IsFilenameFilterActive(state)) {
    count++;
  }
  if (FilterPanel::IsPathFilterActive(state)) {
    count++;
  }
  if (state.foldersOnly) {
    count++;
  }
  if (state.timeFilter != TimeFilter::None) {
    count++;
  }
  if (state.sizeFilter != SizeFilter::None) {
    count++;
  }
  return count;
}

void UIRenderer::RenderManualSearchHeader(GuiState& state, bool& is_expanded) {
  // Accent-tinted headers + TextStrong for section title
  const ImVec4 header_color = Theme::AccentTint(Theme::HeaderAlphas::Base);
  const ImVec4 header_hovered_color = Theme::AccentTint(Theme::HeaderAlphas::Hover);
  const ImVec4 header_active_color = Theme::AccentTint(Theme::HeaderAlphas::Active);

  const int active_filter_count = CountActiveFilters(state);
  std::string header_text = "Manual Search";
  if (active_filter_count > 0) {
    header_text += " (" + std::to_string(active_filter_count) + " filters active)";
  }
  std::string header_label = header_text + "##manual_search";

  ImGui::SetNextItemOpen(state.manualSearchExpanded, ImGuiCond_Always);

  ImGui::PushStyleColor(ImGuiCol_Header, header_color);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, header_hovered_color);
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, header_active_color);
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextStrong);

  is_expanded = ImGui::CollapsingHeader(header_label.c_str(), ImGuiTreeNodeFlags_None);

  ImGui::PopStyleColor(4);
  DrawSectionHeaderAccentBar();  // After header so bar draws on top
  state.manualSearchExpanded = is_expanded;

  if (!is_expanded) {
    ImGui::TextDisabled(
      "[TIP] For precise control: specify path patterns, extensions, and filters directly");
  }
}

void UIRenderer::RenderManualSearchContent(GuiState& state, ui::UIActions* actions,
                                           const UsnMonitor* monitor, GLFWwindow* glfw_window,
                                           bool is_expanded, bool is_index_building,
                                           const AppSettings& settings) {
  if (!is_expanded) {
    return;
  }

  ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kSectionSpacing));
  SearchInputs::Render(state, actions, is_index_building, settings, glfw_window);
  SearchControls::Render(state, actions, is_index_building);

  if (state.showQuickFilters) {
    FilterPanel::RenderQuickFilters(state, monitor);
    FilterPanel::RenderTimeQuickFilters(state);
    FilterPanel::RenderSizeQuickFilters(state);
  }
}

void UIRenderer::RenderAISearchSection(const RenderMainWindowContext& context) {
  GuiState& state = context.state;
  AppSettings& settings = context.settings;
  GLFWwindow* glfw_window = context.glfw_window;
  std::atomic<bool>& show_settings = context.show_settings;
  std::atomic<bool>& show_metrics = context.show_metrics;
  const bool is_index_building = context.is_index_building;
  const bool metrics_available = context.metrics_available;

  // Accent-tinted headers (similar to Search button) for visual consistency
  const ImVec4 header_color = Theme::AccentTint(Theme::HeaderAlphas::Base);
  const ImVec4 header_hovered_color = Theme::AccentTint(Theme::HeaderAlphas::Hover);
  const ImVec4 header_active_color = Theme::AccentTint(Theme::HeaderAlphas::Active);

  const std::string api_key = gemini_api_utils::GetGeminiApiKeyFromEnv();
  const bool api_key_set = !api_key.empty();

  std::string ai_header_text = "AI-Assisted Search";
  if (api_key_set) {
    ai_header_text += " (API key configured)";
  } else {
    ai_header_text += " (Generate/Paste Prompt workflow)";
  }
  std::string ai_header_label = ai_header_text + "##ai_search";

  ImGui::SetNextItemOpen(state.aiSearchExpanded, ImGuiCond_Always);

  ImGui::PushStyleColor(ImGuiCol_Header, header_color);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, header_hovered_color);
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, header_active_color);
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextStrong);

  const bool ai_is_expanded = ImGui::CollapsingHeader(ai_header_label.c_str(), ImGuiTreeNodeFlags_None);

  ImGui::PopStyleColor(4);
  DrawSectionHeaderAccentBar();  // After header so bar draws on top
  state.aiSearchExpanded = ai_is_expanded;

  if (ai_is_expanded) {
    if (settings.showWorkflowHint && api_key_set) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextWrapped("[TIP] Start here: Describe what you're looking for in natural language. "
                         "For manual control, use Manual Search above.");
      ImGui::PopStyleColor();

      ImGui::SameLine();
      if (ImGui::SmallButton("Got it")) {
        settings.showWorkflowHint = false;
        SaveSettings(settings);
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Don't show again")) {
        settings.showWorkflowHint = false;
        SaveSettings(settings);
      }
      ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kBlockPadding));
    }

    // Use local bools for SearchInputs which expects bool* pointers
    bool show_settings_val = show_settings.load();
    bool show_metrics_val = show_metrics.load();
    SearchInputs::RenderAISearch(state, glfw_window, context.actions, is_index_building,
                                 &show_settings_val, &show_metrics_val, metrics_available);
    show_settings.store(show_settings_val);
    show_metrics.store(show_metrics_val);
  }
}

}  // namespace ui
