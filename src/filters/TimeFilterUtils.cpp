/**
 * @file TimeFilterUtils.cpp
 * @brief Implementation of cross-platform TimeFilter and SavedSearch utilities
 *
 * This module implements:
 * - TimeFilter enum to/from string conversion
 * - SavedSearch application to GuiState
 * - Platform-specific FILETIME cutoff calculation for time filters
 *
 * The Windows implementation uses SYSTEMTIME/FILETIME APIs for accurate
 * time calculations. The macOS/Linux implementation uses std::chrono for
 * equivalent calculations with proper Unix-to-Windows epoch conversion.
 */

#include "filters/TimeFilterUtils.h"

#include <algorithm>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#endif  // _WIN32

#include "core/Settings.h"
#include "filters/SizeFilterUtils.h"
#include "gui/GuiState.h"
#include "imgui.h"
#include "search/SearchWorker.h"
#include "utils/FileTimeTypes.h"
#include "utils/StringUtils.h"

const char *TimeFilterToString(TimeFilter filter) {
  switch (filter) {
  case TimeFilter::Today:
    return "Today";
  case TimeFilter::ThisWeek:
    return "ThisWeek";
  case TimeFilter::ThisMonth:
    return "ThisMonth";
  case TimeFilter::ThisYear:
    return "ThisYear";
  case TimeFilter::Older:
    return "Older";
  case TimeFilter::None:
  default:
    return "None";
  }
}

TimeFilter TimeFilterFromString(std::string_view value) {
  if (value == "Today") {
    return TimeFilter::Today;
  }
  if (value == "ThisWeek") {
    return TimeFilter::ThisWeek;
  }
  if (value == "ThisMonth") {
    return TimeFilter::ThisMonth;
  }
  if (value == "ThisYear") {
    return TimeFilter::ThisYear;
  }
  if (value == "Older") {
    return TimeFilter::Older;
  }
  return TimeFilter::None;
}

void ApplySavedSearchToGuiState(const SavedSearch &saved, GuiState &state) {
  state.pathInput.SetValue(saved.path);
  state.extensionInput.SetValue(saved.extensions);
  state.filenameInput.SetValue(saved.filename);
  state.foldersOnly = saved.foldersOnly;
  state.caseSensitive = saved.caseSensitive;
  state.timeFilter = TimeFilterFromString(saved.timeFilter);
  state.sizeFilter = SizeFilterFromString(saved.sizeFilter);
  // Restore AI Search description (copy to fixed-size buffer, truncate if needed)
  strcpy_safe(state.gemini_description_input_.data(), state.gemini_description_input_.size(),
              saved.aiSearchDescription.c_str());
  state.MarkInputChanged();
}

// Helper function to check if two SavedSearch objects are identical
// (ignoring the name field, which is not used for recent searches)
static bool AreSearchesIdentical(const SavedSearch &a, const SavedSearch &b) {
  return a.filename == b.filename &&
         a.extensions == b.extensions &&
         a.path == b.path &&
         a.foldersOnly == b.foldersOnly &&
         a.caseSensitive == b.caseSensitive &&
         a.timeFilter == b.timeFilter &&
         a.sizeFilter == b.sizeFilter &&
         a.aiSearchDescription == b.aiSearchDescription;
}

void RecordRecentSearch(const SearchParams &params, const GuiState &state, AppSettings &settings) {
  // Convert SearchParams and GuiState to SavedSearch.
  // Use SearchParams snapshot for inputs so recent searches are robust to
  // mid-search UI changes (e.g., ClearInputs()) that mutate GuiState.
  SavedSearch recent;
  recent.name = "";  // Empty name for recent searches (unlike savedSearches)
  recent.filename = params.filenameInput;
  recent.extensions = params.extensionInput;
  recent.path = params.pathInput;
  recent.foldersOnly = params.foldersOnly;
  recent.caseSensitive = params.caseSensitive;
  recent.timeFilter = TimeFilterToString(state.timeFilter);
  recent.sizeFilter = SizeFilterToString(state.sizeFilter);
  recent.aiSearchDescription = "";  // Recent searches don't store AI descriptions

  // Check if an identical search already exists and remove it
  auto it = std::find_if(settings.recentSearches.begin(), settings.recentSearches.end(),
                         [&recent](const SavedSearch &existing) {
                           return AreSearchesIdentical(existing, recent);
                         });
  if (it != settings.recentSearches.end()) {  // NOSONAR(cpp:S6004): it is used after the if block (in erase call)
    // Remove the existing duplicate
    settings.recentSearches.erase(it);
  }

  // Add to front of recentSearches (or re-add if it was a duplicate)
  settings.recentSearches.insert(settings.recentSearches.begin(), std::move(recent));

  if (settings.recentSearches.size() > settings_defaults::kMaxRecentSearches) {
    settings.recentSearches.resize(settings_defaults::kMaxRecentSearches);
  }
}

#ifdef _WIN32

