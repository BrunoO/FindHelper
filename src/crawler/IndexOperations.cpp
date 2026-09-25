#include "crawler/IndexOperations.h"

#include "index/NtfsFileReference.h"
#include "path/PathUtils.h"
#include "utils/FileTimeTypes.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

// Retrieve the FileEntry and full path for an id. Returns {nullptr, ""} if not found.
std::pair<const FileEntry*, std::string> GetEntryAndPath(
    const FileIndexStorage& storage, const PathOperations& path_operations, uint64_t id) {
  const FileEntry* entry = storage.GetEntry(id);
  if (entry == nullptr) {
    return {nullptr, {}};
  }
  return {entry, path_operations.GetPath(id)};
}

// Map a USN ParentFileReferenceNumber to the canonical indexed id when the parent
// is already present (stale sequence in the upper 16 bits is common). If the parent
// is not indexed yet, keep the raw reference for later RecomputeAllPaths.
// Cost: one record_number_to_id_ lookup (+ GetEntry) — same order as GetPathView's
// GetEntry; string join / PathStorage work still dominates Insert.
[[nodiscard]] uint64_t ResolveParentIdForStorage(const FileIndexStorage& storage,
                                                 uint64_t parent_id) {
  if (parent_id == 0) {
    return 0;
  }
  // Never rewrite NTFS volume-root parents (MFT record 5). ResolveEntryReference can
  // bind them to synthetic next_file_id_==5 or a fragile indexed "." root and then
  // prune/cascade can drop every C:\* drive-root child with that parent.
  if (ntfs_file_reference::IsRootDirectoryRecord(parent_id)) {
    return parent_id;
  }
  if (const auto [parent_entry, resolved_parent] =
          storage.ResolveEntryReference(parent_id);
      parent_entry != nullptr) {
    return resolved_parent;
  }
  return parent_id;
}

}  // namespace

IndexOperations::IndexOperations(FileIndexStorage& storage,
                                 PathOperations& path_operations,
                                 std::atomic<size_t>& remove_not_in_index_count,
                                 std::atomic<size_t>& remove_inconsistency_count,
                                 index_domain_events::ParentHealedSink on_parent_healed,
                                 PathHashRefreshCallback on_path_refreshed)
    : storage_(storage),
      path_operations_(path_operations),
      remove_not_in_index_count_(remove_not_in_index_count),
      remove_inconsistency_count_(remove_inconsistency_count),
      on_parent_healed_(std::move(on_parent_healed)),
      on_path_refreshed_(std::move(on_path_refreshed)) {}

void IndexOperations::ClearAwaiting() {
  flat_hash_map_t<ntfs_file_reference::MftRecordNumber,
                  std::vector<UnresolvedReference>>{}
      .swap(awaiting_parent_);
}

IndexOperations::AwaitingStats IndexOperations::GetAwaitingStats() const {
  AwaitingStats stats;
  const auto now = std::chrono::steady_clock::now();
  for (const auto& [parent_record, children] : awaiting_parent_) {
    (void)parent_record;
    stats.count += children.size();
    for (const auto& child : children) {
      stats.oldest_age_ms = (std::max)(stats.oldest_age_ms, child.AgeMs(now));
    }
  }
  return stats;
}

void IndexOperations::CollectNeverArriving(uint64_t max_age_ms,
                                          std::vector<NeverArriving>& out) const {
  const auto now = std::chrono::steady_clock::now();
  for (const auto& [parent_record, children] : awaiting_parent_) {
    (void)parent_record;
    for (const auto& child : children) {
      const uint64_t age_ms = child.AgeMs(now);
      if (age_ms >= max_age_ms) {
        out.push_back({child.id, age_ms, child.usn_at_track});
      }
    }
  }
}

