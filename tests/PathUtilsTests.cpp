#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <array>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "TestHelpers.h"
#include "doctest/doctest.h"
#include "path/PathUtils.h"
#include "search/SearchPatternUtils.h"

namespace {

#ifdef _WIN32
// Asserts the three standard postconditions for a truncated path:
// correct prefix preserved, ellipsis present, and result fits within max_width.
void RequireTruncated(const std::string& result,
                      std::string_view expected_prefix,
                      float max_width,
                      const path_utils::TextWidthCalculator& calc) {
  REQUIRE(test_helpers::CheckPathPrefix(result, expected_prefix));
  REQUIRE(test_helpers::CheckPathContainsEllipsis(result));
  REQUIRE(test_helpers::CheckPathWidthWithinLimit(result, max_width, calc));
}

// Long Windows path truncation: drive preserved, ellipsis, suffix, width (shared by SUBCASEs).
void RequireWindowsLongPathTruncated(const std::string& result,
                                      float max_width,
                                      const std::string& expected_suffix,
                                      const path_utils::TextWidthCalculator& calc) {
  REQUIRE(test_helpers::CheckPathPrefix(result, "C:\\"));
  REQUIRE(test_helpers::CheckPathContainsEllipsis(result));
  REQUIRE(result.length() > 6);
  REQUIRE(test_helpers::CheckPathContainsSuffix(result, expected_suffix));
  REQUIRE(test_helpers::CheckPathWidthWithinLimit(result, max_width, calc));
}
#endif  // _WIN32

#ifndef _WIN32
// Relative long path (no drive): leading ellipsis + suffix + width.
void RequireRelativeLongPathTruncated(const std::string& result,
                                      float max_width,
                                      const std::string& expected_suffix,
                                      const path_utils::TextWidthCalculator& calc) {
  REQUIRE(test_helpers::CheckPathPrefix(result, "..."));
  REQUIRE(result.length() > 3);
  REQUIRE(test_helpers::CheckPathContainsSuffix(result, expected_suffix));
  REQUIRE(test_helpers::CheckPathWidthWithinLimit(result, max_width, calc));
}

// Unix absolute truncation: fixed prefix, ellipsis, length bound (Sonar duplication reduction).
void RequireUnixTruncatedPrefixEllipsis(const std::string& result,
                                        std::string_view prefix,
                                        float max_width) {
  REQUIRE(test_helpers::CheckPathPrefix(result, prefix));
  REQUIRE(test_helpers::CheckPathContainsEllipsis(result));
  REQUIRE(result.length() <= static_cast<size_t>(max_width));
}
#endif  // !_WIN32

}  // namespace

