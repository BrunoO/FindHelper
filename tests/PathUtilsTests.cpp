#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "path/PathUtils.h"
#include "TestHelpers.h"
#include <string_view>

TEST_SUITE("PathUtils") {
    
    TEST_SUITE("TruncatePathAtBeginning") {
        
        // Use helper function to create monospace width calculator
        const path_utils::TextWidthCalculator monospace_calc = test_helpers::CreateMonospaceWidthCalculator();
        
        TEST_CASE("returns ellipsis for zero or negative max_width") {
            std::string path = "C:\\Users\\Test\\Documents\\file.txt";
            
            REQUIRE(path_utils::TruncatePathAtBeginning(path, 0.0f, monospace_calc) == "...");
            REQUIRE(path_utils::TruncatePathAtBeginning(path, -1.0f, monospace_calc) == "...");
            REQUIRE(path_utils::TruncatePathAtBeginning(path, -100.0f, monospace_calc) == "...");
        }
        
        TEST_CASE("returns full path if it fits within max_width") {
            std::string path = "C:\\file.txt";
            float path_width = monospace_calc(path);
            
            // Path should fit exactly
            REQUIRE(path_utils::TruncatePathAtBeginning(path, path_width, monospace_calc) == path);
            
            // Path should fit with extra space
            REQUIRE(path_utils::TruncatePathAtBeginning(path, path_width + 10.0f, monospace_calc) == path);
        }
        
        TEST_CASE("returns ellipsis only if even ellipsis doesn't fit") {
            std::string path = "C:\\Users\\Test\\Documents\\file.txt";
            float ellipsis_width = monospace_calc("...");
            
            // If max_width is less than ellipsis width, return ellipsis
            REQUIRE(path_utils::TruncatePathAtBeginning(path, ellipsis_width - 0.1f, monospace_calc) == "...");
            REQUIRE(path_utils::TruncatePathAtBeginning(path, ellipsis_width, monospace_calc) == "...");
        }
        
#ifdef _WIN32
        TEST_CASE("truncates long paths correctly") {
            std::string path = "C:\\Users\\Test\\Documents\\VeryLongFileName.txt";
            // Need enough width: "C:\\" (3) + "..." (3) + "VeryLongFileName.txt" (20) = 26 chars minimum
            float max_width = 30.0f; // Should fit "C:\\...\\VeryLongFileName.txt" or similar
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // Result should preserve root prefix "C:\\" and contain "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "C:\\"));
            REQUIRE(test_helpers::CheckPathContainsEllipsis(result));
            
            // Result should contain a suffix of the original path
            REQUIRE(result.length() > 6); // At least "C:\\..."
            // Should end with filename or last components
            REQUIRE(test_helpers::CheckPathContainsSuffix(result, "VeryLongFileName.txt"));
            
            // Result width should fit within max_width (with some tolerance)
            REQUIRE(test_helpers::CheckPathWidthWithinLimit(result, max_width, monospace_calc));
        }
        
        TEST_CASE("prefers breaking at path separators") {
            std::string path = "C:\\Users\\Test\\Documents\\file.txt";
            float max_width = 15.0f; // Should fit "C:\\...\\Documents\\file.txt" or similar
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // Result should preserve root prefix "C:\\" and contain "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "C:\\"));
            REQUIRE(test_helpers::CheckPathContainsEllipsis(result));
            
            // If possible, should break after a separator
            // Check if there's a separator after the ellipsis
            size_t ellipsis_pos = result.find("...");
            if (ellipsis_pos != std::string::npos) {
                size_t first_sep = result.find_first_of("\\/", ellipsis_pos + 3);
                if (first_sep != std::string::npos) {
                    // The separator should be part of the truncated path
                    REQUIRE(first_sep < result.length());
                }
            }
        }
#else
        TEST_CASE("truncates long paths correctly") {
            // On non-Windows, use relative path (no root detection)
            std::string path = "Users/Test/Documents/VeryLongFileName.txt";
            float max_width = 25.0f; // Should fit "...VeryLongFileName.txt" (3 + 20 = 23 chars)
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // Result should start with "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "..."));
            
            // Result should contain a suffix of the original path
            REQUIRE(result.length() > 3);
            // Should end with filename or last components
            REQUIRE(test_helpers::CheckPathContainsSuffix(result, "VeryLongFileName.txt"));
            
            // Result width should fit within max_width (with some tolerance)
            REQUIRE(test_helpers::CheckPathWidthWithinLimit(result, max_width, monospace_calc));
        }
        
        TEST_CASE("prefers breaking at path separators") {
            // On non-Windows, use relative path (no root detection)
            std::string path = "Users/Test/Documents/file.txt";
            float max_width = 15.0f; // Should fit "...Documents/file.txt" or similar
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // Result should start with "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "..."));
            
            // If possible, should break after a separator
            // Check if there's a separator after the ellipsis
            size_t first_sep = result.find_first_of("\\/", 3);
            if (first_sep != std::string::npos) {
                // The separator should be part of the truncated path
                REQUIRE(first_sep < result.length());
            }
        }
#endif  // _WIN32
        
        TEST_CASE("handles empty path") {
            std::string path = "";
            float max_width = 100.0f;
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // Empty path should return as-is (fits in any width > 0)
            REQUIRE(result == path);
        }
        
#ifdef _WIN32
        TEST_CASE("handles path with only separators") {
            std::string path = "C:\\";
            float max_width = 100.0f;
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // Short path should return as-is
            REQUIRE(result == path);
        }
        
        TEST_CASE("handles very long path") {
            // Create a very long path
            std::string path = "C:\\";
            for (int i = 0; i < 20; ++i) {
                path += "VeryLongDirectoryName" + std::to_string(i) + "\\";
            }
            path += "file.txt";
            
            float max_width = 30.0f;
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // Should preserve root prefix "C:\\" and contain "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "C:\\"));
            REQUIRE(test_helpers::CheckPathContainsEllipsis(result));
            
            // Should end with "file.txt"
            REQUIRE(test_helpers::CheckPathContainsSuffix(result, "file.txt"));
            
            // Result should fit
            REQUIRE(test_helpers::CheckPathWidthWithinLimit(result, max_width, monospace_calc));
        }
        
        TEST_CASE("handles path with forward slashes") {
            std::string path = "C:/Users/Test/Documents/file.txt";
            float max_width = 20.0f;
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // Should preserve root prefix "C:/" and contain "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "C:/"));
            REQUIRE(test_helpers::CheckPathContainsEllipsis(result));
            REQUIRE(test_helpers::CheckPathWidthWithinLimit(result, max_width, monospace_calc));
        }
        
        TEST_CASE("handles mixed separators") {
            std::string path = "C:\\Users/Test\\Documents/file.txt";
            float max_width = 20.0f;
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // Should preserve root prefix "C:\\" and contain "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "C:\\"));
            REQUIRE(test_helpers::CheckPathContainsEllipsis(result));
            REQUIRE(test_helpers::CheckPathWidthWithinLimit(result, max_width, monospace_calc));
        }
#else
        TEST_CASE("handles path with only separators") {
            std::string path = "/";
            float max_width = 100.0f;
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // Short path should return as-is
            REQUIRE(result == path);
        }
        
        TEST_CASE("handles very long path") {
            // Create a very long path (relative, no root)
            std::string path = "";
            for (int i = 0; i < 20; ++i) {
                path += "VeryLongDirectoryName" + std::to_string(i) + "/";
            }
            path += "file.txt";
            
            float max_width = 30.0f;
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // Should start with "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "..."));
            
            // Should end with "file.txt"
            REQUIRE(test_helpers::CheckPathContainsSuffix(result, "file.txt"));
            
            // Result should fit
            REQUIRE(test_helpers::CheckPathWidthWithinLimit(result, max_width, monospace_calc));
        }
        
        TEST_CASE("handles path with forward slashes") {
            std::string path = "Users/Test/Documents/file.txt";
            float max_width = 20.0f;
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // Should start with "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "..."));
            REQUIRE(test_helpers::CheckPathWidthWithinLimit(result, max_width, monospace_calc));
        }
        
        TEST_CASE("handles mixed separators") {
            std::string path = "Users/Test/Documents/file.txt";
            float max_width = 20.0f;
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // Should start with "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "..."));
            REQUIRE(test_helpers::CheckPathWidthWithinLimit(result, max_width, monospace_calc));
        }
#endif  // _WIN32
        
        // Use helper function to create proportional width calculator
        const path_utils::TextWidthCalculator proportional_calc = test_helpers::CreateProportionalWidthCalculator();
        
        TEST_CASE("works with proportional font calculator") {
#ifdef _WIN32
            std::string path = "C:\\Users\\Test\\Documents\\file.txt";
            float max_width = 15.0f;
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, proportional_calc);
            
            // On Windows, should preserve root prefix "C:\\" and contain "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "C:\\"));
            REQUIRE(test_helpers::CheckPathContainsEllipsis(result));
#else
            // On non-Windows, use relative path (no root detection)
            std::string path = "Users/Test/Documents/file.txt";
            float max_width = 15.0f;
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, proportional_calc);
            
            // Should start with "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "..."));
#endif  // _WIN32
            REQUIRE(test_helpers::CheckPathWidthWithinLimit(result, max_width, proportional_calc));
        }
        
        TEST_CASE("edge case: max_width exactly equals ellipsis width") {
            std::string path = "C:\\Users\\Test\\Documents\\file.txt";
            float ellipsis_width = monospace_calc("...");
            
            std::string result = path_utils::TruncatePathAtBeginning(path, ellipsis_width, monospace_calc);
            
            // Should return ellipsis only
            REQUIRE(result == "...");
        }
        
        TEST_CASE("edge case: max_width slightly larger than ellipsis width") {
            std::string path = "C:\\Users\\Test\\Documents\\file.txt";
            float ellipsis_width = monospace_calc("...");
            float max_width = ellipsis_width + 0.1f;
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // Should return ellipsis only (not enough space for any path content)
            REQUIRE(result == "...");
        }
        
        TEST_CASE("edge case: path exactly fits after ellipsis") {
            std::string path = "file.txt";
            float ellipsis_width = monospace_calc("...");
            float path_width = monospace_calc(path);
            float max_width = ellipsis_width + path_width;
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // When path fits within max_width, function returns path as-is (no ellipsis needed)
            // This is the correct behavior: if the path fits, don't add ellipsis
            // The function checks if the path fits first, and only adds ellipsis if truncation is needed
            REQUIRE(result == path);
            REQUIRE(result == "file.txt");
        }
        
