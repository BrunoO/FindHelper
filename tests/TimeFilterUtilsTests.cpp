/**
 * @file TimeFilterUtilsTests.cpp
 * @brief Unit tests for TimeFilterUtils functions
 *
 * Tests cover:
 * - TimeFilter enum to/from string conversion (serialization round-trip)
 * - CalculateCutoffTime() for all filter types
 * - Cross-platform time calculations (Unix-to-Windows epoch conversion)
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <array>
#include <chrono>
#include <ctime>

#include "TestHelpers.h"
#include "filters/TimeFilter.h"        // TimeFilter enum
#include "filters/TimeFilterUtils.h"   // TimeFilterToString, TimeFilterFromString, CalculateCutoffTime
#include "utils/FileTimeTypes.h"       // FILETIME

// Helper to convert FILETIME to Unix timestamp (seconds since 1970)
// Used for verifying cutoff times are in the expected range
static int64_t FileTimeToUnixSeconds(const FILETIME& ft) {
    // Windows epoch: 1601-01-01 00:00:00 UTC
    // Unix epoch: 1970-01-01 00:00:00 UTC
    // Difference: 11644473600 seconds
    constexpr int64_t kEpochDiffSeconds = 11644473600LL;
    
    const uint64_t total_100ns = (static_cast<uint64_t>(ft.dwHighDateTime) << 32U) |
                                 static_cast<uint64_t>(ft.dwLowDateTime);
    constexpr uint64_t kFiletimeTicksPerSecond = 10000000ULL;
    const auto total_seconds =
        static_cast<int64_t>(total_100ns / kFiletimeTicksPerSecond);
    return total_seconds - kEpochDiffSeconds;
}

TEST_SUITE("TimeFilterUtils") {
    
    // NOLINTNEXTLINE(readability-function-cognitive-complexity) - Aggregated doctest suite with subcases
    TEST_SUITE("TimeFilterToString") {
        TEST_CASE("All filter types convert to string correctly") {
            const std::array<std::pair<TimeFilter, std::string>, 6> test_cases = {
                std::make_pair(TimeFilter::None, "None"),
                std::make_pair(TimeFilter::Today, "Today"),
                std::make_pair(TimeFilter::ThisWeek, "ThisWeek"),
                std::make_pair(TimeFilter::ThisMonth, "ThisMonth"),
                std::make_pair(TimeFilter::ThisYear, "ThisYear"),
                std::make_pair(TimeFilter::Older, "Older")
            };
            
            for (const auto& [filter, expected] : test_cases) {
                DOCTEST_SUBCASE(expected.c_str()) {
                    CHECK(std::string(TimeFilterToString(filter)) == expected);
                }
            }
        }
    }
    
    // NOLINTNEXTLINE(readability-function-cognitive-complexity) - Aggregated doctest suite with subcases
    TEST_SUITE("TimeFilterFromString") {
        TEST_CASE("All filter strings convert to enum correctly") {
            const std::array<std::pair<std::string, TimeFilter>, 6> test_cases = {
                std::make_pair("None", TimeFilter::None),
                std::make_pair("Today", TimeFilter::Today),
                std::make_pair("ThisWeek", TimeFilter::ThisWeek),
                std::make_pair("ThisMonth", TimeFilter::ThisMonth),
                std::make_pair("ThisYear", TimeFilter::ThisYear),
                std::make_pair("Older", TimeFilter::Older)
            };
            
            for (const auto& [str, expected] : test_cases) {
                DOCTEST_SUBCASE(str.c_str()) {
                    CHECK(TimeFilterFromString(str) == expected);
                }
            }
        }
        
        TEST_CASE("Unknown string returns None") {
            CHECK(TimeFilterFromString("Unknown") == TimeFilter::None);
            CHECK(TimeFilterFromString("") == TimeFilter::None);
            CHECK(TimeFilterFromString("today") == TimeFilter::None); // Case-sensitive
            CHECK(TimeFilterFromString("NONE") == TimeFilter::None);
        }
    }
    
    TEST_SUITE("TimeFilter serialization round-trip") {
        TEST_CASE("All filter types round-trip correctly") {
            const std::array<TimeFilter, 8> filters = {
                TimeFilter::None,
                TimeFilter::Today,
                TimeFilter::ThisWeek,
                TimeFilter::ThisMonth,
                TimeFilter::ThisYear,
                TimeFilter::Older
            };
            
            for (TimeFilter filter : filters) {
                std::string str = TimeFilterToString(filter);
                TimeFilter result = TimeFilterFromString(str);
                CHECK(result == filter);
            }
        }
    }
    
    TEST_SUITE("CalculateCutoffTime") {
        
        TEST_CASE("None filter returns sentinel") {
            FILETIME result = CalculateCutoffTime(TimeFilter::None);
            // None should return sentinel {0, 0}
            CHECK(result.dwLowDateTime == 0);
            CHECK(result.dwHighDateTime == 0);
        }
        
        TEST_CASE("Today filter returns start of today (midnight)") {
            FILETIME result = CalculateCutoffTime(TimeFilter::Today);
            
            int64_t cutoff_unix = FileTimeToUnixSeconds(result);
            int64_t now_unix = test_helpers::time_filter_test_helpers::GetCurrentUnixSeconds();
            int64_t expected = test_helpers::time_filter_test_helpers::GetStartOfTodayUnix();
            
            test_helpers::time_filter_test_helpers::VerifyCutoffTimeRange(cutoff_unix, expected);
            test_helpers::time_filter_test_helpers::VerifyCutoffTimeInPast(cutoff_unix, now_unix, 24 * 60 * 60);
        }
        
        TEST_CASE("ThisWeek filter returns start of current week (Monday)") {
            FILETIME result = CalculateCutoffTime(TimeFilter::ThisWeek);
            
            int64_t cutoff_unix = FileTimeToUnixSeconds(result);
            int64_t now_unix = test_helpers::time_filter_test_helpers::GetCurrentUnixSeconds();
            int64_t expected = test_helpers::time_filter_test_helpers::GetStartOfWeekUnix();
            
            test_helpers::time_filter_test_helpers::VerifyCutoffTimeRange(cutoff_unix, expected);
            test_helpers::time_filter_test_helpers::VerifyCutoffTimeInPast(cutoff_unix, now_unix, 7 * 24 * 60 * 60);
        }
        
        TEST_CASE("ThisMonth filter returns start of current month") {
            FILETIME result = CalculateCutoffTime(TimeFilter::ThisMonth);
            
            int64_t cutoff_unix = FileTimeToUnixSeconds(result);
            int64_t now_unix = test_helpers::time_filter_test_helpers::GetCurrentUnixSeconds();
            int64_t expected = test_helpers::time_filter_test_helpers::GetStartOfMonthUnix();
            
            test_helpers::time_filter_test_helpers::VerifyCutoffTimeRange(cutoff_unix, expected);
            test_helpers::time_filter_test_helpers::VerifyCutoffTimeInPast(cutoff_unix, now_unix, 31 * 24 * 60 * 60);
        }
        
        TEST_CASE("ThisYear filter returns start of current year") {
            FILETIME result = CalculateCutoffTime(TimeFilter::ThisYear);
            
            int64_t cutoff_unix = FileTimeToUnixSeconds(result);
            int64_t expected = test_helpers::time_filter_test_helpers::GetStartOfYearUnix();
            
            test_helpers::time_filter_test_helpers::VerifyCutoffTimeRange(cutoff_unix, expected);
        }
        
        TEST_CASE("Older filter returns time approximately 1 year ago") {
            FILETIME result = CalculateCutoffTime(TimeFilter::Older);
            
            int64_t cutoff_unix = FileTimeToUnixSeconds(result);
            int64_t now_unix = test_helpers::time_filter_test_helpers::GetCurrentUnixSeconds();
            
            // Expected: approximately 1 year ago (365-366 days, accounting for leap years)
            // Windows implementation subtracts 1 year from the date, which may differ from
            // exactly 365 days due to leap years (Feb 29 handling) and DST transitions
            constexpr int64_t kSecondsPerDay = 24 * 60 * 60;
            constexpr int64_t kMinDaysInYear = 365;
            constexpr int64_t kMaxDaysInYear = 366;
            int64_t min_expected = now_unix - (kMaxDaysInYear * kSecondsPerDay);
            int64_t max_expected = now_unix - (kMinDaysInYear * kSecondsPerDay);
            
            // Allow tolerance for leap year differences and DST (up to 2 days)
            // Also allow small time difference due to calculation timing
            constexpr int64_t kToleranceSeconds = 2 * kSecondsPerDay + 60; // 2 days + 1 minute
            CHECK(cutoff_unix >= min_expected - kToleranceSeconds);
            CHECK(cutoff_unix <= max_expected + kToleranceSeconds);
            
            // Ensure it's in the past and roughly 1 year ago
            CHECK(cutoff_unix < now_unix);
            CHECK(cutoff_unix > now_unix - (400 * kSecondsPerDay)); // Sanity check: not more than ~13 months ago
        }
        
        TEST_CASE("Cutoff times are ordered correctly") {
            // Today >= ThisWeek > ThisMonth > Older (in terms of recency)
            // So as FILETIME values: Today >= ThisWeek > ThisMonth > Older
            // Note: If today is Monday, today_unix == week_unix (both are Monday midnight)
            // Note: Week can start in previous month, so week_unix may be < month_unix
            // In that case, we still ensure week_unix > older_unix
            
            FILETIME today = CalculateCutoffTime(TimeFilter::Today);
            FILETIME week = CalculateCutoffTime(TimeFilter::ThisWeek);
            FILETIME month = CalculateCutoffTime(TimeFilter::ThisMonth);
            FILETIME older = CalculateCutoffTime(TimeFilter::Older);
            
            int64_t today_unix = FileTimeToUnixSeconds(today);
            int64_t week_unix = FileTimeToUnixSeconds(week);
            int64_t month_unix = FileTimeToUnixSeconds(month);
            int64_t older_unix = FileTimeToUnixSeconds(older);
            
            // Today's cutoff is most recent (largest Unix timestamp)
            // If today is Monday, today_unix == week_unix (both are Monday midnight)
            CHECK(today_unix >= week_unix);
            
            // Week cutoff may be >= month cutoff (if week starts in current month)
            // or < month cutoff (if week starts in previous month)
            // In either case, both week and month should be more recent than older
            CHECK(week_unix > older_unix);
            CHECK(month_unix > older_unix);
            
            // If week starts in current month, week >= month
            // If week starts in previous month, week < month (both are valid)
            // No strict ordering check needed between week and month
        }
        
        TEST_CASE("Cutoff times are in valid range") {
            // All cutoff times should be:
            // - In the past (less than current time)
            // - After 2000-01-01 (sanity check)
            
            int64_t now_unix = test_helpers::time_filter_test_helpers::GetCurrentUnixSeconds();
            constexpr int64_t kYear2000Unix = 946684800LL; // 2000-01-01 00:00:00 UTC
            
            const std::array<TimeFilter, 5> filters = {
                TimeFilter::Today,
                TimeFilter::ThisWeek,
                TimeFilter::ThisMonth,
                TimeFilter::ThisYear,
                TimeFilter::Older
            };
            
            for (TimeFilter filter : filters) {
                FILETIME result = CalculateCutoffTime(filter);
                int64_t cutoff_unix = FileTimeToUnixSeconds(result);
                
                CAPTURE(TimeFilterToString(filter));
                CHECK(cutoff_unix < now_unix);  // In the past
                CHECK(cutoff_unix > kYear2000Unix);  // After year 2000
            }
        }
    }
    
    TEST_SUITE("FILETIME epoch conversion") {
        
        TEST_CASE("Epoch difference is correct") {
            // The epoch difference between Windows (1601) and Unix (1970)
            // is exactly 11644473600 seconds
            // This test verifies our conversion math is correct
            
            // Create a FILETIME for Unix epoch (1970-01-01 00:00:00 UTC)
            // In Windows FILETIME, this is 11644473600 seconds * 10,000,000
            constexpr int64_t kEpochDiffSeconds = 11644473600LL;
            constexpr int64_t kEpochDiff100ns = kEpochDiffSeconds * 10000000LL;
            
            FILETIME unix_epoch;
            unix_epoch.dwLowDateTime = static_cast<uint32_t>(kEpochDiff100ns & 0xFFFFFFFF);
            unix_epoch.dwHighDateTime = static_cast<uint32_t>((kEpochDiff100ns >> 32) & 0xFFFFFFFF);
            
            // Converting this FILETIME to Unix seconds should give 0
            int64_t result = FileTimeToUnixSeconds(unix_epoch);
            CHECK(result == 0);
        }
        
        TEST_CASE("Known date conversion") {
            // Test with a known date: 2024-01-01 00:00:00 UTC
            // Unix timestamp: 1704067200
            // Windows FILETIME: (1704067200 + 11644473600) * 10000000
            
            constexpr int64_t kUnixTime = 1704067200LL;
            constexpr int64_t kEpochDiffSeconds = 11644473600LL;
            constexpr int64_t kWindowsTime100ns = (kUnixTime + kEpochDiffSeconds) * 10000000LL;
            
            FILETIME ft;
            ft.dwLowDateTime = static_cast<uint32_t>(kWindowsTime100ns & 0xFFFFFFFF);
            ft.dwHighDateTime = static_cast<uint32_t>((kWindowsTime100ns >> 32) & 0xFFFFFFFF);
            
            int64_t result = FileTimeToUnixSeconds(ft);
            CHECK(result == kUnixTime);
        }
    }
}

