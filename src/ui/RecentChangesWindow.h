#pragma once

/**
 * @file ui/RecentChangesWindow.h
 * @brief Window rendering component for displaying recent filesystem changes from USN Journal
 */

class UsnMonitor;

namespace ui {

/**
 * @class RecentChangesWindow
 * @brief Static utility class for recent changes window rendering
 */
class RecentChangesWindow {
 public:
  /**
   * @brief Renders the recent changes window
   *
   * Displays a resizable tool window with a live feed of USN journal change events.
   *
   * @param p_open Pointer to window visibility flag
   * @param monitor Pointer to UsnMonitor (nullptr on non-Windows or when inactive)
   */
  static void Render(bool *p_open, UsnMonitor *monitor);
};

}  // namespace ui
