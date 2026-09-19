#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "TestHelpers.h"
#include "doctest/doctest.h"
#include "index/FileIndex.h"
#include "index/NtfsFileReference.h"
#include "path/PathUtils.h"
#include "search/FolderSizeAggregator.h"

namespace {

// Spin-poll helper: waits up to ~2 seconds for a result (Windows CI can schedule the worker
// slowly).
std::optional<FolderSizeAggregator::FolderStats> WaitForResult(
  const FolderSizeAggregator& aggregator, uint64_t folder_id) {
  for (int i = 0; i < 200; ++i) {
    if (const auto result = aggregator.GetResult(folder_id); result.has_value()) {
      return result;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return std::nullopt;
}

// Shared tree for "sums under root" and "file_count under root" tests: root with a.txt, b.txt,
// sub/c.txt, plus /other/d.txt outside root (Sonar: duplicate 20-line setup blocks).
void InsertStandardBranchingTree(FileIndex& index) {
  index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "root", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(2), ntfs_file_reference::NtfsFileReference(1), "a.txt", false, {0, 0}, 100);
  index.Insert(ntfs_file_reference::NtfsFileReference(3), ntfs_file_reference::NtfsFileReference(1), "b.txt", false, {0, 0}, 200);
  index.Insert(ntfs_file_reference::NtfsFileReference(4), ntfs_file_reference::NtfsFileReference(1), "sub", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(5), ntfs_file_reference::NtfsFileReference(4), "c.txt", false, {0, 0}, 50);
  index.Insert(ntfs_file_reference::NtfsFileReference(6), ntfs_file_reference::NtfsFileReference(0), "other", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(7), ntfs_file_reference::NtfsFileReference(6), "d.txt", false, {0, 0}, 1000);
  index.RecomputeAllPaths();
}

[[nodiscard]] std::optional<FolderSizeAggregator::FolderStats> AggregatePath(
  FileIndex& index, uint64_t request_id, const char* relative_path) {
  FolderSizeAggregator aggregator(index);
  const std::string full_path =
    path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), relative_path);
  aggregator.Request(request_id, full_path);
  return WaitForResult(aggregator, request_id);
}

[[nodiscard]] std::optional<FolderSizeAggregator::FolderStats>
AggregateStandardBranchingTreeAtRoot() {
  FileIndex index;
  InsertStandardBranchingTree(index);
  return AggregatePath(index, 1, "root");
}

}  // namespace

TEST_CASE("FolderSizeAggregator - sums files under prefix") {
  const auto result = AggregateStandardBranchingTreeAtRoot();
  REQUIRE(result.has_value());
  CHECK(result.value().total_size == 350U);  // 100 + 200 + 50; /other/d.txt excluded
}

TEST_CASE("FolderSizeAggregator - excludes sibling directories") {
  FileIndex index;
  index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "root", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(2), ntfs_file_reference::NtfsFileReference(1), "a.txt", false, {0, 0}, 100);
  index.Insert(ntfs_file_reference::NtfsFileReference(6), ntfs_file_reference::NtfsFileReference(0), "other", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(7), ntfs_file_reference::NtfsFileReference(6), "b.txt", false, {0, 0}, 200);
  index.RecomputeAllPaths();

  const auto result = AggregatePath(index, 6, "other");
  REQUIRE(result.has_value());
  CHECK(result.value().total_size == 200U);
}

TEST_CASE("FolderSizeAggregator - directory entries do not contribute to sum") {
  FileIndex index;
  index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "root", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(2), ntfs_file_reference::NtfsFileReference(1), "sub", true);  // directory — must not add to size or count
  index.Insert(ntfs_file_reference::NtfsFileReference(3), ntfs_file_reference::NtfsFileReference(2), "f.txt", false, {0, 0}, 50);
  index.RecomputeAllPaths();

  const auto result = AggregatePath(index, 1, "root");
  REQUIRE(result.has_value());
  CHECK(result.value().total_size == 50U);  // /root/sub (dir) excluded; /root/sub/f.txt counted
}

