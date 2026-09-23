#include <array>
#include <chrono>
#include <doctest/doctest.h>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "index/FileIndex.h"
#include "index/FileIndexStorage.h"
#include "index/FileName.h"
#include "index/IndexDomainEvents.h"
#include "index/NtfsFileReference.h"
#include "index/RemoveIndexedSubtree.h"
#include "path/PathUtils.h"
#include "utils/FileTimeTypes.h"

namespace {

// Expected directory path after RecomputeAllPaths. PathBuilder uses platform
// separator and (on Windows) default volume root, so we must match that.
std::string ExpectedTemporaryDirPath() {
  return std::string(path_utils::GetDefaultVolumeRootPath())
      .append("root").append(path_utils::kPathSeparatorStr)
      .append("Testing").append(path_utils::kPathSeparatorStr)
      .append("Temporary");
}

// Differential oracle for the materialized SoA paths: rebuild an entry's path
// by walking stored parent links with test-side names (captured at insert /
// rename time), independent of the PathStorage strings. Returns nullopt when
// the chain breaks (missing parent - the awaiting case) or loops.
// Kept test-local on purpose: promoting it to production resolve-on-read is
// Phase 1 of the normalized-store migration, not this step.
std::optional<std::string> WalkIndexedPath(
    const FileIndex& index, uint64_t id,
    const std::unordered_map<uint64_t, std::string>& names) {
  const std::shared_lock lock(index.GetMutex());
  std::vector<std::string> parts;
  uint64_t current = id;
  static constexpr int kMaxParentWalk = 128;
  for (int depth = 0; depth < kMaxParentWalk; ++depth) {
    const FileEntry* const entry = index.GetEntry(current);
    if (entry == nullptr) {
      return std::nullopt;
    }
    const auto name_it = names.find(current);
    if (name_it == names.end()) {
      return std::nullopt;
    }
    parts.push_back(name_it->second);
    const uint64_t parent = entry->parentID.raw;
    if (parent == 0 || ntfs_file_reference::IsRootDirectoryRecord(parent) ||
        ntfs_file_reference::RecordNumber(parent) ==
            ntfs_file_reference::RecordNumber(current)) {
      // Root rule mirrors production (zero / volume-root / self parent).
      std::string path = path_utils::GetDefaultVolumeRootPath();
      for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        path = path_utils::JoinPath(path, *it);
      }
      return path;
    }
    const auto [parent_entry, resolved] = index.ResolveEntryReference(parent);
    if (parent_entry == nullptr) {
      return std::nullopt;
    }
    current = resolved;
  }
  return std::nullopt;  // Cycle guard: parent loop, not a path.
}

// Shared bulk pair for the InsertBatch cases: child staged before its parent
// (out-of-order arrival). The cases differ only in defer_path_indexing
// (Sonar: duplicate 21-line setup blocks).
constexpr uint64_t kBulkParent = 0x00010000000001A0ULL;
constexpr uint64_t kBulkChild = 0x00010000000001B0ULL;

std::vector<FileIndex::PopulationBatchEntry> MakeBulkChildBeforeParentBatch() {
  FileIndex::PopulationBatchEntry child{};
  child.id = ntfs_file_reference::NtfsFileReference(kBulkChild);
  child.parent_id = ntfs_file_reference::NtfsFileReference(kBulkParent);
  child.name = file_name::FileName("bulk.txt");
  child.is_directory = false;
  FileIndex::PopulationBatchEntry parent{};
  parent.id = ntfs_file_reference::NtfsFileReference(kBulkParent);
  parent.parent_id = ntfs_file_reference::NtfsFileReference(0);
  parent.name = file_name::FileName("BulkDir");
  parent.is_directory = true;
  return {child, parent};
}

// Shared tracked-insert harness for the WalkIndexedPath oracle cases
// (Sonar: duplicate 15-line setup blocks).
struct TrackedIndex {
  FileIndex index;
  std::unordered_map<uint64_t, std::string> names;
  std::vector<uint64_t> ids;
  void TrackInsert(uint64_t id, uint64_t parent, std::string_view name, bool is_dir) {
    index.Insert(ntfs_file_reference::NtfsFileReference(id),
                 ntfs_file_reference::NtfsFileReference(parent), name, is_dir);
    names[id] = std::string(name);
    ids.push_back(id);
  }
};

}  // namespace

TEST_CASE("SameRecordNumber ignores FRN sequence for rename-vs-move") {
  constexpr uint64_t kParentA = 0x0002000000000064ULL;
  constexpr uint64_t kParentAStaleSeq = 0x0001000000000064ULL;
  constexpr uint64_t kOtherParent = 0x0001000000000065ULL;

  CHECK(ntfs_file_reference::SameRecordNumber(kParentA, kParentAStaleSeq));
  CHECK_FALSE(ntfs_file_reference::SameRecordNumber(kParentA, kOtherParent));
  CHECK(ntfs_file_reference::SameRecordNumber(0, 0));
}

TEST_CASE("InsertLocked synthetic ids do not shadow MFT record_number_to_id_") {
  std::shared_mutex mutex;
  FileIndexStorage storage(mutex);

  // Real USN-style FRN for MFT record 5 (volume root), with a non-zero sequence.
  constexpr uint64_t kVolumeRootFrn = 0x0001000000000005ULL;
  storage.InsertLocked(ntfs_file_reference::NtfsFileReference(kVolumeRootFrn),
                       ntfs_file_reference::NtfsFileReference(0), "real-root", true,
                       kFileTimeNotLoaded,
                       /*register_mft_record=*/true);

  // Synthetic next_file_id_-style id whose RecordNumber equals 5 — must not replace
  // the FRN mapping used by ResolveEntryReference.
  storage.InsertLocked(ntfs_file_reference::NtfsFileReference(5),
                       ntfs_file_reference::NtfsFileReference(0), "synthetic", true,
                       kFileTimeNotLoaded,
                       /*register_mft_record=*/false);

  const auto [entry, resolved] = storage.ResolveEntryReference(kVolumeRootFrn);
  REQUIRE(entry != nullptr);
  CHECK(resolved == kVolumeRootFrn);
  CHECK(storage.GetNameCache().Find(resolved) == "real-root");

  // Parent refs often carry a stale sequence; resolve by record number still hits FRN.
  constexpr uint64_t kStaleSequenceFrn = 0x0002000000000005ULL;
  const auto [stale_entry, stale_resolved] =
      storage.ResolveEntryReference(kStaleSequenceFrn);
  REQUIRE(stale_entry != nullptr);
  CHECK(stale_resolved == kVolumeRootFrn);
}

TEST_CASE("InsertPath synthetics do not steal ResolveEntryReference from USN FRN") {
  FileIndex index;

  constexpr uint64_t kFolderFrn = 0x0001000000000064ULL;  // MFT record 100
  constexpr uint64_t kChildFrn = 0x0001000000000200ULL;
  index.Insert(ntfs_file_reference::NtfsFileReference(kFolderFrn), ntfs_file_reference::NtfsFileReference(0), "Users", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(kChildFrn), ntfs_file_reference::NtfsFileReference(kFolderFrn), "doc.txt", false);

  // Force a synthetic id equal to record 100 without registering it.
  index.Insert(ntfs_file_reference::NtfsFileReference(100), ntfs_file_reference::NtfsFileReference(0), "poison", true, kFileTimeNotLoaded, kFileSizeNotLoaded,
               FileIndex::InsertOptions{/*register_mft_record=*/false});

  index.RecomputeAllPaths();

  std::string child_path;
  index.ForEachEntryWithPath(
      [&child_path](uint64_t id, const FileEntry& /*entry*/, std::string_view path) {
        if (const uint64_t kChild = 0x0001000000000200ULL; id == kChild) {
          child_path.assign(path);
        }
        return true;
      });

  REQUIRE_FALSE(child_path.empty());
  CHECK(child_path.find("Users") != std::string::npos);
  CHECK(child_path.find("poison") == std::string::npos);
}