void IndexOperations::CollectNeverArrivingByJournalGap(int64_t current_usn,
                                                       int64_t max_usn_gap,
                                                       std::vector<NeverArriving>& out) const {
  const auto now = std::chrono::steady_clock::now();
  for (const auto& [parent_record, children] : awaiting_parent_) {
    (void)parent_record;
    for (const auto& child : children) {
      if (child.usn_at_track == 0 || current_usn <= child.usn_at_track) {
        continue;
      }
      if (current_usn - child.usn_at_track >= max_usn_gap) {
        out.push_back({child.id, child.AgeMs(now), child.usn_at_track});
      }
    }
  }
}

size_t IndexOperations::AwaitingCount() const {
  size_t total = 0;
  for (const auto& [parent_record, children] : awaiting_parent_) {
    (void)parent_record;
    total += children.size();
  }
  return total;
}

void IndexOperations::AwaitParent(ntfs_file_reference::NtfsFileReference parent_id,
                                   ntfs_file_reference::NtfsFileReference child_id,
                                   int64_t usn_at_track) {
  if (parent_id.raw == 0 || ntfs_file_reference::IsRootDirectoryRecord(parent_id.raw)) {
    return;
  }
  const auto record_number =
      ntfs_file_reference::MftRecordNumber::FromFileReference(parent_id.raw);
  auto& bucket = awaiting_parent_[record_number];
  const bool already_tracked = std::any_of(
      bucket.begin(), bucket.end(),
      [child_id](const UnresolvedReference& awaiting) { return awaiting.id == child_id.raw; });
  if (!already_tracked) {
    bucket.push_back({child_id.raw, std::chrono::steady_clock::now(), usn_at_track});
  }
}

void IndexOperations::PublishParentHealed(uint64_t child_id, uint64_t parent_id) const {
  if (on_parent_healed_) {
    on_parent_healed_(index_domain_events::ParentHealed{child_id, parent_id});
  }
}

void IndexOperations::ForgetUnresolved(ntfs_file_reference::MftRecordNumber parent_record,
                                       uint64_t child_id) {
  if (parent_record.record_number == 0) {
    return;
  }
  const auto it = awaiting_parent_.find(parent_record);
  if (it == awaiting_parent_.end()) {
    return;
  }
  auto& bucket = it->second;
  bucket.erase(std::remove_if(bucket.begin(), bucket.end(),
                              [child_id](const UnresolvedReference& awaiting) {
                                return awaiting.id == child_id;
                              }),
               bucket.end());
  if (bucket.empty()) {
    awaiting_parent_.erase(it);
  }
}

bool IndexOperations::IsDescendantOf(uint64_t entry_id, uint64_t ancestor_id) const {
  static constexpr int kMaxParentWalk = 64;
  const uint64_t ancestor_record = ntfs_file_reference::RecordNumber(ancestor_id);
  const FileEntry* entry = storage_.GetEntry(entry_id);
  if (entry == nullptr) {
    return false;
  }
  uint64_t current_parent = entry->parentID.raw;
  for (int depth = 0; depth < kMaxParentWalk; ++depth) {
    if (ntfs_file_reference::RecordNumber(current_parent) == ancestor_record) {
      return true;
    }
    if (current_parent == 0) {
      return false;
    }
    const auto [parent_entry, resolved_parent] =
        storage_.ResolveEntryReference(current_parent);
    if (parent_entry == nullptr) {
      return false;
    }
    // Self-parented root that is not the ancestor (record match checked
    // above) — the walk terminates here.
    if (ntfs_file_reference::RecordNumber(parent_entry->parentID.raw) ==
        ntfs_file_reference::RecordNumber(resolved_parent)) {
      return false;
    }
    current_parent = parent_entry->parentID.raw;
  }
  return false;
}