TEST_SUITE("PathUtils") {

    TEST_SUITE("TruncatePathAtBeginning") {

        // Use helper function to create width calculators
        const path_utils::TextWidthCalculator monospace_calc = test_helpers::CreateMonospaceWidthCalculator();

        TEST_CASE("returns ellipsis for zero or negative max_width") {
            const std::string path = "C:\\Users\\Test\\Documents\\file.txt";

            REQUIRE(path_utils::TruncatePathAtBeginning(path, 0.0f, monospace_calc) == "...");
            REQUIRE(path_utils::TruncatePathAtBeginning(path, -1.0f, monospace_calc) == "...");
            REQUIRE(path_utils::TruncatePathAtBeginning(path, -100.0f, monospace_calc) == "...");
        }

        TEST_CASE("returns full path if it fits within max_width") {
            const std::string path = "C:\\file.txt";
            const float path_width = monospace_calc(path);

            // Path should fit exactly
            REQUIRE(path_utils::TruncatePathAtBeginning(path, path_width, monospace_calc) == path);

            // Path should fit with extra space
            REQUIRE(path_utils::TruncatePathAtBeginning(path, path_width + 10.0f, monospace_calc) == path);
        }

        TEST_CASE("returns ellipsis only if even ellipsis doesn't fit") {
            const std::string path = "C:\\Users\\Test\\Documents\\file.txt";
            const float ellipsis_width = monospace_calc("...");

            // If max_width is less than ellipsis width, return ellipsis
            REQUIRE(path_utils::TruncatePathAtBeginning(path, ellipsis_width - 0.1f, monospace_calc) == "...");
            REQUIRE(path_utils::TruncatePathAtBeginning(path, ellipsis_width, monospace_calc) == "...");
        }

#ifdef _WIN32
        TEST_CASE("truncates long paths correctly") {
            const std::string path = "C:\\Users\\Test\\Documents\\VeryLongFileName.txt";
            // Need enough width: "C:\\" (3) + "..." (3) + "VeryLongFileName.txt" (20) = 26 chars minimum
            const float max_width = 30.0f; // Should fit "C:\\...\\VeryLongFileName.txt" or similar

            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            RequireWindowsLongPathTruncated(result, max_width, "VeryLongFileName.txt", monospace_calc);
        }

        TEST_CASE("prefers breaking at path separators") {
            const std::string path = "C:\\Users\\Test\\Documents\\file.txt";
            const float max_width = 15.0f; // Should fit "C:\\...\\Documents\\file.txt" or similar

            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);

            // Result should preserve root prefix "C:\\" and contain "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "C:\\"));
            REQUIRE(test_helpers::CheckPathContainsEllipsis(result));

            // If possible, should break after a separator
            // Check if there's a separator after the ellipsis
            if (const size_t ellipsis_pos = result.find("..."); ellipsis_pos != std::string::npos) {
                if (const size_t first_sep = result.find_first_of("\\/", ellipsis_pos + 3);
                    first_sep != std::string::npos) {
                    // The separator should be part of the truncated path
                    REQUIRE(first_sep < result.length());
                }
            }
        }
#else
        TEST_CASE("truncates long paths correctly") {
            // On non-Windows, use relative path (no root detection)
            const std::string path = "Users/Test/Documents/VeryLongFileName.txt";
            const float max_width = 25.0f; // Should fit "...VeryLongFileName.txt" (3 + 20 = 23 chars)

            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            RequireRelativeLongPathTruncated(result, max_width, "VeryLongFileName.txt", monospace_calc);
        }

        TEST_CASE("prefers breaking at path separators") {
            // On non-Windows, use relative path (no root detection)
            const std::string path = "Users/Test/Documents/file.txt";
            const float max_width = 15.0f; // Should fit "...Documents/file.txt" or similar

            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);

            // Result should start with "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "..."));

            // If possible, should break after a separator
            // Check if there's a separator after the ellipsis
            if (const size_t first_sep = result.find_first_of("\\/", 3); first_sep != std::string::npos) {
                // The separator should be part of the truncated path
                REQUIRE(first_sep < result.length());
            }
        }
#endif  // _WIN32

        TEST_CASE("handles empty path") {
            const std::string path;
            const float max_width = 100.0f;

            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);

            // Empty path should return as-is (fits in any width > 0)
            REQUIRE(result == path);
        }

#ifdef _WIN32
        TEST_CASE("handles path with only separators") {
            const std::string path = "C:\\";
            const float max_width = 100.0f;

            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);

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

            const float max_width = 30.0f;
            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);

            // Should preserve root prefix "C:\\" and contain "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "C:\\"));
            REQUIRE(test_helpers::CheckPathContainsEllipsis(result));

            // Should end with "file.txt"
            REQUIRE(test_helpers::CheckPathContainsSuffix(result, "file.txt"));

            // Result should fit
            REQUIRE(test_helpers::CheckPathWidthWithinLimit(result, max_width, monospace_calc));
        }

        TEST_CASE("handles path with forward slashes") {
            const std::string path = "C:/Users/Test/Documents/file.txt";
            const float max_width = 20.0f;

            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);

            // Should preserve root prefix "C:/" and contain "..."
            RequireTruncated(result, "C:/", max_width, monospace_calc);
        }

        TEST_CASE("handles mixed separators") {
            const std::string path = "C:\\Users/Test\\Documents/file.txt";
            const float max_width = 20.0f;

            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);

            // Should preserve root prefix "C:\\" and contain "..."
            RequireTruncated(result, "C:\\", max_width, monospace_calc);
        }
