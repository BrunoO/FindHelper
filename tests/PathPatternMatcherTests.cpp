#include "doctest/doctest.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "path/PathPatternMatcher.h"
#include "utils/StringSearch.h"

TEST_SUITE("PathPatternMatcher") {

  using path_pattern::PathPatternMatches;

  TEST_CASE("basic literals match full path") {
    CHECK(PathPatternMatches("C:\\\\file.txt", "C:\\\\file.txt"));
    CHECK_FALSE(PathPatternMatches("C:\\\\file.txt", "C:\\\\file.tx"));
  }

  TEST_CASE("paths with dev and work directory components") {
    CHECK(PathPatternMatches("c:\\dev", "c:\\dev"));
    CHECK(PathPatternMatches("c:\\work", "c:\\work"));
    CHECK(PathPatternMatches("c:\\dev\\file.txt", "c:\\dev\\file.txt"));
    CHECK(PathPatternMatches("c:\\work\\file.txt", "c:\\work\\file.txt"));
    CHECK(PathPatternMatches("c:\\dev\\*.cpp", "c:\\dev\\main.cpp"));
    CHECK(PathPatternMatches("c:\\work\\*.txt", "c:\\work\\notes.txt"));
    // Cross-platform separator matching
    CHECK(PathPatternMatches("c:\\dev", "c:/dev"));
    CHECK(PathPatternMatches("c:/dev", "c:\\dev"));
    CHECK(PathPatternMatches("c:\\work", "c:/work"));
    CHECK(PathPatternMatches("src\\dev\\main.cpp", "src/dev/main.cpp"));
    CHECK(PathPatternMatches("src\\work\\main.cpp", "src/work/main.cpp"));
    // Doubled backslash paths
    CHECK(PathPatternMatches("c:\\\\dev", "c:\\\\dev"));
    CHECK(PathPatternMatches("c:\\\\work", "c:\\\\work"));
    // Other common directory names starting with d or w
    CHECK(PathPatternMatches("c:\\windows\\system32", "c:\\windows\\system32"));
    CHECK(PathPatternMatches("c:\\downloads\\*.zip", "c:\\downloads\\archive.zip"));
    CHECK(PathPatternMatches("d:\\data\\report.csv", "d:\\data\\report.csv"));
    // Path combined with literal brackets
    CHECK(PathPatternMatches("c:\\dev\\*[0-9].cpp", "c:\\dev\\file[0-9].cpp"));
    CHECK_FALSE(PathPatternMatches("c:\\dev\\*[0-9].cpp", "c:\\dev\\file1.cpp"));
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

  TEST_CASE("brackets, braces, and plus match literally") {
    CHECK(PathPatternMatches("file[0-9].txt", "file[0-9].txt"));
    CHECK_FALSE(PathPatternMatches("file[0-9].txt", "file3.txt"));

    CHECK(PathPatternMatches("file[abc].txt", "file[abc].txt"));
    CHECK_FALSE(PathPatternMatches("file[abc].txt", "filea.txt"));

    CHECK(PathPatternMatches("file[^a].txt", "file[^a].txt"));
    CHECK_FALSE(PathPatternMatches("file[^a].txt", "fileb.txt"));

    CHECK(PathPatternMatches("file{1,2}.txt", "file{1,2}.txt"));
    CHECK_FALSE(PathPatternMatches("file{1,2}.txt", "file1.txt"));

    CHECK(PathPatternMatches("file+name.txt", "file+name.txt"));
    CHECK_FALSE(PathPatternMatches("file+name.txt", "filename.txt"));
  }

  TEST_CASE("backslash is a path separator, never a shorthand escape") {
    // '\d' / '\w' compile to separator + literal
    CHECK(PathPatternMatches("c:\\d", "c:/d"));
    CHECK_FALSE(PathPatternMatches("c:\\d", "c:/5"));  // not a digit class
    CHECK(PathPatternMatches("src\\w\\file.txt", "src/w/file.txt"));
    CHECK_FALSE(PathPatternMatches("src\\w\\file.txt", "src/2/file.txt"));

    // '\d' means separator + literal 'd'
    CHECK(PathPatternMatches("**/*\\d*.log", "dir/logs/daily.log"));
    CHECK_FALSE(PathPatternMatches("**/*\\d*.log", "dir/logs/1.log"));
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
                             true));
    CHECK_FALSE(PathPatternMatches("**/*.TXT", "dir/file.txt",
                                   false));
  }

  TEST_CASE("mixed separators") {
    CHECK(PathPatternMatches("C:\\\\Users/**/file.txt",
                             "C:\\\\Users/Me/Documents/file.txt"));
  }

  TEST_CASE("folder pattern with trailing double star") {
    // Test: star-star folder star-star matches files directly in folder AND
    // in subfolders, with the segment boundary preserved.
    CHECK(PathPatternMatches("**/USN_windows**", "USN_windows/file.cpp"));
    CHECK(PathPatternMatches("**/USN_windows**", "USN_windows/subfolder/file.cpp"));
    CHECK(PathPatternMatches("**/USN_windows**", "some/path/USN_windows/file.cpp"));
    CHECK(PathPatternMatches("**/USN_windows**", "some/path/USN_windows/subfolder/file.cpp"));

    // Test: **/folder/** (with trailing **/) should also match (for backward compatibility)  // NOSONAR(cpp:S1103) - Pattern example intentionally contains \"*/\" sequence
    CHECK(PathPatternMatches("**/USN_windows/**", "USN_windows/file.cpp"));
    CHECK(PathPatternMatches("**/USN_windows/**", "USN_windows/subfolder/file.cpp"));
    CHECK(PathPatternMatches("**/USN_windows/**", "some/path/USN_windows/file.cpp"));
    CHECK(PathPatternMatches("**/USN_windows/**", "some/path/USN_windows/subfolder/file.cpp"));
  }

  TEST_CASE("double star slash preserves segment boundary") {
    // Double-star-slash matches zero or more whole segments (leading or
    // middle also match empty, i.e. zero directories).
    CHECK(PathPatternMatches("**/*.cpp", "main.cpp"));
    CHECK(PathPatternMatches("src/**/*.cpp", "src/main.cpp"));
    CHECK(PathPatternMatches("src/**/*.cpp", "src/a/main.cpp"));

    // Strict starts-with: the folder segment must start with "report".
    CHECK(PathPatternMatches("**/report*/**/*.pdf", "docs/report_q1/file.pdf"));
    CHECK(PathPatternMatches("**/report*/**/*.pdf", "report_q1/file.pdf"));
    CHECK_FALSE(PathPatternMatches("**/report*/**/*.pdf", "docs/my_report/file.pdf"));
    CHECK_FALSE(PathPatternMatches("**/report*/**/*.pdf", "docs/my_report/sub/file.pdf"));

    // Contains: star-report-star matches both prefix and infix positions.
    CHECK(PathPatternMatches("**/*report*/**/*.pdf", "docs/report_q1/file.pdf"));
    CHECK(PathPatternMatches("**/*report*/**/*.pdf", "docs/my_report/file.pdf"));

    // Exact folder still matches anywhere, but not partial names.
    CHECK(PathPatternMatches("**/logs/**/*.cpp", "a/logs/main.cpp"));
    CHECK(PathPatternMatches("**/logs/**/*.cpp", "logs/main.cpp"));
    CHECK_FALSE(PathPatternMatches("**/logs/**/*.cpp", "a/my_logs/main.cpp"));
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

    CHECK(path_pattern::FindPathPatternSpans("**/logs/**/storage/**/service/*.cpp",
                                             "/var/logs/app/storage/service/main.cpp", true,
                                             spans));
    REQUIRE(spans.size() == 4);
    CHECK(spans[0].start_ == 5);
    CHECK(spans[0].end_ == 9);    // "logs"
    CHECK(spans[1].start_ == 14);
    CHECK(spans[1].end_ == 21);   // "storage"
    CHECK(spans[2].start_ == 22);
    CHECK(spans[2].end_ == 29);   // "service"
    CHECK(spans[3].start_ == 34);
    CHECK(spans[3].end_ == 38);   // ".cpp"
  }

  TEST_CASE("FindPathPatternSpans case-insensitive") {
    std::vector<string_search::MatchSpan> spans;
    CHECK(path_pattern::FindPathPatternSpans("**/Logs/**", "/var/LOGS/x", false, spans));
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].start_ == 5);
    CHECK(spans[0].end_ == 9);
  }

  TEST_CASE("CompilePathPattern extracts required substring for pre-filtering") {
    const auto compiled = path_pattern::CompilePathPattern(
        "**/long_literal_directory_name/**/another_long_name/*.cpp",
        true);
    CHECK(compiled.has_required_substring);
    CHECK(compiled.required_substring == "long_literal_directory_name");

    const auto short_pat = path_pattern::CompilePathPattern("*.c");
    CHECK_FALSE(short_pat.has_required_substring);
  }

  TEST_CASE("multi-literal AND prefilter requires every long run") {
    const auto compiled =
        path_pattern::CompilePathPattern("**/*controller*test*.*", false);
    REQUIRE(compiled.IsValid());
    CHECK(compiled.required_substring == "controller");
    REQUIRE(compiled.extra_required_substrings.size() == 1);
    CHECK(compiled.extra_required_substrings[0] == "test");

    CHECK(PathPatternMatches("**/*controller*test*.*", "src/mycontroller_unit_test.cpp"));
    // Missing the second literal: rejected by the prefilter, same verdict as the matcher.
    CHECK_FALSE(PathPatternMatches("**/*controller*test*.*", "src/mycontroller_unit.cpp"));
    CHECK_FALSE(PathPatternMatches("**/*controller*test*.*", "src/mytest_unit.cpp"));
  }

  TEST_CASE("trailing run constrains the path end") {
    const auto compiled = path_pattern::CompilePathPattern(
        "**/USN_Windows**/*.md", true);
    REQUIRE(compiled.IsValid());
    CHECK(compiled.required_substring == "usn_windows");
    CHECK(compiled.has_required_suffix);
    CHECK(compiled.required_suffix == ".md");

    CHECK(PathPatternMatches("**/USN_Windows**/*.md", "/a/USN_Windows/b/notes.md",
                             true));
    // Literal present but wrong ending: rejected without backtracking.
    CHECK_FALSE(PathPatternMatches("**/USN_Windows**/*.md", "/a/USN_Windows/b/notes.txt",
                                   true));
    CHECK_FALSE(PathPatternMatches("**/USN_Windows**/*.md", "/a/other/b/notes.md",
                                   true));
  }

  TEST_CASE("a run before a wildcard is a required literal") {
    const auto compiled =
        path_pattern::CompilePathPattern("*xyz*1234*", false);
    REQUIRE(compiled.IsValid());
    CHECK(compiled.required_substring == "1234");
    REQUIRE(compiled.extra_required_substrings.size() == 1);
    CHECK(compiled.extra_required_substrings[0] == "xyz");

    CHECK(PathPatternMatches("*xyz*1234*", "axyzzz1234b"));
    CHECK_FALSE(PathPatternMatches("*xyz*1234*", "axy1234b"));
  }

  TEST_CASE("simple patterns longer than the NFA mask fall back to backtracking") {
    // kMaxSimpleTokens tokens fit the 64-bit NFA state mask; longer patterns
    // compile on the backtracking matcher, which uses the same token meanings.
    constexpr std::size_t kLong = path_pattern::kMaxSimpleTokens + 20U;
    const std::string body(kLong, 'a');
    const std::string pattern = "src/" + body + "*.cpp";

    const auto compiled = path_pattern::CompilePathPattern(pattern, false);
    REQUIRE(compiled.IsValid());
    // Same verdict as the short equivalent, which still uses the NFA.
    CHECK(PathPatternMatches(compiled, "src/" + body + "main.cpp"));
    CHECK_FALSE(PathPatternMatches(compiled, "src/" + body + "main.txt"));
    CHECK_FALSE(PathPatternMatches(compiled, "other/" + body + "main.cpp"));
    CHECK_FALSE(PathPatternMatches(compiled, "src/main.cpp"));

    // Wildcards keep their simple meanings on the fallback path: '*' stays
    // inside one segment and '?' stays exactly one character.
    const std::string wildcards = "src/" + body + "/*/x?.cpp";
    const auto wild = path_pattern::CompilePathPattern(wildcards, false);
    REQUIRE(wild.IsValid());
    CHECK(PathPatternMatches(wild, "src/" + body + "/sub/x1.cpp"));
    CHECK_FALSE(PathPatternMatches(wild, "src/" + body + "/a/b/x1.cpp"));
  }

  TEST_CASE("braces and plus stay literals in all positions") {
    CHECK(PathPatternMatches("a{x}b", "a{x}b"));
    CHECK_FALSE(PathPatternMatches("a{x}b", "ax}b"));
    CHECK(PathPatternMatches("**/a{x}b", "d/a{x}b"));
    CHECK_FALSE(PathPatternMatches("**/a{x}b", "d/ax}b"));

    CHECK(PathPatternMatches("a{2,x}b", "a{2,x}b"));
    CHECK(PathPatternMatches("a{2}b", "a{2}b"));
    CHECK_FALSE(PathPatternMatches("a{2}b", "aab"));
    CHECK(PathPatternMatches("a{2,}b", "a{2,}b"));
    CHECK(PathPatternMatches("a{2,3}b", "a{2,3}b"));
    CHECK(PathPatternMatches("a+b", "a+b"));
    CHECK_FALSE(PathPatternMatches("a+b", "aab"));
  }

  TEST_CASE("a moved-from compiled pattern never matches") {
    path_pattern::CompiledPathPattern original =
        path_pattern::CompilePathPattern("**/*.cpp", false);
    REQUIRE(original.IsValid());
    const path_pattern::CompiledPathPattern moved(std::move(original));
    REQUIRE(moved.IsValid());
    // Reading the marker off a moved-from object is the assertion under test:
    // the move must leave it explicitly unusable rather than looking valid.
    CHECK(original.status == path_pattern::CompileStatus::kMovedFrom);  // NOLINT(bugprone-use-after-move) - intentional: verifying the moved-from marker
    CHECK_FALSE(PathPatternMatches(original, "src/main.cpp"));
    CHECK(PathPatternMatches(moved, "src/main.cpp"));
  }

  TEST_CASE("every compile status has a human-readable description") {
    CHECK(std::string_view(path_pattern::DescribeCompileStatus(
              path_pattern::CompileStatus::kOk)) == "ok");
    CHECK(std::string_view(path_pattern::DescribeCompileStatus(
              path_pattern::CompileStatus::kMovedFrom)) != "ok");
  }

  TEST_CASE("all-pass patterns match unconditionally") {
    const std::vector<std::string_view> all_pass_patterns = {
        "", "**", "***", "****", "^**$", "^**", "**$"
    };
    for (const auto& pat : all_pass_patterns) {
      CHECK(path_pattern::IsAllPassPathPattern(pat));
      const auto compiled = path_pattern::CompilePathPattern(pat, false);
      CHECK(compiled.IsValid());
      CHECK(compiled.is_all_pass);
      CHECK(PathPatternMatches(compiled, ""));
      CHECK(PathPatternMatches(compiled, "foo.txt"));
      CHECK(PathPatternMatches(compiled, "C:\\Windows\\System32\\cmd.exe"));
      CHECK(PathPatternMatches(compiled, "/usr/local/bin/antigravity"));
    }

    const std::vector<std::string_view> non_all_pass_patterns = {
        "*", "a", "**a", "a**", "*/*", "**/*", "file.txt"
    };
    for (const auto& pat : non_all_pass_patterns) {
      CHECK_FALSE(path_pattern::IsAllPassPathPattern(pat));
    }
  }

  // Issue #131: '..' is literal text in a pattern, never path traversal.
  //
  // The matcher is a pure predicate over already-indexed path strings and never
  // touches the filesystem, so a pattern cannot resolve '..' against any root.
  // These checks pin that intent: there is no traversal semantics, and '..' is
  // not a wildcard either. Rejecting patterns that contain '..' is therefore
  // unnecessary — and would break legitimate literal searches for names made of
  // dots.
  TEST_CASE("'..' in a pattern is literal text, never path traversal") {
    // Matches the same text it spells, with either separator style.
    CHECK(PathPatternMatches("a/../b", "a/../b"));
    CHECK(PathPatternMatches("a/../b", "a\\..\\b"));

    // No traversal: the pattern is not resolved, so it never matches the path a
    // filesystem would collapse it to.
    CHECK_FALSE(PathPatternMatches("a/../b", "b"));
    CHECK_FALSE(PathPatternMatches("a/../b", "/b"));

    // '..' is not a wildcard: it does not stand in for two arbitrary characters.
    CHECK_FALSE(PathPatternMatches("a/../b", "a/xy/b"));

    // Standalone '..' matches only itself.
    CHECK(PathPatternMatches("..", ".."));
    CHECK_FALSE(PathPatternMatches("..", "."));

    // Combined with a wildcard, '..' still only selects paths that literally
    // contain those characters.
    CHECK(PathPatternMatches("a/../*", "a/../b"));
    CHECK_FALSE(PathPatternMatches("a/../*", "a/b"));
  }

  TEST_CASE("NFA vs Backtracking 63/64/65-token boundary") {
    // 63 tokens (1 '*' + 62 'a's): fits in 64-bit NFA mask (states 0..63), inline storage
    const std::string pat_63 = "*" + std::string(62, 'a');
    const auto compiled_63 = path_pattern::CompilePathPattern(pat_63, false);
    CHECK(compiled_63.token_count == 63);
    CHECK(compiled_63.overflow_tokens.empty());
    CHECK_FALSE(compiled_63.needs_backtracking);
    CHECK(PathPatternMatches(compiled_63, "xyz" + std::string(62, 'a')));
    CHECK(PathPatternMatches(compiled_63, std::string(62, 'a')));
    CHECK_FALSE(PathPatternMatches(compiled_63, std::string(61, 'a')));
    CHECK_FALSE(PathPatternMatches(compiled_63, "dir/" + std::string(62, 'a')));

    // 64 tokens (1 '*' + 63 'a's): exceeds 63-token NFA limit, routes to backtracking, still inline storage
    const std::string pat_64 = "*" + std::string(63, 'a');
    const auto compiled_64 = path_pattern::CompilePathPattern(pat_64, false);
    CHECK(compiled_64.token_count == 64);
    CHECK(compiled_64.overflow_tokens.empty());
    CHECK(compiled_64.needs_backtracking);
    CHECK(PathPatternMatches(compiled_64, "xyz" + std::string(63, 'a')));
    CHECK(PathPatternMatches(compiled_64, std::string(63, 'a')));
    CHECK_FALSE(PathPatternMatches(compiled_64, std::string(62, 'a')));
    CHECK_FALSE(PathPatternMatches(compiled_64, "dir/" + std::string(63, 'a')));

    // >64 tokens with segment semantics ('**/'): uses overflow_tokens vector
    const std::string pat_70 = "**/" + std::string(67, 'b') + "?";
    const auto compiled_70 = path_pattern::CompilePathPattern(pat_70, false);
    CHECK(compiled_70.token_count == 70);
    CHECK(compiled_70.overflow_tokens.size() == 70);
    CHECK(compiled_70.needs_backtracking);
    CHECK(PathPatternMatches(compiled_70, "a/b/c/" + std::string(67, 'b') + "x"));
    CHECK(PathPatternMatches(compiled_70, std::string(67, 'b') + "z"));
    CHECK_FALSE(PathPatternMatches(compiled_70, std::string(67, 'b')));
  }
}
