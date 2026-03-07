#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <atomic>
#include <shared_mutex>

#include "TestHelpers.h"
#include "crawler/IndexOperations.h"
#include "index/FileIndexStorage.h"
#include "path/DirectoryResolver.h"
#include "path/PathOperations.h"
#include "path/PathStorage.h"

TEST_CASE("DirectoryResolver::GetOrCreateDirectoryId returns 0 for empty path") {
  test_helpers::TestDirectoryResolverFixture fixture;

  const uint64_t root_id = fixture.GetResolver().GetOrCreateDirectoryId("");
  CHECK(root_id == 0);
}

TEST_CASE("DirectoryResolver::GetOrCreateDirectoryId creates single directory") {
  test_helpers::TestDirectoryResolverFixture fixture;

  const uint64_t dir_id = fixture.GetResolver().GetOrCreateDirectoryId("Documents");
  CHECK(dir_id > 0);
  CHECK(dir_id == 1); // First ID should be 1

  // Verify directory was created
  const FileEntry* entry = fixture.GetStorage().GetEntry(dir_id);
  CHECK(entry != nullptr);
  CHECK(entry->name_length == 9);  // len("Documents")
  CHECK(entry->isDirectory == true);
  CHECK(entry->parentID == 0); // Root

  // Verify directory was cached
  const uint64_t cached_id = fixture.GetStorage().GetDirectoryId("Documents");
  CHECK(cached_id == dir_id);
}

TEST_CASE("DirectoryResolver::GetOrCreateDirectoryId creates nested directories recursively") {
  test_helpers::TestDirectoryResolverFixture fixture;

  // Create nested directory path
  const uint64_t nested_id = fixture.GetResolver().GetOrCreateDirectoryId("Users/John/Documents");
  CHECK(nested_id > 0);

  // Verify all parent directories were created
  const uint64_t users_id = fixture.GetStorage().GetDirectoryId("Users");
  CHECK(users_id > 0);
  CHECK(users_id < nested_id);

  const uint64_t john_id = fixture.GetStorage().GetDirectoryId("Users/John");
  CHECK(john_id > 0);
  CHECK(john_id < nested_id);

  // Verify nested directory
  const FileEntry* nested_entry = fixture.GetStorage().GetEntry(nested_id);
  CHECK(nested_entry != nullptr);
  CHECK(nested_entry->name_length == 9);  // len("Documents")
  CHECK(nested_entry->isDirectory == true);
  CHECK(nested_entry->parentID == john_id);

  // Verify parent directory
  const FileEntry* john_entry = fixture.GetStorage().GetEntry(john_id);
  CHECK(john_entry != nullptr);
  CHECK(john_entry->name_length == 4);  // len("John")
  CHECK(john_entry->isDirectory == true);
  CHECK(john_entry->parentID == users_id);

  // Verify root parent directory
  const FileEntry* users_entry = fixture.GetStorage().GetEntry(users_id);
  CHECK(users_entry != nullptr);
  CHECK(users_entry->name_length == 5);  // len("Users")
  CHECK(users_entry->isDirectory == true);
  CHECK(users_entry->parentID == 0); // Root
}

TEST_CASE("DirectoryResolver::GetOrCreateDirectoryId uses cache for existing directories") {
  test_helpers::TestDirectoryResolverFixture fixture;

  // Create directory first time
  const uint64_t first_id = fixture.GetResolver().GetOrCreateDirectoryId("Documents");
  const uint64_t initial_next_id = fixture.GetNextFileId().load();

  // Get same directory second time (should use cache)
  const uint64_t second_id = fixture.GetResolver().GetOrCreateDirectoryId("Documents");
  const uint64_t final_next_id = fixture.GetNextFileId().load();

  // Should return same ID
  CHECK(first_id == second_id);

  // Should not have incremented next_file_id (cache hit)
  CHECK(initial_next_id == final_next_id);
}

TEST_CASE("DirectoryResolver::GetOrCreateDirectoryId handles Windows-style paths") {
  test_helpers::TestDirectoryResolverFixture fixture;

  // Create directory with Windows-style path separator
  const uint64_t dir_id = fixture.GetResolver().GetOrCreateDirectoryId("C:\\Users\\John");
  CHECK(dir_id > 0);

  // Verify directory was created
  const uint64_t cached_id = fixture.GetStorage().GetDirectoryId("C:\\Users\\John");
  CHECK(cached_id == dir_id);

  // Verify parent was created
  const uint64_t users_id = fixture.GetStorage().GetDirectoryId("C:\\Users");
  CHECK(users_id > 0);
  CHECK(users_id < dir_id);
}

TEST_CASE("DirectoryResolver::GetOrCreateDirectoryId handles Unix-style paths") {
  test_helpers::TestDirectoryResolverFixture fixture;

  // Create directory with Unix-style path separator
  const uint64_t dir_id = fixture.GetResolver().GetOrCreateDirectoryId("home/john/documents");
  CHECK(dir_id > 0);

  // Verify directory was created
  const uint64_t cached_id = fixture.GetStorage().GetDirectoryId("home/john/documents");
  CHECK(cached_id == dir_id);

  // Verify parent was created
  const uint64_t john_id = fixture.GetStorage().GetDirectoryId("home/john");
  CHECK(john_id > 0);
  CHECK(john_id < dir_id);
}
