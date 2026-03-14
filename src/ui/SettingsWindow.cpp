/**
 * @file ui/SettingsWindow.cpp
 * @brief Implementation of settings window rendering component
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

// GLFW includes
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "core/IndexBuilder.h"
#include "core/Settings.h"
#include "gui/UIActions.h"
#include "imgui.h"
#include "index/FileIndex.h"
#include "ui/FolderBrowser.h"
#include "ui/IconsFontAwesome.h"
#include "ui/LayoutConstants.h"
#include "ui/SettingsWindow.h"
#include "ui/Theme.h"
#include "ui/UiStyleGuards.h"
#include "utils/ClipboardUtils.h"
#include "utils/Logger.h"

#ifdef _WIN32
#include "platform/windows/ShellContextUtils.h"
#endif  // _WIN32

namespace ui {
namespace {

namespace settings_ui_constants {
// Appearance slider ranges
// Note: kPascalCase naming follows project convention (CXX17_NAMING_CONVENTIONS.md)
// clang-tidy naming checks are disabled for these constants as they're namespace-scoped,
// not truly global, and follow the project's kPascalCase convention for constants
constexpr float kMinFontSize = 10.0F;  // NOLINT(readability-identifier-naming)
constexpr float kMaxFontSize = 24.0F;  // NOLINT(readability-identifier-naming)
constexpr float kMinUIScale = 0.8F;    // NOLINT(readability-identifier-naming)
constexpr float kMaxUIScale = 1.5F;    // NOLINT(readability-identifier-naming)

// Performance tuning limits (thread pool bounds in settings_defaults)
constexpr int kMinDynamicChunkSize = 100;         // NOLINT(readability-identifier-naming)
constexpr int kMaxDynamicChunkSize = 100000;      // NOLINT(readability-identifier-naming)
constexpr int kDynamicChunkSizeStep = 500;        // NOLINT(readability-identifier-naming)
constexpr int kMinHybridInitialWorkPercent = 50;  // NOLINT(readability-identifier-naming)
constexpr int kMaxHybridInitialWorkPercent = 95;  // NOLINT(readability-identifier-naming)

// Settings window dimensions
constexpr int kSettingsWindowWidth = 500;   // NOLINT(readability-identifier-naming)
constexpr int kSettingsWindowHeight = 580;  // NOLINT(readability-identifier-naming)
}  // namespace settings_ui_constants

// Helper to render a consistent section header
void RenderSectionHeader(const char* label) {
  ImGui::Spacing();
  ImGui::TextUnformatted(label);
  ImGui::Separator();
  ImGui::Spacing();
}

// Returns the content scale (DPI scale) for the given window, or primary monitor if window is
// null. Clamped to UI scale slider range. Used by the "Auto" appearance button.
float GetContentScaleForWindow(GLFWwindow* window) {
  float x_scale = 1.0F;
  float y_scale = 1.0F;
  if (window != nullptr) {
    glfwGetWindowContentScale(window, &x_scale, &y_scale);
  } else {
    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    if (primary != nullptr) {
      glfwGetMonitorContentScale(primary, &x_scale, &y_scale);
    }
  }
  // Use max of x/y in case of non-uniform scale; clamp to valid UI scale range
  const float scale = (std::max)(x_scale, y_scale);
  if (scale <= 0.0F || !std::isfinite(scale)) {
    return 1.0F;
  }
  return std::clamp(scale, settings_ui_constants::kMinUIScale,
                   settings_ui_constants::kMaxUIScale);
}

// Function-local static for folder browser instance (thread-safe in C++11+)
FolderBrowser& GetFolderBrowser() {
  static std::unique_ptr<FolderBrowser> instance = nullptr;
  if (!instance) {
    instance = std::make_unique<FolderBrowser>("Select Folder to Index");
  }
  return *instance;
}

// Helper function to render appearance settings section
void RenderAppearanceSettings(AppSettings& settings, GLFWwindow* glfw_window) {
  RenderSectionHeader("Appearance");

  // Theme selection (apply immediately on change)
  static const std::array<const char*, 7> kThemeLabels = {
    "Default Dark", "Dracula", "Nord", "One Dark", "Gruvbox Dark", "Everforest", "Catppuccin Mocha"};
  static const std::array<std::string_view, 7> kThemeIds = {
    "default_dark", "dracula", "nord", "one_dark", "gruvbox", "everforest", "catppuccin"};
  int current_theme_index = 0;
  for (size_t i = 0; i < kThemeIds.size(); ++i) {
    if (settings.themeId == kThemeIds[i]) {  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index from loop
      current_theme_index = static_cast<int>(i);
      break;
    }
  }
  if (ImGui::Combo("Theme", &current_theme_index, kThemeLabels.data(),
                   static_cast<int>(kThemeLabels.size()))) {
    settings.themeId = std::string(kThemeIds[static_cast<size_t>(current_theme_index)]);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index from ImGui::Combo
    const auto viewports_enabled =
      (static_cast<unsigned>(ImGui::GetIO().ConfigFlags) &
       static_cast<unsigned>(ImGuiConfigFlags_ViewportsEnable)) != 0;
    Theme::Apply(settings.themeId, viewports_enabled);
  }

  // Font family selection
  // Note: keep this list in sync with ConfigureFontsFromSettings.
  // Embedded fonts (Roboto Medium, Cousine, Karla) are available on all platforms.
  static const std::array<const char*, 12> kFontLabels = {
    "Roboto Medium", "Cousine", "Karla",  "Consolas",      "Segoe UI",       "Arial",
    "Calibri",       "Verdana", "Tahoma", "Cascadia Mono", "Lucida Console", "Courier New"};
  int current_font_index = 0;
  for (size_t i = 0; i < kFontLabels.size(); ++i) {
    if (settings.fontFamily == kFontLabels[i]) {  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index from loop
      current_font_index = static_cast<int>(i);
      break;
    }
  }

  if (ImGui::Combo("Font", &current_font_index, kFontLabels.data(),
                   static_cast<int>(kFontLabels.size()))) {
    settings.fontFamily =
      kFontLabels[static_cast<size_t>(current_font_index)];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index from ImGui::Combo
  }

  // Font size
  if (float font_size = settings.fontSize;
      ImGui::SliderFloat("Font Size", &font_size, settings_ui_constants::kMinFontSize,
                         settings_ui_constants::kMaxFontSize,
                         "%.1f")) {  // NOSONAR(cpp:S6004) - Variable modified by reference in
                                     // ImGui, then used after if
    settings.fontSize = font_size;
  }

  // Global UI scale
  if (float scale = settings.fontScale; ImGui::SliderFloat(
        "UI Scale", &scale, settings_ui_constants::kMinUIScale, settings_ui_constants::kMaxUIScale,
        "%.2f")) {  // NOSONAR(cpp:S6004) - Variable modified by reference in ImGui, then used after
                    // if
    settings.fontScale = scale;
  }

  // Auto: restore default font size and set UI scale from current monitor DPI
  ImGui::SameLine();
  ImGui::PushID("AppearanceAutoFontScale");
  if (ImGui::SmallButton("Auto")) {
    const float monitor_scale = GetContentScaleForWindow(glfw_window);
    settings.fontScale = monitor_scale;
    settings.fontSize = settings_defaults::kDefaultFontSize;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Restore default font size (%.0f) and set UI scale from current monitor DPI.\n"
                      "Use after moving the window to another display.",
                      static_cast<double>(settings_defaults::kDefaultFontSize));
  }
  ImGui::PopID();

  ImGui::Spacing();

  // UI Mode
  // Simplified and Minimalistic modes automatically enable "Search as you type"
  // and "Auto-refresh" for a streamlined experience.
  constexpr std::array<const char*, 3> kModeLabels = {"Full UI", "Simplified UI",
                                                      "Minimalistic UI"};
  if (auto current_mode = static_cast<int>(settings.uiMode); ImGui::Combo(  // NOSONAR(cpp:S5827)
        "UI Mode", &current_mode, kModeLabels.data(), static_cast<int>(kModeLabels.size()))) {
    settings.uiMode = static_cast<AppSettings::UIMode>(current_mode);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("Full UI: Show all controls and panels.\n"
                "Simplified UI: Hide advanced search options.\n"
                "Minimalistic UI: Show only essential path search input.\n"
                "Non-Full modes automatically enable 'Search as you type' and 'Auto-refresh'.");
    ImGui::EndTooltip();
  }

  ImGui::Checkbox("Stream search results", &settings.streamPartialResults);
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("Shows search results incrementally as they are found.\n"
                "Improves perceived performance, especially on large indexes.");
    ImGui::EndTooltip();
  }

  ImGui::Checkbox("Indent results by folder depth", &settings.showPathHierarchyIndentation);
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("Indents filenames in the results table by their folder depth.\n"
                "Makes the folder structure visible at a glance.");
    ImGui::EndTooltip();
  }

  ImGui::Spacing();
}

namespace {
// Helper function to get strategy index from string (reduces cognitive complexity)
int GetStrategyIndexFromString(std::string_view strategy) {
  if (strategy == "static") {
    return 0;
  }
  if (strategy == "hybrid") {
    return 1;
  }
  if (strategy == "dynamic") {
    return 2;
  }
  if (strategy == "interleaved") {
    return 3;
  }
#if defined(FAST_LIBS_BOOST)
  if (strategy == "work_stealing") {
    return 4;
  }
#endif  // FAST_LIBS_BOOST
  // Default to hybrid if invalid
  return 1;
}

// Helper function to get strategy string from index (reduces cognitive complexity)
std::string GetStrategyStringFromIndex(int index) {
  if (index == 0) {
    return "static";
  }
  if (index == 1) {
    return "hybrid";
  }
  if (index == 2) {
    return "dynamic";
  }
#if defined(FAST_LIBS_BOOST)
  if (index == 3) {
    return "interleaved";
  }
  if (index == 4) {
    return "work_stealing";
  }
  // Default to hybrid if index is out of range (defensive; ImGui::Combo clamps index)
  return "hybrid";
#else
  if (index == 3) {
    return "interleaved";
  }
  // Default to hybrid if index is out of range (defensive; ImGui::Combo clamps index)
  return "hybrid";
#endif  // FAST_LIBS_BOOST
}

// Helper function to render load balancing strategy selector (reduces cognitive complexity)
int RenderLoadBalancingStrategy(AppSettings& settings) {
  // Strategy labels must stay in sync with GetStrategyIndexFromString/GetStrategyStringFromIndex
#if defined(FAST_LIBS_BOOST)
  static const std::array<const char*, 5> kStrategyLabels = {
    "Static", "Hybrid", "Dynamic", "Interleaved", "Work Stealing"};
#else
  static const std::array<const char*, 4> kStrategyLabels = {
    "Static", "Hybrid", "Dynamic", "Interleaved"};
#endif  // FAST_LIBS_BOOST
  int current_strategy_index = GetStrategyIndexFromString(settings.loadBalancingStrategy);

  if (ImGui::Combo("Load Balancing Strategy", &current_strategy_index, kStrategyLabels.data(),
                   static_cast<int>(kStrategyLabels.size()))) {
    settings.loadBalancingStrategy = GetStrategyStringFromIndex(current_strategy_index);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Static: Fixed chunks assigned upfront\n"
                      "Hybrid: Initial large chunks (configurable %%) + dynamic small chunks\n"
                      "Dynamic: Initial chunks (50%%) + guided dynamic scheduling (recommended)\n"
                      "Interleaved: Threads process items in an interleaved manner\n"
#if defined(FAST_LIBS_BOOST)
                      "Work Stealing: Per-thread queues with stealing (requires FAST_LIBS_BOOST)\n"
#endif  // FAST_LIBS_BOOST
    );
  }
  return current_strategy_index;
}

// Helper function to render thread pool size input (reduces cognitive complexity)
void RenderThreadPoolSize(AppSettings& settings) {
  if (int pool_size = settings.searchThreadPoolSize; ImGui::InputInt(  // NOSONAR(cpp:S6004) - ImGui modifies by reference, then used after if
        "Thread Pool Size", &pool_size, 1, 1)) {
    // Clamp to valid range: 0 (auto-detect) or 1-64  // NOSONAR(cpp:S6004) - Issue attached to block; init-statement already used
    pool_size = (std::max)(0, pool_size);  // NOLINT(readability-use-std-min-max) - Using (std::max)
                                           // to prevent Windows macro expansion
    pool_size =
      (std::min)(pool_size,
                 settings_defaults::kMaxThreadPoolSize);  // NOLINT(readability-use-std-min-max)
                                                          // - Using (std::min) to prevent
                                                          // Windows macro expansion
    settings.searchThreadPoolSize = pool_size;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("0 = Auto-detect (use hardware concurrency)\n"
                      "1-%d = Explicit thread count\n"
                      "Allows tuning performance for your hardware\n"
                      "Changes take effect immediately after saving (thread "
                      "pool will be recreated)",
                      settings_defaults::kMaxThreadPoolSize);
  }
}

// Helper function to render dynamic chunk size input (reduces cognitive complexity)
void RenderDynamicChunkSize(AppSettings& settings) {
  if (int chunk_size = settings.dynamicChunkSize;  // NOSONAR(cpp:S1854) - ImGui pattern: modified
                                                   // by reference, then used after if
      ImGui::InputInt(
        "Dynamic Chunk Size", &chunk_size, settings_ui_constants::kMinDynamicChunkSize,
        settings_ui_constants::kDynamicChunkSizeStep)) {  // NOSONAR(cpp:S6004) - Variable modified
                                                          // by reference in ImGui, then used after
                                                          // if
    // Clamp to valid range: 100-100000 (100K max for testing)
    chunk_size =
      (std::max)(chunk_size,
                 settings_ui_constants::kMinDynamicChunkSize);  // NOLINT(readability-use-std-min-max)
                                                                // - Using (std::max) to prevent
                                                                // Windows macro expansion
    chunk_size =
      (std::min)(chunk_size,
                 settings_ui_constants::kMaxDynamicChunkSize);  // NOLINT(readability-use-std-min-max)
                                                                // - Using (std::min) to prevent
                                                                // Windows macro expansion
    settings.dynamicChunkSize = chunk_size;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Chunk size for dynamic load balancing (Hybrid and Dynamic "
                      "strategies)\n"
                      "Range: 100-100000 items per chunk (100K max for testing)\n"
                      "Larger chunks = less overhead, less fine-grained balancing\n"
                      "Smaller chunks = more overhead, better load balancing\n"
                      "Recommended: 1000-10000 for most cases, up to 100000 for large "
                      "datasets\n"
                      "Takes effect on the next search");
  }
}

// Helper function to render hybrid initial work percentage (reduces cognitive complexity)
void RenderHybridInitialWorkPercent(AppSettings& settings) {
  if (int initial_percent =
        settings.hybridInitialWorkPercent;  // NOSONAR(cpp:S1854) - ImGui pattern: modified by
                                            // reference, then used after if
      ImGui::SliderInt(
        "Hybrid Initial Work %", &initial_percent,
        settings_ui_constants::kMinHybridInitialWorkPercent,
        settings_ui_constants::kMaxHybridInitialWorkPercent,  // NOSONAR(cpp:S6004) - Variable
                                                              // modified by reference in ImGui,
                                                              // then used after if
        "%d%%")) {
    // Clamp to valid range: 50-95
    initial_percent =
      (std::max)(initial_percent,
                 settings_ui_constants::
                   kMinHybridInitialWorkPercent);  // NOLINT(readability-use-std-min-max) - Using
                                                   // (std::max) to prevent Windows macro expansion
    initial_percent =
      (std::min)(initial_percent,
                 settings_ui_constants::
                   kMaxHybridInitialWorkPercent);  // NOLINT(readability-use-std-min-max) - Using
                                                   // (std::min) to prevent Windows macro expansion
    settings.hybridInitialWorkPercent = initial_percent;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Percentage of work assigned as initial chunks in Hybrid "
                      "strategy\n"
                      "Range: 50-95%%\n"
                      "Lower = more dynamic chunks = better load balancing, less cache "
                      "locality\n"
                      "Higher = more initial chunks = better cache locality, less load "
                      "balancing\n"
                      "Recommended: 70-80%%\n"
                      "Takes effect on the next search");
  }
}
}  // namespace

// Helper function to render performance settings section
void RenderPerformanceSettings(AppSettings& settings) {
  if (ImGui::CollapsingHeader("Advanced performance tuning")) {
    const int current_strategy_index = RenderLoadBalancingStrategy(
      settings);  // NOLINT(misc-const-correctness) - Variable is used in conditional check below,
                  // but value doesn't change
    RenderThreadPoolSize(settings);
    RenderDynamicChunkSize(settings);

    // Hybrid initial work percentage (only shown if Hybrid strategy is selected)
    if (current_strategy_index == 1) {  // Hybrid strategy
      RenderHybridInitialWorkPercent(settings);
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
}

// Forward declaration for ValidateFolderPath (used in anonymous namespace helpers)
std::pair<bool, std::string> ValidateFolderPath(std::string_view path);

namespace {
// Helper function to get visibility reason for folder selection (reduces cognitive complexity)
std::string GetFolderSelectionVisibilityReason() {
#ifdef _WIN32
  // Windows: Always show, but tailor the explanation based on elevation
  if (bool has_admin = IsProcessElevated(); !has_admin) {
    return "Running without administrator rights.\n"
           "Select a folder below to start indexing files.\n"
           "(For real-time monitoring, restart as administrator or right-click FindHelper → Run as administrator)";
  }
  return "USN Journal monitoring may be active.\n"
         "Use folder indexing below if you prefer indexing a specific path.";
#else
  // macOS/Linux: Always show (no USN Journal alternative)
  return "";
#endif  // _WIN32
}

// Helper function to render folder path input and controls (reduces cognitive complexity)
void RenderFolderPathInput(AppSettings& settings, FolderBrowser& folder_browser) {
  ImGui::Text("Folder to Index:");
  std::array<char, 512> folder_buf = {0};
  if (!settings.crawlFolder.empty()) {
    strcpy_safe(folder_buf.data(), folder_buf.size(), settings.crawlFolder.c_str());
  }
  if (ImGui::InputText("##crawl_folder", folder_buf.data(), folder_buf.size())) {
    settings.crawlFolder = folder_buf.data();
    LOG_INFO_BUILD("Updated crawl folder path to: " << settings.crawlFolder);
  }
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_FOLDER_OPEN " Browse...")) {
    folder_browser.Open(settings.crawlFolder);
  }
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_COPY " Copy Path") && !settings.crawlFolder.empty()) {
    if (GLFWwindow* current_window = glfwGetCurrentContext();
        current_window != nullptr) {  // NOSONAR(cpp:S134) - Nested if acceptable: ImGui immediate
                                      // mode pattern requires button->validation nesting
      clipboard_utils::SetClipboardText(current_window, settings.crawlFolder);
    }
  }
}

// Helper function to render folder validation message (reduces cognitive complexity)
void RenderFolderValidation(const AppSettings& settings) {
  if (!settings.crawlFolder.empty()) {
    const auto [is_valid, validation_error] = ValidateFolderPath(settings.crawlFolder);
    if (!is_valid &&
        !validation_error.empty()) {  // NOSONAR(cpp:S134) - Nested if acceptable: ImGui immediate
                                      // mode pattern requires validation nesting
      ImGui::TextColored(Theme::Colors::Error, "Error: %s", validation_error.c_str());
    }
  } else {
    ImGui::TextDisabled("(No folder selected)");
  }
}

namespace {
// Helper function to render stop indexing button (reduces cognitive complexity)
void RenderStopIndexingButton(ui::UIActions* actions) {
  if (ImGui::Button(ICON_FA_STOP " Stop Indexing") &&
      actions != nullptr) {  // NOSONAR(cpp:S134) - Nested if acceptable: ImGui immediate mode
                             // pattern requires button->validation nesting
    actions->StopIndexBuilding();
  }
  if (ImGui::IsItemHovered()) {  // NOSONAR(cpp:S134) - Nested if acceptable: ImGui immediate mode
                                 // pattern requires button->tooltip nesting
    ImGui::SetTooltip("Stop the current index building process");
  }
}

// Helper function to render start indexing button (reduces cognitive complexity)
void RenderStartIndexingButton(
  const AppSettings& settings,
  ui::UIActions*
    actions) {  // NOSONAR(cpp:S995) - Parameter must be non-const at call site
                // (RenderIndexingButtons) to allow folder selection, but this function only reads
  // Validate folder before allowing start
  const auto [can_start, validation_message] =
    ValidateFolderPath(settings.crawlFolder.empty() ? "" : settings.crawlFolder);
  const std::string display_message =
    settings.crawlFolder.empty() ? "Please select a folder first" : validation_message;

  if (!can_start) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button(ICON_FA_PLAY " Start Indexing") && actions != nullptr &&
      can_start) {  // NOSONAR(cpp:S134) - Nested if acceptable: ImGui immediate mode pattern
                    // requires button->validation nesting
    if (actions->StartIndexBuilding(
          settings.crawlFolder)) {  // NOSONAR(cpp:S134) - Nested if acceptable: ImGui immediate
                                    // mode pattern requires button->action nesting
      LOG_INFO_BUILD("Started indexing folder: " << settings.crawlFolder);
    } else {
      LOG_ERROR("Failed to start indexing folder: " + settings.crawlFolder);
    }
  }
  if (!can_start) {
    ImGui::EndDisabled();
  }
  if (ImGui::IsItemHovered()) {
    const char* tooltip_text =
      can_start ? "Start indexing the selected folder. This will replace the current index."
                : display_message.c_str();
    ImGui::SetTooltip("%s", tooltip_text);
  }
}
}  // namespace

// Helper function to render start/stop indexing buttons (reduces cognitive complexity)
void RenderIndexingButtons(
  const AppSettings& settings,
  ui::UIActions* actions) {  // NOSONAR(cpp:S995) - Parameter must be non-const at call site
                             // (RenderIndexConfiguration) to allow folder selection, but this
                             // function only reads
  // Check if indexing is in progress
  bool is_indexing = false;
  if (actions != nullptr) {
    is_indexing = actions->IsIndexBuilding();
  }

  // Start/Stop indexing buttons
  if (is_indexing) {
    RenderStopIndexingButton(actions);
  } else {
    RenderStartIndexingButton(settings, actions);
  }
}

// Helper: Render recrawl interval input
void RenderRecrawlIntervalInput(AppSettings& settings) {
  if (int recrawl_interval = settings.recrawl.intervalMinutes; ImGui::InputInt(  // NOSONAR(cpp:S6004) - ImGui modifies by reference, then used after if
        "Recrawl Interval (minutes)", &recrawl_interval, 1, 5)) {  // NOLINT(readability-magic-numbers) - ImGui step
    recrawl_interval =  // NOSONAR(cpp:S6004) - Issue attached to block; init-statement already used
      (std::max)(settings_defaults::kMinRecrawlIntervalMinutes,
                 (std::min)(recrawl_interval, settings_defaults::kMaxRecrawlIntervalMinutes));
    if (recrawl_interval != settings.recrawl.intervalMinutes) {  // NOSONAR(cpp:S6004) - Init-statement used above; issue may attach to block
      settings.recrawl.intervalMinutes = recrawl_interval;
      LOG_INFO_BUILD("Updated recrawl interval to " << recrawl_interval << " minutes");
    }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("How often to automatically recrawl the folder (1-60 minutes).\n"
                      "Recrawl behavior depends on idle requirement setting below.");
  }
  ImGui::TextDisabled("(Current: %d minutes)", settings.recrawl.intervalMinutes);
}

// Helper: Render recrawl idle requirement and threshold
void RenderRecrawlIdleSettings(AppSettings& settings) {
  bool require_idle = settings.recrawl.requireIdle;  // NOSONAR(cpp:S1854) - ImGui pattern: modified
                                                    // by reference, then used after if
  if (ImGui::Checkbox("Only recrawl when idle", &require_idle) &&
      require_idle != settings.recrawl.requireIdle) {
    settings.recrawl.requireIdle = require_idle;
    LOG_INFO_BUILD(
      "Updated recrawl idle requirement to: " << (require_idle ? "enabled" : "disabled"));
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("If enabled, recrawl only occurs when system and application are idle.\n"
                      "If disabled, recrawl happens immediately when interval elapses.");
  }
  if (require_idle) {
    ImGui::Indent();
    if (int idle_threshold = settings.recrawl.idleThresholdMinutes; ImGui::InputInt(  // NOSONAR(cpp:S6004) - ImGui modifies by reference, then used after if
          "Idle threshold (minutes)", &idle_threshold, 1, 5)) {  // NOLINT(readability-magic-numbers) - ImGui step
      idle_threshold =  // NOSONAR(cpp:S6004) - Issue attached to block; init-statement already used
        (std::max)(settings_defaults::kMinRecrawlIdleThresholdMinutes,
                   (std::min)(idle_threshold, settings_defaults::kMaxRecrawlIdleThresholdMinutes));
      if (idle_threshold != settings.recrawl.idleThresholdMinutes) {  // NOSONAR(cpp:S6004) - Init-statement used above; issue may attach to block
        settings.recrawl.idleThresholdMinutes = idle_threshold;
        LOG_INFO_BUILD("Updated recrawl idle threshold to " << idle_threshold << " minutes");
      }
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("How long system and application must be idle before recrawl (1-30 minutes).");
    }
    ImGui::TextDisabled("(Current: %d minutes)", settings.recrawl.idleThresholdMinutes);
    ImGui::Unindent();
  }
}

// Helper function to render recrawl settings (reduces cognitive complexity)
void RenderRecrawlSettings(AppSettings& settings) {
  if (settings.crawlFolder.empty()) {
    return;
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  ImGui::Text("Periodic Recrawl:");
  ImGui::Spacing();

  RenderRecrawlIntervalInput(settings);
  ImGui::Spacing();
  RenderRecrawlIdleSettings(settings);
}

// Helper function to render recrawl summary (reduces cognitive complexity)
void RenderRecrawlSummary(const AppSettings& settings) {
  if (settings.crawlFolder.empty()) {
    return;
  }

  ImGui::Spacing();
  if (settings.recrawl.requireIdle) {
    ImGui::TextDisabled(
      "Recrawl will run roughly every %d minutes, after the system has been idle for %d minutes.",
      settings.recrawl.intervalMinutes, settings.recrawl.idleThresholdMinutes);
  } else {
    ImGui::TextDisabled(
      "Recrawl will run roughly every %d minutes, even when the system is active.",
      settings.recrawl.intervalMinutes);
  }
  if (settings.recrawl.intervalMinutes <= 2 && !settings.recrawl.requireIdle) {
    ImGui::TextColored(
      Theme::Colors::Warning,
      "Frequent recrawls without idle requirement may increase background CPU and disk usage.");
  }
}
}  // namespace

// Helper function to validate folder path
std::pair<bool, std::string> ValidateFolderPath(std::string_view path) {
  if (path.empty()) {
    return {false, "Folder path is empty"};
  }

  try {
    const std::string path_str(path);
    const std::filesystem::path folder_path(path_str);
    if (!std::filesystem::exists(folder_path)) {
      return {false, "Folder does not exist"};
    }
    if (!std::filesystem::is_directory(folder_path)) {
      return {false, "Path is not a directory"};
    }
    return {true, ""};
  } catch (const std::filesystem::filesystem_error& e) {  // NOLINT(bugprone-empty-catch) - handled by return
    return {false, "Error accessing folder: " + std::string(e.what())};
  } catch (const std::system_error& e) {  // NOLINT(bugprone-empty-catch) - handled by return
    return {false, "System error: " + std::string(e.what())};
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) NOLINT(bugprone-empty-catch) - catch-all; handled by return
    return {false, "Error: " + std::string(e.what())};
  } catch (...) {  // NOSONAR(cpp:S2738) NOLINT(bugprone-empty-catch) - final safety net; handled by return
    return {false, "Unknown error occurred"};
  }
}

// Helper function to render index configuration section
void RenderIndexConfiguration(AppSettings& settings, [[maybe_unused]] FileIndex& file_index,
                              ui::UIActions* actions, FolderBrowser& folder_browser) {
  if (ImGui::CollapsingHeader("Index Configuration")) {
    // Determine if folder selection UI should be visible
    if (const std::string visibility_reason = GetFolderSelectionVisibilityReason();
        !visibility_reason
           .empty()) {  // NOSONAR(cpp:S6004) - Variable used after if statement for display
      ImGui::TextColored(Theme::Colors::Warning, "%s", visibility_reason.c_str());
      ImGui::Spacing();
    }

    // Display current folder path and controls
    RenderFolderPathInput(settings, folder_browser);

    // Validate folder
    RenderFolderValidation(settings);

    // Handle folder browser selection (rendering is done at top level in SettingsWindow::Render)
    if (folder_browser.HasSelected()) {
      settings.crawlFolder = folder_browser.GetSelected();
      LOG_INFO_BUILD("Selected folder for indexing: " << settings.crawlFolder);
      folder_browser.ClearSelected();
    }

    ImGui::Spacing();

    // Start/Stop indexing buttons
    RenderIndexingButtons(settings, actions);

    // Recrawl settings (only show if folder is set)
    RenderRecrawlSettings(settings);

    // Summary of current recrawl behavior
    RenderRecrawlSummary(settings);
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
}

// Helper function to render log file information section
void RenderLogFileSection(
  [[maybe_unused]] GLFWwindow* glfw_window) {  // NOLINT(misc-unused-parameters) - Parameter used
                                               // conditionally in function body
  if (!ImGui::CollapsingHeader("Log File")) {
    return;
  }

  // Get log file path from Logger singleton
  if (const std::string log_file_path = Logger::Instance().GetLogFilePath();
      log_file_path.empty()) {
    ImGui::TextDisabled("(Logging disabled or log file not available)");
  } else {
    ImGui::TextWrapped("Location:");
    ImGui::TextWrapped("%s", log_file_path.c_str());

    ImGui::Spacing();

    if (glfw_window != nullptr) {
      if (ImGui::Button(ICON_FA_COPY " Copy Path to Clipboard")) {
        clipboard_utils::SetClipboardText(glfw_window, log_file_path);
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Click to copy log file path to clipboard");
      }
    } else {
      ImGui::BeginDisabled();
      ImGui::Button(ICON_FA_COPY " Copy Path to Clipboard");
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("GLFW window not available - clipboard operations disabled");
      }
    }
  }

  ImGui::Spacing();
}

void RenderTipsSection() {
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::CollapsingHeader("Tips")) {
    ImGui::TextWrapped("Settings are saved to a small JSON file next to the executable.\n"
                       "Font changes will take effect after restarting the application.\n"
                       "Load balancing strategy takes effect on the next search.\n"
                       "Thread pool size takes effect after saving settings.");

    ImGui::Spacing();

    ImGui::TextDisabled("Changes are only written when you click Save. Close discards unsaved changes.");
    ImGui::TextDisabled("Keyboard: Use Tab/Shift+Tab to move focus, Enter/Space to activate the focused button.");
    ImGui::Spacing();
  }
}

void RenderSaveStatus(bool has_save_result, bool last_save_success,
                      const std::string& last_save_message) {
  if (has_save_result) {
    const ImVec4 status_color = last_save_success ? Theme::Colors::Success : Theme::Colors::Error;
    ImGui::TextColored(status_color, "%s", last_save_message.c_str());
    ImGui::Spacing();
  }
}

// Groups save-feedback outputs to keep HandleSaveAction at or below 7 parameters (Sonar S107).
// Uses references so call sites cannot pass null; avoids null dereference if reused elsewhere.
// Call sites must pass lvalues (e.g. local or static variables) that outlive HandleSaveAction to avoid dangling references.
struct SaveFeedbackState {
  bool* has_save_result;
  bool* last_save_success;
  std::string* last_save_message;
};

void HandleSaveAction(bool* p_open, AppSettings& settings, const AppSettings& working_settings,
                      FileIndex& file_index, SaveFeedbackState save_feedback, bool& previous_open) {
  // Check if thread pool size changed before saving
  AppSettings old_settings;
  LoadSettings(old_settings);
  const bool thread_pool_size_changed =
    (old_settings.searchThreadPoolSize != working_settings.searchThreadPoolSize);

  // Apply working settings to live settings before persisting
  settings = working_settings;

  const bool save_ok = SaveSettings(settings);
  assert(save_feedback.has_save_result != nullptr);
  assert(save_feedback.last_save_success != nullptr);
  assert(save_feedback.last_save_message != nullptr);

  *save_feedback.has_save_result = true;

  if (save_ok) {
    *save_feedback.last_save_success = true;
    *save_feedback.last_save_message = "Settings saved successfully.";

    // If thread pool size changed, reset it so the new size takes effect
    if (thread_pool_size_changed) {
      file_index.ResetThreadPool();
      LOG_INFO_BUILD("Thread pool size changed from " << old_settings.searchThreadPoolSize
                                                      << " to " << settings.searchThreadPoolSize
                                                      << " - thread pool reset");
    }

    *p_open = false;
    previous_open = false;
  } else {
    *save_feedback.last_save_success = false;
    *save_feedback.last_save_message = "Failed to save settings. See log file for details.";
  }
}

}  // namespace

void SettingsWindow::Render(bool* p_open, AppSettings& settings, FileIndex& file_index,
                            ui::UIActions* actions, GLFWwindow* glfw_window) {
  // Get folder browser instance (initialized on first use)
  FolderBrowser& folder_browser = GetFolderBrowser();
  static bool has_save_result = false;
  static bool last_save_success = false;
  static std::string last_save_message;
  static AppSettings working_settings;
  static bool previous_open = false;

  if (p_open == nullptr || !*p_open) {
    previous_open = false;
    return;
  }

  // Initialize working copy when window is opened
  if (!previous_open) {
    working_settings = settings;
    previous_open = true;
    has_save_result = false;
    last_save_message.clear();
  }

  const detail::StyleColorGuard window_bg_guard(ImGuiCol_WindowBg, Theme::Colors::WindowBg);

  // Set window position and size - use FirstUseEver so position is only set once, allowing user to
  // move it
  const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  auto center =
    ImVec2(main_viewport->WorkPos.x +
             (main_viewport->WorkSize.x *
              0.5F),  // NOLINT(readability-math-missing-parentheses,readability-magic-numbers) -
                      // Parentheses added for clarity; 0.5 is standard center factor (50%)
           main_viewport->WorkPos.y +
             (main_viewport->WorkSize.y *
              0.5F));  // NOLINT(readability-math-missing-parentheses,readability-magic-numbers) -
                       // Parentheses added for clarity; 0.5 is standard center factor (50%)
  ImGui::SetNextWindowPos(
    center, ImGuiCond_FirstUseEver,
    ImVec2(0.5F, 0.5F));  // NOLINT(readability-magic-numbers) - 0.5 is standard center factor (50%)
  ImGui::SetNextWindowSize(ImVec2(static_cast<float>(settings_ui_constants::kSettingsWindowWidth),
                                  static_cast<float>(settings_ui_constants::kSettingsWindowHeight)),
                           ImGuiCond_FirstUseEver);

  {
    const detail::WindowGuard window_guard("Settings", p_open, ImGuiWindowFlags_None);
    if (window_guard.ShowContent()) {
      RenderAppearanceSettings(working_settings, glfw_window);
      RenderPerformanceSettings(working_settings);
      RenderIndexConfiguration(working_settings, file_index, actions, folder_browser);
      RenderLogFileSection(glfw_window);

      RenderTipsSection();
      RenderSaveStatus(has_save_result, last_save_success, last_save_message);

      if (ImGui::Button(ICON_FA_SAVE " Save")) {
        // has_save_result, last_save_success, last_save_message are static locals (lvalues); safe for SaveFeedbackState refs
        HandleSaveAction(p_open, settings, working_settings, file_index,
                         SaveFeedbackState{&has_save_result, &last_save_success, &last_save_message},
                         previous_open);
      }
      ImGui::SameLine();
      if (ImGui::Button(ICON_FA_XMARK " Close", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0))) {
        *p_open = false;
        previous_open = false;
        has_save_result = false;
        last_save_message.clear();
      }
    }
  }

  // Render folder browser popup OUTSIDE of Settings window block
  // This is important for modal popups to work correctly on all platforms (especially macOS)
  if (folder_browser.IsOpen()) {
    folder_browser.Render();
  }
}

}  // namespace ui
