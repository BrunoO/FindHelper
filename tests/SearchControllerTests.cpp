// Mark the test thread as UI thread so UIThreadOwned<T> assert()s pass in tests.
// All GuiState mutations in these tests happen on this (single test) thread.
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

#include <chrono>
#include <memory>
#include <thread>

#include "core/Settings.h"
#include "gui/GuiState.h"
#include "index/FileIndex.h"
#include "search/SearchController.h"
#include "search/SearchWorker.h"
#include "utils/ThreadUtils.h"

int main(int argc, char** argv) {
  MarkCurrentThreadAsUI();
  doctest::Context context(argc, argv);
  return context.run();
}

TEST_CASE("SearchController - RepeatedAutoRefreshKeepsResultPoolCapacityBounded") {
  auto file_index = std::make_unique<FileIndex>();
  file_index->InsertPath("C:\\monkey1.pdf");
  file_index->InsertPath("C:\\monkey2.pdf");
  file_index->InsertPath("C:\\other_file.txt");

  auto search_worker = std::make_unique<SearchWorker>(*file_index);
  GuiState state;
  AppSettings settings;
  SearchController controller;

  state.searchCriteria.auto_refresh = true;
  state.searchCriteria.filename_input.SetValue("monkey*pdf");
  state.searchCriteria.path_input.SetValue("pp:**");
  state.searchCriteria.instant_search = false;

  // Trigger initial manual search
  controller.TriggerManualSearch(state, *search_worker, nullptr, settings);

  // Wait for worker to complete
  while (search_worker->IsBusy()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  // Poll results to populate front buffer
  controller.Update(state, *search_worker, nullptr,
                    {file_index->GetTotalItems(), file_index->MutationCount(), false},
                    settings, *file_index);

  REQUIRE(state.result_pool_->Results().size() == 2U);
  const size_t initial_results_cap = state.result_pool_->Results().capacity();
  const size_t initial_pool_cap = state.result_pool_->Pool().capacity();

  // Simulate 100 auto-refresh iterations with changing index version and auto-refresh triggers
  for (size_t i = 0; i < 100; ++i) {
    // Advance index size and mutation version to bypass the trigger checks
    size_t fake_index_size = file_index->GetTotalItems() + i + 1;
    uint64_t fake_mutation = file_index->MutationCount() + i + 1;
    // Set last auto-refresh time into the past to bypass 500ms cooldown
    state.search_pipeline.last_auto_refresh_time = std::chrono::steady_clock::now() - std::chrono::seconds(2);

    controller.Update(state, *search_worker, nullptr,
                      {fake_index_size, fake_mutation, false},
                      settings, *file_index);

    while (search_worker->IsBusy()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    controller.Update(state, *search_worker, nullptr,
                      {fake_index_size, fake_mutation, false},
                      settings, *file_index);

    REQUIRE(state.result_pool_->Results().size() == 2U);
  }

  // Verify that result pool capacities remain bounded and do not grow uncontrollably
  CHECK(state.result_pool_->Results().capacity() <= initial_results_cap * 2 + 16);
  CHECK(state.result_pool_->Pool().capacity() <= initial_pool_cap * 2 + 256);

  // Quiesce: disarm auto-refresh and wait for any in-flight search to finish so the
  // worker thread never outlives the stack objects its search params captured
  // (settings / file_index references) before the test scope unwinds.
  state.searchCriteria.auto_refresh = false;
  state.input_debounce.last_input_time = std::chrono::steady_clock::now();
  while (search_worker->IsBusy()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

TEST_CASE("SearchController - ClearSearchResultsShrinksCapacityToFit") {
  auto file_index = std::make_unique<FileIndex>();
  file_index->InsertPath("C:\\monkey1.pdf");
  file_index->InsertPath("C:\\monkey2.pdf");

  auto search_worker = std::make_unique<SearchWorker>(*file_index);
  GuiState state;
  AppSettings settings;
  SearchController controller;

  state.searchCriteria.auto_refresh = false;
  state.searchCriteria.filename_input.SetValue("monkey*pdf");
  state.searchCriteria.path_input.SetValue("pp:**");

  controller.TriggerManualSearch(state, *search_worker, nullptr, settings);

  while (search_worker->IsBusy()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  // Poll results
  controller.Update(state, *search_worker, nullptr,
                    {file_index->GetTotalItems(), file_index->MutationCount(), false},
                    settings, *file_index);

  REQUIRE(state.result_pool_->Results().capacity() > 0U);

  // Request clear
  state.search_pipeline.clear_results_requested = true;

  controller.Update(state, *search_worker, nullptr,
                    {file_index->GetTotalItems(), file_index->MutationCount(), false},
                    settings, *file_index);

  CHECK(state.result_pool_->Results().size() == 0U);
  CHECK(state.result_pool_->Results().capacity() == 0U);
  CHECK(state.result_pool_->Pool().size() == 0U);
  CHECK(state.result_pool_->Pool().capacity() == 0U);
}

TEST_CASE("SearchController - AutoRefreshArmsOnEmptyResultsWithQueryText") {
  // Rare/transient targets (e.g. hex temp names): the first search is empty,
  // but typed query text still arms auto-refresh so hits appear without a
  // manual re-search. A fully empty query box stays unarmed.
  auto file_index = std::make_unique<FileIndex>();
  file_index->InsertPath("C:\\existing.txt");

  auto search_worker = std::make_unique<SearchWorker>(*file_index);
  GuiState state;
  AppSettings settings;
  SearchController controller;

  state.searchCriteria.auto_refresh = true;
  state.searchCriteria.instant_search = false;
  state.searchCriteria.filename_input.SetValue("a1b2c3nomatch");
  state.search_pipeline.last_auto_refresh_time =
      std::chrono::steady_clock::now() - std::chrono::seconds(2);

  // Empty results + query text + moved version => refresh triggers a search.
  controller.Update(state, *search_worker, nullptr,
                    {file_index->GetTotalItems(), file_index->MutationCount() + 1, false},
                    settings, *file_index);
  CHECK(search_worker->IsBusy());

  // Quiesce before scope teardown (worker captures stack references).
  state.searchCriteria.auto_refresh = false;
  while (search_worker->IsBusy()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

TEST_CASE("SearchController - AutoRefreshStaysUnarmedOnEmptyQueryAndEmptyResults") {
  auto file_index = std::make_unique<FileIndex>();
  file_index->InsertPath("C:\\existing.txt");

  auto search_worker = std::make_unique<SearchWorker>(*file_index);
  GuiState state;
  AppSettings settings;
  SearchController controller;

  state.searchCriteria.auto_refresh = true;
  state.searchCriteria.instant_search = false;
  // No query text anywhere; results empty.
  state.search_pipeline.last_auto_refresh_time =
      std::chrono::steady_clock::now() - std::chrono::seconds(2);

  controller.Update(state, *search_worker, nullptr,
                    {file_index->GetTotalItems(), file_index->MutationCount() + 1, false},
                    settings, *file_index);
  CHECK_FALSE(search_worker->IsBusy());

  while (search_worker->IsBusy()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

TEST_CASE("SearchController - SingleEntryIndexIsSearchable") {
  // Regression: the hybrid strategy rounded initial chunks to empty for a
  // 1-entry index (total*percent/100 == 0) and launched zero tasks — silent
  // empty results. A single full-path entry must be found.
  auto file_index = std::make_unique<FileIndex>();
  constexpr uint64_t kOnly = 0x00010000000000B0ULL;
  file_index->Insert(ntfs_file_reference::NtfsFileReference(kOnly), ntfs_file_reference::NtfsFileReference(0), "onlyone.txt", false);
  REQUIRE(file_index->Size() == 1U);

  auto search_worker = std::make_unique<SearchWorker>(*file_index);
  GuiState state;
  AppSettings settings;
  SearchController controller;

  state.searchCriteria.auto_refresh = false;
  state.searchCriteria.instant_search = false;
  state.searchCriteria.filename_input.SetValue("onlyone*");
  controller.TriggerManualSearch(state, *search_worker, nullptr, settings);
  while (search_worker->IsBusy()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  controller.Update(state, *search_worker, nullptr,
                    {file_index->GetTotalItems(), file_index->MutationCount(), false},
                    settings, *file_index);
  CHECK(state.result_pool_->Results().size() == 1U);

  state.searchCriteria.auto_refresh = false;
  while (search_worker->IsBusy()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

TEST_CASE("SearchController - AutoRefreshFiresOnHealWithoutSizeChange") {
  // Heal/rename keeps Size() but bumps the mutation version: visible results
  // must refresh so healed paths replace stale bare entries.
  auto file_index = std::make_unique<FileIndex>();
  constexpr uint64_t kParent = 0x00010000000000A0ULL;
  constexpr uint64_t kChild = 0x00010000000000B0ULL;
  file_index->Insert(ntfs_file_reference::NtfsFileReference(kChild), ntfs_file_reference::NtfsFileReference(kParent), "a1b2c3d4e5f60718293a4b5c", false);

  auto search_worker = std::make_unique<SearchWorker>(*file_index);
  GuiState state;
  AppSettings settings;
  SearchController controller;

  state.searchCriteria.auto_refresh = true;
  state.searchCriteria.instant_search = false;
  state.searchCriteria.filename_input.SetValue("a1b2c3*");
  controller.TriggerManualSearch(state, *search_worker, nullptr, settings);
  while (search_worker->IsBusy()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  controller.Update(state, *search_worker, nullptr,
                    {file_index->GetTotalItems(), file_index->MutationCount(), false},
                    settings, *file_index);
  // Awaiting bare placeholder is quarantined from results before parent arrives
  REQUIRE(state.result_pool_->Results().size() == 0U);
  // Drain any refresh the first Update triggered (baseline started at 0),
  // then poll it so the baseline settles at the current version.
  while (search_worker->IsBusy()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  controller.Update(state, *search_worker, nullptr,
                    {file_index->GetTotalItems(), file_index->MutationCount(), false},
                    settings, *file_index);

  // Parent arrives: Size() grows by exactly the added parent (the child's
  // path heal is invisible to it) while the version moves => refresh must
  // trigger on the version, not the size.
  const size_t size_before = file_index->Size();
  file_index->Insert(ntfs_file_reference::NtfsFileReference(kParent), ntfs_file_reference::NtfsFileReference(0), "LateDir", true);
  CHECK(file_index->Size() == size_before + 1);
  state.search_pipeline.last_auto_refresh_time =
      std::chrono::steady_clock::now() - std::chrono::seconds(2);
  controller.Update(state, *search_worker, nullptr,
                    {file_index->Size(), file_index->MutationCount(), false},
                    settings, *file_index);
  CHECK(search_worker->IsBusy());

  while (search_worker->IsBusy()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  controller.Update(state, *search_worker, nullptr,
                    {file_index->Size(), file_index->MutationCount(), false},
                    settings, *file_index);

  // Now the child has been healed from a bare placeholder to a full path,
  // so it is no longer quarantined by ShouldQuarantineHit / IsAwaitingChild.
  CHECK(state.result_pool_->Results().size() == 1U);

  state.searchCriteria.auto_refresh = false;
  // Quiesce before scope teardown: the worker/search-pool tasks capture
  // file_index / settings by reference (rule: drain IsBusy before unwind).
  while (search_worker->IsBusy()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}
