/**
 * @file UIRenderer.cpp
 * @brief Implementation of UIRenderer coordinator for UI rendering
 */

#include "ui/UIRenderer.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <atomic>
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
#include "ui/WelcomePanel.h"
#include "usn/UsnMonitor.h"
#include "utils/Logger.h"
#include "utils/ThreadPool.h"

namespace ui {

// ============================================================================
// Application Controls (toolbar row: UI mode, Settings, Metrics, Help)
// Owned by UIRenderer because they are application-level controls, not filters.
// ============================================================================

static const char* GetUIModeButtonLabel(AppSettings::UIMode mode) {
  if (mode == AppSettings::UIMode::Simplified) {
    return ICON_FA_EXPAND " UI: Simplified";
  }
  if (mode == AppSettings::UIMode::Minimalistic) {
    return ICON_FA_EXPAND " UI: Minimalistic";
  }
  return ICON_FA_EXPAND " UI: Full";
}

static void DismissWorkflowHint(AppSettings& settings) {
  settings.showWorkflowHint = false;
  if (!SaveSettings(settings)) {
    settings.showWorkflowHint = true;  // Revert so hint reappears on next launch
    LOG_ERROR("Failed to persist workflow hint dismissal");
  }
}

static void CycleUIModeAndSave(AppSettings& settings) {
  const AppSettings::UIMode previous_mode = settings.uiMode;
  if (settings.uiMode == AppSettings::UIMode::Full) {
    settings.uiMode = AppSettings::UIMode::Simplified;
  } else if (settings.uiMode == AppSettings::UIMode::Simplified) {
    settings.uiMode = AppSettings::UIMode::Minimalistic;
  } else {
    settings.uiMode = AppSettings::UIMode::Full;
  }
  if (!SaveSettings(settings)) {
    settings.uiMode = previous_mode;  // Revert in-memory state so UI stays consistent with disk
    LOG_ERROR("Failed to persist UI mode change");
  }
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

static void RenderModeCycleButton(AppSettings& settings) {
  const char* mode_label = GetUIModeButtonLabel(settings.uiMode);
  ImGui::SetNextItemAllowOverlap();
  if (ImGui::SmallButton(mode_label)) {
    CycleUIModeAndSave(settings);
  }
  if (ImGui::IsItemHovered()) {
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

static void RenderApplicationControls(GuiState& state, std::atomic<bool>& show_settings,
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
  AlignGroupRight(total_group_width);

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

void UIRenderer::RenderMainWindow(const RenderMainWindowContext& context) {
  // NOLINTBEGIN(misc-misplaced-const) - NativeWindowHandle pointer typedef gives void*const in structured binding; correct
  const auto& [state, actions, settings, file_index, thread_pool, monitor, search_worker,
               aggregator, native_window, glfw_window, show_settings, show_metrics,
               is_index_building, metrics_available, seconds_until_recrawl,
               show_test_engine_window] = context;
  // NOLINTEND(misc-misplaced-const)
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
  RenderApplicationControls(state, show_settings, show_metrics, metrics_available, settings, show_test_engine_window);
  ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kToolbarVerticalPadding));

  // Reserve space at the bottom for the non-scrollable footer area:
  // - Status bar row
  // Using a child window with a negative height keeps this footer area always visible
  // even when a vertical scrollbar appears for the main content.
  const float frame_height_with_spacing = ImGui::GetFrameHeightWithSpacing();
  const float footer_reserved_height =
    frame_height_with_spacing * LayoutConstants::kFooterHeightMultiplierStatusOnly;
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
    RenderManualSearchContent(state, actions, glfw_window, is_expanded, is_index_building,
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
  } else if (!state.searchActive && state.result_pool_->Results().empty()) {
    // Show placeholder when ResultsTable::Render won't render the table
    WelcomePanel::Render(state, actions, settings, is_index_building);
  } else {
    // Always call ResultsTable::Render - it handles its own conditions internally
    ResultsTable::Render(state, native_window, glfw_window, thread_pool, file_index,
                         aggregator,
                         settings.showPathHierarchyIndentation);
  }

  ImGui::EndChild();

  // Render popups (modal dialogs, can be rendered outside scrollable area)
  // Popup state management is handled inside each Render function
  Popups::RenderRegexGeneratorPopup(state.pathInput.Data(), SearchInputField::MaxLength(), state);
  Popups::RenderRegexGeneratorPopupFilename(state.filenameInput.Data(),
                                            SearchInputField::MaxLength(), state);
  Popups::RenderHistoryRenamePopup(state, settings);
  Popups::RenderHistoryDeletePopup(state, settings);

  // Render status bar
  StatusBar::Render(state, search_worker, monitor, file_index,
#ifdef _WIN32
                    settings.monitoredVolume,
#else
                    std::string_view{},
#endif  // _WIN32
                    context.seconds_until_recrawl);

  ImGui::End();
}

void UIRenderer::RenderFloatingWindows(const RenderFloatingWindowsContext& context) {
  const auto& [state, actions, settings, file_index, monitor, search_worker, glfw_window,
               show_settings, show_metrics, metrics_available, startup_args_display] = context;
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
    const bool is_monitoring_active = (monitor != nullptr) && monitor->IsActive();
    HelpWindow::Render(&state.showHelpWindow, &state.memory_bytes_, is_monitoring_active,
                       startup_args_display);
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
                                           GLFWwindow* glfw_window,
                                           bool is_expanded, bool is_index_building,
                                           const AppSettings& settings) {
  if (!is_expanded) {
    return;
  }

  ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kSectionSpacing));
  SearchInputs::Render(state, actions, is_index_building, settings, glfw_window);
  SearchControls::Render(state, actions, is_index_building);

  if (state.showQuickFilters) {
    FilterPanel::RenderQuickFilters(state);
    FilterPanel::RenderTimeQuickFilters(state);
    FilterPanel::RenderSizeQuickFilters(state);
  }
}

void UIRenderer::RenderAISearchSection(const RenderMainWindowContext& context) {
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

  ImGui::SetNextItemOpen(context.state.aiSearchExpanded, ImGuiCond_Always);

  ImGui::PushStyleColor(ImGuiCol_Header, header_color);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, header_hovered_color);
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, header_active_color);
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextStrong);

  const bool ai_is_expanded = ImGui::CollapsingHeader(ai_header_label.c_str(), ImGuiTreeNodeFlags_None);

  ImGui::PopStyleColor(4);
  DrawSectionHeaderAccentBar();  // After header so bar draws on top
  context.state.aiSearchExpanded = ai_is_expanded;

  if (ai_is_expanded) {
    if (context.settings.showWorkflowHint && api_key_set) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextWrapped("[TIP] Start here: Describe what you're looking for in natural language. "
                         "For manual control, use Manual Search above.");
      ImGui::PopStyleColor();

      ImGui::SameLine();
      if (ImGui::SmallButton("Got it")) {
        DismissWorkflowHint(context.settings);
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Don't show again")) {
        DismissWorkflowHint(context.settings);
      }
      ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kBlockPadding));
    }

    // Use local bools for SearchInputs which expects bool* pointers
    bool show_settings_val = context.show_settings.load();
    bool show_metrics_val = context.show_metrics.load();
    SearchInputs::RenderAISearch(context.state, context.glfw_window, context.actions,
                                 context.is_index_building, &show_settings_val,
                                 &show_metrics_val, context.metrics_available);
    context.show_settings.store(show_settings_val);
    context.show_metrics.store(show_metrics_val);
  }
}

}  // namespace ui