#else
        TEST_CASE("handles path with only separators") {
            const std::string path = "/";
            const float max_width = 100.0f;

            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);

            // Short path should return as-is
            REQUIRE(result == path);
        }

        TEST_CASE("handles very long path") {
            // Create a very long path (relative, no root)
            std::string path;
            for (int i = 0; i < 20; ++i) {
                path += "VeryLongDirectoryName" + std::to_string(i) + "/";
            }
            path += "file.txt";

            const float max_width = 30.0f;
            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);

            // Should start with "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "..."));

            // Should end with "file.txt"
            REQUIRE(test_helpers::CheckPathContainsSuffix(result, "file.txt"));

            // Result should fit
            REQUIRE(test_helpers::CheckPathWidthWithinLimit(result, max_width, monospace_calc));
        }

        TEST_CASE("handles path with forward slashes") {
            const std::string path = "Users/Test/Documents/file.txt";
            const float max_width = 20.0f;

            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);

            // Should start with "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "..."));
            REQUIRE(test_helpers::CheckPathWidthWithinLimit(result, max_width, monospace_calc));
        }

        TEST_CASE("handles mixed separators") {
            const std::string path = "Users/Test/Documents/file.txt";
            const float max_width = 20.0f;

            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);

            // Should start with "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "..."));
            REQUIRE(test_helpers::CheckPathWidthWithinLimit(result, max_width, monospace_calc));
        }
#endif  // _WIN32

        // Use helper function to create proportional width calculator
        const path_utils::TextWidthCalculator proportional_calc = test_helpers::CreateProportionalWidthCalculator();

        TEST_CASE("works with proportional font calculator") {
#ifdef _WIN32
            const std::string path = "C:\\Users\\Test\\Documents\\file.txt";
            const float max_width = 15.0f;

            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, proportional_calc);

            // On Windows, should preserve root prefix "C:\\" and contain "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "C:\\"));
            REQUIRE(test_helpers::CheckPathContainsEllipsis(result));
#else
            // On non-Windows, use relative path (no root detection)
            const std::string path = "Users/Test/Documents/file.txt";
            const float max_width = 15.0f;

            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, proportional_calc);

            // Should start with "..."
            REQUIRE(test_helpers::CheckPathPrefix(result, "..."));
#endif  // _WIN32
            REQUIRE(test_helpers::CheckPathWidthWithinLimit(result, max_width, proportional_calc));
        }

        TEST_CASE("edge case: max_width at or barely above ellipsis width") {
            const std::string path = "C:\\Users\\Test\\Documents\\file.txt";
            const float ellipsis_width = monospace_calc("...");
            SUBCASE("exactly equals ellipsis width") {
                REQUIRE(path_utils::TruncatePathAtBeginning(path, ellipsis_width, monospace_calc) == "...");
            }
            SUBCASE("slightly larger than ellipsis width") {
                REQUIRE(path_utils::TruncatePathAtBeginning(path, ellipsis_width + 0.1f, monospace_calc) ==
                        "...");
            }
        }

        TEST_CASE("edge case: path exactly fits after ellipsis") {
            const std::string path = "file.txt";
            const float ellipsis_width = monospace_calc("...");
            const float path_width = monospace_calc(path);
            const float max_width = ellipsis_width + path_width;

            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);

            // When path fits within max_width, function returns path as-is (no ellipsis needed)
            // This is the correct behavior: if the path fits, don't add ellipsis
            // The function checks if the path fits first, and only adds ellipsis if truncation is needed
            REQUIRE(result == path);
            REQUIRE(result == "file.txt");
        }

#ifdef _WIN32
        TEST_CASE("preserves Windows drive letter in truncated paths") {
            const std::string path = "C:\\Users\\Test\\Documents\\VeryLongFileName.txt";
            const float max_width = 25.0f; // Not enough for full path, but enough for "C:\\...\\VeryLongFileName.txt"

            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);

            // Should preserve "C:\\" prefix and fit within max_width
            RequireTruncated(result, "C:\\", max_width, monospace_calc);
        }

        TEST_CASE("preserves Windows UNC path prefix") {
            const std::string path = "\\\\server\\share\\Documents\\VeryLongFileName.txt";
            const float max_width = 30.0f; // Not enough for full path, but enough for "\\\\server\\share\\...\\VeryLongFileName.txt"

            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);

            // Should preserve "\\\\server\\share\\" prefix and fit within max_width
            RequireTruncated(result, "\\\\server\\share\\", max_width, monospace_calc);
        }
