#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "path/PathStorage.h"
#include "search/CombinedIndexPostings.h"
#include "search/ExtensionIndex.h"
#include "search/FilenameTrigramIndex.h"
#include "search/SearchThreadPool.h"
#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

[[nodiscard]] std::vector<extension_index::ExtensionGroup> MakeGroups(
    std::vector<std::pair<std::string, std::vector<uint32_t>>> entries) {
  std::vector<extension_index::ExtensionGroup> groups;
  groups.reserve(entries.size());
  for (auto& [ext, rows] : entries) {
    groups.push_back({std::move(ext), std::move(rows)});
  }
  return groups;
}

void WaitUntilReady(const extension_index::ExtensionIndex& index, int max_retries = 200) {
  for (int retry = 0; retry < max_retries && !index.IsReady(); ++retry) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

}  // namespace

TEST_SUITE("ExtensionIndex") {

  TEST_CASE("Empty index queries") {
    const extension_index::ExtensionIndex index;
    CHECK(index.GetMemoryUsageBytes() == 0);

    // Empty filter returns nullopt
    CHECK(!index.QueryCandidates({}).has_value());
    CHECK(!index.UnionCardinality({}).has_value());

    // Query on unknown extension returns definite empty (has_value, empty)
    const auto res = index.QueryCandidates({"cpp"});
    REQUIRE(res.has_value());
    CHECK(res->isEmpty());
    const auto card = index.UnionCardinality({"cpp"});
    REQUIRE(card.has_value());
    CHECK(*card == 0);
  }

  TEST_CASE("NormalizeExtension") {
    CHECK(extension_index::ExtensionIndex::NormalizeExtension(".cpp") == "cpp");
    CHECK(extension_index::ExtensionIndex::NormalizeExtension("CPP") == "cpp");
    CHECK(extension_index::ExtensionIndex::NormalizeExtension(".PyThOn") == "python");
    CHECK(extension_index::ExtensionIndex::NormalizeExtension("") == "");
    CHECK(extension_index::ExtensionIndex::NormalizeExtension(".") == "");
  }

  TEST_CASE("AddEntry and QueryCandidates") {
    extension_index::ExtensionIndex index;
    index.AddEntry(10, "cpp");
    index.AddEntry(20, ".CPP");  // leading dot + case both normalize
    index.AddEntry(30, "h");

    CHECK(index.GetMemoryUsageBytes() > 0);

    const auto cpp_res = index.QueryCandidates({"cpp"});
    REQUIRE(cpp_res.has_value());
    CHECK(cpp_res->cardinality() == 2);
    CHECK(cpp_res->contains(10));
    CHECK(cpp_res->contains(20));
    CHECK(!cpp_res->contains(30));

    // Empty extension is a no-op: no key created
    index.AddEntry(40, "");
    index.AddEntry(41, ".");
    CHECK(!index.QueryCandidates({"cpp"})->contains(40));
  }

  TEST_CASE("Union over multiple extensions") {
    extension_index::ExtensionIndex index;
    index.AddEntry(1, "cpp");
    index.AddEntry(2, "h");
    index.AddEntry(3, "txt");

    const auto res = index.QueryCandidates({"cpp", "h"});
    REQUIRE(res.has_value());
    CHECK(res->cardinality() == 2);

    // Unknown terms contribute nothing to the union
    const auto partial = index.QueryCandidates({"cpp", "nonexistent"});
    REQUIRE(partial.has_value());
    CHECK(partial->cardinality() == 1);

    // All-unknown filter is definite empty, not unknown
    const auto none = index.QueryCandidates({"nonexistent"});
    REQUIRE(none.has_value());
    CHECK(none->isEmpty());
  }

  TEST_CASE("UnionCardinality agrees with materialized union") {
    extension_index::ExtensionIndex index;
    index.AddEntry(1, "cpp");
    index.AddEntry(2, "cpp");
    index.AddEntry(3, "h");

    const auto card = index.UnionCardinality({"cpp", "h"});
    const auto res = index.QueryCandidates({"cpp", "h"});
    REQUIRE(card.has_value());
    REQUIRE(res.has_value());
    CHECK(*card == res->cardinality());
    CHECK(*card == 3);

    CHECK(index.UnionCardinality({"cpp"}) == 2);
    CHECK(index.UnionCardinality({"nonexistent"}) == 0);
  }

  TEST_CASE("UnionCardinality dedupes terms; any empty term declines") {
    extension_index::ExtensionIndex index;
    index.AddEntry(1, "cpp");
    index.AddEntry(2, "h");

    // Duplicate terms count once (posting lists are disjoint; the sum over
    // unique terms is exact).
    CHECK(index.UnionCardinality({"cpp", "CPP", ".cpp"}) == 1);
    CHECK(index.UnionCardinality({"cpp", "h", "cpp"}) == 2);

    // A term normalizing to empty (".") matches extension-less files in scan
    // mode — a set the index cannot express — so the whole query declines
    // to nullopt (caller falls back) instead of returning a subset.
    CHECK(!index.UnionCardinality({"cpp", "."}).has_value());
    CHECK(!index.QueryCandidates({"cpp", "."}).has_value());
  }

  TEST_CASE("ExtractCandidateIdsNormalized - disjoint extraction") {
    extension_index::ExtensionIndex index;
    index.AddEntry(10, "cpp");
    index.AddEntry(20, "cpp");
    index.AddEntry(30, "h");
    index.AddEntry(40, "txt");

    std::vector<uint32_t> ids;

    // Single term
    REQUIRE(index.ExtractCandidateIds({"cpp"}, ids));
    CHECK(ids.size() == 2);
    CHECK(ids[0] == 10);
    CHECK(ids[1] == 20);

    // Multi-term extraction (pairwise disjoint)
    REQUIRE(index.ExtractCandidateIds({"cpp", "h", "txt"}, ids));
    CHECK(ids.size() == 4);
    std::sort(ids.begin(), ids.end());  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
    CHECK(ids == std::vector<uint32_t>{10, 20, 30, 40});

    // Unknown terms contribute nothing
    REQUIRE(index.ExtractCandidateIds({"cpp", "nonexistent"}, ids));
    CHECK(ids.size() == 2);

    // All-unknown filter returns true with empty vector (definite empty)
    REQUIRE(index.ExtractCandidateIds({"nonexistent"}, ids));
    CHECK(ids.empty());

    // Empty vector returns true with empty vector
    REQUIRE(index.ExtractCandidateIds({}, ids));
    CHECK(ids.empty());

    // Not-ready index returns false
    const uint64_t gen = index.MarkBuilding();
    REQUIRE(!index.IsReady());
    CHECK(!index.ExtractCandidateIds({"cpp"}, ids));
    CHECK(!index.ExtractCandidateIdsNormalized({"cpp"}, ids));
    index.AbortBuild(gen);
    REQUIRE(index.IsReady());
    REQUIRE(index.ExtractCandidateIds({"cpp"}, ids));
    CHECK(ids.size() == 2);
  }

  TEST_CASE("RemoveEntry erases keys when bitmaps empty") {
    extension_index::ExtensionIndex index;
    index.AddEntry(1, "cpp");
    index.AddEntry(2, "h");

    index.RemoveEntry(1, "cpp");
    const auto res = index.QueryCandidates({"cpp"});
    REQUIRE(res.has_value());
    CHECK(res->isEmpty());

    // Removing the last row for "h" drops the key: memory returns to baseline
    index.RemoveEntry(2, "h");
    CHECK(index.GetMemoryUsageBytes() == 0);

    // Removing a non-existent entry is a no-op
    index.RemoveEntry(99, "cpp");
    index.RemoveEntry(1, "never-indexed");
  }

  TEST_CASE("Clear resets index") {
    extension_index::ExtensionIndex index;
    index.AddEntry(1, "cpp");
    REQUIRE(index.QueryCandidates({"cpp"})->cardinality() == 1);

    index.Clear();
    const auto res = index.QueryCandidates({"cpp"});
    REQUIRE(res.has_value());
    CHECK(res->isEmpty());
    CHECK(index.GetMemoryUsageBytes() == 0);
    CHECK(index.IsReady());
  }

  TEST_CASE("Build from PathStorage SoAView") {
    PathStorage storage;
    (void)storage.InsertPath(1, "C:\\docs\\report2026.DOCX", false, std::nullopt);
    (void)storage.InsertPath(2, "C:\\docs\\notes.txt", false, std::nullopt);
    (void)storage.InsertPath(3, "C:\\code\\main.cpp", false, std::nullopt);
    (void)storage.InsertPath(4, "C:\\code\\Makefile", false, std::nullopt);

    // Mark row 1 ("notes.txt") as deleted
    (void)storage.RemovePathByIndex(1);

    extension_index::ExtensionIndex index;
    index.Build(storage.GetReadOnlyView(), storage.GetStorageSize());
    CHECK(index.IsReady());

    // Uppercase extension normalized at build time
    const auto docx_res = index.QueryCandidates({"docx"});
    REQUIRE(docx_res.has_value());
    CHECK(docx_res->contains(0));

    // Deleted row invisible
    const auto txt_res = index.QueryCandidates({"txt"});
    REQUIRE(txt_res.has_value());
    CHECK(txt_res->isEmpty());

    const auto cpp_res = index.QueryCandidates({"cpp"});
    REQUIRE(cpp_res.has_value());
    CHECK(cpp_res->contains(2));

    // Extension-less file indexed nowhere
    CHECK(index.UnionCardinality({"cpp", "docx", "txt"}) == 2);
  }

  TEST_CASE("Async build and mutation buffering") {
    extension_index::ExtensionIndex index;
    CHECK(index.IsReady());

    const uint64_t gen = index.MarkBuilding();
    CHECK(!index.IsReady());

    // While building, queries short-circuit to nullopt for scan fallback
    CHECK(!index.QueryCandidates({"cpp"}).has_value());

    // Live arrival while the background populate is in-flight
    index.AddEntry(7, "dat");

    // Complete background build with a pre-extracted snapshot
    index.BuildFromPostings(gen, MakeGroups({{"cpp", {0, 1}}}));
    CHECK(index.IsReady());

    const auto base_res = index.QueryCandidates({"cpp"});
    REQUIRE(base_res.has_value());
    CHECK(base_res->contains(0));
    CHECK(base_res->contains(1));

    // Buffered arrival replayed at commit
    const auto live_res = index.QueryCandidates({"dat"});
    REQUIRE(live_res.has_value());
    CHECK(live_res->contains(7));
  }

  TEST_CASE("BeginRebuild without MarkBuilding is refused") {
    extension_index::ExtensionIndex index;
    index.AddEntry(1, "cpp");

    auto ticket = index.BeginRebuild(MakeGroups({{"h", {2}}}));
    index.SubmitBuild(std::move(ticket), nullptr);

    // Refused ticket commits nothing; live map untouched and ready
    CHECK(index.IsReady());
    REQUIRE(index.QueryCandidates({"cpp"})->contains(1));
    CHECK(index.QueryCandidates({"h"})->isEmpty());
  }

  TEST_CASE("Clear cancels in-flight build (stale generation dropped)") {
    extension_index::ExtensionIndex index;
    const uint64_t gen = index.MarkBuilding();

    // User or crawl clears index while build is in flight
    index.Clear();
    CHECK(index.IsReady());
    CHECK(index.GetMemoryUsageBytes() == 0);

    // Old task finishes and tries to commit with stale gen
    index.BuildFromPostings(gen, MakeGroups({{"cpp", {0}}}));

    // Stale groups must NOT have resurrected the index
    CHECK(index.QueryCandidates({"cpp"})->isEmpty());
    CHECK(index.GetMemoryUsageBytes() == 0);
  }

  TEST_CASE("Concurrent mutations during build") {
    extension_index::ExtensionIndex index;
    const uint64_t gen = index.MarkBuilding();

    // Launch build on background thread
    std::thread worker([&index, gen, g = MakeGroups({{"txt", {0, 1, 2}}})]() mutable {
      index.BuildFromPostings(gen, std::move(g));
    });

    // Main thread concurrently adds and removes entries
    index.AddEntry(100, "log");
    index.AddEntry(101, "log");
    index.RemoveEntry(101, "log");

    worker.join();
    CHECK(index.IsReady());

    const auto txt_res = index.QueryCandidates({"txt"});
    REQUIRE(txt_res.has_value());
    CHECK(txt_res->cardinality() == 3);

    const auto inc_res = index.QueryCandidates({"log"});
    REQUIRE(inc_res.has_value());
    CHECK(inc_res->contains(100));
    CHECK(!inc_res->contains(101));
  }

  TEST_CASE("AbortBuild preserves existing index and replays buffered mutations") {
    extension_index::ExtensionIndex index;
    index.AddEntry(0, "txt");
    CHECK(index.IsReady());

    // Initiate new build that will fail/abort
    const uint64_t gen = index.MarkBuilding();
    CHECK(!index.IsReady());

    // Live arrival buffered during build
    index.AddEntry(5, "dat");

    // Abort the build (simulating exception or cancellation)
    index.AbortBuild(gen);

    // Existing index must remain ready with old + replayed entries
    CHECK(index.IsReady());
    CHECK(index.QueryCandidates({"txt"})->contains(0));
    CHECK(index.QueryCandidates({"dat"})->contains(5));
  }

  TEST_CASE("AbortBuild on empty index without mutations remains unready") {
    extension_index::ExtensionIndex index;
    const uint64_t gen = index.MarkBuilding();
    index.AbortBuild(gen);
    CHECK(!index.IsReady());
    CHECK(!index.QueryCandidates({"cpp"}).has_value());
  }

  // Characterization test (tech-debt D1): the extension index's replay ERASES
  // posting lists emptied by buffered removals. The trigram twin KEEPS them -
  // see "AbortBuild replay keeps emptied posting lists" in
  // FilenameTrigramIndexTests.cpp. Each replay mirrors its own index's live
  // RemoveEntry contract (this one erases per the extension-index spec §3;
  // the trigram never does: packed-int keys make kept-empty postings ~free,
  // and shared n-gram postings rarely empty). Unification keeps both policies
  // as an explicit parameter (index_build_lifecycle::ReplayPendingMutations).
  TEST_CASE("AbortBuild replay erases posting lists emptied by buffered removals") {
    extension_index::ExtensionIndex index;
    index.AddEntry(0, "txt");
    REQUIRE(index.IsReady());
    REQUIRE(index.GetMemoryUsageBytes() > 0);

    const uint64_t gen = index.MarkBuilding();
    index.RemoveEntry(0, "txt");  // buffered: last row of the only posting
    index.AbortBuild(gen);        // replays the buffered removal

    CHECK(index.GetMemoryUsageBytes() == 0);  // map empty again: key erased
    CHECK(!index.IsReady());                  // postings empty -> abort leaves unready
    CHECK(!index.QueryCandidates({"txt"}).has_value());
  }

  TEST_CASE("RebuildAsync synchronous when thread pool is null") {
    PathStorage storage;
    (void)storage.InsertPath(1, "C:\\folder\\sync_file.txt", false, std::nullopt);

    extension_index::ExtensionIndex index;
    index.RebuildAsync(storage.GetReadOnlyView(), storage.GetStorageSize(), nullptr);

    CHECK(index.IsReady());
    const auto res = index.QueryCandidates({"txt"});
    REQUIRE(res.has_value());
    CHECK(res->contains(0));
  }

  TEST_CASE("RebuildAsync offloads to SearchThreadPool") {
    PathStorage storage;
    (void)storage.InsertPath(1, "C:\\folder\\async_file.hpp", false, std::nullopt);

    auto thread_pool = std::make_shared<SearchThreadPool>(2);
    extension_index::ExtensionIndex index;
    index.RebuildAsync(storage.GetReadOnlyView(), storage.GetStorageSize(), thread_pool);

    WaitUntilReady(index);
    CHECK(index.IsReady());

    const auto res = index.QueryCandidates({"hpp"});
    REQUIRE(res.has_value());
    CHECK(res->contains(0));
  }

  TEST_CASE("Back-to-back SubmitBuild calls converge on the newest snapshot") {
    auto thread_pool = std::make_shared<SearchThreadPool>(2);
    extension_index::ExtensionIndex index;

    // Two submissions back to back. The index must reflect the second snapshot.
    (void)index.MarkBuilding();
    auto ticket1 = index.BeginRebuild(MakeGroups({{"old", {0}}}));
    (void)index.MarkBuilding();
    auto ticket2 = index.BeginRebuild(MakeGroups({{"new", {0}}}));
    index.SubmitBuild(std::move(ticket1), thread_pool);
    index.SubmitBuild(std::move(ticket2), thread_pool);

    WaitUntilReady(index, 300);
    CHECK(index.IsReady());

    CHECK(index.QueryCandidates({"new"})->contains(0));
    CHECK(index.QueryCandidates({"old"})->isEmpty());
  }

  TEST_CASE("IsBuilding lifecycle transitions") {
    extension_index::ExtensionIndex index;
    CHECK_FALSE(index.IsBuilding());

    const uint64_t gen = index.MarkBuilding();
    CHECK(index.IsBuilding());

    index.BuildFromPostings(gen, MakeGroups({{"cpp", {0}}}));
    CHECK_FALSE(index.IsBuilding());
    CHECK(index.IsReady());
  }

  TEST_CASE("Combined extractor trigram half matches ExtractPostings") {
    PathStorage storage;
    (void)storage.InsertPath(1, "C:\\docs\\Quarterly_Report2026.docx", false, std::nullopt);
    (void)storage.InsertPath(2, "C:\\docs\\ab.txt", false, std::nullopt);  // short name: no trigrams
    (void)storage.InsertPath(3, "C:\\code\\MAIN.CPP", false, std::nullopt);
    (void)storage.InsertPath(4, "C:\\code\\deleted_file.h", false, std::nullopt);
    (void)storage.RemovePathByIndex(3);  // deleted row invisible to both halves

    const CombinedIndexPostings combined =
        ExtractCombinedPostings(storage.GetReadOnlyView(), storage.GetStorageSize());
    filename_trigram::FilenameTrigramIndex trigram_probe;
    const std::vector<uint64_t> reference =
        trigram_probe.ExtractPostings(storage.GetReadOnlyView(), storage.GetStorageSize());

    CHECK(combined.trigram_postings == reference);
    CHECK(!combined.trigram_postings.empty());
  }

  TEST_CASE("Combined extractor extension half matches reference index") {
    PathStorage storage;
    (void)storage.InsertPath(1, "C:\\docs\\report.DOCX", false, std::nullopt);
    (void)storage.InsertPath(2, "C:\\docs\\notes.txt", false, std::nullopt);
    (void)storage.InsertPath(3, "C:\\code\\main.cpp", false, std::nullopt);
    (void)storage.InsertPath(4, "C:\\code\\helper.cpp", false, std::nullopt);
    (void)storage.InsertPath(5, "C:\\code\\Makefile", false, std::nullopt);  // no extension
    (void)storage.InsertPath(6, "C:\\code\\old.cpp", false, std::nullopt);
    (void)storage.RemovePathByIndex(5);  // deleted row invisible

    const CombinedIndexPostings combined =
        ExtractCombinedPostings(storage.GetReadOnlyView(), storage.GetStorageSize());

    extension_index::ExtensionIndex reference;
    reference.Build(storage.GetReadOnlyView(), storage.GetStorageSize());
    REQUIRE(reference.IsReady());

    size_t total_rows = 0;
    std::vector<std::string> all_exts;
    for (const auto& group : combined.extension_groups) {
      const auto res = reference.QueryCandidates({group.extension});
      REQUIRE(res.has_value());
      CHECK(res->cardinality() == group.row_ids.size());
      for (const uint32_t row : group.row_ids) {
        CHECK(res->contains(row));
      }
      total_rows += group.row_ids.size();
      all_exts.push_back(group.extension);
    }
    // cpp x2 (rows 2,3), docx (row 0), txt (row 1); Makefile + deleted row absent
    CHECK(total_rows == 4);
    CHECK(reference.UnionCardinality(all_exts) == total_rows);
  }

}  // TEST_SUITE("ExtensionIndex")