void IndexOperations::SyncHealedChildSubtree(std::string_view old_full_path,
                                              std::string_view new_full_path,
                                              ntfs_file_reference::NtfsFileReference child_id,
                                              bool is_directory) {
  // The child's own row always moves to the healed path.
  path_operations_.InsertPath(child_id.raw, new_full_path, is_directory);
  if (on_path_refreshed_) {
    on_path_refreshed_(child_id.raw, old_full_path, new_full_path);
  }
  if (!is_directory) {
    return;
  }
  // Never rewrite a whole volume ("C:" + sep would match every path on the drive).
  // An empty old path cannot anchor a prefix rewrite either (same guard class).
  if (old_full_path.empty() || path_utils::IsDriveLetterRootKey(old_full_path) ||
      path_utils::IsVolumeRootPath(old_full_path)) {
    // Full clear (not surgical remove/cache): descendants' cached paths are
    // stale after the move and are re-resolved on demand.
    storage_.ClearDirectoryCache();
    return;
  }
  const std::string old_prefix = std::string(old_full_path) + path_utils::kPathSeparatorStr;
  const std::string new_prefix = std::string(new_full_path) + path_utils::kPathSeparatorStr;
  // Collect first: InsertPath may reallocate PathStorage and invalidate views.
  struct SubtreeUpdate {
    uint64_t id = 0;
    std::string old_path;
    std::string path;
    bool is_directory = false;
  };
  std::vector<SubtreeUpdate> updates;
  for (const auto& [id, entry] : storage_) {
    if (id.raw == child_id.raw) {
      continue;
    }
    const std::string_view path = path_operations_.GetPathView(id.raw);
    if (path.size() < old_prefix.size() ||
        path.substr(0, old_prefix.size()) != old_prefix) {
      continue;
    }
    if (!IsDescendantOf(id.raw, child_id.raw)) {
      continue;
    }
    SubtreeUpdate update;
    update.id = id.raw;
    update.old_path = std::string(path);
    update.path = new_prefix;
    update.path.append(path.substr(old_prefix.size()));
    update.is_directory = entry.isDirectory;
    updates.push_back(std::move(update));
  }
  for (const auto& update : updates) {
    path_operations_.InsertPath(update.id, update.path, update.is_directory);
    if (on_path_refreshed_) {
      on_path_refreshed_(update.id, update.old_path, update.path);
    }
  }

  // Full clear (see above): descendants' cached paths are stale after the
  // move and are re-resolved on demand.
  storage_.ClearDirectoryCache();  // Clear to be safe for descendants
}

void IndexOperations::HealAwaiting(ntfs_file_reference::NtfsFileReference new_parent_id) {
  std::vector<ntfs_file_reference::NtfsFileReference> queue;
  queue.reserve(8);
  queue.push_back(new_parent_id);
  size_t front = 0;
  while (front < queue.size()) {
    const ntfs_file_reference::NtfsFileReference current_parent = queue[front++];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - front < size() loop invariant
    const auto record_number =
        ntfs_file_reference::MftRecordNumber::FromFileReference(current_parent.raw);
    const auto awaiting_it = awaiting_parent_.find(record_number);
    if (awaiting_it == awaiting_parent_.end()) {
      continue;
    }
    std::vector<UnresolvedReference> children;
    children.swap(awaiting_it->second);
    awaiting_parent_.erase(awaiting_it);

    const std::string parent_path(path_operations_.GetPathView(current_parent.raw));
    if (parent_path.empty()) {
      // Parent has no path (removed concurrently) — re-track children for a
      // future re-creation of this record instead of dropping them silently.
      // Re-tracking refreshes the age but preserves the original journal
      // position; the gap is a live race, not staleness.
      for (const auto& awaiting : children) {
        AwaitParent(current_parent,
                    ntfs_file_reference::NtfsFileReference(awaiting.id),
                    awaiting.usn_at_track);
      }
      continue;
    }
    for (const auto& awaiting : children) {
      const uint64_t child_id = awaiting.id;
      const FileEntry* const child_entry = storage_.GetEntry(child_id);
      if (child_entry == nullptr) {
        continue;
      }
      // Child moved away while parent was missing — leave it to its current
      // parent's bucket (tracked at Move time) instead of healing it here.
      if (ntfs_file_reference::MftRecordNumber::FromFileReference(child_entry->parentID.raw) !=
          record_number) {
        continue;
      }
      const std::string_view old_path_view = path_operations_.GetPathView(child_id);
      if (old_path_view.empty()) {
        continue;
      }
      const std::string old_path(old_path_view);
      const std::string_view leaf = path_utils::GetFilename(old_path_view);
      if (leaf.empty()) {
        continue;
      }
      // Healed: this awaiting entry leaves the population via parent arrival
      // (both the trivial already-correct path below and the subtree sync).
      // Skipped entries above (removed, moved away, pathless) are not heals.
      PublishParentHealed(child_id, current_parent.raw);
      const std::string new_path = path_utils::JoinPath(parent_path, leaf);
      // Copy before Sync: FileEntry* has no stability across map inserts.
      const bool child_is_directory = child_entry->isDirectory;
      const auto child_ref = ntfs_file_reference::NtfsFileReference(child_id);
      if (new_path == old_path) {
        queue.push_back(child_ref);
        continue;
      }
      // Scoped cascade: bare placeholders are not unique, so a global prefix
      // rewrite could mis-graft an unrelated subtree sharing the dir name.
      SyncHealedChildSubtree(old_path, new_path, child_ref, child_is_directory);
      queue.push_back(child_ref);
    }
  }
}

