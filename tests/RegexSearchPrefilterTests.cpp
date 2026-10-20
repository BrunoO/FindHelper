
#ifndef _WIN32
#ifndef USN_WINDOWS_TESTS
#define USN_WINDOWS_TESTS
#endif  // USN_WINDOWS_TESTS
#endif  // _WIN32

#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "MockSearchableIndex.h"
#include "TestHelpers.h"
#include "doctest/doctest.h"
#include "path/PathPatternMatcher.h"
#include "search/ParallelSearchEngine.h"
#include "search/SearchContext.h"
#include "search/SearchPatternUtils.h"
#include "utils/StdRegexUtils.h"

// Regression baseline for the regex literal pre-filter optimisation.
//
// These tests document the *exact* match semantics that must be preserved
// after a fast-path literal pre-filter is inserted in front of std::regex.
// The integration suite (SearchAsyncWithData) is the primary regression
// guard: any change that silently drops or adds results will break it.

namespace {

std::set<uint64_t> IdsFromResults(const std::vector<SearchResultData>& results) {
    std::set<uint64_t> ids;
    for (const auto& r : results) {
        ids.insert(r.id);
    }
    return ids;
}

// Thin wrapper: build a SearchContext from a regex query, run a full
// parallel search, and return the matched file IDs.
std::set<uint64_t> RunRegexSearch(
    const test_helpers::TestParallelSearchEngineFixture& fixture,
    const MockSearchableIndex& index,
    std::string_view regex_query,
    bool case_sensitive = false)
{
    const SearchContext ctx = test_helpers::CreateSimpleSearchContext(regex_query, case_sensitive);
    const std::string query_str{regex_query};
    const auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchWithDataAndCollect(
        fixture.GetEngine(), index, query_str, -1, ctx);
    return IdsFromResults(results);
}

// Variant with a path query, so both the filename and path matchers are built.
// These feed the matcher-ordering regression guards: the scan-vs-filter
// differential harness cannot catch a symmetric MatchesPatterns bug, so these
// assert exact, independently-computed ID sets instead.
std::set<uint64_t> RunRegexSearchWithPath(
    const test_helpers::TestParallelSearchEngineFixture& fixture,
    const MockSearchableIndex& index,
    std::string_view regex_query,
    std::string_view path_query,
    bool case_sensitive = false)
{
    const SearchContext ctx =
        test_helpers::CreateSearchContextWithPath(regex_query, path_query, case_sensitive);
    const std::string query_str{regex_query};
    const auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchWithDataAndCollect(
        fixture.GetEngine(), index, query_str, -1, ctx);
    return IdsFromResults(results);
}

template <typename Matcher>
void ExpectExtensionAnchorBehavior(const Matcher& matcher) {
    CHECK(matcher("invoice.pdf"));
    CHECK(matcher("SUMMARY.PDF"));           // icase
    CHECK_FALSE(matcher("invoice.pdf.bak")); // $ anchor after .bak
    CHECK_FALSE(matcher("invoice.docx"));
}

template <typename Matcher>
void ExpectPrefixAnchorBehavior(const Matcher& matcher) {
    CHECK(matcher("report_2024.xlsx"));
    CHECK(matcher("REPORT_2024.xlsx"));      // icase
    CHECK_FALSE(matcher("annual_report.xlsx"));
}

template <typename Matcher>
void ExpectAlternationBehavior(const Matcher& matcher) {
    CHECK(matcher("report.pdf"));
    CHECK(matcher("error.log"));
    CHECK(matcher("REPORT.PDF"));            // icase
    CHECK_FALSE(matcher("report.xlsx"));
}

template <typename FourDigitsMatcher, typename StartsWithDigitMatcher>
void ExpectDigitClassBehavior(const FourDigitsMatcher& four_digits_matcher,
                              const StartsWithDigitMatcher& starts_with_digit_matcher) {
    CHECK(four_digits_matcher("file_2024.txt"));
    CHECK_FALSE(four_digits_matcher("file_123.txt")); // only 3 digits
    CHECK(starts_with_digit_matcher("123_numbers.txt"));
    CHECK_FALSE(starts_with_digit_matcher("abc_123.txt"));
}

// Test-only oracle: re: semantics via RegexMatchStrict (no literal pre-filter).
[[nodiscard]] bool MatchReStrict(std::string_view re_query, std::string_view text,
                                 bool case_sensitive) {
    const std::string regex_pattern = search_pattern_utils::ExtractPattern(re_query);
    if (regex_pattern.empty() || regex_pattern.length() > search_pattern_utils::kMaxPatternLength) {
        return false;
    }
    return std_regex_utils::RegexMatchStrict(regex_pattern, text, case_sensitive);
}

template <typename StrictMatcher, typename PrefilterMatcher>
void ExpectPrefilterParityForTexts(const StrictMatcher& strict_matcher,
                                   const PrefilterMatcher& prefilter_matcher,
                                   const std::vector<std::string_view>& texts) {
    for (const std::string_view text : texts) {
        CHECK(prefilter_matcher(text) == strict_matcher(text));
    }
}

// Rich index covering the pre-filter edge cases:
//   • lowercase vs uppercase extensions (case-sensitivity in pre-filter)
//   • alternation patterns (no single safe literal)
//   • pure character-class patterns (no literal at all)
//   • compound extensions (.tar.gz)
//   • names starting with digits
struct RegexSearchFixture {
    MockSearchableIndex index;
    test_helpers::TestParallelSearchEngineFixture fixture;

