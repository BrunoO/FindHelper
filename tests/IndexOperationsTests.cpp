#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <atomic>
#include <shared_mutex>

#include "TestHelpers.h"
#include "crawler/IndexOperations.h"
#include "index/FileIndexStorage.h"
#include "path/PathOperations.h"
#include "path/PathStorage.h"

TEST_CASE("IndexOperations::Insert creates file entry") {
  test_helpers::TestIndexOperationsFixture fixture;

  // Insert root directory first
  fixture.GetOperations().Insert(1, 0, "root", true);

  // Insert a file
  fixture.GetOperations().Insert(2, 1, "test.txt", false);

  // Verify file exists in storage
  const FileEntry* entry = fixture.GetStorage().GetEntry(2);
  CHECK(entry != nullptr);
  CHECK(entry->name_length == 8);  // len("test.txt")
  CHECK(entry->isDirectory == false);
  CHECK(entry->parentID == 1);

  // Verify path was inserted
  const std::string path = fixture.GetPathStorage().GetPath(2);
  CHECK(path.find("test.txt") != std::string::npos);
}

TEST_CASE("IndexOperations::Insert creates directory entry") {
  test_helpers::TestIndexOperationsFixture fixture;

  // Insert root directory
  fixture.GetOperations().Insert(1, 0, "root", true);

  // Insert a subdirectory
  fixture.GetOperations().Insert(2, 1, "subdir", true);

  // Verify directory exists in storage
  const FileEntry* entry = fixture.GetStorage().GetEntry(2);
  CHECK(entry != nullptr);
  CHECK(entry->name_length == 6);  // len("subdir")
  CHECK(entry->isDirectory == true);
  CHECK(entry->parentID == 1);
}

TEST_CASE("IndexOperations::Remove removes file entry") {
  test_helpers::TestIndexOperationsFixture fixture;

  // Insert root directory
  fixture.GetOperations().Insert(1, 0, "root", true);

  // Insert a file
  fixture.GetOperations().Insert(2, 1, "test.txt", false);

  // Verify file exists
  CHECK(fixture.GetStorage().GetEntry(2) != nullptr);

  // Remove file
  fixture.GetOperations().Remove(2);

  // Verify file is removed
  CHECK(fixture.GetStorage().GetEntry(2) == nullptr);

  // Verify path is marked as deleted
  CHECK(fixture.GetPathStorage().GetDeletedCount() > 0);
}

TEST_CASE("IndexOperations::Remove handles non-existent file") {
  test_helpers::TestIndexOperationsFixture fixture;

  // Try to remove non-existent file
  fixture.GetOperations().Remove(999);

  // Should track that file was not in index
  CHECK(fixture.GetRemoveNotInIndexCount().load() == 1);
}

TEST_CASE("IndexOperations::Rename updates file name") {
  test_helpers::TestIndexOperationsFixture fixture;

  // Insert root directory
  fixture.GetOperations().Insert(1, 0, "root", true);

  // Insert a file
  fixture.GetOperations().Insert(2, 1, "oldname.txt", false);

  // Rename file
  const bool result = fixture.GetOperations().Rename(2, "newname.txt");
  CHECK(result == true);

  // Verify name was updated
  const FileEntry* entry = fixture.GetStorage().GetEntry(2);
  CHECK(entry != nullptr);
  CHECK(entry->name_length == 11);  // len("newname.txt")

  // Verify path was updated
  const std::string path = fixture.GetPathStorage().GetPath(2);
  CHECK(path.find("newname.txt") != std::string::npos);
}

TEST_CASE("IndexOperations::Rename returns false for non-existent file") {
  test_helpers::TestIndexOperationsFixture fixture;

  // Try to rename non-existent file
  const bool result = fixture.GetOperations().Rename(999, "newname.txt");
  CHECK(result == false);
}

TEST_CASE("IndexOperations::Move updates parent directory") {
  test_helpers::TestIndexOperationsFixture fixture;

  // Insert root directory
  fixture.GetOperations().Insert(1, 0, "root", true);

  // Insert two directories
  fixture.GetOperations().Insert(2, 1, "dir1", true);
  fixture.GetOperations().Insert(3, 1, "dir2", true);

  // Insert a file in dir1
  fixture.GetOperations().Insert(4, 2, "file.txt", false);

  // Move file to dir2
  const bool result = fixture.GetOperations().Move(4, 3);
  CHECK(result == true);

  // Verify parent was updated
  const FileEntry* entry = fixture.GetStorage().GetEntry(4);
  CHECK(entry != nullptr);
  CHECK(entry->parentID == 3);

  // Verify path was updated
  const std::string path = fixture.GetPathStorage().GetPath(4);
  CHECK(path.find("dir2") != std::string::npos);
}

TEST_CASE("IndexOperations::Move returns false for non-existent file") {
  test_helpers::TestIndexOperationsFixture fixture;

  // Try to move non-existent file
  const bool result = fixture.GetOperations().Move(999, 1);
  CHECK(result == false);
}
