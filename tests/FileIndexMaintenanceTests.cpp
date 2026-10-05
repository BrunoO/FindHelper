#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "index/FileIndexMaintenance.h"
#include "path/PathStorage.h"
#include "search/FilenameTrigramExtractor.h"
#include "TestHelpers.h"
#include <atomic>
#include <cstdint>
#include <shared_mutex>
#include <string_view>
#include <vector>

TEST_CASE("FileIndexMaintenance::Maintain returns false when no deleted entries") {
  test_helpers::TestFileIndexMaintenanceFixture fixture;

  // No entries, so no maintenance needed
  CHECK(fixture.GetMaintenance().Maintain() == false);
}

TEST_CASE("FileIndexMaintenance::Maintain triggers rebuild when threshold exceeded") {
  test_helpers::TestFileIndexMaintenanceFixture fixture;

  // Insert some paths
  fixture.InsertTestPaths(1, 3);

  // Remove paths to create deleted entries
  fixture.RemoveTestPaths(1, 3);

  // Insert many more paths to exceed absolute threshold
  fixture.InsertAndRemoveTestPaths(4, FileIndexMaintenance::kRebuildDeletedCountThreshold + 7);

  // Should trigger rebuild because deleted count exceeds threshold
  test_helpers::maintenance_test_helpers::VerifyMaintenanceResult(fixture, true, 0);
}

TEST_CASE("FileIndexMaintenance::Maintain triggers rebuild when percentage threshold exceeded") {
  // Use a pointer to path_storage that will be set after fixture construction
  // We can't capture fixture in the lambda because fixture doesn't exist yet during construction
  const PathStorage* path_storage_ptr = nullptr;
  test_helpers::TestFileIndexMaintenanceFixture fixture(
      [&path_storage_ptr]() {
        if (path_storage_ptr == nullptr) {
          return size_t(0);
        }
        return path_storage_ptr->GetSize() - path_storage_ptr->GetDeletedCount();
      });

  // Now set the pointer after fixture is constructed
  path_storage_ptr = &fixture.GetPathStorage();

  // Insert 1000 paths
  fixture.InsertTestPaths(1, 1000);

  // Remove 150 paths (15% > 10% threshold)
  fixture.RemoveTestPaths(1, 150);

  // Should trigger rebuild because percentage exceeds threshold
  test_helpers::maintenance_test_helpers::VerifyMaintenanceResult(fixture, true, 0);
}

TEST_CASE("FileIndexMaintenance::GetMaintenanceStats returns correct statistics") {
  test_helpers::TestFileIndexMaintenanceFixture fixture(5, 3, 2, []() { return size_t(1); });

  // Insert some paths
  fixture.InsertTestPaths(1, 2);
  fixture.RemoveTestPath(1);

  auto stats = fixture.GetMaintenance().GetMaintenanceStats();

  test_helpers::maintenance_test_helpers::VerifyMaintenanceStats(stats, 1, 2, 5, 3, 2);
}

TEST_CASE("FileIndexMaintenance::Maintain compacts the path buffer once thresholds trip") {
  test_helpers::TestFileIndexMaintenanceFixture fixture([]() { return size_t(1); });

  // Insert and remove paths (2/3 deleted > 10% threshold, so Maintain rebuilds)
  fixture.InsertTestPaths(1, 3);
  fixture.RemoveTestPaths(1, 2);

  CHECK(fixture.GetPathStorage().GetDeletedCount() == 2);
  CHECK(fixture.GetPathStorage().GetSize() == 3);

  // NOTE: RebuildPathBuffer is private (it must run via Maintain so the
  // trigram index is refreshed afterwards); this exercises that entry point.
  CHECK(fixture.GetMaintenance().Maintain() == true);

  // After rebuild, deleted entries should be removed
  CHECK(fixture.GetPathStorage().GetDeletedCount() == 0);
  CHECK(fixture.GetPathStorage().GetSize() == 1); // Only alive entry remains
}

TEST_CASE("FileIndexMaintenance::Maintain skips rebuild when no deleted entries") {
  test_helpers::TestFileIndexMaintenanceFixture fixture([]() { return size_t(2); });

  // Insert paths but don't remove any
  fixture.InsertTestPaths(1, 2);

  CHECK(fixture.GetPathStorage().GetDeletedCount() == 0);
  size_t size_before = fixture.GetPathStorage().GetSize();

  CHECK(fixture.GetMaintenance().Maintain() == false);

  // Size should be unchanged
  CHECK(fixture.GetPathStorage().GetSize() == size_before);
  CHECK(fixture.GetPathStorage().GetDeletedCount() == 0);
}


TEST_CASE("FileIndexMaintenance::Maintain refreshes the trigram index") {
  // Pins the entry-point contract: RebuildPathBuffer is private so a
  // compaction always pairs with its trigram snapshot. The fixture inserts
  // bypass the incremental AddEntry path, so the trigram starts empty and
  // only the rebuild inside Maintain() can populate it.
  test_helpers::TestFileIndexMaintenanceFixture fixture([] { return size_t(1); });

  fixture.InsertTestPath(1, "C:\\test\\keeper_key_file.txt");
  fixture.InsertTestPath(2, "C:\\test\\other.txt");
  fixture.InsertTestPath(3, "C:\\test\\another.txt");
  fixture.RemoveTestPath(2);
  fixture.RemoveTestPath(3);

  const std::vector<filename_trigram::TrigramKey> trigrams =
      filename_trigram::ExtractTrigrams("key");
  REQUIRE(!trigrams.empty());
  CHECK(!fixture.GetTrigramIndex().QueryCandidates(trigrams).has_value());

  // 2/3 deleted trips the percentage threshold.
  REQUIRE(fixture.GetMaintenance().Maintain() == true);

  // The null pool provider builds synchronously: ready immediately, no wait loop.
  REQUIRE(fixture.GetTrigramIndex().IsReady());
  size_t keeper_idx = fixture.GetPathStorage().GetSize();
  for (size_t i = 0; i < fixture.GetPathStorage().GetSize(); ++i) {
    if (fixture.GetPathStorage().GetPathByIndex(i) == "C:\\test\\keeper_key_file.txt") {
      keeper_idx = i;
    }
  }
  REQUIRE(keeper_idx < fixture.GetPathStorage().GetSize());
  const auto after = fixture.GetTrigramIndex().QueryCandidates(trigrams);
  REQUIRE(after.has_value());
  CHECK(after->cardinality() == 1);
  CHECK(after->contains(static_cast<uint32_t>(keeper_idx)));
}
