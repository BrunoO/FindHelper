#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "search/SearchContext.h"
#include "search/SearchContextBuilder.h"
#include "search/TrigramQueryPlanner.h"
#include <algorithm>
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

    // Path separators -> leaf-only extraction (directory segments ignored)
    const SearchContext ctx_slash = SearchContextBuilder::Build("pp:src/main.cpp", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_slash = filename_trigram::TrigramQueryPlanner::Plan(ctx_slash);
    REQUIRE(plan_slash.has_value());
    CHECK(plan_slash->size() == 6); // leaf "main.cpp" -> 6 trigrams

    const SearchContext ctx_backslash = SearchContextBuilder::Build("pp:src\\main.cpp", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_backslash = filename_trigram::TrigramQueryPlanner::Plan(ctx_backslash);
    REQUIRE(plan_backslash.has_value());
    CHECK(plan_backslash->size() == 6); // leaf "main.cpp" -> 6 trigrams

    // Metacharacters -> nullopt (fallback to parallel scan)
    const SearchContext ctx_class = SearchContextBuilder::Build("pp:doc[0-9].txt", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx_class).has_value());

    const SearchContext ctx_alt = SearchContextBuilder::Build("pp:{debug,release}.log", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx_alt).has_value());

    const SearchContext ctx_anchor = SearchContextBuilder::Build("pp:^file.txt$", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx_anchor).has_value());
  }

  TEST_CASE("PathPattern leaf extraction") {
    // pp:**/controller*.cpp -> leaf "controller*.cpp": "controller" (8) + ".cpp" (2) = 10
    const SearchContext ctx_controller =
        SearchContextBuilder::Build("pp:**/controller*.cpp", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_controller = filename_trigram::TrigramQueryPlanner::Plan(ctx_controller);
    REQUIRE(plan_controller.has_value());
    CHECK(plan_controller->size() == 10);

    // Leaf with no >= 3-char literal -> nullopt
    const SearchContext ctx_short_leaf =
        SearchContextBuilder::Build("pp:src/gui/*.h", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx_short_leaf).has_value());

    // Trailing slash (pure directory match) -> nullopt
    const SearchContext ctx_trailing =
        SearchContextBuilder::Build("pp:src/gui/", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx_trailing).has_value());

    // Backslash separators: leaf "*builder*.cpp" -> "builder" (5) + ".cpp" (2) = 7
    const SearchContext ctx_builder =
        SearchContextBuilder::Build("pp:src\\tools\\*builder*.cpp", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_builder = filename_trigram::TrigramQueryPlanner::Plan(ctx_builder);
    REQUIRE(plan_builder.has_value());
    CHECK(plan_builder->size() == 7);

    // Complex leaf: "cat" (1) + "report" (4) + ".pdf" (2) = 7
    const SearchContext ctx_complex =
        SearchContextBuilder::Build("pp:**/cat*report*.pdf", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_complex = filename_trigram::TrigramQueryPlanner::Plan(ctx_complex);
    REQUIRE(plan_complex.has_value());
    CHECK(plan_complex->size() == 7);

    // Character class in leaf -> whole-leaf nullopt (no per-segment fallback)
    const SearchContext ctx_class_leaf =
        SearchContextBuilder::Build("pp:**/test[0-9]*.cpp", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx_class_leaf).has_value());

    // Leaf-only "**" (directory wildcard) -> nullopt
    const SearchContext ctx_dir_wild =
        SearchContextBuilder::Build("pp:**/include/**", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx_dir_wild).has_value());
  }

  TEST_CASE("Glob with separators uses leaf only") {
    // "src/search/*controller*.h" is a Glob (no pp: prefix): leaf "*controller*.h"
    // gives "controller" (8); directory trigrams like "src" must NOT be required.
    const SearchContext ctx_glob_sep =
        SearchContextBuilder::Build("src/search/*controller*.h", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_glob_sep = filename_trigram::TrigramQueryPlanner::Plan(ctx_glob_sep);
    REQUIRE(plan_glob_sep.has_value());
    CHECK(plan_glob_sep->size() == 8);
    const auto src_trigram = filename_trigram::PackTrigram('s', 'r', 'c');
    CHECK(std::find(plan_glob_sep->begin(), plan_glob_sep->end(), src_trigram) == plan_glob_sep->end());
  }

  TEST_CASE("Path query side planning") {
    // Path-only query (empty filename): leaf "entity*.cpp" -> "entity" (4) + ".cpp" (2) = 6
    const SearchContext ctx_path_only =
        SearchContextBuilder::Build("", "pp:**/entity*.cpp", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_path_only = filename_trigram::TrigramQueryPlanner::Plan(ctx_path_only);
    REQUIRE(plan_path_only.has_value());
    CHECK(plan_path_only->size() == 6);

    // Combined filename ("model" -> 3) + path leaf ("entity*.cpp" -> 6): intersection = 9
    const SearchContext ctx_combined =
        SearchContextBuilder::Build("model", "pp:**/entity*.cpp", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_combined = filename_trigram::TrigramQueryPlanner::Plan(ctx_combined);
    REQUIRE(plan_combined.has_value());
    CHECK(plan_combined->size() == 9);

    // Separator-free path_query may match a directory -> nullopt for the path side
    const SearchContext ctx_path_sub =
        SearchContextBuilder::Build("", "controller", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx_path_sub).has_value());

    const SearchContext ctx_path_glob =
        SearchContextBuilder::Build("", "*controller*", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx_path_glob).has_value());

    // Filename side alone still filters when the path side is unplannable
    const SearchContext ctx_single_side =
        SearchContextBuilder::Build("model", "controller", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_single_side = filename_trigram::TrigramQueryPlanner::Plan(ctx_single_side);
    REQUIRE(plan_single_side.has_value());
    CHECK(plan_single_side->size() == 3);

    // Substring path with separators cannot constrain the filename -> nullopt
    const SearchContext ctx_path_dir =
        SearchContextBuilder::Build("", "pp:src/gui/", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::TrigramQueryPlanner::Plan(ctx_path_dir).has_value());
  }

}
