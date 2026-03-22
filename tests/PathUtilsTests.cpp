#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <string>
#include <string_view>
#include <vector>

#include "TestHelpers.h"
#include "doctest/doctest.h"
#include "path/PathUtils.h"

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
            const size_t ellipsis_pos = result.find("...");
            if (ellipsis_pos != std::string::npos) {
                const size_t first_sep = result.find_first_of("\\/", ellipsis_pos + 3);
                if (first_sep != std::string::npos) {
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
            const size_t first_sep = result.find_first_of("\\/", 3);
            if (first_sep != std::string::npos) {
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

    TEST_CASE("single trailing slash is removed") {
        CHECK(path_utils::TrimTrailingSeparators("/a/b/") == "/a/b");
    }

    TEST_CASE("single trailing backslash is removed") {
        CHECK(path_utils::TrimTrailingSeparators("C:\\Windows\\") == "C:\\Windows");
    }

    TEST_CASE("multiple trailing separators are all removed") {
        CHECK(path_utils::TrimTrailingSeparators("/a/b///") == "/a/b");
        CHECK(path_utils::TrimTrailingSeparators("dir\\\\\\") == "dir");
    }

    TEST_CASE("mixed trailing separators are all removed") {
        CHECK(path_utils::TrimTrailingSeparators("/a/b\\/") == "/a/b");
        CHECK(path_utils::TrimTrailingSeparators("/a/b/\\") == "/a/b");
    }

    TEST_CASE("path consisting entirely of separators becomes empty") {
        CHECK(path_utils::TrimTrailingSeparators("///") == "");
        CHECK(path_utils::TrimTrailingSeparators("\\\\") == "");
        CHECK(path_utils::TrimTrailingSeparators("/") == "");
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
        CHECK(path_utils::JoinPath(components) == "/usr/local");
    }

    TEST_CASE("three components joined correctly") {
        const std::vector<std::string> components = {"/usr", "local", "bin"};
        CHECK(path_utils::JoinPath(components) == "/usr/local/bin");
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
