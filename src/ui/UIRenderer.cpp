/**
 * @file UIRenderer.cpp
 * @brief Implementation of UIRenderer coordinator for UI rendering
 */

#include "ui/UIRenderer.h"

#include "utils/CTrackWrapper.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <atomic>
#include <string>

#include "api/AiProvider/ProviderCatalog.h"
#include "api/AiProvider/ProviderFactory.h"
#include "core/AppIdentity.h"
#include "core/Settings.h"
#include "gui/GuiState.h"
#include "gui/ImGuiUtils.h"
#include "gui/UIActions.h"
#include "imgui.h"
#include "index/FileIndex.h"
#include "ui/AiProviderWindow.h"
#include "ui/FilterPanel.h"
#include "ui/HelpWindow.h"
#include "ui/IconsFontAwesome.h"
#include "ui/LayoutConstants.h"
#include "ui/MetricsWindow.h"
#include "ui/Popups.h"
#include "ui/RecentChangesWindow.h"
#include "ui/ResultsTable.h"
#include "ui/SearchHelpWindow.h"
#include "ui/SearchInputs.h"
#include "ui/SettingsWindow.h"
#include "ui/StatusBar.h"
#include "ui/Theme.h"
#include "ui/WelcomePanel.h"
#include "usn/UsnMonitor.h"
#include "utils/Logger.h"

namespace ui {

namespace {

constexpr int kStoppingVerticalSpacingCount = 5;

void RenderStoppingState() {
  AddVerticalSpacing(kStoppingVerticalSpacingCount);
  const float window_width = ImGui::GetContentRegionAvail().x;
  const char* stopping_text = "Stopping...";
  const float text_width = ImGui::CalcTextSize(stopping_text).x;
  ImGui::SetCursorPosX((window_width - text_width) * 0.5F);

  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::Text);
  ImGui::Text("%s", stopping_text);
  ImGui::PopStyleColor();

  AddVerticalSpacing(kStoppingVerticalSpacingCount);
}

}  // namespace

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

static void DismissWorkflowHint(AppSettings& settings, ui::UIActions* actions) {
  settings.showWorkflowHint = false;
  if (actions == nullptr || !actions->PersistSettings()) {
    settings.showWorkflowHint = true;  // Revert so hint reappears on next launch
    LOG_ERROR("Failed to persist workflow hint dismissal");
  }
}

static void CycleUIModeAndSave(AppSettings& settings, ui::UIActions* actions) {
  const AppSettings::UIMode previous_mode = settings.uiMode;
  if (settings.uiMode == AppSettings::UIMode::Full) {
    settings.uiMode = AppSettings::UIMode::Simplified;
  } else if (settings.uiMode == AppSettings::UIMode::Simplified) {
    settings.uiMode = AppSettings::UIMode::Minimalistic;
  } else {
    settings.uiMode = AppSettings::UIMode::Full;
  }
  if (actions == nullptr || !actions->PersistSettings()) {
    settings.uiMode = previous_mode;  // Revert in-memory state so UI stays consistent with disk
    LOG_ERROR("Failed to persist UI mode change");
  }
}

// Number of always-present application controls in the right-aligned group
// (UI mode cycle, Settings, AI Settings, Help). Each one is separated from the
// previous by an item spacing.
constexpr int kBaseApplicationControlCount = 4;

// Parameter bundle for the application-controls toolbar width computation.
// Keeps ComputeApplicationControlsTotalWidth within the sonar-cpp S107 limit (7 params).
struct ComputeApplicationControlsTotalWidthParams {
  const char* settings_label = nullptr;
  const char* ai_settings_label = nullptr;
  const char* mode_label = nullptr;
  float help_width = 0.0F;
  bool metrics_available = false;
  bool show_metrics_val = false;
  bool show_recent_changes_val = false;
  const bool* show_test_engine_window = nullptr;  // non-null when Test Engine build enabled (used only to decide width)
  const ImGuiStyle& style;  // NOSONAR(cpp:S6094) - struct-local reference aliases ImGui::GetStyle(), always valid during a frame
};