TEST_CASE("RemoveIndexedSubtree does not evacuate volume-root children") {
  // Soft-delete cascade matches by resolved parent-id membership. Drive-root
  // files parent to MFT record 5. Cascading must NOT treat volume-root parents as
  // deleted even when a synthetic DirectoryResolver id collides with record 5.
  FileIndex index;

  constexpr uint64_t kVolumeRootFrn = ntfs_file_reference::kRootDirectoryRecordNumber;
  constexpr uint64_t kDriveRootFileId = 0x0001000000000100ULL;
  constexpr uint64_t kFolderId = 0x0002000000000200ULL;
  constexpr uint64_t kNestedFileId = 0x0003000000000300ULL;
  // Synthetic id whose record number equals the volume root (collision scenario).
  constexpr uint64_t kSyntheticVolumeRootId = ntfs_file_reference::kRootDirectoryRecordNumber;

  index.Insert(ntfs_file_reference::NtfsFileReference(kDriveRootFileId), ntfs_file_reference::NtfsFileReference(kVolumeRootFrn), "file.txt", false, kFileTimeNotLoaded);
  index.Insert(ntfs_file_reference::NtfsFileReference(kFolderId), ntfs_file_reference::NtfsFileReference(kVolumeRootFrn), "SomeFolder", true, kFileTimeNotLoaded);
  index.Insert(ntfs_file_reference::NtfsFileReference(kNestedFileId), ntfs_file_reference::NtfsFileReference(kFolderId), "nested.txt", false, kFileTimeNotLoaded);
  // Pretend a synthetic "C:" directory occupies record number 5.
  index.Insert(ntfs_file_reference::NtfsFileReference(kSyntheticVolumeRootId), ntfs_file_reference::NtfsFileReference(0), "C:", true, kFileTimeNotLoaded,
               kFileSizeNotLoaded,
               FileIndex::InsertOptions{/*register_mft_record=*/false});

  REQUIRE(index.GetEntry(kDriveRootFileId) != nullptr);
  REQUIRE(index.GetEntry(kNestedFileId) != nullptr);
  REQUIRE(index.GetEntry(kFolderId) != nullptr);

  // Soft-delete the nested folder only — must remove folder + nested file, keep
  // the volume-root child even though record 5 is present in the index.
  RemoveIndexedSubtree(index, ntfs_file_reference::NtfsFileReference(kFolderId));

  CHECK(index.GetEntry(kFolderId) == nullptr);
  CHECK(index.GetEntry(kNestedFileId) == nullptr);
  CHECK(index.GetEntry(kDriveRootFileId) != nullptr);
  CHECK(index.GetEntry(kSyntheticVolumeRootId) != nullptr);
}

TEST_CASE("RemoveIndexedSubtree keeps drive-root path when parentID wrongly points at victim") {
  // Failure mode: path is still C:\file1.txt (or platform volume-root child) but
  // parentID was corrupted to a folder being soft-deleted. Cascade must not
  // evacuate the drive-root entry — only an explicit Remove of its own id.
  FileIndex index;

  constexpr uint64_t kVolumeRootFrn = 0x0001000000000005ULL;
  constexpr uint64_t kVictimFolder = 0x0001000000000A00ULL;
  constexpr uint64_t kVictimChild = 0x0001000000000A01ULL;
  constexpr uint64_t kDriveRootFile = 0x0001000000000B00ULL;

  index.Insert(ntfs_file_reference::NtfsFileReference(kVictimFolder), ntfs_file_reference::NtfsFileReference(kVolumeRootFrn), "bulk", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(kVictimChild), ntfs_file_reference::NtfsFileReference(kVictimFolder), "temp.txt", false);
  // Insert under volume root so PathStorage gets a drive-root child path, then
  // corrupt parentID to the victim folder (simulates linkage drift).
  index.Insert(ntfs_file_reference::NtfsFileReference(kDriveRootFile), ntfs_file_reference::NtfsFileReference(kVolumeRootFrn), "file1.txt", false);
  {
    const std::unique_lock lock(index.GetMutex());
    FileEntry* const entry = index.GetEntryMutable(kDriveRootFile);
    REQUIRE(entry != nullptr);
    entry->parentID = ntfs_file_reference::NtfsFileReference(kVictimFolder);
  }

  const std::string drive_path = index.GetPathAccessor().GetPathCopy(kDriveRootFile);
  REQUIRE(path_utils::IsVolumeRootChildPath(drive_path));

  RemoveIndexedSubtree(index, ntfs_file_reference::NtfsFileReference(kVictimFolder));

  CHECK(index.GetEntry(kVictimFolder) == nullptr);
  CHECK(index.GetEntry(kVictimChild) == nullptr);
  REQUIRE(index.GetEntry(kDriveRootFile) != nullptr);
  CHECK(index.GetPathAccessor().GetPathCopy(kDriveRootFile) == drive_path);
}

TEST_CASE("Bulk soft-delete then Maintain keeps drive-root files searchable") {
  // Repro shape: many Recycle Bin soft-deletes trip path-buffer rebuild (>1000
  // tombstones). Drive-root files must remain in PathStorage after Maintain.
  FileIndex index;

  constexpr uint64_t kVolumeRootFrn = 0x0001000000000005ULL;
  constexpr uint64_t kDriveRootFile = 0x0001000000000100ULL;
  constexpr uint64_t kBulkFolder = 0x0001000000000200ULL;
  constexpr size_t kBulkFileCount = 1200;

  index.Insert(ntfs_file_reference::NtfsFileReference(kDriveRootFile), ntfs_file_reference::NtfsFileReference(kVolumeRootFrn), "root.txt", false);
  index.Insert(ntfs_file_reference::NtfsFileReference(kBulkFolder), ntfs_file_reference::NtfsFileReference(kVolumeRootFrn), "bulk", true);
  for (size_t i = 0; i < kBulkFileCount; ++i) {
    const uint64_t file_id = 0x0002000000001000ULL + i;
    index.Insert(ntfs_file_reference::NtfsFileReference(file_id), ntfs_file_reference::NtfsFileReference(kBulkFolder), "f", false);
  }

  const std::string root_path_before =
      index.GetPathAccessor().GetPathCopy(kDriveRootFile);
  REQUIRE(path_utils::IsVolumeRootChildPath(root_path_before));

  for (size_t i = 0; i < kBulkFileCount; ++i) {
    RemoveIndexedSubtree(index, ntfs_file_reference::NtfsFileReference(0x0002000000001000ULL + i));
  }
  RemoveIndexedSubtree(index, ntfs_file_reference::NtfsFileReference(kBulkFolder));

  // Force path-buffer rebuild (same threshold path as ApplicationLogic::Maintain).
  REQUIRE(index.Maintain());

  REQUIRE(index.GetEntry(kDriveRootFile) != nullptr);
  CHECK(index.GetPathAccessor().GetPathCopy(kDriveRootFile) == root_path_before);
  CHECK(index.GetEntry(kBulkFolder) == nullptr);
}

TEST_CASE("RemoveIndexedSubtree with record-5 root only removes that entry") {
  FileIndex index;

  constexpr uint64_t kVolumeRootFrn = ntfs_file_reference::kRootDirectoryRecordNumber;
  constexpr uint64_t kDriveRootFileId = 0x0001000000000100ULL;
  constexpr uint64_t kSyntheticVolumeRootId = ntfs_file_reference::kRootDirectoryRecordNumber;

  index.Insert(ntfs_file_reference::NtfsFileReference(kDriveRootFileId), ntfs_file_reference::NtfsFileReference(kVolumeRootFrn), "file.txt", false, kFileTimeNotLoaded);
  index.Insert(ntfs_file_reference::NtfsFileReference(kSyntheticVolumeRootId), ntfs_file_reference::NtfsFileReference(0), "C:", true, kFileTimeNotLoaded,
               kFileSizeNotLoaded,
               FileIndex::InsertOptions{/*register_mft_record=*/false});

  RemoveIndexedSubtree(index, ntfs_file_reference::NtfsFileReference(kSyntheticVolumeRootId));

  CHECK(index.GetEntry(kSyntheticVolumeRootId) == nullptr);
  // Cascading through record 5 must not remove drive-root children.
  CHECK(index.GetEntry(kDriveRootFileId) != nullptr);
}

TEST_CASE("RemoveIndexedSubtree synthetic id does not wipe USN folder with same record") {
  // Soft-delete of synthetic id N must not remove children of a real USN folder
  // whose FRN shares MFT record number N (cascade is by resolved parent id, not
  // bare record number).
  FileIndex index;

  constexpr uint64_t kFolderFrn = 0x0001000000000064ULL;  // MFT record 100
  constexpr uint64_t kChildFrn = 0x0001000000000200ULL;
  constexpr uint64_t kSyntheticId = 100;  // RecordNumber == 100

  index.Insert(ntfs_file_reference::NtfsFileReference(kFolderFrn), ntfs_file_reference::NtfsFileReference(0), "Users", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(kChildFrn), ntfs_file_reference::NtfsFileReference(kFolderFrn), "doc.txt", false);
  index.Insert(ntfs_file_reference::NtfsFileReference(kSyntheticId), ntfs_file_reference::NtfsFileReference(0), "poison", true, kFileTimeNotLoaded, kFileSizeNotLoaded,
               FileIndex::InsertOptions{/*register_mft_record=*/false});

  RemoveIndexedSubtree(index, ntfs_file_reference::NtfsFileReference(kSyntheticId));

  CHECK(index.GetEntry(kSyntheticId) == nullptr);
  CHECK(index.GetEntry(kFolderFrn) != nullptr);
  CHECK(index.GetEntry(kChildFrn) != nullptr);
}

