#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

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
    CHECK(search_pattern_utils::DetectPatternType("re:test") == search_pattern_utils::PatternType::StdRegex);
    CHECK(search_pattern_utils::DetectPatternType("pp:test") == search_pattern_utils::PatternType::PathPattern);
    CHECK(search_pattern_utils::DetectPatternType("*.txt") == search_pattern_utils::PatternType::Glob);
    CHECK(search_pattern_utils::DetectPatternType("test") == search_pattern_utils::PatternType::Substring);
  }

  TEST_CASE("SearchPatternUtils::CreatePatternMatcher (Fuzzy)") {
    const auto matcher_fbr_cs = search_pattern_utils::CreatePatternMatcher("fz:fbr", true);
    const auto matcher_fbr_ci = search_pattern_utils::CreatePatternMatcher("fz:FBR", false);
    const auto matcher_FBR_cs = search_pattern_utils::CreatePatternMatcher("fz:FBR", true);
    CHECK(matcher_fbr_cs("foobar"));
    CHECK(matcher_fbr_ci("foobar"));
    CHECK_FALSE(matcher_FBR_cs("foobar"));
  }

  TEST_CASE("SearchPatternUtils::CreateMatcher (Fuzzy)") {
    auto matcher = search_pattern_utils::CreatePathMatcher("fz:fbr", false);
    CHECK(matcher("foobar"));
    CHECK(matcher("FooBar"));
    CHECK_FALSE(matcher("fob"));
  }

  TEST_CASE("SearchPatternUtils::ExtractPatternView") {
    CHECK(search_pattern_utils::ExtractPatternView("fz:fbr") == "fbr");
    CHECK(search_pattern_utils::ExtractPatternView("re:a.*") == "a.*");
    CHECK(search_pattern_utils::ExtractPatternView("plain") == "plain");
  }

  TEST_CASE("StringSearch::DamerauLevenshteinDistance1") {
    // Exact match
    CHECK(string_search::DamerauLevenshteinDistance1("find", "find"));

    // Single-edit operations
    CHECK(string_search::DamerauLevenshteinDistance1("finq", "find"));  // Substitution
    CHECK(string_search::DamerauLevenshteinDistance1("fnd", "find"));   // Insertion into pattern
    CHECK(string_search::DamerauLevenshteinDistance1("finnd", "find")); // Deletion from pattern
    CHECK(string_search::DamerauLevenshteinDistance1("fnid", "find"));  // Adjacent transposition

    // Two edits (should fail)
    CHECK_FALSE(string_search::DamerauLevenshteinDistance1("fqq", "find"));
    CHECK_FALSE(string_search::DamerauLevenshteinDistance1("finqq", "find"));
    CHECK_FALSE(string_search::DamerauLevenshteinDistance1("fdin", "find")); // non-adjacent transposition

    // Length difference > 1 (O(1) filter)
    CHECK_FALSE(string_search::DamerauLevenshteinDistance1("fi", "find"));
    CHECK_FALSE(string_search::DamerauLevenshteinDistance1("find_all", "find"));

    // Case sensitivity (FinQ vs find: 2 case-sensitive diffs, 1 case-insensitive diff)
    CHECK(string_search::DamerauLevenshteinDistance1("FinQ", "find", false));
    CHECK_FALSE(string_search::DamerauLevenshteinDistance1("FinQ", "find", true));
  }

  TEST_CASE("StringSearch::ComputeFuzzyScore") {
    // Exact match band [900, 1000]
    CHECK(string_search::ComputeFuzzyScore("find", "find") == 1000);

    // Prefix match band [900, 950]
    const uint16_t prefix_score = string_search::ComputeFuzzyScore("find_helper.cpp", "find");
    CHECK(prefix_score >= 900);
    CHECK(prefix_score <= 950);

    // Subsequence match band [400, 899]
    const uint16_t subseq_score = string_search::ComputeFuzzyScore("foobar", "fbr");
    CHECK(subseq_score >= 400);
    CHECK(subseq_score <= 899);

    // Word boundary & camelCase bonuses
    const uint16_t boundary_score = string_search::ComputeFuzzyScore("SearchController.cpp", "SC");
    const uint16_t non_boundary_score = string_search::ComputeFuzzyScore("asc.cpp", "sc");
    CHECK(boundary_score > non_boundary_score);

    // Typo match band [100, 300]
    const uint16_t typo_score = string_search::ComputeFuzzyScore("find", "finq");
    CHECK(typo_score >= 100);
    CHECK(typo_score <= 300);

    // Dominance invariant (AC-2): Subsequence minimum (400) > Typo maximum (300)
    CHECK(subseq_score > typo_score);

    // Length / excess penalty & coverage bonus: compact names rank above bloated names
    const uint16_t compact_score = string_search::ComputeFuzzyScore("FindHelper", "helper");
    const uint16_t bloated_score = string_search::ComputeFuzzyScore(
        "_fused_moving_avg_obs_fq_helper_compositeexplicitautograd_dispatch", "helper");
    CHECK(compact_score > bloated_score);

    // Non-match
    CHECK(string_search::ComputeFuzzyScore("completely_unrelated.txt", "finq") == 0);
    CHECK(string_search::ComputeFuzzyScore("", "find") == 0);
  }

  TEST_CASE("StringSearch::ComputeFuzzyScoreV2Ordering") {
    // Compact names separate from bloated wrappers by tens of points (v1: 1pt).
    const uint16_t compact = string_search::ComputeFuzzyScore("FindHelper", "helper");
    const uint16_t bloated = string_search::ComputeFuzzyScore(
        "_fused_moving_avg_obs_fq_helper_compositeexplicitautograd_dispatch", "helper");
    CHECK(compact > bloated);
    CHECK(compact - bloated >= 50);

    // Boundary-anchored match beats accidental mid-word match with same gaps.
    const uint16_t boundary = string_search::ComputeFuzzyScore("foo_bar", "fb");
    const uint16_t accidental = string_search::ComputeFuzzyScore("foobar", "fb");
    CHECK(boundary > accidental);
    CHECK(boundary - accidental >= 20);

    // Contiguous run beats gapped alignment.
    CHECK(string_search::ComputeFuzzyScore("Xab", "ab") >
          string_search::ComputeFuzzyScore("aXb", "ab"));

    // Earlier match beats later match, all else equal.
    CHECK(string_search::ComputeFuzzyScore("XabYYYY", "ab") >
          string_search::ComputeFuzzyScore("XXXabY", "ab"));

    // Stem match beats extension match (non-prefix pair so the extension
    // penalty, not the prefix band, decides).
    CHECK(string_search::ComputeFuzzyScore("mytxtdoc", "txt") >
          string_search::ComputeFuzzyScore("file.txt", "txt"));

    // Prefix still dominates subsequence, subsequence still dominates typo.
    const uint16_t prefix = string_search::ComputeFuzzyScore("findings.txt", "find");
    const uint16_t subseq = string_search::ComputeFuzzyScore("xfinezd.txt", "find");
    const uint16_t typo = string_search::ComputeFuzzyScore("finq", "find");
    CHECK(prefix >= 900);
    CHECK(subseq >= 400);
    CHECK(subseq <= 899);
    CHECK(typo >= 100);
    CHECK(typo <= 300);
    CHECK(prefix > subseq);
    CHECK(subseq > typo);
  }

  TEST_CASE("StringSearch::FuzzyMatchWithTypos") {
    // Without typos (default allow_typos = false)
    CHECK_FALSE(string_search::FuzzyMatch("find", "finq", false));
    CHECK_FALSE(string_search::FuzzyMatchI("find", "finq", false));

    // With typos enabled (allow_typos = true)
    CHECK(string_search::FuzzyMatch("find", "finq", true));
    CHECK(string_search::FuzzyMatchI("Find", "finq", true));

    // Length guard: queries < 3 chars reject typos even with allow_typos = true
    CHECK_FALSE(string_search::FuzzyMatch("find", "fq", true));
    CHECK_FALSE(string_search::FuzzyMatchI("find", "fq", true));

    // Substring-scoped typos: single edit inside a longer basename
    CHECK(string_search::FuzzyMatch("find_helper.cpp", "finq", true));
    CHECK(string_search::FuzzyMatchI("SearchController.cpp", "cotnroller", true));
    CHECK_FALSE(string_search::FuzzyMatch("find_helper.cpp", "finq", false));
    CHECK_FALSE(string_search::FuzzyMatchI("SearchController.cpp", "cotnroller", false));

    // Still no match when more than one edit away
    CHECK_FALSE(string_search::FuzzyMatch("find_helper.cpp", "fqxx", true));
    CHECK_FALSE(string_search::FuzzyMatch("abc", "finq", true));
  }

  TEST_CASE("StringSearch::SubstringTypoScore") {
    // Same error quality scores the same in a bare or a longer basename.
    CHECK(string_search::ComputeFuzzyScore("find_helper.cpp", "finq") ==
          string_search::ComputeFuzzyScore("find", "finq"));

    // Substring typos stay in the typo band, below the subsequence floor.
    const uint16_t sub_typo = string_search::ComputeFuzzyScore("find_helper.cpp", "finq");
    CHECK(sub_typo >= 100);
    CHECK(sub_typo <= 300);
    CHECK(sub_typo < string_search::ComputeFuzzyScore("foobar", "fbr"));

    // Boundary-anchored typo outranks a late mid-word typo.
    CHECK(string_search::ComputeFuzzyScore("my_find_helper.cpp", "finq") >
          string_search::ComputeFuzzyScore("aaafinb.cpp", "finq"));
  }

  TEST_CASE("StringSearch::AdjacentKeys") {
    using string_search::AreAdjacentKeys;
    using string_search::KeyboardLayout;
    // Layout-specific pairs (m moved rows between layouts).
    CHECK(AreAdjacentKeys('m', 'n', KeyboardLayout::Qwerty));
    CHECK_FALSE(AreAdjacentKeys('m', 'n', KeyboardLayout::Azerty));
    CHECK(AreAdjacentKeys('m', 'l', KeyboardLayout::Azerty));
    CHECK_FALSE(AreAdjacentKeys('m', 'l', KeyboardLayout::Qwerty));
    // Union accepts either layout's pairs.
    CHECK(AreAdjacentKeys('m', 'n', KeyboardLayout::Union));
    CHECK(AreAdjacentKeys('m', 'l', KeyboardLayout::Union));
    // Shared pairs and rejections hold on every layout.
    CHECK(AreAdjacentKeys('d', 'x', KeyboardLayout::Qwerty));
    CHECK(AreAdjacentKeys('d', 'x', KeyboardLayout::Azerty));
    CHECK(AreAdjacentKeys('d', 'x', KeyboardLayout::Union));
    CHECK_FALSE(AreAdjacentKeys('w', 'x', KeyboardLayout::Qwerty));
    CHECK(AreAdjacentKeys('w', 'x', KeyboardLayout::Azerty));
    CHECK_FALSE(AreAdjacentKeys('f', 'q', KeyboardLayout::Union));
    // Non-letters and identical chars are never "adjacent".
    CHECK_FALSE(AreAdjacentKeys('1', 'q', KeyboardLayout::Union));
    CHECK_FALSE(AreAdjacentKeys('d', 'd', KeyboardLayout::Union));
  }

  TEST_CASE("StringSearch::KeyboardAwareTypoScoring") {
    // Fat-finger (x sits below d on QWERTY) outranks a random substitution.
    const uint16_t fat_finger = string_search::ComputeFuzzyScore("find", "finx");
    const uint16_t random_sub = string_search::ComputeFuzzyScore("find", "finq");
    CHECK(fat_finger >= 100);
    CHECK(fat_finger <= 300);
    CHECK(fat_finger - random_sub >= 20);

    // Explicit layouts price the same pair differently (x/a adjacent on
    // QWERTY only): |P| = 6 keeps both inside the k=2 budget.
    const uint16_t qwerty_score =
        string_search::ComputeFuzzyScore("abcdef", "xbcdeq", false, string_search::KeyboardLayout::Qwerty);
    const uint16_t azerty_score =
        string_search::ComputeFuzzyScore("abcdef", "xbcdeq", false, string_search::KeyboardLayout::Azerty);
    CHECK(qwerty_score > azerty_score);

    // Recall is layout-independent: acceptance never depends on the layout.
    CHECK(string_search::ComputeFuzzyScore("abcdef", "xbcdeq", false,
                                           string_search::KeyboardLayout::Qwerty) > 0);
    CHECK(string_search::ComputeFuzzyScore("abcdef", "xbcdeq", false,
                                           string_search::KeyboardLayout::Azerty) > 0);
  }

  TEST_CASE("StringSearch::TypoGateDifferential") {
    // Exhaustive small-alphabet proof that the prefilter gates never change
    // outcomes: every (pattern, text) pair over {a,B,_} must agree between
    // the gated public filter and an ungated brute-force reference built
    // from the raw distance primitives (no gates, no caps beyond the scan).
    using string_search::string_search_detail::DamerauLevenshteinDistance1Internal;
    using string_search::string_search_detail::EditDistanceLEInternal;
    using string_search::string_search_detail::FuzzyMatchInternal;
    using string_search::string_search_detail::CaseSensitive;
    using string_search::string_search_detail::CaseInsensitive;
    using string_search::string_search_detail::TypoBudgetForPatternLen;
    const std::vector<char> alphabet = {'a', 'B', '_'};
    std::vector<std::string> patterns;
    std::vector<std::string> texts;
    std::string cur;
    const std::function<void(int, std::vector<std::string>&)> gen = [&](int max_len,
                                                                        std::vector<std::string>& out) {
      cur.clear();
      out.emplace_back("");
      const std::function<void(int)> rec = [&](int left) {
        if (left == 0) {
          return;
        }
        for (const char c : alphabet) {
          cur.push_back(c);
          out.push_back(cur);
          rec(left - 1);
          cur.pop_back();
        }
      };
      rec(max_len);
    };
    gen(6, patterns);
    gen(4, texts);
    auto RefMatch = [&](const std::string& text, const std::string& pattern, bool case_sensitive) {
      const std::string_view t(text);
      const std::string_view p(pattern);
      if (p.length() < 3) {
        return false;
      }
      const int budget = TypoBudgetForPatternLen(p.length());
      if (case_sensitive ? FuzzyMatchInternal<CaseSensitive>(t, p)
                         : FuzzyMatchInternal<CaseInsensitive>(t, p)) {
        return true;
      }
      auto DistLE = [&](std::string_view a, std::string_view b, int k) {
        if (case_sensitive) {
          return k <= 1 ? DamerauLevenshteinDistance1Internal<CaseSensitive>(a, b)
                        : EditDistanceLEInternal<CaseSensitive>(a, b, k);
        }
        return k <= 1 ? DamerauLevenshteinDistance1Internal<CaseInsensitive>(a, b)
                      : EditDistanceLEInternal<CaseInsensitive>(a, b, k);
      };
      if (DistLE(p, t, 1)) {
        return true;
      }
      // Ungated scan: every window length in |P|±budget at the full budget
      // (mirrors the implementation's predicate choices exactly; only the
      // gates are omitted, so any mismatch is a gate bug).
      const int p_len = static_cast<int>(p.length());
      const int t_len = static_cast<int>(t.length());
      for (int wl = p_len - budget; wl <= p_len + budget; ++wl) {
        if (wl <= 0 || wl > t_len) {
          continue;
        }
        for (int s = 0; s + wl <= t_len; ++s) {
          if (DistLE(p, t.substr(static_cast<size_t>(s), static_cast<size_t>(wl)), budget)) {
            return true;
          }
        }
      }
      return false;
    };
    int mismatches_ci = 0;
    int mismatches_cs = 0;
    int pairs = 0;
    for (const auto& pat : patterns) {
      if (pat.length() < 3 || pat.length() > 6) {
        continue;
      }
      for (const auto& txt : texts) {
        ++pairs;
        if (string_search::FuzzyMatchI(txt, pat, true) != RefMatch(txt, pat, false)) {
          ++mismatches_ci;
        }
        if (string_search::FuzzyMatch(txt, pat, true) != RefMatch(txt, pat, true)) {
          ++mismatches_cs;
        }
      }
    }
    CHECK(pairs > 100000);
    CHECK(mismatches_ci == 0);
    CHECK(mismatches_cs == 0);
  }

  TEST_CASE("StringSearch::DoubleEditTypos") {
    // Two edits need a pattern of >= 6 chars (edit budget scales with length).
    CHECK(string_search::FuzzyMatchI("SearchController.cpp", "Cotnroler", true));
    CHECK(string_search::FuzzyMatch("find", "finndd", true));
    CHECK_FALSE(string_search::FuzzyMatchI("SearchController.cpp", "Cotnroler", false));

    // Short patterns stay at k=1: two edits away means no match.
    CHECK_FALSE(string_search::FuzzyMatch("find_helper.cpp", "fnqxx", true));

    // k=2 scores stay in the typo band but below k=1 at the same position.
    const uint16_t k2 = string_search::ComputeFuzzyScore("SearchController.cpp", "Cotnroler");
    const uint16_t k1 = string_search::ComputeFuzzyScore("SearchController.cpp", "cotnroller");
    CHECK(k2 >= 100);
    CHECK(k2 <= 300);
    CHECK(k2 < k1);

    // Highlighting covers the matched window.
    std::vector<string_search::MatchSpan> spans;
    CHECK(string_search::FindFuzzySpans("SearchController.cpp", "Cotnroler", false, spans));
    CHECK(!spans.empty());
  }

  TEST_CASE("StringSearch::TypoGateLongPatterns") {
    // Bigram tier (k=2, |P| = 8-10): disjoint double transpositions keep an
    // exact bigram and must match; three transpositions exceed the budget.
    CHECK(string_search::FuzzyMatchI("bacdfegh", "abcdefgh", true));
    CHECK_FALSE(string_search::FuzzyMatchI("badcfegh", "abcdefgh", true));
    CHECK_FALSE(string_search::FuzzyMatchI("xxxxxxxx", "abcdefgh", true));
    CHECK_FALSE(string_search::FuzzyMatchI("hgfedcba", "abcdefgh", true));
    // Multiset tier (k=2, |P| = 6-7): double transpositions with no exact
    // bigram at all must still match (transpositions preserve the multiset).
    CHECK(string_search::FuzzyMatchI("acbedf", "abcdef", true));
    // Same tier at |P| = 7, transpositions placed to isolate every survivor.
    CHECK(string_search::FuzzyMatchI("acbdfeg", "abcdefg", true));
    // Trigram tier (k=2, |P| >= 11).
    CHECK(string_search::FuzzyMatchI("saerchcontrollre", "searchcontroller", true));
    CHECK_FALSE(string_search::FuzzyMatchI("zzzzzzzzzzzzzz", "searchcontroller", true));
  }

  TEST_CASE("StringSearch::FindFuzzySpansSubstringTypo") {
    std::vector<string_search::MatchSpan> spans;
    CHECK(string_search::FindFuzzySpans("find_helper.cpp", "finq", false, spans));
    CHECK(!spans.empty());
    CHECK(spans.front().start_ == 0);
  }
}