#else
        TEST_CASE("preserves Unix root directory in truncated paths") {
            const std::string path = "/Users/Test/Documents/VeryLongFileName.txt";
            const float max_width = 25.0f; // Not enough for full path, but enough for "/Users/.../VeryLongFileName.txt"

            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            RequireUnixTruncatedPrefixEllipsis(result, "/Users/", max_width);
        }

        TEST_CASE("preserves Unix root slash for paths without first component") {
            const std::string path = "/etc/config/very/long/path/to/file.txt";
            const float max_width = 20.0f; // Very narrow, just enough for "/.../file.txt"

            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);
            RequireUnixTruncatedPrefixEllipsis(result, "/", max_width);
        }
#endif  // _WIN32

        TEST_CASE("relative paths work as before (no root to preserve)") {
            const std::string path = "Documents/VeryLongFileName.txt";
            const float max_width = 20.0f;

            const std::string result = path_utils::TruncatePathAtBeginning(path, max_width, monospace_calc);

            // Relative paths should truncate from beginning (existing behavior)
            REQUIRE(test_helpers::CheckPathPrefix(result, "..."));
            REQUIRE(result.length() <= static_cast<size_t>(max_width));
        }

    }
}

TEST_SUITE("TrimTrailingSeparators") {

    TEST_CASE("empty path returns empty") {
        CHECK(path_utils::TrimTrailingSeparators("") == "");
    }

    TEST_CASE("path with no trailing separators is unchanged") {
        CHECK(path_utils::TrimTrailingSeparators("/a/b/c") == "/a/b/c");
        CHECK(path_utils::TrimTrailingSeparators("file.txt") == "file.txt");
        CHECK(path_utils::TrimTrailingSeparators("C:\\Windows") == "C:\\Windows");
    }

    TEST_CASE("trailing separator removal and all-separator collapse") {
        const std::array<std::pair<const char*, const char*>, 9> cases{{
            {"/a/b/", "/a/b"},
            {"C:\\Windows\\", "C:\\Windows"},
            {"/a/b///", "/a/b"},
            {R"(dir\\\)", "dir"},
            {"/a/b\\/", "/a/b"},
            {"/a/b/\\", "/a/b"},
            {"///", ""},
            {R"(\\)", ""},
            {"/", ""},
        }};
        for (const auto& [input, expected] : cases) {
            CHECK(path_utils::TrimTrailingSeparators(input) == expected);
        }
    }

    TEST_CASE("leading separators are preserved") {
        CHECK(path_utils::TrimTrailingSeparators("/a") == "/a");
        CHECK(path_utils::TrimTrailingSeparators("//server/share") == "//server/share");
    }

    TEST_CASE("returns a view into the original string (no allocation)") {
        const std::string path = "/a/b/c/";
        const std::string_view trimmed = path_utils::TrimTrailingSeparators(path);
        CHECK(trimmed == "/a/b/c");
        // The view points into the original buffer
        CHECK(trimmed.data() == path.data());
    }
}

TEST_SUITE("GetFilename") {

    TEST_CASE("returns filename after forward slash") {
        CHECK(path_utils::GetFilename("/path/to/file.txt") == "file.txt");
    }

    TEST_CASE("returns filename after backslash") {
        CHECK(path_utils::GetFilename("C:\\path\\file.txt") == "file.txt");
    }

    TEST_CASE("returns filename after mixed separators") {
        CHECK(path_utils::GetFilename("/path\\to/file.txt") == "file.txt");
    }

    TEST_CASE("path with no separator returns whole string") {
        CHECK(path_utils::GetFilename("file.txt") == "file.txt");
        CHECK(path_utils::GetFilename("Makefile") == "Makefile");
    }

    TEST_CASE("empty path returns empty") {
        CHECK(path_utils::GetFilename("") == "");
    }

    TEST_CASE("trailing separator produces empty filename") {
        CHECK(path_utils::GetFilename("/dir/") == "");
    }
}