    RegexSearchFixture() {
        index.AddEntry(1,  0, "C:\\",                  true,  "C:\\");
        // PDF files: lowercase and uppercase extension (ids 2-4)
        index.AddEntry(2,  1, "invoice_2024.pdf",       false, "C:\\invoice_2024.pdf");
        index.AddEntry(3,  1, "summary.pdf",            false, "C:\\summary.pdf");
        index.AddEntry(4,  1, "ANNUAL_REPORT.PDF",      false, "C:\\ANNUAL_REPORT.PDF");
        // Log files (ids 5-6)
        index.AddEntry(5,  1, "error.log",              false, "C:\\error.log");
        index.AddEntry(6,  1, "debug.log",              false, "C:\\debug.log");
        // Excel reports with 4-digit year (ids 7-8 lowercase, 16 uppercase)
        index.AddEntry(7,  1, "report_2024_01.xlsx",    false, "C:\\report_2024_01.xlsx");
        index.AddEntry(8,  1, "report_2024_02.xlsx",    false, "C:\\report_2024_02.xlsx");
        // Compound extension .tar.gz (ids 9-10)
        index.AddEntry(9,  1, "archive.tar.gz",         false, "C:\\archive.tar.gz");
        index.AddEntry(10, 1, "data.tar.gz",            false, "C:\\data.tar.gz");
        // Other formats (ids 11-12)
        index.AddEntry(11, 1, "config.json",            false, "C:\\config.json");
        index.AddEntry(12, 1, "main.cpp",               false, "C:\\main.cpp");
        // Backup files: lowercase vs uppercase (ids 13-14)
        index.AddEntry(13, 1, "file_backup.bak",        false, "C:\\file_backup.bak");
        index.AddEntry(14, 1, "FILE_BACKUP.BAK",        false, "C:\\FILE_BACKUP.BAK");
        // Name starting with digits (id 15)
        index.AddEntry(15, 1, "123_numbers.txt",        false, "C:\\123_numbers.txt");
        // Uppercase report with year (id 16)
        index.AddEntry(16, 1, "REPORT_2024_03.xlsx",    false, "C:\\REPORT_2024_03.xlsx");
    }
};

} // namespace

