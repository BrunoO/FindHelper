#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "utils/StringSearch.h"
#include "doctest/doctest.h"
#include <string>
#include <string_view>
#include <vector>

TEST_SUITE("StringSearch") {

  TEST_CASE("ToLowerChar") {
    CHECK(string_search::ToLowerChar('A') == 'a');
    CHECK(string_search::ToLowerChar('z') == 'z');
    CHECK(string_search::ToLowerChar('0') == '0');
    CHECK(string_search::ToLowerChar('!') == '!');
  }

  struct SubstringTestCase {
    std::string_view text;
    std::string_view pattern;
    bool expected;
  };

  TEST_CASE("ContainsSubstring") {
    std::vector<SubstringTestCase> test_cases = {
        {"hello", "", true},
        {"", "", true},
        {"hello world", "hello", true},
        {"hello world", "lo wo", true},
        {"hello world", "world", true},
        {"hello world", "xyz", false},
        {"", "pattern", false},
        {"short", "very long pattern", false},
        {"exact", "exact", true},
        {"Hello", "hello", false}, // Case sensitive
        {"path/to/file.txt", "/", true},
        {"C:\\Users\\Test", "\\", true},};

    for (const auto &tc : test_cases) {
      DOCTEST_SUBCASE(
          (std::string(tc.pattern) + " in " + std::string(tc.text)).c_str()) {
        CHECK(string_search::ContainsSubstring(tc.text, tc.pattern) ==
              tc.expected);
      }
    }
  }

  TEST_CASE("ContainsSubstringI") {
    std::vector<SubstringTestCase> test_cases = {
        {"Hello World", "hello", true}, {"Hello World", "WORLD", true},
        {"Hello World", "Lo Wo", true}, {"HELLO", "hello", true},
        {"hello world", "HeLLo", true}, {"TEST", "test", true},
        {"Exact", "exact", true},       {"EXACT", "exact", true},
        {"exact", "EXACT", true},};

    for (const auto &tc : test_cases) {
      DOCTEST_SUBCASE(
          (std::string(tc.pattern) + " in " + std::string(tc.text)).c_str()) {
        CHECK(string_search::ContainsSubstringI(tc.text, tc.pattern) ==
              tc.expected);
      }
    }
  }

  TEST_CASE("StrStrCaseInsensitive") {
    const char *haystack = "Hello World";

    CHECK(string_search::StrStrCaseInsensitive(haystack, "hello") == haystack);
    CHECK(string_search::StrStrCaseInsensitive(haystack, "lo wo") ==
          haystack + 3);
    CHECK(string_search::StrStrCaseInsensitive(haystack, "WORLD") ==
          haystack + 6);
    CHECK(string_search::StrStrCaseInsensitive(haystack, "xyz") == nullptr);
    CHECK(string_search::StrStrCaseInsensitive(haystack, "") == haystack);
    CHECK(string_search::StrStrCaseInsensitive(nullptr, "pattern") == nullptr);
  }

  TEST_CASE("Long strings and AVX2") {
    std::string long_text(100, 'A');
    long_text.replace(50, 10, "TestPattern");

    CHECK(string_search::ContainsSubstring(long_text, "TestPattern"));
    CHECK(string_search::ContainsSubstringI(long_text, "testpattern"));

    std::string block32(64, 'a');
    block32.replace(30, 4, "TEST");
    CHECK(string_search::ContainsSubstringI(block32, "test"));
  }

  TEST_CASE("StrStrCaseInsensitive - SIMD threshold: not found does not fall through to scalar") {
    // 64-byte ASCII haystack — meets both AVX2 (>=32) and NEON (>=16) thresholds.
    // Needle is 4 chars so it clears the SIMD needle-length gate.
    // Before the fix, SIMD returned nullptr ("not found") but the caller fell through
    // to the scalar tolower loop and re-scanned the whole haystack redundantly.
    const std::string haystack(64, 'a');
    CHECK(string_search::StrStrCaseInsensitive(haystack.c_str(), "xyzw") == nullptr);
  }

  TEST_CASE("StrStrCaseInsensitive - SIMD threshold: found returns correct pointer") {
    // Same threshold conditions — verifies the optional-wrapped pointer is correctly
    // dereferenced and points to the first match position.
    const std::string haystack = std::string(30, 'a') + "XYZW" + std::string(30, 'a');
    const char* const ptr = string_search::StrStrCaseInsensitive(haystack.c_str(), "xyzw");
    REQUIRE(ptr != nullptr);
    CHECK(ptr == haystack.c_str() + 30);
  }

  TEST_CASE("SimdSubstringHints matches default ContainsSubstring behavior") {
    const std::string long_match_text =
        std::string(80, 'a') + "Target" + std::string(80, 'b');
    const std::string long_miss_text(80, 'x');
    const std::vector<SubstringTestCase> cases = {
        {long_match_text, "Target", true},
        {long_miss_text, "nomatch", false},
    };
    for (const auto& tc : cases) {
      const string_search::SimdSubstringHints hints =
          string_search::MakeSimdSubstringHints(tc.pattern);
      CHECK(string_search::ContainsSubstring(tc.text, tc.pattern, hints) == tc.expected);
      CHECK(string_search::ContainsSubstring(tc.text, tc.pattern) == tc.expected);
    }
  }

  TEST_CASE("SimdSubstringHints matches default ContainsSubstringI behavior") {
    std::string text(80, 'a');
    text += "FindMe";
    text += std::string(80, 'b');
    const std::string pattern = "findme";
    const string_search::SimdSubstringHints hints =
        string_search::MakeSimdSubstringHints(pattern);
    CHECK(string_search::ContainsSubstringI(text, pattern, hints));
    CHECK(string_search::ContainsSubstringI(text, pattern));
  }

  TEST_CASE("case-sensitive SIMD tolerates non-ASCII haystack bytes") {
    constexpr int kPad = 50;
    std::string text(kPad, 'a');
    text[kPad / 2] = static_cast<char>(0x80);
    text += "UniqueToken";
    text += std::string(kPad, 'z');
    const string_search::SimdSubstringHints hints =
        string_search::MakeSimdSubstringHints("UniqueToken");
    CHECK(string_search::ContainsSubstring(text, "UniqueToken"));
    CHECK(string_search::ContainsSubstring(text, "UniqueToken", hints));
  }

  TEST_CASE("case-insensitive still matches non-ASCII haystack via scalar") {
    constexpr int kPad = 50;
    std::string text(kPad, 'a');
    text[kPad / 2] = static_cast<char>(0x80);
    text += "pattern";
    CHECK(string_search::ContainsSubstringI(text, "pattern"));
  }

  TEST_CASE("MakeSimdSubstringHints short pattern disables SIMD") {
    const string_search::SimdSubstringHints hints =
        string_search::MakeSimdSubstringHints("abc");
    CHECK_FALSE(hints.pattern_is_ascii);
    CHECK_FALSE(hints.use_avx2);
    CHECK_FALSE(hints.use_neon);
  }

  TEST_CASE("MakeSimdSubstringHints non-ASCII pattern disables case-insensitive SIMD") {
    const std::string pattern = std::string("test") + static_cast<char>(0xFF);
    const string_search::SimdSubstringHints hints =
        string_search::MakeSimdSubstringHints(pattern);
    CHECK_FALSE(hints.pattern_is_ascii);
  }

  TEST_CASE("case-insensitive SIMD requires full ASCII haystack not just prefix") {
    constexpr int kPad = 70;
    std::string text(kPad, 'a');
    text[kPad - 1] = static_cast<char>(0x80);
    text += "pattern";
    CHECK(string_search::ContainsSubstringI(text, "pattern"));
  }

  TEST_CASE("IsViewAscii rejects non-ASCII after byte 64") {
    std::string text(80, 'a');
    text[70] = static_cast<char>(0xC3);
    CHECK_FALSE(string_search::string_search_detail::IsViewAscii(text));
    CHECK(string_search::string_search_detail::IsViewAsciiScalar(std::string(80, 'a')));
  }

  // Regression baseline for removing the per-item IsViewAscii(haystack) gate in
  // TryAVX2/NEONPathCaseInsensitive (StringSearch.h). These record the scalar-correct
  // outputs under the CURRENT code (non-ASCII haystacks fall back to scalar) so the
  // SIMD fast-path change must reproduce them byte-identically. Production code
  // is intentionally untouched by this commit.
  TEST_CASE("AsciiPatternVsNonAsciiHaystackLongMatch") {
    constexpr int kPad = 50;
    std::string text(kPad, 'a');
    text += static_cast<char>(0x80);
    text += std::string(kPad, 'b');
    text += "pattern";
    text += std::string(kPad, 'c');
    const std::string pattern = "pattern";
    const string_search::SimdSubstringHints hints =
        string_search::MakeSimdSubstringHints(pattern);
    CHECK(hints.pattern_is_ascii);
    CHECK(string_search::ContainsSubstringI(text, pattern));
    CHECK(string_search::ContainsSubstringI(text, pattern, hints));
    CHECK(string_search::ContainsSubstringI(text, pattern, hints) ==
          string_search::ContainsSubstringI(text, pattern));
  }

  TEST_CASE("AsciiPatternVsNonAsciiHaystackLongNoMatch") {
    std::string text(40, 'a');
    text += "C:/donn\xC3\xA9" "es/\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E/";
    text += std::string(40, 'b');
    const std::string pattern = "nomatch1234";
    const string_search::SimdSubstringHints hints =
        string_search::MakeSimdSubstringHints(pattern);
    CHECK(hints.pattern_is_ascii);
    CHECK_FALSE(string_search::ContainsSubstringI(text, pattern));
    CHECK_FALSE(string_search::ContainsSubstringI(text, pattern, hints));
  }

  TEST_CASE("NonAsciiPatternUsesScalarPath") {
    const std::string pattern = "caf\xC3\xA9";
    const string_search::SimdSubstringHints hints =
        string_search::MakeSimdSubstringHints(pattern);
    CHECK_FALSE(hints.pattern_is_ascii);
    std::string hit(40, 'a');
    hit += "caf\xC3\xA9";
    hit += std::string(40, 'b');
    CHECK(string_search::ContainsSubstringI(hit, pattern));
    CHECK(string_search::ContainsSubstringI(hit, pattern, hints));
    const std::string miss(90, 'x');
    CHECK_FALSE(string_search::ContainsSubstringI(miss, pattern));
    CHECK_FALSE(string_search::ContainsSubstringI(miss, pattern, hints));
  }

  TEST_CASE("CaseFoldingWithNonAsciiAroundMatch") {
    std::string text(30, 'x');
    text += "CAF\xC3\x89_REPORT.TXT";
    text += std::string(30, 'y');
    CHECK(string_search::ContainsSubstringI(text, "report"));
    const string_search::SimdSubstringHints hints =
        string_search::MakeSimdSubstringHints("report");
    CHECK(string_search::ContainsSubstringI(text, "report", hints));
    CHECK(string_search::ContainsSubstringI(text, "REPORT", hints));
    CHECK_FALSE(string_search::ContainsSubstringI(text, "nomatch99"));
  }

  TEST_CASE("SimdBoundaryLengthsHintAgreesWithDefault") {
    const std::string anchor = "pattern_anchor";
    const std::string absent = "zzzz_qqqq";
    const std::vector<size_t> pads = {15, 16, 17, 31, 32, 33, 64};
    for (const size_t pad : pads) {
      DOCTEST_SUBCASE(std::string("pad=" + std::to_string(pad)).c_str()) {
        std::string text(pad, 'a');
        text += "PATTERN_ANCHOR";
        text += std::string(pad, 'b');
        const string_search::SimdSubstringHints hit_hints =
            string_search::MakeSimdSubstringHints(anchor);
        CHECK(string_search::ContainsSubstringI(text, anchor));
        CHECK(string_search::ContainsSubstringI(text, anchor, hit_hints));
        const string_search::SimdSubstringHints miss_hints =
            string_search::MakeSimdSubstringHints(absent);
        CHECK_FALSE(string_search::ContainsSubstringI(text, absent));
        CHECK_FALSE(string_search::ContainsSubstringI(text, absent, miss_hints));
      }
    }
  }

  TEST_CASE("ShortPatternLengthsHintAgreesWithDefault") {
    const std::string text(70, 'a');
    for (const std::string& pattern : {"abc", "abcd", "abcde"}) {
      DOCTEST_SUBCASE(pattern.c_str()) {
        const string_search::SimdSubstringHints hints =
            string_search::MakeSimdSubstringHints(pattern);
        CHECK(string_search::ContainsSubstringI(text, pattern, hints) ==
              string_search::ContainsSubstringI(text, pattern));
      }
    }
  }

  TEST_CASE("EmojiCyrillicChunkEdgeNoOverread") {
    // 4-byte emoji placed so its bytes straddle the 32-byte AVX2 chunk edge.
    std::string text(30, 'a');
    text += "\xF0\x9F\x98\x80";
    text += "pattern";
    text += std::string(40, 'b');
    CHECK(string_search::ContainsSubstringI(text, "pattern"));
    const string_search::SimdSubstringHints hints =
        string_search::MakeSimdSubstringHints("pattern");
    CHECK(string_search::ContainsSubstringI(text, "pattern", hints));
    CHECK_FALSE(string_search::ContainsSubstringI(text, "nomatch99"));
    // Cyrillic (2-byte UTF-8) haystack with ASCII pattern present.
    std::string cyrillic(20, 'm');
    cyrillic += "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
    cyrillic += std::string(20, 'n');
    cyrillic += "token42";
    cyrillic += std::string(20, 'o');
    CHECK(string_search::ContainsSubstringI(cyrillic, "token42"));
    CHECK(string_search::ContainsSubstringI(
        cyrillic, "token42",
        string_search::MakeSimdSubstringHints("token42")));
  }

  TEST_CASE("DifferentialHintsVsDefaultNonAsciiCorpus") {
    const std::vector<std::pair<std::string, std::string>> corpus = {
        {std::string(50, 'a') + "\xC3\xA9" + std::string(50, 'b') + "pattern", "pattern"},
        {std::string(40, 'a') + "\xE6\x97\xA5" + std::string(40, 'b'), "nomatch1234"},
        {"caf\xC3\xA9" + std::string(80, 'x'), "caf\xC3\xA9"},
        {std::string(30, 'x') + "CAF\xC3\x89_REPORT.TXT" + std::string(30, 'y'), "report"},
        {std::string(30, 'a') + "\xF0\x9F\x98\x80" + "pattern" + std::string(40, 'b'), "pattern"},
    };
    for (const auto& [text, pattern] : corpus) {
      DOCTEST_SUBCASE(pattern.c_str()) {
        const string_search::SimdSubstringHints hints =
            string_search::MakeSimdSubstringHints(pattern);
        CHECK(string_search::ContainsSubstringI(text, pattern, hints) ==
              string_search::ContainsSubstringI(text, pattern));
        CHECK(string_search::ContainsSubstring(text, pattern, hints) ==
              string_search::ContainsSubstring(text, pattern));
      }
    }
  }

  TEST_CASE("StrStrCaseInsensitiveNonAsciiHaystackAgreesWithSubstringI") {
    const std::string haystack =
        std::string(40, 'a') + "\xC3\xA9" + std::string(40, 'b') + "Needle" + std::string(20, 'c');
    const char* const found =
        string_search::StrStrCaseInsensitive(haystack.c_str(), "needle");
    REQUIRE(found != nullptr);
    CHECK(found == haystack.c_str() + haystack.find("Needle"));
    CHECK(string_search::ContainsSubstringI(haystack, "needle"));
    const std::string miss =
        std::string(50, 'a') + "\xE6\x97\xA5" + std::string(50, 'b');
    CHECK(string_search::StrStrCaseInsensitive(miss.c_str(), "needle") == nullptr);
    CHECK_FALSE(string_search::ContainsSubstringI(miss, "needle"));
  }

  TEST_CASE("FindSubstringSpans case-sensitive") {
    std::vector<string_search::MatchSpan> spans;
    CHECK(string_search::FindSubstringSpans("foobar", "foo", true, spans));
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].start_ == 0);
    CHECK(spans[0].end_ == 3);

    CHECK_FALSE(string_search::FindSubstringSpans("foobar", "FOO", true, spans));
    CHECK(spans.empty());

    CHECK_FALSE(string_search::FindSubstringSpans("foobar", "", true, spans));
    CHECK(spans.empty());
  }

  TEST_CASE("FindSubstringSpans finds all occurrences case-sensitive") {
    std::vector<string_search::MatchSpan> spans;
    CHECK(string_search::FindSubstringSpans("foo/bar/foo.txt", "foo", true, spans));
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].start_ == 0);
    CHECK(spans[0].end_ == 3);
    CHECK(spans[1].start_ == 8);
    CHECK(spans[1].end_ == 11);
  }

  TEST_CASE("FindSubstringSpans case-insensitive") {
    std::vector<string_search::MatchSpan> spans;
    CHECK(string_search::FindSubstringSpans("FooBar", "oob", false, spans));
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].start_ == 1);
    CHECK(spans[0].end_ == 4);
  }

  TEST_CASE("FindSubstringSpans finds all occurrences case-insensitive") {
    std::vector<string_search::MatchSpan> spans;
    CHECK(string_search::FindSubstringSpans("FOO/bar/Foo.txt", "foo", false, spans));
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].start_ == 0);
    CHECK(spans[0].end_ == 3);
    CHECK(spans[1].start_ == 8);
    CHECK(spans[1].end_ == 11);
  }

  TEST_CASE("FindFuzzySpans records and merges adjacent matches") {
    std::vector<string_search::MatchSpan> spans;
    CHECK(string_search::FindFuzzySpans("foobar", "fbr", true, spans));
    REQUIRE(spans.size() == 3);
    CHECK(spans[0].start_ == 0);
    CHECK(spans[0].end_ == 1);
    CHECK(spans[1].start_ == 3);
    CHECK(spans[1].end_ == 4);
    CHECK(spans[2].start_ == 5);
    CHECK(spans[2].end_ == 6);

    // Adjacent chars in both text and pattern merge into one span.
    CHECK(string_search::FindFuzzySpans("foobar", "foo", true, spans));
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].start_ == 0);
    CHECK(spans[0].end_ == 3);

    CHECK_FALSE(string_search::FindFuzzySpans("foobar", "fbz", true, spans));
    CHECK(spans.empty());
  }

  TEST_CASE("FindGlobLiteralSpans highlights literal runs") {
    std::vector<string_search::MatchSpan> spans;

    // Literals "final" and ".txt" are adjacent in the text → merged into one span.
    CHECK(string_search::FindGlobLiteralSpans("report_final.txt", "*final*.txt", true, spans));
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].start_ == 7);
    CHECK(spans[0].end_ == 16);

    CHECK(string_search::FindGlobLiteralSpans("foo_bar_baz", "foo*baz", true, spans));
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].start_ == 0);
    CHECK(spans[0].end_ == 3);
    CHECK(spans[1].start_ == 8);
    CHECK(spans[1].end_ == 11);

    CHECK_FALSE(string_search::FindGlobLiteralSpans("anything", "***", true, spans));
    CHECK(spans.empty());

    CHECK(string_search::FindGlobLiteralSpans("Report.TXT", "*.txt", false, spans));
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].start_ == 6);
    CHECK(spans[0].end_ == 10);
  }

  TEST_CASE("SortAndMergeSpans sorts and coalesces overlap") {
    std::vector<string_search::MatchSpan> spans{{5, 11}, {0, 5}, {10, 14}, {18, 21}};
    string_search::SortAndMergeSpans(spans);
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].start_ == 0);
    CHECK(spans[0].end_ == 14);
    CHECK(spans[1].start_ == 18);
    CHECK(spans[1].end_ == 21);
  }
}