TEST_CASE("FolderSizeAggregator - empty folder returns 0") {
  FileIndex index;
  index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "empty", true);
  index.RecomputeAllPaths();

  const auto result = AggregatePath(index, 1, "empty");
  REQUIRE(result.has_value());
  CHECK(result.value().total_size == 0U);
  CHECK(result.value().file_count == 0U);
}

TEST_CASE("FolderSizeAggregator - non-existent path returns 0") {
  FileIndex index;
  const auto result = AggregatePath(index, 999, "noexist");
  REQUIRE(result.has_value());
  CHECK(result.value().total_size == 0U);
  CHECK(result.value().file_count == 0U);
}

TEST_CASE("FolderSizeAggregator - deduplicates requests") {
  FileIndex index;
  index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "root", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(2), ntfs_file_reference::NtfsFileReference(1), "a.txt", false, {0, 0}, 100);
  index.RecomputeAllPaths();

  FolderSizeAggregator aggregator(index);
  const std::string root_path =
    path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "root");
  aggregator.Request(1, root_path);
  aggregator.Request(1, root_path);  // Should be a no-op.

  const auto result = WaitForResult(aggregator, 1);
  REQUIRE(result.has_value());
  CHECK(result.value().total_size == 100U);
}

TEST_CASE("FolderSizeAggregator - Reset clears results") {
  FileIndex index;
  index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "root", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(2), ntfs_file_reference::NtfsFileReference(1), "a.txt", false, {0, 0}, 100);
  index.RecomputeAllPaths();

  FolderSizeAggregator aggregator(index);
  const std::string root_path =
    path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "root");
  aggregator.Request(1, root_path);

  const auto before_reset = WaitForResult(aggregator, 1);
  CHECK(before_reset.has_value());

  aggregator.Reset();

  CHECK_FALSE(aggregator.GetResult(1).has_value());
}

TEST_CASE("FolderSizeAggregator - Reset discards in-flight result") {
  // Insert entries and immediately Reset before the worker completes.
  // The generation counter must cause the result to be discarded.
  FileIndex index;
  index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "root", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(2), ntfs_file_reference::NtfsFileReference(1), "a.txt", false, {0, 0}, 42);
  index.RecomputeAllPaths();

  FolderSizeAggregator aggregator(index);
  const std::string root_path =
    path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "root");
  aggregator.Request(1, root_path);
  aggregator.Reset();  // Bumps generation before job completes.

  // Give the worker time to finish the computation and attempt to write its result.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK_FALSE(aggregator.GetResult(1).has_value());
}

TEST_CASE("FolderSizeAggregator - Reset required after index inserts for fresh file_count") {
  // Repro: compute Descendant Files* once, then insert more files under the same folder.
  // CancelPending() preserves results_, so Request() is a no-op and the UI keeps the stale
  // count. Reset() clears the cache so a subsequent Request recomputes from the live index.
  //
  // Live USN CREATE never re-runs RecomputeAllPaths after initial population — Insert alone
  // maintains paths. A second RecomputeAllPaths would ReleaseNameCache() and break leaf names.
  FileIndex index;
  index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "root", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(2), ntfs_file_reference::NtfsFileReference(1), "a.txt", false, {0, 0}, 100);
  index.RecomputeAllPaths();

  FolderSizeAggregator aggregator(index);
  const std::string root_path =
    path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "root");
  aggregator.Request(1, root_path);

  const auto before = WaitForResult(aggregator, 1);
  REQUIRE(before.has_value());
  CHECK(before.value().file_count == 1U);

  index.Insert(ntfs_file_reference::NtfsFileReference(3), ntfs_file_reference::NtfsFileReference(1), "b.txt", false, {0, 0}, 200);
  index.Insert(ntfs_file_reference::NtfsFileReference(4), ntfs_file_reference::NtfsFileReference(1), "c.txt", false, {0, 0}, 300);

  // Mirrors SearchController new-search path without index-change invalidation:
  // CancelPending keeps the cache, so Request stays a no-op.
  aggregator.CancelPending();
  aggregator.Request(1, root_path);
  const auto stale = aggregator.GetResult(1);
  REQUIRE(stale.has_value());
  CHECK(stale.value().file_count == 1U);

  aggregator.Reset();
  aggregator.Request(1, root_path);
  const auto after = WaitForResult(aggregator, 1);
  REQUIRE(after.has_value());
  CHECK(after.value().file_count == 3U);
  CHECK(after.value().total_size == 600U);
}

