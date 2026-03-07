#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "path/PathPatternMatcher.h"

TEST_SUITE("PathPatternMatcher") {

  using path_pattern::MatchOptions;
  using path_pattern::PathPatternMatches;

  TEST_CASE("basic literals match full path") {
    CHECK(PathPatternMatches("C:\\\\file.txt", "C:\\\\file.txt"));
    CHECK_FALSE(PathPatternMatches("C:\\\\file.txt", "C:\\\\file.tx"));
  }

  TEST_CASE("single star does not cross separators") {
    CHECK(PathPatternMatches("src/*.cpp", "src/main.cpp"));
    CHECK_FALSE(PathPatternMatches("src/*.cpp", "src/foo/main.cpp"));
  }

  TEST_CASE("double star crosses separators") {
    CHECK(PathPatternMatches("src/**/*.cpp", "src/main.cpp"));
    CHECK(PathPatternMatches("src/**/*.cpp", "src/foo/main.cpp"));
    CHECK(PathPatternMatches("src/**/*.cpp", "src/a/b/c/main.cpp"));
    CHECK_FALSE(PathPatternMatches("src/**/*.cpp", "src/main.c"));
  }

  TEST_CASE("question mark matches a single non-separator") {
    CHECK(PathPatternMatches("file?.txt", "file1.txt"));
    CHECK(PathPatternMatches("file?.txt", "filea.txt"));
    CHECK_FALSE(PathPatternMatches("file?.txt", "file.txt"));
    CHECK_FALSE(PathPatternMatches("file?.txt", "file12.txt"));
  }

  TEST_CASE("character classes") {
    CHECK(PathPatternMatches("file[0-9].txt", "file3.txt"));
    CHECK_FALSE(PathPatternMatches("file[0-9].txt", "filex.txt"));

    CHECK(PathPatternMatches("file[abc].txt", "filea.txt"));
    CHECK(PathPatternMatches("file[abc].txt", "fileb.txt"));
    CHECK_FALSE(PathPatternMatches("file[abc].txt", "filed.txt"));

    CHECK(PathPatternMatches("file[^a].txt", "fileb.txt"));
    CHECK_FALSE(PathPatternMatches("file[^a].txt", "filea.txt"));
  }

  TEST_CASE("shorthands digit and word") {
    CHECK(PathPatternMatches("**/*\\d{3}*.log", "logs/error123.log"));
    CHECK_FALSE(PathPatternMatches("**/*\\d{3}*.log", "logs/error12.log"));

    CHECK(PathPatternMatches("**/\\w+.txt", "dir/file_1.txt"));
    CHECK_FALSE(PathPatternMatches("**/\\w+.txt", "dir/file-name.txt"));
  }

  TEST_CASE("quantifiers on classes and shorthands") {
    CHECK(PathPatternMatches("**/[A-Za-z]{2}_test.cpp", "src/ab_test.cpp"));
    CHECK_FALSE(PathPatternMatches("**/[A-Za-z]{2}_test.cpp", "src/a_test.cpp"));
    CHECK_FALSE(PathPatternMatches("**/[A-Za-z]{2}_test.cpp", "src/abc_test.cpp"));

    CHECK(PathPatternMatches("**/*\\d{3}*.log", "dir/a999b.log"));
    CHECK_FALSE(PathPatternMatches("**/*\\d{4}*.log", "dir/a999b.log"));
  }

  TEST_CASE("anchors") {
    CHECK(PathPatternMatches("^C:\\\\Windows\\\\**/*.exe$", "C:\\\\Windows\\\\notepad.exe"));
    CHECK_FALSE(PathPatternMatches("^C:\\\\Windows\\\\**/*.exe$", "D:\\\\Windows\\\\notepad.exe"));
  }

  TEST_CASE("double star root matches everything") {
    CHECK(PathPatternMatches("**/*.cpp", "main.cpp"));
    CHECK(PathPatternMatches("**/*.cpp", "src/main.cpp"));
    CHECK(PathPatternMatches("**/*.cpp", "a/b/c/main.cpp"));
  }

  TEST_CASE("case insensitive option") {
    CHECK(PathPatternMatches("**/*.TXT", "dir/file.txt",
                             MatchOptions::kCaseInsensitive));
    CHECK_FALSE(PathPatternMatches("**/*.TXT", "dir/file.txt",
                                   MatchOptions::kNone));
  }

  TEST_CASE("mixed separators") {
    CHECK(PathPatternMatches("C:\\\\Users/**/file.txt",
                             "C:\\\\Users/Me/Documents/file.txt"));
  }

  TEST_CASE("folder pattern with trailing double star") {
    // Test: **/folder** should match files directly in folder AND in subfolders
    // After normalization, **/folder** becomes **folder**
    // This is the preferred pattern format (without trailing **/)
    CHECK(PathPatternMatches("**/USN_windows**", "USN_windows/file.cpp"));
    CHECK(PathPatternMatches("**/USN_windows**", "USN_windows/subfolder/file.cpp"));
    CHECK(PathPatternMatches("**/USN_windows**", "some/path/USN_windows/file.cpp"));
    CHECK(PathPatternMatches("**/USN_windows**", "some/path/USN_windows/subfolder/file.cpp"));
    
    // Test: **/folder/** (with trailing **/) should also match (for backward compatibility)  // NOSONAR(cpp:S1103) - Pattern example intentionally contains \"*/\" sequence
    // After normalization, **/folder/** becomes **folder/**  // NOSONAR(cpp:S1103) - Pattern example intentionally contains \"*/\" sequence
    CHECK(PathPatternMatches("**/USN_windows/**", "USN_windows/file.cpp"));
    CHECK(PathPatternMatches("**/USN_windows/**", "USN_windows/subfolder/file.cpp"));
    CHECK(PathPatternMatches("**/USN_windows/**", "some/path/USN_windows/file.cpp"));
    CHECK(PathPatternMatches("**/USN_windows/**", "some/path/USN_windows/subfolder/file.cpp"));
  }
}