namespace time_filter_detail {
  /**
   * @brief Set SYSTEMTIME to midnight (00:00:00.000)
   * 
   * Helper function to eliminate duplication across time filter calculations.
   */
  inline void SetTimeToMidnight(SYSTEMTIME& st) {
    st.wHour = 0;
    st.wMinute = 0;
    st.wSecond = 0;
    st.wMilliseconds = 0;
  }

  /**
   * @brief Convert local SYSTEMTIME to FILETIME (UTC)
   * 
   * Helper function to eliminate duplication across time filter calculations.
   * Converts a local time SYSTEMTIME to UTC and then to FILETIME format.
   */
  inline FILETIME ConvertLocalTimeToFileTime(const SYSTEMTIME& st_local) {
    SYSTEMTIME st_utc;
    TzSpecificLocalTimeToSystemTime(nullptr, &st_local, &st_utc);
    FILETIME ft;
    SystemTimeToFileTime(&st_utc, &ft);
    return ft;
  }
} // namespace time_filter_detail

FILETIME CalculateCutoffTime(TimeFilter filter) {
  switch (filter) {
  case TimeFilter::Today: {
    // Start of today (midnight in local timezone)
    SYSTEMTIME st_local;
    GetLocalTime(&st_local);
    time_filter_detail::SetTimeToMidnight(st_local);
    return time_filter_detail::ConvertLocalTimeToFileTime(st_local);
  }
  case TimeFilter::ThisWeek: {
    // Start of current week (Monday in local timezone)
    SYSTEMTIME st_local;
    GetLocalTime(&st_local);
    
    // Calculate days since Monday (0=Monday, 1=Tuesday, ..., 6=Sunday)
    int days_since_monday = (st_local.wDayOfWeek == 0) ? 6 : (st_local.wDayOfWeek - 1);
    
    // Subtract days to get to Monday
    FILETIME ft_local;
    SystemTimeToFileTime(&st_local, &ft_local);
    // Convert FILETIME to ULONGLONG using std::memcpy (safe type-punning)
    ULONGLONG filetime_value;
    static_assert(sizeof(FILETIME) == sizeof(ULONGLONG),
                  "FILETIME and ULONGLONG must have the same size for safe type-punning");
    std::memcpy(&filetime_value, &ft_local, sizeof(FILETIME));
    // Use ULONGLONG type for constants to match QuadPart type
    const ULONGLONG nanoseconds_per_day = static_cast<ULONGLONG>(24) * 60 * 60 * 10000000;
    filetime_value -= static_cast<ULONGLONG>(days_since_monday) * nanoseconds_per_day;
    // Convert back to FILETIME using std::memcpy
    std::memcpy(&ft_local, &filetime_value, sizeof(FILETIME));
    
    // Convert back to SYSTEMTIME to set time to midnight
    FileTimeToSystemTime(&ft_local, &st_local);
    time_filter_detail::SetTimeToMidnight(st_local);
    return time_filter_detail::ConvertLocalTimeToFileTime(st_local);
  }
  case TimeFilter::ThisMonth: {
    // Start of current month (1st day at midnight in local timezone)
    SYSTEMTIME st_local;
    GetLocalTime(&st_local);
    st_local.wDay = 1;
    time_filter_detail::SetTimeToMidnight(st_local);
    return time_filter_detail::ConvertLocalTimeToFileTime(st_local);
  }
  case TimeFilter::ThisYear: {
    // Start of current year (Jan 1st at midnight in local timezone)
    SYSTEMTIME st_local;
    GetLocalTime(&st_local);
    st_local.wMonth = 1;
    st_local.wDay = 1;
    time_filter_detail::SetTimeToMidnight(st_local);
    return time_filter_detail::ConvertLocalTimeToFileTime(st_local);
  }
  case TimeFilter::Older: {
    // More than 1 year ago (1 year before now in local timezone)
    SYSTEMTIME st_local;
    GetLocalTime(&st_local);
    st_local.wYear -= 1;
    return time_filter_detail::ConvertLocalTimeToFileTime(st_local);
  }
  default:
    return FILETIME{0, 0}; // Sentinel
  }
}

#else

// Cross-platform implementation using std::chrono
// Converts std::chrono time_point to FILETIME format
#include <chrono>  // NOSONAR(cpp:S954) - Includes must be after function definitions for this platform-specific implementation
#include <ctime>  // NOSONAR(cpp:S954) - Includes must be after function definitions for this platform-specific implementation

/**
 * @brief Converts a std::chrono time_point to FILETIME
 * 
 * Windows FILETIME: 100-nanosecond intervals since 1601-01-01 00:00:00 UTC
 * Unix epoch: seconds since 1970-01-01 00:00:00 UTC
 * Difference: 11644473600 seconds (369 years)
 */