TEST_CASE("FolderSizeAggregator - file_count counts only non-directory descendants") {
  const auto result = AggregateStandardBranchingTreeAtRoot();
  REQUIRE(result.has_value());
  // a.txt, b.txt, sub/c.txt → 3 files; /other/d.txt is outside /root; "sub" dir not counted
  CHECK(result.value().file_count == 3U);
}

TEST_CASE("FolderSizeAggregator - file_count is 0 for empty folder") {
  FileIndex index;
  index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "empty", true);
  index.RecomputeAllPaths();

  const auto result = AggregatePath(index, 1, "empty");
  REQUIRE(result.has_value());
  CHECK(result.value().file_count == 0U);
}

TEST_CASE("FolderSizeAggregator - file_count excludes sibling directory files") {
  FileIndex index;
  index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "root", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(2), ntfs_file_reference::NtfsFileReference(1), "a.txt", false, {0, 0}, 100);
  index.Insert(ntfs_file_reference::NtfsFileReference(6), ntfs_file_reference::NtfsFileReference(0), "other", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(7), ntfs_file_reference::NtfsFileReference(6), "b.txt", false, {0, 0}, 200);
  index.Insert(ntfs_file_reference::NtfsFileReference(8), ntfs_file_reference::NtfsFileReference(6), "c.txt", false, {0, 0}, 300);
  index.RecomputeAllPaths();

  FolderSizeAggregator aggregator(index);
  const std::string root_path =
    path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "root");
  const std::string other_path =
    path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "other");
  aggregator.Request(1, root_path);
  aggregator.Request(6, other_path);

  const auto root_result = WaitForResult(aggregator, 1);
  const auto other_result = WaitForResult(aggregator, 6);
  REQUIRE(root_result.has_value());
  REQUIRE(other_result.has_value());
  CHECK(root_result.value().file_count == 1U);   // only a.txt
  CHECK(other_result.value().file_count == 2U);  // b.txt + c.txt
}

TEST_CASE("FolderSizeAggregator - nested folders in one batch credit all ancestors") {
  FileIndex index;
  InsertStandardBranchingTree(index);

  FolderSizeAggregator aggregator(index);
  const std::string root_path =
      path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "root");
  const std::string sub_path = path_utils::JoinPath(root_path, "sub");
  aggregator.RequestBatch({{1, root_path}, {4, sub_path}});

  const auto root_result = WaitForResult(aggregator, 1);
  const auto sub_result = WaitForResult(aggregator, 4);
  REQUIRE(root_result.has_value());
  REQUIRE(sub_result.has_value());
  CHECK(root_result.value().total_size == 350U);  // a.txt + b.txt + sub/c.txt
  CHECK(root_result.value().file_count == 3U);
  CHECK(sub_result.value().total_size == 50U);  // c.txt only
  CHECK(sub_result.value().file_count == 1U);
}

