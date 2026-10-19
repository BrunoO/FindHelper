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
class GuiState;
namespace diagnostics {
class MetricsFlightRecorder;
}  // namespace diagnostics

namespace ui::metrics_window {

/**
 * @brief Renders the metrics window
 *
 * Displays a floating, resizable two-column window with performance metrics
 * organized in collapsing sections:
 * - Left column:
 *   - Automatic Diagnostics & Flight Recorder: Live health, last incident post-mortem, manual dump
 *   - System Performance (cross-platform): FPS, search threads, index build & acceleration memory
 *   - USN Buffers (Windows only): Counts, queue, read/process timing, lock contention, throughput
 * - Right column:
 *   - Processing Statistics (Windows only): Record counts, index maintenance stats
 *   - File Operations (Windows only): Created/deleted/renamed/modified counts
 *   - Parent Placeholders (Windows only): Entries awaiting a missing parent
 *   - Error Statistics (Windows only): Error counts and streaks
 *   - Performance Summary (Windows only): Record / file-operation rates
 *   - Search Performance (cross-platform): Search metrics, timing, throughput
 *
 * @param p_open Pointer to window visibility flag (modified when window is closed)
 * @param monitor UsnMonitor pointer (nullptr on macOS; const for read/metrics reset)
 * @param search_worker Search worker for search performance metrics
 * @param file_index File index for maintenance statistics
 * @param state GUI state (read-only; used for index build timing)
 * @param flight_recorder Optional MetricsFlightRecorder pointer for live/post-mortem diagnostics
 */
void Render(bool *p_open,
            [[maybe_unused]] const UsnMonitor *monitor,
            SearchWorker *search_worker,
            FileIndex &file_index,
            const GuiState &state,
            diagnostics::MetricsFlightRecorder *flight_recorder = nullptr);

}  // namespace ui::metrics_window