TEST_SUITE("GetExtension") {

    TEST_CASE("returns extension without the dot") {
        CHECK(path_utils::GetExtension("/path/to/file.txt") == "txt");
        CHECK(path_utils::GetExtension("archive.tar.gz") == "gz");
        CHECK(path_utils::GetExtension("file.cpp") == "cpp");
    }

    TEST_CASE("file with no extension returns empty") {
        CHECK(path_utils::GetExtension("Makefile") == "");
        CHECK(path_utils::GetExtension("/path/no_ext") == "");
    }

    TEST_CASE("empty path returns empty") {
        CHECK(path_utils::GetExtension("") == "");
    }

    TEST_CASE("file ending with dot returns empty (no extension after dot)") {
        CHECK(path_utils::GetExtension("file.") == "");
    }
}

TEST_SUITE("GetDirectoryPathView") {

    TEST_CASE("Windows drive-root file keeps C:\\") {
        CHECK(path_utils::GetDirectoryPathView(R"(C:\file.txt)", 3) == "C:\\");
        CHECK(path_utils::GetDirectoryPathView(R"(C:\file.txt)") == "C:\\");
        CHECK(path_utils::GetDirectoryPathView("C:/file.txt") == "C:/");
    }

    TEST_CASE("Unix root file keeps /") {
        CHECK(path_utils::GetDirectoryPathView("/file.txt", 1) == "/");
        CHECK(path_utils::GetDirectoryPathView("/file.txt") == "/");
    }

    TEST_CASE("nested paths omit trailing separator") {
        CHECK(path_utils::GetDirectoryPathView(R"(C:\Users\docs\file.txt)") == "C:\\Users\\docs");
        CHECK(path_utils::GetDirectoryPathView("/home/user/file.txt") == "/home/user");
    }

    TEST_CASE("no separator yields empty directory") {
        CHECK(path_utils::GetDirectoryPathView("file.txt").empty());
        CHECK(path_utils::GetDirectoryPathView("file.txt", 0).empty());
    }

    TEST_CASE("volume root path alone is its own directory") {
        CHECK(path_utils::GetDirectoryPathView(R"(C:\)") == "C:\\");
        CHECK(path_utils::GetDirectoryPathView("/") == "/");
    }
}

TEST_SUITE("VolumeRootPathHelpers") {

    TEST_CASE("IsDriveLetterRootKey") {
        CHECK(path_utils::IsDriveLetterRootKey("C:"));
        CHECK(path_utils::IsDriveLetterRootKey("d:"));
        CHECK_FALSE(path_utils::IsDriveLetterRootKey(R"(C:\)"));
        CHECK_FALSE(path_utils::IsDriveLetterRootKey("C"));
        CHECK_FALSE(path_utils::IsDriveLetterRootKey(""));
    }

    TEST_CASE("IsVolumeRootChildPath") {
        CHECK(path_utils::IsVolumeRootChildPath(R"(C:\file1.txt)"));
        CHECK(path_utils::IsVolumeRootChildPath("/tmpfile"));
        CHECK_FALSE(path_utils::IsVolumeRootChildPath(R"(C:\Users\file.txt)"));
        CHECK_FALSE(path_utils::IsVolumeRootChildPath(R"(C:\)"));
        CHECK_FALSE(path_utils::IsVolumeRootChildPath("/"));
        CHECK_FALSE(path_utils::IsVolumeRootChildPath("file1.txt"));
    }

    TEST_CASE("IsVolumeWidePathPrefix") {
        CHECK(path_utils::IsVolumeWidePathPrefix(R"(C:\)"));
        CHECK(path_utils::IsVolumeWidePathPrefix("/"));
        CHECK_FALSE(path_utils::IsVolumeWidePathPrefix(R"(C:\Users\)"));
        CHECK_FALSE(path_utils::IsVolumeWidePathPrefix("C:"));
    }
}

