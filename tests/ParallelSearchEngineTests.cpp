#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

// Enable test mode for non-Windows platforms
#ifndef _WIN32
#ifndef USN_WINDOWS_TESTS
#define USN_WINDOWS_TESTS
#endif  // USN_WINDOWS_TESTS
#endif  // _WIN32

#include "search/ParallelSearchEngine.h"
#include "search/SearchContext.h"
#include "search/SearchThreadPool.h"
#include "MockSearchableIndex.h"
#include "index/FileIndex.h"
#include "utils/Logger.h"
#include "TestHelpers.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <set>
#include <string>
#include <vector>

// Helper functions for test data setup
namespace {

/**
 * Create a simple test index with a few files
 */
void PopulateSimpleIndex(MockSearchableIndex& index) {
  // Root directory
  index.AddEntry(1, 0, "C:\\", true, "C:\\");

  // Test files
  index.AddEntry(2, 1, "test.txt", false, "C:\\test.txt");
  index.AddEntry(3, 1, "example.cpp", false, "C:\\example.cpp");
  index.AddEntry(4, 1, "readme.md", false, "C:\\readme.md");
  index.AddEntry(5, 1, "data.json", false, "C:\\data.json");
}

/**
 * Create a larger test index for performance testing
 */
void PopulateLargeIndex(MockSearchableIndex& index, size_t file_count) {
  // Root directory
  index.AddEntry(1, 0, "C:\\", true, "C:\\");

  // Create files with predictable names
  for (size_t i = 1; i <= file_count; ++i) {
    uint64_t file_id = i + 1;
    std::string name = "file_" + std::to_string(i) + ".txt";
    std::string path = "C:\\" + name;
    index.AddEntry(file_id, 1, name, false, path);
  }
}

// CreateSimpleSearchContext, CreateExtensionSearchContext, CollectResults, and CollectResultData
// are now provided by test_helpers namespace
using test_helpers::CreateSimpleSearchContext;
using test_helpers::CreateExtensionSearchContext;

// Wrapper function kept as a thin adapter around test_helpers for existing tests
std::vector<uint64_t> CollectResults(
    std::vector<std::future<std::vector<uint64_t>>>& futures) {
  return test_helpers::CollectFutures(futures);
}

} // anonymous namespace

TEST_SUITE("ParallelSearchEngine - SearchAsync") {

  TEST_CASE("SearchAsync - empty index returns no results") {
    MockSearchableIndex index;
    test_helpers::TestParallelSearchEngineFixture fixture;

    SearchContext ctx = CreateSimpleSearchContext("test");
    auto futures = fixture.GetEngine().SearchAsync(index, "test", -1, ctx);

    CHECK(futures.empty());
  }

  TEST_CASE("SearchAsync - simple filename search") {
    MockSearchableIndex index;
    PopulateSimpleIndex(index);

    test_helpers::TestParallelSearchEngineFixture fixture;

    SearchContext ctx = CreateSimpleSearchContext("test");
    auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchAndCollect(
        fixture.GetEngine(), index, "test", -1, ctx);

    CHECK(results.size() == 1);
    CHECK(std::find(results.begin(), results.end(), 2) != results.end());
  }

  TEST_CASE("SearchAsync - case-insensitive search") {
    MockSearchableIndex index;
    PopulateSimpleIndex(index);

    test_helpers::TestParallelSearchEngineFixture fixture;

    SearchContext ctx = CreateSimpleSearchContext("TEST", false);
    auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchAndCollect(
        fixture.GetEngine(), index, "TEST", -1, ctx);

    CHECK(results.size() == 1);
    CHECK(std::find(results.begin(), results.end(), 2) != results.end());
  }

  TEST_CASE("SearchAsync - case-sensitive search") {
    MockSearchableIndex index;
    PopulateSimpleIndex(index);

    test_helpers::TestParallelSearchEngineFixture fixture;

    SearchContext ctx = CreateSimpleSearchContext("TEST", true);
    auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchAndCollect(
        fixture.GetEngine(), index, "TEST", -1, ctx);

    // Case-sensitive search for "TEST" should not match "test.txt"
    CHECK(results.empty());
  }

  TEST_CASE("SearchAsync - extension filter") {
    MockSearchableIndex index;
    PopulateSimpleIndex(index);

    test_helpers::TestParallelSearchEngineFixture fixture;

    SearchContext ctx = CreateExtensionSearchContext({".txt", ".md"});
    auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchAndCollect(
        fixture.GetEngine(), index, "", -1, ctx);

    CHECK(results.size() == 2);
    CHECK(std::find(results.begin(), results.end(), 2) != results.end()); // test.txt
    CHECK(std::find(results.begin(), results.end(), 4) != results.end()); // readme.md
  }

  TEST_CASE("SearchAsync - multiple threads") {
    MockSearchableIndex index;
    PopulateLargeIndex(index, 100);

    test_helpers::TestParallelSearchEngineFixture fixture;

    SearchContext ctx = CreateSimpleSearchContext("file");
    auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchAndCollect(
        fixture.GetEngine(), index, "file", 4, ctx);

    CHECK(results.size() == 100); // All files match "file"
  }

  TEST_CASE("SearchAsync - cancellation") {
    MockSearchableIndex index;
    PopulateLargeIndex(index, 1000);

    test_helpers::TestParallelSearchEngineFixture fixture;

    std::atomic cancel_flag(false);
    SearchContext ctx = CreateSimpleSearchContext("file");
    ctx.cancel_flag = &cancel_flag;

    // Start search
    auto futures = fixture.GetEngine().SearchAsync(index, "file", 4, ctx);

    // Cancel immediately
    cancel_flag = true;

    // Wait for results (should be partial or empty due to cancellation)
    auto results = CollectResults(futures);
    // Results may be empty or partial depending on cancellation timing
    CHECK(results.size() <= 1000);
  }

  TEST_CASE("SearchAsync - search stats") {
    MockSearchableIndex index;
    PopulateSimpleIndex(index);

    test_helpers::TestParallelSearchEngineFixture fixture;

    SearchContext ctx = CreateSimpleSearchContext("test");
    SearchStats stats;
    auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchWithStatsAndCollect(
        fixture.GetEngine(), index, "test", -1, ctx, stats);

    CHECK(stats.total_matches_found_ == 1);
    CHECK(stats.total_items_scanned_ > 0);
    CHECK(stats.num_threads_used_ > 0);
    CHECK(stats.duration_milliseconds_ >= 0.0);
  }
}