TEST_CASE("RemoveIndexedSubtree missing root FRN does not resolve to live record peer") {
  // Regression: GetEntry miss must not fall back to ResolveEntryReference(root).
  // A recycled / $-prefixed FRN sharing only the 48-bit MFT record number with a
  // live indexed file would otherwise redirect and Remove() the live entry.
  FileIndex index;

  constexpr uint64_t kLiveFrn = 0x0001000000000064ULL;       // seq=1, record=100
  constexpr uint64_t kMissingPeerFrn = 0x0002000000000064ULL;  // seq=2, same record

  index.Insert(ntfs_file_reference::NtfsFileReference(kLiveFrn), ntfs_file_reference::NtfsFileReference(0), "live.txt", false);

  RemoveIndexedSubtree(index, ntfs_file_reference::NtfsFileReference(kMissingPeerFrn));

  CHECK(index.GetEntry(kLiveFrn) != nullptr);
  CHECK(index.GetEntry(kMissingPeerFrn) == nullptr);
  CHECK(index.GetPathAccessor().GetPathCopy(kLiveFrn).find("live.txt") != std::string::npos);
}

TEST_CASE("RemoveIndexedSubtree does not cascade via file record-number resolve") {
  // Stale parentID that ResolveEntryReference maps to a FILE in remove_ids must not
  // cascade-delete unrelated entries (only directories are valid parents).
  FileIndex index;

  constexpr uint64_t kVictimDir = 0x0001000000000100ULL;
  constexpr uint64_t kVictimFile = 0x0001000000000200ULL;  // MFT record 0x200
  constexpr uint64_t kUnrelated = 0x0001000000000300ULL;
  constexpr uint64_t kStaleParentToVictimFile = 0x0002000000000200ULL;  // seq=2, record=0x200

  // Insert unrelated first so parent stays as the stale ref (victim file not indexed yet).
  index.Insert(ntfs_file_reference::NtfsFileReference(kUnrelated), ntfs_file_reference::NtfsFileReference(kStaleParentToVictimFile), "unrelated.txt", false);
  index.Insert(ntfs_file_reference::NtfsFileReference(kVictimDir), ntfs_file_reference::NtfsFileReference(0), "VictimDir", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(kVictimFile), ntfs_file_reference::NtfsFileReference(kVictimDir), "victim.txt", false);

  {
    const std::shared_lock lock(index.GetMutex());
    const FileEntry* const unrelated = index.GetEntry(kUnrelated);
    REQUIRE(unrelated != nullptr);
    CHECK(unrelated->parentID.raw == kStaleParentToVictimFile);
  }

  RemoveIndexedSubtree(index, ntfs_file_reference::NtfsFileReference(kVictimDir));

  CHECK(index.GetEntry(kVictimDir) == nullptr);
  CHECK(index.GetEntry(kVictimFile) == nullptr);
  CHECK(index.GetEntry(kUnrelated) != nullptr);
}

TEST_CASE("RemoveIndexedSubtree finds children when parentID has stale sequence") {
  FileIndex index;

  constexpr uint64_t kFolderFrn = 0x0002000000000064ULL;
  constexpr uint64_t kStaleParentRef = 0x0001000000000064ULL;
  constexpr uint64_t kChildFrn = 0x0001000000000200ULL;

  // Child inserted before parent keeps the raw stale ParentFileReferenceNumber.
  index.Insert(ntfs_file_reference::NtfsFileReference(kChildFrn), ntfs_file_reference::NtfsFileReference(kStaleParentRef), "doc.txt", false);
  index.Insert(ntfs_file_reference::NtfsFileReference(kFolderFrn), ntfs_file_reference::NtfsFileReference(0), "Users", true);

  {
    const std::shared_lock lock(index.GetMutex());
    const FileEntry* const child = index.GetEntry(kChildFrn);
    REQUIRE(child != nullptr);
    CHECK(child->parentID.raw == kStaleParentRef);
  }

  RemoveIndexedSubtree(index, ntfs_file_reference::NtfsFileReference(kFolderFrn));

  CHECK(index.GetEntry(kFolderFrn) == nullptr);
  CHECK(index.GetEntry(kChildFrn) == nullptr);
}

TEST_CASE("Insert resolves stale parent FRN to canonical id and path") {
  FileIndex index;

  constexpr uint64_t kFolderFrn = 0x0002000000000064ULL;
  constexpr uint64_t kStaleParentRef = 0x0001000000000064ULL;
  constexpr uint64_t kChildFrn = 0x0001000000000200ULL;

  index.Insert(ntfs_file_reference::NtfsFileReference(kFolderFrn), ntfs_file_reference::NtfsFileReference(0), "Users", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(kChildFrn), ntfs_file_reference::NtfsFileReference(kStaleParentRef), "doc.txt", false);

  {
    const std::shared_lock lock(index.GetMutex());
    const FileEntry* const child = index.GetEntry(kChildFrn);
    REQUIRE(child != nullptr);
    CHECK(child->parentID.raw == kFolderFrn);
  }

  const std::string child_path = index.GetPathAccessor().GetPathCopy(kChildFrn);
  CHECK(child_path.find("Users") != std::string::npos);
  CHECK(child_path.find("doc.txt") != std::string::npos);
  // Must not be a bare-filename placeholder from unresolved parent.
  CHECK(child_path.find(path_utils::kPathSeparator) != std::string::npos);
}

TEST_CASE("Move resolves stale parent FRN to canonical id and path") {
  FileIndex index;

  constexpr uint64_t kOldParent = 0x0001000000000010ULL;
  constexpr uint64_t kNewParent = 0x0002000000000020ULL;
  constexpr uint64_t kStaleNewParent = 0x0001000000000020ULL;
  constexpr uint64_t kFileId = 0x0001000000000300ULL;

  index.Insert(ntfs_file_reference::NtfsFileReference(kOldParent), ntfs_file_reference::NtfsFileReference(0), "OldDir", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(kNewParent), ntfs_file_reference::NtfsFileReference(0), "NewDir", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(kFileId), ntfs_file_reference::NtfsFileReference(kOldParent), "doc.txt", false);

  REQUIRE(index.Move(ntfs_file_reference::NtfsFileReference(kFileId), ntfs_file_reference::NtfsFileReference(kStaleNewParent)));

  {
    const std::shared_lock lock(index.GetMutex());
    const FileEntry* const file = index.GetEntry(kFileId);
    REQUIRE(file != nullptr);
    CHECK(file->parentID.raw == kNewParent);
  }

  const std::string path = index.GetPathAccessor().GetPathCopy(kFileId);
  CHECK(path.find("NewDir") != std::string::npos);
  CHECK(path.find("doc.txt") != std::string::npos);
}

TEST_CASE("RemoveIndexedSubtree scan stays sub-linear-cost on large unrelated index") {
  // Soft-delete does a full-index fixed-point scan. With the GetEntry fast path,
  // non-children must not pay ResolveEntryReference. Bound total wall time so a
  // regression (Resolve on every entry) fails loudly on CI-class machines.
  FileIndex index;

  constexpr uint64_t kVictimFolder = 0x0001000000000F00ULL;
  constexpr uint64_t kVictimChild = 0x0001000000000F01ULL;
  constexpr size_t kUnrelatedCount = 50000;

  index.Insert(ntfs_file_reference::NtfsFileReference(kVictimFolder), ntfs_file_reference::NtfsFileReference(0), "victim", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(kVictimChild), ntfs_file_reference::NtfsFileReference(kVictimFolder), "child.txt", false);

  for (size_t i = 0; i < kUnrelatedCount; ++i) {
    const uint64_t folder_id = 0x0002000000001000ULL + i;
    const uint64_t file_id = 0x0003000000001000ULL + i;
    index.Insert(ntfs_file_reference::NtfsFileReference(folder_id), ntfs_file_reference::NtfsFileReference(0), "d", true);
    index.Insert(ntfs_file_reference::NtfsFileReference(file_id), ntfs_file_reference::NtfsFileReference(folder_id), "f", false);
  }

  const auto start = std::chrono::steady_clock::now();
  RemoveIndexedSubtree(index, ntfs_file_reference::NtfsFileReference(kVictimFolder));
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count();

  CHECK(index.GetEntry(kVictimFolder) == nullptr);
  CHECK(index.GetEntry(kVictimChild) == nullptr);
  CHECK(index.Size() == kUnrelatedCount * 2);
  // Generous bound: Debug+no-asan on Apple Silicon typically <50ms; allow headroom.
  CHECK(elapsed_ms < 2000);
  MESSAGE("RemoveIndexedSubtree over ", kUnrelatedCount * 2, " unrelated entries: ",
          elapsed_ms, " ms");
}

