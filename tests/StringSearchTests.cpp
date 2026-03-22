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
}
