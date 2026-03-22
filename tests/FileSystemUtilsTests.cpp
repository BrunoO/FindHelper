#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#ifdef _WIN32
// Windows headers must be included in the correct order for propkey.h to work
// 1. windows.h must come first (with proper defines from CMake)
// 2. initguid.h for GUID initialization
// 3. propsys.h defines PROPERTYKEY type needed by propkeydef.h
// 4. Then FileSystemUtils.h can safely include propkey.h
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <initguid.h>
#include <propsys.h>
#endif  // _WIN32

#include <cstdint>

#include "utils/FileAttributeConstants.h"
#include "utils/FileSystemUtils.h"

TEST_SUITE("FileSystemUtils") {

    TEST_SUITE("GetFileAttributes") {
        TEST_CASE("non-existent file returns failure sentinels") {
            std::string nonExistentPath = "/nonexistent/path/to/file/that/should/not/exist.txt";
            FileAttributes attrs = GetFileAttributes(nonExistentPath);

            CHECK(attrs.success == false);
            CHECK(attrs.fileSize == kFileSizeFailed);
            CHECK(IsFailedTime(attrs.lastModificationTime));
        }
    }

    TEST_SUITE("FormatFileSize") {
        TEST_CASE("zero bytes") {
            REQUIRE(FormatFileSize(0) == "0 B");
        }

        TEST_CASE("bytes less than 1024") {
            REQUIRE(FormatFileSize(1) == "1 B");
            REQUIRE(FormatFileSize(100) == "100 B");
            REQUIRE(FormatFileSize(512) == "512 B");
            REQUIRE(FormatFileSize(1023) == "1023 B");
        }

        TEST_CASE("exactly 1024 bytes") {
            std::string result = FormatFileSize(1024);
            REQUIRE(result.find("KB") != std::string::npos);
            REQUIRE(result.find("1.0") != std::string::npos);
        }

        TEST_CASE("kilobytes range") {
            std::string result1 = FormatFileSize(1024);
            REQUIRE(result1.find("KB") != std::string::npos);

            std::string result2 = FormatFileSize(2048);
            REQUIRE(result2.find("KB") != std::string::npos);
            REQUIRE(result2.find("2.0") != std::string::npos);

            std::string result3 = FormatFileSize(5120);
            REQUIRE(result3.find("KB") != std::string::npos);
            REQUIRE(result3.find("5.0") != std::string::npos);

            // Test fractional KB
            std::string result4 = FormatFileSize(1536);  // 1.5 KB
            REQUIRE(result4.find("KB") != std::string::npos);
            REQUIRE(result4.find("1.5") != std::string::npos);
        }

        TEST_CASE("boundary at 1 MB") {
            std::string result1 = FormatFileSize(1024 * 1024 - 1);
            REQUIRE(result1.find("KB") != std::string::npos);

            std::string result2 = FormatFileSize(1024 * 1024);
            REQUIRE(result2.find("MB") != std::string::npos);
            REQUIRE(result2.find("1.0") != std::string::npos);
        }

        TEST_CASE("megabytes range") {
            std::string result1 = FormatFileSize(1024ULL * 1024);
            REQUIRE(result1.find("MB") != std::string::npos);
            REQUIRE(result1.find("1.0") != std::string::npos);

            std::string result2 = FormatFileSize(2ULL * 1024 * 1024);
            REQUIRE(result2.find("MB") != std::string::npos);
            REQUIRE(result2.find("2.0") != std::string::npos);

            std::string result3 = FormatFileSize(5ULL * 1024 * 1024);
            REQUIRE(result3.find("MB") != std::string::npos);
            REQUIRE(result3.find("5.0") != std::string::npos);

            // Test fractional MB
            std::string result4 = FormatFileSize(1536ULL * 1024);  // 1.5 MB
            REQUIRE(result4.find("MB") != std::string::npos);
            REQUIRE(result4.find("1.5") != std::string::npos);
        }

        TEST_CASE("boundary at 1 GB") {
            std::string result1 = FormatFileSize(1024ULL * 1024 * 1024 - 1);
            REQUIRE(result1.find("MB") != std::string::npos);

            std::string result2 = FormatFileSize(1024ULL * 1024 * 1024);
            REQUIRE(result2.find("GB") != std::string::npos);
        }

        TEST_CASE("gigabytes range") {
            std::string result1 = FormatFileSize(1024ULL * 1024 * 1024);
            REQUIRE(result1.find("GB") != std::string::npos);

            std::string result2 = FormatFileSize(2ULL * 1024 * 1024 * 1024);
            REQUIRE(result2.find("GB") != std::string::npos);
            REQUIRE(result2.find("2.00") != std::string::npos);

            std::string result3 = FormatFileSize(5ULL * 1024 * 1024 * 1024);
            REQUIRE(result3.find("GB") != std::string::npos);
            REQUIRE(result3.find("5.00") != std::string::npos);

            // Test fractional GB
            std::string result4 = FormatFileSize(1536ULL * 1024 * 1024);  // 1.5 GB
            REQUIRE(result4.find("GB") != std::string::npos);
            REQUIRE(result4.find("1.50") != std::string::npos);
        }

        TEST_CASE("very large sizes") {
            std::string result1 = FormatFileSize(10ULL * 1024 * 1024 * 1024);
            REQUIRE(result1.find("GB") != std::string::npos);
            REQUIRE(result1.find("10.00") != std::string::npos);

            std::string result2 = FormatFileSize(100ULL * 1024 * 1024 * 1024);
            REQUIRE(result2.find("GB") != std::string::npos);
            REQUIRE(result2.find("100.00") != std::string::npos);

            std::string result3 = FormatFileSize(1024ULL * 1024 * 1024 * 1024);
            REQUIRE(result3.find("GB") != std::string::npos);
            // Should show 1024.00 GB (not TB, as function only goes up to GB)
        }

        TEST_CASE("precision formatting") {
            // Test that KB uses 1 decimal place
            std::string kb_result = FormatFileSize(1536);  // 1.5 KB
            REQUIRE(kb_result.find("1.5") != std::string::npos);

            // Test that MB uses 1 decimal place
            std::string mb_result = FormatFileSize(1536ULL * 1024);  // 1.5 MB
            REQUIRE(mb_result.find("1.5") != std::string::npos);

            // Test that GB uses 2 decimal places
            std::string gb_result = FormatFileSize(1536ULL * 1024 * 1024);  // 1.5 GB
            REQUIRE(gb_result.find("1.50") != std::string::npos);
        }

        TEST_CASE("boundary values") {
            // Test exact boundaries
            REQUIRE(FormatFileSize(1023).find('B') != std::string::npos);
            REQUIRE(FormatFileSize(1024).find("KB") != std::string::npos);

            REQUIRE(FormatFileSize(1024 * 1024 - 1).find("KB") != std::string::npos);
            REQUIRE(FormatFileSize(1024 * 1024).find("MB") != std::string::npos);

            REQUIRE(FormatFileSize(1024ULL * 1024 * 1024 - 1).find("MB") != std::string::npos);
            REQUIRE(FormatFileSize(1024ULL * 1024 * 1024).find("GB") != std::string::npos);
        }

        TEST_CASE("common file sizes") {
            // Typical file sizes
            std::string result1 = FormatFileSize(500);  // Small text file
            REQUIRE(result1 == "500 B");

            std::string result2 = FormatFileSize(50 * 1024);  // ~50 KB
            REQUIRE(result2.find("KB") != std::string::npos);

            std::string result3 = FormatFileSize(5 * 1024 * 1024);  // 5 MB
            REQUIRE(result3.find("MB") != std::string::npos);
            REQUIRE(result3.find("5.0") != std::string::npos);

            std::string result4 = FormatFileSize(2ULL * 1024 * 1024 * 1024);  // 2 GB
            REQUIRE(result4.find("GB") != std::string::npos);
            REQUIRE(result4.find("2.00") != std::string::npos);
        }

        TEST_CASE("format consistency") {
            // All results should end with space + unit
            std::string result1 = FormatFileSize(100);
            bool has_b_unit = result1.back() == 'B' || result1.find(" B") != std::string::npos;
            REQUIRE(has_b_unit);

            std::string result2 = FormatFileSize(1024);
            REQUIRE(result2.find(" KB") != std::string::npos);

            std::string result3 = FormatFileSize(1024 * 1024);
            REQUIRE(result3.find(" MB") != std::string::npos);

            std::string result4 = FormatFileSize(1024ULL * 1024 * 1024);
            REQUIRE(result4.find(" GB") != std::string::npos);
        }

        TEST_CASE("edge case: exactly 1 byte") {
            REQUIRE(FormatFileSize(1) == "1 B");
        }

        TEST_CASE("edge case: exactly 1023 bytes") {
            REQUIRE(FormatFileSize(1023) == "1023 B");
        }

        TEST_CASE("edge case: exactly 1 KB") {
            std::string result = FormatFileSize(1024);
            REQUIRE(result.find("KB") != std::string::npos);
            REQUIRE(result.find("1.0") != std::string::npos);
        }

        TEST_CASE("edge case: exactly 1 MB") {
            std::string result = FormatFileSize(1024 * 1024);
            REQUIRE(result.find("MB") != std::string::npos);
            REQUIRE(result.find("1.0") != std::string::npos);
        }

        TEST_CASE("edge case: exactly 1 GB") {
            std::string result = FormatFileSize(1024ULL * 1024 * 1024);
            REQUIRE(result.find("GB") != std::string::npos);
            REQUIRE(result.find("1.00") != std::string::npos);
        }
    }

    TEST_SUITE("FormatFileTime") {
        TEST_CASE("not-loaded sentinel returns loading indicator '...'") {
            CHECK(FormatFileTime(kFileTimeNotLoaded) == "...");
        }

        TEST_CASE("failed sentinel returns N/A") {
            CHECK(FormatFileTime(kFileTimeFailed) == "N/A");
        }

        TEST_CASE("valid FILETIME returns non-empty, non-sentinel string") {
            // A known valid FILETIME: 2024-01-01 in UTC = 133483584000000000 (100-ns since 1601)
            // dwLowDateTime  = 133483584000000000 & 0xFFFFFFFF = 0xB8B4DC00 = 3099959296
            // dwHighDateTime = 133483584000000000 >> 32       = 0x01DA8CE1 = 31100129
            constexpr uint32_t kLow  = 0xB8B4DC00U;
            constexpr uint32_t kHigh = 0x01DA8CE1U;
            const FILETIME ft{kLow, kHigh};
            const std::string result = FormatFileTime(ft);
            CHECK(!result.empty());
            CHECK(result != "...");
            CHECK(result != "N/A");
            // Format is "YYYY-MM-DD HH:MM" at the start of the buffer
            CHECK(result[4] == '-');
            CHECK(result[7] == '-');
            CHECK(result[10] == ' ');
            CHECK(result[13] == ':');
        }
    }

#ifndef _WIN32
    TEST_SUITE("TimespecToFileTime and FileTimeToTimespec") {
        TEST_CASE("Unix epoch {0, 0} maps to Windows epoch offset") {
            // Unix epoch (1970-01-01) is kEpochDiffSeconds * 10^7 100-ns intervals
            // after the Windows epoch (1601-01-01).
            constexpr int64_t kExpected100ns =
                file_time_constants::kEpochDiffSeconds *
                file_system_utils_constants::kFiletimeIntervalsPerSecond;
            const struct timespec ts{0, 0};
            const FILETIME ft = TimespecToFileTime(ts);
            const uint64_t actual =
                (static_cast<uint64_t>(ft.dwHighDateTime) << 32U) |
                static_cast<uint64_t>(ft.dwLowDateTime);
            CHECK(actual == static_cast<uint64_t>(kExpected100ns));
        }

        TEST_CASE("round-trip: timespec -> FILETIME -> timespec is lossless at second precision") {
            // 2024-06-15 12:00:00 UTC = 1718445600 seconds since Unix epoch
            constexpr time_t kTestSec = 1718445600;
            const struct timespec ts_in{kTestSec, 0};
            const FILETIME ft = TimespecToFileTime(ts_in);
            const struct timespec ts_out = FileTimeToTimespec(ft);
            CHECK(ts_out.tv_sec == ts_in.tv_sec);
            CHECK(ts_out.tv_nsec == ts_in.tv_nsec);
        }

        TEST_CASE("sub-second nanoseconds survive round-trip (truncated to 100-ns precision)") {
            constexpr time_t kTestSec = 1000000000;  // arbitrary second
            constexpr int64_t kNs = 123456700LL;          // 1234567 × 100 ns (exact 100-ns multiple)
            const struct timespec ts_in{kTestSec, kNs};
            const FILETIME ft = TimespecToFileTime(ts_in);
            const struct timespec ts_out = FileTimeToTimespec(ft);
            CHECK(ts_out.tv_sec == ts_in.tv_sec);
            CHECK(ts_out.tv_nsec == kNs);
        }

        TEST_CASE("FileTimeToTimespec of Unix-epoch FILETIME returns {0, 0}") {
            // Construct FILETIME = kEpochDiffSeconds * 10^7
            constexpr uint64_t kEpoch100ns =
                static_cast<uint64_t>(file_time_constants::kEpochDiffSeconds) *
                static_cast<uint64_t>(file_system_utils_constants::kFiletimeIntervalsPerSecond);
            const FILETIME ft{
                static_cast<uint32_t>(kEpoch100ns & 0xFFFFFFFFU),
                static_cast<uint32_t>(kEpoch100ns >> 32U)};
            const struct timespec ts = FileTimeToTimespec(ft);
            CHECK(ts.tv_sec == 0);
            CHECK(ts.tv_nsec == 0);
        }
    }
#endif  // _WIN32
}