TEST_CASE("InsertPath drive-root Windows path inserts once (no duplicate inflation)") {
  FileIndex index;

  // InsertPathUnderLock splits C:\file.txt → parent "C:" (not GetDirectoryPathView's "C:\\").
  // Single insert must yield exactly one file + one drive-letter directory.
  index.InsertPath(R"(C:\file.txt)", false);

  auto count_entries = [&index] {
    size_t total = 0;
    size_t files = 0;
    size_t named_file_txt = 0;
    index.ForEachEntryWithPath(
        [&total, &files, &named_file_txt](uint64_t /*id*/, const FileEntry& entry,
                                          std::string_view path) {
          ++total;
          if (!entry.isDirectory) {
            ++files;
            if (path_utils::GetFilename(path) == "file.txt") {
              ++named_file_txt;
            }
          }
          return true;
        });
    return std::array<size_t, 3>{total, files, named_file_txt};
  };

  {
    const auto [total, files, named_file_txt] = count_entries();
    CHECK(total == 2);
    CHECK(files == 1);
    CHECK(named_file_txt == 1);
  }

  index.RecomputeAllPaths();
  {
    const auto [total, files, named_file_txt] = count_entries();
    CHECK(total == 2);
    CHECK(files == 1);
    CHECK(named_file_txt == 1);
  }

#ifdef _WIN32
  // On Windows stored path matches input, so path_to_id dedup must reject a second insert.
  index.InsertPath(R"(C:\file.txt)", false);
  {
    const auto [total, files, named_file_txt] = count_entries();
    CHECK(total == 2);
    CHECK(files == 1);
    CHECK(named_file_txt == 1);
  }
#endif  // _WIN32
}

TEST_CASE("InsertPath is idempotent for existing directory created via DirectoryResolver") {
  FileIndex index;

  // First insert a file path; this will create parent directories via DirectoryResolver.
  index.InsertPath("/root/Testing/Temporary/file1.txt", false);

  // Now insert the directory path itself; this should NOT create a second directory entry.
  index.InsertPath("/root/Testing/Temporary", true);

  // Recompute paths to ensure path storage is populated for search.
  index.RecomputeAllPaths();

  const std::string expected_dir = ExpectedTemporaryDirPath();

  // Collect all entries with full path expected_dir and verify we have exactly one.
  size_t count = 0;
  const auto accessor = index.GetPathAccessor();
  (void)accessor;
  index.ForEachEntryWithPath(
    [&count, &expected_dir](uint64_t /*id*/, const FileEntry& entry, std::string_view path) {
      if (entry.isDirectory && path == expected_dir) {
        ++count;
      }
      return true;
    });

  CHECK(count == 1);  // NOLINT(cert-err33-c) - doctest CHECK macro handles assertion; return value is not meant to be consumed
}

TEST_CASE("InsertPath is idempotent when directory is inserted before files") {
  FileIndex index;

  // First insert the directory path; this creates the FileEntry and caches ancestors.
  index.InsertPath("/root/Testing/Temporary", true);

  // Now insert a file under that directory; this must reuse the existing directory ID
  // instead of creating a second directory entry for the same full path.
  index.InsertPath("/root/Testing/Temporary/file1.txt", false);

  // Recompute paths to ensure path storage is populated for search.
  index.RecomputeAllPaths();

  const std::string expected_dir = ExpectedTemporaryDirPath();

  // Collect all entries with full path expected_dir and verify we have exactly one.
  size_t count = 0;
  const auto accessor = index.GetPathAccessor();
  (void)accessor;
  index.ForEachEntryWithPath(
    [&count, &expected_dir](uint64_t /*id*/, const FileEntry& entry, std::string_view path) {
      if (entry.isDirectory && path == expected_dir) {
        ++count;
      }
      return true;
    });

  CHECK(count == 1);  // NOLINT(cert-err33-c) - doctest CHECK macro handles assertion; return value is not meant to be consumed
}

TEST_CASE("RecomputeAllPaths resolves FRN sequence mismatch in parent chain") {
  FileIndex index;

  index.Insert(ntfs_file_reference::NtfsFileReference(1), ntfs_file_reference::NtfsFileReference(1), "refs", true);
  const uint64_t remotes_id = 0x0002000000000050ULL;
  index.Insert(ntfs_file_reference::NtfsFileReference(remotes_id), ntfs_file_reference::NtfsFileReference(1), "remotes", true);

  const uint64_t stale_remotes_ref = 0x0001000000000050ULL;
  const uint64_t origin_id = 0x0003000000000060ULL;
  index.Insert(ntfs_file_reference::NtfsFileReference(origin_id), ntfs_file_reference::NtfsFileReference(stale_remotes_ref), "origin", true);

  const uint64_t head_id = 0x0004000000000070ULL;
  index.Insert(ntfs_file_reference::NtfsFileReference(head_id), ntfs_file_reference::NtfsFileReference(origin_id), "HEAD", false);

  index.RecomputeAllPaths();

  std::string head_path;
  index.ForEachEntryWithPath([&head_path](uint64_t id, const FileEntry& /*entry*/,
                                                    std::string_view path) {
    if (const uint64_t kHeadId = 0x0004000000000070ULL; id == kHeadId) {
      head_path.assign(path);
    }
    return true;
  });

  const std::string expected_suffix = std::string("refs")
      .append(path_utils::kPathSeparatorStr).append("remotes")
      .append(path_utils::kPathSeparatorStr).append("origin")
      .append(path_utils::kPathSeparatorStr).append("HEAD");
  CHECK(head_path.size() >= expected_suffix.size());
  CHECK(head_path.compare(head_path.size() - expected_suffix.size(), expected_suffix.size(),
                          expected_suffix) == 0);
}

TEST_CASE("RecomputeAllPaths prunes orphan subtree with phantom volume-root paths") {
  FileIndex index;

  const uint64_t missing_parent = 0x0001000000000099ULL;
  const uint64_t origin_id = 0x0003000000000060ULL;
  index.Insert(ntfs_file_reference::NtfsFileReference(origin_id), ntfs_file_reference::NtfsFileReference(missing_parent), "origin", true);

  const uint64_t head_id = 0x0004000000000070ULL;
  index.Insert(ntfs_file_reference::NtfsFileReference(head_id), ntfs_file_reference::NtfsFileReference(origin_id), "HEAD", false);

  index.RecomputeAllPaths();

  CHECK(index.Size() == 0);
}

TEST_CASE("RecomputeAllPaths keeps volume-root children when NTFS root record is absent") {
  FileIndex index;

  // MFT record 5 (volume root) is usually not present in the index after USN enum.
  const uint64_t root_frn = 0x0001000000000005ULL;
  const uint64_t windows_id = 0x0002000000000100ULL;
  index.Insert(ntfs_file_reference::NtfsFileReference(windows_id), ntfs_file_reference::NtfsFileReference(root_frn), "Windows", true);

  const uint64_t system32_id = 0x0003000000000200ULL;
  index.Insert(ntfs_file_reference::NtfsFileReference(system32_id), ntfs_file_reference::NtfsFileReference(windows_id), "System32", true);

  index.RecomputeAllPaths();

  CHECK(index.Size() == 2);

  std::string windows_path;
  std::string system32_path;
  index.ForEachEntryWithPath([&windows_path, &system32_path](
                                 uint64_t id, const FileEntry& /*entry*/, std::string_view path) {
    constexpr uint64_t kWindowsId = 0x0002000000000100ULL;
    constexpr uint64_t kSystem32Id = 0x0003000000000200ULL;
    if (id == kWindowsId) {
      windows_path.assign(path);
    } else if (id == kSystem32Id) {
      system32_path.assign(path);
    }
    return true;
  });

  CHECK(windows_path == std::string(path_utils::GetDefaultVolumeRootPath()).append("Windows"));
  CHECK(system32_path == std::string(path_utils::GetDefaultVolumeRootPath())
                              .append("Windows")
                              .append(path_utils::kPathSeparatorStr)
                              .append("System32"));
}

TEST_CASE("RecomputeAllPaths keeps drive-root files when poisoned volume-root entry is pruned") {
  // Regression: ResolveEntryReference can bind parent record 5 to an indexed "." /
  // synthetic entry. If that entry is an orphan, prune must NOT evacuate C:\* children
  // that still parent to MFT record 5.
  FileIndex index;

  constexpr uint64_t kMissingParent = 0x0001000000000099ULL;
  constexpr uint64_t kPoisonedVolumeRoot = 0x0001000000000005ULL;
  constexpr uint64_t kDriveRootFile = 0x0001000000000100ULL;
  constexpr uint64_t kStaleVolumeParent = 0x0002000000000005ULL;

  index.Insert(ntfs_file_reference::NtfsFileReference(kPoisonedVolumeRoot), ntfs_file_reference::NtfsFileReference(kMissingParent), ".", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(kDriveRootFile), ntfs_file_reference::NtfsFileReference(kStaleVolumeParent), "file1.txt", false);

  {
    const std::shared_lock lock(index.GetMutex());
    const FileEntry* const file = index.GetEntry(kDriveRootFile);
    REQUIRE(file != nullptr);
    // Insert must not rewrite volume-root parents to the poisoned canonical id.
    CHECK(file->parentID.raw == kStaleVolumeParent);
  }

  index.RecomputeAllPaths();

  CHECK(index.GetEntry(kPoisonedVolumeRoot) == nullptr);
  REQUIRE(index.GetEntry(kDriveRootFile) != nullptr);

  const std::string path = index.GetPathAccessor().GetPathCopy(kDriveRootFile);
  CHECK(path == std::string(path_utils::GetDefaultVolumeRootPath()).append("file1.txt"));
}

