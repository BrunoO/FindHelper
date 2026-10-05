#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "path/PathStorage.h"

TEST_CASE("PathStorage::BulkAppendPath works correctly") {
  PathStorage storage;

  const std::string path1 = R"(C:\test\file.txt)";
  const std::string path2 = R"(C:\test\folder)";
  const std::string path3 = R"(C:\test\archive.zip)";

  // Need to provide enough space; +1 for null termination
  const size_t expected_bytes = path1.length() + 1 + path2.length() + 1 + path3.length() + 1;

  storage.BeginBulkAppend(3, expected_bytes);

  const size_t idx1 = storage.BulkAppendPath(1, path1, false);
  const size_t idx2 = storage.BulkAppendPath(2, path2, true);
  const size_t idx3 = storage.BulkAppendPath(3, path3, false, 8, 15);

  storage.FinishBulkAppend();

  CHECK(idx1 == 0);
  CHECK(idx2 == 1);
  CHECK(idx3 == 2);

  const auto view = storage.GetReadOnlyView();
  REQUIRE(view.size == 3);

  CHECK(view.path_ids[0] == 1);
  CHECK(view.path_ids[1] == 2);
  CHECK(view.path_ids[2] == 3);

  CHECK(view.is_directory[0] == 0);
  CHECK(view.is_directory[1] == 1);
  CHECK(view.is_directory[2] == 0);

  CHECK(view.is_deleted[0] == 0);
  CHECK(view.is_deleted[1] == 0);
  CHECK(view.is_deleted[2] == 0);

  CHECK(view.filename_start[2] == 8);
  CHECK(view.extension_start[2] == 15);

  const auto stats = storage.GetStats();
  CHECK(stats.total_entries == 3);
  CHECK(stats.deleted_entries == 0);
  CHECK(stats.path_array_capacity >= 3);
}

TEST_CASE("PathStorage::BulkAppendPath handles implicit offsets") {
  PathStorage storage;
  const std::string path = R"(C:\test\file.txt)";

  storage.BeginBulkAppend(1, path.length() + 1);
  const size_t idx = storage.BulkAppendPath(1, path, false);
  storage.FinishBulkAppend();

  const auto view = storage.GetReadOnlyView();
  REQUIRE(view.size == 1);

  CHECK(idx == 0);
  CHECK(view.filename_start[0] == 8);
  CHECK(view.extension_start[0] == 13);
}

TEST_CASE("PathStorage::InsertPath handles new inserts and updates") {
  PathStorage storage;

  // Insert a new path
  const std::string path1 = R"(C:\folder\file1.txt)";
  const size_t idx1 = storage.InsertPath(101, path1, false);
  CHECK(idx1 == 0);
  CHECK(storage.GetPathByIndex(idx1) == path1);

  // In-place update with same length
  const std::string path2 = R"(C:\folder\file2.txt)";
  const size_t idx2 = storage.InsertPath(101, path2, false, idx1);
  CHECK(idx2 == idx1);
  CHECK(storage.GetPathByIndex(idx2) == path2);

  // Update with different length (causes tombstone + append)
  const std::string path3 = R"(C:\folder\longer_file_name.txt)";
  const size_t idx3 = storage.InsertPath(101, path3, false, idx2);
  CHECK(idx3 != idx2);
  CHECK(storage.GetPathByIndex(idx3) == path3);
  CHECK(storage.GetPathByIndex(idx2).empty());  // Old index is tombstoned
  CHECK(storage.GetDeletedCount() == 1);

  const auto view = storage.GetReadOnlyView();
  REQUIRE(view.size == 2);
  CHECK(view.is_deleted[0] == 1);
  CHECK(view.is_deleted[1] == 0);
  CHECK(view.path_ids[1] == 101);
}

