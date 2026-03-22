/**
 * @file TimeFilterUtilsTests.cpp
 * @brief Unit tests for TimeFilterUtils functions
 *
 * Tests cover:
 * - TimeFilter enum to/from string conversion (serialization round-trip)
 * - CalculateCutoffTime() for all filter types
 * - Cross-platform time calculations (Unix-to-Windows epoch conversion)
 * - RecordRecentSearch(): deduplication, ordering, size cap
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <array>
#include <chrono>
#include <ctime>

#include "TestHelpers.h"
#include "core/Settings.h"             // AppSettings, settings_defaults::kMaxRecentSearches
#include "filters/SizeFilter.h"        // SizeFilter enum
#include "filters/SizeFilterUtils.h"   // SizeFilterToString, SizeFilterFromString, SizeFilterDisplayLabel, MatchesSizeFilter
#include "filters/TimeFilter.h"        // TimeFilter enum
#include "filters/TimeFilterUtils.h"   // TimeFilterToString, TimeFilterFromString, CalculateCutoffTime, RecordRecentSearch
#include "gui/GuiState.h"              // GuiState (for RecordRecentSearch)
#include "search/SearchTypes.h"        // SearchParams
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
                const std::string str = TimeFilterToString(filter);
                const TimeFilter result = TimeFilterFromString(str);
                CHECK(result == filter);
            }
        }
    }

    TEST_SUITE("CalculateCutoffTime") {

        TEST_CASE("None filter returns sentinel") {
            const FILETIME result = CalculateCutoffTime(TimeFilter::None);
            // None should return sentinel {0, 0}
            CHECK(result.dwLowDateTime == 0);
            CHECK(result.dwHighDateTime == 0);
        }

        TEST_CASE("Today filter returns start of today (midnight)") {
            const FILETIME result = CalculateCutoffTime(TimeFilter::Today);

            const int64_t cutoff_unix = FileTimeToUnixSeconds(result);
            const int64_t now_unix = test_helpers::time_filter_test_helpers::GetCurrentUnixSeconds();
            const int64_t expected = test_helpers::time_filter_test_helpers::GetStartOfTodayUnix();

            test_helpers::time_filter_test_helpers::VerifyCutoffTimeRange(cutoff_unix, expected);
            test_helpers::time_filter_test_helpers::VerifyCutoffTimeInPast(cutoff_unix, now_unix, 24 * 60 * 60);
        }

        TEST_CASE("ThisWeek filter returns start of current week (Monday)") {
            const FILETIME result = CalculateCutoffTime(TimeFilter::ThisWeek);

            const int64_t cutoff_unix = FileTimeToUnixSeconds(result);
            const int64_t now_unix = test_helpers::time_filter_test_helpers::GetCurrentUnixSeconds();
            const int64_t expected = test_helpers::time_filter_test_helpers::GetStartOfWeekUnix();

            test_helpers::time_filter_test_helpers::VerifyCutoffTimeRange(cutoff_unix, expected);
            test_helpers::time_filter_test_helpers::VerifyCutoffTimeInPast(cutoff_unix, now_unix, 7 * 24 * 60 * 60);
        }

        TEST_CASE("ThisMonth filter returns start of current month") {
            const FILETIME result = CalculateCutoffTime(TimeFilter::ThisMonth);

            const int64_t cutoff_unix = FileTimeToUnixSeconds(result);
            const int64_t now_unix = test_helpers::time_filter_test_helpers::GetCurrentUnixSeconds();
            const int64_t expected = test_helpers::time_filter_test_helpers::GetStartOfMonthUnix();

            test_helpers::time_filter_test_helpers::VerifyCutoffTimeRange(cutoff_unix, expected);
            test_helpers::time_filter_test_helpers::VerifyCutoffTimeInPast(cutoff_unix, now_unix, 31 * 24 * 60 * 60);
        }

        TEST_CASE("ThisYear filter returns start of current year") {
            const FILETIME result = CalculateCutoffTime(TimeFilter::ThisYear);

            const int64_t cutoff_unix = FileTimeToUnixSeconds(result);
            const int64_t expected = test_helpers::time_filter_test_helpers::GetStartOfYearUnix();

            test_helpers::time_filter_test_helpers::VerifyCutoffTimeRange(cutoff_unix, expected);
        }

        TEST_CASE("Older filter returns time approximately 1 year ago") {
            const FILETIME result = CalculateCutoffTime(TimeFilter::Older);

            const int64_t cutoff_unix = FileTimeToUnixSeconds(result);
            const int64_t now_unix = test_helpers::time_filter_test_helpers::GetCurrentUnixSeconds();

            // Expected: approximately 1 year ago (365-366 days, accounting for leap years)
            // Windows implementation subtracts 1 year from the date, which may differ from
            // exactly 365 days due to leap years (Feb 29 handling) and DST transitions
            constexpr int64_t kSecondsPerDay = 24 * 60 * 60;
            constexpr int64_t kMinDaysInYear = 365;
            constexpr int64_t kMaxDaysInYear = 366;
            const int64_t min_expected = now_unix - (kMaxDaysInYear * kSecondsPerDay);
            const int64_t max_expected = now_unix - (kMinDaysInYear * kSecondsPerDay);

            // Allow tolerance for leap year differences and DST (up to 2 days)
            // Also allow small time difference due to calculation timing
            constexpr int64_t kToleranceSeconds = (2 * kSecondsPerDay) + 60; // 2 days + 1 minute
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

            const FILETIME today = CalculateCutoffTime(TimeFilter::Today);
            const FILETIME week  = CalculateCutoffTime(TimeFilter::ThisWeek);
            const FILETIME month = CalculateCutoffTime(TimeFilter::ThisMonth);
            const FILETIME older = CalculateCutoffTime(TimeFilter::Older);

            const int64_t today_unix = FileTimeToUnixSeconds(today);
            const int64_t week_unix  = FileTimeToUnixSeconds(week);
            const int64_t month_unix = FileTimeToUnixSeconds(month);
            const int64_t older_unix = FileTimeToUnixSeconds(older);

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

            const int64_t now_unix = test_helpers::time_filter_test_helpers::GetCurrentUnixSeconds();
            constexpr int64_t kYear2000Unix = 946684800LL; // 2000-01-01 00:00:00 UTC

            const std::array<TimeFilter, 5> filters = {
                TimeFilter::Today,
                TimeFilter::ThisWeek,
                TimeFilter::ThisMonth,
                TimeFilter::ThisYear,
                TimeFilter::Older
            };

            for (TimeFilter filter : filters) {
                const FILETIME result = CalculateCutoffTime(filter);
                const int64_t cutoff_unix = FileTimeToUnixSeconds(result);

                CAPTURE(TimeFilterToString(filter));
                CHECK(cutoff_unix < now_unix);       // In the past
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
            const int64_t result = FileTimeToUnixSeconds(unix_epoch);
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

            const int64_t result = FileTimeToUnixSeconds(ft);
            CHECK(result == kUnixTime);
        }
    }
}

// ============================================================================
// SizeFilterUtils tests
// ============================================================================

TEST_SUITE("SizeFilterUtils - SizeFilterToString") {

    TEST_CASE("round-trips for every enum value") {
        CHECK(std::string_view(SizeFilterToString(SizeFilter::None))    == "None");
        CHECK(std::string_view(SizeFilterToString(SizeFilter::Empty))   == "Empty");
        CHECK(std::string_view(SizeFilterToString(SizeFilter::Tiny))    == "Tiny");
        CHECK(std::string_view(SizeFilterToString(SizeFilter::Small))   == "Small");
        CHECK(std::string_view(SizeFilterToString(SizeFilter::Medium))  == "Medium");
        CHECK(std::string_view(SizeFilterToString(SizeFilter::Large))   == "Large");
        CHECK(std::string_view(SizeFilterToString(SizeFilter::Huge))    == "Huge");
        CHECK(std::string_view(SizeFilterToString(SizeFilter::Massive)) == "Massive");
    }
}

TEST_SUITE("SizeFilterUtils - SizeFilterFromString") {

    TEST_CASE("parses every valid string") {
        CHECK(SizeFilterFromString("None")    == SizeFilter::None);
        CHECK(SizeFilterFromString("Empty")   == SizeFilter::Empty);
        CHECK(SizeFilterFromString("Tiny")    == SizeFilter::Tiny);
        CHECK(SizeFilterFromString("Small")   == SizeFilter::Small);
        CHECK(SizeFilterFromString("Medium")  == SizeFilter::Medium);
        CHECK(SizeFilterFromString("Large")   == SizeFilter::Large);
        CHECK(SizeFilterFromString("Huge")    == SizeFilter::Huge);
        CHECK(SizeFilterFromString("Massive") == SizeFilter::Massive);
    }

    TEST_CASE("unrecognised string returns None") {
        CHECK(SizeFilterFromString("")        == SizeFilter::None);
        CHECK(SizeFilterFromString("unknown") == SizeFilter::None);
        CHECK(SizeFilterFromString("tiny")    == SizeFilter::None); // case-sensitive
    }

    TEST_CASE("round-trip: ToStr then FromStr is identity") {
        constexpr std::array filters = {
            SizeFilter::None, SizeFilter::Empty, SizeFilter::Tiny,
            SizeFilter::Small, SizeFilter::Medium, SizeFilter::Large,
            SizeFilter::Huge, SizeFilter::Massive
        };
        for (const SizeFilter f : filters) {
            CHECK(SizeFilterFromString(SizeFilterToString(f)) == f);
        }
    }
}

TEST_SUITE("SizeFilterUtils - SizeFilterDisplayLabel") {

    TEST_CASE("returns non-empty label for every enum value") {
        constexpr std::array filters = {
            SizeFilter::None, SizeFilter::Empty, SizeFilter::Tiny,
            SizeFilter::Small, SizeFilter::Medium, SizeFilter::Large,
            SizeFilter::Huge, SizeFilter::Massive
        };
        for (const SizeFilter f : filters) {
            CHECK(std::string_view(SizeFilterDisplayLabel(f)) != "");
        }
    }

    TEST_CASE("labels contain expected size ranges") {
        CHECK(std::string_view(SizeFilterDisplayLabel(SizeFilter::Empty))   == "Empty (0 bytes)");
        CHECK(std::string_view(SizeFilterDisplayLabel(SizeFilter::Tiny))    == "Tiny (< 1 KB)");
        CHECK(std::string_view(SizeFilterDisplayLabel(SizeFilter::Small))   == "Small (1-100 KB)");
        CHECK(std::string_view(SizeFilterDisplayLabel(SizeFilter::Medium))  == "Medium (100 KB - 10 MB)");
        CHECK(std::string_view(SizeFilterDisplayLabel(SizeFilter::Large))   == "Large (10-100 MB)");
        CHECK(std::string_view(SizeFilterDisplayLabel(SizeFilter::Huge))    == "Huge (100 MB - 1 GB)");
        CHECK(std::string_view(SizeFilterDisplayLabel(SizeFilter::Massive)) == "Massive (> 1 GB)");
    }
}

TEST_SUITE("SizeFilterUtils - MatchesSizeFilter") {
    using size_filter_constants::kOneGB;
    using size_filter_constants::kOneHundredKB;
    using size_filter_constants::kOneHundredMB;
    using size_filter_constants::kOneKB;
    using size_filter_constants::kTenMB;

    TEST_CASE("None matches every size") {
        CHECK(MatchesSizeFilter(0,          SizeFilter::None));
        CHECK(MatchesSizeFilter(1,          SizeFilter::None));
        CHECK(MatchesSizeFilter(kOneGB * 2, SizeFilter::None));
    }

    TEST_CASE("Empty matches only 0 bytes") {
        CHECK( MatchesSizeFilter(0,       SizeFilter::Empty));
        CHECK(!MatchesSizeFilter(1,       SizeFilter::Empty));
        CHECK(!MatchesSizeFilter(kOneKB,  SizeFilter::Empty));
    }

    TEST_CASE("Tiny: 1 byte to kOneKB-1") {
        CHECK(!MatchesSizeFilter(0,         SizeFilter::Tiny));
        CHECK( MatchesSizeFilter(1,         SizeFilter::Tiny));
        CHECK( MatchesSizeFilter(512,       SizeFilter::Tiny));
        CHECK( MatchesSizeFilter(kOneKB-1,  SizeFilter::Tiny));
        CHECK(!MatchesSizeFilter(kOneKB,    SizeFilter::Tiny));
    }

    TEST_CASE("Small: kOneKB to kOneHundredKB-1") {
        CHECK(!MatchesSizeFilter(kOneKB-1,        SizeFilter::Small));
        CHECK( MatchesSizeFilter(kOneKB,          SizeFilter::Small));
        CHECK( MatchesSizeFilter(kOneKB * 50,     SizeFilter::Small));
        CHECK( MatchesSizeFilter(kOneHundredKB-1, SizeFilter::Small));
        CHECK(!MatchesSizeFilter(kOneHundredKB,   SizeFilter::Small));
    }

    TEST_CASE("Medium: kOneHundredKB to kTenMB-1") {
        CHECK(!MatchesSizeFilter(kOneHundredKB-1, SizeFilter::Medium));
        CHECK( MatchesSizeFilter(kOneHundredKB,   SizeFilter::Medium));
        CHECK( MatchesSizeFilter(kTenMB-1,        SizeFilter::Medium));
        CHECK(!MatchesSizeFilter(kTenMB,          SizeFilter::Medium));
    }

    TEST_CASE("Large: kTenMB to kOneHundredMB-1") {
        CHECK(!MatchesSizeFilter(kTenMB-1,          SizeFilter::Large));
        CHECK( MatchesSizeFilter(kTenMB,            SizeFilter::Large));
        CHECK( MatchesSizeFilter(kOneHundredMB-1,   SizeFilter::Large));
        CHECK(!MatchesSizeFilter(kOneHundredMB,     SizeFilter::Large));
    }

    TEST_CASE("Huge: kOneHundredMB to kOneGB-1") {
        CHECK(!MatchesSizeFilter(kOneHundredMB-1, SizeFilter::Huge));
        CHECK( MatchesSizeFilter(kOneHundredMB,   SizeFilter::Huge));
        CHECK( MatchesSizeFilter(kOneGB-1,        SizeFilter::Huge));
        CHECK(!MatchesSizeFilter(kOneGB,          SizeFilter::Huge));
    }

    TEST_CASE("Massive: kOneGB and above") {
        CHECK(!MatchesSizeFilter(kOneGB-1,   SizeFilter::Massive));
        CHECK( MatchesSizeFilter(kOneGB,     SizeFilter::Massive));
        CHECK( MatchesSizeFilter(kOneGB * 4, SizeFilter::Massive));
    }

    TEST_CASE("boundary values are covered by exactly one filter") {
        // Each threshold value belongs to precisely one filter
        CHECK(!MatchesSizeFilter(kOneKB, SizeFilter::Tiny));
        CHECK( MatchesSizeFilter(kOneKB, SizeFilter::Small));

        CHECK(!MatchesSizeFilter(kOneHundredKB, SizeFilter::Small));
        CHECK( MatchesSizeFilter(kOneHundredKB, SizeFilter::Medium));

        CHECK(!MatchesSizeFilter(kTenMB, SizeFilter::Medium));
        CHECK( MatchesSizeFilter(kTenMB, SizeFilter::Large));

        CHECK(!MatchesSizeFilter(kOneHundredMB, SizeFilter::Large));
        CHECK( MatchesSizeFilter(kOneHundredMB, SizeFilter::Huge));

        CHECK(!MatchesSizeFilter(kOneGB, SizeFilter::Huge));
        CHECK( MatchesSizeFilter(kOneGB, SizeFilter::Massive));
    }
}

// ============================================================================
// IsEpochTime tests
// ============================================================================

TEST_SUITE("IsEpochTime") {

    TEST_CASE("exact Windows epoch {0,0} is epoch time") {
        const FILETIME ft{0, 0};
        CHECK(IsEpochTime(ft));
    }

    TEST_CASE("small dwLowDateTime within year-1601 range is epoch time") {
        // Any value where dwHighDateTime==0 and dwLowDateTime < kYear1601Max
        const FILETIME ft{0x1000, 0};  // small non-zero value, still year 1601
        CHECK(IsEpochTime(ft));
    }

    TEST_CASE("modern date is not epoch time") {
        // 2024-01-01 00:00:00 UTC in FILETIME: (1704067200 + 11644473600) * 10^7
        constexpr int64_t kWindowsTime100ns = (1704067200LL + 11644473600LL) * 10000000LL;
        FILETIME ft;
        ft.dwLowDateTime  = static_cast<uint32_t>(kWindowsTime100ns & 0xFFFFFFFFLL);
        ft.dwHighDateTime = static_cast<uint32_t>((kWindowsTime100ns >> 32) & 0xFFFFFFFFLL);
        CHECK(!IsEpochTime(ft));
    }

    TEST_CASE("sentinel (not-loaded) value is not epoch time") {
        CHECK(!IsEpochTime(kFileTimeNotLoaded));
    }

    TEST_CASE("failed-load sentinel {1,0} is treated as epoch time (within year-1601 range)") {
        // kFileTimeFailed = {dwLowDateTime=1, dwHighDateTime=0}: 100ns after epoch.
        // IsEpochTime uses a broad year-1601 range to handle timezone offsets,
        // so any value this close to the epoch is considered epoch time.
        CHECK(IsEpochTime(kFileTimeFailed));
    }
}

// ============================================================================
// RecordRecentSearch tests
// ============================================================================

namespace {

// Build a minimal SearchParams for use in RecordRecentSearch tests.
SearchParams MakeSearchParams(std::string_view filename,
                              std::string_view extensions = "",
                              std::string_view path = "") {
    SearchParams params;
    params.filenameInput  = std::string(filename);
    params.extensionInput = std::string(extensions);
    params.pathInput      = std::string(path);
    params.foldersOnly    = false;
    params.caseSensitive  = false;
    return params;
}

}  // namespace

TEST_SUITE("RecordRecentSearch") {

    TEST_CASE("Single search is added to an empty list") {
        AppSettings settings;
        GuiState state;
        const SearchParams params = MakeSearchParams("hello");

        RecordRecentSearch(params, state, settings);

        REQUIRE(settings.recentSearches.size() == 1U);
        CHECK(settings.recentSearches[0].filename == "hello");
    }

    TEST_CASE("name field of recent search is always empty") {
        AppSettings settings;
        GuiState state;
        const SearchParams params = MakeSearchParams("myfile");

        RecordRecentSearch(params, state, settings);

        CHECK(settings.recentSearches[0].name.empty());
    }

    TEST_CASE("Newer search is inserted at the front") {
        AppSettings settings;
        GuiState state;

        RecordRecentSearch(MakeSearchParams("first"),  state, settings);
        RecordRecentSearch(MakeSearchParams("second"), state, settings);

        REQUIRE(settings.recentSearches.size() == 2U);
        CHECK(settings.recentSearches[0].filename == "second");
        CHECK(settings.recentSearches[1].filename == "first");
    }

    TEST_CASE("Identical search is moved to front (no duplicates)") {
        AppSettings settings;
        GuiState state;

        RecordRecentSearch(MakeSearchParams("alpha"), state, settings);
        RecordRecentSearch(MakeSearchParams("beta"),  state, settings);
        RecordRecentSearch(MakeSearchParams("alpha"), state, settings);  // duplicate

        REQUIRE(settings.recentSearches.size() == 2U);
        CHECK(settings.recentSearches[0].filename == "alpha");
        CHECK(settings.recentSearches[1].filename == "beta");
    }

    TEST_CASE("List is capped at kMaxRecentSearches") {
        AppSettings settings;
        GuiState state;

        const auto limit = static_cast<int>(settings_defaults::kMaxRecentSearches);
        for (int i = 0; i < limit + 5; ++i) {
            RecordRecentSearch(MakeSearchParams(std::to_string(i)), state, settings);
        }

        CHECK(settings.recentSearches.size() == settings_defaults::kMaxRecentSearches);
    }

    TEST_CASE("Time filter is serialised into the recent search") {
        AppSettings settings;
        GuiState state;
        state.timeFilter = TimeFilter::Today;
        const SearchParams params = MakeSearchParams("report");

        RecordRecentSearch(params, state, settings);

        REQUIRE(!settings.recentSearches.empty());
        CHECK(settings.recentSearches[0].timeFilter == std::string(TimeFilterToString(TimeFilter::Today)));
    }

    TEST_CASE("Size filter is serialised into the recent search") {
        AppSettings settings;
        GuiState state;
        state.sizeFilter = SizeFilter::Large;
        const SearchParams params = MakeSearchParams("video");

        RecordRecentSearch(params, state, settings);

        REQUIRE(!settings.recentSearches.empty());
        CHECK(settings.recentSearches[0].sizeFilter == std::string(SizeFilterToString(SizeFilter::Large)));
    }

    TEST_CASE("Searches differing only in extension are not treated as duplicates") {
        AppSettings settings;
        GuiState state;

        RecordRecentSearch(MakeSearchParams("file", "txt"), state, settings);
        RecordRecentSearch(MakeSearchParams("file", "pdf"), state, settings);

        CHECK(settings.recentSearches.size() == 2U);
    }

    TEST_CASE("AI search description is always empty for recent searches") {
        AppSettings settings;
        GuiState state;
        const SearchParams params = MakeSearchParams("report");

        RecordRecentSearch(params, state, settings);

        REQUIRE(!settings.recentSearches.empty());
        CHECK(settings.recentSearches[0].aiSearchDescription.empty());
    }

}  // TEST_SUITE("RecordRecentSearch")