TEST_CASE("Insert builds drive-root path without RecomputeAllPaths") {
  // Live USN CREATE after initial population never re-runs RecomputeAllPaths.
  // Parent MFT record 5 has no PathStorage entry, so Insert must join the
  // configured volume root immediately — not store a bare filename placeholder.
  FileIndex index;

  constexpr uint64_t kRootFrn = 0x0001000000000005ULL;
  constexpr uint64_t kFileId = 0x0001000000000100ULL;
  index.Insert(ntfs_file_reference::NtfsFileReference(kFileId), ntfs_file_reference::NtfsFileReference(kRootFrn), "file1.txt", false);

  REQUIRE(index.GetEntry(kFileId) != nullptr);
  CHECK(index.GetPathAccessor().GetPathCopy(kFileId) ==
        std::string(path_utils::GetDefaultVolumeRootPath()).append("file1.txt"));
}

TEST_CASE("Move onto volume-root parent builds drive-root path") {
  FileIndex index;

  constexpr uint64_t kOldParent = 0x0001000000000010ULL;
  constexpr uint64_t kRootFrn = 0x0001000000000005ULL;
  constexpr uint64_t kFileId = 0x0001000000000300ULL;

  index.Insert(ntfs_file_reference::NtfsFileReference(kOldParent), ntfs_file_reference::NtfsFileReference(0), "OldDir", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(kFileId), ntfs_file_reference::NtfsFileReference(kOldParent), "file1.txt", false);
  REQUIRE(index.Move(ntfs_file_reference::NtfsFileReference(kFileId), ntfs_file_reference::NtfsFileReference(kRootFrn)));

  CHECK(index.GetPathAccessor().GetPathCopy(kFileId) ==
        std::string(path_utils::GetDefaultVolumeRootPath()).append("file1.txt"));
}

TEST_CASE("InsertPath deduplicates against healed full path from USN out-of-order parent arrival") {
  FileIndex index;

  constexpr uint64_t kParent = 0x00010000000000A0ULL;
  constexpr uint64_t kChild = 0x00010000000000B0ULL;
  constexpr std::string_view kFileName = "dedup_test_file.txt";

  // Out-of-order child insert (parent missing): path is bare placeholder ("dedup_test_file.txt").
  index.Insert(ntfs_file_reference::NtfsFileReference(kChild), ntfs_file_reference::NtfsFileReference(kParent), kFileName, false);
  const std::string bare = index.GetPathAccessor().GetPathCopy(kChild);
  REQUIRE(bare == kFileName);

  // Parent arrives: child path is healed to full path.
  index.Insert(ntfs_file_reference::NtfsFileReference(kParent), ntfs_file_reference::NtfsFileReference(0), "HealedFolder", true);
  const std::string healed_full_path = index.GetPathAccessor().GetPathCopy(kChild);
  REQUIRE(healed_full_path != kFileName);
  REQUIRE(healed_full_path.find("HealedFolder") != std::string::npos);

  const size_t size_after_heal = index.Size();

  // Subsequent InsertPath with the healed full path must deduplicate (not create a duplicate entry).
  index.InsertPath(healed_full_path, false);

  CHECK(index.Size() == size_after_heal);

  size_t matching_file_count = 0;
  index.ForEachEntryWithPath([&matching_file_count, &healed_full_path](
                                  uint64_t /*id*/, const FileEntry& entry, std::string_view path) {
    if (!entry.isDirectory && path == healed_full_path) {
      ++matching_file_count;
    }
    return true;
  });

  CHECK(matching_file_count == 1);
}

TEST_CASE("Healing awaiting bare placeholder correctly updates path_to_id_ map without unlinking bare path") {
  FileIndex index;

  constexpr uint64_t kParent = 0x00010000000000A0ULL;
  constexpr uint64_t kChild = 0x00010000000000B0ULL;
  constexpr std::string_view kFileName = "bare_placeholder.txt";

  // 1. Insert child before parent. Path is "bare_placeholder.txt".
  index.Insert(ntfs_file_reference::NtfsFileReference(kChild), ntfs_file_reference::NtfsFileReference(kParent), kFileName, false);
  CHECK(index.GetPathAccessor().GetPathCopy(kChild) == kFileName);

  // 2. Insert parent. Child heals to full path e.g. "ParentFolder/bare_placeholder.txt"
  index.Insert(ntfs_file_reference::NtfsFileReference(kParent), ntfs_file_reference::NtfsFileReference(0), "ParentFolder", true);
  const std::string healed_path = index.GetPathAccessor().GetPathCopy(kChild);
  CHECK(healed_path.find("ParentFolder") != std::string::npos);

  // 3. Verify InsertPath for healed_path deduplicates and reuses kChild
  const size_t size_before = index.Size();
  index.InsertPath(healed_path, false);
  CHECK(index.Size() == size_before);

  // 4. Verify path_to_id_ map correctly resolved the healed_path
  size_t count = 0;
  index.ForEachEntryWithPath([&count, &healed_path](uint64_t id, const FileEntry& entry, std::string_view path) {
    if (!entry.isDirectory && path == healed_path && id == kChild) {
      ++count;
    }
    return true;
  });
  CHECK(count == 1);
}

TEST_CASE("Live out-of-order child heals when parent arrives") {
  // Regression: live USN CREATE after initial population never re-runs
  // RecomputeAllPaths. With ReturnOnlyOnClose the directory CLOSE record
  // routinely arrives after the files created inside it, so the child is
  // first stored as a bare-name placeholder (name == path, no separator).
  // The later parent INSERT must heal it (Option A: targeted re-resolution).
  FileIndex index;

  constexpr uint64_t kParent = 0x00010000000000A0ULL;  // MFT record 160
  constexpr uint64_t kChild = 0x00010000000000B0ULL;   // MFT record 176
  constexpr std::string_view kHexName = "a1b2c3d4e5f60718293a4b5c";

  index.Insert(ntfs_file_reference::NtfsFileReference(kChild), ntfs_file_reference::NtfsFileReference(kParent), kHexName, false);
  const std::string bare = index.GetPathAccessor().GetPathCopy(kChild);
  REQUIRE(bare == kHexName);
  CHECK(bare.find_first_of("\\/") == std::string::npos);

  index.Insert(ntfs_file_reference::NtfsFileReference(kParent), ntfs_file_reference::NtfsFileReference(0), "NewAppDir", true);
  const std::string healed = index.GetPathAccessor().GetPathCopy(kChild);
  CHECK(healed.find_first_of("\\/") != std::string::npos);
  CHECK(healed.find("NewAppDir") != std::string::npos);
  CHECK(healed.find(kHexName) != std::string::npos);
  CHECK(healed == path_utils::JoinPath(
      index.GetPathAccessor().GetPathCopy(kParent), kHexName));
  CHECK(index.CheckBareNameInvariant());
}

TEST_CASE("Grandchild chain heals transitively when grandparent arrives") {
  // Child arrives before its parent dir, which itself arrives before the
  // grandparent: F bare, then P bare with F joined onto the placeholder
  // ("Staging/payload.bin"), then all healed when GP arrives.
  FileIndex index;

  constexpr uint64_t kGrandParent = 0x00010000000000C0ULL;
  constexpr uint64_t kParent = 0x00010000000000D0ULL;
  constexpr uint64_t kFile = 0x00010000000000E0ULL;

  index.Insert(ntfs_file_reference::NtfsFileReference(kFile), ntfs_file_reference::NtfsFileReference(kParent), "payload.bin", false);
  CHECK(index.GetPathAccessor().GetPathCopy(kFile) == "payload.bin");

  index.Insert(ntfs_file_reference::NtfsFileReference(kParent), ntfs_file_reference::NtfsFileReference(kGrandParent), "Staging", true);
  CHECK(index.GetPathAccessor().GetPathCopy(kParent) == "Staging");
  CHECK(index.GetPathAccessor().GetPathCopy(kFile) ==
        path_utils::JoinPath("Staging", "payload.bin"));

  index.Insert(ntfs_file_reference::NtfsFileReference(kGrandParent), ntfs_file_reference::NtfsFileReference(0), "AppDir", true);
  const std::string healed_parent = index.GetPathAccessor().GetPathCopy(kParent);
  const std::string healed_file = index.GetPathAccessor().GetPathCopy(kFile);
  CHECK(healed_parent.find("AppDir") != std::string::npos);
  CHECK(healed_parent.find("Staging") != std::string::npos);
  CHECK(healed_file.find("AppDir") != std::string::npos);
  CHECK(healed_file.find("Staging") != std::string::npos);
  CHECK(healed_file.find("payload.bin") != std::string::npos);
  CHECK(healed_file == path_utils::JoinPath(healed_parent, "payload.bin"));
  CHECK(index.CheckBareNameInvariant());
}

