/**
 * @file FileEntryAttributesTests.cpp
 * @brief Regression tests for indexed NTFS attribute bitmasks (offline plan).
 *
 * Covers: exact offline classification via FileEntry::IsOffline (replacing
 * the retired IsLikelyCloudFile failure-heuristic and IsOneDriveFile
 * substring probe), insert-time carriage through Insert/InsertBatch, and
 * UpdateFileAttributes refresh. In particular, an entry with a zero mask
 * (e.g. access-denied at enumerate time) must classify as local — the old
 * heuristic treated unreadable files as cloud.
 */

#include <cstdint>
#include <string_view>
#include <vector>

#include "doctest/doctest.h"
#include "index/FileIndex.h"
#include "index/FileIndexStorage.h"
#include "index/FileName.h"

namespace {

constexpr uint64_t kVolumeRootFrn = 0x0001000000000005ULL;
constexpr uint64_t kDirId = 0x0001000000000100ULL;
constexpr uint64_t kFileId = 0x0001000000000200ULL;
constexpr uint32_t kArchive = 0x20U;

FileIndex::PopulationBatchEntry MakeBatchEntry(uint64_t id, uint64_t parent_id,
                                               std::string_view name, bool is_dir,
                                               uint32_t attributes) {
  FileIndex::PopulationBatchEntry entry;
  entry.id = ntfs_file_reference::NtfsFileReference(id);
  entry.parent_id = ntfs_file_reference::NtfsFileReference(parent_id);
  entry.name = file_name::FileName(name);
  entry.is_directory = is_dir;
  entry.file_attributes = attributes;
  return entry;
}

}  // namespace

TEST_SUITE("FileEntryAttributes") {
  TEST_CASE("IsOffline tests the OFFLINE bit exactly") {
    FileEntry local{};
    local.fileAttributes = kArchive;
    CHECK_FALSE(local.IsOffline());

    FileEntry offline{};
    offline.fileAttributes = kFileAttributeOffline;
    CHECK(offline.IsOffline());

    FileEntry combined{};
    combined.fileAttributes = kFileAttributeOffline | kArchive;
    CHECK(combined.IsOffline());

    // Zero mask (e.g. attributes unreadable at enumerate time) is local.
    // The retired failure-heuristic classified such files as cloud.
    FileEntry unknown{};
    CHECK_FALSE(unknown.IsOffline());
  }

  TEST_CASE("Insert carries the attribute bitmask") {
    FileIndex index;
    index.Insert(ntfs_file_reference::NtfsFileReference(kDirId),
                 ntfs_file_reference::NtfsFileReference(kVolumeRootFrn), "TestFolder",
                 true, kFileTimeNotLoaded, kFileSizeNotLoaded, FileIndex::InsertOptions{},
                 kFileAttributeOffline | kArchive);
    const FileEntry* const entry = index.GetEntry(kDirId);
    REQUIRE(entry != nullptr);
    CHECK(entry->fileAttributes == (kFileAttributeOffline | kArchive));
    CHECK(entry->IsOffline());
  }

  TEST_CASE("InsertBatch carries per-entry bitmasks") {
    FileIndex index;
    std::vector<FileIndex::PopulationBatchEntry> batch;
    batch.push_back(MakeBatchEntry(kDirId, kVolumeRootFrn, "TestFolder", true, kArchive));
    batch.push_back(
        MakeBatchEntry(kFileId, kDirId, "data.txt", false, kFileAttributeOffline));
    index.InsertBatch(batch);

    const FileEntry* const dir = index.GetEntry(kDirId);
    const FileEntry* const file = index.GetEntry(kFileId);
    REQUIRE(dir != nullptr);
    REQUIRE(file != nullptr);
    CHECK_FALSE(dir->IsOffline());
    CHECK(file->IsOffline());
  }

  TEST_CASE("UpdateFileAttributesLocked refreshes the mask") {
    FileIndex index;
    index.Insert(ntfs_file_reference::NtfsFileReference(kFileId),
                 ntfs_file_reference::NtfsFileReference(kDirId), "data.txt", false);
    REQUIRE(index.GetEntry(kFileId) != nullptr);
    CHECK_FALSE(index.GetEntry(kFileId)->IsOffline());

    // Offline transition (e.g. file paged out to cloud) without re-indexing.
    index.UpdateFileAttributesLocked(kFileId, kFileAttributeOffline);
    CHECK(index.GetEntry(kFileId)->IsOffline());

    // And back to local.
    index.UpdateFileAttributesLocked(kFileId, kArchive);
    CHECK_FALSE(index.GetEntry(kFileId)->IsOffline());

    // Absent entry is a null-safe no-op (e.g. DELETE raced the refresh).
    index.UpdateFileAttributesLocked(0x0001000000000999ULL, kFileAttributeOffline);
  }
}