static FILETIME ChronoToFileTime(std::chrono::system_clock::time_point tp) {
  // Get seconds since Unix epoch
  auto duration = tp.time_since_epoch();
  auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
  auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration - seconds);
  constexpr int64_t hundred_nanoseconds_per_second = 10000000LL;
  constexpr int64_t nanoseconds_per_hundred_nanosecond = 100;
  constexpr uint64_t low_32_bits_mask = 0xFFFFFFFFULL;
  int64_t total_100ns = (seconds.count() + file_time_constants::kEpochDiffSeconds) * hundred_nanoseconds_per_second;
  total_100ns += nanoseconds.count() / nanoseconds_per_hundred_nanosecond;  // Truncate to 100-ns precision
  
  // Split into high and low 32-bit parts
  constexpr int bits_per_32_bit_word = 32;
  FILETIME ft;
  // NOLINTNEXTLINE(hicpp-signed-bitwise) - FILETIME requires bitwise operations on signed int64_t
  ft.dwLowDateTime = static_cast<uint32_t>(total_100ns & static_cast<int64_t>(low_32_bits_mask));
  // NOLINTNEXTLINE(hicpp-signed-bitwise) - FILETIME requires bitwise operations on signed int64_t
  ft.dwHighDateTime = static_cast<uint32_t>((total_100ns >> bits_per_32_bit_word) & static_cast<int64_t>(low_32_bits_mask));
  
  return ft;
}

namespace time_filter_detail {
  /**
   * @brief Get current local time as tm struct
   * 
   * Helper function to eliminate duplication across time filter calculations.
   */
  inline std::tm GetLocalTime(std::chrono::system_clock::time_point now) {
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm{};  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - zero-init; localtime_s/localtime_r fill all fields
#ifdef _WIN32
    localtime_s(&now_tm, &now_time_t);
#else
    localtime_r(&now_time_t, &now_tm);
#endif  // _WIN32
    return now_tm;
  }

  /**
   * @brief Create tm struct with time set to midnight and convert to FILETIME
   * 
   * Helper function to eliminate duplication across time filter calculations.
   * Creates a tm struct based on the provided time components, sets time to midnight,
   * and converts to FILETIME format.
   */
  inline FILETIME CreateTimeStructAndConvert(
      int year, int mon, int mday,
      [[maybe_unused]] std::chrono::system_clock::time_point now) {
    std::tm time_start = {};
    time_start.tm_year = year;   // Years since 1900
    time_start.tm_mon = mon;     // Month (0-11)
    time_start.tm_mday = mday;   // Day of month
    time_start.tm_hour = 0;
    time_start.tm_min = 0;
    time_start.tm_sec = 0;
    time_start.tm_isdst = -1;  // Let system determine DST
    
    auto cutoff_time_t = std::mktime(&time_start);
    auto cutoff = std::chrono::system_clock::from_time_t(cutoff_time_t);
    return ChronoToFileTime(cutoff);
  }
} // namespace time_filter_detail

FILETIME CalculateCutoffTime(TimeFilter filter) {
  using std::chrono::hours;
  using std::chrono::system_clock;

  auto now = system_clock::now();
  
  switch (filter) {
  case TimeFilter::Today: {
    // Start of today (midnight)
    const std::tm now_tm = time_filter_detail::GetLocalTime(now);
    return time_filter_detail::CreateTimeStructAndConvert(
        now_tm.tm_year, now_tm.tm_mon, now_tm.tm_mday, now);
  }
  case TimeFilter::ThisWeek: {
    // Start of current week (Monday in local timezone)
    const std::tm now_tm = time_filter_detail::GetLocalTime(now);
    
    // Calculate days since Monday (0=Monday, 1=Tuesday, ..., 6=Sunday)
    const int days_since_monday = (now_tm.tm_wday == 0) ? 6 : (now_tm.tm_wday - 1);  // NOLINT(readability-magic-numbers) - 6 is days from Sunday to Monday (tm_wday: 0=Sunday, 6=Saturday)
    
    // Create time for start of week (Monday at midnight)
    std::tm week_start = now_tm;
    week_start.tm_mday -= days_since_monday;
    week_start.tm_hour = 0;
    week_start.tm_min = 0;
    week_start.tm_sec = 0;
    week_start.tm_isdst = -1;  // Let system determine DST
    
    auto cutoff_time_t = std::mktime(&week_start);
    auto cutoff = system_clock::from_time_t(cutoff_time_t);
    return ChronoToFileTime(cutoff);
  }
  case TimeFilter::ThisMonth: {
    // Start of current month (1st day at midnight in local timezone)
    const std::tm now_tm = time_filter_detail::GetLocalTime(now);
    return time_filter_detail::CreateTimeStructAndConvert(
        now_tm.tm_year, now_tm.tm_mon, 1, now);
  }
  case TimeFilter::ThisYear: {
    // Start of current year
    const std::tm now_tm = time_filter_detail::GetLocalTime(now);
    return time_filter_detail::CreateTimeStructAndConvert(
        now_tm.tm_year, 0, 1, now);  // January (month 0), day 1
  }
  case TimeFilter::Older: {
    // More than 1 year ago (365 days)
    auto cutoff = now - hours(24 * 365);  // NOLINT(readability-magic-numbers) - 24 is hours per day, 365 is days per year (standard calendar constants)
    return ChronoToFileTime(cutoff);
  }
  default:
    return FILETIME{0, 0}; // Sentinel
  }
}

#endif  // _WIN32

