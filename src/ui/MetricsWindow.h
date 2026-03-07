#pragma once

/**
 * @file ui/MetricsWindow.h
 * @brief Metrics window rendering component
 *
 * This component handles rendering the metrics window, displaying:
 * - Processing statistics (Windows only)
 * - File operations statistics (Windows only)
 * - Queue statistics (Windows only)
 * - Error statistics (Windows only)
 * - Timing statistics (Windows only)
 * - Performance summary (Windows only)
 * - Search performance metrics (cross-platform)
 */

#include <cstdint>

// Forward declarations
class UsnMonitor;
class SearchWorker;
class FileIndex;

namespace ui {

/**
 * @class MetricsWindow
 * @brief Static utility class for metrics window rendering
 *
 * Renders a floating window displaying performance metrics:
 * - Windows-specific USN journal monitoring metrics (if monitor available)
 * - Cross-platform search performance metrics
 * - Reset buttons for metrics
 */
class MetricsWindow {
public:
  /**
   * @brief Renders the metrics window
   *
   * Displays a floating, resizable window with performance metrics organized
   * in collapsing sections:
   * - Processing Statistics (Windows only): Buffer/record counts, maintenance stats
   * - File Operations (Windows only): Created/deleted/renamed/modified counts
   * - Queue Statistics (Windows only): Queue depth, dropped buffers
   * - Error Statistics (Windows only): Error counts and streaks
   * - Timing Statistics (Windows only): Read/process times, averages
   * - Performance Summary (Windows only): Throughput rates
   * - Search Performance (cross-platform): Search metrics, timing, throughput
   *
   * @param p_open Pointer to window visibility flag (modified when window is closed)
   * @param monitor UsnMonitor pointer (nullptr on macOS, used for Windows metrics)
   * @param search_worker Search worker for search performance metrics
   * @param file_index File index for maintenance statistics
   */
  static void Render(bool *p_open,
                           [[maybe_unused]] UsnMonitor *monitor,
                           SearchWorker *search_worker,
                           FileIndex &file_index);

  /**
   * @brief Helper to render metric text with tooltip
   *
   * Renders formatted text using printf-style formatting and displays a tooltip
   * on hover if provided. Public so that implementation helpers in the .cpp
   * (e.g. RenderMonitorProcessingStatistics) can call it.
   *
   * @param tooltip Tooltip text to display on hover (can be nullptr or empty)
   * @param fmt Printf-style format string
   * @param ... Format arguments
   */
  static void RenderMetricText(const char *tooltip, const char *fmt, ...);

private:

  /**
   * @brief Render result processing rate metric
   *
   * Calculates and displays the average result throughput in results per second.
   *
   * @param total_results_found Total number of results found
   * @param total_time_ms Total time in milliseconds (search + postprocess)
   */
  static void RenderResultThroughputMetric(uint64_t total_results_found,
                                           uint64_t total_time_ms);

  /**
   * @brief Render time distribution metrics
   *
   * Calculates and displays the breakdown of total search time between
   * search and post-processing phases.
   *
   * @param total_search_time_ms Time spent in search phase (milliseconds)
   * @param total_postprocess_time_ms Time spent in post-processing phase (milliseconds)
   */
  static void RenderTimeDistributionMetrics(uint64_t total_search_time_ms,
                                           uint64_t total_postprocess_time_ms);

  /** Renders Windows-only USN monitor metrics section (if monitor non-null). */
  static void RenderMonitorMetricsSection(const UsnMonitor *monitor,
                                          const FileIndex &file_index);

  /** Renders search performance section (if search_worker non-null). */
  static void RenderSearchPerformanceSection(SearchWorker *search_worker);

  /** Renders system performance section (FPS, thread count). */
  static void RenderSystemPerformanceSection(FileIndex &file_index);
};

} // namespace ui

