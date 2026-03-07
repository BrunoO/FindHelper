#pragma once

/**
 * @file ui/SettingsWindow.h
 * @brief Settings window rendering component
 *
 * This component handles rendering the settings window, displaying:
 * - Appearance settings (font family, font size, UI scale)
 * - Performance settings (load balancing strategy, thread pool size, dynamic chunk size, hybrid initial work percentage)
 * - Save and Close buttons
 */

// Forward declarations
struct AppSettings;
class FileIndex;
struct GLFWwindow;

namespace ui {
  class UIActions;
}

namespace ui {

/**
 * @class SettingsWindow
 * @brief Static utility class for settings window rendering
 *
 * Renders a floating window displaying application settings organized in sections:
 * - Appearance: Font family, font size, UI scale
 * - Performance: Load balancing strategy, thread pool size, dynamic chunk size,
 *   hybrid initial work percentage (conditional on strategy)
 *
 * Settings are saved to JSON file and some changes require application restart.
 */
class SettingsWindow {
public:
  /**
   * @brief Renders the settings window
   *
   * Displays a floating, resizable window with application settings organized
   * in sections:
   * - Appearance: Font family selection, font size slider, UI scale slider
   * - Performance: Load balancing strategy, thread pool size, dynamic chunk size,
   *   hybrid initial work percentage (only shown for Hybrid strategy)
   * - Index Configuration: Folder selection for crawling (Windows: only when no admin or USN Journal inactive)
   *
   * Settings are saved to a JSON file next to the executable when the Save
   * button is clicked. Font family, font size, and UI scale (including the "Auto"
   * button) apply immediately without restart.
   *
   * @param p_open Pointer to window visibility flag (modified when window is closed)
   * @param settings Application settings (modified in-place, saved on Save button click)
   * @param file_index FileIndex instance (needed to reset thread pool when size changes)
   * @param actions UI actions interface (for runtime index building, may be nullptr)
   * @param glfw_window GLFW window handle (for clipboard operations, may be nullptr)
   */
  static void Render(bool *p_open, AppSettings &settings, FileIndex &file_index, UIActions* actions = nullptr, GLFWwindow* glfw_window = nullptr);
};

} // namespace ui


