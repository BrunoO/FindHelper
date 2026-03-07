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

#include "utils/FileSystemUtils.h"
#include "utils/FileAttributeConstants.h"
#include <cstdint>

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
            REQUIRE(FormatFileSize(1023).find("B") != std::string::npos);
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
}
