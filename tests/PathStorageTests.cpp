#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstddef>
#include <string>
#include <string_view>

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