static float ComputeApplicationControlsTotalWidth(
  const ComputeApplicationControlsTotalWidthParams& params) {
  const float settings_width = ComputeButtonWidth(params.settings_label);
  const float ai_settings_width = ComputeButtonWidth(params.ai_settings_label);
  const float mode_width = ComputeButtonWidth(params.mode_label);
  float total = settings_width + ai_settings_width + mode_width + params.help_width +
                (params.style.ItemSpacing.x * static_cast<float>(kBaseApplicationControlCount - 1));
  if (params.show_test_engine_window != nullptr) {
    total += ComputeButtonWidth("Test Engine") + params.style.ItemSpacing.x;
  }
  if (params.metrics_available) {
    const char* metrics_label =
      params.show_metrics_val ? ICON_FA_CHART_BAR " Hide Metrics" : ICON_FA_CHART_BAR " Metrics";
    total += ComputeButtonWidth(metrics_label) + params.style.ItemSpacing.x;
#ifdef _WIN32
    const char* recent_changes_label =
      params.show_recent_changes_val ? ICON_FA_HISTORY " Hide Activity" : ICON_FA_HISTORY " Activity";
    total += ComputeButtonWidth(recent_changes_label) + params.style.ItemSpacing.x;
#else
    (void)params.show_recent_changes_val;
#endif  // _WIN32
  }
  return total;
}

static void RenderModeCycleButton(AppSettings& settings, ui::UIActions* actions) {
  const char* mode_label = GetUIModeButtonLabel(settings.uiMode);
  ImGui::SetNextItemAllowOverlap();
  if (ImGui::SmallButton(mode_label)) {
    CycleUIModeAndSave(settings, actions);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Cycle UI Mode (Full -> Simplified -> Minimalistic)");
  }
}

// Stable ID ##toolbar_settings for tests. See ImGuiTestEngineTests settings_window_open.
static void RenderSettingsButton(bool show_settings_val,
                                 std::atomic<bool>& show_settings) {  // NOLINT(misc-const-correctness) - mutated via store()
  const char* settings_label =
    show_settings_val ? ICON_FA_GEAR " Hide Settings##toolbar_settings"
                      : ICON_FA_GEAR " Settings##toolbar_settings";
  ImGui::SameLine();
  ImGui::SetNextItemAllowOverlap();
  if (ImGui::SmallButton(settings_label)) {
    show_settings.store(!show_settings.load());
  }
}

// Stable ID ##toolbar_ai_settings for tests. Opens the AI provider picker.
static void RenderAiSettingsButton(bool show_ai_settings_val, bool& show_ai_settings) {
  const char* label = show_ai_settings_val ? ICON_FA_ROBOT " Hide AI Settings##toolbar_ai_settings"
                                           : ICON_FA_ROBOT " AI Settings##toolbar_ai_settings";
  ImGui::SameLine();
  ImGui::SetNextItemAllowOverlap();
  if (ImGui::SmallButton(label)) {
    show_ai_settings = !show_ai_settings_val;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Choose the AI provider used for AI-assisted search");
  }
}

// Stable ID ##toolbar_help for tests; avoids relying on visible label (icons/localization). See ImGuiTestEngineTests help_window_open.
static void RenderHelpButton(GuiState& state) {
  const char* help_label =
    state.ui_visibility.show_help_window ? ICON_FA_BOOK_OPEN " Hide Help##toolbar_help"
                         : ICON_FA_BOOK_OPEN " Help##toolbar_help";
  ImGui::SameLine();
  ImGui::SetNextItemAllowOverlap();
  if (ImGui::SmallButton(help_label)) {
    state.ui_visibility.show_help_window = !state.ui_visibility.show_help_window;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Keyboard shortcuts");
  }
}

