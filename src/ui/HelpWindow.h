#pragma once

#include <cstddef>
#include <string_view>

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
   * @param memory_bytes_from_state Optional pointer to throttled process memory (e.g. state.memory_bytes_).
   *        When non-null, the About section uses this value so refresh matches the status bar (~10 s).
   *        When null, process memory is read each frame (e.g. for tests).
   * @param is_monitoring_active True when the USN monitor is running and active (Windows only).
   *        Controls the platform/monitoring label in the About section.
   * @param startup_args_display Full command-line arguments passed at startup (excluding argv[0]).
   *        Empty means no startup arguments were provided.
   */
  static void Render(bool* p_open, const size_t* memory_bytes_from_state = nullptr,
                     bool is_monitoring_active = false,
                     std::string_view startup_args_display = {});
};

}  // namespace ui
