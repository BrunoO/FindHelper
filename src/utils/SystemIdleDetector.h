#pragma once

#include <chrono>

/**
 * @file SystemIdleDetector.h
 * @brief Cross-platform system idle time detection
 *
 * This module provides functions to detect:
 * 1. Process-level idle: Time since last user interaction within this application
 * 2. System-level idle: Time since last system-wide user input (mouse/keyboard)
 *
 * Platform-specific implementations:
 * - Windows: Uses GetLastInputInfo() from user32.dll
 * - macOS: Uses CGEventSourceSecondsSinceLastEventType() from CoreGraphics
 * - Linux: Uses X11 APIs or systemd-logind (fallback to process idle)
 */

namespace system_idle_detector {

/**
 * @brief Get the time since last system-wide user input (mouse/keyboard)
 *
 * This detects system-level idle time - when the entire computer has been idle,
 * not just this application. Useful for power-saving or background tasks.
 *
 * @return Time since last system input in seconds, or -1.0 on error/unsupported platform
 *
 * @note On Linux, this may return -1.0 if X11 is not available. In that case,
 *       use process-level idle detection instead.
 */
double GetSystemIdleTimeSeconds();

/**
 * @brief Check if the system has been idle for at least the specified duration
 *
 * @param idle_threshold_seconds Minimum idle time in seconds to consider system idle
 * @return true if system has been idle for at least idle_threshold_seconds, false otherwise
 */
bool IsSystemIdleFor(double idle_threshold_seconds);

/**
 * @brief Get the time since last process-level user interaction
 *
 * This detects application-level idle time - when this specific application
 * has not received user input. This is tracked by the application itself.
 *
 * @param last_interaction_time Time point of last user interaction
 * @return Time since last interaction in seconds
 */
double GetProcessIdleTimeSeconds(const std::chrono::steady_clock::time_point& last_interaction_time);

/**
 * @brief Check if the process has been idle for at least the specified duration
 *
 * @param last_interaction_time Time point of last user interaction
 * @param idle_threshold_seconds Minimum idle time in seconds to consider process idle
 * @return true if process has been idle for at least idle_threshold_seconds, false otherwise
 */
bool IsProcessIdleFor(const std::chrono::steady_clock::time_point& last_interaction_time,
                      double idle_threshold_seconds);

} // namespace system_idle_detector