TEST_CASE("FolderSizeAggregator - stale NTFS parent sequence still matches folder_id") {
  FileIndex index;
  index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(0), "root", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(2), ntfs_file_reference::NtfsFileReference(1), "a.txt", false, {0, 0}, 100);
  index.RecomputeAllPaths();

  {
    const std::unique_lock lock(index.GetMutex());
    FileEntry* const entry = index.GetEntryMutable(2);
    REQUIRE(entry != nullptr);
    entry->parentID = ntfs_file_reference::NtfsFileReference(1ULL | (99ULL << 48));  // same MFT record, stale sequence
  }

  const auto result = AggregatePath(index, 1, "root");
  REQUIRE(result.has_value());
  CHECK(result.value().total_size == 100U);
  CHECK(result.value().file_count == 1U);
}

TEST_CASE("FolderSizeAggregator - unindexed NTFS volume root parent still counts under folder") {
  // Drive-root children parent to MFT record 5, which is usually not in the index.
  FileIndex index;
  index.Insert(ntfs_file_reference::NtfsFileReference(10), ntfs_file_reference::NtfsFileReference(ntfs_file_reference::kRootDirectoryRecordNumber), "Windows", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(11), ntfs_file_reference::NtfsFileReference(10), "notepad.exe", false, {0, 0}, 100);
  index.RecomputeAllPaths();

  const auto result = AggregatePath(index, 10, "Windows");
  REQUIRE(result.has_value());
  CHECK(result.value().total_size == 100U);
  CHECK(result.value().file_count == 1U);
}

namespace {

// Create a real directory with `file_count` files of known sizes (size = 10 + i%5),
// insert it plus every file into the index as uncached entries (no sizes), and
// return the temp dir path plus its synthetic index id (0 if not found). The caller
// removes the directory when done.
struct RealFileTree {
  std::string dir_path;
  uint64_t dir_id = 0;
  uint64_t expected_total = 0;
};

[[nodiscard]] RealFileTree InsertRealFileTree(FileIndex& index, int file_count) {
  RealFileTree tree;
  tree.dir_path = test_helpers::CreateTempDirectory("fsa_size_agg");
  index.InsertPath(tree.dir_path, true);
  for (int i = 0; i < file_count; ++i) {
    const uint64_t size = 10U + static_cast<uint64_t>(i % 5);
    const std::string file_path = tree.dir_path + "/file_" + std::to_string(i) + ".bin";
    {
      std::ofstream out(file_path, std::ios::binary);
      out << std::string(size, 'x');
    }
    index.InsertPath(file_path, false);
    tree.expected_total += size;
  }
  index.ForEachEntryWithPath([&tree](uint64_t id, const FileEntry&, std::string_view path) {
    if (path == tree.dir_path) {
      tree.dir_id = id;
      return false;
    }
    return true;
  });
  return tree;
}

}  // namespace

TEST_CASE("FolderSizeAggregator - parallel stat branch sums uncached files above threshold") {
  FileIndex index;
  const RealFileTree tree = InsertRealFileTree(index, 300);  // >= kMinMissesForParallelStats

  REQUIRE(tree.dir_id != 0);
  FolderSizeAggregator aggregator(index);
  aggregator.Request(tree.dir_id, tree.dir_path);
  const auto result = WaitForResult(aggregator, tree.dir_id);
  REQUIRE(result.has_value());
  CHECK(result.value().total_size == tree.expected_total);
  CHECK(result.value().file_count == 300U);
  (void)std::filesystem::remove_all(tree.dir_path);
}

TEST_CASE("FolderSizeAggregator - sequential stat branch sums small uncached miss sets") {
  FileIndex index;
  const RealFileTree tree = InsertRealFileTree(index, 3);  // < kMinMissesForParallelStats

  REQUIRE(tree.dir_id != 0);
  FolderSizeAggregator aggregator(index);
  aggregator.Request(tree.dir_id, tree.dir_path);
  const auto result = WaitForResult(aggregator, tree.dir_id);
  REQUIRE(result.has_value());
  CHECK(result.value().total_size == tree.expected_total);
  CHECK(result.value().file_count == 3U);
  (void)std::filesystem::remove_all(tree.dir_path);
}
