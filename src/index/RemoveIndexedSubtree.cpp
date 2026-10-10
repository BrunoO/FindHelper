#include "index/RemoveIndexedSubtree.h"

#include <cstdint>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "index/FileIndex.h"
#include "index/NtfsFileReference.h"
#include "path/PathUtils.h"

namespace {

struct SubtreeSnapshotEntry {
  uint64_t id = 0;
  uint64_t parent_raw = 0;
  bool is_directory = false;
};

// Snapshot rows that must never enter the subtree: already marked entries,
// and entries parented directly by the implicit NTFS volume root (MFT
// record 5) or the synthetic root (parentID 0) — same guards as the
// pre-worklist implementation.
bool IsExcludedFromCascade(const SubtreeSnapshotEntry& snap,
                           const std::unordered_set<uint64_t>& remove_ids) {
  return remove_ids.count(snap.id) != 0 || snap.parent_raw == 0 ||
         ntfs_file_reference::IsRootDirectoryRecord(snap.parent_raw);
}

// True when snap.parent ties this entry to the marked directory being
// processed. Exact-id match first; for a stale USN sequence (parent's
// sequence number moved on) fall back to an authoritative resolve: only
// directories cascade and the resolved id must already be marked — so a
// file that merely shares the record number never evacuates unrelated
// entries.
bool IsMarkedSubtreeMember(const SubtreeSnapshotEntry& snap,
                           uint64_t marked_id,
                           uint64_t marked_record,
                           const std::unordered_set<uint64_t>& remove_ids,
                           const FileIndex& file_index) {
  if (snap.parent_raw == marked_id) {
    return true;
  }
  if (ntfs_file_reference::RecordNumber(snap.parent_raw) != marked_record) {
    return false;
  }
  const auto [parent_entry, resolved_parent] =
      file_index.ResolveEntryReference(snap.parent_raw);
  return parent_entry != nullptr && parent_entry->isDirectory &&
         remove_ids.count(resolved_parent) != 0;
}

}  // namespace

void RemoveIndexedSubtreeLocked(FileIndex& file_index,
                                ntfs_file_reference::NtfsFileReference root_id) {
  // Exact id only. Do not ResolveEntryReference(root_id): that maps by 48-bit MFT
  // record number and ignores sequence, so a missing $-prefixed / recycled FRN
  // that shares a record number with a live indexed file would redirect here and
  // Remove() would silently evict the live file. USN roots are current live FRNs;
  // stale-sequence resolve remains only for child parentID matching below.
  // Missing root, or a root that resolves to a plain file: single-entry
  // removal, no cascade.
  if (const FileEntry* const root_entry = file_index.GetEntry(root_id.raw);
      root_entry == nullptr || !root_entry->isDirectory) {
    file_index.RemoveLocked(root_id);
    return;
  }

  std::unordered_set<uint64_t> remove_ids;
  remove_ids.insert(root_id.raw);
  std::vector<uint64_t> ids_to_remove;
  ids_to_remove.push_back(root_id.raw);

  // Single enumeration pass into thread-local scratch (clear() keeps
  // capacity across consecutive directory deletes in a storm). Discovery is
  // then driven by a worklist of marked directories: each one scans the
  // compact snapshot once for exact-id children (integer compare) plus
  // record-number matches resolved authoritatively for stale USN sequences.
  // The old code rescanned the index once per tree-depth level with one live
  // GetEntry per entry per pass. Outcomes are identical: the index is
  // immutable during discovery (caller holds unique_lock; removals happen
  // after), files never cascade (only directories enter the worklist, same
  // rule as before), and the resolve tail keeps its directory + marked
  // checks — so a file sharing the record number never evacuates unrelated
  // entries.
  thread_local std::vector<SubtreeSnapshotEntry> snapshot;
  snapshot.clear();
  snapshot.reserve(file_index.Size());
  file_index.ForEachEntryLockHeld([](uint64_t id,
                                     const FileEntry& entry) {
    // No capture: snapshot is thread_local (static storage duration),
    // referenced directly. Explicit [&] is banned in template contexts
    // (MSVC) and capturing thread_locals does not compile.
    snapshot.push_back({id, entry.parentID.raw, entry.isDirectory});
    return true;
  });

  std::vector<uint64_t> dir_worklist;
  dir_worklist.push_back(root_id.raw);
  while (!dir_worklist.empty()) {
    const uint64_t marked_id = dir_worklist.back();
    dir_worklist.pop_back();
    const uint64_t marked_record =
        ntfs_file_reference::RecordNumber(marked_id);
    for (const SubtreeSnapshotEntry& snap : snapshot) {
      if (IsExcludedFromCascade(snap, remove_ids)) {
        continue;
      }
      if (!IsMarkedSubtreeMember(snap, marked_id, marked_record, remove_ids,
                                 file_index)) {
        continue;
      }
      // Belt-and-suspenders: corrupted parentID must not evacuate entries
      // whose stored path is still a drive-root child (C:\name).
      if (path_utils::IsVolumeRootChildPath(
              file_index.GetPathViewLockHeld(snap.id))) {
        continue;
      }
      remove_ids.insert(snap.id);
      ids_to_remove.push_back(snap.id);
      if (snap.is_directory) {
        dir_worklist.push_back(snap.id);
      }
    }
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
