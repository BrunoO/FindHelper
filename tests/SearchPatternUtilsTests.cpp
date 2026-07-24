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

TEST_SUITE("SearchPatternUtils - ExtractPattern") {
  TEST_CASE("rs: prefix is stripped") {
    CHECK(search_pattern_utils::ExtractPattern("rs:hello") == "hello");
  }

  TEST_CASE("pp: prefix is stripped") {
    CHECK(search_pattern_utils::ExtractPattern("pp:**/*.cpp") == "**/*.cpp");
  }

  TEST_CASE("fz: prefix is stripped") {
    CHECK(search_pattern_utils::ExtractPattern("fz:query") == "query");
  }

  TEST_CASE("plain pattern returned unchanged") {
    CHECK(search_pattern_utils::ExtractPattern("report") == "report");
    CHECK(search_pattern_utils::ExtractPattern("*.txt") == "*.txt");
  }

  TEST_CASE("prefix-only (rs:) returns empty string") {
    CHECK(search_pattern_utils::ExtractPattern("rs:") == "");
  }

  TEST_CASE("empty string returns empty string") {
    CHECK(search_pattern_utils::ExtractPattern("") == "");
  }

  TEST_CASE("short strings (< 3 chars) are returned unchanged") {
    CHECK(search_pattern_utils::ExtractPattern("ab") == "ab");
    CHECK(search_pattern_utils::ExtractPattern("r") == "r");
  }
}

TEST_SUITE("SearchPatternUtils - CreatePatternMatcher") {
  const auto match = [](const std::string_view pattern, const std::string_view text,
                        const bool case_sensitive) {
    return search_pattern_utils::CreatePatternMatcher(pattern, case_sensitive)(text);
  };

  TEST_CASE("empty pattern matches any text") {
    CHECK(match("", "anything", false));
    CHECK(match("", "", false));
  }

  TEST_CASE("substring match (case-insensitive)") {
    CHECK(match("report", "annual_report_2024.pdf", false));
    CHECK(match("REPORT", "annual_report_2024.pdf", false));
    CHECK_FALSE(match("budget", "annual_report_2024.pdf", false));
  }

  TEST_CASE("substring match (case-sensitive)") {
    CHECK(match("report", "annual_report_2024.pdf", true));
    CHECK_FALSE(match("REPORT", "annual_report_2024.pdf", true));
  }

  TEST_CASE("glob pattern matches wildcard") {
    CHECK(match("*.txt", "readme.txt", false));
    CHECK_FALSE(match("*.txt", "readme.pdf", false));
  }

  TEST_CASE("fuzzy match via fz: prefix") {
    CHECK(match("fz:rpt", "report", false));
    CHECK_FALSE(match("fz:xyz", "report", false));
  }

  TEST_CASE("std regex via rs: prefix") {
    CHECK(match("rs:rep.*", "report", false));
    CHECK_FALSE(match("rs:^budget$", "report", false));
  }

  TEST_CASE("rs: with empty pattern returns false") {
    CHECK_FALSE(match("rs:", "anything", false));
  }

  TEST_CASE("path pattern via pp: prefix") {
    CHECK(match("pp:**/*.cpp", "src/utils/Logger.cpp", false));
    CHECK_FALSE(match("pp:**/*.cpp", "src/utils/Logger.h", false));
  }

  TEST_CASE("path pattern auto-detected via double-star") {
    // "**" triggers PathPattern auto-detection (no pp: prefix needed).
    CHECK(match("**/*.txt", "docs/file.txt", false));
    CHECK_FALSE(match("**/*.txt", "docs/file.cpp", false));
  }
}

TEST_SUITE("SearchPatternUtils - ExtensionMatches") {
  TEST_CASE("exact match (case-sensitive)") {
    const ExtensionSet exts{"cpp", "h", "txt"};
    CHECK(search_pattern_utils::ExtensionMatches("txt", exts, true));
    CHECK(search_pattern_utils::ExtensionMatches("cpp", exts, true));
    CHECK_FALSE(search_pattern_utils::ExtensionMatches("TXT", exts, true));
    CHECK_FALSE(search_pattern_utils::ExtensionMatches("pdf", exts, true));
  }

  TEST_CASE("case-insensitive match folds to lowercase") {
    const ExtensionSet exts{"cpp", "txt"};
    CHECK(search_pattern_utils::ExtensionMatches("TXT", exts, false));
    CHECK(search_pattern_utils::ExtensionMatches("CPP", exts, false));
    CHECK_FALSE(search_pattern_utils::ExtensionMatches("pdf", exts, false));
  }

  TEST_CASE("empty extension matches empty entry in set") {
    const ExtensionSet with_empty{""};
    CHECK(search_pattern_utils::ExtensionMatches("", with_empty, true));

    const ExtensionSet without_empty{"txt"};
    CHECK_FALSE(search_pattern_utils::ExtensionMatches("", without_empty, true));
  }

  TEST_CASE("empty set never matches") {
    const ExtensionSet empty_set;
    CHECK_FALSE(search_pattern_utils::ExtensionMatches("txt", empty_set, true));
    CHECK_FALSE(search_pattern_utils::ExtensionMatches("txt", empty_set, false));
  }
}
