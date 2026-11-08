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
#include "search/ParallelSearchEngine.h"
#include "search/SearchContext.h"
#include "search/TrigramQueryPlanner.h"
#include "utils/Logger.h"
#include "utils/ThreadPool.h"
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

  TEST_CASE_FIXTURE(PopulatedIndexFixture, "SearchAsyncWithData - pp:** matches all items") {
    const SearchContext ctx = CreateSimpleSearchContext("pp:**");
    auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchWithDataAndCollect(
        fixture.GetEngine(), index, "pp:**", -1, ctx);

    CHECK(results.size() == 5);
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

  TEST_CASE("ProcessChunkRange - all-pass bulk emission handles tombstones correctly") {
    MockSearchableIndex index;
    // Add 10 entries
    index.AddEntry(1, 0, "C:\\", true, "C:\\");
    for (size_t i = 1; i <= 9; ++i) {
      index.AddEntry(i + 1, 1, "file_" + std::to_string(i) + ".txt", false,
                     "C:\\file_" + std::to_string(i) + ".txt");
    }

    // Mark index 2 ("C:\file_2.txt") and index 5 ("C:\file_5.txt") as deleted
    REQUIRE(index.RemovePathByIndex(2));
    REQUIRE(index.RemovePathByIndex(5));

    const PathStorage::SoAView view = index.GetSearchableView();
    REQUIRE(view.size == 10);
    REQUIRE(view.is_deleted[2] != 0);
    REQUIRE(view.is_deleted[5] != 0);

    SearchContext ctx; // All-pass: no queries, no filters
    SearchResultBatch batch;
    search_pattern_utils::PatternMatcher no_matcher;

    ParallelSearchEngine::ProcessChunkRange(view, 0, 10, batch, ctx,
                                            index.GetStorageSize(), no_matcher, no_matcher);

    // 10 entries total, 2 deleted -> 8 hits
    CHECK(batch.Size() == 8);

    // Verify deleted IDs (3 and 6, corresponding to file_2 and file_5) are not present
    std::vector<uint64_t> result_ids;
    for (const auto& hit : batch.hits) {
      result_ids.push_back(hit.id);
      const std::string_view path(batch.arena.data() + hit.path_offset, hit.path_len);
      CHECK_FALSE(path.empty());
      CHECK(path.find("file_2.txt") == std::string_view::npos);
      CHECK(path.find("file_5.txt") == std::string_view::npos);
    }
    CHECK(std::find(result_ids.begin(), result_ids.end(), 3) == result_ids.end());
    CHECK(std::find(result_ids.begin(), result_ids.end(), 6) == result_ids.end());

    // Verify debug invariant: batch arena size must equal sum of hit lengths + 1
    size_t batch_bytes = 0;
    for (const auto& hit : batch.hits) {
      batch_bytes += hit.path_len + 1;
    }
    CHECK(batch_bytes == batch.arena.size());
  }

  TEST_CASE("ProcessChunkRange - all-pass bulk emission handles edge and consecutive tombstones") {
    MockSearchableIndex index;
    // Add 6 entries: 0..5
    for (size_t i = 0; i < 6; ++i) {
      index.AddEntry(i + 1, 0, "path_" + std::to_string(i), false,
                     "C:\\path_" + std::to_string(i));
    }

    // Edge tombstone at start (0), consecutive tombstones in middle (2, 3), edge tombstone at end (5)
    REQUIRE(index.RemovePathByIndex(0));
    REQUIRE(index.RemovePathByIndex(2));
    REQUIRE(index.RemovePathByIndex(3));
    REQUIRE(index.RemovePathByIndex(5));

    const PathStorage::SoAView view = index.GetSearchableView();
    SearchContext ctx;
    SearchResultBatch batch;
    search_pattern_utils::PatternMatcher no_matcher;

    ParallelSearchEngine::ProcessChunkRange(view, 0, 6, batch, ctx,
                                            index.GetStorageSize(), no_matcher, no_matcher);

    // Only indices 1 and 4 remain (2 hits)
    CHECK(batch.Size() == 2);
    REQUIRE(batch.hits.size() == 2);
    CHECK(batch.hits[0].id == 2); // path_1
    CHECK(batch.hits[1].id == 5); // path_4

    CHECK(std::string_view(batch.arena.data() + batch.hits[0].path_offset, batch.hits[0].path_len) == "C:\\path_1");
    CHECK(std::string_view(batch.arena.data() + batch.hits[1].path_offset, batch.hits[1].path_len) == "C:\\path_4");

    size_t batch_bytes = 0;
    for (const auto& hit : batch.hits) {
      batch_bytes += hit.path_len + 1;
    }
    CHECK(batch_bytes == batch.arena.size());
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

  TEST_CASE("SearchAsyncWithData - all-pass query uses 100% static initial chunks without dynamic chunking") {
    MockSearchableIndex index;
    PopulateLargeIndex(index, 200);

    test_helpers::TestParallelSearchEngineFixture fixture;
    const SearchContext ctx = search_context_builder::Build(
        "", "pp:**", nullptr, ItemTypeFilter::All, false, nullptr, false);

    std::vector<ThreadTiming> thread_timings;
    auto futures = fixture.GetEngine().SearchAsyncWithData(index, "", 4, ctx, &thread_timings);

    size_t total_hits = 0;
    for (auto& f : futures) {
      total_hits += f.get().hits.size();
    }
    CHECK(total_hits == 201);

    REQUIRE(!thread_timings.empty());
    size_t total_items_from_timing = 0;
    for (const auto& timing : thread_timings) {
      CHECK(timing.dynamic_chunks_processed_ == 0);
      total_items_from_timing += timing.items_processed_;
    }
    CHECK(total_items_from_timing == 201);
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
    SearchContext ctx1 = search_context_builder::Build("image???.???", "", nullptr, ItemTypeFilter::All, false, nullptr);
    auto futures1 = fixture.GetEngine().SearchAsyncWithData(index, "image???.???", -1, ctx1);
    size_t hits1 = 0;
    for (auto& f : futures1) {
      hits1 += f.get().hits.size();
    }

    // Search 2: "image???.png"
    SearchContext ctx2 = search_context_builder::Build("image???.png", "", nullptr, ItemTypeFilter::All, false, nullptr);
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
    SearchContext ctx = search_context_builder::Build("test", "", nullptr, ItemTypeFilter::All, false, nullptr);
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
    SearchContext ctx_nomatch = search_context_builder::Build("xyz", "", nullptr, ItemTypeFilter::All, false, nullptr);
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
          search_context_builder::Build("key", "", nullptr, ItemTypeFilter::All, false, nullptr);
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
    // scope inside FileIndex::RebuildPathBuffer, and the filter
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
                      FileIndex::kRebuildDeletedPercentageThreshold,
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
            search_context_builder::Build("key", "", nullptr, ItemTypeFilter::All, false, nullptr);
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


TEST_SUITE("ParallelSearchEngine - ExtensionIndex filter") {

  namespace {
  struct ExtensionFilterSetup {
    FileIndex index;
    test_helpers::TestParallelSearchEngineFixture engine_fixture;
  };

  void WaitForBothIndexes(ExtensionFilterSetup& setup) {
    for (int retry = 0; retry < 200 && !setup.index.GetTrigramIndex()->IsReady(); ++retry) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    for (int retry = 0; retry < 200 && !setup.index.GetExtensionIndex()->IsReady(); ++retry) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(setup.index.GetTrigramIndex()->IsReady());
    REQUIRE(setup.index.GetExtensionIndex()->IsReady());
  }

  [[nodiscard]] std::vector<uint64_t> CollectHitIds(ExtensionFilterSetup& setup,
                                                    std::string_view query, const SearchContext& ctx,
                                                    std::vector<ThreadTiming>* timings = nullptr) {
    auto futures =
        setup.engine_fixture.GetEngine().SearchAsyncWithData(setup.index, query, -1, ctx, timings);
    std::vector<uint64_t> ids;
    for (auto& f : futures) {
      for (const auto& hit : f.get().hits) {
        ids.push_back(hit.id);
      }
    }
    std::sort(ids.begin(), ids.end());  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
    return ids;
  }

  void PopulateBulkWithCppPair(ExtensionFilterSetup& setup) {
    for (size_t i = 1; i <= 2000; ++i) {
      setup.index.InsertPath("C:\\bulk\\filler_" + std::to_string(i) + ".txt", false);
    }
    setup.index.InsertPath("C:\\src\\quarterly_report.cpp", false);
    setup.index.InsertPath("C:\\src\\quarterly_report.txt", false);
    setup.index.InsertPath("C:\\src\\quarterly_notes.cpp", false);
    setup.index.RecomputeAllPaths();
    WaitForBothIndexes(setup);
  }

  [[nodiscard]] SearchContext BuildExtCtx(std::string_view filename,
                                         const std::vector<std::string>* extensions,
                                         bool case_sensitive = false) {
    return search_context_builder::Build(filename, "", extensions, ItemTypeFilter::All,
                                       case_sensitive, nullptr);
  }
  }  // namespace

  TEST_CASE("Extension-only filter returns exact hits") {
    ExtensionFilterSetup setup;
    PopulateBulkWithCppPair(setup);

    const std::vector<std::string> extensions = {"cpp"};
    const auto ids = CollectHitIds(setup, "", BuildExtCtx("", &extensions));
    CHECK(ids.size() == 2);
  }

  TEST_CASE("Extension-only filter takes the filter path") {
    ExtensionFilterSetup setup;
    PopulateBulkWithCppPair(setup);

    const std::vector<std::string> extensions = {"cpp"};
    std::vector<ThreadTiming> timings;
    const auto ids = CollectHitIds(setup, "", BuildExtCtx("", &extensions), &timings);
    CHECK(ids.size() == 2);
    // Single timing entry proves the single-threaded candidate-filter path
    // (a parallel scan would resize timings to thread_count).
    CHECK(timings.size() == 1);
  }

  TEST_CASE("Multi-extension query uses disjoint traversal") {
    ExtensionFilterSetup setup;
    PopulateBulkWithCppPair(setup);

    const std::vector<std::string> extensions = {"cpp", "txt"};
    std::vector<ThreadTiming> timings;
    const auto ids = CollectHitIds(setup, "", BuildExtCtx("", &extensions), &timings);
    // 2 cpp files + 1 report.txt + 2000 filler.txt = 2003 hits
    CHECK(ids.size() == 2003);
    CHECK(timings.size() == 1);
  }

  TEST_CASE("Filename+extension query intersects both sides") {
    ExtensionFilterSetup setup;
    PopulateBulkWithCppPair(setup);

    const std::vector<std::string> extensions = {"txt"};
    std::vector<ThreadTiming> timings;
    const auto ids = CollectHitIds(setup, "report", BuildExtCtx("report", &extensions), &timings);
    // "quarterly_report.txt" only: report.cpp excluded by extension,
    // notes.cpp excluded by both, filler excluded by filename.
    CHECK(ids.size() == 1);
    CHECK(timings.size() == 1);
  }


  TEST_CASE("Not-ready extension index falls back with identical results") {
    ExtensionFilterSetup setup;
    PopulateBulkWithCppPair(setup);

    const std::vector<std::string> txt = {"txt"};
    const auto baseline = CollectHitIds(setup, "report", BuildExtCtx("report", &txt));

    // Drive the lifecycle directly: the production getter is const (search
    // never mutates), so the cast is test-only scaffolding.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) - test-only lifecycle driving; production const-correctness is intentional and unchanged
    auto* mutable_ext = const_cast<extension_index::ExtensionIndex*>(setup.index.GetExtensionIndex());
    REQUIRE(mutable_ext != nullptr);
    const uint64_t gen = mutable_ext->MarkBuilding();
    REQUIRE(!mutable_ext->IsReady());
    CHECK(CollectHitIds(setup, "report", BuildExtCtx("report", &txt)) == baseline);
    mutable_ext->AbortBuild(gen);
    REQUIRE(mutable_ext->IsReady());
    CHECK(CollectHitIds(setup, "report", BuildExtCtx("report", &txt)) == baseline);
  }

  TEST_CASE("Case-sensitive extension precision over lowercase index") {
    ExtensionFilterSetup setup;
    setup.index.InsertPath("C:\\src\\Upper.CPP", false);
    setup.index.InsertPath("C:\\src\\lower.cpp", false);
    setup.index.RecomputeAllPaths();
    WaitForBothIndexes(setup);

    const std::vector<std::string> cpp = {"cpp"};
    // Case-sensitive: verifier rejects "CPP" (index is a lowercase superset).
    CHECK(CollectHitIds(setup, "", BuildExtCtx("", &cpp, true)).size() == 1);
    // Case-insensitive: both match.
    CHECK(CollectHitIds(setup, "", BuildExtCtx("", &cpp, false)).size() == 2);
  }

  TEST_CASE("Dot-only extension filter declines the index without losing rows") {
    ExtensionFilterSetup setup;
    setup.index.InsertPath("C:\\src\\has_ext.cpp", false);
    setup.index.InsertPath("C:\\src\\no_extension", false);
    setup.index.RecomputeAllPaths();
    WaitForBothIndexes(setup);

    // "." normalizes to empty: matches extension-less files in scan mode, a
    // set the index cannot express. The gate must decline (scan decides).
    // Asserted as a complement: dot-hits + cpp-hits == all rows (implicit
    // ancestor entries included), disjoint.
    const std::vector<std::string> cpp = {"cpp"};
    const auto cpp_ids = CollectHitIds(setup, "", BuildExtCtx("", &cpp));
    const std::vector<std::string> dot = {"."};
    const auto dot_ids = CollectHitIds(setup, "", BuildExtCtx("", &dot));
    const auto all_ids = CollectHitIds(setup, "", BuildExtCtx("", nullptr));
    CHECK(dot_ids.size() + cpp_ids.size() == all_ids.size());
    for (const uint64_t id : cpp_ids) {
      CHECK(std::find(dot_ids.begin(), dot_ids.end(), id) == dot_ids.end());
    }
    CHECK(!dot_ids.empty());
  }

}  // TEST_SUITE("ParallelSearchEngine - ExtensionIndex filter")

TEST_SUITE("ParallelSearchEngine - System file filtering") {

  struct SystemFileIndexFixture {
    MockSearchableIndex index;
    test_helpers::TestParallelSearchEngineFixture fixture;

    SystemFileIndexFixture() {
      // Root and directories
      index.AddEntry(1, 0, "C:\\", true, "C:\\");
      index.AddEntry(2, 1, "dev", true, "C:\\dev");
      index.AddEntry(3, 1, "$Extend", true, "C:\\$Extend");
      index.AddEntry(4, 2, "$subfolder", true, "C:\\dev\\$subfolder");

      // System files
      index.AddEntry(5, 1, "$MFT", false, "C:\\$MFT");
      index.AddEntry(6, 2, "$report.txt", false, "C:\\dev\\$report.txt");
      index.AddEntry(7, 3, "ObjId", false, "C:\\$Extend\\ObjId");
      index.AddEntry(8, 4, "inner.txt", false, "C:\\dev\\$subfolder\\inner.txt");

      // Normal files
      index.AddEntry(9, 1, "normal.txt", false, "C:\\normal.txt");
      index.AddEntry(10, 1, "normal$file.txt", false, "C:\\normal$file.txt");
    }
  };

  TEST_CASE("IsSystemPath helper unit tests") {
    CHECK(parallel_search_detail::IsSystemPath("C:\\$MFT", 3));
    CHECK(parallel_search_detail::IsSystemPath("C:\\dev\\$report.txt", 7));
    CHECK(parallel_search_detail::IsSystemPath("C:\\$Extend\\ObjId", 11));
    CHECK(parallel_search_detail::IsSystemPath("C:\\dev\\$subfolder\\inner.txt", 18));
    CHECK(parallel_search_detail::IsSystemPath("C:/$Extend/ObjId", 11));
    CHECK(parallel_search_detail::IsSystemPath("C:/dev/$subfolder/inner.txt", 18));
    CHECK_FALSE(parallel_search_detail::IsSystemPath("C:\\normal.txt", 3));
    CHECK_FALSE(parallel_search_detail::IsSystemPath("C:\\normal$file.txt", 3));
    CHECK_FALSE(parallel_search_detail::IsSystemPath("C:\\normal$folder\\file.txt", 17));
  }

  TEST_CASE("SearchContext ShouldFilterSystemFiles behavior") {
    SearchContext ctx;
    ctx.hide_system_files = true;
    CHECK(ctx.ShouldFilterSystemFiles());

    ctx.hide_system_files = false;
    CHECK_FALSE(ctx.ShouldFilterSystemFiles());

    ctx.hide_system_files = true;
    ctx.filename_query = "$report";
    CHECK_FALSE(ctx.ShouldFilterSystemFiles());

    ctx.filename_query = "report";
    CHECK(ctx.ShouldFilterSystemFiles());

    ctx.path_query = "$Extend";
    CHECK_FALSE(ctx.ShouldFilterSystemFiles());
  }

  TEST_CASE_FIXTURE(SystemFileIndexFixture, "hide_system_files = true filters out system files") {
    const SearchContext ctx = search_context_builder::Build(
        "", "", nullptr, ItemTypeFilter::FilesOnly, false, nullptr, true);
    const auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchWithDataAndCollect(
        fixture.GetEngine(), index, "", -1, ctx);

    std::vector<std::string> paths;
    paths.reserve(results.size());
    for (const auto& r : results) {
      paths.push_back(r.fullPath);
    }

    CHECK(paths.size() == 2);
    CHECK(std::find(paths.begin(), paths.end(), "C:\\normal.txt") != paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "C:\\normal$file.txt") != paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "C:\\$MFT") == paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "C:\\dev\\$report.txt") == paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "C:\\$Extend\\ObjId") == paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "C:\\dev\\$subfolder\\inner.txt") == paths.end());
  }

  TEST_CASE_FIXTURE(SystemFileIndexFixture, "hide_system_files = false returns all files without filtering") {
    const SearchContext ctx = search_context_builder::Build(
        "", "", nullptr, ItemTypeFilter::FilesOnly, false, nullptr, false);
    const auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchWithDataAndCollect(
        fixture.GetEngine(), index, "", -1, ctx);

    std::vector<std::string> paths;
    paths.reserve(results.size());
    for (const auto& r : results) {
      paths.push_back(r.fullPath);
    }

    CHECK(paths.size() == 6);
    CHECK(std::find(paths.begin(), paths.end(), "C:\\normal.txt") != paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "C:\\normal$file.txt") != paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "C:\\$MFT") != paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "C:\\dev\\$report.txt") != paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "C:\\$Extend\\ObjId") != paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "C:\\dev\\$subfolder\\inner.txt") != paths.end());
  }

  TEST_CASE_FIXTURE(SystemFileIndexFixture, "Explicit query with dollar sign overrides hide_system_files = true") {
    // filename_search = "$report" returns C:\dev\$report.txt even when hide_system_files = true
    {
      const SearchContext ctx = search_context_builder::Build(
          "$report", "", nullptr, ItemTypeFilter::FilesOnly, false, nullptr, true);
      const auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchWithDataAndCollect(
          fixture.GetEngine(), index, "$report", -1, ctx);

      REQUIRE(results.size() == 1);
      CHECK(results[0].fullPath == "C:\\dev\\$report.txt");
    }

    // path_search = "$Extend" returns C:\$Extend\ObjId even when hide_system_files = true
    {
      const SearchContext ctx = search_context_builder::Build(
          "", "$Extend", nullptr, ItemTypeFilter::FilesOnly, false, nullptr, true);
      const auto results = test_helpers::parallel_search_test_helpers::ExecuteSearchWithDataAndCollect(
          fixture.GetEngine(), index, "", -1, ctx);

      REQUIRE(results.size() == 1);
      CHECK(results[0].fullPath == "C:\\$Extend\\ObjId");
    }
  }

  TEST_CASE("Search for $report in real FileIndex with FilenameTrigramIndex") {
    FileIndex index;
    constexpr uint64_t kVolumeRootFrn = 0x0001000000000005ULL;
    constexpr uint64_t kDevFolder = 0x0001000000000200ULL;
    constexpr uint64_t kReportFile = 0x0001000000000300ULL;
    constexpr uint64_t kNormalFile = 0x0001000000000400ULL;

    const auto ref = [](uint64_t raw) { return ntfs_file_reference::NtfsFileReference(raw); };
    index.Insert(ref(kDevFolder), ref(kVolumeRootFrn), "dev", true);
    index.Insert(ref(kReportFile), ref(kDevFolder), "$report.text.test", false);
    index.Insert(ref(kNormalFile), ref(kDevFolder), "normal.txt", false);

    // Populate enough files so total_items triggers ShouldUseTrigramFilter
    constexpr size_t kLargeCount = 10000;
    for (size_t i = 1; i <= kLargeCount; ++i) {
      index.Insert(ref(0x0001000000010000ULL + i), ref(kDevFolder), "filler_file_" + std::to_string(i) + ".dat", false);
    }
    index.RecomputeAllPaths();

    for (int retry = 0; retry < 100 && !index.GetTrigramIndex()->IsReady(); ++retry) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(index.GetTrigramIndex()->IsReady());

    test_helpers::TestParallelSearchEngineFixture fixture;

    // Search with "report" when hide_system_files = true
    {
      const SearchContext ctx = search_context_builder::Build(
          "report", "", nullptr, ItemTypeFilter::All, false, nullptr, true);
      auto futures = fixture.GetEngine().SearchAsyncWithData(index, "report", -1, ctx);
      size_t hits = 0;
      for (auto& f : futures) {
        hits += f.get().hits.size();
      }
      CHECK(hits == 0); // "report" has no '$', so system file is hidden
    }

    // Search with "report" when hide_system_files = false
    {
      const SearchContext ctx = search_context_builder::Build(
          "report", "", nullptr, ItemTypeFilter::All, false, nullptr, false);
      auto futures = fixture.GetEngine().SearchAsyncWithData(index, "report", -1, ctx);
      size_t hits = 0;
      for (auto& f : futures) {
        hits += f.get().hits.size();
      }
      CHECK(hits == 1);
    }

    // Search with "$report" when hide_system_files = true (query override)
    {
      const SearchContext ctx = search_context_builder::Build(
          "$report", "", nullptr, ItemTypeFilter::All, false, nullptr, true);
      auto futures = fixture.GetEngine().SearchAsyncWithData(index, "$report", -1, ctx);
      size_t hits = 0;
      for (auto& f : futures) {
        hits += f.get().hits.size();
      }
      CHECK(hits == 1);
    }

    // Now insert a file LIVE after the trigram index was already marked ready
    constexpr uint64_t kLiveReport = 0x0001000000000500ULL;
    index.Insert(ref(kLiveReport), ref(kDevFolder), "$live_report.text.test", false);

    // Search with "live_report" (hide_system_files = false)
    {
      const SearchContext ctx = search_context_builder::Build(
          "live_report", "", nullptr, ItemTypeFilter::All, false, nullptr, false);
      auto futures = fixture.GetEngine().SearchAsyncWithData(index, "live_report", -1, ctx);
      size_t hits = 0;
      for (auto& f : futures) {
        hits += f.get().hits.size();
      }
      CHECK(hits == 1);
    }

    // Search with "$live_report" (hide_system_files = true)
    {
      const SearchContext ctx = search_context_builder::Build(
          "$live_report", "", nullptr, ItemTypeFilter::All, false, nullptr, true);
      auto futures = fixture.GetEngine().SearchAsyncWithData(index, "$live_report", -1, ctx);
      size_t hits = 0;
      for (auto& f : futures) {
        hits += f.get().hits.size();
      }
      CHECK(hits == 1);
    }
  }

}  // TEST_SUITE("ParallelSearchEngine - System file filtering")