TEST_SUITE("SearchPatternUtils - FuzzyRelevance") {

  TEST_CASE("HasFuzzyQuery detects either field") {
    CHECK(search_pattern_utils::HasFuzzyQuery("fz:find", ""));
    CHECK(search_pattern_utils::HasFuzzyQuery("", "fz:src"));
    CHECK(search_pattern_utils::HasFuzzyQuery("fz:find", "fz:src"));
    CHECK_FALSE(search_pattern_utils::HasFuzzyQuery("find", "src"));
    CHECK_FALSE(search_pattern_utils::HasFuzzyQuery("", ""));
  }

  TEST_CASE("filename-only query scores the basename") {
    CHECK(search_pattern_utils::ComputeFuzzyRelevance("find", "C:/x/find", "fz:find", "", false) ==
          1000);
    CHECK(search_pattern_utils::ComputeFuzzyRelevance("query.txt", "C:/x/query.txt", "fz:zzz",
                                                      "plain", false) == 0);
  }

  TEST_CASE("path-only query scores the full path") {
    // The reported case: a path-spanning fz: query must produce relevance
    // instead of zeros (basename alone cannot match it).
    const uint16_t path_score = search_pattern_utils::ComputeFuzzyRelevance(
        "query.txt", "C:/x/finq/helper/query.txt", "", "fz:finqhelqer", false);
    CHECK(path_score >= 400);
    CHECK(path_score <= 899);
  }

  TEST_CASE("both fields take the best part (bands preserved)") {
    // Exact filename match stays 1000 despite a mediocre path part.
    CHECK(search_pattern_utils::ComputeFuzzyRelevance("find", "C:/src/find", "fz:find", "fz:src",
                                                      false) == 1000);
    // Combined score is exactly one of the two parts.
    const uint16_t name_part = string_search::ComputeFuzzyScore("my_findings.txt", "find", false);
    const uint16_t path_part =
        string_search::ComputeFuzzyScore("C:/src/my_findings.txt", "src", false);
    const uint16_t combined = search_pattern_utils::ComputeFuzzyRelevance(
        "my_findings.txt", "C:/src/my_findings.txt", "fz:find", "fz:src", false);
    CHECK(combined >= name_part);
    CHECK(combined >= path_part);
    CHECK((combined == name_part || combined == path_part));
  }

  TEST_CASE("no fuzzy query scores zero") {
    CHECK(search_pattern_utils::ComputeFuzzyRelevance("find", "C:/src/find", "find", "src", false) ==
          0);
  }
}
