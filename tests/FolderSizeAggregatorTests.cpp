#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <chrono>
#include <thread>

#include "doctest/doctest.h"
#include "index/FileIndex.h"
#include "search/FolderSizeAggregator.h"

namespace {

// Spin-poll helper: waits up to 1 second for a result to become available.
std::optional<uint64_t> WaitForResult(const FolderSizeAggregator& aggregator, uint64_t folder_id) {
  for (int i = 0; i < 100; ++i) {
    if (const auto result = aggregator.GetResult(folder_id); result.has_value()) {
      return result;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return std::nullopt;
}

}  // namespace

TEST_CASE("FolderSizeAggregator - sums files under prefix") {
  FileIndex index;
  index.Insert(1, 0, "root", true);
  index.Insert(2, 1, "a.txt", false, {0, 0}, 100);
  index.Insert(3, 1, "b.txt", false, {0, 0}, 200);
  index.Insert(4, 1, "sub", true);
  index.Insert(5, 4, "c.txt", false, {0, 0}, 50);
  index.Insert(6, 0, "other", true);
  index.Insert(7, 6, "d.txt", false, {0, 0}, 1000);
  index.RecomputeAllPaths();

  FolderSizeAggregator aggregator(index);
  aggregator.Request(1, "/root");

  const auto result = WaitForResult(aggregator, 1);
  CHECK(result == std::optional<uint64_t>{350U});  // 100 + 200 + 50; /other/d.txt excluded
}

TEST_CASE("FolderSizeAggregator - excludes sibling directories") {
  FileIndex index;
  index.Insert(1, 0, "root", true);
  index.Insert(2, 1, "a.txt", false, {0, 0}, 100);
  index.Insert(6, 0, "other", true);
  index.Insert(7, 6, "b.txt", false, {0, 0}, 200);
  index.RecomputeAllPaths();

  FolderSizeAggregator aggregator(index);
  aggregator.Request(6, "/other");

  const auto result = WaitForResult(aggregator, 6);
  CHECK(result == std::optional<uint64_t>{200U});
}

TEST_CASE("FolderSizeAggregator - directory entries do not contribute to sum") {
  FileIndex index;
  index.Insert(1, 0, "root", true);
  index.Insert(2, 1, "sub", true);   // directory — must not add to size
  index.Insert(3, 2, "f.txt", false, {0, 0}, 50);
  index.RecomputeAllPaths();

  FolderSizeAggregator aggregator(index);
  aggregator.Request(1, "/root");

  const auto result = WaitForResult(aggregator, 1);
  CHECK(result == std::optional<uint64_t>{50U});  // /root/sub (dir) excluded; /root/sub/f.txt counted
}

TEST_CASE("FolderSizeAggregator - empty folder returns 0") {
  FileIndex index;
  index.Insert(1, 0, "empty", true);
  index.RecomputeAllPaths();

  FolderSizeAggregator aggregator(index);
  aggregator.Request(1, "/empty");

  const auto result = WaitForResult(aggregator, 1);
  CHECK(result == std::optional<uint64_t>{0U});
}

TEST_CASE("FolderSizeAggregator - non-existent path returns 0") {
  FileIndex index;
  FolderSizeAggregator aggregator(index);
  aggregator.Request(999, "/noexist");

  const auto result = WaitForResult(aggregator, 999);
  CHECK(result == std::optional<uint64_t>{0U});
}

TEST_CASE("FolderSizeAggregator - deduplicates requests") {
  FileIndex index;
  index.Insert(1, 0, "root", true);
  index.Insert(2, 1, "a.txt", false, {0, 0}, 100);
  index.RecomputeAllPaths();

  FolderSizeAggregator aggregator(index);
  aggregator.Request(1, "/root");
  aggregator.Request(1, "/root");  // Should be a no-op.

  const auto result = WaitForResult(aggregator, 1);
  CHECK(result == std::optional<uint64_t>{100U});
}

TEST_CASE("FolderSizeAggregator - Reset clears results") {
  FileIndex index;
  index.Insert(1, 0, "root", true);
  index.Insert(2, 1, "a.txt", false, {0, 0}, 100);
  index.RecomputeAllPaths();

  FolderSizeAggregator aggregator(index);
  aggregator.Request(1, "/root");

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
  aggregator.Request(1, "/root");
  aggregator.Reset();  // Bumps generation before job completes.

  // Give the worker time to finish the computation and attempt to write its result.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK_FALSE(aggregator.GetResult(1).has_value());
}