void IndexOperations::Insert(ntfs_file_reference::NtfsFileReference id,
                             ntfs_file_reference::NtfsFileReference parent_id,
                             std::string_view name,
                             bool is_directory,
                             FILETIME modification_time,
                              InsertOptions options,
                              uint64_t file_size) {
  // Deferral is a bulk-staging-only policy: without a following recompute the
  // entry would stay invisible to path search, and the awaiting block below
  // keys off is_placeholder, which deferral leaves false. No caller combines
  // them (bulk passes resolve_awaiting=false); fail fast in Debug if one does.
  assert((!options.defer_path_indexing || !options.resolve_awaiting) &&
         "defer_path_indexing requires resolve_awaiting=false");
  name = TruncateAtEmbeddedNull(name);
  uint64_t old_parent_record = 0;
  bool had_existing_entry = false;
  if (const FileEntry* const existing = storage_.GetEntry(id.raw); existing != nullptr) {
    had_existing_entry = true;
    old_parent_record = ntfs_file_reference::RecordNumber(existing->parentID.raw);
  }
  const uint64_t resolved_parent_id = ResolveParentIdForStorage(storage_, parent_id.raw);
  // Insert into storage (store canonical parent id when resolvable).
  // Note: this storage write precedes the path reads below but does not
  // affect them — path views come from the separate PathStorage, which is
  // only mutated by the gated InsertPath further down.
  storage_.InsertLocked(id, ntfs_file_reference::NtfsFileReference(resolved_parent_id), name,
                        is_directory, modification_time, options.register_mft_record, file_size);

  // Update path arrays (lock already held), unless the caller deferred path
  // indexing to the post-population recompute (bulk staging): the per-insert
  // join + InsertPath would be discarded by RecomputeAllPaths' Clear().
  // The entry stays invisible to path search until then; search is gated on
  // IsIndexBuilding() for the whole population window.
  bool is_placeholder = false;
  if (!options.defer_path_indexing) {
    // Compute full path:
    //   1. Parent path known: join parent + name.
    //   2. Parent is NTFS volume root (MFT record 5, usually not indexed): join the
    //      configured volume root (C:\, D:\, …) + name. Required for live USN CREATE
    //      after initial population — RecomputeAllPaths is not run again.
    //   3. resolved_parent_id == 0: true volume-root parent (tests and the
    //      non-Windows crawler use 0 for root-level entries) — join the volume
    //      root so the entry is absolute, matching PathRecomputer/PathBuilder.
    //      On Windows, drive-letter keys ("C:") are exempt: they ARE the root,
    //      so joining would corrupt them to "C:\C:"; they stay bare by design
    //      (see CheckBareNameInvariant's drive-letter exemption, fed by
    //      DirectoryResolver which inserts "C:" with parent 0). Non-Windows
    //      keeps the legacy unconditional join so Unix behavior is bit-identical
    //      (a root-level file literally named "C:" still stores as "/C:").
    //   4. Else: bare-name placeholder for out-of-order USN parents (fixed by recompute).
    const auto parent_path = path_operations_.GetPathView(resolved_parent_id);
    std::string joined_path;  // storage for JoinPath result when a join is needed
    std::string_view full_path;
    if (!parent_path.empty()) {
      joined_path = path_utils::JoinPath(parent_path, name);
      full_path = joined_path;
    } else if (ntfs_file_reference::IsRootDirectoryRecord(resolved_parent_id)
#ifdef _WIN32
               || (resolved_parent_id == 0 && !path_utils::IsDriveLetterRootKey(name))
#else
               || resolved_parent_id == 0
#endif  // _WIN32
    ) {
      // NTFS volume root (record 5, usually unindexed) or true root parent 0:
      // join the configured volume root so live USN CREATE gets a full path.
      joined_path =
          path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), name);
      full_path = joined_path;
    } else {
      full_path = name;  // USN out-of-order parent placeholder — no allocation needed
      is_placeholder = true;
    }

    // Update path arrays (lock already held)
    path_operations_.InsertPath(id.raw, full_path, is_directory);
  }

  // Live USN path: track bare-name placeholders so a later parent INSERT can
  // heal them, and heal children that arrived before this parent (with
  // ReturnOnlyOnClose the directory CLOSE record routinely arrives after the
  // files created inside it). Skipped for bulk MFT population —
  // RecomputeAllPaths resolves everything in one topological pass and
  // per-insert bookkeeping/cascades would be O(N^2). See InsertOptions for
  // the full ingest-source matrix.
  if (options.resolve_awaiting) {
    // A re-insert that now resolves must leave its old bucket (same-record
    // re-insert would otherwise keep a stale awaiting entry forever).
    const uint64_t new_parent_record = ntfs_file_reference::RecordNumber(resolved_parent_id);
    if (is_placeholder) {
      AwaitParent(ntfs_file_reference::NtfsFileReference(resolved_parent_id), id,
                  options.usn_at_track);
    } else {
      ForgetUnresolved(ntfs_file_reference::MftRecordNumber(new_parent_record), id.raw);
    }
    if (had_existing_entry && old_parent_record != new_parent_record) {
      ForgetUnresolved(ntfs_file_reference::MftRecordNumber(old_parent_record), id.raw);
    }
    HealAwaiting(id);
  }
}