// ---------------------------------------------------------------------------
// 1. RegexMatchStrict: unit-level correctness for re: patterns
//    Tests underlying regex semantics without any pre-filter involved.
// ---------------------------------------------------------------------------
TEST_SUITE("RegexSearchPrefilter - RegexMatchStrict correctness") {

    TEST_CASE("extension anchor patterns") {
        const auto matcher = [](std::string_view text) {
            return MatchReStrict("re:\\.pdf$", text, false);
        };
        ExpectExtensionAnchorBehavior(matcher);
    }

    TEST_CASE("start-of-name anchor patterns") {
        const auto matcher = [](std::string_view text) {
            return MatchReStrict("re:^report", text, false);
        };
        ExpectPrefixAnchorBehavior(matcher);
    }

    TEST_CASE("mid-name literal with digit class") {
        CHECK(MatchReStrict("re:report_\\d{4}", "report_2024.xlsx", false));
        CHECK_FALSE(MatchReStrict("re:report_\\d{4}", "report_abcd.xlsx", false));
        CHECK_FALSE(MatchReStrict("re:report_\\d{4}", "report_123.xlsx", false)); // 3 digits
    }

    TEST_CASE("compound extension anchor") {
        CHECK(MatchReStrict("re:\\.tar\\.gz$", "archive.tar.gz", false));
        CHECK_FALSE(MatchReStrict("re:\\.tar\\.gz$", "archive.tar", false));
        CHECK_FALSE(MatchReStrict("re:\\.tar\\.gz$", "archive.gz", false));
    }

    TEST_CASE("alternation - both branches must match") {
        const auto matcher = [](std::string_view text) {
            return MatchReStrict("re:(\\.pdf|\\.log)$", text, false);
        };
        ExpectAlternationBehavior(matcher);
    }

    TEST_CASE("pure character class - no literal to extract") {
        const auto four_digits_matcher = [](std::string_view text) {
            return MatchReStrict("re:\\d{4}", text, false);
        };
        const auto starts_with_digit_matcher = [](std::string_view text) {
            return MatchReStrict("re:^\\d", text, false);
        };
        ExpectDigitClassBehavior(four_digits_matcher, starts_with_digit_matcher);
    }

    TEST_CASE("case sensitivity: pre-filter must mirror regex icase behaviour") {
        CHECK(MatchReStrict("re:\\.pdf$", "REPORT.PDF", false));
        CHECK(MatchReStrict("re:^report", "REPORT_2024.xlsx", false));
        CHECK_FALSE(MatchReStrict("re:\\.pdf$", "REPORT.PDF", true));
        CHECK_FALSE(MatchReStrict("re:^report", "REPORT_2024.xlsx", true));
        CHECK(MatchReStrict("re:\\.PDF$", "REPORT.PDF", true));
        CHECK(MatchReStrict("re:^REPORT", "REPORT_2024.xlsx", true));
    }

    TEST_CASE("escaped special characters in pattern") {
        CHECK(MatchReStrict("re:file\\(1\\)", "file(1).txt", false));
        CHECK_FALSE(MatchReStrict("re:file\\(1\\)", "file1.txt", false));
    }

    TEST_CASE("no-match pattern returns empty results") {
        CHECK_FALSE(MatchReStrict("re:^XXXX_NO_SUCH_FILE", "report.pdf", false));
        CHECK_FALSE(MatchReStrict("re:^XXXX_NO_SUCH_FILE", "config.json", false));
    }
}

