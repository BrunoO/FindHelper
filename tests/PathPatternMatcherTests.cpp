#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <string_view>
#include <vector>

#include "path/PathPatternMatcher.h"
#include "utils/StringSearch.h"

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

  TEST_CASE("AppendLiteralRuns extracts all safe literal segments") {
    std::vector<std::string_view> runs;
    path_pattern::AppendLiteralRuns("**/logs/**/*.cpp", runs);
    REQUIRE(runs.size() == 2);
    CHECK(runs[0] == "logs");
    CHECK(runs[1] == ".cpp");

    path_pattern::AppendLiteralRuns("src/**/main", runs);
    REQUIRE(runs.size() == 2);
    CHECK(runs[0] == "src");
    CHECK(runs[1] == "main");

    path_pattern::AppendLiteralRuns("**/*", runs);
    CHECK(runs.empty());
  }

  TEST_CASE("FindPathPatternSpans highlights multiple literals in order") {
    std::vector<string_search::MatchSpan> spans;
    CHECK(path_pattern::FindPathPatternSpans("**/logs/**/*.cpp", "/var/logs/app/main.cpp", true,
                                             spans));
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].start_ == 5);
    CHECK(spans[0].end_ == 9);  // "logs"
    CHECK(spans[1].start_ == 18);
    CHECK(spans[1].end_ == 22);  // ".cpp"

    // Adjacent literals merge: "report" and ".txt" in report.txt
    CHECK(path_pattern::FindPathPatternSpans("**/report.txt", "/tmp/report.txt", true, spans));
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].start_ == 5);
    CHECK(spans[0].end_ == 15);  // "report.txt"

    CHECK_FALSE(path_pattern::FindPathPatternSpans("**/*", "/any/path", true, spans));
    CHECK(spans.empty());

    // Filename-only text: literals from pp:**Monkey*pdf still paint (also used for Name/Path columns).
    CHECK(path_pattern::FindPathPatternSpans("**Monkey*pdf", "SuperMonkeyReport.pdf", true, spans));
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].start_ == 5);
    CHECK(spans[0].end_ == 11);  // "Monkey"
    CHECK(spans[1].start_ == 18);
    CHECK(spans[1].end_ == 21);  // "pdf"
  }

  TEST_CASE("FindPathPatternSpans case-insensitive") {
    std::vector<string_search::MatchSpan> spans;
    CHECK(path_pattern::FindPathPatternSpans("**/Logs/**", "/var/LOGS/x", false, spans));
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].start_ == 5);
    CHECK(spans[0].end_ == 9);
  }
}