void IndexOperations::Remove(ntfs_file_reference::NtfsFileReference id) {
  const FileEntry* entry = storage_.GetEntry(id.raw);
  if (entry != nullptr) {
    // Leave no stale awaiting state: drop the entry from its parent's bucket,
    // and release buckets keyed on this id (children of a deleted parent are
    // removed via their own DELETE records; holding them would leak).
    ForgetUnresolved(ntfs_file_reference::MftRecordNumber(
                         ntfs_file_reference::RecordNumber(entry->parentID.raw)),
                     id.raw);
    awaiting_parent_.erase(ntfs_file_reference::MftRecordNumber::FromFileReference(id.raw));
    // If it's a directory, remove from path cache to prevent memory leak
    if (entry->isDirectory) {
      const std::string_view path = path_operations_.GetPathView(id.raw);
      if (!path.empty()) {
        storage_.RemoveDirectoryFromCache(path);
      }
    }

    // Mark path entry as deleted first (needs entry.path_storage_index)
    if (!path_operations_.RemovePath(id.raw)) {
      if (!path_operations_.HasPath(id.raw)) {
        // Deferred bulk staging never indexed this entry's path: expected when
        // a replay-drain delete lands before the post-population recompute.
        // Not an inconsistency; keep the diagnostic metric clean.
        LOG_DEBUG_BUILD("IndexOperations::Remove: File "
                        << id.raw
                        << " staged without path index (deferred population)");
      } else {
        // File exists in storage but not in path_storage_ - data inconsistency
        remove_inconsistency_count_.fetch_add(1);
        LOG_WARNING_BUILD("IndexOperations::Remove: File "
                          << id.raw
                          << " in storage but not in path_storage_ (data inconsistency)");
      }
    }

    // Remove from storage
    storage_.RemoveLocked(id);
  } else {
    // File not in index - delete event for file that was never indexed
    // This is expected for files created/deleted before initial indexing, or
    // filtered files. Track this for diagnostics.
    remove_not_in_index_count_.fetch_add(1);
    LOG_INFO_BUILD("IndexOperations::Remove: File "
                   << id.raw
                   << " not in index (delete event for unindexed file)");
  }
}

