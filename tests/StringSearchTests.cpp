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
        {"C:\\Users\\Test", "\\", true}};

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
        {"exact", "EXACT", true}};

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
}
