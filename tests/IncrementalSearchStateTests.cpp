#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <list>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "search/SearchTypes.h"
#include "ui/IncrementalSearchState.h"

namespace {

struct ResultStore {
  std::list<std::string> paths;

  SearchResult MakeResult(const std::string& path) {
    paths.push_back(path);
    SearchResult result{};
    result.fullPath = paths.back();
    result.isDirectory = false;
    result.fileId = 0;
    return result;
  }
};

struct SearchStateFixture {
  ResultStore store;
  ui::IncrementalSearchState state;
  std::vector<SearchResult> results;
  int selected_row = -1;
  bool scroll_to_selected = false;
};

}  // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Aggregated doctest suite with many small cases
TEST_SUITE("IncrementalSearchState") {  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - doctest macro expands to a global test suite object with required name

  TEST_CASE("Default construction") {
    const ui::IncrementalSearchState state;
    CHECK_FALSE(state.IsPromptVisible());
    CHECK_FALSE(state.IsFilterActive());
    CHECK_EQ(state.MatchCount(), 0);
    CHECK(state.Query().empty());
  }

  TEST_CASE_FIXTURE(SearchStateFixture, "Begin sets prompt visible") {
    results.push_back(store.MakeResult("folder/file.txt"));

    constexpr float kInitialScrollY = 10.0F;
    state.Begin(results, selected_row, kInitialScrollY, 1U);

    CHECK(state.IsPromptVisible());
    CHECK_FALSE(state.IsFilterActive());
    CHECK_EQ(state.MatchCount(), 0);
  }

  // NOLINTNEXTLINE(readability-function-cognitive-complexity) - Aggregated doctest case with multiple checks
  TEST_CASE_FIXTURE(SearchStateFixture, "UpdateQuery - matching query") {
    results.push_back(store.MakeResult("folder/foo.txt"));
    results.push_back(store.MakeResult("other/bar.txt"));

    state.Begin(results, selected_row, 0.0F, 1U);

    state.UpdateQuery("foo", results, selected_row, scroll_to_selected);

    CHECK(state.IsFilterActive());
    CHECK_EQ(state.MatchCount(), 1);
    const auto& filtered = state.FilteredResults();
    REQUIRE_EQ(filtered.size(), 1U);
    CHECK(filtered[0].fullPath == std::string_view("folder/foo.txt"));
    CHECK_EQ(selected_row, 0);
    CHECK(scroll_to_selected);
  }

  TEST_CASE_FIXTURE(SearchStateFixture, "UpdateQuery - empty query clears filter") {
    results.push_back(store.MakeResult("folder/foo.txt"));

    state.Begin(results, selected_row, 0.0F, 1U);

    state.UpdateQuery("foo", results, selected_row, scroll_to_selected);
    CHECK(state.IsFilterActive());

    state.UpdateQuery("", results, selected_row, scroll_to_selected);
    CHECK_FALSE(state.IsFilterActive());
    CHECK_EQ(state.MatchCount(), 0);
    CHECK_EQ(selected_row, -1);
  }

  TEST_CASE_FIXTURE(SearchStateFixture, "UpdateQuery - no matches") {
    results.push_back(store.MakeResult("folder/foo.txt"));

    state.Begin(results, selected_row, 0.0F, 1U);

    state.UpdateQuery("zzz", results, selected_row, scroll_to_selected);
    CHECK(state.IsFilterActive());
    CHECK_EQ(state.MatchCount(), 0);
    CHECK_EQ(selected_row, -1);
    CHECK_FALSE(scroll_to_selected);
  }

  TEST_CASE_FIXTURE(SearchStateFixture, "NavigateNext wraps") {
    results.push_back(store.MakeResult("folder/a.txt"));
    results.push_back(store.MakeResult("folder/b.txt"));

    state.Begin(results, selected_row, 0.0F, 1U);
    state.UpdateQuery("folder", results, selected_row, scroll_to_selected);

    CHECK_EQ(state.MatchCount(), 2);
    CHECK_EQ(selected_row, 0);

    state.NavigateNext(selected_row, scroll_to_selected);
    CHECK_EQ(selected_row, 1);
    CHECK(scroll_to_selected);

    state.NavigateNext(selected_row, scroll_to_selected);
    CHECK_EQ(selected_row, 0);
  }

  TEST_CASE_FIXTURE(SearchStateFixture, "NavigatePrev wraps") {
    results.push_back(store.MakeResult("folder/a.txt"));
    results.push_back(store.MakeResult("folder/b.txt"));

    state.Begin(results, selected_row, 0.0F, 1U);
    state.UpdateQuery("folder", results, selected_row, scroll_to_selected);

    // Move to last match first
    state.NavigatePrev(selected_row, scroll_to_selected);
    CHECK_EQ(selected_row, 1);

    state.NavigatePrev(selected_row, scroll_to_selected);
    CHECK_EQ(selected_row, 0);
  }

  TEST_CASE_FIXTURE(SearchStateFixture, "Accept - non-empty query keeps filter") {
    results.push_back(store.MakeResult("folder/foo.txt"));

    state.Begin(results, selected_row, 0.0F, 1U);
    state.UpdateQuery("foo", results, selected_row, scroll_to_selected);

    state.Accept();

    CHECK_FALSE(state.IsPromptVisible());
    CHECK(state.IsFilterActive());
    CHECK_EQ(state.MatchCount(), 1);
  }

  TEST_CASE_FIXTURE(SearchStateFixture, "Accept - empty query clears filter") {
    results.push_back(store.MakeResult("folder/foo.txt"));

    state.Begin(results, selected_row, 0.0F, 1U);

    state.Accept();
    CHECK_FALSE(state.IsPromptVisible());
    CHECK_FALSE(state.IsFilterActive());
    CHECK_EQ(state.MatchCount(), 0);
  }

  // NOLINTNEXTLINE(readability-function-cognitive-complexity) - Aggregated doctest case with multiple checks
  TEST_CASE_FIXTURE(SearchStateFixture, "Cancel restores selection and scroll") {
    results.push_back(store.MakeResult("folder/foo.txt"));
    results.push_back(store.MakeResult("folder/bar.txt"));

    selected_row = 1;
    const float original_scroll = 42.0F;
    state.Begin(results, selected_row, original_scroll, 1U);

    // Change selection via query
    state.UpdateQuery("foo", results, selected_row, scroll_to_selected);
    CHECK_EQ(selected_row, 0);

    state.Cancel(selected_row, scroll_to_selected);
    CHECK_FALSE(state.IsPromptVisible());
    CHECK_FALSE(state.IsFilterActive());
    CHECK_EQ(selected_row, 1);
    CHECK(scroll_to_selected);

    float restored_y = 0.0F;
    CHECK(state.ConsumeScrollRestorePending(restored_y));
    CHECK_EQ(restored_y, original_scroll);
    CHECK_FALSE(state.ConsumeScrollRestorePending(restored_y));
  }

  // NOLINTNEXTLINE(readability-function-cognitive-complexity) - Aggregated doctest case with multiple checks
  TEST_CASE_FIXTURE(SearchStateFixture, "CheckBatchNumber resets on change") {
    results.push_back(store.MakeResult("folder/foo.txt"));

    state.Begin(results, selected_row, 0.0F, 1U);
    state.UpdateQuery("foo", results, selected_row, scroll_to_selected);

    CHECK(state.IsPromptVisible());
    CHECK(state.IsFilterActive());

    state.CheckBatchNumber(2U, selected_row);

    CHECK_FALSE(state.IsPromptVisible());
    CHECK_FALSE(state.IsFilterActive());
    CHECK_EQ(state.MatchCount(), 0);
    CHECK_EQ(selected_row, -1);
  }

  TEST_CASE_FIXTURE(SearchStateFixture, "CheckBatchNumber no-op when unchanged") {
    results.push_back(store.MakeResult("folder/foo.txt"));

    constexpr unsigned kBatchNumber = 5U;
    state.Begin(results, selected_row, 0.0F, kBatchNumber);
    state.UpdateQuery("foo", results, selected_row, scroll_to_selected);

    state.CheckBatchNumber(kBatchNumber, selected_row);

    CHECK(state.IsFilterActive());
    CHECK_EQ(state.MatchCount(), 1);
  }

  TEST_CASE_FIXTURE(SearchStateFixture, "IncrementalSearchMatches - case insensitive") {
    const SearchResult result = store.MakeResult("dir/FoObAr.txt");
    CHECK(ui::IncrementalSearchMatches("foo", result));
    CHECK(ui::IncrementalSearchMatches("FOO", result));
  }

  TEST_CASE_FIXTURE(SearchStateFixture, "IncrementalSearchMatches - empty query matches all") {
    const SearchResult result = store.MakeResult("anything.txt");
    CHECK(ui::IncrementalSearchMatches("", result));
  }

  TEST_CASE_FIXTURE(SearchStateFixture, "IncrementalSearchMatches - dotfile matched by basename") {
    const SearchResult result = store.MakeResult("/home/user/.bashrc");
    CHECK(ui::IncrementalSearchMatches("bash", result));
  }

  TEST_CASE_FIXTURE(SearchStateFixture, "IncrementalSearchMatches - volume root directory kept") {
    SUBCASE("Windows drive-root matches C:\\ in directory") {
      const SearchResult result = store.MakeResult(R"(C:\file.txt)");
      CHECK(ui::IncrementalSearchMatches(R"(C:\)", result));
      CHECK(ui::IncrementalSearchMatches("C:", result));
      CHECK(ui::IncrementalSearchMatches("file", result));
      CHECK_FALSE(ui::IncrementalSearchMatches("Users", result));
    }

    SUBCASE("Unix root matches / in directory") {
      const SearchResult result = store.MakeResult("/file.txt");
      CHECK(ui::IncrementalSearchMatches("/", result));
      CHECK(ui::IncrementalSearchMatches("file", result));
    }

    SUBCASE("nested path still matches directory segment") {
      const SearchResult result = store.MakeResult(R"(C:\Users\docs\file.txt)");
      CHECK(ui::IncrementalSearchMatches("Users", result));
      CHECK(ui::IncrementalSearchMatches("docs", result));
    }
  }

}  // TEST_SUITE