void IndexOperations::CollectDescendantIds(ntfs_file_reference::MftRecordNumber parent_record,
                                              std::vector<uint64_t>& out) const {
  if (parent_record.record_number == 0 ||
      parent_record.record_number == ntfs_file_reference::kRootDirectoryRecordNumber) {
    return;
  }
  for (const auto& [id, entry] : storage_) {
    (void)entry;
    if (!IsDescendantOf(id.raw, parent_record.record_number)) {
      continue;
    }
    // Corrupted parentID must not evacuate drive-root children (mirrors
    // RemoveIndexedSubtree's IsVolumeRootChildPath veto).
    if (path_utils::IsVolumeRootChildPath(path_operations_.GetPathView(id.raw))) {
      continue;
    }
    out.push_back(id.raw);
  }
}

bool IndexOperations::IsAwaitingChild(ntfs_file_reference::NtfsFileReference child_id) const {
  const FileEntry* const entry = storage_.GetEntry(child_id.raw);
  if (entry == nullptr) {
    return false;
  }
  const auto it = awaiting_parent_.find(
      ntfs_file_reference::MftRecordNumber::FromFileReference(entry->parentID.raw));
  if (it == awaiting_parent_.end()) {
    return false;
  }
  const auto& bucket = it->second;
  return std::any_of(bucket.begin(), bucket.end(), [child_id](const UnresolvedReference& awaiting) {
    return awaiting.id == child_id.raw;
  });
}

bool IndexOperations::CheckBareNameInvariant() const {
  return std::all_of(storage_.begin(), storage_.end(), [this](const auto& kv) {
    const auto& [id, entry] = kv;
    (void)entry;
    const std::string_view path = path_operations_.GetPathView(id.raw);
    if (path.empty() || path.find_first_of("\\/") != std::string_view::npos) {
      return true;
    }
    // Synthetic drive-letter keys ("C:") carry no separator by design and are
    // never tracked awaiting (AwaitParent exempts id 0 / record 5).
    if (path_utils::IsDriveLetterRootKey(path)) {
      return true;
    }
    return IsAwaitingChild(ntfs_file_reference::NtfsFileReference(id));
  });
}

bool IndexOperations::Rename(ntfs_file_reference::NtfsFileReference id,
                            std::string_view new_name) {
  new_name = TruncateAtEmbeddedNull(new_name);
  const auto [entry, old_full_path] = GetEntryAndPath(storage_, path_operations_, id.raw);
  if (entry == nullptr) {
    return false;
  }

  // Store old full path for prefix matching (retrieved above)

  // Recompute full path (replace filename at end)
  std::string full_path = old_full_path;
  // Find last path separator (cross-platform)
  if (const size_t last_slash = full_path.find_last_of("\\/"); last_slash != std::string::npos) {
    full_path = full_path.substr(0, last_slash + 1);
    full_path += new_name;  // operator+= accepts string_view in C++17
  } else if (IsAwaitingChild(id) || !path_operations_.HasPath(id.raw)) {
    // Bare placeholder (out-of-order child) or deferred bulk entry whose path
    // is not indexed yet: stay bare so the entry keeps looking unresolved
    // until the parent arrives and heals it (or the post-population recompute
    // resolves it). Joining the volume root here would impersonate a real root
    // file (e.g. C:\<hex>) and slip past the bare-name invariant's
    // rooted-means-resolved assumption. Awaiting tracking is untouched, so
    // healing still applies.
    full_path = std::string(new_name);
  } else {
    // Fallback: no directory separator found, place file at volume root.
    full_path = path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), new_name);  // JoinPath accepts string_view
  }
  const std::string new_full_path = full_path;

  // Copy before mutation: FileEntry* has no stability across map inserts.
  const bool was_directory = entry->isDirectory;
  // Update entry in storage
  // Convert string_view to string when storing (ensures lifetime)
  if (const std::string new_name_str(new_name); !storage_.RenameLocked(id, new_name_str)) {
    return false;
  }

  // Scoped cascade (not a global prefix rewrite): the renamed entry may be a
  // still-bare placeholder whose prefix collides with an unrelated subtree.
  SyncHealedChildSubtree(old_full_path, new_full_path, id, was_directory);

  return true;
}

