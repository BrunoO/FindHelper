#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

// Enable test mode for non-Windows platforms
#ifndef _WIN32
#ifndef USN_WINDOWS_TESTS
#define USN_WINDOWS_TESTS
#endif  // USN_WINDOWS_TESTS
#endif  // _WIN32

#include "MockSearchableIndex.h"
#include "TestHelpers.h"
#include "index/FileIndex.h"
#include "index/FileIndexMaintenance.h"
#include "index/RemoveIndexedSubtree.h"
#include "search/ParallelSearchEngine.h"
#include "search/SearchContext.h"
#include "search/SearchContextBuilder.h"
#include "search/SearchThreadPool.h"
#include "utils/Logger.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <set>
#include <string>
#include <thread>
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

// Fixture for tests that operate on a pre-populated simple index (5 files)
struct PopulatedIndexFixture {
  MockSearchableIndex index;
  test_helpers::TestParallelSearchEngineFixture fixture;
  PopulatedIndexFixture() { PopulateSimpleIndex(index); }
};

} // anonymous namespace

TEST_SUITE("ParallelSearchEngine - SearchAsyncWithData") {

  TEST_CASE("SearchAsyncWithData - empty index returns no results") {
    MockSearchableIndex index;
    test_helpers::TestParallelSearchEngineFixture fixture;

    SearchContext ctx = CreateSimpleSearchContext("test");
    auto futures = fixture.GetEngine().SearchAsyncWithData(index, "test", -1, ctx);

    CHECK(futures.empty());
  }

  TEST_CASE_FIXTURE(PopulatedIndexFixture, "SearchAsyncWithData - returns full path data") {
    SearchContext ctx = CreateSimpleSearchContext("test");
    auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchWithDataAndCollect(
        fixture.GetEngine(), index, "test", -1, ctx);

    CHECK(results.size() == 1);
    CHECK(results[0].id == 2);
    CHECK(results[0].fullPath == "C:\\test.txt");
    CHECK(results[0].isDirectory == false);
  }

  TEST_CASE_FIXTURE(PopulatedIndexFixture, "SearchAsyncWithData - simple filename search") {
    SearchContext ctx = CreateSimpleSearchContext("test");
    auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchWithDataAndCollect(
        fixture.GetEngine(), index, "test", -1, ctx);

    CHECK(results.size() == 1);
    CHECK(results[0].id == 2);
  }

  TEST_CASE_FIXTURE(PopulatedIndexFixture, "SearchAsyncWithData - case-insensitive search") {
    SearchContext ctx = CreateSimpleSearchContext("TEST", false);
    auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchWithDataAndCollect(
        fixture.GetEngine(), index, "TEST", -1, ctx);

    CHECK(results.size() == 1);
    CHECK(results[0].id == 2);
  }

  TEST_CASE_FIXTURE(PopulatedIndexFixture, "SearchAsyncWithData - case-sensitive search") {
    SearchContext ctx = CreateSimpleSearchContext("TEST", true);
    auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchWithDataAndCollect(
        fixture.GetEngine(), index, "TEST", -1, ctx);

    // Case-sensitive search for "TEST" should not match "test.txt"
    CHECK(results.empty());
  }

  TEST_CASE_FIXTURE(PopulatedIndexFixture, "SearchAsyncWithData - extension filter") {
    SearchContext ctx = CreateExtensionSearchContext({".txt", ".md"});
    auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchWithDataAndCollect(
        fixture.GetEngine(), index, "", -1, ctx);

    CHECK(results.size() == 2);
    const bool has_id2 = std::any_of(results.begin(), results.end(), [](const auto& r) { return r.id == 2; });
    const bool has_id4 = std::any_of(results.begin(), results.end(), [](const auto& r) { return r.id == 4; });
    CHECK(has_id2); // test.txt
    CHECK(has_id4); // readme.md
  }

  TEST_CASE("SearchAsyncWithData - multiple threads") {
    MockSearchableIndex index;
    PopulateLargeIndex(index, 100);

    test_helpers::TestParallelSearchEngineFixture fixture;

    SearchContext ctx = CreateSimpleSearchContext("file");
    auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchWithDataAndCollect(
        fixture.GetEngine(), index, "file", 4, ctx);

    CHECK(results.size() == 100); // All files match "file"
  }

  TEST_CASE("SearchAsyncWithData - cancellation") {
    MockSearchableIndex index;
    PopulateLargeIndex(index, 1000);

    test_helpers::TestParallelSearchEngineFixture fixture;

    std::atomic cancel_flag(false);
    SearchContext ctx = CreateSimpleSearchContext("file");
    ctx.cancel_flag = &cancel_flag;

    // Start search
    auto futures = fixture.GetEngine().SearchAsyncWithData(index, "file", 4, ctx);

    // Cancel immediately
    cancel_flag = true;

    // Wait for results (should be partial or empty due to cancellation)
    auto results = test_helpers::CollectFutures(futures);
    // Results may be empty or partial depending on cancellation timing
    CHECK(results.size() <= 1000);
  }

  TEST_CASE("ProcessChunkRange - polls cancellation in chunks without interval multiples") {
    // Regression: the cancel poll was keyed on the ABSOLUTE index (i & mask), so
    // a chunk like [1, 128) - one that straddles no multiple of
    // kCancellationCheckInterval (128) - never polled the flag and scanned the
    // whole chunk despite cancellation. Fixed to poll on the position relative
    // to the chunk start, mirroring items_checked counter.
    MockSearchableIndex index;
    PopulateLargeIndex(index, 200);  // 201 entries: root + file_1..file_200

    const PathStorage::SoAView view = index.GetSearchableView();
    REQUIRE(view.size >= 128);

    std::atomic cancel_flag{true};
    SearchContext ctx;  // Default: no queries, no extension filter -> every live row matches
    ctx.cancel_flag = &cancel_flag;

    search_pattern_utils::PatternMatcher no_matcher;

    // Chunk [1, 128): contains no multiple of kCancellationCheckInterval.
    SearchResultBatch batch;
    ParallelSearchEngine::ProcessChunkRange(view, 1, 128, batch, ctx,
                                            index.GetStorageSize(), no_matcher, no_matcher);
    CHECK(batch.Size() == 0);  // Cancelled at the very first item of the chunk

    // Control: same chunk with the flag cleared must scan fully - guards the
    // cancelled assertion above against passing vacuously via an invalid-range
    // early return.
    cancel_flag = false;
    SearchResultBatch batch_control;
    ParallelSearchEngine::ProcessChunkRange(view, 1, 128, batch_control, ctx,
                                            index.GetStorageSize(), no_matcher, no_matcher);
    CHECK(batch_control.Size() == 127);
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
    if (hw_concurrency == 0) { hw_concurrency = 4; }  // Fallback
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

TEST_SUITE("ParallelSearchEngine - TrigramAcceleration") {

  TEST_CASE("SearchAsyncWithData - Wildcard Image Search Bug Verification") {
    FileIndex index;
    index.InsertPath("C:\\image001.png", false);
    index.InsertPath("C:\\image002.png", false);
    index.InsertPath("C:\\image003.png", false);
    index.InsertPath("C:\\image004.png", false);
    index.InsertPath("C:\\image005.png", false);
    index.InsertPath("C:\\image001.jpg", false);
    index.InsertPath("C:\\image002.jpg", false);
    index.RecomputeAllPaths();

    for (int retry = 0; retry < 100 && !index.GetTrigramIndex()->IsReady(); ++retry) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(index.GetTrigramIndex()->IsReady());

    test_helpers::TestParallelSearchEngineFixture fixture;

    // Search 1: "image???.???"
    SearchContext ctx1 = SearchContextBuilder::Build("image???.???", "", nullptr, ItemTypeFilter::All, false, nullptr);
    auto futures1 = fixture.GetEngine().SearchAsyncWithData(index, "image???.???", -1, ctx1);
    size_t hits1 = 0;
    for (auto& f : futures1) {
      hits1 += f.get().hits.size();
    }

    // Search 2: "image???.png"
    SearchContext ctx2 = SearchContextBuilder::Build("image???.png", "", nullptr, ItemTypeFilter::All, false, nullptr);
    auto futures2 = fixture.GetEngine().SearchAsyncWithData(index, "image???.png", -1, ctx2);
    size_t hits2 = 0;
    for (auto& f : futures2) {
      hits2 += f.get().hits.size();
    }

    LOG_INFO_BUILD("Wildcard test: image???.??? hit_count = " << hits1 << ", image???.png hit_count = " << hits2);
    CHECK(hits1 == 7);
    CHECK(hits2 == 5);
  }

  TEST_CASE("SearchAsyncWithData with Trigram Acceleration") {
    FileIndex index;
    index.InsertPath("C:\\test_file.txt", false);
    index.InsertPath("C:\\another_example.cpp", false);
    index.InsertPath("C:\\readme.md", false);
    index.RecomputeAllPaths();

    for (int retry = 0; retry < 100 && !index.GetTrigramIndex()->IsReady(); ++retry) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(index.GetTrigramIndex()->IsReady());

    test_helpers::TestParallelSearchEngineFixture fixture;

    // Search for "test" (trigram "tes", "est")
    SearchContext ctx = SearchContextBuilder::Build("test", "", nullptr, ItemTypeFilter::All, false, nullptr);
    std::vector<ThreadTiming> timings;
    auto futures = fixture.GetEngine().SearchAsyncWithData(index, "test", -1, ctx, &timings);
    size_t hit_count = 0;
    for (auto& f : futures) {
      hit_count += f.get().hits.size();
    }

    CHECK(hit_count == 1);
    REQUIRE(timings.size() == 1);
    CHECK(timings[0].items_processed_ <= 1); // Accelerated via trigram index (sparse scan <= 1 candidate)

    // Non-matching query "xyz" -> 0 candidate early exit
    SearchContext ctx_nomatch = SearchContextBuilder::Build("xyz", "", nullptr, ItemTypeFilter::All, false, nullptr);
    std::vector<ThreadTiming> timings_nomatch;
    auto futures_nomatch = fixture.GetEngine().SearchAsyncWithData(index, "xyz", -1, ctx_nomatch, &timings_nomatch);
    size_t hit_count_nomatch = 0;
    for (auto& f : futures_nomatch) {
      hit_count_nomatch += f.get().hits.size();
    }

    CHECK(hit_count_nomatch == 0);
    CHECK(futures_nomatch.empty()); // Early exit
  }

  TEST_CASE("Trigram search survives path-buffer rebuild (Maintain)") {
    // Regression: a file found right after cold start disappeared a few
    // minutes later, right after the metrics reported a buffer rebuild.
    // RebuildPathBuffer compacts PathStorage (new SoA row-ids) without
    // refreshing trigram postings, so the filter pointed at tombstoned rows
    // and the query returned empty with no fallback scan.
    //
    // The keeper uses a larger id on purpose: RecomputeAllPaths lays out rows
    // in id order, so the keeper lands last and the defrag actually moves it.
    // (A first-row keeper would keep row 0 through an order-preserving
    // compact and could not catch this bug.)
    FileIndex index;
    constexpr uint64_t kVolumeRootFrn = 0x0001000000000005ULL;
    constexpr uint64_t kBulkFolder = 0x0001000000000200ULL;
    constexpr uint64_t kBulkBase = 0x0002000000001000ULL;
    constexpr uint64_t kKeeperId = 0x0009000000000100ULL;
    constexpr size_t kBulkFileCount = 1200;

    const auto ref = [](uint64_t raw) { return ntfs_file_reference::NtfsFileReference(raw); };
    index.Insert(ref(kBulkFolder), ref(kVolumeRootFrn), "bulk", true);
    for (size_t i = 0; i < kBulkFileCount; ++i) {
      index.Insert(ref(kBulkBase + i), ref(kBulkFolder), "f", false);
    }
    index.Insert(ref(kKeeperId), ref(kVolumeRootFrn), "keeper_key_file.txt", false);
    index.RecomputeAllPaths();

    for (int retry = 0; retry < 100 && !index.GetTrigramIndex()->IsReady(); ++retry) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(index.GetTrigramIndex()->IsReady());

    test_helpers::TestParallelSearchEngineFixture engine_fixture;
    auto search_key_hits = [&] {
      SearchContext ctx =
          SearchContextBuilder::Build("key", "", nullptr, ItemTypeFilter::All, false, nullptr);
      auto futures = engine_fixture.GetEngine().SearchAsyncWithData(index, "key", -1, ctx, nullptr);
      size_t hits = 0;
      for (auto& f : futures) {
        hits += f.get().hits.size();
      }
      return hits;
    };

    // Baseline: cold start finds the file via the trigram filter.
    CHECK(search_key_hits() == 1);

    // Churn: bulk deletes leave >1000 tombstones, tripping the defrag.
    for (size_t i = 0; i < kBulkFileCount; ++i) {
      RemoveIndexedSubtree(index, ref(kBulkBase + i));
    }
    RemoveIndexedSubtree(index, ref(kBulkFolder));

    // Same threshold path as production Maintain.
    REQUIRE(index.Maintain());

    for (int retry = 0; retry < 100 && !index.GetTrigramIndex()->IsReady(); ++retry) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(index.GetTrigramIndex()->IsReady());

    // The keeper must still be found after the rebuild.
    CHECK(search_key_hits() == 1);
  }

  TEST_CASE("Concurrent churn+Maintain never hides the keeper") {
    // TOCTOU regressions: compaction and the trigram snapshot share one lock
    // scope inside FileIndexMaintenance::RebuildPathBuffer, and the filter
    // re-queries its bitmap under the same lock as its view snapshot inside
    // FilterTrigramCandidates — so a search can never observe compacted rows
    // with stale postings. A searcher thread queries continuously while the
    // main thread churns and Maintains; every search must find the keeper.
    //
    // Coverage note: deterministic against a missing rebuild (every
    // post-compact search would miss); statistical pressure on the lock-drop
    // windows themselves, which are structural — no test hook can land a
    // search inside a window that no longer exists.
    FileIndex index;
    constexpr uint64_t kVolumeRootFrn = 0x0001000000000005ULL;
    constexpr uint64_t kBulkFolder = 0x0001000000000200ULL;
    constexpr uint64_t kBulkBase = 0x0002000000001000ULL;
    constexpr uint64_t kKeeperId = 0x0009000000000100ULL;
    constexpr size_t kBulkFiles = 30;
    constexpr int kRounds = 30;
    // Must keep tripping the defrag percentage threshold every round so each
    // round compacts: churned / total must stay above the threshold. Pinned
    // with a static_assert so a threshold change breaks compilation here with
    // a clear message instead of silently hollowing out the test.
    constexpr size_t kChurnPerRound = 4;
    static_assert(static_cast<double>(kChurnPerRound) /
                          static_cast<double>(kBulkFiles + 2) >
                      FileIndexMaintenance::kRebuildDeletedPercentageThreshold,
                  "churn per round must trip the maintenance percentage threshold");

    const auto ref = [](uint64_t raw) { return ntfs_file_reference::NtfsFileReference(raw); };
    index.Insert(ref(kBulkFolder), ref(kVolumeRootFrn), "bulk", true);
    std::vector<uint64_t> live_bulk;
    for (size_t i = 0; i < kBulkFiles; ++i) {
      const uint64_t id = kBulkBase + i;
      index.Insert(ref(id), ref(kBulkFolder), "bulkfile", false);
      live_bulk.push_back(id);
    }
    index.Insert(ref(kKeeperId), ref(kVolumeRootFrn), "keeper_key_file.txt", false);
    index.RecomputeAllPaths();

    for (int retry = 0; retry < 100 && !index.GetTrigramIndex()->IsReady(); ++retry) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(index.GetTrigramIndex()->IsReady());

    test_helpers::TestParallelSearchEngineFixture engine_fixture;
    std::atomic stop{false};
    std::atomic<size_t> searches{0};
    std::atomic<size_t> misses{0};
    std::thread searcher([&] {
      while (!stop.load()) {
        SearchContext ctx =
            SearchContextBuilder::Build("key", "", nullptr, ItemTypeFilter::All, false, nullptr);
        auto futures = engine_fixture.GetEngine().SearchAsyncWithData(index, "key", -1, ctx, nullptr);
        size_t hits = 0;
        for (auto& f : futures) {
          hits += f.get().hits.size();
        }
        searches.fetch_add(1);
        if (hits != 1) {
          misses.fetch_add(1);
        }
      }
    });

    uint64_t next_id = kBulkBase + kBulkFiles;
    for (int round = 0; round < kRounds; ++round) {
      // Delete oldest-live bulk files, compact, then replenish so the next
      // round trips again (see the static_assert above).
      for (size_t k = 0; k < kChurnPerRound; ++k) {
        RemoveIndexedSubtree(index, ref(live_bulk[k]));
      }
      live_bulk.erase(live_bulk.begin(), live_bulk.begin() + kChurnPerRound);
      (void)index.Maintain();
      for (size_t k = 0; k < kChurnPerRound; ++k) {
        index.Insert(ref(next_id), ref(kBulkFolder), "bulkfile", false);
        live_bulk.push_back(next_id);
        ++next_id;
      }
    }

    stop.store(true);
    searcher.join();

    CHECK(searches.load() > 0);
    CHECK(misses.load() == 0);
  }

  TEST_CASE("ShouldUseTrigramFilter cost model") {
    using parallel_search_detail::ShouldUseTrigramFilter;
    // Measured points (Release/arm64, ~96k rows): filter wins up to ~1.4x rows single-threaded.
    CHECK(ShouldUseTrigramFilter(4029, 96684, 1));
    CHECK(ShouldUseTrigramFilter(14747, 97461, 1));
    CHECK(ShouldUseTrigramFilter(27091, 94739, 1));
    CHECK(ShouldUseTrigramFilter(56553, 95978, 1));
    // Empty candidate set filters instantly.
    CHECK(ShouldUseTrigramFilter(0, 100, 1));
    // Parallel scan wins when candidates approach the row count on many threads.
    CHECK_FALSE(ShouldUseTrigramFilter(200000, 100000, 8));
    CHECK(ShouldUseTrigramFilter(10000, 100000, 8));
    // Hard cap bounds arena memory regardless of the estimate.
    CHECK_FALSE(ShouldUseTrigramFilter(300000, 100000000, 8));
    // Degenerate thread count falls back to single-thread estimate, not division by zero.
    CHECK(ShouldUseTrigramFilter(10, 100, 0));
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

