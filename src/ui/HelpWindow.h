#pragma once

/**
 * @file ui/HelpWindow.h
 * @brief Help window (keyboard shortcuts) rendering component
 *
 * Renders a normal floating window with keyboard shortcuts reference,
 * so users can keep it visible while learning shortcuts (same pattern as Metrics/Settings).
 */

namespace ui {

/**
 * @class HelpWindow
 * @brief Static utility class for Help (keyboard shortcuts) window rendering
 */
class HelpWindow {
public:
  /**
   * @brief Renders the Help window
   *
   * Displays a floating, resizable window with keyboard shortcuts for
   * global, search, file operations, and mark mode.
   *
   * @param p_open Pointer to window visibility flag (modified when window is closed)
   */
  static void Render(bool* p_open);
};

}  // namespace ui
