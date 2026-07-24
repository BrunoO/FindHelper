#include <array>
#include <chrono>
#include <doctest/doctest.h>
#include <mutex>
#include <shared_mutex>

#include "index/FileIndex.h"
#include "index/FileIndexStorage.h"
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
  storage.InsertLocked(kVolumeRootFrn, 0, "real-root", true, kFileTimeNotLoaded,
                       /*register_mft_record=*/true);

  // Synthetic next_file_id_-style id whose RecordNumber equals 5 — must not replace
  // the FRN mapping used by ResolveEntryReference.
  storage.InsertLocked(5, 0, "synthetic", true, kFileTimeNotLoaded,
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
  index.Insert(kFolderFrn, 0, "Users", true);
  index.Insert(kChildFrn, kFolderFrn, "doc.txt", false);

  // Force a synthetic id equal to record 100 without registering it.
  index.Insert(100, 0, "poison", true, kFileTimeNotLoaded, kFileSizeNotLoaded,
               /*register_mft_record=*/false);

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

  index.Insert(kDriveRootFileId, kVolumeRootFrn, "file.txt", false, kFileTimeNotLoaded);
  index.Insert(kFolderId, kVolumeRootFrn, "SomeFolder", true, kFileTimeNotLoaded);
  index.Insert(kNestedFileId, kFolderId, "nested.txt", false, kFileTimeNotLoaded);
  // Pretend a synthetic "C:" directory occupies record number 5.
  index.Insert(kSyntheticVolumeRootId, 0, "C:", true, kFileTimeNotLoaded,
               kFileSizeNotLoaded, /*register_mft_record=*/false);

  REQUIRE(index.GetEntry(kDriveRootFileId) != nullptr);
  REQUIRE(index.GetEntry(kNestedFileId) != nullptr);
  REQUIRE(index.GetEntry(kFolderId) != nullptr);

  // Soft-delete the nested folder only — must remove folder + nested file, keep
  // the volume-root child even though record 5 is present in the index.
  RemoveIndexedSubtree(index, kFolderId);

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

  index.Insert(kVictimFolder, kVolumeRootFrn, "bulk", true);
  index.Insert(kVictimChild, kVictimFolder, "temp.txt", false);
  // Insert under volume root so PathStorage gets a drive-root child path, then
  // corrupt parentID to the victim folder (simulates linkage drift).
  index.Insert(kDriveRootFile, kVolumeRootFrn, "file1.txt", false);
  {
    const std::unique_lock lock(index.GetMutex());
    FileEntry* const entry = index.GetEntryMutable(kDriveRootFile);
    REQUIRE(entry != nullptr);
    entry->parentID = kVictimFolder;
  }

  const std::string drive_path = index.GetPathAccessor().GetPathCopy(kDriveRootFile);
  REQUIRE(path_utils::IsVolumeRootChildPath(drive_path));

  RemoveIndexedSubtree(index, kVictimFolder);

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

  index.Insert(kDriveRootFile, kVolumeRootFrn, "root.txt", false);
  index.Insert(kBulkFolder, kVolumeRootFrn, "bulk", true);
  for (size_t i = 0; i < kBulkFileCount; ++i) {
    const uint64_t file_id = 0x0002000000001000ULL + i;
    index.Insert(file_id, kBulkFolder, "f", false);
  }

  const std::string root_path_before =
      index.GetPathAccessor().GetPathCopy(kDriveRootFile);
  REQUIRE(path_utils::IsVolumeRootChildPath(root_path_before));

  for (size_t i = 0; i < kBulkFileCount; ++i) {
    RemoveIndexedSubtree(index, 0x0002000000001000ULL + i);
  }
  RemoveIndexedSubtree(index, kBulkFolder);

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

  index.Insert(kDriveRootFileId, kVolumeRootFrn, "file.txt", false, kFileTimeNotLoaded);
  index.Insert(kSyntheticVolumeRootId, 0, "C:", true, kFileTimeNotLoaded,
               kFileSizeNotLoaded, /*register_mft_record=*/false);

  RemoveIndexedSubtree(index, kSyntheticVolumeRootId);

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

  index.Insert(kFolderFrn, 0, "Users", true);
  index.Insert(kChildFrn, kFolderFrn, "doc.txt", false);
  index.Insert(kSyntheticId, 0, "poison", true, kFileTimeNotLoaded, kFileSizeNotLoaded,
               /*register_mft_record=*/false);

  RemoveIndexedSubtree(index, kSyntheticId);

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

  index.Insert(kLiveFrn, 0, "live.txt", false);

  RemoveIndexedSubtree(index, kMissingPeerFrn);

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
  index.Insert(kUnrelated, kStaleParentToVictimFile, "unrelated.txt", false);
  index.Insert(kVictimDir, 0, "VictimDir", true);
  index.Insert(kVictimFile, kVictimDir, "victim.txt", false);

  {
    const std::shared_lock lock(index.GetMutex());
    const FileEntry* const unrelated = index.GetEntry(kUnrelated);
    REQUIRE(unrelated != nullptr);
    CHECK(unrelated->parentID == kStaleParentToVictimFile);
  }

  RemoveIndexedSubtree(index, kVictimDir);

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
  index.Insert(kChildFrn, kStaleParentRef, "doc.txt", false);
  index.Insert(kFolderFrn, 0, "Users", true);

  {
    const std::shared_lock lock(index.GetMutex());
    const FileEntry* const child = index.GetEntry(kChildFrn);
    REQUIRE(child != nullptr);
    CHECK(child->parentID == kStaleParentRef);
  }

  RemoveIndexedSubtree(index, kFolderFrn);

  CHECK(index.GetEntry(kFolderFrn) == nullptr);
  CHECK(index.GetEntry(kChildFrn) == nullptr);
}