// ---------------------------------------------------------------------------
// 2. Prefilter parity: CreatePatternMatcher must not change re: semantics
// ---------------------------------------------------------------------------
TEST_SUITE("RegexSearchPrefilter - prefilter parity with RegexMatchStrict") {

    TEST_CASE("extension anchor") {
        const auto strict = [](std::string_view text) { return MatchReStrict("re:\\.pdf$", text, false); };
        const auto matcher = search_pattern_utils::CreatePatternMatcher("re:\\.pdf$", false);
        ExpectPrefilterParityForTexts(strict, matcher,
            {"invoice.pdf", "SUMMARY.PDF", "invoice.pdf.bak", "invoice.docx"});
    }

    TEST_CASE("prefix anchor") {
        const auto strict = [](std::string_view text) { return MatchReStrict("re:^report", text, false); };
        const auto matcher = search_pattern_utils::CreatePatternMatcher("re:^report", false);
        ExpectPrefilterParityForTexts(strict, matcher,
            {"report_2024.xlsx", "REPORT_2024.xlsx", "annual_report.xlsx"});
    }

    TEST_CASE("alternation") {
        const auto strict = [](std::string_view text) {
            return MatchReStrict("re:(\\.pdf|\\.log)$", text, false);
        };
        const auto matcher = search_pattern_utils::CreatePatternMatcher("re:(\\.pdf|\\.log)$", false);
        ExpectPrefilterParityForTexts(strict, matcher,
            {"report.pdf", "error.log", "REPORT.PDF", "report.xlsx"});
    }

    TEST_CASE("digit class and case sensitivity") {
        const auto strict_four = [](std::string_view text) { return MatchReStrict("re:\\d{4}", text, false); };
        const auto matcher_four = search_pattern_utils::CreatePatternMatcher("re:\\d{4}", false);
        ExpectPrefilterParityForTexts(strict_four, matcher_four,
            {"file_2024.txt", "file_123.txt"});

        const auto strict_cs = [](std::string_view text) { return MatchReStrict("re:\\.pdf$", text, true); };
        const auto matcher_cs = search_pattern_utils::CreatePatternMatcher("re:\\.pdf$", true);
        ExpectPrefilterParityForTexts(strict_cs, matcher_cs, {"report.pdf", "REPORT.PDF"});
    }

    // Lever B guards: suffix extraction must not change semantics for anchored
    // literal, compound, or alternation suffixes. Parity is against the plain
    // regex oracle (RegexMatchStrict), which must stay prefilter-free.
    TEST_CASE("anchored literal suffix") {
        const auto strict = [](std::string_view text) { return MatchReStrict("re:\\.gz$", text, false); };
        const auto matcher = search_pattern_utils::CreatePatternMatcher("re:\\.gz$", false);
        ExpectPrefilterParityForTexts(strict, matcher,
            {"archive.gz", "ARCHIVE.GZ", "archive.tar.gz", "archive.tar", "notes.txt"});
    }

    TEST_CASE("compound suffix") {
        const auto strict = [](std::string_view text) { return MatchReStrict("re:\\.tar\\.gz$", text, false); };
        const auto matcher = search_pattern_utils::CreatePatternMatcher("re:\\.tar\\.gz$", false);
        ExpectPrefilterParityForTexts(strict, matcher,
            {"archive.tar.gz", "ARCHIVE.TAR.GZ", "archive.tar", "archive.gz", "archive.tar.gz.bak"});
    }

    TEST_CASE("alternation suffix set - every branch") {
        const auto strict = [](std::string_view text) {
            return MatchReStrict("re:(\\.cpp|\\.h)$", text, false);
        };
        const auto matcher = search_pattern_utils::CreatePatternMatcher("re:(\\.cpp|\\.h)$", false);
        // Only the 2nd alternative matches for some texts; the prefilter must not
        // assume the first alternative's literal.
        ExpectPrefilterParityForTexts(strict, matcher,
            {"main.cpp", "MAIN.CPP", "main.h", "MAIN.H", "main.md", "main.cpp.bak", "h"});
    }

    TEST_CASE("no-literal alternation suffix") {
        const auto strict = [](std::string_view text) { return MatchReStrict("re:.*\\.(pdf|log)$", text, false); };
        const auto matcher = search_pattern_utils::CreatePatternMatcher("re:.*\\.(pdf|log)$", false);
        ExpectPrefilterParityForTexts(strict, matcher,
            {"report.pdf", "REPORT.LOG", "error.log", "report.txt", "report.pdf.bak"});
    }
}

