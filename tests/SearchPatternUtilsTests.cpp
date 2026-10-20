#include <string>
#include <string_view>

#include "TestHelpers.h"
#include "doctest/doctest.h"
#include "gui/PatternDiagnostics.h"
#include "search/SearchPatternUtils.h"
#include "search/SearchPrefixes.h"

namespace test_helpers {
struct DetectPatternTypeTestCase {
  std::string pattern;
  search_pattern_utils::PatternType expected_type;
};

inline void RunParameterizedDetectPatternTypeTests(const std::vector<DetectPatternTypeTestCase>& test_cases) {
  for (const auto& tc : test_cases) {
    DOCTEST_SUBCASE(tc.pattern.c_str()) {
      CHECK(search_pattern_utils::DetectPatternType(tc.pattern) == tc.expected_type);
    }
  }
}
}  // namespace test_helpers

TEST_SUITE("SearchPatternUtils - Pattern Detection") {

  TEST_CASE("Explicit prefixes (highest priority)") {
    const std::vector<test_helpers::DetectPatternTypeTestCase> test_cases = {
      {"re:test", search_pattern_utils::PatternType::StdRegex},
      {"pp:test", search_pattern_utils::PatternType::PathPattern},
      {"re:", search_pattern_utils::PatternType::StdRegex},
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
      // Legacy '\d{3}' spelling: braces still auto-detect as PathPattern
      // (its meaning is now separator + literal 'd' thrice; digits use [0-9])
      {"\\d{3}", search_pattern_utils::PatternType::PathPattern},
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
      {"c:\\dev\\*.cpp", search_pattern_utils::PatternType::Glob},
      {"c:\\work\\*.txt", search_pattern_utils::PatternType::Glob},
    };
    test_helpers::RunParameterizedDetectPatternTypeTests(test_cases);
  }

  TEST_CASE("Substring (default)") {
    const std::vector<test_helpers::DetectPatternTypeTestCase> test_cases = {
      {"test", search_pattern_utils::PatternType::Substring},
      {"file.txt", search_pattern_utils::PatternType::Substring},
      {"", search_pattern_utils::PatternType::Substring},
      {"partage", search_pattern_utils::PatternType::Substring},
      {"c:\\dev", search_pattern_utils::PatternType::Substring},
      {"c:\\work", search_pattern_utils::PatternType::Substring},
      {"c:\\\\dev", search_pattern_utils::PatternType::Substring},
      {"c:\\\\work", search_pattern_utils::PatternType::Substring},
      // No escapes: '\w' is a literal segment, so this is a plain substring
      {"\\w+.txt", search_pattern_utils::PatternType::Substring},
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
      {"re:^test$", search_pattern_utils::PatternType::StdRegex},
      {"pp:*.txt", search_pattern_utils::PatternType::PathPattern},
      {"re:**/*.cpp", search_pattern_utils::PatternType::StdRegex},
    };
    test_helpers::RunParameterizedDetectPatternTypeTests(test_cases);
  }

  TEST_CASE("Retired rs: prefix is no longer a regex") {
    // `rs:` was renamed to `re:` (see SearchPrefixes.h). It must not resolve to
    // StdRegex any more: an old query is read as an ordinary pattern, so these
    // cases pin what an un-migrated `rs:` query now does - match the literal
    // "rs:" text, or land in PathPattern because of an anchor/class.
    const std::vector<test_helpers::DetectPatternTypeTestCase> test_cases = {
      {"rs:test", search_pattern_utils::PatternType::Substring},
      {"rs:", search_pattern_utils::PatternType::Substring},
      // '^' is no longer at index 0, so the pattern is not even auto-detected
      // as a PathPattern anchor - it is a plain substring.
      {"rs:^report", search_pattern_utils::PatternType::Substring},
      // A trailing '$' or a '{...}' still trips PathPattern auto-detection.
      {"rs:.*\\.(cpp|h)$", search_pattern_utils::PatternType::PathPattern},
      {"rs:report_\\d{4}", search_pattern_utils::PatternType::PathPattern},
    };
    test_helpers::RunParameterizedDetectPatternTypeTests(test_cases);

    // Sanity: the same shapes with the new prefix really are regexes, so the
    // cases above fail for the prefix and not for the pattern syntax.
    CHECK(search_pattern_utils::DetectPatternType("re:test") ==
          search_pattern_utils::PatternType::StdRegex);
    CHECK(search_pattern_utils::DetectPatternType("re:report_\\d{4}") ==
          search_pattern_utils::PatternType::StdRegex);
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
  TEST_CASE("plain pattern returned unchanged") {
    CHECK(search_pattern_utils::ExtractPattern("report") == "report");
    CHECK(search_pattern_utils::ExtractPattern("*.txt") == "*.txt");
  }

  TEST_CASE("prefix-only (re:) returns empty string") {
    CHECK(search_pattern_utils::ExtractPattern("re:") == "");
  }

  TEST_CASE("empty string returns empty string") {
    CHECK(search_pattern_utils::ExtractPattern("") == "");
  }

  TEST_CASE("short strings (< 3 chars) are returned unchanged") {
    CHECK(search_pattern_utils::ExtractPattern("ab") == "ab");
    CHECK(search_pattern_utils::ExtractPattern("r") == "r");
  }

  TEST_CASE("retired rs: prefix is not stripped") {
    // Only a recognized prefix is removed; `rs:` is payload now.
    CHECK(search_pattern_utils::ExtractPattern("rs:test") == "rs:test");
    CHECK(search_pattern_utils::ExtractPatternView("rs:test") == "rs:test");
    CHECK_FALSE(search_pattern_utils::HasExplicitPrefix("rs:test"));
    CHECK(search_pattern_utils::HasLegacyRegexPrefix("rs:test"));
    CHECK_FALSE(search_pattern_utils::HasLegacyRegexPrefix("re:test"));
  }
}

TEST_SUITE("SearchPatternUtils - Prefix tokens") {
  // Pins the prefix table against the dispatch functions: every declared token
  // must be consumed by detection and stripping, and nothing else may be.
  TEST_CASE("every declared prefix is detected and stripped") {
    for (const std::string_view token : search_pattern_utils::prefix::kAll) {
      CAPTURE(token);
      const std::string query = std::string(token) + "body";
      CHECK(search_pattern_utils::HasPrefix(query, token));
      CHECK(search_pattern_utils::HasExplicitPrefix(query));
      CHECK(search_pattern_utils::IsBarePrefix(token));
      CHECK_FALSE(search_pattern_utils::IsBarePrefix(query));
      CHECK(search_pattern_utils::StripPrefix(query) == "body");
      CHECK(search_pattern_utils::ExtractPattern(query) == "body");
      // The token is consumed, not ignored: it changes the detected type.
      CHECK(search_pattern_utils::DetectPatternType(query) !=
            search_pattern_utils::DetectPatternType("body"));
    }
  }

  TEST_CASE("plain input is passed through untouched") {
    CHECK_FALSE(search_pattern_utils::HasExplicitPrefix("body"));
    CHECK_FALSE(search_pattern_utils::HasExplicitPrefix("pp"));
    CHECK_FALSE(search_pattern_utils::HasPrefix("", search_pattern_utils::prefix::kRegex));
    CHECK(search_pattern_utils::StripPrefix("body") == "body");
    CHECK(search_pattern_utils::StripPrefix("") == "");
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

  TEST_CASE("std regex via re: prefix") {
    CHECK(match("re:rep.*", "report", false));
    CHECK_FALSE(match("re:^budget$", "report", false));
  }

  TEST_CASE("re: with empty pattern returns false") {
    CHECK_FALSE(match("re:", "anything", false));
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
    // Alternation over extensions (JSON "re:.*\\.(cpp|hpp|cxx|cc)$").
    CHECK(match("re:.*\\.(cpp|hpp|cxx|cc)$", "a/main.cpp", false));
    CHECK(match("re:.*\\.(cpp|hpp|cxx|cc)$", "a/main.hpp", false));
    CHECK_FALSE(match("re:.*\\.(cpp|hpp|cxx|cc)$", "a/main.h", false));
    // Exclusion via lookahead.
    CHECK(match("re:^(?!.*[/\\\\]thirdparty[/\\\\]).*\\.cpp$", "C:\\p\\src\\a.cpp", false));
    CHECK_FALSE(match("re:^(?!.*[/\\\\]thirdparty[/\\\\]).*\\.cpp$", "C:\\p\\thirdparty\\a.cpp", false));
    // Filename strictly starting with test.
    CHECK(match("re:.*[/\\\\]test[^/\\\\]*\\.py$", "C:\\p\\test_foo.py", false));
    CHECK_FALSE(match("re:.*[/\\\\]test[^/\\\\]*\\.py$", "C:\\p\\mytest_foo.py", false));
    CHECK_FALSE(match("re:.*[/\\\\]test[^/\\\\]*\\.py$", "C:\\p\\mytest\\foo.py", false));
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
    CHECK(Diagnose("re:[a-z").empty());
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

  TEST_CASE("the retired rs: prefix is explained, not silently mis-matched") {
    // `rs:` still type-detects (as PathPattern/Glob/Substring), so these queries
    // would otherwise compile clean and return wrong rows. The empty-result view
    // must name the rename instead.
    const std::string path_message = Diagnose("rs:.*\\.(cpp|h)$");
    CHECK(path_message.find("Path pattern") != std::string::npos);
    CHECK(path_message.find("rs:") != std::string::npos);
    CHECK(path_message.find("re:") != std::string::npos);

    SearchPipelineState pipeline;
    UpdatePatternDiagnostics("rs:^test", "", false, pipeline);
    CHECK(pipeline.pattern_error.find("Name pattern") != std::string::npos);
    CHECK(pipeline.pattern_error.find("re:") != std::string::npos);
  }

  TEST_CASE("the rs: rename hint reports the path query first") {
    SearchPipelineState pipeline;
    UpdatePatternDiagnostics("rs:name", "rs:path", false, pipeline);
    CHECK(pipeline.pattern_error.find("Path pattern") != std::string::npos);
  }

  TEST_CASE("the migrated re: queries are reported as usable") {
    CHECK(Diagnose("re:^report").empty());
    CHECK(Diagnose("re:.*\\.(cpp|h)$").empty());
    CHECK(Diagnose("re:report_\\d{4}").empty());
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

TEST_SUITE("SearchPatternUtils - MatcherKind labels") {

  TEST_CASE("MatcherKindLabel maps every kind") {
    using search_pattern_utils::MatcherKind;
    using search_pattern_utils::MatcherKindLabel;
    CHECK(std::string(MatcherKindLabel(MatcherKind::AllPass)) == "all");
    CHECK(std::string(MatcherKindLabel(MatcherKind::Empty)) == "none");
    CHECK(std::string(MatcherKindLabel(MatcherKind::Substring)) == "substr");
    CHECK(std::string(MatcherKindLabel(MatcherKind::GlobPrefix)) == "prefix");
    CHECK(std::string(MatcherKindLabel(MatcherKind::GlobSuffix)) == "suffix");
    CHECK(std::string(MatcherKindLabel(MatcherKind::StdRegex)) == "regex");
    CHECK(std::string(MatcherKindLabel(MatcherKind::PathPattern)) == "pp");
    CHECK(std::string(MatcherKindLabel(MatcherKind::Fuzzy)) == "fuzzy");
    CHECK(std::string(MatcherKindLabel(MatcherKind::GlobComplex)) == "gpp");
  }

  TEST_CASE("ClassifyMatcherKind agrees with the built matcher") {
    // Locks classification to construction: every valid pattern must label
    // identically whether classified or built. Two documented divergences:
    // invalid re: patterns (classified StdRegex, built EmptyMatcher) and
    // complex globs (classified GlobComplex for provenance, built PathPattern).
    const std::vector<std::string> patterns = {"",
                                               "foo",
                                               "*",
                                               "**",
                                               "***",
                                               "**/*.txt",
                                               "*mid*",
                                               "pre*",
                                               "*suf",
                                               "file[0-9].txt",
                                               "pp:*find*pg[cd]",
                                               "pp:",
                                               "pp:**",
                                               "fz:",
                                               "re:.*",
                                               "fz:abc",
                                               "^a",
                                               "a{2}",
                                               "re:.*\\.cpp$",
                                               };
    for (const std::string& pattern : patterns) {
      const auto built =
          search_pattern_utils::CreatePatternMatcher(pattern, false);
      CHECK(std::string(search_pattern_utils::MatcherLabel(built)) ==
            std::string(search_pattern_utils::MatcherKindLabel(
                search_pattern_utils::ClassifyMatcherKind(pattern))));
    }
    for (const std::string& pattern : std::vector<std::string>{"a?b", "a*b*c"}) {
      CHECK(search_pattern_utils::ClassifyMatcherKind(pattern) ==
            search_pattern_utils::MatcherKind::GlobComplex);
      const auto built =
          search_pattern_utils::CreatePatternMatcher(pattern, false);
      CHECK(std::string(search_pattern_utils::MatcherLabel(built)) == "pp");
    }
  }
}

// ---------------------------------------------------------------------------
// Matcher ordering (Lever A): only reorder when the filename side has no cheap
// reject and the path side does. Pins the heuristic so it cannot drift.
// ---------------------------------------------------------------------------
TEST_SUITE("SearchPatternUtils - PreferPathMatcherFirst") {

  TEST_CASE("unfiltered regex filename + cheap path -> path first") {
    const auto filename = search_pattern_utils::CreateFilenameMatcher("re:\\d{4}", false);
    const auto path = search_pattern_utils::CreatePathMatcher("/src", false);
    CHECK(search_pattern_utils::PreferPathMatcherFirst(filename, path));
  }

  TEST_CASE("cheap filename + cheap path -> filename first") {
    const auto filename = search_pattern_utils::CreateFilenameMatcher("report", false);
    const auto path = search_pattern_utils::CreatePathMatcher("/src", false);
    CHECK_FALSE(search_pattern_utils::PreferPathMatcherFirst(filename, path));
  }

  TEST_CASE("literal-pre-filter regex filename -> filename first") {
    const auto filename = search_pattern_utils::CreateFilenameMatcher("re:report", false);
    const auto path = search_pattern_utils::CreatePathMatcher("/src", false);
    CHECK_FALSE(search_pattern_utils::PreferPathMatcherFirst(filename, path));
  }

  TEST_CASE("suffix-pre-filter regex filename -> filename first") {
    const auto filename = search_pattern_utils::CreateFilenameMatcher("re:.*\\.(cpp|h)$", false);
    const auto path = search_pattern_utils::CreatePathMatcher("/src", false);
    CHECK_FALSE(search_pattern_utils::PreferPathMatcherFirst(filename, path));
  }

  TEST_CASE("fuzzy filename + cheap path -> path first") {
    const auto filename = search_pattern_utils::CreateFilenameMatcher("fz:imgui", false);
    const auto path = search_pattern_utils::CreatePathMatcher("/src", false);
    CHECK(search_pattern_utils::PreferPathMatcherFirst(filename, path));
  }

  TEST_CASE("both unfiltered -> filename first") {
    const auto filename = search_pattern_utils::CreateFilenameMatcher("re:\\d{4}", false);
    const auto path = search_pattern_utils::CreatePathMatcher("re:\\d{4}", false);
    CHECK_FALSE(search_pattern_utils::PreferPathMatcherFirst(filename, path));
  }

  TEST_CASE("no path matcher -> filename first") {
    const auto filename = search_pattern_utils::CreateFilenameMatcher("re:\\d{4}", false);
    const search_pattern_utils::PatternMatcher no_path;
    CHECK_FALSE(search_pattern_utils::PreferPathMatcherFirst(filename, no_path));
  }
}
