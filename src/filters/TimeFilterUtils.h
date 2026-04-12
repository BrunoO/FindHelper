#pragma once

/**
 * @file TimeFilterUtils.h
 * @brief Cross-platform helper functions for TimeFilter and SavedSearch operations
 *
 * This module provides utilities for:
 * - Converting TimeFilter enum to/from persisted string values
 * - Applying SavedSearch presets to GuiState
 * - Calculating FILETIME cutoff values for time filters (platform-specific)
 *
 * The time filter cutoff calculation is platform-specific:
 * - Windows: Uses SYSTEMTIME/FILETIME APIs
 * - macOS/Linux: Uses std::chrono with Unix-to-Windows epoch conversion
 */

#include "filters/TimeFilter.h"       // TimeFilter enum
#include "utils/FileTimeTypes.h"    // FILETIME
#include <ctime>
#include <string>

/**
 * @brief Map TimeFilter enum to persisted string value
 *
 * Converts a TimeFilter enum value to its string representation for persistence
 * in settings files.
 *
 * @param filter TimeFilter enum value
 * @return String representation (e.g., "Today", "ThisWeek", "None")
 */
const char *TimeFilterToString(TimeFilter filter);

/**
 * @brief Map persisted string back to TimeFilter enum
 *
 * Converts a string value from settings files back to a TimeFilter enum.
 * Returns TimeFilter::None if the string is unrecognized.
 *
 * @param value String representation (e.g., "Today", "ThisWeek")
 * @return TimeFilter enum value, or TimeFilter::None if unrecognized
 */
TimeFilter TimeFilterFromString(std::string_view value);

/**
 * @brief Returns the start of the current week (Monday at midnight, local time)
 *        as a std::time_t.
 *
 * Cross-platform inline helper shared by the time filter implementation and
 * test utilities.  Uses localtime_s (Windows) / localtime_r (POSIX) internally.
 * Inlined to avoid a link-time dependency on TimeFilterUtils.cpp in test binaries
 * that include this header but do not link the full filter module.
 */
inline std::time_t GetStartOfThisWeekTimeT() {
  const std::time_t now = std::time(nullptr);
  std::tm now_tm{};  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - zero-init; localtime_s/localtime_r fill all fields
#ifdef _WIN32
  localtime_s(&now_tm, &now);
#else
  localtime_r(&now, &now_tm);
#endif  // _WIN32
  const int days_since_monday = (now_tm.tm_wday == 0) ? 6 : (now_tm.tm_wday - 1);  // NOLINT(readability-magic-numbers) - 6 is days from Sunday to Monday (tm_wday: 0=Sunday, 6=Saturday)
  std::tm week_start = now_tm;
  week_start.tm_mday -= days_since_monday;
  week_start.tm_hour = 0;
  week_start.tm_min = 0;
  week_start.tm_sec = 0;
  week_start.tm_isdst = -1;  // Let system determine DST
  return std::mktime(&week_start);
}

/**
 * @brief Platform-specific time filter cutoff calculation
 *
 * Calculates a FILETIME cutoff value for the given time filter:
 * - Today: Start of today (midnight in local timezone)
 * - ThisWeek: Start of current week (Monday at midnight in local timezone)
 * - ThisMonth: Start of current month (1st day at midnight in local timezone)
 * - ThisYear: Start of current year (January 1st at midnight in local timezone)
 * - Older: 1 year before current time (in local timezone)
 *
 * Platform-specific implementations:
 * - Windows: Uses SYSTEMTIME/FILETIME APIs
 * - macOS/Linux: Uses std::chrono with Unix-to-Windows epoch conversion
 *
 * @param filter TimeFilter enum value
 * @return FILETIME cutoff value, or sentinel {0, 0} for TimeFilter::None
 */
FILETIME CalculateCutoffTime(TimeFilter filter);

