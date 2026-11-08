#include "doctest/doctest.h"

#include "search/SearchContext.h"
#include "search/TrigramQueryPlanner.h"
#include <algorithm>
#include <vector>

TEST_SUITE("TrigramQueryPlanner") {

  TEST_CASE("Empty query returns nullopt") {
    const SearchContext ctx = search_context_builder::Build("", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx).has_value());
  }

  TEST_CASE("Substring planning") {
    // Short queries (< 3 chars) cannot form a trigram
    const SearchContext ctx_short = search_context_builder::Build("ab", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_short).has_value());

    // Valid 3-char query
    const SearchContext ctx_3 = search_context_builder::Build("abc", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_3 = filename_trigram::PlanTrigrams(ctx_3);
    REQUIRE(plan_3.has_value());
    CHECK(plan_3->size() == 1);

    // Multi-trigram query
    const SearchContext ctx_sub = search_context_builder::Build("document", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_sub = filename_trigram::PlanTrigrams(ctx_sub);
    REQUIRE(plan_sub.has_value());
    CHECK(plan_sub->size() == 6); // "doc", "ocu", "cum", "ume", "men", "ent"
  }

  TEST_CASE("Fuzzy queries return nullopt") {
    const SearchContext ctx_fz = search_context_builder::Build("fz:document", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_fz).has_value());
  }

  TEST_CASE("Glob planning - multi-segment literal intersection") {
    // Glob with no valid literal segment (all < 3 chars)
    const SearchContext ctx_short_glob = search_context_builder::Build("*.c", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_short_glob).has_value());

    // Glob with single valid segment
    const SearchContext ctx_single = search_context_builder::Build("test*.c", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_single = filename_trigram::PlanTrigrams(ctx_single);
    REQUIRE(plan_single.has_value());
    CHECK(plan_single->size() == 2); // "tes", "est"

    // Glob with multiple valid segments: "cat*report*.pdf"
    // "cat" -> 1 ("cat")
    // "report" -> 4 ("rep", "epo", "por", "ort")
    // ".pdf" -> 2 (".pd", "pdf")
    // Total = 7 unique trigrams
    const SearchContext ctx_multi = search_context_builder::Build("cat*report*.pdf", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_multi = filename_trigram::PlanTrigrams(ctx_multi);
    REQUIRE(plan_multi.has_value());
    CHECK(plan_multi->size() == 7);
  }

  TEST_CASE("StdRegex planning") {
    // Regex with extracting required literal >= 3 chars
    const SearchContext ctx_regex = search_context_builder::Build("re:config_.*\\.json", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_regex = filename_trigram::PlanTrigrams(ctx_regex);
    REQUIRE(plan_regex.has_value());
    // Required literal "config_" has 7 chars -> 5 trigrams
    CHECK(plan_regex->size() == 5);

    // Regex with unconstrained wildcard (no required literal)
    const SearchContext ctx_wild = search_context_builder::Build("re:.*", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_wild).has_value());
  }

  TEST_CASE("PathPattern planning and safety fallbacks") {
    // Plain literal with pp: prefix (prefix stripped)
    const SearchContext ctx_pp = search_context_builder::Build("pp:report", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_pp = filename_trigram::PlanTrigrams(ctx_pp);
    REQUIRE(plan_pp.has_value());
    CHECK(plan_pp->size() == 4); // "rep", "epo", "por", "ort"

    // Wildcard without path separators
    const SearchContext ctx_pp_glob = search_context_builder::Build("pp:test*.cpp", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_pp_glob = filename_trigram::PlanTrigrams(ctx_pp_glob);
    REQUIRE(plan_pp_glob.has_value());
    CHECK(plan_pp_glob->size() == 4);

    // Path separators -> leaf-only extraction (directory segments ignored)
    const SearchContext ctx_slash = search_context_builder::Build("pp:src/main.cpp", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_slash = filename_trigram::PlanTrigrams(ctx_slash);
    REQUIRE(plan_slash.has_value());
    CHECK(plan_slash->size() == 6); // leaf "main.cpp" -> 6 trigrams

    const SearchContext ctx_backslash = search_context_builder::Build("pp:src\\main.cpp", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_backslash = filename_trigram::PlanTrigrams(ctx_backslash);
    REQUIRE(plan_backslash.has_value());
    CHECK(plan_backslash->size() == 6); // leaf "main.cpp" -> 6 trigrams

    // Metacharacters -> nullopt (fallback to parallel scan)
    const SearchContext ctx_class = search_context_builder::Build("pp:doc[0-9].txt", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_class).has_value());

    const SearchContext ctx_alt = search_context_builder::Build("pp:{debug,release}.log", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_alt).has_value());

    const SearchContext ctx_anchor = search_context_builder::Build("pp:^file.txt$", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_anchor).has_value());
  }

  TEST_CASE("PathPattern leaf extraction") {
    // pp:**/controller*.cpp -> leaf "controller*.cpp": "controller" (8) + ".cpp" (2) = 10
    const SearchContext ctx_controller =
        search_context_builder::Build("pp:**/controller*.cpp", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_controller = filename_trigram::PlanTrigrams(ctx_controller);
    REQUIRE(plan_controller.has_value());
    CHECK(plan_controller->size() == 10);

    // Leaf with no >= 3-char literal -> nullopt
    const SearchContext ctx_short_leaf =
        search_context_builder::Build("pp:src/gui/*.h", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_short_leaf).has_value());

    // Trailing slash (pure directory match) -> nullopt
    const SearchContext ctx_trailing =
        search_context_builder::Build("pp:src/gui/", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_trailing).has_value());

    // Backslash separators: leaf "*builder*.cpp" -> "builder" (5) + ".cpp" (2) = 7
    const SearchContext ctx_builder =
        search_context_builder::Build("pp:src\\tools\\*builder*.cpp", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_builder = filename_trigram::PlanTrigrams(ctx_builder);
    REQUIRE(plan_builder.has_value());
    CHECK(plan_builder->size() == 7);

    // Complex leaf: "cat" (1) + "report" (4) + ".pdf" (2) = 7
    const SearchContext ctx_complex =
        search_context_builder::Build("pp:**/cat*report*.pdf", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_complex = filename_trigram::PlanTrigrams(ctx_complex);
    REQUIRE(plan_complex.has_value());
    CHECK(plan_complex->size() == 7);

    // Character class in leaf -> whole-leaf nullopt (no per-segment fallback)
    const SearchContext ctx_class_leaf =
        search_context_builder::Build("pp:**/test[0-9]*.cpp", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_class_leaf).has_value());

    // Leaf-only "**" (directory wildcard) -> nullopt
    const SearchContext ctx_dir_wild =
        search_context_builder::Build("pp:**/include/**", "", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_dir_wild).has_value());
  }

  TEST_CASE("Glob with separators uses leaf only") {
    // "src/search/*controller*.h" is a Glob (no pp: prefix): leaf "*controller*.h"
    // gives "controller" (8); directory trigrams like "src" must NOT be required.
    const SearchContext ctx_glob_sep =
        search_context_builder::Build("src/search/*controller*.h", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_glob_sep = filename_trigram::PlanTrigrams(ctx_glob_sep);
    REQUIRE(plan_glob_sep.has_value());
    CHECK(plan_glob_sep->size() == 8);
    const auto src_trigram = filename_trigram::PackTrigram('s', 'r', 'c');
    CHECK(std::find(plan_glob_sep->begin(), plan_glob_sep->end(), src_trigram) == plan_glob_sep->end());
  }

  TEST_CASE("Path query side planning") {
    // Path-only query (empty filename): leaf "entity*.cpp" -> "entity" (4) + ".cpp" (2) = 6
    const SearchContext ctx_path_only =
        search_context_builder::Build("", "pp:**/entity*.cpp", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_path_only = filename_trigram::PlanTrigrams(ctx_path_only);
    REQUIRE(plan_path_only.has_value());
    CHECK(plan_path_only->size() == 6);

    // Combined filename ("model" -> 3) + path leaf ("entity*.cpp" -> 6): intersection = 9
    const SearchContext ctx_combined =
        search_context_builder::Build("model", "pp:**/entity*.cpp", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_combined = filename_trigram::PlanTrigrams(ctx_combined);
    REQUIRE(plan_combined.has_value());
    CHECK(plan_combined->size() == 9);

    // Separator-free path_query may match a directory -> nullopt for the path side
    const SearchContext ctx_path_sub =
        search_context_builder::Build("", "controller", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_path_sub).has_value());

    const SearchContext ctx_path_glob =
        search_context_builder::Build("", "*controller*", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_path_glob).has_value());

    // Filename side alone still filters when the path side is unplannable
    const SearchContext ctx_single_side =
        search_context_builder::Build("model", "controller", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_single_side = filename_trigram::PlanTrigrams(ctx_single_side);
    REQUIRE(plan_single_side.has_value());
    CHECK(plan_single_side->size() == 3);

    // Substring path with separators cannot constrain the filename -> nullopt
    const SearchContext ctx_path_dir =
        search_context_builder::Build("", "pp:src/gui/", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_path_dir).has_value());
  }

  TEST_CASE("Separator-free suffix globs constrain the filename end") {
    // "*.pdf" (suffix shape): the match must end at the path end, which lies
    // inside the filename -> ".pd" + "pdf" required.
    const SearchContext ctx_suffix =
        search_context_builder::Build("", "*.pdf", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_suffix = filename_trigram::PlanTrigrams(ctx_suffix);
    REQUIRE(plan_suffix.has_value());
    CHECK(plan_suffix->size() == 2);
    const auto pdf_trigram = filename_trigram::PackTrigram('.', 'p', 'd');
    CHECK(std::find(plan_suffix->begin(), plan_suffix->end(), pdf_trigram) != plan_suffix->end());

    // Complex shape (*lit* with a non-star tail): interior literals may land
    // on directory portions -> still nullopt.
    const SearchContext ctx_complex =
        search_context_builder::Build("", "*find*.pdf", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_complex).has_value());

    // Prefix and contains shapes pin the start or nothing -> nullopt.
    const SearchContext ctx_prefix =
        search_context_builder::Build("", "pre*", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_prefix).has_value());
    const SearchContext ctx_contains =
        search_context_builder::Build("", "*foo*", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_contains).has_value());

    // Suffix shape but too short for a trigram -> nullopt.
    const SearchContext ctx_short_suffix =
        search_context_builder::Build("", "*a", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_short_suffix).has_value());

    // Suffix shape with a character class: the leaf is not a plain literal.
    const SearchContext ctx_class_suffix =
        search_context_builder::Build("", "*[ab]", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_class_suffix).has_value());
  }

  TEST_CASE("Path-side leaf pinning and separator-glob soundness") {
    // F1 asymmetry: pp:**/dev** path-side -> nullopt; filename-side -> "dev" (1)
    const SearchContext ctx_path_dev_path =
        search_context_builder::Build("", "pp:**/dev**", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_path_dev_path).has_value());

    const SearchContext ctx_path_dev_fn =
        search_context_builder::Build("pp:**/dev**", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_dev_fn = filename_trigram::PlanTrigrams(ctx_path_dev_fn);
    REQUIRE(plan_dev_fn.has_value());
    CHECK(plan_dev_fn->size() == 1);
    const auto dev_trigram = filename_trigram::PackTrigram('d', 'e', 'v');
    CHECK(std::find(plan_dev_fn->begin(), plan_dev_fn->end(), dev_trigram) != plan_dev_fn->end());

    // Path leaf pinning after last **:
    // pp:/a/log**x -> tail "x" < 3 chars -> nullopt
    const SearchContext ctx_log_x =
        search_context_builder::Build("", "pp:/a/log**x", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_log_x).has_value());

    // pp:a**bcd -> tail "bcd" >= 3 chars -> extracts "bcd" (1)
    const SearchContext ctx_a_bcd =
        search_context_builder::Build("", "pp:a**bcd", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_a_bcd = filename_trigram::PlanTrigrams(ctx_a_bcd);
    REQUIRE(plan_a_bcd.has_value());
    CHECK(plan_a_bcd->size() == 1);
    const auto bcd_trigram = filename_trigram::PackTrigram('b', 'c', 'd');
    CHECK(std::find(plan_a_bcd->begin(), plan_a_bcd->end(), bcd_trigram) != plan_a_bcd->end());

    // Leaf without **: pp:/a/bcd -> unchanged full extraction
    const SearchContext ctx_a_bcd_no_dd =
        search_context_builder::Build("", "pp:/a/bcd", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_no_dd = filename_trigram::PlanTrigrams(ctx_a_bcd_no_dd);
    REQUIRE(plan_no_dd.has_value());
    CHECK(plan_no_dd->size() == 1);
    CHECK(std::find(plan_no_dd->begin(), plan_no_dd->end(), bcd_trigram) != plan_no_dd->end());

    // F2: glob with separator on path side -> nullopt
    const SearchContext ctx_glob_sep_path =
        search_context_builder::Build("", "src/*.cpp", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_glob_sep_path).has_value());

    const SearchContext ctx_glob_slash_path =
        search_context_builder::Build("", "/src/*.cpp", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_glob_slash_path).has_value());

    // Separator glob on filename side still extracts leaf: leaf "*.cpp" -> ".cpp" (2 trigrams: .cp, cpp)
    const SearchContext ctx_glob_sep_fn =
        search_context_builder::Build("src/*.cpp", "", nullptr, ItemTypeFilter::All, false, nullptr);
    const auto plan_glob_sep_fn = filename_trigram::PlanTrigrams(ctx_glob_sep_fn);
    REQUIRE(plan_glob_sep_fn.has_value());
    CHECK(plan_glob_sep_fn->size() == 2);

    // F3: '+' in leaf pattern metacharacters -> nullopt
    const SearchContext ctx_plus_leaf =
        search_context_builder::Build("", "pp:/x/log+", nullptr, ItemTypeFilter::All, false, nullptr);
    CHECK(!filename_trigram::PlanTrigrams(ctx_plus_leaf).has_value());
  }

}

