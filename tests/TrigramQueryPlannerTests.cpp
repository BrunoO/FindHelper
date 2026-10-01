#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "search/SearchContext.h"
#include "search/SearchContextBuilder.h"
#include "search/TrigramQueryPlanner.h"
#include <vector>

TEST_SUITE("TrigramQueryPlanner") {

  TEST_CASE("Empty query returns nullopt") {
    const SearchContext ctx = SearchContextBuilder::Build("", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx).has_value());
  }

  TEST_CASE("Substring planning") {
    // Short queries (< 3 chars) cannot form a trigram
    const SearchContext ctx_short = SearchContextBuilder::Build("ab", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx_short).has_value());

    // Valid 3-char query
    const SearchContext ctx_3 = SearchContextBuilder::Build("abc", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_3 = filename_trigram::TrigramQueryPlanner::Plan(ctx_3);
    REQUIRE(plan_3.has_value());
    CHECK(plan_3->size() == 1);

    // Multi-trigram query
    const SearchContext ctx_sub = SearchContextBuilder::Build("document", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_sub = filename_trigram::TrigramQueryPlanner::Plan(ctx_sub);
    REQUIRE(plan_sub.has_value());
    CHECK(plan_sub->size() == 6); // "doc", "ocu", "cum", "ume", "men", "ent"
  }

  TEST_CASE("Fuzzy queries return nullopt") {
    const SearchContext ctx_fz = SearchContextBuilder::Build("fz:document", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx_fz).has_value());
  }

  TEST_CASE("Glob planning - multi-segment literal intersection") {
    // Glob with no valid literal segment (all < 3 chars)
    const SearchContext ctx_short_glob = SearchContextBuilder::Build("*.c", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx_short_glob).has_value());

    // Glob with single valid segment
    const SearchContext ctx_single = SearchContextBuilder::Build("test*.c", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_single = filename_trigram::TrigramQueryPlanner::Plan(ctx_single);
    REQUIRE(plan_single.has_value());
    CHECK(plan_single->size() == 2); // "tes", "est"

    // Glob with multiple valid segments: "cat*report*.pdf"
    // "cat" -> 1 ("cat")
    // "report" -> 4 ("rep", "epo", "por", "ort")
    // ".pdf" -> 2 (".pd", "pdf")
    // Total = 7 unique trigrams
    const SearchContext ctx_multi = SearchContextBuilder::Build("cat*report*.pdf", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_multi = filename_trigram::TrigramQueryPlanner::Plan(ctx_multi);
    REQUIRE(plan_multi.has_value());
    CHECK(plan_multi->size() == 7);
  }

  TEST_CASE("StdRegex planning") {
    // Regex with extracting required literal >= 3 chars
    const SearchContext ctx_regex = SearchContextBuilder::Build("rs:config_.*\\.json", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_regex = filename_trigram::TrigramQueryPlanner::Plan(ctx_regex);
    REQUIRE(plan_regex.has_value());
    // Required literal "config_" has 7 chars -> 5 trigrams
    CHECK(plan_regex->size() == 5);

    // Regex with unconstrained wildcard (no required literal)
    const SearchContext ctx_wild = SearchContextBuilder::Build("rs:.*", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx_wild).has_value());
  }

  TEST_CASE("PathPattern planning and safety fallbacks") {
    // Plain literal with pp: prefix (prefix stripped)
    const SearchContext ctx_pp = SearchContextBuilder::Build("pp:report", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_pp = filename_trigram::TrigramQueryPlanner::Plan(ctx_pp);
    REQUIRE(plan_pp.has_value());
    CHECK(plan_pp->size() == 4); // "rep", "epo", "por", "ort"

    // Wildcard without path separators
    const SearchContext ctx_pp_glob = SearchContextBuilder::Build("pp:test*.cpp", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_pp_glob = filename_trigram::TrigramQueryPlanner::Plan(ctx_pp_glob);
    REQUIRE(plan_pp_glob.has_value());
    CHECK(plan_pp_glob->size() == 4);

    // Path separators -> nullopt (fallback to parallel scan)
    const SearchContext ctx_slash = SearchContextBuilder::Build("pp:src/main.cpp", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx_slash).has_value());

    const SearchContext ctx_backslash = SearchContextBuilder::Build("pp:src\\main.cpp", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx_backslash).has_value());

    // Metacharacters -> nullopt (fallback to parallel scan)
    const SearchContext ctx_class = SearchContextBuilder::Build("pp:doc[0-9].txt", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx_class).has_value());

    const SearchContext ctx_alt = SearchContextBuilder::Build("pp:{debug,release}.log", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx_alt).has_value());

    const SearchContext ctx_anchor = SearchContextBuilder::Build("pp:^file.txt$", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx_anchor).has_value());
  }

}