// Title of the window created by ImGuiTestEngine_ShowTestEngineWindows()
// (external/imgui_test_engine/imgui_test_engine/imgui_te_ui.cpp).
static constexpr const char* kTestEngineWindowName = "Dear ImGui Test Engine";

static void RenderTestEngineButton(bool* show_test_engine_window) {
  ImGui::SameLine();
  ImGui::SetNextItemAllowOverlap();
  if (ImGui::SmallButton("Test Engine")) {
    *show_test_engine_window = true;
    // The full-viewport main window is raised by this very click (and, with
    // multi-viewport, its OS window is raised above a detached Test Engine
    // window), which would hide an already-open Test Engine window with no way
    // to recover it. Re-focus it explicitly so the button always brings it back.
    ImGui::SetWindowFocus(kTestEngineWindowName);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Show ImGui Test Engine window (run UI tests)");
  }
}

static void RenderMetricsButton(std::atomic<bool>& show_metrics,  // NOLINT(misc-const-correctness) - mutated via store()
                                bool metrics_available) {
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

static void RenderRecentChangesButton(
    std::atomic<bool>& show_recent_changes,  // NOLINT(misc-const-correctness) NOSONAR(cpp:S995) - Windows branch TOGGLES the flag (store); a const ref is not possible
    bool metrics_available) {
#ifdef _WIN32
  if (!metrics_available) {
    return;
  }
  const bool show_val = show_recent_changes.load();
  const char* label = show_val ? ICON_FA_HISTORY " Hide Activity" : ICON_FA_HISTORY " Activity";
  ImGui::SameLine();
  ImGui::SetNextItemAllowOverlap();
  if (ImGui::SmallButton(label)) {
    show_recent_changes.store(!show_recent_changes.load());
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Show recent drive activity (USN Journal live feed)");
  }
#else
  (void)show_recent_changes;
  (void)metrics_available;
#endif  // _WIN32
}

// Parameter bundle for the application-controls toolbar render; the atomics are
// non-const references because toggling buttons stores into them (sonar-cpp S107).
struct RenderApplicationControlsParams {
  GuiState& state;
  AppSettings& settings;
  ui::UIActions* actions;
  std::atomic<bool>& show_settings;
  std::atomic<bool>& show_metrics;
  std::atomic<bool>& show_recent_changes;
  bool metrics_available;
  bool* show_test_engine_window;
};

static void RenderApplicationControls(const RenderApplicationControlsParams& p) {
  const bool show_settings_val = p.show_settings.load();
  const char* settings_label =
    show_settings_val ? ICON_FA_GEAR " Hide Settings" : ICON_FA_GEAR " Settings";
  const bool show_ai_settings_val = p.state.ui_visibility.show_ai_settings;
  const char* ai_settings_label =
    show_ai_settings_val ? ICON_FA_ROBOT " Hide AI Settings" : ICON_FA_ROBOT " AI Settings";
  const char* mode_label = GetUIModeButtonLabel(p.settings.uiMode);
  const float help_width =
    ComputeButtonWidth(p.state.ui_visibility.show_help_window ? ICON_FA_BOOK_OPEN " Hide Help"
                                              : ICON_FA_BOOK_OPEN " Help");
  const bool show_metrics_val = p.metrics_available ? p.show_metrics.load() : false;
  const bool show_recent_changes_val =
      p.metrics_available ? p.show_recent_changes.load() : false;
  const ImGuiStyle& style = ImGui::GetStyle();

  const ComputeApplicationControlsTotalWidthParams width_params{
      settings_label, ai_settings_label, mode_label, help_width, p.metrics_available,
      show_metrics_val, show_recent_changes_val, p.show_test_engine_window, style,
  };
  const float total_group_width = ComputeApplicationControlsTotalWidth(width_params);
  AlignGroupRight(total_group_width);

  ImGui::PushStyleColor(ImGuiCol_Button, Theme::Colors::Surface);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Colors::SurfaceHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Colors::SurfaceActive);
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDim);

  RenderModeCycleButton(p.settings, p.actions);
  RenderSettingsButton(show_settings_val, p.show_settings);
  RenderAiSettingsButton(show_ai_settings_val, p.state.ui_visibility.show_ai_settings);
  RenderHelpButton(p.state);

  if (p.show_test_engine_window != nullptr) {
    RenderTestEngineButton(p.show_test_engine_window);
  }
  RenderMetricsButton(p.show_metrics, p.metrics_available);
  RenderRecentChangesButton(p.show_recent_changes, p.metrics_available);

  ImGui::PopStyleColor(4);
}