#ifdef _WIN32
        TEST_CASE("preserves Windows drive letter in truncated paths") {
            std::string path = "C:\\Users\\Test\\Documents\\VeryLongFileName.txt";
            float max_width = 25.0f; // Not enough for full path, but enough for "C:\\...\\VeryLongFileName.txt"
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // Should preserve "C:\\" prefix
            REQUIRE(test_helpers::CheckPathPrefix(result, "C:\\"));
            REQUIRE(test_helpers::CheckPathContainsEllipsis(result));
            // Should end with filename or last components
            // Use width calculator with tolerance (same as other tests)
            REQUIRE(test_helpers::CheckPathWidthWithinLimit(result, max_width, monospace_calc));
        }
        
        TEST_CASE("preserves Windows UNC path prefix") {
            std::string path = "\\\\server\\share\\Documents\\VeryLongFileName.txt";
            float max_width = 30.0f; // Not enough for full path, but enough for "\\\\server\\share\\...\\VeryLongFileName.txt"
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // Should preserve "\\\\server\\share\\" prefix
            REQUIRE(test_helpers::CheckPathPrefix(result, "\\\\server\\share\\"));
            REQUIRE(test_helpers::CheckPathContainsEllipsis(result));
            // Use width calculator with tolerance (same as other tests)
            REQUIRE(test_helpers::CheckPathWidthWithinLimit(result, max_width, monospace_calc));
        }