TEST_CASE("Healing one placeholder does not mis-graft a same-named subtree") {
  // Two concurrently-missing parents with same-named staging dirs: healing
  // the first must not rewrite the second's children via a global string
  // prefix ("Staging/" matches both subtrees).
  FileIndex index;

  constexpr uint64_t kDir1 = 0x0001000000000200ULL;
  constexpr uint64_t kDir2 = 0x0001000000000210ULL;
  constexpr uint64_t kStagingA = 0x0001000000000220ULL;
  constexpr uint64_t kStagingB = 0x0001000000000230ULL;
  constexpr uint64_t kFileA = 0x0001000000000240ULL;
  constexpr uint64_t kFileB = 0x0001000000000250ULL;

  index.Insert(ntfs_file_reference::NtfsFileReference(kFileA), ntfs_file_reference::NtfsFileReference(kStagingA), "payload.bin", false);
  index.Insert(ntfs_file_reference::NtfsFileReference(kStagingA), ntfs_file_reference::NtfsFileReference(kDir1), "Staging", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(kFileB), ntfs_file_reference::NtfsFileReference(kStagingB), "other.txt", false);
  index.Insert(ntfs_file_reference::NtfsFileReference(kStagingB), ntfs_file_reference::NtfsFileReference(kDir2), "Staging", true);
  REQUIRE(index.GetPathAccessor().GetPathCopy(kFileA) ==
          path_utils::JoinPath("Staging", "payload.bin"));
  REQUIRE(index.GetPathAccessor().GetPathCopy(kFileB) ==
          path_utils::JoinPath("Staging", "other.txt"));

  index.Insert(ntfs_file_reference::NtfsFileReference(kDir1), ntfs_file_reference::NtfsFileReference(0), "Dir1", true);
  CHECK(index.GetPathAccessor().GetPathCopy(kFileA).find("Dir1") != std::string::npos);
  // The unrelated same-named subtree must be untouched.
  CHECK(index.GetPathAccessor().GetPathCopy(kStagingB) == "Staging");
  CHECK(index.GetPathAccessor().GetPathCopy(kFileB) ==
        path_utils::JoinPath("Staging", "other.txt"));

  index.Insert(ntfs_file_reference::NtfsFileReference(kDir2), ntfs_file_reference::NtfsFileReference(0), "Dir2", true);
  const std::string healed_b = index.GetPathAccessor().GetPathCopy(kFileB);
  CHECK(healed_b.find("Dir2") != std::string::npos);
  CHECK(healed_b.find("Dir1") == std::string::npos);
  CHECK(healed_b.find("other.txt") != std::string::npos);
}

TEST_CASE("Never-arriving sweep evicts aged placeholders but spares fresh ones") {
  // Never-arriving parents: EvictNeverArrivingLocked(0) matches everything
  // (age >= 0 always holds); a far-future threshold spares fresh entries.
  // Deterministic without sleeping.
  FileIndex fresh_index;
  constexpr uint64_t kFreshParent = 0x0001000000000500ULL;
  constexpr uint64_t kFreshChild = 0x0001000000000510ULL;
  fresh_index.Insert(ntfs_file_reference::NtfsFileReference(kFreshChild), ntfs_file_reference::NtfsFileReference(kFreshParent), "fresh.tmp", false);
  CHECK(fresh_index.EvictNeverArrivingLocked(3600000) == 0);
  REQUIRE(fresh_index.GetEntry(kFreshChild) != nullptr);
  CHECK(fresh_index.GetPathAccessor().GetPathCopy(kFreshChild) == "fresh.tmp");
  const auto fresh_stats = fresh_index.GetAwaitingStats();
  CHECK(fresh_stats.count == 1);
  CHECK(fresh_index.CheckBareNameInvariant());

  FileIndex stale_index;
  constexpr uint64_t kStaleParent = 0x0001000000000520ULL;
  constexpr uint64_t kStaleChild = 0x0001000000000530ULL;
  stale_index.Insert(ntfs_file_reference::NtfsFileReference(kStaleChild), ntfs_file_reference::NtfsFileReference(kStaleParent), "a1b2c3d4e5f60718293a4b5c", false);
  REQUIRE(stale_index.GetAwaitingStats().count == 1);
  CHECK(stale_index.EvictNeverArrivingLocked(0) == 1);
  CHECK(stale_index.GetEntry(kStaleChild) == nullptr);
  CHECK(stale_index.GetAwaitingStats().count == 0);
  CHECK(stale_index.CheckBareNameInvariant());
}

TEST_CASE("Never-arriving sweep publishes evicted ids with wait ages") {
  FileIndex index;
  constexpr uint64_t kParent = 0x0001000000000540ULL;
  constexpr uint64_t kChild = 0x0001000000000550ULL;
  index.Insert(ntfs_file_reference::NtfsFileReference(kChild), ntfs_file_reference::NtfsFileReference(kParent), "gone.tmp", false);
  REQUIRE(index.GetAwaitingStats().count == 1);

  std::vector<index_domain_events::NeverArrivingEvicted> received;
  const size_t evicted = index.EvictNeverArrivingLocked(
      0, [&received](const index_domain_events::NeverArrivingEvicted& event) {
        received.push_back(event);
      });
  CHECK(evicted == 1);
  REQUIRE(received.size() == 1U);
  CHECK(received[0].id == kChild);
  // Age is wall-clock (Insert vs Collect instants); assert presence, not value.
}

TEST_CASE("Never-arriving sweep publishes the journal position with evictions") {
  FileIndex index;
  constexpr uint64_t kParent = 0x0001000000000560ULL;
  constexpr uint64_t kChild = 0x0001000000000570ULL;
  constexpr int64_t kUsn = 123456;
  FileIndex::InsertOptions options{};
  options.usn_at_track = kUsn;
  index.Insert(ntfs_file_reference::NtfsFileReference(kChild), ntfs_file_reference::NtfsFileReference(kParent), "gone.tmp", false, kFileTimeNotLoaded,
               kFileSizeNotLoaded, options);
  REQUIRE(index.GetAwaitingStats().count == 1);

  std::vector<index_domain_events::NeverArrivingEvicted> received;
  const size_t evicted = index.EvictNeverArrivingLocked(
      0, [&received](const index_domain_events::NeverArrivingEvicted& event) {
        received.push_back(event);
      });
  CHECK(evicted == 1);
  REQUIRE(received.size() == 1U);
  CHECK(received[0].id == kChild);
  CHECK(received[0].usn_at_track == kUsn);

  // Unknown journal position (default options) stays 0.
  FileIndex plain;
  constexpr uint64_t kPlainParent = 0x0001000000000580ULL;
  constexpr uint64_t kPlainChild = 0x0001000000000590ULL;
  plain.Insert(ntfs_file_reference::NtfsFileReference(kPlainChild), ntfs_file_reference::NtfsFileReference(kPlainParent), "plain.tmp", false);
  std::vector<index_domain_events::NeverArrivingEvicted> plain_received;
  CHECK(plain.EvictNeverArrivingLocked(
            0, [&plain_received](const index_domain_events::NeverArrivingEvicted& event) {
              plain_received.push_back(event);
            }) == 1);
  REQUIRE(plain_received.size() == 1U);
  CHECK(plain_received[0].usn_at_track == 0);
}

TEST_CASE("Never-arriving sweep evicts by journal gap in steady state") {
  FileIndex index;
  constexpr uint64_t kParent = 0x00010000000005A0ULL;
  constexpr uint64_t kOldChild = 0x00010000000005B0ULL;
  constexpr uint64_t kFreshChild = 0x00010000000005C0ULL;
  constexpr int64_t kGap = 1024 * 1024;
  constexpr int64_t kCurrentUsn = 2000000;
  FileIndex::InsertOptions old_options{};
  old_options.usn_at_track = kCurrentUsn - kGap;
  index.Insert(ntfs_file_reference::NtfsFileReference(kOldChild), ntfs_file_reference::NtfsFileReference(kParent), "old.tmp", false, kFileTimeNotLoaded,
               kFileSizeNotLoaded, old_options);
  FileIndex::InsertOptions fresh_options{};
  fresh_options.usn_at_track = kCurrentUsn - kGap + 100;
  index.Insert(ntfs_file_reference::NtfsFileReference(kFreshChild), ntfs_file_reference::NtfsFileReference(kParent), "fresh.tmp", false, kFileTimeNotLoaded,
               kFileSizeNotLoaded, fresh_options);
  REQUIRE(index.GetAwaitingStats().count == 2);

  std::vector<index_domain_events::NeverArrivingEvicted> received;
  const size_t evicted = index.EvictNeverArrivingByJournalGapLocked(
      kCurrentUsn, kGap,
      [&received](const index_domain_events::NeverArrivingEvicted& event) {
        received.push_back(event);
      });
  CHECK(evicted == 1);
  REQUIRE(received.size() == 1U);
  CHECK(received[0].id == kOldChild);
  CHECK(received[0].usn_at_track == kCurrentUsn - kGap);
  // Fresh entry survives the gap eviction; wall-clock spares it too.
  CHECK(index.GetEntry(kFreshChild) != nullptr);
  CHECK(index.GetAwaitingStats().count == 1);
  CHECK(index.CheckBareNameInvariant());
}

