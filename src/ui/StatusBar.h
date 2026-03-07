#pragma once

/**
 * @file ui/StatusBar.h
 * @brief Status bar rendering component
 *
 * This component handles rendering the status bar at the bottom of the main window,
 * displaying application version, monitoring status, search metrics, and system information.
 *
 * The status bar is organized into three logical groups:
 * - Left: Version, build type, monitoring status (colored dot icon), volume (Windows)
 * - Center: Total indexed items, displayed results count, queue size (during building), search time (when completed)
 * - Right: Search status, memory usage
 *
 * Technical details (FPS, thread count) have been moved to the Metrics window.
 */

#include <string_view>

// Forward declarations
class GuiState;
class SearchWorker;
class UsnMonitor;
class FileIndex;

namespace ui {

/**
 * @class StatusBar
 * @brief Static utility class for status bar rendering
 *
 * Renders the status bar with:
 * - Version and build type (left group)
 * - Monitoring status with colored dot icon (left group, Windows only)
 * - Total indexed items count (center group)
 * - Displayed results count (center group, with filtered count if applicable)
 * - Queue size indicator (center group, Windows only, shown during index building)
 * - Last search time (center group, shown only when search completed)
 * - Search status (right group: Searching.../Idle/Loading attributes...)
 * - Memory usage (right group)
 *
 * Note: FPS and thread count have been moved to the Metrics window to reduce
 * status bar clutter.
 */
class StatusBar {
public:
  /**
   * @brief Renders the status bar at the bottom of the main window
   *
   * Displays application information organized into three groups:
   *
   * Left Group:
   * - Version and build type: "v-X.X.X (Release/Debug, Boost/FS)"
 * - Monitoring status: Colored dot icon "[*]" with tooltip (Windows only)
 *   - Green: Active and healthy
 *   - Red: Inactive or errors (buffers dropped)
 *   - Yellow: Building index
 *   - Volume (e.g. D:) shown when configured (Windows), confirms --win-volume / settings
 *
 * Center Group:
 * - Total indexed items: Count from monitor (Windows) or file index (macOS)
 * - Displayed results: Count of visible results (with filtered count if filters active)
 * - Queue size: Current USN buffer queue depth (Windows only, shown only during index building)
 * - Last search time: Duration of most recent search in ms or seconds (shown only when completed)
   *
   * Right Group:
   * - Search status: "Searching...", "Idle", or "Loading attributes..."
   * - Memory usage: Current process memory (updated every 10 seconds, cross-platform)
   *
   * @param state GUI state containing search results and filter state
   * @param search_worker Search worker for metrics (search time, status)
   * @param monitor UsnMonitor pointer (nullptr on macOS, used for monitoring status and queue size)
   * @param file_index File index for total items count (macOS) and filter cache updates
   * @param monitored_volume Volume label to show on Windows (e.g. "D:" from --win-volume or settings), empty on other platforms
   */
  static void Render(GuiState &state,
                     const SearchWorker &search_worker,
                     const UsnMonitor *monitor,
                     const FileIndex &file_index,
                     std::string_view monitored_volume = {});
};

} // namespace ui