void UIRenderer::RenderMainWindow(const RenderMainWindowContext& context) {
  CTRACK_DEV_NAME("UIRenderer::RenderMainWindow");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  // NOLINTBEGIN(misc-misplaced-const) - NativeWindowHandle pointer typedef gives void*const in structured binding; correct
  const auto& [state, actions, settings, file_index, thread_pool, monitor, search_worker,
               aggregator, native_window, glfw_window, show_settings, show_metrics, show_recent_changes,
               is_index_building, metrics_available, seconds_until_recrawl,
               show_test_engine_window, recycle_snapshot] = context;
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
  const RenderApplicationControlsParams controls_params{
      state, settings, actions, show_settings, show_metrics, show_recent_changes,
      metrics_available, show_test_engine_window,
  };
  RenderApplicationControls(controls_params);
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
  status_bar::RenderBusyProgressBarInRect(state, search_worker, gap_min, gap_max,
                                         file_index.IsTrigramIndexBuilding());

  if (settings.uiMode == AppSettings::UIMode::Minimalistic) {
    // MINIMALISTIC UI: Line 1 (Name + Ext + Search now)
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
    filter_panel::RenderActiveFilterIndicators(state);
    ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kSectionSpacing));
  }

  // Show stopping state if window is closing
  if (glfwWindowShouldClose(glfw_window) != 0) {
    RenderStoppingState();
  } else if (!state.search_pipeline.search_active && state.result_pool_->Results().empty()) {
    // Show placeholder when results_table::Render won't render the table
    welcome_panel::Render(state, actions, settings, is_index_building);
  } else {
    // Always call results_table::Render - it handles its own conditions internally
    const results_table::RenderContext results_context{
      state, native_window, glfw_window, thread_pool, file_index,
      aggregator, settings.showPathHierarchyIndentation, actions,
      recycle_snapshot,
    };
    results_table::Render(results_context);
  }

  ImGui::EndChild();

  // Render popups (modal dialogs, can be rendered outside scrollable area)
  // Popup state management is handled inside each Render function
  popups::RenderRegexGeneratorPopup(state.searchCriteria.path_input.Data(), SearchInputField::MaxLength(), state);
  popups::RenderRegexGeneratorPopupFilename(state.searchCriteria.filename_input.Data(),
                                            SearchInputField::MaxLength(), state);
  popups::RenderHistoryRenamePopup(state, settings, actions);
  popups::RenderHistoryDeletePopup(state, settings, actions);
  popups::RenderExportResultPopup(state);

  // Render status bar
  status_bar::Render(state, search_worker, monitor, file_index,
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
               show_settings, show_metrics, show_recent_changes, metrics_available, startup_args_display,
               unknown_args_display, flight_recorder] = context;
  // Render floating windows
  // Use local bools for compatibility with SettingsWindow/MetricsWindow which expect bool*
  bool show_settings_val = show_settings.load();
  bool show_metrics_val = show_metrics.load();
  bool show_recent_changes_val = show_recent_changes.load();
  if (show_settings_val) {
    settings_window::Render(&show_settings_val, settings, file_index, actions, glfw_window);
    show_settings.store(show_settings_val);
  }
  if (metrics_available && show_metrics_val) {
    metrics_window::Render(
        &show_metrics_val, monitor, &search_worker, file_index, state, flight_recorder);
    show_metrics.store(show_metrics_val);
  }
  if (metrics_available && show_recent_changes_val) {
    recent_changes_window::Render(&show_recent_changes_val, monitor);
    show_recent_changes.store(show_recent_changes_val);
  }
  if (state.ui_visibility.show_help_window) {
    const bool is_monitoring_active = (monitor != nullptr) && monitor->IsActive();
    help_window::Render(&state.ui_visibility.show_help_window, &state.memory_bytes_, is_monitoring_active,
                        startup_args_display, unknown_args_display);
  }
  if (state.ui_visibility.show_search_help_window) {
    search_help_window::Render(&state.ui_visibility.show_search_help_window);
  }
  if (state.ui_visibility.show_ai_settings) {
    ai_provider_window::Render(&state.ui_visibility.show_ai_settings, settings, actions,
                               glfw_window);
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

// Header label of the AI-Assisted Search section: names the provider that is
// currently selected ("AI-Assisted Search (OpenCode · deepseek-v4.1-flash)"), and
// flags a missing API key so the one-click path is not silently unavailable.
// The "##ai_search" suffix keeps the widget ID stable across frames.
//
// Called every frame, so the scratch string is reused (static, function-local,
// UI thread only) instead of allocating per frame. The returned reference is
// invalidated by the next call -- this function must not be called again while
// the previous result is still in use (the single caller consumes it
// immediately).
const std::string& BuildAiHeaderLabel(const ai_provider::ProviderConfig& config,
                                      ai_provider::ApiKeyStatus key_status) {
  static thread_local std::string label;
  label.clear();
  label.append("AI-Assisted Search (").append(ai_provider::ProviderSummary(config));  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
  if (key_status == ai_provider::ApiKeyStatus::Missing) {
    label.append(" · no key");
  }
  label.append(")##ai_search");
  return label;
}

}  // namespace

