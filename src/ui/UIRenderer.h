#pragma once

/**
 * @file UIRenderer.h
 * @brief Coordinates rendering of all UI components
 *
 * This class handles the main window setup and coordinates rendering of all UI components.
 * It replaces rendering methods previously in the Application class, providing better
 * separation of concerns.
 */

#include <atomic>

#include "gui/GuiState.h"

// Forward declarations
struct AppSettings;
class FileIndex;
class ThreadPool;
class UsnMonitor;
class SearchWorker;
struct GLFWwindow;

namespace ui {
  class UIActions;
}

// NativeWindowHandle forward declaration (platform-specific)
#ifdef _WIN32
struct HWND__;
using NativeWindowHandle = HWND__*; // HWND on Windows (opaque pointer)
#else
using NativeWindowHandle = void*; // NOSONAR(cpp:S5008) - Cross-platform abstraction: always nullptr on non-Windows
#endif  // _WIN32

namespace ui {

/**
 * @struct RenderMainWindowContext
 * @brief Context parameters for rendering main window
 */
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init,cppcoreguidelines-avoid-const-or-ref-data-members) - Struct with reference members for context passing (intentional design)
struct RenderMainWindowContext {
  GuiState& state;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) - Reference member for context passing
  ui::UIActions* actions;
  AppSettings& settings;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) - Reference member for context passing
  FileIndex& file_index;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) - Reference member for context passing
  ThreadPool& thread_pool;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) - Reference member for context passing
  UsnMonitor* monitor;
  SearchWorker& search_worker;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) - Reference member for context passing
  NativeWindowHandle native_window;
  GLFWwindow* glfw_window;
  std::atomic<bool>& show_settings;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) - Reference member for context passing
  std::atomic<bool>& show_metrics;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) - Reference member for context passing
  bool is_index_building;
  bool metrics_available;
  /** When non-null (ImGui Test Engine build), UI may show "Test Engine" button to set *show_test_engine_window = true. */
  bool* show_test_engine_window = nullptr;
};

/**
 * @struct RenderFloatingWindowsContext
 * @brief Context parameters for rendering floating windows
 */
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init,cppcoreguidelines-avoid-const-or-ref-data-members) - Struct with reference members for context passing (intentional design)
struct RenderFloatingWindowsContext {
  GuiState& state;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) - Reference member for context passing
  ui::UIActions* actions;
  AppSettings& settings;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) - Reference member for context passing
  FileIndex& file_index;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) - Reference member for context passing
  UsnMonitor* monitor;
  SearchWorker& search_worker;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) - Reference member for context passing
  GLFWwindow* glfw_window;
  std::atomic<bool>& show_settings;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) - Reference member for context passing
  std::atomic<bool>& show_metrics;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) - Reference member for context passing
  bool metrics_available;
};

/**
 * @class UIRenderer
 * @brief Coordinates rendering of all UI components
 *
 * This class handles the main window setup and coordinates rendering of all UI components.
 * It replaces rendering methods previously in the Application class, providing better
 * separation of concerns.
 */
class UIRenderer {
 public:
  /**
   * @brief Render main window content
   * @param context Rendering context with all required parameters
   */
  static void RenderMainWindow(const RenderMainWindowContext& context);

  /**
   * @brief Render floating windows (Settings, Metrics, etc.)
   * @param context Rendering context with all required parameters
   *
   * Note: SettingsWindow currently takes Application* (will be updated in Phase 4).
   * For now, we pass actions cast to Application*.
   */
  static void RenderFloatingWindows(const RenderFloatingWindowsContext& context);

 private:
  /**
   * @brief Count active filters in state
   * @param state GUI state (read-only)
   * @return Number of active filters
   */
  static int CountActiveFilters(const GuiState& state);

  /**
   * @brief Render manual search header (collapsible section)
   * @param state GUI state (modified: manualSearchExpanded)
   * @param show_settings Settings window visibility flag (modified by buttons)
   * @param show_metrics Metrics window visibility flag (modified by buttons)
   * @param is_expanded Output: whether section is expanded
   * @param metrics_available Whether Metrics UI is available
   */
  static void RenderManualSearchHeader(GuiState& state,
                                         bool& is_expanded);

  /**
   * @brief Render manual search content (search inputs and controls)
   * @param state GUI state
   * @param actions UI actions interface
   * @param monitor USN monitor (may be nullptr, read-only)
   * @param glfw_window GLFW window handle
   * @param is_expanded Whether section is expanded
   * @param is_index_building Whether index is building
   * @param settings Application settings (read-only, used to check UI mode)
   */
  static void RenderManualSearchContent(GuiState& state,
                                         ui::UIActions* actions,
                                         const UsnMonitor* monitor,
                                         GLFWwindow* glfw_window,
                                         bool is_expanded,
                                         bool is_index_building,
                                         const AppSettings& settings);

  /**
   * @brief Render AI-assisted search section (collapsible)
   * @param context Main window rendering context (state, actions, settings, glfw_window,
   *        show_settings, show_metrics, is_index_building, metrics_available)
   */
  static void RenderAISearchSection(const RenderMainWindowContext& context);
};

}  // namespace ui
