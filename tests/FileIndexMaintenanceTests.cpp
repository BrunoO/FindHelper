#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "index/FileIndex.h"
#include "index/NtfsFileReference.h"
#include "search/FilenameTrigramExtractor.h"

namespace {

using ntfs_file_reference::NtfsFileReference;

void InsertRootAndFiles(FileIndex& index, uint64_t start_id, uint64_t count) {
  if (index.GetEntry(1) == nullptr) {
    index.Insert(NtfsFileReference(1), NtfsFileReference(0), "test", true);
  }
  for (uint64_t i = 0; i < count; ++i) {
    const uint64_t id = start_id + i;
    index.Insert(NtfsFileReference(id), NtfsFileReference(1),
                 "file" + std::to_string(id) + ".txt", false);
  }
}

void RemoveFiles(FileIndex& index, uint64_t start_id, uint64_t count) {
  for (uint64_t i = 0; i < count; ++i) {
    index.Remove(NtfsFileReference(start_id + i));
  }
}

void WaitForIndexBuilds(const FileIndex& index) {
  while (index.IsTrigramIndexBuilding() || index.IsExtensionIndexBuilding()) {
    std::this_thread::yield();
  }
}

}  // namespace

TEST_CASE("FileIndex::Maintain returns false when no deleted entries") {
  FileIndex index;
  CHECK(index.Maintain() == false);
}

TEST_CASE("FileIndex::Maintain triggers rebuild when threshold exceeded") {
  FileIndex index;

  InsertRootAndFiles(index, 2, FileIndex::kRebuildDeletedCountThreshold + 10);
  RemoveFiles(index, 2, FileIndex::kRebuildDeletedCountThreshold + 5);

  CHECK(index.Maintain() == true);
  WaitForIndexBuilds(index);
  CHECK(index.GetMaintenanceStats().deleted_count == 0);
}

TEST_CASE("FileIndex::Maintain triggers rebuild when percentage threshold exceeded") {
  FileIndex index;

  InsertRootAndFiles(index, 2, 100);
  RemoveFiles(index, 2, 15);  // ~15% > 10% threshold

  CHECK(index.Maintain() == true);
  WaitForIndexBuilds(index);
  CHECK(index.GetMaintenanceStats().deleted_count == 0);
}

TEST_CASE("FileIndex::GetMaintenanceStats returns correct statistics") {
  FileIndex index;

  InsertRootAndFiles(index, 2, 2);
  index.Remove(NtfsFileReference(2));
  index.Remove(NtfsFileReference(999));  // not in index

  const auto stats = index.GetMaintenanceStats();
  CHECK(stats.deleted_count == 1);
  CHECK(stats.total_entries == 3);  // root + 2 files
  CHECK(stats.remove_not_in_index_count == 1);
}

TEST_CASE("FileIndex::Maintain compacts the path buffer once thresholds trip") {
  FileIndex index;

  InsertRootAndFiles(index, 2, 3);
  RemoveFiles(index, 2, 2);

  CHECK(index.GetMaintenanceStats().deleted_count == 2);
  CHECK(index.GetTotalItems() == 4);  // root + 3 files

  CHECK(index.Maintain() == true);
  WaitForIndexBuilds(index);

  CHECK(index.GetMaintenanceStats().deleted_count == 0);
  CHECK(index.GetTotalItems() == 2);  // root + 1 surviving file
}

TEST_CASE("FileIndex::Maintain skips rebuild when no deleted entries") {
  FileIndex index;

  InsertRootAndFiles(index, 2, 2);
  const size_t size_before = index.GetTotalItems();

  CHECK(index.Maintain() == false);
  CHECK(index.GetTotalItems() == size_before);
  CHECK(index.GetMaintenanceStats().deleted_count == 0);
}

TEST_CASE("FileIndex::Maintain refreshes the trigram index") {
  FileIndex index;

  index.Insert(NtfsFileReference(1), NtfsFileReference(0), "test", true);
  index.Insert(NtfsFileReference(2), NtfsFileReference(1), "other.txt", false);
  index.Insert(NtfsFileReference(3), NtfsFileReference(1), "another.txt", false);
  index.Insert(NtfsFileReference(4), NtfsFileReference(1), "keeper_key_file.txt", false);
  index.Remove(NtfsFileReference(2));
  index.Remove(NtfsFileReference(3));

  const std::vector<filename_trigram::TrigramKey> trigrams =
      filename_trigram::ExtractTrigrams("key");
  REQUIRE(!trigrams.empty());

  REQUIRE(index.Maintain() == true);
  WaitForIndexBuilds(index);

  REQUIRE(index.GetTrigramIndex()->IsReady());
  const FileEntry* keeper = index.GetEntry(4);
  REQUIRE(keeper != nullptr);
  const auto after = index.GetTrigramIndex()->QueryCandidates(trigrams);
  REQUIRE(after.has_value());
  CHECK(after->cardinality() == 1);
  CHECK(after->contains(static_cast<uint32_t>(keeper->path_storage_index)));
}

TEST_CASE("FileIndex::Maintain refreshes the extension index") {
  FileIndex index;

  index.Insert(NtfsFileReference(1), NtfsFileReference(0), "test", true);
  index.Insert(NtfsFileReference(2), NtfsFileReference(1), "other.txt", false);
  index.Insert(NtfsFileReference(3), NtfsFileReference(1), "another.txt", false);
  index.Insert(NtfsFileReference(4), NtfsFileReference(1), "keeper_key_file.txt", false);
  index.Remove(NtfsFileReference(2));
  index.Remove(NtfsFileReference(3));

  REQUIRE(index.Maintain() == true);
  WaitForIndexBuilds(index);

  REQUIRE(index.GetExtensionIndex()->IsReady());
  const FileEntry* keeper = index.GetEntry(4);
  REQUIRE(keeper != nullptr);
  const auto after = index.GetExtensionIndex()->QueryCandidates({"txt"});
  REQUIRE(after.has_value());
  CHECK(after->cardinality() == 1);
  CHECK(after->contains(static_cast<uint32_t>(keeper->path_storage_index)));
}