int UIRenderer::CountActiveFilters(const GuiState& state) {
  int count = 0;
  if (filter_panel::IsExtensionFilterActive(state)) {
    count++;
  }
  if (filter_panel::IsFilenameFilterActive(state)) {
    count++;
  }
  if (filter_panel::IsPathFilterActive(state)) {
    count++;
  }
  if (state.searchCriteria.item_type_filter != ItemTypeFilter::All || state.searchCriteria.folders_only) {
    count++;
  }
  if (state.searchCriteria.time_filter != TimeFilter::None) {
    count++;
  }
  if (state.searchCriteria.size_filter != SizeFilter::None) {
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
  static const GuiState* last_state_ptr = nullptr;
  static int last_filter_count = -1;
  static std::string cached_header_label;
  if (last_state_ptr != &state || active_filter_count != last_filter_count) {
    if (active_filter_count > 0) {
      cached_header_label = "Manual Search (" + std::to_string(active_filter_count) +
                            " filters active)##manual_search";
    } else {
      cached_header_label = "Manual Search##manual_search";
    }
    last_state_ptr = &state;
    last_filter_count = active_filter_count;
  }

  ImGui::SetNextItemOpen(state.ui_visibility.manual_search_expanded, ImGuiCond_Always);

  ImGui::PushStyleColor(ImGuiCol_Header, header_color);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, header_hovered_color);
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, header_active_color);
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextStrong);

  is_expanded = ImGui::CollapsingHeader(cached_header_label.c_str(), ImGuiTreeNodeFlags_None);

  ImGui::PopStyleColor(4);
  DrawSectionHeaderAccentBar();  // After header so bar draws on top
  state.ui_visibility.manual_search_expanded = is_expanded;

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

  if (state.ui_visibility.show_quick_filters) {
    filter_panel::RenderQuickFilters(state);
    filter_panel::RenderTimeQuickFilters(state);
    filter_panel::RenderSizeQuickFilters(state);
  }
}

