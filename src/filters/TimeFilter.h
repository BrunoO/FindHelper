#pragma once

/**
 * @file TimeFilter.h
 * @brief TimeFilter enum for "Last Modified" quick filters
 *
 * This enum is separated from GuiState.h to allow time filter utilities
 * to be used and tested independently of the GUI layer.
 */

// Time filter options for "Last Modified" quick filters
// NOLINTNEXTLINE(performance-enum-size) - int base type is acceptable for enum, provides better compatibility
enum class TimeFilter {
  None,
  Today,     // Files modified today (since midnight)
  ThisWeek,  // Files modified this week (since Monday)
  ThisMonth, // Files modified this month (since 1st of month)
  ThisYear,  // Files modified this year (since January 1st)
  Older      // Files modified more than 1 year ago
};