TEST_SUITE("ParallelSearchEngine - SearchAsyncWithData") {

  TEST_CASE("SearchAsyncWithData - empty index returns no results") {
    MockSearchableIndex index;
    test_helpers::TestParallelSearchEngineFixture fixture;

    SearchContext ctx = CreateSimpleSearchContext("test");
    auto futures = fixture.GetEngine().SearchAsyncWithData(index, "test", -1, ctx);

    CHECK(futures.empty());
  }

  TEST_CASE("SearchAsyncWithData - returns full path data") {
    MockSearchableIndex index;
    PopulateSimpleIndex(index);

    test_helpers::TestParallelSearchEngineFixture fixture;

    SearchContext ctx = CreateSimpleSearchContext("test");
    auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchWithDataAndCollect(
        fixture.GetEngine(), index, "test", -1, ctx);

    CHECK(results.size() == 1);
    CHECK(results[0].id == 2);
    CHECK(results[0].fullPath == "C:\\test.txt");
    CHECK(results[0].isDirectory == false);
  }

  TEST_CASE("SearchAsyncWithData - thread timings") {
    MockSearchableIndex index;
    PopulateLargeIndex(index, 100);

    test_helpers::TestParallelSearchEngineFixture fixture;

    SearchContext ctx = CreateSimpleSearchContext("file");
    std::vector<ThreadTiming> thread_timings;
    auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchWithDataAndTimings(
        fixture.GetEngine(), index, "file", 4, ctx, thread_timings);

    CHECK(thread_timings.size() > 0);
    CHECK(thread_timings.size() <= 4); // At most 4 threads
    for (const auto& timing : thread_timings) {
      CHECK(timing.thread_index_ < 4);
      CHECK(timing.items_processed_ > 0);
    }
  }
}

TEST_SUITE("ParallelSearchEngine - DetermineThreadCount") {

  TEST_CASE("DetermineThreadCount - auto-detection") {
    test_helpers::TestParallelSearchEngineFixture fixture;

    // Small dataset should use fewer threads
    int count = ParallelSearchEngine::DetermineThreadCount(-1, 1000);
    CHECK(count >= 1);
    // Allow up to hardware concurrency (test may run on systems with > 4 cores)
    unsigned int hw_concurrency = std::thread::hardware_concurrency();
    if (hw_concurrency == 0) hw_concurrency = 4; // Fallback
    CHECK(count <= static_cast<int>(hw_concurrency));

    // Large dataset should use more threads (limited by data size and hardware)
    int count_large = ParallelSearchEngine::DetermineThreadCount(-1, 1000000);
    CHECK(count_large >= 1);
    // Allow up to hardware concurrency (test may run on systems with > 4 cores)
    CHECK(count_large <= 16);
  }

  TEST_CASE("DetermineThreadCount - manual thread count") {
    test_helpers::TestParallelSearchEngineFixture fixture;

    int count = ParallelSearchEngine::DetermineThreadCount(2, 1000000);
    CHECK(count == 2);
  }
}

TEST_SUITE("ParallelSearchEngine - CreatePatternMatchers") {

  TEST_CASE("CreatePatternMatchers - literal pattern") {
    SearchContext ctx = CreateSimpleSearchContext("test");
    auto matchers = ParallelSearchEngine::CreatePatternMatchers(ctx);

    CHECK(static_cast<bool>(matchers.filename_matcher));      // NOSONAR(cpp:S1905) - Explicit bool conversion documents intent
    CHECK(!static_cast<bool>(matchers.path_matcher));         // NOSONAR(cpp:S1905) - Explicit bool conversion documents intent
  }

  TEST_CASE("CreatePatternMatchers - extension-only mode") {
    SearchContext ctx = CreateExtensionSearchContext({".txt"});
    auto matchers = ParallelSearchEngine::CreatePatternMatchers(ctx);

    // Extension-only mode should not create matchers
    CHECK(!static_cast<bool>(matchers.filename_matcher));     // NOSONAR(cpp:S1905) - Explicit bool conversion documents intent
    CHECK(!static_cast<bool>(matchers.path_matcher));         // NOSONAR(cpp:S1905) - Explicit bool conversion documents intent
  }
}