// ---------------------------------------------------------------------------
// 3. CreateFilenameMatcher / CreatePathMatcher: alias identity only.
//    Both are one-line forwarders to CreatePatternMatcher with no
//    filename/path-specific behaviour, so identity with the canonical factory
//    is their whole contract. Their *behaviour* is pinned by suites 1, 2 and 4
//    (oracle parity + integration), which is strictly stronger than the
//    hand-written expectations this suite used to restate.
// ---------------------------------------------------------------------------
TEST_SUITE("RegexSearchPrefilter - filename/path factory aliases") {

    TEST_CASE("filename/path factories are CreatePatternMatcher aliases") {
        for (const std::string_view pattern :
             {"re:\\.pdf$", "re:^report", "fz:abc", "pp:**/*.cpp"}) {
            CAPTURE(pattern);
            const auto canonical = search_pattern_utils::CreatePatternMatcher(pattern, false);
            const auto filename = search_pattern_utils::CreateFilenameMatcher(pattern, false);
            const auto path = search_pattern_utils::CreatePathMatcher(pattern, false);
            CHECK(std::string(search_pattern_utils::MatcherLabel(filename)) ==
                  search_pattern_utils::MatcherLabel(canonical));
            CHECK(std::string(search_pattern_utils::MatcherLabel(path)) ==
                  search_pattern_utils::MatcherLabel(canonical));
            CHECK(filename("report_2024.xlsx") == canonical("report_2024.xlsx"));
            CHECK(path("C:\\report_2024.xlsx") == canonical("C:\\report_2024.xlsx"));
        }

        // The pre-compiled PathPattern overloads are aliases of the same factory.
        const auto compiled = std::make_shared<path_pattern::CompiledPathPattern>(
            path_pattern::CompilePathPattern("**/*.cpp", path_pattern::MatchOptions::kNone));
        const auto canonical_compiled = search_pattern_utils::CreatePatternMatcher(compiled);
        const auto filename_compiled = search_pattern_utils::CreateFilenameMatcher(compiled);
        const auto path_compiled = search_pattern_utils::CreatePathMatcher(compiled);
        CHECK(std::string(search_pattern_utils::MatcherLabel(filename_compiled)) ==
              search_pattern_utils::MatcherLabel(canonical_compiled));
        CHECK(std::string(search_pattern_utils::MatcherLabel(path_compiled)) ==
              search_pattern_utils::MatcherLabel(canonical_compiled));
    }
}

// ---------------------------------------------------------------------------
// 4. SearchAsyncWithData integration: full parallel search pipeline
//    These tests are the primary regression guard.  Any pre-filter that
//    silently drops or adds results will cause an assertion failure here.
// ---------------------------------------------------------------------------
TEST_SUITE("RegexSearchPrefilter - SearchAsyncWithData integration") {

    TEST_CASE_FIXTURE(RegexSearchFixture, "extension anchor matches lowercase and uppercase") {
        // CRITICAL: case-insensitive pre-filter must not reject ANNUAL_REPORT.PDF (id 4).
        const auto ids = RunRegexSearch(fixture, index, "re:\\.pdf$", false);
        CHECK(ids == std::set<uint64_t>{2, 3, 4});
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "extension anchor case-sensitive excludes uppercase extension") {
        const auto ids = RunRegexSearch(fixture, index, "re:\\.pdf$", true);
        CHECK(ids == std::set<uint64_t>{2, 3}); // ANNUAL_REPORT.PDF excluded
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "start anchor case-insensitive") {
        const auto ids = RunRegexSearch(fixture, index, "re:^report", false);
        CHECK(ids == std::set<uint64_t>{7, 8, 16}); // report_* and REPORT_*
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "start anchor case-sensitive lowercase only") {
        const auto ids = RunRegexSearch(fixture, index, "re:^report", true);
        CHECK(ids == std::set<uint64_t>{7, 8}); // REPORT_2024_03 excluded
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "mid-name literal with digit class") {
        const auto ids = RunRegexSearch(fixture, index, "re:report_\\d{4}", false);
        CHECK(ids == std::set<uint64_t>{7, 8, 16});
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "compound extension anchor") {
        const auto ids = RunRegexSearch(fixture, index, "re:\\.tar\\.gz$", false);
        CHECK(ids == std::set<uint64_t>{9, 10});
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "alternation: both branches returned, no literal pre-filter possible") {
        // re:(\.pdf|\.log)$ has no single safe literal substring.
        // All pdf AND log files must be returned.
        const auto ids = RunRegexSearch(fixture, index, "re:(\\.pdf|\\.log)$", false);
        CHECK(ids == std::set<uint64_t>{2, 3, 4, 5, 6}); // pdfs (incl. uppercase) + logs
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "pure digit class: files with 4-digit run") {
        // \d{4} has no extractable literal; pre-filter must pass everything through.
        const auto ids = RunRegexSearch(fixture, index, "re:\\d{4}", false);
        CHECK(ids == std::set<uint64_t>{2, 7, 8, 16}); // files containing "2024"
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "digit at start-of-name") {
        const auto ids = RunRegexSearch(fixture, index, "re:^\\d", false);
        CHECK(ids == std::set<uint64_t>{15}); // 123_numbers.txt only
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "substring literal case-insensitive matches both cases") {
        const auto ids = RunRegexSearch(fixture, index, "re:backup", false);
        CHECK(ids == std::set<uint64_t>{13, 14});
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "substring literal case-sensitive lowercase only") {
        const auto ids = RunRegexSearch(fixture, index, "re:backup", true);
        CHECK(ids == std::set<uint64_t>{13});
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "substring literal case-sensitive uppercase only") {
        const auto ids = RunRegexSearch(fixture, index, "re:BACKUP", true);
        CHECK(ids == std::set<uint64_t>{14});
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "no-match pattern returns empty result set") {
        const auto ids = RunRegexSearch(fixture, index, "re:^XXXX_NO_MATCH_EVER", false);
        CHECK(ids.empty());
    }
}

