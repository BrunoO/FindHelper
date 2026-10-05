#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <string>
#include <string_view>

#include "TestHelpers.h"
#include "doctest/doctest.h"
#include "gui/PatternDiagnostics.h"

TEST_SUITE("SearchPatternUtils - Pattern Detection") {

  TEST_CASE("Explicit prefixes (highest priority)") {
    const std::vector<test_helpers::DetectPatternTypeTestCase> test_cases = {
      {"rs:test", search_pattern_utils::PatternType::StdRegex},
      {"pp:test", search_pattern_utils::PatternType::PathPattern},
      {"rs:", search_pattern_utils::PatternType::StdRegex},
      {"pp:", search_pattern_utils::PatternType::PathPattern},
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
      {"\\w+.txt", search_pattern_utils::PatternType::PathPattern},
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
      {"src/*.cpp", search_pattern_utils::PatternType::Glob},
    };
    test_helpers::RunParameterizedDetectPatternTypeTests(test_cases);
  }

  TEST_CASE("Substring (default)") {
    const std::vector<test_helpers::DetectPatternTypeTestCase> test_cases = {
      {"test", search_pattern_utils::PatternType::Substring},
      {"file.txt", search_pattern_utils::PatternType::Substring},
      {"", search_pattern_utils::PatternType::Substring},
      {"partage", search_pattern_utils::PatternType::Substring},
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
      {"*[0-9].txt", search_pattern_utils::PatternType::PathPattern},
    };
    test_helpers::RunParameterizedDetectPatternTypeTests(test_cases);
  }

  TEST_CASE("Prefix takes precedence over auto-detection") {
    const std::vector<test_helpers::DetectPatternTypeTestCase> test_cases = {
      {"rs:^test$", search_pattern_utils::PatternType::StdRegex},
      {"pp:*.txt", search_pattern_utils::PatternType::PathPattern},
      {"rs:**/*.cpp", search_pattern_utils::PatternType::StdRegex},
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

  TEST_CASE("AI prompt examples behave as documented") {
    // Mirrors every path/filename pattern in BuildSearchConfigPrompt
    // (src/api/GeminiApiUtils.cpp). App default is case-insensitive.
    // Single extension.
    CHECK(match("pp:**/*.cpp", "src/main.cpp", false));
    CHECK_FALSE(match("pp:**/*.cpp", "src/main.h", false));
    // Folder + extension, both separators (Windows backslash included).
    CHECK(match("pp:**/src/**/*.cpp", "C:\\proj\\src\\a\\b\\x.cpp", false));
    CHECK(match("pp:**/src/**/*.cpp", "C:/proj/src/x.cpp", false));
    CHECK_FALSE(match("pp:**/src/**/*.cpp", "C:\\proj\\other\\x.cpp", false));
    CHECK_FALSE(match("pp:**/src/**/*.cpp", "C:\\proj\\my_src\\x.cpp", false));
    // Folder only: exact segment, anywhere, with subfolders.
    CHECK(match("pp:**/folder_name**", "a/folder_name/f.txt", false));
    CHECK_FALSE(match("pp:**/folder_name**", "a/my_folder_name/f.txt", false));
    // Folder starting with vs containing.
    CHECK(match("pp:**/report*/**/*.pdf", "d/report_q1/f.pdf", false));
    CHECK_FALSE(match("pp:**/report*/**/*.pdf", "d/my_report/f.pdf", false));
    CHECK(match("pp:**/*report*/**/*.pdf", "d/my_report/f.pdf", false));
    CHECK(match("pp:**/*report*/**/*.pdf", "d/report_q1/f.pdf", false));
    // Prompt response examples.
    CHECK(match("pp:**/USN_windows**/*.cpp", "D:\\w\\USN_windows\\a.cpp", false));
    CHECK_FALSE(match("pp:**/USN_windows**/*.cpp", "D:\\w\\other\\a.cpp", false));
    CHECK(match("pp:**/documents**/*.txt", "/home/u/documents/a.txt", false));
    CHECK(match("pp:**/src/**", "C:\\p\\src\\readme.md", false));
    CHECK_FALSE(match("pp:**/src/**", "C:\\p\\other\\f", false));
    CHECK(match("pp:**/docs/**", "C:\\p\\docs\\a.md", false));
    CHECK(match("pp:**/*client_xxx*/**", "/d/my_client_xxx/f", false));
    CHECK_FALSE(match("pp:**/*client_xxx*/**", "/d/other/f", false));
    // Filename globs from the prompt examples.
    CHECK(match("readme*", "readme.txt", false));
    CHECK_FALSE(match("readme*", "notreadme.txt", false));
    CHECK(match("*devis*", "Facture-devis-2024.pdf", false));
    CHECK_FALSE(match("*devis*", "report.pdf", false));
    // Alternation over extensions (JSON "rs:.*\\.(cpp|hpp|cxx|cc)$").
    CHECK(match("rs:.*\\.(cpp|hpp|cxx|cc)$", "a/main.cpp", false));
    CHECK(match("rs:.*\\.(cpp|hpp|cxx|cc)$", "a/main.hpp", false));
    CHECK_FALSE(match("rs:.*\\.(cpp|hpp|cxx|cc)$", "a/main.h", false));
    // Exclusion via lookahead.
    CHECK(match("rs:^(?!.*[/\\\\]thirdparty[/\\\\]).*\\.cpp$", "C:\\p\\src\\a.cpp", false));
    CHECK_FALSE(match("rs:^(?!.*[/\\\\]thirdparty[/\\\\]).*\\.cpp$", "C:\\p\\thirdparty\\a.cpp", false));
    // Filename strictly starting with test.
    CHECK(match("rs:.*[/\\\\]test[^/\\\\]*\\.py$", "C:\\p\\test_foo.py", false));
    CHECK_FALSE(match("rs:.*[/\\\\]test[^/\\\\]*\\.py$", "C:\\p\\mytest_foo.py", false));
    CHECK_FALSE(match("rs:.*[/\\\\]test[^/\\\\]*\\.py$", "C:\\p\\mytest\\foo.py", false));
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

TEST_SUITE("PatternDiagnostics") {

  // Runs the diagnostic over one query and returns the resulting message
  // (empty when the query is fine).
  static std::string Diagnose(std::string_view query, bool case_sensitive = false) {
    SearchPipelineState pipeline;
    UpdatePatternDiagnostics("", query, case_sensitive, pipeline);
    return pipeline.pattern_error;
  }

  TEST_CASE("a usable pattern reports nothing") {
    CHECK(Diagnose("").empty());
    CHECK(Diagnose("**/*.cpp").empty());
    CHECK(Diagnose("pp:**/logs*/**/*.cpp").empty());
    // Over the NFA mask limit, but compiled fine via the backtracking fallback.
    CHECK(Diagnose("pp:**/" + std::string(100, 'a') + "/*.cpp").empty());
  }

  TEST_CASE("queries that are not PathPatterns are never reported") {
    // An explicit non-path prefix wins over auto-detection, so unbalanced
    // brackets or braces inside them are that dialect's business, not ours.
    CHECK(Diagnose("plain text").empty());
    CHECK(Diagnose("rs:[a-z").empty());
    CHECK(Diagnose("fz:bro{ken").empty());
    CHECK(Diagnose("pp:").empty());  // empty pp: matches everything
  }

  TEST_CASE("an unterminated character class is reported") {
    // '[' is itself a PathPattern trigger, so this needs no prefix.
    const std::string message = Diagnose("**/[abc.log");
    CHECK(message.find("Path pattern") != std::string::npos);
    CHECK(message.find("']'") != std::string::npos);
  }

  TEST_CASE("an over-long pattern is reported, and no longer silently dropped") {
    // Each literal is one atom (a class is a single atom however long it is), so
    // this needs 202 - past kMaxPatternAtoms. Before the backtracking fallback a
    // long pattern like this silently matched nothing.
    const std::string message = Diagnose("pp:[a]" + std::string(200, 'a') + "[0-9]");
    CHECK(message.find("too many atoms") != std::string::npos);
  }

  TEST_CASE("the name query is reported when it is the broken one") {
    SearchPipelineState pipeline;
    UpdatePatternDiagnostics("pp:**/[bad", "", false, pipeline);
    CHECK(pipeline.pattern_error.find("Name pattern") != std::string::npos);
  }

  TEST_CASE("the path query is reported first when both are broken") {
    SearchPipelineState pipeline;
    UpdatePatternDiagnostics("pp:[bad", "pp:[worse", false, pipeline);
    CHECK(pipeline.pattern_error.find("Path pattern") != std::string::npos);
  }

  TEST_CASE("recomputing is skipped while the inputs are unchanged") {
    SearchPipelineState pipeline;
    UpdatePatternDiagnostics("", "pp:**/[bad", false, pipeline);
    const std::string first = pipeline.pattern_error;
    REQUIRE(!first.empty());

    // Same inputs: no recompute, same verdict.
    UpdatePatternDiagnostics("", "pp:**/[bad", false, pipeline);
    CHECK(pipeline.pattern_error == first);

    // Fixed input: the stale error clears.
    UpdatePatternDiagnostics("", "pp:**/*.cpp", false, pipeline);
    CHECK(pipeline.pattern_error.empty());

    // Case-sensitivity is part of the cache key.
    UpdatePatternDiagnostics("", "pp:**/[bad", false, pipeline);
    REQUIRE(!pipeline.pattern_error.empty());
    UpdatePatternDiagnostics("", "pp:**/[bad", true, pipeline);
    CHECK(!pipeline.pattern_error.empty());
  }
}