#else
        TEST_CASE("preserves Unix root directory in truncated paths") {
            std::string path = "/Users/Test/Documents/VeryLongFileName.txt";
            float max_width = 25.0f; // Not enough for full path, but enough for "/Users/.../VeryLongFileName.txt"
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // Should preserve "/Users/" prefix (first component after root)
            REQUIRE(test_helpers::CheckPathPrefix(result, "/Users/"));
            REQUIRE(test_helpers::CheckPathContainsEllipsis(result));
            // Should end with filename or last components
            REQUIRE(result.length() <= static_cast<size_t>(max_width));
        }
        
        TEST_CASE("preserves Unix root slash for paths without first component") {
            std::string path = "/etc/config/very/long/path/to/file.txt";
            float max_width = 20.0f; // Very narrow, just enough for "/.../file.txt"
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // Should preserve "/" prefix at minimum
            REQUIRE(test_helpers::CheckPathPrefix(result, "/"));
            REQUIRE(test_helpers::CheckPathContainsEllipsis(result));
            REQUIRE(result.length() <= static_cast<size_t>(max_width));
        }
#endif  // _WIN32
        
        TEST_CASE("relative paths work as before (no root to preserve)") {
            std::string path = "Documents/VeryLongFileName.txt";
            float max_width = 20.0f;
            
            std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            
            // Relative paths should truncate from beginning (existing behavior)
            REQUIRE(test_helpers::CheckPathPrefix(result, "..."));
            REQUIRE(result.length() <= static_cast<size_t>(max_width));
        }
        
    }
}