TEST_CASE("Evicting a filtered parent removes early-indexed children") {
  // Live mirror of HandleSystemFileFilter: a child CREATE processed before
  // its parent was marked $-filtered stays indexed (the parent is filtered,
  // never inserted, so awaiting-heal can never fix it). Marking filtered must
  // evict such descendants instead of leaving bare placeholders behind.
  FileIndex index;

  constexpr uint64_t kFilteredParent = 0x0001000000000300ULL;
  constexpr uint64_t kChildDir = 0x0001000000000310ULL;
  constexpr uint64_t kGrandChild = 0x0001000000000320ULL;

  index.Insert(ntfs_file_reference::NtfsFileReference(kChildDir), ntfs_file_reference::NtfsFileReference(kFilteredParent), "S-1-5-21-999", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(kGrandChild), ntfs_file_reference::NtfsFileReference(kChildDir), "leaked.txt", false);
  REQUIRE(index.GetEntry(kGrandChild) != nullptr);

  index.EvictDescendantsOfFilteredParentLocked(ntfs_file_reference::MftRecordNumber(
      ntfs_file_reference::RecordNumber(kFilteredParent)));
  CHECK(index.GetEntry(kChildDir) == nullptr);
  CHECK(index.GetEntry(kGrandChild) == nullptr);
  CHECK(index.CheckBareNameInvariant());
}

TEST_CASE("Renaming a bare placeholder does not mis-graft same-named subtree") {
  // Finding 2: Rename/Move cascades used a global prefix rewrite. Renaming
  // one of two colliding bare "Staging" dirs must not rewrite the other's
  // children.
  FileIndex index;

  constexpr uint64_t kDir1 = 0x0001000000000400ULL;
  constexpr uint64_t kDir2 = 0x0001000000000410ULL;
  constexpr uint64_t kStagingA = 0x0001000000000420ULL;
  constexpr uint64_t kStagingB = 0x0001000000000430ULL;
  constexpr uint64_t kFileA = 0x0001000000000440ULL;
  constexpr uint64_t kFileB = 0x0001000000000450ULL;

  index.Insert(ntfs_file_reference::NtfsFileReference(kFileA), ntfs_file_reference::NtfsFileReference(kStagingA), "payload.bin", false);
  index.Insert(ntfs_file_reference::NtfsFileReference(kStagingA), ntfs_file_reference::NtfsFileReference(kDir1), "Staging", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(kFileB), ntfs_file_reference::NtfsFileReference(kStagingB), "other.txt", false);
  index.Insert(ntfs_file_reference::NtfsFileReference(kStagingB), ntfs_file_reference::NtfsFileReference(kDir2), "Staging", true);

  REQUIRE(index.Rename(ntfs_file_reference::NtfsFileReference(kStagingA), "StagingRenamed"));
  // Awaiting bare entries stay bare on rename (no volume-root fallback),
  // descendants follow the renamed placeholder.
  const std::string expected_dir = "StagingRenamed";
  CHECK(index.GetPathAccessor().GetPathCopy(kStagingA) == expected_dir);
  CHECK(index.GetPathAccessor().GetPathCopy(kFileA) ==
        path_utils::JoinPath(expected_dir, "payload.bin"));
  // Unrelated same-named subtree untouched.
  CHECK(index.GetPathAccessor().GetPathCopy(kStagingB) == "Staging");
  CHECK(index.GetPathAccessor().GetPathCopy(kFileB) ==
        path_utils::JoinPath("Staging", "other.txt"));
  CHECK(index.CheckBareNameInvariant());
}

TEST_CASE("Renaming an awaiting placeholder stays bare and still heals") {
  // A rename arriving before the parent must not park the entry at the
  // volume root (C:\<hex> impersonates a real root file and slips past the
  // rooted-means-resolved assumption). It stays bare, stays tracked, and
  // heals under the new name when the parent arrives.
  FileIndex index;

  constexpr uint64_t kParent = 0x0001000000000600ULL;
  constexpr uint64_t kChild = 0x0001000000000610ULL;
  constexpr std::string_view kRenamed = "b2c3d4e5f60718293a4b5c7d";

  index.Insert(ntfs_file_reference::NtfsFileReference(kChild), ntfs_file_reference::NtfsFileReference(kParent), "staging.tmp", false);
  REQUIRE(index.Rename(ntfs_file_reference::NtfsFileReference(kChild), kRenamed));

  const std::string renamed = index.GetPathAccessor().GetPathCopy(kChild);
  CHECK(renamed == kRenamed);
  CHECK(renamed.find_first_of("\\/") == std::string::npos);
  CHECK(index.GetAwaitingStats().count == 1);
  CHECK(index.CheckBareNameInvariant());

  index.Insert(ntfs_file_reference::NtfsFileReference(kParent), ntfs_file_reference::NtfsFileReference(0), "AppDir", true);
  const std::string healed = index.GetPathAccessor().GetPathCopy(kChild);
  CHECK(healed == path_utils::JoinPath(
      index.GetPathAccessor().GetPathCopy(kParent), kRenamed));
  CHECK(index.CheckBareNameInvariant());
}

TEST_CASE("Move to unknown parent heals when parent arrives") {
  // Move onto a not-yet-indexed parent stores the same bare-name placeholder
  // as Insert branch 4; the later parent INSERT must heal it.
  FileIndex index;

  constexpr uint64_t kDirA = 0x0001000000000100ULL;
  constexpr uint64_t kFile = 0x0001000000000110ULL;
  constexpr uint64_t kMissingParent = 0x0001000000000120ULL;

  index.Insert(ntfs_file_reference::NtfsFileReference(kDirA), ntfs_file_reference::NtfsFileReference(0), "DirA", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(kFile), ntfs_file_reference::NtfsFileReference(kDirA), "doc.txt", false);
  REQUIRE(index.Move(ntfs_file_reference::NtfsFileReference(kFile), ntfs_file_reference::NtfsFileReference(kMissingParent)));

  const std::string bare = index.GetPathAccessor().GetPathCopy(kFile);
  CHECK(bare == "doc.txt");
  CHECK(bare.find_first_of("\\/") == std::string::npos);

  index.Insert(ntfs_file_reference::NtfsFileReference(kMissingParent), ntfs_file_reference::NtfsFileReference(0), "LateDir", true);
  const std::string healed = index.GetPathAccessor().GetPathCopy(kFile);
  CHECK(healed.find("LateDir") != std::string::npos);
  CHECK(healed.find("doc.txt") != std::string::npos);
  CHECK(healed == path_utils::JoinPath(
      index.GetPathAccessor().GetPathCopy(kMissingParent), "doc.txt"));
  CHECK(index.CheckBareNameInvariant());
}

TEST_CASE("InsertBatch defers healing to RecomputeAllPaths") {
  // Bulk MFT population skips per-insert healing (O(N^2) cascades); the
  // post-population recompute resolves out-of-order parents in one pass.
  FileIndex index;

  index.InsertBatch(MakeBulkChildBeforeParentBatch());

  CHECK(index.GetPathAccessor().GetPathCopy(kBulkChild) == "bulk.txt");

  index.RecomputeAllPaths();
  const std::string healed = index.GetPathAccessor().GetPathCopy(kBulkChild);
  CHECK(healed.find("BulkDir") != std::string::npos);
  CHECK(healed.find("bulk.txt") != std::string::npos);
  CHECK(healed.find_first_of("\\/") != std::string::npos);
  // Bulk inserts skip awaiting tracking by design (recompute resolves all),
  // so the invariant only holds after RecomputeAllPaths here.
  CHECK(index.CheckBareNameInvariant());
}

TEST_CASE("InsertBatch with deferred path indexing resolves paths once in recompute") {
  // Single-pass path indexing: bulk staging stores entries without paths
  // (the per-insert join + InsertPath would be discarded by the recompute
  // Clear()); RecomputeAllPaths indexes every path exactly once.
  FileIndex index;

  index.InsertBatch(MakeBulkChildBeforeParentBatch(), /*defer_path_indexing=*/true);

  // Entries are stored (visible by size) but carry no paths yet.
  CHECK(index.Size() == 2U);
  CHECK(index.GetPathAccessor().GetPathCopy(kBulkChild).empty());
  CHECK(index.GetPathAccessor().GetPathCopy(kBulkParent).empty());

  index.RecomputeAllPaths();
  const std::string healed = index.GetPathAccessor().GetPathCopy(kBulkChild);
  CHECK(healed.find("BulkDir") != std::string::npos);
  CHECK(healed.find("bulk.txt") != std::string::npos);
  CHECK(index.CheckBareNameInvariant());
}

