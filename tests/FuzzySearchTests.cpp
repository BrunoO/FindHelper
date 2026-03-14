#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <string_view>

#include "doctest/doctest.h"
#include "search/SearchPatternUtils.h"
#include "utils/StringSearch.h"

TEST_SUITE("FuzzySearch") {

  struct FuzzyTestCase {
    std::string_view text;
    std::string_view pattern;
    bool expected;
  };

  TEST_CASE("StringSearch::FuzzyMatch") {
    const std::vector<FuzzyTestCase> test_cases = {
      {"foobar", "fbr", true},
      {"foobar", "foo", true},
      {"foobar", "bar", true},
      {"foobar", "r", true},
      {"foobar", "", true},
      {"", "", true},
      {"foobar", "fbz", false},
      {"foobar", "rf", false},
      {"", "f", false},
      {"one park email", "mmopok", false},  // From PR feedback
      // Path-like and longer input scenarios (case-sensitive)
      {"src/foo/bar/baz.cpp", "f/bz", true},
      {"C:/Users/John/Documents/report_final_v2.docx", "cujr2", false},
      {"foo", "foobar", false}
    };

    for (const auto& tc : test_cases) {
      DOCTEST_SUBCASE((std::string(tc.pattern) + " in " + std::string(tc.text)).c_str()) {
        CHECK(string_search::FuzzyMatch(tc.text, tc.pattern) == tc.expected);
      }
    }
  }

  TEST_CASE("StringSearch::FuzzyMatchI") {
    const std::vector<FuzzyTestCase> test_cases = {
      {"FooBar", "fbr", true},
      {"FooBar", "FBR", true},
      {"foobar", "FBR", true},
      {"foobar", "fbz", false},
      // Case-insensitive path-like scenarios and length edge cases
      {"Foo/Bar/Baz.cpp", "f/bz", true},
      {"foo/bar", "F/B", true},
      {"Ab", "ABCD", false},
      {"C:/Users/John/Documents/report_final_v2.docx", "cujr2", true}
    };

    for (const auto& tc : test_cases) {
      DOCTEST_SUBCASE((std::string(tc.pattern) + " in " + std::string(tc.text)).c_str()) {
        CHECK(string_search::FuzzyMatchI(tc.text, tc.pattern) == tc.expected);
      }
    }
  }

  TEST_CASE("SearchPatternUtils::DetectPatternType") {
    CHECK(search_pattern_utils::DetectPatternType("fz:test") == search_pattern_utils::PatternType::Fuzzy);
    CHECK(search_pattern_utils::DetectPatternType("fz:") == search_pattern_utils::PatternType::Fuzzy);

    // Check it doesn't break others
    CHECK(search_pattern_utils::DetectPatternType("rs:test") == search_pattern_utils::PatternType::StdRegex);
    CHECK(search_pattern_utils::DetectPatternType("pp:test") == search_pattern_utils::PatternType::PathPattern);
    CHECK(search_pattern_utils::DetectPatternType("*.txt") == search_pattern_utils::PatternType::Glob);
    CHECK(search_pattern_utils::DetectPatternType("test") == search_pattern_utils::PatternType::Substring);
  }

  TEST_CASE("SearchPatternUtils::MatchPattern (Fuzzy)") {
    CHECK(search_pattern_utils::MatchPattern("fz:fbr", "foobar", true));
    CHECK(search_pattern_utils::MatchPattern("fz:FBR", "foobar", false));
    CHECK_FALSE(search_pattern_utils::MatchPattern("fz:FBR", "foobar", true));
  }

  TEST_CASE("SearchPatternUtils::CreateMatcher (Fuzzy)") {
    auto matcher = search_pattern_utils::CreatePathMatcher("fz:fbr", false);
    CHECK(matcher("foobar"));
    CHECK(matcher("FooBar"));
    CHECK_FALSE(matcher("fob"));
  }
}
