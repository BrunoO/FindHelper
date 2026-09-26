#pragma once

/**
 * @file ui/FilterPanel.h
 * @brief Filter panel rendering component
 *
 * This component handles rendering all filter-related UI elements:
 * - Quick filter buttons (file types and location shortcuts)
 * - Time-based quick filters
 * - Active filter indicators and badges
 * - Saved searches dropdown
 */

#include <atomic>
#include <functional>

#include "gui/UIActions.h"

// Forward declarations
class GuiState;
struct AppSettings;

namespace ui {

/**
 * @class FilterPanel
 * @brief Static utility class for filter panel rendering
 *
 * Renders all filter-related UI components:
 * - Quick filter buttons (Documents, Executables, Videos, Pictures, Current User, Desktop, Downloads, Recycle Bin)
 * - Time-based quick filters (Today, This Week, This Month, This Year, Older)
 * - Size-based quick filters (Empty, Tiny, Small, Medium, Large, Huge, Massive)
 * - Saved searches dropdown and controls
 * - Active filter badges (Extensions, Filename, Path, Folders Only, Time Filter, Size Filter)
 *
 */
class FilterPanel {
public:
  /**
   * @brief Renders quick filter buttons
   *
   * Displays horizontal row of buttons for:
   * - File type filters: Documents, Executables, Videos, Pictures
   * - Location shortcuts: Current User, Desktop, Downloads, Recycle Bin
   *
   * @param state GUI state containing input buffers (modified by button clicks)
   */
  static void RenderQuickFilters(GuiState &state);

  /**
   * @brief Renders time-based quick filter buttons
   *
   * Displays buttons for filtering by last modification time:
   * - Today: Files modified today
   * - This Week: Files modified this week
   * - This Month: Files modified this month
   * - This Year: Files modified this year
   * - Older: Files modified before this year
   *
   * Also displays build/CPU feature summary on the right side of the row.
   *
   * @param state GUI state containing time filter setting (modified by button clicks)
   */
  static void RenderTimeQuickFilters(GuiState &state);

  /**
   * @brief Renders size-based quick filter buttons
   *
   * Displays buttons for filtering by file size:
   * - Empty: 0 bytes
   * - Tiny: < 1 KB
   * - Small: 1 KB - 100 KB
   * - Medium: 100 KB - 10 MB
   * - Large: 10 MB - 100 MB
   * - Huge: 100 MB - 1 GB
   * - Massive: > 1 GB
   *
   * @param state GUI state containing size filter setting (modified by button clicks)
   */
  static void RenderSizeQuickFilters(GuiState &state);

  /**
   * @brief Renders active filter indicators
   *
   * Displays active filter badges (Extensions, Filename, Path, Folders Only, Time Filter, Size Filter)
   * with clear buttons for each active filter.
   *
   * @param state GUI state containing filter settings (modified by badge clicks)
   */
  static void RenderActiveFilterIndicators(GuiState &state);

  /**
   * @brief Renders a single filter badge with clear button
   *
   * @param label Text label for the filter badge
   * @param is_active Whether the filter is currently active
   * @param on_clear Callback function to execute when clear button is clicked
   */
  static void RenderFilterBadge(const char *label, bool is_active,
                                const std::function<void()> &on_clear);

  /**
   * @brief Checks if extension filter is currently active
   *
   * @param state GUI state containing extension input
   * @return true if extension filter has non-empty, non-default value
   */
  [[nodiscard]] static bool IsExtensionFilterActive(const GuiState &state);

  /**
   * @brief Checks if filename filter is currently active
   *
   * @param state GUI state containing filename input
   * @return true if filename input is non-empty
   */
  [[nodiscard]] static bool IsFilenameFilterActive(const GuiState &state);

  /**
   * @brief Checks if path filter is currently active
   *
   * @param state GUI state containing path input
   * @return true if path input is non-empty
   */
  [[nodiscard]] static bool IsPathFilterActive(const GuiState &state);

};

} // namespace ui

