#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <string>
#include <string_view>
#include <utility>
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
        path_pattern::MatchOptions::kCaseInsensitive);
    CHECK(compiled.has_required_substring);
    CHECK(compiled.required_substring == "long_literal_directory_name");

    const auto short_pat = path_pattern::CompilePathPattern("*.c");
    CHECK_FALSE(short_pat.has_required_substring);
  }

  TEST_CASE("multi-literal AND prefilter requires every long run") {
    const auto compiled =
        path_pattern::CompilePathPattern("**/*controller*test*.*", path_pattern::MatchOptions::kNone);
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
        "**/USN_Windows**/*.md", path_pattern::MatchOptions::kCaseInsensitive);
    REQUIRE(compiled.IsValid());
    CHECK(compiled.required_substring == "usn_windows");
    CHECK(compiled.has_required_suffix);
    CHECK(compiled.required_suffix == ".md");

    CHECK(PathPatternMatches("**/USN_Windows**/*.md", "/a/USN_Windows/b/notes.md",
                             path_pattern::MatchOptions::kCaseInsensitive));
    // Literal present but wrong ending: rejected without backtracking.
    CHECK_FALSE(PathPatternMatches("**/USN_Windows**/*.md", "/a/USN_Windows/b/notes.txt",
                                   path_pattern::MatchOptions::kCaseInsensitive));
    CHECK_FALSE(PathPatternMatches("**/USN_Windows**/*.md", "/a/other/b/notes.md",
                                   path_pattern::MatchOptions::kCaseInsensitive));
  }

  TEST_CASE("a run before a wildcard is a required literal again") {
    // '*' is not a {0,} quantifier, so "xyz" is mandatory and can be prefiltered.
    const auto compiled =
        path_pattern::CompilePathPattern("[a]xyz*1234", path_pattern::MatchOptions::kNone);
    REQUIRE(compiled.IsValid());
    CHECK(compiled.required_substring == "1234");
    REQUIRE(compiled.extra_required_substrings.size() == 1);
    CHECK(compiled.extra_required_substrings[0] == "xyz");

    CHECK(PathPatternMatches("[a]xyz*1234", "axyzzz1234"));
    CHECK_FALSE(PathPatternMatches("[a]xyz*1234", "axy1234"));
  }

  TEST_CASE("class interior is never a required literal (primary extraction)") {
    // Old code returned "0123456789abcdef" as required_substring, rejecting
    // every valid match. Class members are syntax, not literals.
    const auto compiled =
        path_pattern::CompilePathPattern("**/[0123456789abcdef]/x", path_pattern::MatchOptions::kNone);
    REQUIRE(compiled.IsValid());
    CHECK_FALSE(compiled.has_required_substring);
    CHECK(PathPatternMatches("**/[0123456789abcdef]/x", "d/a/x"));
    CHECK(PathPatternMatches("**/[0123456789abcdef]/x", "d/f/x"));
    CHECK_FALSE(PathPatternMatches("**/[0123456789abcdef]/x", "d/g/x"));
    CHECK_FALSE(PathPatternMatches("**/[0123456789abcdef]/x", "d/a/y"));
  }

  TEST_CASE("optional last char of a run is not required (brace repetition)") {    // 'z{0,2}' can match zero copies, so "xyz" is not guaranteed: "axy1234" matches.
    CHECK(PathPatternMatches("[a]xyz{0,2}1234", "axy1234"));
    CHECK(PathPatternMatches("[a]xyz{0,2}1234", "axyz1234"));
    CHECK(PathPatternMatches("[a]xyz{0,2}1234", "axyzz1234"));
    CHECK_FALSE(PathPatternMatches("[a]xyz{0,2}1234", "axy12"));
    // One-or-more keeps the run: 'z+' guarantees at least one 'z', full run intact.
    CHECK(PathPatternMatches("[a]xyz+1234", "axyzzz1234"));
    CHECK_FALSE(PathPatternMatches("[a]xyz+1234", "axy1234"));
  }

  TEST_CASE("wildcards win over the former quantifier position") {
    // '*' and '?' are always wildcards, even after a quantifiable atom, so the
    // run before them is mandatory: "xyz" is required, and "xy?" needs one more
    // character than "xyz".
    CHECK_FALSE(PathPatternMatches("[a]xyz?1234", "axy1234"));
    CHECK(PathPatternMatches("[a]xyz?1234", "axyzQ1234"));
    CHECK_FALSE(PathPatternMatches("[a]xyz*1234", "axy1234"));
    CHECK(PathPatternMatches("[a]xyz*1234", "axyzzz1234"));

    // The old readings stay available, spelled with braces.
    CHECK(PathPatternMatches("[a]xyz{0,1}1234", "axy1234"));
    CHECK(PathPatternMatches("[a]xyz{0,}1234", "axy1234"));
  }

  TEST_CASE("'*' after a class or shorthand needs one matching character") {
    // Regression: '*' used to be swallowed as a {0,} quantifier, so the class
    // could match zero characters and these matched any ".log" file.
    CHECK(PathPatternMatches("**/[0-9]*.log", "dir/1.log"));
    CHECK(PathPatternMatches("**/[0-9]*.log", "dir/9ab.log"));
    CHECK_FALSE(PathPatternMatches("**/[0-9]*.log", "dir/a.log"));
    CHECK(PathPatternMatches("**/\\d*.log", "dir/1.log"));
    CHECK_FALSE(PathPatternMatches("**/\\d*.log", "dir/a.log"));
  }

  TEST_CASE("wildcard reading of the reported bug: *find*pg[cd]") {
    // 'd*' was "zero or more d", so only a bare "findpgc" matched.
    CHECK(PathPatternMatches("*find*pg[cd]", "findpgc"));
    CHECK(PathPatternMatches("*find*pg[cd]", "afindbpgc"));
    CHECK(PathPatternMatches("*find*pg[cd]", "findxxpgd"));
    // '*' does not cross separators, and the class is the last thing matched.
    CHECK_FALSE(PathPatternMatches("*find*pg[cd]", "x/findypgc"));
    CHECK_FALSE(PathPatternMatches("*find*pg[cd]", "findpgcx"));
  }

  TEST_CASE("advanced pattern exceeding the atom limit is invalid, not truncated") {
    // '[a]' and '[0-9]' compile to one atom each, so a run of n literals
    // between them needs n + 2 atoms.
    const auto at_limit = path_pattern::kMaxPatternAtoms - 2U;  // exactly at the limit
    std::string at_limit_pattern = "[a]" + std::string(at_limit, 'x') + "[0-9]";
    const auto ok = path_pattern::CompilePathPattern(at_limit_pattern, MatchOptions::kNone);
    REQUIRE(ok.IsValid());
    CHECK(PathPatternMatches(ok, "a" + std::string(at_limit, 'x') + "7"));

    // One literal more overflows: report invalid instead of matching only the
    // atoms that fitted.
    const std::string over_limit_pattern = "[a]" + std::string(at_limit + 1U, 'x') + "[0-9]";
    const auto overflowed = path_pattern::CompilePathPattern(over_limit_pattern, MatchOptions::kNone);
    CHECK(overflowed.status == path_pattern::CompileStatus::kTooManyAtoms);
    CHECK_FALSE(PathPatternMatches(overflowed, "a" + std::string(at_limit, 'x') + "7"));
  }

  TEST_CASE("an unterminated character class is reported, not silently dropped") {
    const auto compiled = path_pattern::CompilePathPattern("**/[abc.log", MatchOptions::kNone);
    CHECK(compiled.status == path_pattern::CompileStatus::kUnterminatedClass);
    CHECK_FALSE(PathPatternMatches(compiled, "x/abc.log"));
  }

  TEST_CASE("simple patterns longer than the NFA mask fall back to backtracking") {
    // kMaxSimpleTokens tokens fit the 64-bit NFA state mask; longer patterns used
    // to be rejected outright and matched nothing. They now compile on the
    // backtracking matcher, which uses the same token meanings.
    constexpr std::size_t kLong = path_pattern::kMaxSimpleTokens + 20U;
    const std::string body(kLong, 'a');
    const std::string pattern = "src/" + body + "*.cpp";

    const auto compiled = path_pattern::CompilePathPattern(pattern, MatchOptions::kNone);
    REQUIRE(compiled.IsValid());
    // Same verdict as the short equivalent, which still uses the NFA.
    CHECK(PathPatternMatches(compiled, "src/" + body + "main.cpp"));
    CHECK_FALSE(PathPatternMatches(compiled, "src/" + body + "main.txt"));
    CHECK_FALSE(PathPatternMatches(compiled, "other/" + body + "main.cpp"));
    CHECK_FALSE(PathPatternMatches(compiled, "src/main.cpp"));

    // Wildcards keep their simple meanings on the fallback path: '*' stays
    // inside one segment and '?' stays exactly one character.
    const std::string wildcards = "src/" + body + "/*/x?.cpp";
    const auto wild = path_pattern::CompilePathPattern(wildcards, MatchOptions::kNone);
    REQUIRE(wild.IsValid());
    CHECK(PathPatternMatches(wild, "src/" + body + "/sub/x1.cpp"));
    CHECK_FALSE(PathPatternMatches(wild, "src/" + body + "/a/b/x1.cpp"));
  }

  TEST_CASE("a malformed brace group stays a literal, matching the simple path") {
    // '{' only starts a repetition when it parses as {m}, {m,} or {m,n}.
    // Otherwise the simple compile path and CompileSimplePatternAsBacktracking
    // both treat it as a literal, so the advanced path must agree.
    CHECK(PathPatternMatches("a{x}b", "a{x}b"));
    CHECK_FALSE(PathPatternMatches("a{x}b", "ax}b"));
    CHECK(PathPatternMatches("**/a{x}b", "d/a{x}b"));
    CHECK_FALSE(PathPatternMatches("**/a{x}b", "d/ax}b"));

    // Trailing garbage before '}' is not a repetition either.
    CHECK(PathPatternMatches("a{2,x}b", "a{2,x}b"));
    // Genuine repetition is unaffected.
    CHECK(PathPatternMatches("a{2}b", "aab"));
    CHECK_FALSE(PathPatternMatches("a{2}b", "ab"));
    CHECK(PathPatternMatches("a{2,}b", "aaab"));
    CHECK(PathPatternMatches("a{2,3}b", "aab"));
    CHECK_FALSE(PathPatternMatches("a{2,3}b", "ab"));
  }

  TEST_CASE("a moved-from compiled pattern never matches") {
    path_pattern::CompiledPathPattern original =
        path_pattern::CompilePathPattern("**/*.cpp", MatchOptions::kNone);
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
              path_pattern::CompileStatus::kTooManyAtoms)) != "ok");
    CHECK(std::string_view(path_pattern::DescribeCompileStatus(
              path_pattern::CompileStatus::kUnterminatedClass)) != "ok");
    CHECK(std::string_view(path_pattern::DescribeCompileStatus(
              path_pattern::CompileStatus::kMovedFrom)) != "ok");
  }
}