// ---------------------------------------------------------------------------
// 5. Combined filename + path matchers: independent exact-ID oracles.
//    The scan-vs-filter differential harness runs MatchesPatterns on both sides,
//    so it cannot detect a symmetric ordering bug. These assert hand-computed ID
//    sets and are the primary regression guard for matcher-ordering changes.
// ---------------------------------------------------------------------------
TEST_SUITE("RegexSearchPrefilter - combined filename and path matchers") {

    TEST_CASE_FIXTURE(RegexSearchFixture, "filename substring + path substring both applied") {
        const auto ids = RunRegexSearchWithPath(fixture, index, "report", "xlsx", false);
        CHECK(ids == std::set<uint64_t>{7, 8, 16});
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "regex filename + path substring") {
        const auto ids = RunRegexSearchWithPath(fixture, index, "re:report", "xlsx", false);
        CHECK(ids == std::set<uint64_t>{7, 8, 16});
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "alternation suffix regex + broad path") {
        const auto ids = RunRegexSearchWithPath(fixture, index, R"(re:(\.pdf|\.log)$)", "C:\\", false);
        CHECK(ids == std::set<uint64_t>{2, 3, 4, 5, 6});
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "both matchers selective and disjoint yields empty") {
        const auto ids = RunRegexSearchWithPath(fixture, index, "log", "backup", false);
        CHECK(ids.empty());
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "regex filename with path that matches nothing yields empty") {
        // The 124 ms production case: the expensive filename regex must still return
        // the correct (empty) set when the path filter excludes every row.
        const auto ids = RunRegexSearchWithPath(fixture, index, R"(re:.*\.(cpp|h)$)", "ZZZ_no_such_path", false);
        CHECK(ids.empty());
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "filename substring with non-matching path yields empty") {
        const auto ids = RunRegexSearchWithPath(fixture, index, "backup", "pdf", false);
        CHECK(ids.empty());
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "short filename + path intersect correctly") {
        const auto ids = RunRegexSearchWithPath(fixture, index, "123", "txt", false);
        CHECK(ids == std::set<uint64_t>{15});
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "case sensitivity propagates to both matchers") {
        // 'report' CS matches lowercase report_* only; REPORT_2024_03.xlsx (id 16) is excluded.
        const auto ids = RunRegexSearchWithPath(fixture, index, "report", "xlsx", true);
        CHECK(ids == std::set<uint64_t>{7, 8});
    }

    // Lever A: an unfiltered regex filename (no literal, no suffix prefilter) means
    // the path matcher is evaluated first. The result set must be unchanged.
    TEST_CASE_FIXTURE(RegexSearchFixture, "unfiltered regex filename + selective path intersect") {
        const auto ids = RunRegexSearchWithPath(fixture, index, "re:\\d{4}", "xlsx", false);
        CHECK(ids == std::set<uint64_t>{7, 8, 16});
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "unfiltered regex filename + path matching nothing yields empty") {
        const auto ids = RunRegexSearchWithPath(fixture, index, "re:\\d{4}", "ZZZ_no_such_path", false);
        CHECK(ids.empty());
    }
}