TEST_SUITE("GetParentDirectory") {

    TEST_CASE("drive-root file parent keeps separator; volume root has no parent") {
        CHECK(path_utils::GetParentDirectory(R"(C:\file.txt)") == "C:\\");
        CHECK(path_utils::GetParentDirectory("/file.txt") == "/");
        CHECK(path_utils::GetParentDirectory(R"(C:\)").empty());
        CHECK(path_utils::GetParentDirectory("/").empty());
    }

    TEST_CASE("parent walk from drive-root file reaches volume root then stops") {
        std::string base = R"(C:\file.txt)";
        int steps = 0;
        constexpr int kMax = 20;
        while (steps < kMax && !base.empty()) {
            base = path_utils::GetParentDirectory(base);
            ++steps;
        }
        CHECK(steps == 2);  // C:\file.txt → C:\ → ""
        CHECK(base.empty());
    }

    TEST_CASE("nested parent omits trailing separator") {
        CHECK(path_utils::GetParentDirectory(R"(C:\Users\file.txt)") == "C:\\Users");
        CHECK(path_utils::GetParentDirectory("/home/user/file.txt") == "/home/user");
    }

    TEST_CASE("no parent yields empty") {
        CHECK(path_utils::GetParentDirectory("file.txt").empty());
        CHECK(path_utils::GetParentDirectory("").empty());
    }
}

TEST_SUITE("GetDefaultVolumeRootPathView") {

    TEST_CASE("returns non-empty root") {
        const std::string_view root = path_utils::GetDefaultVolumeRootPathView();
        CHECK_FALSE(root.empty());
    }

    TEST_CASE("content matches GetDefaultVolumeRootPath") {
        const std::string root_str = path_utils::GetDefaultVolumeRootPath();
        const std::string_view root_view = path_utils::GetDefaultVolumeRootPathView();
        CHECK(root_view == root_str);
    }

#ifndef _WIN32
    TEST_CASE("non-Windows root is forward slash") {
        CHECK(path_utils::GetDefaultVolumeRootPathView() == "/");
    }
#endif  // _WIN32
}

TEST_SUITE("JoinPath vector overload") {

    TEST_CASE("empty vector returns empty string") {
        const std::vector<std::string> components;
        CHECK(path_utils::JoinPath(components) == "");
    }

    TEST_CASE("single component returns that component") {
        const std::vector<std::string> components = {"/usr"};
        CHECK(path_utils::JoinPath(components) == "/usr");
    }

    TEST_CASE("two components joined correctly") {
        const std::vector<std::string> components = {"/usr", "local"};
#ifdef _WIN32
        // JoinPath inserts the platform separator between segments when neither has one.
        CHECK(path_utils::JoinPath(components) == "/usr\\local");
#else
        CHECK(path_utils::JoinPath(components) == "/usr/local");
#endif  // _WIN32
    }

    TEST_CASE("three components joined correctly") {
        const std::vector<std::string> components = {"/usr", "local", "bin"};
#ifdef _WIN32
        CHECK(path_utils::JoinPath(components) == "/usr\\local\\bin");
#else
        CHECK(path_utils::JoinPath(components) == "/usr/local/bin");
#endif  // _WIN32
    }

    TEST_CASE("components with trailing separator deduplicated") {
        const std::vector<std::string> components = {"/usr/", "local/", "bin"};
        CHECK(path_utils::JoinPath(components) == "/usr/local/bin");
    }

    TEST_CASE("result equals repeated two-argument JoinPath") {
        const std::vector<std::string> components = {"a", "b", "c", "d"};
        const std::string expected =
            path_utils::JoinPath(path_utils::JoinPath(path_utils::JoinPath("a", "b"), "c"), "d");
        CHECK(path_utils::JoinPath(components) == expected);
    }
}

TEST_SUITE("JoinPath dot handling") {

    TEST_CASE("joining with dot component returns base unchanged") {
        CHECK(path_utils::JoinPath("C:\\", ".") == "C:\\");
        CHECK(path_utils::JoinPath("/", ".") == "/");
        CHECK(path_utils::JoinPath("C:\\Users", ".") == "C:\\Users");
        CHECK(path_utils::JoinPath("/home/user", ".") == "/home/user");
    }

    TEST_CASE("joining with dot-dot component returns base unchanged") {
        CHECK(path_utils::JoinPath("C:\\", "..") == "C:\\");
        CHECK(path_utils::JoinPath("/", "..") == "/");
        CHECK(path_utils::JoinPath("C:\\Users", "..") == "C:\\Users");
        CHECK(path_utils::JoinPath("/home/user", "..") == "/home/user");
    }

    TEST_CASE("joining with empty component returns base") {
        CHECK(path_utils::JoinPath("C:\\", "") == "C:\\");
        CHECK(path_utils::JoinPath("/home", "") == "/home");
    }
}

