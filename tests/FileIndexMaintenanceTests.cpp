#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "index/FileIndexMaintenance.h"
#include "path/PathStorage.h"
#include "TestHelpers.h"
#include <atomic>
#include <shared_mutex>

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

TEST_CASE("FileIndexMaintenance::RebuildPathBuffer removes deleted entries") {
  test_helpers::TestFileIndexMaintenanceFixture fixture([]() { return size_t(1); });

  // Insert and remove paths
  fixture.InsertTestPaths(1, 3);
  fixture.RemoveTestPaths(1, 2);

  CHECK(fixture.GetPathStorage().GetDeletedCount() == 2);
  CHECK(fixture.GetPathStorage().GetSize() == 3);

  fixture.GetMaintenance().RebuildPathBuffer();

  // After rebuild, deleted entries should be removed
  CHECK(fixture.GetPathStorage().GetDeletedCount() == 0);
  CHECK(fixture.GetPathStorage().GetSize() == 1); // Only alive entry remains
}

TEST_CASE("FileIndexMaintenance::RebuildPathBuffer skips when no deleted entries") {
  test_helpers::TestFileIndexMaintenanceFixture fixture([]() { return size_t(2); });

  // Insert paths but don't remove any
  fixture.InsertTestPaths(1, 2);

  CHECK(fixture.GetPathStorage().GetDeletedCount() == 0);
  size_t size_before = fixture.GetPathStorage().GetSize();

  fixture.GetMaintenance().RebuildPathBuffer();

  // Size should be unchanged
  CHECK(fixture.GetPathStorage().GetSize() == size_before);
  CHECK(fixture.GetPathStorage().GetDeletedCount() == 0);
}

