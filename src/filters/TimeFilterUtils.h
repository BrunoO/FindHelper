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
#include <string>

// Forward declarations (full headers in .cpp)
class GuiState;
struct SavedSearch;

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
 * @brief Apply a SavedSearch preset to the current GuiState
 *
 * Applies all saved search parameters to the GUI state:
 * - Path, extension, and filename inputs
 * - Folders-only and case-sensitive flags
 * - Time filter value
 * - Marks inputs as changed to trigger search
 *
 * @param saved SavedSearch preset to apply
 * @param state GUI state to modify
 */
void ApplySavedSearchToGuiState(const SavedSearch &saved, GuiState &state);

// Forward declarations
struct SearchParams;
struct AppSettings;

/**
 * @brief Record a recent search in AppSettings
 *
 * Converts SearchParams and GuiState to SavedSearch and adds it to the front
 * of recentSearches. Limits to settings_defaults::kMaxRecentSearches (removes oldest when over limit).
 * The name field is left empty (unlike savedSearches which require names).
 *
 * @param params Search parameters from the completed search
 * @param state GUI state containing filter information
 * @param settings AppSettings to update (will be modified)
 */
void RecordRecentSearch(const SearchParams &params, const GuiState &state, AppSettings &settings);

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

