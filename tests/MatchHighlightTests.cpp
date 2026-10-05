/**
 * @file MatchHighlightTests.cpp
 * @brief Unit tests for Name-column highlight span union and Extensions allowlist highlighting.
 */

#include <vector>

#include "doctest/doctest.h"
#include "gui/GuiState.h"
#include "ui/MatchHighlight.h"

TEST_SUITE("MatchHighlight Name column union") {

  TEST_CASE("PathPattern path query highlights filename literals") {
    const ui::ColumnHighlightQuery name_query{};
    const ui::ColumnHighlightQuery path_query{ui::ColumnHighlightKind::PathPattern, "**Monkey*pdf",
                                              false,
    };
    std::vector<string_search::MatchSpan> spans;
    CHECK(ui::FindNameColumnMatchSpans("SuperMonkeyReport.pdf", name_query, path_query, spans));
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].start_ == 5);
    CHECK(spans[0].end_ == 11);
    CHECK(spans[1].start_ == 18);
    CHECK(spans[1].end_ == 21);
  }

  TEST_CASE("Name substring and PathPattern spans merge") {
    const ui::ColumnHighlightQuery name_query{ui::ColumnHighlightKind::Substring, "Monkey", true};
    const ui::ColumnHighlightQuery path_query{ui::ColumnHighlightKind::PathPattern, "**Monkey*pdf",
                                              true,
    };
    std::vector<string_search::MatchSpan> spans;
    CHECK(ui::FindNameColumnMatchSpans("SuperMonkeyReport.pdf", name_query, path_query, spans));
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].start_ == 5);
    CHECK(spans[0].end_ == 11);
    CHECK(spans[1].start_ == 18);
    CHECK(spans[1].end_ == 21);
  }

  TEST_CASE("identical Name and Path queries are applied once") {
    const ui::ColumnHighlightQuery shared{ui::ColumnHighlightKind::Substring, "foo", true};
    std::vector<string_search::MatchSpan> spans;
    CHECK(ui::FindNameColumnMatchSpans("foobar", shared, shared, spans));
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].start_ == 0);
    CHECK(spans[0].end_ == 3);
  }

  TEST_CASE("Path query that only matches a directory yields no filename spans") {
    const ui::ColumnHighlightQuery name_query{};
    const ui::ColumnHighlightQuery path_query{ui::ColumnHighlightKind::Substring, "docs", true};
    std::vector<string_search::MatchSpan> spans;
    CHECK_FALSE(ui::FindNameColumnMatchSpans("notes.txt", name_query, path_query, spans));
    CHECK(spans.empty());
  }
}

TEST_SUITE("MatchHighlight Extension allowlist") {

  TEST_CASE("empty allowlist is inactive and matches nothing") {
    const ui::ExtensionHighlightQuery query{};
    CHECK_FALSE(query.IsActive());
    CHECK_FALSE(ui::ExtensionAllowlistMatches("cpp", query));
    CHECK_FALSE(ui::ExtensionAllowlistMatches("", query));
  }

  TEST_CASE("BuildExtensionHighlightQuery parses dots semicolons and case") {
    GuiState state;
    state.searchCriteria.extension_input.SetValue("CPP; .H ;;txt");
    const ui::ExtensionHighlightQuery query = ui::BuildExtensionHighlightQuery(state);
    CHECK(query.IsActive());
    CHECK(ui::ExtensionAllowlistMatches("cpp", query));
    CHECK(ui::ExtensionAllowlistMatches("H", query));
    CHECK(ui::ExtensionAllowlistMatches("TXT", query));
    CHECK_FALSE(ui::ExtensionAllowlistMatches("md", query));
    CHECK_FALSE(ui::ExtensionAllowlistMatches("", query));
  }

  TEST_CASE("empty Extensions field yields inactive query") {
    GuiState state;
    state.searchCriteria.extension_input.Clear();
    const ui::ExtensionHighlightQuery query = ui::BuildExtensionHighlightQuery(state);
    CHECK_FALSE(query.IsActive());
  }
}