TEST_CASE(
  "PathStorage::RebuildPathBuffer purges deleted entries and maintains SoA synchronization") {
  PathStorage storage;

  const size_t idx1 = storage.InsertPath(1, R"(C:\a.txt)", false);
  const size_t idx2 = storage.InsertPath(2, R"(C:\b.txt)", false);
  const size_t idx3 = storage.InsertPath(3, R"(C:\c.txt)", false);

  CHECK(idx1 == 0);
  CHECK(idx3 == 2);
  CHECK(storage.GetSize() == 3);

  // Remove middle item
  CHECK(storage.RemovePathByIndex(idx2) == true);
  CHECK(storage.GetDeletedCount() == 1);

  std::unordered_map<uint64_t, size_t> rebuilt_map;
  storage.RebuildPathBuffer(
    [&rebuilt_map](uint64_t id, size_t new_idx) { rebuilt_map[id] = new_idx; });

  CHECK(storage.GetSize() == 2);
  CHECK(storage.GetDeletedCount() == 0);
  CHECK(rebuilt_map.size() == 2);
  CHECK(rebuilt_map[1] == 0);
  CHECK(rebuilt_map[3] == 1);

  CHECK(storage.GetPathByIndex(0) == R"(C:\a.txt)");
  CHECK(storage.GetPathByIndex(1) == R"(C:\c.txt)");

  const auto view = storage.GetReadOnlyView();
  REQUIRE(view.size == 2);
  CHECK(view.path_ids[0] == 1);
  CHECK(view.path_ids[1] == 3);
  CHECK(view.is_deleted[0] == 0);
  CHECK(view.is_deleted[1] == 0);
}

TEST_CASE("PathStorage::UpdatePrefix rewrites descendants and remaps indices on length change") {
  PathStorage storage;

  const size_t idx1 = storage.InsertPath(1, R"(C:\old_folder\file1.txt)", false);
  const size_t idx2 = storage.InsertPath(2, R"(C:\old_folder\sub\file2.txt)", false);
  const size_t idx3 = storage.InsertPath(3, R"(C:\other_folder\file3.txt)", false);

  REQUIRE(idx1 == 0);
  REQUIRE(idx2 == 1);
  REQUIRE(idx3 == 2);

  // Unequal-length replacement: the rename cannot stay in place, so each
  // descendant tombstones its old slot and appends a new one. That is the only
  // path that invokes on_index_changed, so an equal-length prefix would leave
  // the callback untested.
  std::unordered_map<uint64_t, size_t> changed_indices;
  storage.UpdatePrefix(
    R"(C:\old_folder)", R"(C:\new_folder_name)",
    [&changed_indices](uint64_t id, size_t /*old_idx*/, size_t new_idx) { changed_indices[id] = new_idx; });

  // The non-descendant is left alone.
  CHECK(storage.GetPathByIndex(idx3) == R"(C:\other_folder\file3.txt)");

  // Rewritten descendants tombstone their previous slot.
  CHECK(storage.GetPathByIndex(idx1).empty());
  CHECK(storage.GetPathByIndex(idx2).empty());
  CHECK(storage.GetDeletedCount() == 2);

  REQUIRE(changed_indices.size() == 2);
  const size_t idx1_new = changed_indices.at(1);
  const size_t idx2_new = changed_indices.at(2);
  CHECK(idx1_new != idx1);
  CHECK(idx2_new != idx2);

  CHECK(storage.GetPathByIndex(idx1_new) == R"(C:\new_folder_name\file1.txt)");
  CHECK(storage.GetPathByIndex(idx2_new) == R"(C:\new_folder_name\sub\file2.txt)");

  // SoA arrays stay synchronized across the tombstone + append churn.
  const auto view = storage.GetReadOnlyView();
  REQUIRE(view.size == 5);
  CHECK(view.path_ids[idx1_new] == 1);
  CHECK(view.path_ids[idx2_new] == 2);
  CHECK(view.is_deleted[idx1] == 1);
  CHECK(view.is_deleted[idx2] == 1);
  CHECK(view.is_deleted[idx3] == 0);
  CHECK(view.is_deleted[idx1_new] == 0);
  CHECK(view.is_deleted[idx2_new] == 0);
}

TEST_CASE("PathStorage::Clear resets all state") {
  PathStorage storage;

  const size_t idx = storage.InsertPath(1, R"(C:\test.txt)", false);
  CHECK(idx == 0);
  CHECK(storage.GetSize() == 1);

  storage.Clear();
  CHECK(storage.GetSize() == 0);
  CHECK(storage.GetDeletedCount() == 0);

  const auto view = storage.GetReadOnlyView();
  CHECK(view.size == 0);
  CHECK(view.path_ids == nullptr);
}