bool IndexOperations::Move(ntfs_file_reference::NtfsFileReference id,
                           ntfs_file_reference::NtfsFileReference new_parent_id) {
  const auto [entry, old_full_path] = GetEntryAndPath(storage_, path_operations_, id.raw);
  if (entry == nullptr) {
    return false;
  }

  const uint64_t resolved_parent_id =
      ResolveParentIdForStorage(storage_, new_parent_id.raw);

  // Derive current filename from PathStorage and build new full path.
  const size_t last_sep = old_full_path.find_last_of("/\\");
  std::string_view current_name = (last_sep != std::string::npos)
      ? std::string_view(old_full_path).substr(last_sep + 1)
      : std::string_view(old_full_path);
  if (current_name.empty()) {
    // Deferred bulk entry: path not indexed yet, so the leaf name lives only
    // in the name arena. Consumed synchronously by JoinPath below with no
    // intervening mutation, so the arena view is stable per its contract.
    // (Empty when the arena was already released: same as before.)
    current_name = storage_.GetNameCache().Find(id.raw);
  }
  const uint64_t old_parent_record = ntfs_file_reference::RecordNumber(entry->parentID.raw);
  const bool was_directory = entry->isDirectory;
  std::string_view new_parent_path = path_operations_.GetPathView(resolved_parent_id);
  std::string volume_root_path;
  // Volume-root fallback mirrors Insert branch 3 (parent record 5 or true
  // root parent 0; on Windows drive-letter keys stay bare);
  // otherwise JoinPath would store a bare-name placeholder that
  // AwaitParent refuses to track for id 0 (unhealable drift).
  if (new_parent_path.empty() &&
      (ntfs_file_reference::IsRootDirectoryRecord(resolved_parent_id)
#ifdef _WIN32
       || (resolved_parent_id == 0 && !path_utils::IsDriveLetterRootKey(current_name))
#else
       || resolved_parent_id == 0
#endif  // _WIN32
       )) {
    volume_root_path = path_utils::GetDefaultVolumeRootPath();
    new_parent_path = volume_root_path;
  }
  // Empty non-root parent: unknown/out-of-order target. JoinPath yields a
  // bare-name placeholder (same as Insert branch 4) — track it so a later
  // parent INSERT heals this entry instead of drifting silently.
  const bool new_parent_unresolved = new_parent_path.empty();
  const std::string new_full_path = path_utils::JoinPath(new_parent_path, current_name);

  // Update entry in storage
  if (!storage_.MoveLocked(id, ntfs_file_reference::NtfsFileReference(resolved_parent_id))) {
    return false;
  }

  const uint64_t new_parent_record = ntfs_file_reference::RecordNumber(resolved_parent_id);
  if (old_parent_record != new_parent_record) {
    ForgetUnresolved(ntfs_file_reference::MftRecordNumber(old_parent_record), id.raw);
  }
  if (new_parent_unresolved) {
    AwaitParent(ntfs_file_reference::NtfsFileReference(resolved_parent_id), id);
  } else {
    ForgetUnresolved(ntfs_file_reference::MftRecordNumber(new_parent_record), id.raw);
  }

  // Scoped cascade (not a global prefix rewrite): the moved entry may be a
  // still-bare placeholder whose prefix collides with an unrelated subtree.
  SyncHealedChildSubtree(old_full_path, new_full_path, id, was_directory);

  return true;
}