#ifndef _WIN32
namespace {

// Sets (or unsets, when value is nullptr) an environment variable for one scope and restores the
// previous state on exit. Test-only; these tests run single-threaded.
class ScopedEnvVar {
public:
    ScopedEnvVar(const char* name, const char* value) : name_(name) {
        if (const char* previous = std::getenv(name_); previous != nullptr) {  // NOLINT(concurrency-mt-unsafe) - test-only
            had_value_ = true;
            previous_value_ = previous;
        }
        Apply(value);
    }
    ~ScopedEnvVar() { Apply(had_value_ ? previous_value_.c_str() : nullptr); }
    ScopedEnvVar(const ScopedEnvVar&) = delete;
    ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;
    ScopedEnvVar(ScopedEnvVar&&) = delete;
    ScopedEnvVar& operator=(ScopedEnvVar&&) = delete;

private:
    void Apply(const char* value) const {
        if (value != nullptr) {
            setenv(name_, value, 1);  // NOLINT(concurrency-mt-unsafe) - test-only
        } else {
            unsetenv(name_);  // NOLINT(concurrency-mt-unsafe) - test-only
        }
    }

    const char* name_;
    bool had_value_ = false;
    std::string previous_value_;
};

}  // namespace
#endif  // !_WIN32

TEST_SUITE("GetTrashPath") {

#ifndef _WIN32
    TEST_CASE("HOME set: result is absolute, under HOME, never a literal ~ or the empty-home fallback") {
        const ScopedEnvVar home("HOME", "/tmp/findhelper_trash_home");
        const ScopedEnvVar xdg("XDG_DATA_HOME", nullptr);
        const std::string trash = path_utils::GetTrashPath();
        CHECK_FALSE(trash.empty());
        CHECK(trash.front() == '/');
        CHECK(trash.front() != '~');
        CHECK(trash != "/Users/");
        CHECK(trash.rfind("/tmp/findhelper_trash_home/", 0) == 0);
    }

#ifdef __APPLE__
    TEST_CASE("macOS: trash is <home>/.Trash and ignores XDG_DATA_HOME") {
        const ScopedEnvVar home("HOME", "/tmp/fh_home");
        const ScopedEnvVar xdg("XDG_DATA_HOME", "/tmp/fh_xdg");
        CHECK(path_utils::GetTrashPath() == "/tmp/fh_home/.Trash");
    }
#endif  // __APPLE__

#ifdef __linux__
    // Freedesktop Trash spec: $XDG_DATA_HOME/Trash, else ~/.local/share/Trash. Compiled on Linux only.
    TEST_CASE("Linux: absolute XDG_DATA_HOME overrides HOME") {
        const ScopedEnvVar home("HOME", "/tmp/fh_home");
        const ScopedEnvVar xdg("XDG_DATA_HOME", "/tmp/fh_xdg");
        CHECK(path_utils::GetTrashPath() == "/tmp/fh_xdg/Trash");
    }

    TEST_CASE("Linux: XDG_DATA_HOME unset uses <home>/.local/share/Trash") {
        const ScopedEnvVar home("HOME", "/tmp/fh_home");
        const ScopedEnvVar xdg("XDG_DATA_HOME", nullptr);
        CHECK(path_utils::GetTrashPath() == "/tmp/fh_home/.local/share/Trash");
    }

    TEST_CASE("Linux: empty or relative XDG_DATA_HOME is ignored (freedesktop spec)") {
        const ScopedEnvVar home("HOME", "/tmp/fh_home");
        {
            const ScopedEnvVar xdg("XDG_DATA_HOME", "");
            CHECK(path_utils::GetTrashPath() == "/tmp/fh_home/.local/share/Trash");
        }
        {
            const ScopedEnvVar xdg("XDG_DATA_HOME", "relative/dir");
            CHECK(path_utils::GetTrashPath() == "/tmp/fh_home/.local/share/Trash");
        }
    }
#endif  // __linux__
#else
    TEST_CASE("Windows: result is non-empty and never a literal ~") {
        const std::string trash = path_utils::GetTrashPath();
        CHECK_FALSE(trash.empty());
        CHECK(trash.find('~') == std::string::npos);
    }
#endif  // !_WIN32
}