void UIRenderer::RenderAISearchSection(const RenderMainWindowContext& context) {
  // Accent-tinted headers (similar to Search button) for visual consistency
  const ImVec4 header_color = Theme::AccentTint(Theme::HeaderAlphas::Base);
  const ImVec4 header_hovered_color = Theme::AccentTint(Theme::HeaderAlphas::Hover);
  const ImVec4 header_active_color = Theme::AccentTint(Theme::HeaderAlphas::Active);

  // Resolve the configured LLM provider from settings (aiProvider group;
  // empty = Gemini default, unknown ids fall back to Gemini) and check its
  // key. Env is read fresh every frame from this single source of truth: the
  // key can be set/unset at runtime (settings UI, tests, rotation), so a
  // static cache would freeze the first-observed value. Passed down to
  // SearchInputs::RenderAISearch (same value).
  const ai_provider::ProviderConfig provider_config =
      ai_provider::ResolveProviderConfig(context.settings);
  const auto llm_provider = ai_provider::CreateProviderForConfig(provider_config);
  const bool key_present =
      !ai_provider::ResolveApiKey(provider_config, llm_provider->DefaultEnvVarName()).empty();
  // A keyless provider (a local server) must not be reported as "not
  // configured": it works without one. The catalog knows which providers
  // require a key.
  const ai_provider::ApiKeyStatus key_status =
      ai_provider::ResolveApiKeyStatus(provider_config);
  const bool api_key_set =
      key_present || key_status == ai_provider::ApiKeyStatus::NotRequired;

  // The header summarises the current provider selection instead of the
  // (now less useful) API-key-only wording.
  const std::string& ai_header_label = BuildAiHeaderLabel(provider_config, key_status);

  ImGui::SetNextItemOpen(context.state.ui_visibility.ai_search_expanded, ImGuiCond_Always);

  ImGui::PushStyleColor(ImGuiCol_Header, header_color);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, header_hovered_color);
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, header_active_color);
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextStrong);

  // The label stays valid across the call (buffer is owned by
  // BuildAiHeaderLabel and not rebuilt until the next frame).
  const bool ai_is_expanded =
      ImGui::CollapsingHeader(ai_header_label.c_str(), ImGuiTreeNodeFlags_None);

  ImGui::PopStyleColor(4);
  DrawSectionHeaderAccentBar();  // After header so bar draws on top
  context.state.ui_visibility.ai_search_expanded = ai_is_expanded;

  if (ai_is_expanded) {
    if (context.settings.showWorkflowHint && api_key_set) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextWrapped("[TIP] Start here: Describe what you're looking for in natural language. "
                         "For manual control, use Manual Search above.");
      ImGui::PopStyleColor();

      ImGui::SameLine();
      if (ImGui::SmallButton("Got it")) {
        DismissWorkflowHint(context.settings, context.actions);
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Don't show again")) {
        DismissWorkflowHint(context.settings, context.actions);
      }
      ImGui::Dummy(ImVec2(0.0F, LayoutConstants::kBlockPadding));
    }

    // Use local bools for SearchInputs which expects bool* pointers
    bool show_settings_val = context.show_settings.load();
    bool show_metrics_val = context.show_metrics.load();
    AISearchArgs ai_search_args;
    ai_search_args.window = context.glfw_window;
    ai_search_args.actions = context.actions;
    ai_search_args.is_index_building = context.is_index_building;
    ai_search_args.show_settings = &show_settings_val;
    ai_search_args.show_metrics = &show_metrics_val;
    ai_search_args.metrics_available = context.metrics_available;
    ai_search_args.api_key_set = api_key_set;
    ai_search_args.provider_config = provider_config;
    SearchInputs::RenderAISearch(context.state, ai_search_args);
    context.show_settings.store(show_settings_val);
    context.show_metrics.store(show_metrics_val);
  }
}

}  // namespace ui