TEST_CASE("Insert resolves stale parent FRN to canonical id and path") {
  FileIndex index;

  constexpr uint64_t kFolderFrn = 0x0002000000000064ULL;
  constexpr uint64_t kStaleParentRef = 0x0001000000000064ULL;
  constexpr uint64_t kChildFrn = 0x0001000000000200ULL;

  index.Insert(kFolderFrn, 0, "Users", true);
  index.Insert(kChildFrn, kStaleParentRef, "doc.txt", false);

  {
    const std::shared_lock lock(index.GetMutex());
    const FileEntry* const child = index.GetEntry(kChildFrn);
    REQUIRE(child != nullptr);
    CHECK(child->parentID == kFolderFrn);
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

  index.Insert(kOldParent, 0, "OldDir", true);
  index.Insert(kNewParent, 0, "NewDir", true);
  index.Insert(kFileId, kOldParent, "doc.txt", false);

  REQUIRE(index.Move(kFileId, kStaleNewParent));

  {
    const std::shared_lock lock(index.GetMutex());
    const FileEntry* const file = index.GetEntry(kFileId);
    REQUIRE(file != nullptr);
    CHECK(file->parentID == kNewParent);
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

  index.Insert(kVictimFolder, 0, "victim", true);
  index.Insert(kVictimChild, kVictimFolder, "child.txt", false);

  for (size_t i = 0; i < kUnrelatedCount; ++i) {
    const uint64_t folder_id = 0x0002000000001000ULL + i;
    const uint64_t file_id = 0x0003000000001000ULL + i;
    index.Insert(folder_id, 0, "d", true);
    index.Insert(file_id, folder_id, "f", false);
  }

  const auto start = std::chrono::steady_clock::now();
  RemoveIndexedSubtree(index, kVictimFolder);
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

  auto count_entries = [&index]() {
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

  index.Insert(1, 1, "refs", true);
  const uint64_t remotes_id = 0x0002000000000050ULL;
  index.Insert(remotes_id, 1, "remotes", true);

  const uint64_t stale_remotes_ref = 0x0001000000000050ULL;
  const uint64_t origin_id = 0x0003000000000060ULL;
  index.Insert(origin_id, stale_remotes_ref, "origin", true);

  const uint64_t head_id = 0x0004000000000070ULL;
  index.Insert(head_id, origin_id, "HEAD", false);

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
  index.Insert(origin_id, missing_parent, "origin", true);

  const uint64_t head_id = 0x0004000000000070ULL;
  index.Insert(head_id, origin_id, "HEAD", false);

  index.RecomputeAllPaths();

  CHECK(index.Size() == 0);
}

TEST_CASE("RecomputeAllPaths keeps volume-root children when NTFS root record is absent") {
  FileIndex index;

  // MFT record 5 (volume root) is usually not present in the index after USN enum.
  const uint64_t root_frn = 0x0001000000000005ULL;
  const uint64_t windows_id = 0x0002000000000100ULL;
  index.Insert(windows_id, root_frn, "Windows", true);

  const uint64_t system32_id = 0x0003000000000200ULL;
  index.Insert(system32_id, windows_id, "System32", true);

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

  index.Insert(kPoisonedVolumeRoot, kMissingParent, ".", true);
  index.Insert(kDriveRootFile, kStaleVolumeParent, "file1.txt", false);

  {
    const std::shared_lock lock(index.GetMutex());
    const FileEntry* const file = index.GetEntry(kDriveRootFile);
    REQUIRE(file != nullptr);
    // Insert must not rewrite volume-root parents to the poisoned canonical id.
    CHECK(file->parentID == kStaleVolumeParent);
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
  index.Insert(kFileId, kRootFrn, "file1.txt", false);

  REQUIRE(index.GetEntry(kFileId) != nullptr);
  CHECK(index.GetPathAccessor().GetPathCopy(kFileId) ==
        std::string(path_utils::GetDefaultVolumeRootPath()).append("file1.txt"));
}

TEST_CASE("Move onto volume-root parent builds drive-root path") {
  FileIndex index;

  constexpr uint64_t kOldParent = 0x0001000000000010ULL;
  constexpr uint64_t kRootFrn = 0x0001000000000005ULL;
  constexpr uint64_t kFileId = 0x0001000000000300ULL;

  index.Insert(kOldParent, 0, "OldDir", true);
  index.Insert(kFileId, kOldParent, "file1.txt", false);
  REQUIRE(index.Move(kFileId, kRootFrn));

  CHECK(index.GetPathAccessor().GetPathCopy(kFileId) ==
        std::string(path_utils::GetDefaultVolumeRootPath()).append("file1.txt"));
}

TEST_CASE("Insert does not rewrite volume-root parent via record map") {
  FileIndex index;

  constexpr uint64_t kIndexedVolumeRoot = 0x0001000000000005ULL;
  constexpr uint64_t kZeroSeqVolumeParent = ntfs_file_reference::kRootDirectoryRecordNumber;
  constexpr uint64_t kDriveRootFile = 0x0001000000000100ULL;

  // Real FRN for record 5 in the map. Insert must still keep a zero-sequence
  // ParentFileReferenceNumber=5 as-is (volume-root parents are not rewritten).
  index.Insert(kIndexedVolumeRoot, 0, ".", true);
  index.Insert(kDriveRootFile, kZeroSeqVolumeParent, "file1.txt", false);

  {
    const std::shared_lock lock(index.GetMutex());
    const FileEntry* const file = index.GetEntry(kDriveRootFile);
    REQUIRE(file != nullptr);
    CHECK(file->parentID == kZeroSeqVolumeParent);
    CHECK_FALSE(file->parentID == kIndexedVolumeRoot);
  }

  index.RecomputeAllPaths();
  CHECK(index.GetEntry(kDriveRootFile) != nullptr);
  CHECK(index.GetPathAccessor().GetPathCopy(kDriveRootFile) ==
        std::string(path_utils::GetDefaultVolumeRootPath()).append("file1.txt"));
}
