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

  TEST_CASE("Begin sets prompt visible") {
    ResultStore store;
    ui::IncrementalSearchState state;
    std::vector<SearchResult> results;
    results.push_back(store.MakeResult("folder/file.txt"));

    const int selected_row = -1;
    constexpr float kInitialScrollY = 10.0F;
    state.Begin(results, selected_row, kInitialScrollY, 1U);

    CHECK(state.IsPromptVisible());
    CHECK_FALSE(state.IsFilterActive());
    CHECK_EQ(state.MatchCount(), 0);
  }

  // NOLINTNEXTLINE(readability-function-cognitive-complexity) - Aggregated doctest case with multiple checks
  TEST_CASE("UpdateQuery - matching query") {
    ResultStore store;
    ui::IncrementalSearchState state;
    std::vector<SearchResult> results;
    results.push_back(store.MakeResult("folder/foo.txt"));
    results.push_back(store.MakeResult("other/bar.txt"));

    int selected_row = -1;
    bool scroll_to_selected = false;
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

  TEST_CASE("UpdateQuery - empty query clears filter") {
    ResultStore store;
    ui::IncrementalSearchState state;
    std::vector<SearchResult> results;
    results.push_back(store.MakeResult("folder/foo.txt"));

    int selected_row = -1;
    bool scroll_to_selected = false;
    state.Begin(results, selected_row, 0.0F, 1U);

    state.UpdateQuery("foo", results, selected_row, scroll_to_selected);
    CHECK(state.IsFilterActive());

    state.UpdateQuery("", results, selected_row, scroll_to_selected);
    CHECK_FALSE(state.IsFilterActive());
    CHECK_EQ(state.MatchCount(), 0);
    CHECK_EQ(selected_row, -1);
  }

  TEST_CASE("UpdateQuery - no matches") {
    ResultStore store;
    ui::IncrementalSearchState state;
    std::vector<SearchResult> results;
    results.push_back(store.MakeResult("folder/foo.txt"));

    int selected_row = -1;
    bool scroll_to_selected = false;
    state.Begin(results, selected_row, 0.0F, 1U);

    state.UpdateQuery("zzz", results, selected_row, scroll_to_selected);
    CHECK(state.IsFilterActive());
    CHECK_EQ(state.MatchCount(), 0);
    CHECK_EQ(selected_row, -1);
    CHECK_FALSE(scroll_to_selected);
  }

  TEST_CASE("NavigateNext wraps") {
    ResultStore store;
    ui::IncrementalSearchState state;
    std::vector<SearchResult> results;
    results.push_back(store.MakeResult("folder/a.txt"));
    results.push_back(store.MakeResult("folder/b.txt"));

    int selected_row = -1;
    bool scroll_to_selected = false;
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

  TEST_CASE("NavigatePrev wraps") {
    ResultStore store;
    ui::IncrementalSearchState state;
    std::vector<SearchResult> results;
    results.push_back(store.MakeResult("folder/a.txt"));
    results.push_back(store.MakeResult("folder/b.txt"));

    int selected_row = -1;
    bool scroll_to_selected = false;
    state.Begin(results, selected_row, 0.0F, 1U);
    state.UpdateQuery("folder", results, selected_row, scroll_to_selected);

    // Move to last match first
    state.NavigatePrev(selected_row, scroll_to_selected);
    CHECK_EQ(selected_row, 1);

    state.NavigatePrev(selected_row, scroll_to_selected);
    CHECK_EQ(selected_row, 0);
  }

  TEST_CASE("Accept - non-empty query keeps filter") {
    ResultStore store;
    ui::IncrementalSearchState state;
    std::vector<SearchResult> results;
    results.push_back(store.MakeResult("folder/foo.txt"));

    int selected_row = -1;
    bool scroll_to_selected = false;
    state.Begin(results, selected_row, 0.0F, 1U);
    state.UpdateQuery("foo", results, selected_row, scroll_to_selected);

    state.Accept();

    CHECK_FALSE(state.IsPromptVisible());
    CHECK(state.IsFilterActive());
    CHECK_EQ(state.MatchCount(), 1);
  }

  TEST_CASE("Accept - empty query clears filter") {
    ResultStore store;
    ui::IncrementalSearchState state;
    std::vector<SearchResult> results;
    results.push_back(store.MakeResult("folder/foo.txt"));

    const int selected_row = -1;
    state.Begin(results, selected_row, 0.0F, 1U);

    state.Accept();
    CHECK_FALSE(state.IsPromptVisible());
    CHECK_FALSE(state.IsFilterActive());
    CHECK_EQ(state.MatchCount(), 0);
  }

  // NOLINTNEXTLINE(readability-function-cognitive-complexity) - Aggregated doctest case with multiple checks
  TEST_CASE("Cancel restores selection and scroll") {
    ResultStore store;
    ui::IncrementalSearchState state;
    std::vector<SearchResult> results;
    results.push_back(store.MakeResult("folder/foo.txt"));
    results.push_back(store.MakeResult("folder/bar.txt"));

    int selected_row = 1;
    bool scroll_to_selected = false;
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
  TEST_CASE("CheckBatchNumber resets on change") {
    ResultStore store;
    ui::IncrementalSearchState state;
    std::vector<SearchResult> results;
    results.push_back(store.MakeResult("folder/foo.txt"));

    int selected_row = -1;
    bool scroll_to_selected = false;
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

  TEST_CASE("CheckBatchNumber no-op when unchanged") {
    ResultStore store;
    ui::IncrementalSearchState state;
    std::vector<SearchResult> results;
    results.push_back(store.MakeResult("folder/foo.txt"));

    int selected_row = -1;
    bool scroll_to_selected = false;
    constexpr unsigned kBatchNumber = 5U;
    state.Begin(results, selected_row, 0.0F, kBatchNumber);
    state.UpdateQuery("foo", results, selected_row, scroll_to_selected);

    state.CheckBatchNumber(kBatchNumber, selected_row);

    CHECK(state.IsFilterActive());
    CHECK_EQ(state.MatchCount(), 1);
  }

  TEST_CASE("IncrementalSearchMatches - case insensitive") {
    ResultStore store;
    const SearchResult result = store.MakeResult("dir/FoObAr.txt");
    CHECK(ui::IncrementalSearchMatches("foo", result));
    CHECK(ui::IncrementalSearchMatches("FOO", result));
  }

  TEST_CASE("IncrementalSearchMatches - empty query matches all") {
    ResultStore store;
    const SearchResult result = store.MakeResult("anything.txt");
    CHECK(ui::IncrementalSearchMatches("", result));
  }

  TEST_CASE("IncrementalSearchMatches - dotfile matched by basename") {
    ResultStore store;
    const SearchResult result = store.MakeResult("/home/user/.bashrc");
    CHECK(ui::IncrementalSearchMatches("bash", result));
  }

}  // TEST_SUITE

