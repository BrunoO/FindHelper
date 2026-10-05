#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "path/PathStorage.h"
#include "search/FilenameTrigramExtractor.h"
#include "search/FilenameTrigramIndex.h"
#include "search/SearchThreadPool.h"
#include <chrono>
#include <string_view>
#include <thread>
#include <vector>

TEST_SUITE("FilenameTrigramIndex") {

  TEST_CASE("Empty index queries") {
    const filename_trigram::FilenameTrigramIndex index;
    CHECK(index.GetMemoryUsageBytes() == 0);

    // Empty trigram list returns nullopt
    CHECK(!index.QueryCandidates({}).has_value());

    // Query on non-existent trigram returns nullopt (short-circuit)
    const auto trigrams = filename_trigram::ExtractTrigrams("test");
    CHECK(!index.QueryCandidates(trigrams).has_value());
  }

  TEST_CASE("AddEntry and QueryCandidates") {
    filename_trigram::FilenameTrigramIndex index;
    index.AddEntry(10, "alpha_document.txt");
    index.AddEntry(20, "beta_document.txt");
    index.AddEntry(30, "other_file.cpp");

    CHECK(index.GetMemoryUsageBytes() > 0);

    // Query "document" -> both 10 and 20 match
    const auto doc_trigrams = filename_trigram::ExtractTrigrams("document");
    const auto doc_candidates = index.QueryCandidates(doc_trigrams);
    REQUIRE(doc_candidates.has_value());
    CHECK(doc_candidates->cardinality() == 2);
    CHECK(doc_candidates->contains(10));
    CHECK(doc_candidates->contains(20));
    CHECK(!doc_candidates->contains(30));

    // Query "alpha" -> only 10 matches
    const auto alpha_trigrams = filename_trigram::ExtractTrigrams("alpha");
    const auto alpha_candidates = index.QueryCandidates(alpha_trigrams);
    REQUIRE(alpha_candidates.has_value());
    CHECK(alpha_candidates->cardinality() == 1);
    CHECK(alpha_candidates->contains(10));

    // Query "gamma" -> 0 matches, short-circuit
    const auto gamma_trigrams = filename_trigram::ExtractTrigrams("gamma");
    CHECK(!index.QueryCandidates(gamma_trigrams).has_value());
  }

  TEST_CASE("RemoveEntry") {
    filename_trigram::FilenameTrigramIndex index;
    index.AddEntry(1, "my_report.pdf");
    index.AddEntry(2, "my_notes.txt");

    const auto my_trigrams = filename_trigram::ExtractTrigrams("my_");
    auto res1 = index.QueryCandidates(my_trigrams);
    REQUIRE(res1.has_value());
    CHECK(res1->cardinality() == 2);

    // Remove row 1
    index.RemoveEntry(1, "my_report.pdf");
    auto res2 = index.QueryCandidates(my_trigrams);
    REQUIRE(res2.has_value());
    CHECK(res2->cardinality() == 1);
    CHECK(res2->contains(2));
    CHECK(!res2->contains(1));
  }

  TEST_CASE("Clear resets index") {
    filename_trigram::FilenameTrigramIndex index;
    index.AddEntry(1, "sample.txt");
    const auto trigrams = filename_trigram::ExtractTrigrams("sample");
    CHECK(index.QueryCandidates(trigrams).has_value());

    index.Clear();
    CHECK(!index.QueryCandidates(trigrams).has_value());
    CHECK(index.GetMemoryUsageBytes() == 0);
  }

  TEST_CASE("Build from PathStorage SoAView") {
    PathStorage storage;
    (void)storage.InsertPath(1, "C:\\docs\\report2026.docx", false, std::nullopt);
    (void)storage.InsertPath(2, "C:\\docs\\notes.txt", false, std::nullopt);
    (void)storage.InsertPath(3, "C:\\code\\main.cpp", false, std::nullopt);

    // Mark row 2 as deleted
    (void)storage.RemovePathByIndex(1); // row 1 in 0-indexed SoA is "notes.txt"

    filename_trigram::FilenameTrigramIndex index;
    index.Build(storage.GetReadOnlyView(), storage.GetStorageSize());

    // "report" should match row 0
    const auto rep_trigrams = filename_trigram::ExtractTrigrams("report");
    const auto rep_res = index.QueryCandidates(rep_trigrams);
    REQUIRE(rep_res.has_value());
    CHECK(rep_res->cardinality() == 1);
    CHECK(rep_res->contains(0));

    // "notes" was deleted -> should return nullopt or empty
    const auto notes_trigrams = filename_trigram::ExtractTrigrams("notes");
    CHECK(!index.QueryCandidates(notes_trigrams).has_value());
  }

  TEST_CASE("Build with empty and multi-entry SoAView") {
    PathStorage empty_storage;
    filename_trigram::FilenameTrigramIndex index;
    index.Build(empty_storage.GetReadOnlyView(), 0);
    CHECK(index.GetMemoryUsageBytes() == 0);

    // Multi-entry build verifying sort-based inverted postings grouping
    PathStorage storage;
    for (size_t i = 1; i <= 50; ++i) {
      const std::string name = "test_file_" + std::to_string(i) + ".log";
      (void)storage.InsertPath(i, "C:\\logs\\" + name, false, std::nullopt);
    }
    // Add extra files with distinct keywords
    (void)storage.InsertPath(51, "C:\\logs\\database_backup.bak", false, std::nullopt);
    (void)storage.InsertPath(52, "C:\\logs\\database_dump.sql", false, std::nullopt);

    index.Build(storage.GetReadOnlyView(), storage.GetStorageSize());
    CHECK(index.GetMemoryUsageBytes() > 0);

    // All 50 "test_file_*.log" should match "test_file"
    const auto test_trigrams = filename_trigram::ExtractTrigrams("test_file");
    const auto test_res = index.QueryCandidates(test_trigrams);
    REQUIRE(test_res.has_value());
    CHECK(test_res->cardinality() == 50);
    for (uint32_t r = 0; r < 50; ++r) {
      CHECK(test_res->contains(r));
    }
    CHECK(!test_res->contains(50));
    CHECK(!test_res->contains(51));

    // "database" should match rows 50 and 51
    const auto db_trigrams = filename_trigram::ExtractTrigrams("database");
    const auto db_res = index.QueryCandidates(db_trigrams);
    REQUIRE(db_res.has_value());
    CHECK(db_res->cardinality() == 2);
    CHECK(db_res->contains(50));
    CHECK(db_res->contains(51));
  }

  TEST_CASE("Rebuild from empty SoAView clears previously populated index") {
    PathStorage storage;
    (void)storage.InsertPath(1, "C:\\base\\existing.txt", false, std::nullopt);

    filename_trigram::FilenameTrigramIndex index;
    index.Build(storage.GetReadOnlyView(), storage.GetStorageSize());
    CHECK(index.GetMemoryUsageBytes() > 0);

    // Rebuild from empty storage must reset the index
    PathStorage empty_storage;
    index.Build(empty_storage.GetReadOnlyView(), 0);
    CHECK(index.GetMemoryUsageBytes() == 0);

    const auto trigrams = filename_trigram::ExtractTrigrams("existing");
    CHECK(!index.QueryCandidates(trigrams).has_value());
  }

  TEST_CASE("Async build and mutation buffering") {
    PathStorage storage;
    (void)storage.InsertPath(1, "C:\\base\\archive_data.zip", false, std::nullopt);

    filename_trigram::FilenameTrigramIndex index;
    CHECK(index.IsReady());

    // Initiate async build phases
    const uint64_t gen = index.MarkBuilding();
    CHECK(!index.IsReady());

    // While building, queries short-circuit to nullopt for linear scan fallback
    const auto archive_trigrams = filename_trigram::ExtractTrigrams("archive");
    CHECK(!index.QueryCandidates(archive_trigrams).has_value());

    // Extract postings under simulated lock
    auto postings = index.ExtractPostings(storage.GetReadOnlyView(), storage.GetStorageSize());
    CHECK(!postings.empty());

    // Live arrival while background sorting/populating is in-flight
    index.AddEntry(2, "live_incoming.dat");

    // Complete background build
    index.BuildFromPostings(gen, std::move(postings));
    CHECK(index.IsReady());

    // Base entries match
    const auto base_res = index.QueryCandidates(archive_trigrams);
    REQUIRE(base_res.has_value());
    CHECK(base_res->contains(0));

    // Live arrival buffered and applied
    const auto live_trigrams = filename_trigram::ExtractTrigrams("incoming");
    const auto live_res = index.QueryCandidates(live_trigrams);
    REQUIRE(live_res.has_value());
    CHECK(live_res->contains(2));
  }

  TEST_CASE("Clear cancels in-flight build (stale generation dropped)") {
    PathStorage storage;
    (void)storage.InsertPath(1, "C:\\base\\old_file.txt", false, std::nullopt);

    filename_trigram::FilenameTrigramIndex index;
    const uint64_t gen = index.MarkBuilding();
    auto postings = index.ExtractPostings(storage.GetReadOnlyView(), storage.GetStorageSize());

    // User or crawl clears index while build is in flight
    index.Clear();
    CHECK(index.IsReady());
    CHECK(index.GetMemoryUsageBytes() == 0);

    // Old task finishes and tries to commit with stale gen
    index.BuildFromPostings(gen, std::move(postings));

    // Stale postings must NOT have resurrected the index!
    const auto old_trigrams = filename_trigram::ExtractTrigrams("old_file");
    CHECK(!index.QueryCandidates(old_trigrams).has_value());
    CHECK(index.GetMemoryUsageBytes() == 0);
  }

  TEST_CASE("Concurrent mutations during build") {
    PathStorage storage;
    for (size_t i = 1; i <= 20; ++i) {
      (void)storage.InsertPath(i, "C:\\base\\file_" + std::to_string(i) + ".txt", false, std::nullopt);
    }

    filename_trigram::FilenameTrigramIndex index;
    const uint64_t gen = index.MarkBuilding();
    auto postings = index.ExtractPostings(storage.GetReadOnlyView(), storage.GetStorageSize());

    // Launch build on background thread
    std::thread worker([&index, gen, p = std::move(postings)]() mutable {
      index.BuildFromPostings(gen, std::move(p));
    });

    // Main thread concurrently adds and removes entries
    index.AddEntry(100, "concurrent_incoming.log");
    index.AddEntry(101, "concurrent_temp.log");
    index.RemoveEntry(101, "concurrent_temp.log");

    worker.join();
    CHECK(index.IsReady());

    // Base entries are queryable
    const auto file_trigrams = filename_trigram::ExtractTrigrams("file_1");
    const auto file_res = index.QueryCandidates(file_trigrams);
    REQUIRE(file_res.has_value());
    CHECK(file_res->contains(0)); // row 0 is file_1

    // Added entry is queryable
    const auto inc_trigrams = filename_trigram::ExtractTrigrams("incoming");
    const auto inc_res = index.QueryCandidates(inc_trigrams);
    REQUIRE(inc_res.has_value());
    CHECK(inc_res->contains(100));

    // Removed entry was pruned
    const auto temp_trigrams = filename_trigram::ExtractTrigrams("temp");
    CHECK(!index.QueryCandidates(temp_trigrams).has_value());
  }

  TEST_CASE("AbortBuild preserves existing index and replays buffered mutations") {
    PathStorage storage;
    (void)storage.InsertPath(1, "C:\\base\\existing.txt", false, std::nullopt);

    filename_trigram::FilenameTrigramIndex index;
    index.Build(storage.GetReadOnlyView(), storage.GetStorageSize());
    CHECK(index.IsReady());

    // Initiate new build that will fail/abort
    const uint64_t gen = index.MarkBuilding();
    CHECK(!index.IsReady());

    // Live arrival buffered during build
    index.AddEntry(5, "live_arrival.dat");

    // Abort the build (simulating exception or cancellation)
    index.AbortBuild(gen);

    // Existing index must remain ready
    CHECK(index.IsReady());

    // Existing entries must still match
    const auto exist_trigrams = filename_trigram::ExtractTrigrams("existing");
    const auto exist_res = index.QueryCandidates(exist_trigrams);
    REQUIRE(exist_res.has_value());
    CHECK(exist_res->contains(0));

    // Buffered live arrival must have been replayed onto the surviving map!
    const auto live_trigrams = filename_trigram::ExtractTrigrams("arrival");
    const auto live_res = index.QueryCandidates(live_trigrams);
    REQUIRE(live_res.has_value());
    CHECK(live_res->contains(5));
  }

  TEST_CASE("AbortBuild on empty index without mutations remains unready") {
    filename_trigram::FilenameTrigramIndex index;
    const uint64_t gen = index.MarkBuilding();
    CHECK(!index.IsReady());

    index.AbortBuild(gen);
    CHECK(!index.IsReady()); // Remains not-ready because posting_lists_ is empty

    // Next successful build should succeed cleanly
    PathStorage storage;
    (void)storage.InsertPath(1, "C:\\new\\fresh.txt", false, std::nullopt);
    index.Build(storage.GetReadOnlyView(), storage.GetStorageSize());
    CHECK(index.IsReady());

    const auto fresh_trigrams = filename_trigram::ExtractTrigrams("fresh");
    const auto fresh_res = index.QueryCandidates(fresh_trigrams);
    REQUIRE(fresh_res.has_value());
    CHECK(fresh_res->contains(0));
  }

  TEST_CASE("RebuildAsync synchronous when thread pool is null") {
    PathStorage storage;
    (void)storage.InsertPath(1, "C:\\folder\\sync_file.txt", false, std::nullopt);

    filename_trigram::FilenameTrigramIndex index;
    index.RebuildAsync(storage.GetReadOnlyView(), storage.GetStorageSize(), nullptr);

    CHECK(index.IsReady());
    const auto trigrams = filename_trigram::ExtractTrigrams("sync_file");
    const auto res = index.QueryCandidates(trigrams);
    REQUIRE(res.has_value());
    CHECK(res->contains(0));
  }

  TEST_CASE("RebuildAsync offloads to SearchThreadPool") {
    PathStorage storage;
    (void)storage.InsertPath(1, "C:\\folder\\async_file.txt", false, std::nullopt);

    auto thread_pool = std::make_shared<SearchThreadPool>(2);
    filename_trigram::FilenameTrigramIndex index;
    index.RebuildAsync(storage.GetReadOnlyView(), storage.GetStorageSize(), thread_pool);

    for (int retry = 0; retry < 100 && !index.IsReady(); ++retry) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(index.IsReady());

    const auto trigrams = filename_trigram::ExtractTrigrams("async_file");
    const auto res = index.QueryCandidates(trigrams);
    REQUIRE(res.has_value());
    CHECK(res->contains(0));
  }

  TEST_CASE("RebuildAsync on large index") {
    PathStorage storage;
    for (size_t i = 1; i <= 25'000; ++i) {
      (void)storage.InsertPath(i, "C:\\folder\\file_" + std::to_string(i) + ".txt", false, std::nullopt);
    }

    auto thread_pool = std::make_shared<SearchThreadPool>(4);
    filename_trigram::FilenameTrigramIndex index;
    index.RebuildAsync(storage.GetReadOnlyView(), storage.GetStorageSize(), thread_pool);
    for (int retry = 0; retry < 200 && !index.IsReady(); ++retry) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(index.IsReady());

    const auto trigrams = filename_trigram::ExtractTrigrams("file_12345");
    const auto res = index.QueryCandidates(trigrams);
    REQUIRE(res.has_value());
    CHECK(res->contains(12344));  // 0-indexed in storage
  }

  TEST_CASE("Back-to-back SubmitBuild calls converge on the newest snapshot") {
    PathStorage first_storage;
    for (size_t i = 1; i <= 25'000; ++i) {
      (void)first_storage.InsertPath(i, "C:\\first\\old_file_" + std::to_string(i) + ".txt", false, std::nullopt);
    }
    PathStorage second_storage;
    for (size_t i = 1; i <= 25'000; ++i) {
      (void)second_storage.InsertPath(i, "C:\\second\\new_file_" + std::to_string(i) + ".txt", false, std::nullopt);
    }

    auto thread_pool = std::make_shared<SearchThreadPool>(4);
    filename_trigram::FilenameTrigramIndex index;

    // Two submissions back to back. The index must reflect the second snapshot.
    //
    // Scope note: this asserts the supersession contract, which the generation
    // guard provides on its own. It does NOT cover the wait inside SubmitBuild,
    // which exists for a different reason: keeping at most one task referencing
    // `this` in flight so that ~FilenameTrigramIndex waiting on the single
    // build_future_ is sufficient. That is a lifetime invariant with no
    // deterministic unit-level assertion.
    index.SubmitBuild(index.BeginRebuild(first_storage.GetReadOnlyView(), first_storage.GetStorageSize()), thread_pool);
    index.SubmitBuild(index.BeginRebuild(second_storage.GetReadOnlyView(), second_storage.GetStorageSize()), thread_pool);

    for (int retry = 0; retry < 300 && !index.IsReady(); ++retry) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(index.IsReady());

    const auto new_trigrams = filename_trigram::ExtractTrigrams("new_file_1");
    const auto new_res = index.QueryCandidates(new_trigrams);
    REQUIRE(new_res.has_value());
    CHECK(new_res->contains(0));

    const auto old_trigrams = filename_trigram::ExtractTrigrams("old_file_1");
    CHECK(!index.QueryCandidates(old_trigrams).has_value());
  }

}



