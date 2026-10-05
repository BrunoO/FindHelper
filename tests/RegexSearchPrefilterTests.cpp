#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#ifndef _WIN32
#ifndef USN_WINDOWS_TESTS
#define USN_WINDOWS_TESTS
#endif  // USN_WINDOWS_TESTS
#endif  // _WIN32

#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "MockSearchableIndex.h"
#include "TestHelpers.h"
#include "doctest/doctest.h"
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

// Test-only oracle: rs: semantics via RegexMatchStrict (no literal pre-filter).
[[nodiscard]] bool MatchRsStrict(std::string_view rs_query, std::string_view text,
                                 bool case_sensitive) {
    const std::string regex_pattern = search_pattern_utils::ExtractPattern(rs_query);
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
// 1. RegexMatchStrict: unit-level correctness for rs: patterns
//    Tests underlying regex semantics without any pre-filter involved.
// ---------------------------------------------------------------------------
TEST_SUITE("RegexSearchPrefilter - RegexMatchStrict correctness") {

    TEST_CASE("extension anchor patterns") {
        const auto matcher = [](std::string_view text) {
            return MatchRsStrict("rs:\\.pdf$", text, false);
        };
        ExpectExtensionAnchorBehavior(matcher);
    }

    TEST_CASE("start-of-name anchor patterns") {
        const auto matcher = [](std::string_view text) {
            return MatchRsStrict("rs:^report", text, false);
        };
        ExpectPrefixAnchorBehavior(matcher);
    }

    TEST_CASE("mid-name literal with digit class") {
        CHECK(MatchRsStrict("rs:report_\\d{4}", "report_2024.xlsx", false));
        CHECK_FALSE(MatchRsStrict("rs:report_\\d{4}", "report_abcd.xlsx", false));
        CHECK_FALSE(MatchRsStrict("rs:report_\\d{4}", "report_123.xlsx", false)); // 3 digits
    }

    TEST_CASE("compound extension anchor") {
        CHECK(MatchRsStrict("rs:\\.tar\\.gz$", "archive.tar.gz", false));
        CHECK_FALSE(MatchRsStrict("rs:\\.tar\\.gz$", "archive.tar", false));
        CHECK_FALSE(MatchRsStrict("rs:\\.tar\\.gz$", "archive.gz", false));
    }

    TEST_CASE("alternation - both branches must match") {
        const auto matcher = [](std::string_view text) {
            return MatchRsStrict("rs:(\\.pdf|\\.log)$", text, false);
        };
        ExpectAlternationBehavior(matcher);
    }

    TEST_CASE("pure character class - no literal to extract") {
        const auto four_digits_matcher = [](std::string_view text) {
            return MatchRsStrict("rs:\\d{4}", text, false);
        };
        const auto starts_with_digit_matcher = [](std::string_view text) {
            return MatchRsStrict("rs:^\\d", text, false);
        };
        ExpectDigitClassBehavior(four_digits_matcher, starts_with_digit_matcher);
    }

    TEST_CASE("case sensitivity: pre-filter must mirror regex icase behaviour") {
        CHECK(MatchRsStrict("rs:\\.pdf$", "REPORT.PDF", false));
        CHECK(MatchRsStrict("rs:^report", "REPORT_2024.xlsx", false));
        CHECK_FALSE(MatchRsStrict("rs:\\.pdf$", "REPORT.PDF", true));
        CHECK_FALSE(MatchRsStrict("rs:^report", "REPORT_2024.xlsx", true));
        CHECK(MatchRsStrict("rs:\\.PDF$", "REPORT.PDF", true));
        CHECK(MatchRsStrict("rs:^REPORT", "REPORT_2024.xlsx", true));
    }

    TEST_CASE("escaped special characters in pattern") {
        CHECK(MatchRsStrict("rs:file\\(1\\)", "file(1).txt", false));
        CHECK_FALSE(MatchRsStrict("rs:file\\(1\\)", "file1.txt", false));
    }

    TEST_CASE("no-match pattern returns empty results") {
        CHECK_FALSE(MatchRsStrict("rs:^XXXX_NO_SUCH_FILE", "report.pdf", false));
        CHECK_FALSE(MatchRsStrict("rs:^XXXX_NO_SUCH_FILE", "config.json", false));
    }
}

// ---------------------------------------------------------------------------
// 2. Prefilter parity: CreatePatternMatcher must not change rs: semantics
// ---------------------------------------------------------------------------
TEST_SUITE("RegexSearchPrefilter - prefilter parity with RegexMatchStrict") {

    TEST_CASE("extension anchor") {
        const auto strict = [](std::string_view text) { return MatchRsStrict("rs:\\.pdf$", text, false); };
        const auto matcher = search_pattern_utils::CreatePatternMatcher("rs:\\.pdf$", false);
        ExpectPrefilterParityForTexts(strict, matcher,
            {"invoice.pdf", "SUMMARY.PDF", "invoice.pdf.bak", "invoice.docx"});
    }

    TEST_CASE("prefix anchor") {
        const auto strict = [](std::string_view text) { return MatchRsStrict("rs:^report", text, false); };
        const auto matcher = search_pattern_utils::CreatePatternMatcher("rs:^report", false);
        ExpectPrefilterParityForTexts(strict, matcher,
            {"report_2024.xlsx", "REPORT_2024.xlsx", "annual_report.xlsx"});
    }

    TEST_CASE("alternation") {
        const auto strict = [](std::string_view text) {
            return MatchRsStrict("rs:(\\.pdf|\\.log)$", text, false);
        };
        const auto matcher = search_pattern_utils::CreatePatternMatcher("rs:(\\.pdf|\\.log)$", false);
        ExpectPrefilterParityForTexts(strict, matcher,
            {"report.pdf", "error.log", "REPORT.PDF", "report.xlsx"});
    }

    TEST_CASE("digit class and case sensitivity") {
        const auto strict_four = [](std::string_view text) { return MatchRsStrict("rs:\\d{4}", text, false); };
        const auto matcher_four = search_pattern_utils::CreatePatternMatcher("rs:\\d{4}", false);
        ExpectPrefilterParityForTexts(strict_four, matcher_four,
            {"file_2024.txt", "file_123.txt"});

        const auto strict_cs = [](std::string_view text) { return MatchRsStrict("rs:\\.pdf$", text, true); };
        const auto matcher_cs = search_pattern_utils::CreatePatternMatcher("rs:\\.pdf$", true);
        ExpectPrefilterParityForTexts(strict_cs, matcher_cs, {"report.pdf", "REPORT.PDF"});
    }
}

// ---------------------------------------------------------------------------
// 3. CreateFilenameMatcher: callable-level tests
//    After the optimisation this callable will wrap the pre-filter; these
//    tests verify the wrapper produces identical results.
// ---------------------------------------------------------------------------
TEST_SUITE("RegexSearchPrefilter - CreateFilenameMatcher") {

    TEST_CASE("extension anchor") {
        const auto m = search_pattern_utils::CreateFilenameMatcher("rs:\\.pdf$", false);
        ExpectExtensionAnchorBehavior(m);
    }

    TEST_CASE("prefix anchor") {
        const auto m = search_pattern_utils::CreateFilenameMatcher("rs:^report", false);
        ExpectPrefixAnchorBehavior(m);
    }

    TEST_CASE("alternation - no single safe literal") {
        const auto m = search_pattern_utils::CreateFilenameMatcher("rs:(\\.pdf|\\.log)$", false);
        ExpectAlternationBehavior(m);
    }

    TEST_CASE("pure digit class - no literal") {
        const auto mi = search_pattern_utils::CreateFilenameMatcher("rs:\\d{4}", false);
        const auto ms = search_pattern_utils::CreateFilenameMatcher("rs:^\\d", false);
        ExpectDigitClassBehavior(mi, ms);
    }

    TEST_CASE("case-sensitive matcher rejects wrong case") {
        const auto ms = search_pattern_utils::CreateFilenameMatcher("rs:\\.pdf$", true);
        CHECK(ms("report.pdf"));
        CHECK_FALSE(ms("REPORT.PDF")); // uppercase must not match
    }

    TEST_CASE("character class stops prefilter at bracket; prefix still applied") {
        // ExtractRequiredLiteral("file[0-9]+\\.txt") == "file" (4 chars).
        // Prefilter rejects anything not containing "file" before the regex runs.
        const auto m = search_pattern_utils::CreateFilenameMatcher("rs:file[0-9]+\\.txt", false);
        CHECK(m("file5.txt"));           // prefilter "file" passes, regex matches
        CHECK_FALSE(m("fileX.txt"));     // prefilter "file" passes, regex rejects (no digit)
        CHECK_FALSE(m("other5.txt"));    // prefilter "file" rejects immediately
    }

    TEST_CASE("optional plain char: prefix before * or ? is used as prefilter") {
        // "prefix_a?suffix" — prefilter = "prefix_" (stops before optional 'a').
        const auto m = search_pattern_utils::CreateFilenameMatcher("rs:prefix_a?suffix", false);
        CHECK(m("prefix_suffix"));       // 'a' absent (zero occurrences)
        CHECK(m("prefix_asuffix"));      // 'a' present (one occurrence)
        CHECK_FALSE(m("other_suffix"));  // prefilter "prefix_" rejects
    }

    TEST_CASE("literal after closing group is the prefilter candidate") {
        // "(report|invoice)\\.xlsx" — group skipped, ".xlsx" (5 chars) extracted.
        const auto m = search_pattern_utils::CreateFilenameMatcher("rs:(report|invoice)\\.xlsx", false);
        CHECK(m("report.xlsx"));
        CHECK(m("invoice.xlsx"));
        CHECK_FALSE(m("report.pdf"));    // prefilter ".xlsx" rejects immediately
        CHECK_FALSE(m("summary.xlsx"));  // prefilter passes; regex rejects (not report|invoice)
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
        const auto ids = RunRegexSearch(fixture, index, "rs:\\.pdf$", false);
        CHECK(ids == std::set<uint64_t>{2, 3, 4});
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "extension anchor case-sensitive excludes uppercase extension") {
        const auto ids = RunRegexSearch(fixture, index, "rs:\\.pdf$", true);
        CHECK(ids == std::set<uint64_t>{2, 3}); // ANNUAL_REPORT.PDF excluded
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "start anchor case-insensitive") {
        const auto ids = RunRegexSearch(fixture, index, "rs:^report", false);
        CHECK(ids == std::set<uint64_t>{7, 8, 16}); // report_* and REPORT_*
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "start anchor case-sensitive lowercase only") {
        const auto ids = RunRegexSearch(fixture, index, "rs:^report", true);
        CHECK(ids == std::set<uint64_t>{7, 8}); // REPORT_2024_03 excluded
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "mid-name literal with digit class") {
        const auto ids = RunRegexSearch(fixture, index, "rs:report_\\d{4}", false);
        CHECK(ids == std::set<uint64_t>{7, 8, 16});
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "compound extension anchor") {
        const auto ids = RunRegexSearch(fixture, index, "rs:\\.tar\\.gz$", false);
        CHECK(ids == std::set<uint64_t>{9, 10});
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "alternation: both branches returned, no literal pre-filter possible") {
        // rs:(\.pdf|\.log)$ has no single safe literal substring.
        // All pdf AND log files must be returned.
        const auto ids = RunRegexSearch(fixture, index, "rs:(\\.pdf|\\.log)$", false);
        CHECK(ids == std::set<uint64_t>{2, 3, 4, 5, 6}); // pdfs (incl. uppercase) + logs
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "pure digit class: files with 4-digit run") {
        // \d{4} has no extractable literal; pre-filter must pass everything through.
        const auto ids = RunRegexSearch(fixture, index, "rs:\\d{4}", false);
        CHECK(ids == std::set<uint64_t>{2, 7, 8, 16}); // files containing "2024"
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "digit at start-of-name") {
        const auto ids = RunRegexSearch(fixture, index, "rs:^\\d", false);
        CHECK(ids == std::set<uint64_t>{15}); // 123_numbers.txt only
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "substring literal case-insensitive matches both cases") {
        const auto ids = RunRegexSearch(fixture, index, "rs:backup", false);
        CHECK(ids == std::set<uint64_t>{13, 14});
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "substring literal case-sensitive lowercase only") {
        const auto ids = RunRegexSearch(fixture, index, "rs:backup", true);
        CHECK(ids == std::set<uint64_t>{13});
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "substring literal case-sensitive uppercase only") {
        const auto ids = RunRegexSearch(fixture, index, "rs:BACKUP", true);
        CHECK(ids == std::set<uint64_t>{14});
    }

    TEST_CASE_FIXTURE(RegexSearchFixture, "no-match pattern returns empty result set") {
        const auto ids = RunRegexSearch(fixture, index, "rs:^XXXX_NO_MATCH_EVER", false);
        CHECK(ids.empty());
    }
}
