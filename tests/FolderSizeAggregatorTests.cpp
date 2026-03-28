#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <chrono>
#include <thread>

#include "doctest/doctest.h"
#include "index/FileIndex.h"
#include "path/PathUtils.h"
#include "search/FolderSizeAggregator.h"

namespace {

// Spin-poll helper: waits up to ~2 seconds for a result (Windows CI can schedule the worker slowly).
std::optional<FolderSizeAggregator::FolderStats> WaitForResult(const FolderSizeAggregator& aggregator, uint64_t folder_id) {
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
  index.Insert(1, 0, "root", true);
  index.Insert(2, 1, "a.txt", false, {0, 0}, 100);
  index.Insert(3, 1, "b.txt", false, {0, 0}, 200);
  index.Insert(4, 1, "sub", true);
  index.Insert(5, 4, "c.txt", false, {0, 0}, 50);
  index.Insert(6, 0, "other", true);
  index.Insert(7, 6, "d.txt", false, {0, 0}, 1000);
  index.RecomputeAllPaths();
}

[[nodiscard]] std::optional<FolderSizeAggregator::FolderStats> AggregateStandardBranchingTreeAtRoot() {
  FileIndex index;
  InsertStandardBranchingTree(index);
  FolderSizeAggregator aggregator(index);
  const std::string root_path = path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "root");
  aggregator.Request(1, root_path);
  return WaitForResult(aggregator, 1);
}

}  // namespace

TEST_CASE("FolderSizeAggregator - sums files under prefix") {
  const auto result = AggregateStandardBranchingTreeAtRoot();
  REQUIRE(result.has_value());
  CHECK(result.value().total_size == 350U);  // 100 + 200 + 50; /other/d.txt excluded
}

TEST_CASE("FolderSizeAggregator - excludes sibling directories") {
  FileIndex index;
  index.Insert(1, 0, "root", true);
  index.Insert(2, 1, "a.txt", false, {0, 0}, 100);
  index.Insert(6, 0, "other", true);
  index.Insert(7, 6, "b.txt", false, {0, 0}, 200);
  index.RecomputeAllPaths();

  FolderSizeAggregator aggregator(index);
  const std::string other_path = path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "other");
  aggregator.Request(6, other_path);

  const auto result = WaitForResult(aggregator, 6);
  REQUIRE(result.has_value());
  const auto& stats = result.value();
  CHECK(stats.total_size == 200U);
}

TEST_CASE("FolderSizeAggregator - directory entries do not contribute to sum") {
  FileIndex index;
  index.Insert(1, 0, "root", true);
  index.Insert(2, 1, "sub", true);   // directory — must not add to size or count
  index.Insert(3, 2, "f.txt", false, {0, 0}, 50);
  index.RecomputeAllPaths();

  FolderSizeAggregator aggregator(index);
  const std::string root_path = path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "root");
  aggregator.Request(1, root_path);

  const auto result = WaitForResult(aggregator, 1);
  REQUIRE(result.has_value());
  const auto& stats = result.value();
  CHECK(stats.total_size == 50U);  // /root/sub (dir) excluded; /root/sub/f.txt counted
}

TEST_CASE("FolderSizeAggregator - empty folder returns 0") {
  FileIndex index;
  index.Insert(1, 0, "empty", true);
  index.RecomputeAllPaths();

  FolderSizeAggregator aggregator(index);
  const std::string empty_path = path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "empty");
  aggregator.Request(1, empty_path);

  const auto result = WaitForResult(aggregator, 1);
  REQUIRE(result.has_value());
  const auto& stats = result.value();
  CHECK(stats.total_size == 0U);
  CHECK(stats.file_count == 0U);
}

TEST_CASE("FolderSizeAggregator - non-existent path returns 0") {
  FileIndex index;
  FolderSizeAggregator aggregator(index);
  const std::string noexist_path = path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "noexist");
  aggregator.Request(999, noexist_path);

  const auto result = WaitForResult(aggregator, 999);
  REQUIRE(result.has_value());
  const auto& stats = result.value();
  CHECK(stats.total_size == 0U);
  CHECK(stats.file_count == 0U);
}

TEST_CASE("FolderSizeAggregator - deduplicates requests") {
  FileIndex index;
  index.Insert(1, 0, "root", true);
  index.Insert(2, 1, "a.txt", false, {0, 0}, 100);
  index.RecomputeAllPaths();

  FolderSizeAggregator aggregator(index);
  const std::string root_path = path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "root");
  aggregator.Request(1, root_path);
  aggregator.Request(1, root_path);  // Should be a no-op.

  const auto result = WaitForResult(aggregator, 1);
  REQUIRE(result.has_value());
  const auto& stats = result.value();
  CHECK(stats.total_size == 100U);
}

TEST_CASE("FolderSizeAggregator - Reset clears results") {
  FileIndex index;
  index.Insert(1, 0, "root", true);
  index.Insert(2, 1, "a.txt", false, {0, 0}, 100);
  index.RecomputeAllPaths();

  FolderSizeAggregator aggregator(index);
  const std::string root_path = path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "root");
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
  index.Insert(1, 0, "root", true);
  index.Insert(2, 1, "a.txt", false, {0, 0}, 42);
  index.RecomputeAllPaths();

  FolderSizeAggregator aggregator(index);
  const std::string root_path = path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "root");
  aggregator.Request(1, root_path);
  aggregator.Reset();  // Bumps generation before job completes.

  // Give the worker time to finish the computation and attempt to write its result.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK_FALSE(aggregator.GetResult(1).has_value());
}

TEST_CASE("FolderSizeAggregator - file_count counts only non-directory descendants") {
  const auto result = AggregateStandardBranchingTreeAtRoot();
  REQUIRE(result.has_value());
  // a.txt, b.txt, sub/c.txt → 3 files; /other/d.txt is outside /root; "sub" dir not counted
  CHECK(result.value().file_count == 3U);
}

TEST_CASE("FolderSizeAggregator - file_count is 0 for empty folder") {
  FileIndex index;
  index.Insert(1, 0, "empty", true);
  index.RecomputeAllPaths();

  FolderSizeAggregator aggregator(index);
  const std::string empty_path = path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "empty");
  aggregator.Request(1, empty_path);

  const auto result = WaitForResult(aggregator, 1);
  REQUIRE(result.has_value());
  const auto& stats = result.value();
  CHECK(stats.file_count == 0U);
}

TEST_CASE("FolderSizeAggregator - file_count excludes sibling directory files") {
  FileIndex index;
  index.Insert(1, 0, "root", true);
  index.Insert(2, 1, "a.txt", false, {0, 0}, 100);
  index.Insert(6, 0, "other", true);
  index.Insert(7, 6, "b.txt", false, {0, 0}, 200);
  index.Insert(8, 6, "c.txt", false, {0, 0}, 300);
  index.RecomputeAllPaths();

  FolderSizeAggregator aggregator(index);
  const std::string root_path = path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "root");
  const std::string other_path = path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "other");
  aggregator.Request(1, root_path);
  aggregator.Request(6, other_path);

  const auto root_result = WaitForResult(aggregator, 1);
  const auto other_result = WaitForResult(aggregator, 6);
  REQUIRE(root_result.has_value());
  REQUIRE(other_result.has_value());
  const auto& root_stats = root_result.value();
  const auto& other_stats = other_result.value();
  CHECK(root_stats.file_count == 1U);   // only a.txt
  CHECK(other_stats.file_count == 2U);  // b.txt + c.txt
}