TEST_CASE("MutationCount tracks membership and path changes") {
  // Auto-refresh trigger source: must move on every visible change,
  // including heals that keep Size().
  FileIndex index;
  CHECK(index.MutationCount() == 0);

  constexpr uint64_t kParent = 0x00010000000000A0ULL;
  constexpr uint64_t kChild = 0x00010000000000B0ULL;
  index.Insert(ntfs_file_reference::NtfsFileReference(kChild), ntfs_file_reference::NtfsFileReference(kParent), "late.txt", false);
  const uint64_t after_insert = index.MutationCount();
  CHECK(after_insert > 0);

  // Heal (parent arrival): Size() grows by exactly the added parent while
  // the child's path fix is invisible to it — the version still moves.
  const size_t size_before = index.Size();
  index.Insert(ntfs_file_reference::NtfsFileReference(kParent), ntfs_file_reference::NtfsFileReference(0), "LateDir", true);
  CHECK(index.Size() == size_before + 1);
  CHECK(index.MutationCount() > after_insert);

  // Rename / move / remove bump.
  const uint64_t after_heal = index.MutationCount();
  REQUIRE(index.Rename(ntfs_file_reference::NtfsFileReference(kChild), "renamed.txt"));
  CHECK(index.MutationCount() > after_heal);
  const uint64_t after_rename = index.MutationCount();
  REQUIRE(index.Move(ntfs_file_reference::NtfsFileReference(kChild), ntfs_file_reference::NtfsFileReference(kParent)));
  CHECK(index.MutationCount() > after_rename);
  const uint64_t after_move = index.MutationCount();
  index.Remove(ntfs_file_reference::NtfsFileReference(kChild));
  CHECK(index.MutationCount() > after_move);

  // Clear and recompute bump.
  const uint64_t before_clear = index.MutationCount();
  index.Clear();
  CHECK(index.MutationCount() > before_clear);
  index.Insert(ntfs_file_reference::NtfsFileReference(kChild), ntfs_file_reference::NtfsFileReference(kParent), "late.txt", false);
  const uint64_t before_recompute = index.MutationCount();
  index.RecomputeAllPaths();
  CHECK(index.MutationCount() > before_recompute);
}

TEST_CASE("Insert does not rewrite volume-root parent via record map") {
  FileIndex index;

  constexpr uint64_t kIndexedVolumeRoot = 0x0001000000000005ULL;
  constexpr uint64_t kZeroSeqVolumeParent = ntfs_file_reference::kRootDirectoryRecordNumber;
  constexpr uint64_t kDriveRootFile = 0x0001000000000100ULL;

  // Real FRN for record 5 in the map. Insert must still keep a zero-sequence
  // ParentFileReferenceNumber=5 as-is (volume-root parents are not rewritten).
  index.Insert(ntfs_file_reference::NtfsFileReference(kIndexedVolumeRoot), ntfs_file_reference::NtfsFileReference(0), ".", true);
  index.Insert(ntfs_file_reference::NtfsFileReference(kDriveRootFile), ntfs_file_reference::NtfsFileReference(kZeroSeqVolumeParent), "file1.txt", false);

  {
    const std::shared_lock lock(index.GetMutex());
    const FileEntry* const file = index.GetEntry(kDriveRootFile);
    REQUIRE(file != nullptr);
    CHECK(file->parentID.raw == kZeroSeqVolumeParent);
    CHECK_FALSE(file->parentID.raw == kIndexedVolumeRoot);
  }

  index.RecomputeAllPaths();
  CHECK(index.GetEntry(kDriveRootFile) != nullptr);
  CHECK(index.GetPathAccessor().GetPathCopy(kDriveRootFile) ==
        std::string(path_utils::GetDefaultVolumeRootPath()).append("file1.txt"));
}

TEST_CASE("WalkIndexedPath matches materialized paths after mixed inserts and recompute") {
  TrackedIndex harness;

  constexpr uint64_t kVolumeRoot = ntfs_file_reference::kRootDirectoryRecordNumber;
  constexpr uint64_t kDirA = 0x00010000000000A0ULL;
  constexpr uint64_t kFileB = 0x00010000000000B0ULL;
  constexpr uint64_t kFileC = 0x00010000000000C0ULL;
  constexpr uint64_t kDirD = 0x00010000000000D0ULL;

  harness.TrackInsert(1, kVolumeRoot, "root", true);
  // Out-of-order: files before parents, then heal via parent inserts.
  harness.TrackInsert(kFileC, kFileB, "c.txt", false);
  harness.TrackInsert(kFileB, kDirA, "b.txt", false);
  harness.TrackInsert(kDirD, 1, "DirD", true);
  harness.TrackInsert(kDirA, 1, "DirA", true);

  // Rename + move before the topological pass (map tracks the rename).
  CHECK(harness.index.Rename(ntfs_file_reference::NtfsFileReference(kFileB), "renamed.txt"));
  harness.names[kFileB] = "renamed.txt";
  CHECK(harness.index.Move(ntfs_file_reference::NtfsFileReference(kFileC), ntfs_file_reference::NtfsFileReference(kDirD)));

  harness.index.RecomputeAllPaths();
  REQUIRE(harness.index.GetAwaitingStats().count == 0);

  for (const uint64_t id : harness.ids) {
    const auto walked = WalkIndexedPath(harness.index, id, harness.names);
    REQUIRE(walked.has_value());
    CHECK(*walked == harness.index.GetPathAccessor().GetPathCopy(id));
  }
  CHECK(harness.index.CheckBareNameInvariant());
}

TEST_CASE("WalkIndexedPath agrees with awaiting tracking without recompute") {
  TrackedIndex harness;

  constexpr uint64_t kVolumeRoot = ntfs_file_reference::kRootDirectoryRecordNumber;
  constexpr uint64_t kDirA = 0x00010000000000A0ULL;
  constexpr uint64_t kFileB = 0x00010000000000B0ULL;
  constexpr uint64_t kDirD = 0x00010000000000D0ULL;

  harness.TrackInsert(1, kVolumeRoot, "root", true);
  harness.TrackInsert(kFileB, kDirA, "b.txt", false);  // Parent missing: awaiting.
  harness.TrackInsert(kDirD, 1, "DirD", true);          // Parent present: resolved.
  REQUIRE(harness.index.GetAwaitingStats().count == 1);

  // Biconditional without any recompute: the walk breaks exactly where
  // awaiting tracking says it must, and agrees everywhere else.
  for (const uint64_t id : harness.ids) {
    const auto walked = WalkIndexedPath(harness.index, id, harness.names);
    if (harness.index.IsAwaitingChild(ntfs_file_reference::NtfsFileReference(id))) {
      CHECK(!walked.has_value());
    } else {
      REQUIRE(walked.has_value());
      CHECK(*walked == harness.index.GetPathAccessor().GetPathCopy(id));
    }
  }

  // Parent arrives: heal resolves the chain; full agreement, nothing awaiting.
  harness.TrackInsert(kDirA, 1, "DirA", true);
  REQUIRE(harness.index.GetAwaitingStats().count == 0);
  for (const uint64_t id : harness.ids) {
    const auto walked = WalkIndexedPath(harness.index, id, harness.names);
    REQUIRE(walked.has_value());
    CHECK(*walked == harness.index.GetPathAccessor().GetPathCopy(id));
  }
  CHECK(harness.index.CheckBareNameInvariant());
}

TEST_CASE("RecomputeAllPaths resolves subtrees whose top-level directories parent to absent volume root") {
  FileIndex index;
  // Volume root (record 5) is absent from the index (standard NTFS USN layout).
  constexpr uint64_t kVolumeRootFrn = 0x0001000000000005ULL;
  constexpr uint64_t kUsersDir = 0x0001000000000100ULL;
  constexpr uint64_t kJohnDir = 0x0001000000000200ULL;
  constexpr uint64_t kDocFile = 0x0001000000000300ULL;

  // Insert via population batch so paths are initially placeholders and resolved in RecomputeAllPaths
  std::vector<FileIndex::PopulationBatchEntry> batch;
  batch.push_back({ntfs_file_reference::NtfsFileReference(kUsersDir),
                   ntfs_file_reference::NtfsFileReference(kVolumeRootFrn),
                   file_name::FileName("Users"), true,});
  batch.push_back({ntfs_file_reference::NtfsFileReference(kJohnDir),
                   ntfs_file_reference::NtfsFileReference(kUsersDir),
                   file_name::FileName("John"), true,});
  batch.push_back({ntfs_file_reference::NtfsFileReference(kDocFile),
                   ntfs_file_reference::NtfsFileReference(kJohnDir),
                   file_name::FileName("resume.pdf"), false,});

  index.InsertBatch(batch);
  index.RecomputeAllPaths();

  const std::string users_path = index.GetPathAccessor().GetPathCopy(kUsersDir);
  const std::string john_path = index.GetPathAccessor().GetPathCopy(kJohnDir);
  const std::string doc_path = index.GetPathAccessor().GetPathCopy(kDocFile);

  CHECK_FALSE(users_path.empty());
  CHECK(users_path.find("Users") != std::string::npos);
  CHECK(john_path.find("Users") != std::string::npos);
  CHECK(john_path.find("John") != std::string::npos);
  CHECK(doc_path.find("Users") != std::string::npos);
  CHECK(doc_path.find("John") != std::string::npos);
  CHECK(doc_path.find("resume.pdf") != std::string::npos);
}
