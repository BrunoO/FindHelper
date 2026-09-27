#include "index/RemoveIndexedSubtree.h"

#include <shared_mutex>
#include <unordered_set>
#include <vector>

#include "index/FileIndex.h"
#include "index/NtfsFileReference.h"
#include "path/PathUtils.h"

namespace {

[[nodiscard]] bool ParentIsMarkedForRemoval(
    const FileIndex& file_index, uint64_t parent_id,
    const std::unordered_set<uint64_t>& remove_ids) {
  if (remove_ids.count(parent_id) != 0) {
    // Exact id in the remove set: only directories cascade. Files appear in remove_ids
    // as cascade leaves; treating them as parents would delete unrelated entries
    // whose parentID was rewritten to that file id (stale-sequence Insert resolve).
    if (const FileEntry* const marked = file_index.GetEntry(parent_id); marked != nullptr) {
      return marked->isDirectory;
    }
    return false;
  }
  // Fast path: parent exists under this exact id and is not marked. Skip
  // ResolveEntryReference for the common non-child case (one hash lookup per
  // index entry per scan would dominate soft-delete on large indexes).
  // Resolve only when the stored parentID is missing (stale USN sequence).
  if (file_index.GetEntry(parent_id) != nullptr) {
    return false;
  }
  const auto [parent_entry, resolved_parent] =
      file_index.ResolveEntryReference(parent_id);
  // Only directories can be parents. A file in remove_ids that shares an MFT record
  // number with a stale parentID must not cascade-delete unrelated entries.
  return parent_entry != nullptr && parent_entry->isDirectory &&
         remove_ids.count(resolved_parent) != 0;
}

[[nodiscard]] bool TryMarkEntryForSubtreeRemoval(
    const FileIndex& file_index, uint64_t id, const FileEntry& entry,
    std::unordered_set<uint64_t>& remove_ids,
    std::vector<uint64_t>& ids_to_remove) {
  if (remove_ids.count(id) != 0) {
    return false;
  }
  // Never cascade through the implicit NTFS volume root (MFT record 5).
  // Drive-root files (C:\file.txt) all parent there; treating record 5 as
  // a deleted parent would evacuate every volume-root child.
  if (ntfs_file_reference::IsRootDirectoryRecord(entry.parentID.raw)) {
    return false;
  }
  // parentID 0 is the true root for synthetic InsertPath entries.
  if (entry.parentID.raw == 0) {
    return false;
  }
  if (!ParentIsMarkedForRemoval(file_index, entry.parentID.raw, remove_ids)) {
    return false;
  }
  // Belt-and-suspenders: corrupted parentID must not evacuate entries
  // whose stored path is still a drive-root child (C:\name).
  // ForEachEntryLockHeld already assumes the index lock is held.
  if (path_utils::IsVolumeRootChildPath(file_index.GetPathViewLockHeld(id))) {
    return false;
  }
  remove_ids.insert(id);
  ids_to_remove.push_back(id);
  return true;
}

}  // namespace

void RemoveIndexedSubtreeLocked(FileIndex& file_index,
                                ntfs_file_reference::NtfsFileReference root_id) {
  bool is_directory = false;
  bool root_found = false;
  // Exact id only. Do not ResolveEntryReference(root_id): that maps by 48-bit MFT
  // record number and ignores sequence, so a missing $-prefixed / recycled FRN
  // that shares a record number with a live indexed file would redirect here and
  // Remove() would silently evict the live file. USN roots are current live FRNs;
  // stale-sequence resolve remains only for child parentID matching below.
  if (const FileEntry* const entry = file_index.GetEntry(root_id.raw); entry != nullptr) {
    is_directory = entry->isDirectory;
    root_found = true;
  }
  if (!root_found) {
    file_index.RemoveLocked(root_id);
    return;
  }
  if (!is_directory) {
    file_index.RemoveLocked(root_id);
    return;
  }

  std::unordered_set<uint64_t> remove_ids;
  remove_ids.insert(root_id.raw);
  std::vector<uint64_t> ids_to_remove;
  ids_to_remove.push_back(root_id.raw);

  // Fixed-point discovery: each pass finds children of already-marked parents.
  // Recycle Bin folder depth is typically small, so full-index scans stay cheap.
  // Use ForEachEntryLockHeld (not ForEachEntryWithPath): resolving every path on a
  // large index under the exclusive lock starves search; path veto is only for
  // the rare parentID-corruption case and runs after membership matches.
  bool grew = true;
  while (grew) {
    grew = false;
    file_index.ForEachEntryLockHeld(
        [&file_index, &remove_ids, &ids_to_remove, &grew](uint64_t id,
                                                          const FileEntry& entry) {
          if (TryMarkEntryForSubtreeRemoval(file_index, id, entry, remove_ids,
                                            ids_to_remove)) {
            grew = true;
          }
          return true;
        });
  }

  for (const uint64_t id : ids_to_remove) {
    file_index.RemoveLocked(ntfs_file_reference::NtfsFileReference(id));
  }
}

void RemoveIndexedSubtree(FileIndex& file_index,
                          ntfs_file_reference::NtfsFileReference root_id) {
  const std::unique_lock lock(file_index.GetMutex());
  RemoveIndexedSubtreeLocked(file_index, root_id);
}
