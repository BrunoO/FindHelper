#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <string>
#include <string_view>

#include "TestHelpers.h"
#include "doctest/doctest.h"

TEST_SUITE("SearchPatternUtils - Pattern Detection") {

  TEST_CASE("Explicit prefixes (highest priority)") {
    const std::vector<test_helpers::DetectPatternTypeTestCase> test_cases = {
      {"rs:test", search_pattern_utils::PatternType::StdRegex},
      {"pp:test", search_pattern_utils::PatternType::PathPattern},
      {"rs:", search_pattern_utils::PatternType::StdRegex},
      {"pp:", search_pattern_utils::PatternType::PathPattern}
    };
    test_helpers::RunParameterizedDetectPatternTypeTests(test_cases);
  }

  TEST_CASE("Advanced PathPattern auto-detect") {
    const std::vector<test_helpers::DetectPatternTypeTestCase> test_cases = {
      // Anchors
      {"^test", search_pattern_utils::PatternType::PathPattern},
      {"test$", search_pattern_utils::PatternType::PathPattern},
      {"^test$", search_pattern_utils::PatternType::PathPattern},
      // Double star (recursive)
      {"**/*.txt", search_pattern_utils::PatternType::PathPattern},
      {"src/**/file", search_pattern_utils::PatternType::PathPattern},
      // Character classes
      {"file[0-9].txt", search_pattern_utils::PatternType::PathPattern},
      {"[abc]test", search_pattern_utils::PatternType::PathPattern},
      {"test[^a-z]", search_pattern_utils::PatternType::PathPattern},
      // Quantifiers
      {"file{3}.txt", search_pattern_utils::PatternType::PathPattern},
      {"test{2,5}", search_pattern_utils::PatternType::PathPattern},
      {"file{3,}", search_pattern_utils::PatternType::PathPattern},
      // Shorthands
      {"\\d{3}", search_pattern_utils::PatternType::PathPattern},
      {"\\w+.txt", search_pattern_utils::PatternType::PathPattern}
    };
    test_helpers::RunParameterizedDetectPatternTypeTests(test_cases);
  }

  TEST_CASE("Glob patterns (simple wildcards)") {
    const std::vector<test_helpers::DetectPatternTypeTestCase> test_cases = {
      {"*.txt", search_pattern_utils::PatternType::Glob},
      {"*.pdf", search_pattern_utils::PatternType::Glob},
      {"*partage*.pdf", search_pattern_utils::PatternType::Glob},
      {"*partage*pdf", search_pattern_utils::PatternType::Glob},
      {"file?.*", search_pattern_utils::PatternType::Glob},
      {"test*.cpp", search_pattern_utils::PatternType::Glob},
      {"*error*2024.log", search_pattern_utils::PatternType::Glob},
      {"src/*.cpp", search_pattern_utils::PatternType::Glob}
    };
    test_helpers::RunParameterizedDetectPatternTypeTests(test_cases);
  }

  TEST_CASE("Substring (default)") {
    const std::vector<test_helpers::DetectPatternTypeTestCase> test_cases = {
      {"test", search_pattern_utils::PatternType::Substring},
      {"file.txt", search_pattern_utils::PatternType::Substring},
      {"", search_pattern_utils::PatternType::Substring},
      {"partage", search_pattern_utils::PatternType::Substring}
    };
    test_helpers::RunParameterizedDetectPatternTypeTests(test_cases);
  }

  TEST_CASE("Edge cases and combinations") {
    const std::vector<test_helpers::DetectPatternTypeTestCase> test_cases = {
      // Pattern with * and . but no advanced features -> Glob
      {"*.*", search_pattern_utils::PatternType::Glob},
      {"*.cpp", search_pattern_utils::PatternType::Glob},
      {"file.*", search_pattern_utils::PatternType::Glob},
      // Pattern with * and . but also has ** -> PathPattern
      {"**/*.cpp", search_pattern_utils::PatternType::PathPattern},
      // Pattern with * and . but also has anchor -> PathPattern
      {"^*.cpp$", search_pattern_utils::PatternType::PathPattern},
      // Pattern with * and . but also has character class -> PathPattern
      {"*[0-9].txt", search_pattern_utils::PatternType::PathPattern}
    };
    test_helpers::RunParameterizedDetectPatternTypeTests(test_cases);
  }

  TEST_CASE("Prefix takes precedence over auto-detection") {
    const std::vector<test_helpers::DetectPatternTypeTestCase> test_cases = {
      {"rs:^test$", search_pattern_utils::PatternType::StdRegex},
      {"pp:*.txt", search_pattern_utils::PatternType::PathPattern},
      {"rs:**/*.cpp", search_pattern_utils::PatternType::StdRegex}
    };
    test_helpers::RunParameterizedDetectPatternTypeTests(test_cases);
  }
}

TEST_SUITE("SearchPatternUtils - Pattern Matching") {
  TEST_CASE("Glob patterns match substrings") {
    // This is the key test: *partage*.pdf should match as Glob (substring)
    auto matcher = search_pattern_utils::CreatePathMatcher("*partage*.pdf", false);

    // Should match files with "partage" in the name and .pdf extension
    CHECK(matcher("mon_partage_file.pdf"));
    CHECK(matcher("C:/Users/Documents/partage_document.pdf"));
    CHECK(matcher("partage.pdf"));
    CHECK_FALSE(matcher("partage.txt"));  // Wrong extension
    CHECK_FALSE(matcher("document.pdf"));  // No "partage"
  }

  TEST_CASE("PathPattern requires full path match") {
    // PathPattern with ** should match full paths
    auto matcher = search_pattern_utils::CreatePathMatcher("**partage*.pdf", false);

    // Should match full paths
    CHECK(matcher("C:/Users/Documents/partage_document.pdf"));
    CHECK(matcher("partage.pdf"));
    // Note: PathPattern matches entire path, so this behavior may differ from Glob
  }
}