// ---------------------------------------------------------------------------
// 6. Extension-set fast path (re:.*\.(ext|...)$) is routed to an
//    ExtensionSetMatcher. Pin the exact '$' / case semantics against the
//    plain-regex oracle, including trailing-newline edge texts.
// ---------------------------------------------------------------------------
TEST_SUITE("RegexSearchPrefilter - extension fast path parity") {

    TEST_CASE("engine '$' agrees with regex_traits") {
        // The extension fast path decides without running the regex, so it
        // trusts regex_traits::kDollarIsLineOriented. Pin that constant against
        // the engine this build actually linked: swapping engines or adding
        // regex_constants::multiline then fails here instead of silently
        // changing which rows a query returns.
        const bool line_oriented = regex_traits::kDollarIsLineOriented;
        CHECK(MatchReStrict("re:\\.cpp$", "x.cpp", false));  // end of input, both engines
        CHECK(MatchReStrict("re:\\.cpp$", "x.cpp\n", false) == line_oriented);
        CHECK(MatchReStrict("re:\\.cpp$", "x.cpp\r\n", false) == line_oriented);
        // Line-oriented means before *any* line terminator, not only a final one.
        CHECK(MatchReStrict("re:\\.cpp$", "x.cpp\nX", false) == line_oriented);
        CHECK(MatchReStrict("re:\\.cpp$", "x.cpp\n\n", false) == line_oriented);
        // Without a line terminator both engines anchor at the end of the input.
        CHECK_FALSE(MatchReStrict("re:\\.cpp$", "x.cpp.", false));
        CHECK_FALSE(MatchReStrict("re:\\.cpp$", "x.cppx", false));
    }

    TEST_CASE("anchored suffix edge texts") {
        const auto strict = [](std::string_view text) { return MatchReStrict("re:\\.cpp$", text, false); };
        const auto matcher = search_pattern_utils::CreatePatternMatcher("re:\\.cpp$", false);
        ExpectPrefilterParityForTexts(strict, matcher,
            {"x.cpp", "x.CPP", "X.CPP", "x.cpp\n", "x.cpp\r\n", "x.cpp\n\n", "x.cpp\nX",
             "x.cpp.", "x.cppx", ".cpp", "cpp", "a.b.cpp"});
    }

    TEST_CASE("alternation edge texts") {
        const auto strict = [](std::string_view text) { return MatchReStrict("re:.*\\.(cpp|h)$", text, false); };
        const auto matcher = search_pattern_utils::CreatePatternMatcher("re:.*\\.(cpp|h)$", false);
        ExpectPrefilterParityForTexts(strict, matcher,
            {"a.cpp", "a.h", "a.H", "a.hpp", "a.cpp\n", "a.h\r\n", "a.cpp\n\n", "a.h\nX",
             "a.ch", "h", "a.cpp.bak"});
    }

    TEST_CASE("case-sensitive extension") {
        const auto strict = [](std::string_view text) { return MatchReStrict("re:\\.cpp$", text, true); };
        const auto matcher = search_pattern_utils::CreatePatternMatcher("re:\\.cpp$", true);
        ExpectPrefilterParityForTexts(strict, matcher, {"x.cpp", "x.CPP", "X.cpp", "x.cPP"});
    }

    TEST_CASE("classify reports Extension only for exact shapes") {
        CHECK(search_pattern_utils::ClassifyMatcherKind("re:.*\\.(cpp|h)$") ==
              search_pattern_utils::MatcherKind::Extension);
        CHECK(search_pattern_utils::ClassifyMatcherKind("re:\\.pdf$") ==
              search_pattern_utils::MatcherKind::Extension);
        CHECK(search_pattern_utils::ClassifyMatcherKind("re:^report") ==
              search_pattern_utils::MatcherKind::StdRegex);
        CHECK(search_pattern_utils::ClassifyMatcherKind("re:.*\\.tar\\.gz$") ==
              search_pattern_utils::MatcherKind::StdRegex);
        CHECK(search_pattern_utils::ClassifyMatcherKind("re:(report|invoice)\\.xlsx") ==
              search_pattern_utils::MatcherKind::StdRegex);
    }
}