// AC-5: the quick-filter literal matched through the path matcher. The app forces case-insensitive
// matching while the path text is "$Recycle.Bin" (SearchWorker.cpp); these cases prove that matcher
// reaches every drive and casing. Lives here because SearchPatternUtilsTests is outside this scope.
TEST_SUITE("Recycle Bin quick filter matcher (AC-5)") {

    TEST_CASE("case-insensitive $Recycle.Bin matches every drive and on-disk casing") {
        const auto matcher = search_pattern_utils::CreatePathMatcher("$Recycle.Bin", false);
        CHECK(matcher("C:\\$Recycle.Bin\\S-1-5-21-1000\\$R1A2B3C.txt"));
        CHECK(matcher("D:\\$RECYCLE.BIN\\S-1-5-21-1000\\$Rxyz.jpg"));
        CHECK_FALSE(matcher("C:\\Users\\bob\\Desktop\\notes.txt"));
    }

    TEST_CASE("case-sensitive $Recycle.Bin misses upper-case on-disk casing (why forcing is needed)") {
        const auto matcher = search_pattern_utils::CreatePathMatcher("$Recycle.Bin", true);
        CHECK(matcher("C:\\$Recycle.Bin\\S-1-5-21-1000\\$R1A2B3C.txt"));
        CHECK_FALSE(matcher("D:\\$RECYCLE.BIN\\S-1-5-21-1000\\$Rxyz.jpg"));
    }
}

TEST_SUITE("IsUnderRecycleBin") {

    TEST_CASE("matches $Recycle.Bin in every on-disk casing") {
        CHECK(path_utils::IsUnderRecycleBin("C:\\$Recycle.Bin\\S-1-5-21-1000\\$R1A2B3C.txt"));
        CHECK(path_utils::IsUnderRecycleBin("C:\\$RECYCLE.BIN\\S-1-5-21-1000\\$R1A2B3C.txt"));
        CHECK(path_utils::IsUnderRecycleBin("C:\\$recycle.bin\\S-1-5-21-1000\\$R1A2B3C.txt"));
        CHECK(path_utils::IsUnderRecycleBin("C:\\$ReCyCle.BiN\\S-1-5-21-1000\\$R1A2B3C.txt"));
    }

    TEST_CASE("matches the bin on every drive letter") {
        CHECK(path_utils::IsUnderRecycleBin("C:\\$Recycle.Bin\\S-1-5-21-1000\\$Rabc.txt"));
        CHECK(path_utils::IsUnderRecycleBin("D:\\$RECYCLE.BIN\\S-1-5-21-1000\\$Rabc.txt"));
        CHECK(path_utils::IsUnderRecycleBin("E:/$Recycle.Bin/S-1-5-21-1000/$Rabc.txt"));
    }

    TEST_CASE("matches a nested bin path (substring, not only drive-root prefix)") {
        CHECK(path_utils::IsUnderRecycleBin("C:\\Data\\$Recycle.Bin\\S-1-5-21-1000\\$Rabc\\sub\\f.txt"));
    }

    TEST_CASE("rejects ordinary user files and folders") {
        CHECK_FALSE(path_utils::IsUnderRecycleBin("C:\\Users\\bob\\Desktop\\notes.txt"));
        CHECK_FALSE(path_utils::IsUnderRecycleBin("C:\\Users\\bob\\Documents\\$Report.docx"));
        CHECK_FALSE(path_utils::IsUnderRecycleBin("/Users/bob/Desktop/notes.txt"));
    }

    TEST_CASE("rejects near-miss names that are not the bin folder") {
        CHECK_FALSE(path_utils::IsUnderRecycleBin("C:\\$Report.docx"));
        CHECK_FALSE(path_utils::IsUnderRecycleBin("C:\\Recycle.Bin\\f.txt"));
        CHECK_FALSE(path_utils::IsUnderRecycleBin("C:\\$Recycle\\f.txt"));
    }

    TEST_CASE("rejects $recycle.bin as a name prefix, not a whole path segment") {
        CHECK_FALSE(path_utils::IsUnderRecycleBin("C:\\Users\\bob\\$recycle.bin_notes\\f.txt"));
    }

    TEST_CASE("empty string is not in the bin") {
        CHECK_FALSE(path_utils::IsUnderRecycleBin(""));
    }
}