// ---------------------------------------------------------------------------
// 7. Prefix prefilter for start-anchored patterns. Pin the semantics against
//    the plain-regex oracle, including alternation prefixes and bail cases.
// ---------------------------------------------------------------------------
TEST_SUITE("RegexSearchPrefilter - prefix prefilter parity") {

    TEST_CASE("literal prefix") {
        const auto strict = [](std::string_view text) { return MatchReStrict("re:^Test_", text, false); };
        const auto matcher = search_pattern_utils::CreatePatternMatcher("re:^Test_", false);
        ExpectPrefilterParityForTexts(strict, matcher,
            {"Test_a", "test_a", "TEST_A", "xTest_a", "Test", "Tes", "\nTest_a", "Test_2024"});
    }

    TEST_CASE("alternation prefix") {
        const auto strict = [](std::string_view text) { return MatchReStrict("re:^(get|set)_", text, false); };
        const auto matcher = search_pattern_utils::CreatePatternMatcher("re:^(get|set)_", false);
        ExpectPrefilterParityForTexts(strict, matcher,
            {"get_x", "set_x", "GET_X", "Set_x", "bet_x", "ge", "get", "xget_x", "set"});
    }

    TEST_CASE("optional or repeated leading group stays regex-correct") {
        const auto optional = [](std::string_view text) { return MatchReStrict("re:^(get|set)?_x", text, false); };
        const auto optional_matcher = search_pattern_utils::CreatePatternMatcher("re:^(get|set)?_x", false);
        ExpectPrefilterParityForTexts(optional, optional_matcher, {"_x", "get_x", "set_x", "get_x2", "x_x"});

        const auto plus = [](std::string_view text) { return MatchReStrict("re:^(get|set)+_x", text, false); };
        const auto plus_matcher = search_pattern_utils::CreatePatternMatcher("re:^(get|set)+_x", false);
        ExpectPrefilterParityForTexts(plus, plus_matcher, {"get_x", "getget_x", "set_x", "_x", "x"});
    }

    TEST_CASE("quantified leading run") {
        const auto strict = [](std::string_view text) { return MatchReStrict("re:^ab+c", text, false); };
        const auto matcher = search_pattern_utils::CreatePatternMatcher("re:^ab+c", false);
        ExpectPrefilterParityForTexts(strict, matcher, {"abc", "abbc", "abbbc", "ac", "xabc", "ab"});
    }

    TEST_CASE("start and end anchored") {
        const auto strict = [](std::string_view text) { return MatchReStrict("re:^abc.*\\.txt$", text, false); };
        const auto matcher = search_pattern_utils::CreatePatternMatcher("re:^abc.*\\.txt$", false);
        ExpectPrefilterParityForTexts(strict, matcher,
            {"abc.txt", "abc_x.txt", "xabc.txt", "abc.txt.bak", "abc", "abc.txt\n"});

        // Single literal between the anchors (no `.*`): the prefix and suffix
        // pre-filters must both stay exact.
        const auto strict_literal = [](std::string_view text) {
            return MatchReStrict("re:^\\.cpp$", text, false);
        };
        const auto matcher_literal =
            search_pattern_utils::CreatePatternMatcher("re:^\\.cpp$", false);
        ExpectPrefilterParityForTexts(strict_literal, matcher_literal,
                                      {"x.cpp", ".cpp", "a.cpp", "cpp"});
    }

    TEST_CASE("case-sensitive prefix") {
        const auto strict = [](std::string_view text) { return MatchReStrict("re:^Get_", text, true); };
        const auto matcher = search_pattern_utils::CreatePatternMatcher("re:^Get_", true);
        ExpectPrefilterParityForTexts(strict, matcher, {"Get_x", "get_x", "GET_x", "Get_", "XGet_x"});
    }
}
