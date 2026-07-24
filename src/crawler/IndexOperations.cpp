#include "crawler/IndexOperations.h"

#include "index/NtfsFileReference.h"
#include "path/PathUtils.h"
#include "utils/FileTimeTypes.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"
#include <cstdint>

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
                                 std::atomic<size_t>& remove_inconsistency_count)
    : storage_(storage),
      path_operations_(path_operations),
      remove_not_in_index_count_(remove_not_in_index_count),
      remove_inconsistency_count_(remove_inconsistency_count) {}

void IndexOperations::Insert(uint64_t id,
                            uint64_t parent_id,
                            std::string_view name,
                            bool is_directory,
                            FILETIME modification_time,
                            bool register_mft_record) {
  name = TruncateAtEmbeddedNull(name);
  const uint64_t resolved_parent_id = ResolveParentIdForStorage(storage_, parent_id);
  // Compute full path:
  //   1. Parent path known: join parent + name.
  //   2. Parent is NTFS volume root (MFT record 5, usually not indexed): join the
  //      configured volume root (C:\, D:\, …) + name. Required for live USN CREATE
  //      after initial population — RecomputeAllPaths is not run again.
  //   3. resolved_parent_id == 0 on non-Windows: prepend "/" for root-level dirs.
  //   4. Else: bare-name placeholder for out-of-order USN parents (fixed by recompute).
  const auto parent_path = path_operations_.GetPathView(resolved_parent_id);
  std::string joined_path;  // storage for JoinPath result when a join is needed
  std::string_view full_path;
  if (!parent_path.empty()) {
    joined_path = path_utils::JoinPath(parent_path, name);
    full_path = joined_path;
  } else if (ntfs_file_reference::IsRootDirectoryRecord(resolved_parent_id)
#ifndef _WIN32
             || resolved_parent_id == 0
#endif  // _WIN32
  ) {
    // NTFS volume root (record 5, usually unindexed) or non-Windows parent 0:
    // join the configured volume root so live USN CREATE gets a full path.
    joined_path =
        path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), name);
    full_path = joined_path;
  } else {
    full_path = name;  // USN out-of-order parent placeholder — no allocation needed
  }

  // Insert into storage (store canonical parent id when resolvable).
  storage_.InsertLocked(id, resolved_parent_id, name, is_directory, modification_time,
                        register_mft_record);

  // Update path arrays (lock already held)
  path_operations_.InsertPath(id, full_path, is_directory);
}

void IndexOperations::Remove(uint64_t id) {
  const FileEntry* entry = storage_.GetEntry(id);
  if (entry != nullptr) {
    // If it's a directory, remove from path cache to prevent memory leak
    if (entry->isDirectory) {
      const std::string_view path = path_operations_.GetPathView(id);
      if (!path.empty()) {
        storage_.RemoveDirectoryFromCache(path);
      }
    }

    // Mark path entry as deleted first (needs entry.path_storage_index)
    if (!path_operations_.RemovePath(id)) {
      // File exists in storage but not in path_storage_ - data inconsistency
      remove_inconsistency_count_.fetch_add(1);
      LOG_WARNING_BUILD("IndexOperations::Remove: File "
                       << id
                       << " in storage but not in path_storage_ (data inconsistency)");
    }

    // Remove from storage
    storage_.RemoveLocked(id);
  } else {
    // File not in index - delete event for file that was never indexed
    // This is expected for files created/deleted before initial indexing, or
    // filtered files. Track this for diagnostics.
    remove_not_in_index_count_.fetch_add(1);
    LOG_INFO_BUILD("IndexOperations::Remove: File "
                   << id
                   << " not in index (delete event for unindexed file)");
  }
}

bool IndexOperations::Rename(uint64_t id, std::string_view new_name) {
  new_name = TruncateAtEmbeddedNull(new_name);
  const auto [entry, old_full_path] = GetEntryAndPath(storage_, path_operations_, id);
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
  } else {
    // Fallback: no directory separator found, place file at volume root.
    full_path = path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), new_name);  // JoinPath accepts string_view
  }
  const std::string new_full_path = full_path;

  // Update entry in storage
  // Convert string_view to string when storing (ensures lifetime)
  const std::string new_name_str(new_name);
  std::string old_path_dummy;
  std::string new_path_dummy;
  if (!storage_.RenameLocked(id, new_name_str, old_path_dummy, new_path_dummy)) {  // NOSONAR(cpp:S6004) - Output parameter, must be declared before function call
    return false;
  }

  // If directory, update all descendants (never when old path is a volume-root key:
  // "C:" + sep → "C:\\" would rewrite every path on the volume).
  if (entry->isDirectory && !path_utils::IsDriveLetterRootKey(old_full_path) &&
      !path_utils::IsVolumeRootPath(old_full_path)) {
    const std::string old_prefix = old_full_path + path_utils::kPathSeparatorStr;
    const std::string new_prefix = new_full_path + path_utils::kPathSeparatorStr;
    path_operations_.UpdatePrefix(old_prefix, new_prefix);

    // Update entry in directory cache if it exists
    storage_.RemoveDirectoryFromCache(old_full_path);
    storage_.CacheDirectory(new_full_path, id);
    storage_.ClearDirectoryCache(); // Clear to be safe for descendants
  }

  // Update path arrays (lock already held)
  path_operations_.InsertPath(id, new_full_path, entry->isDirectory);

  return true;
}

bool IndexOperations::Move(uint64_t id, uint64_t new_parent_id) {
  const auto [entry, old_full_path] = GetEntryAndPath(storage_, path_operations_, id);
  if (entry == nullptr) {
    return false;
  }

  const uint64_t resolved_parent_id =
      ResolveParentIdForStorage(storage_, new_parent_id);

  // Derive current filename from PathStorage and build new full path.
  const size_t last_sep = old_full_path.find_last_of("/\\");
  const std::string_view current_name = (last_sep != std::string::npos)
      ? std::string_view(old_full_path).substr(last_sep + 1)
      : std::string_view(old_full_path);
  std::string_view new_parent_path = path_operations_.GetPathView(resolved_parent_id);
  std::string volume_root_path;
  if (new_parent_path.empty() &&
      ntfs_file_reference::IsRootDirectoryRecord(resolved_parent_id)) {
    volume_root_path = path_utils::GetDefaultVolumeRootPath();
    new_parent_path = volume_root_path;
  }
  const std::string new_full_path = path_utils::JoinPath(new_parent_path, current_name);

  // Update entry in storage
  std::string old_path_dummy;
  std::string new_path_dummy;
  if (!storage_.MoveLocked(id, resolved_parent_id, old_path_dummy, new_path_dummy)) {  // NOSONAR(cpp:S6004) - Output parameter, must be declared before function call
    return false;
  }

  if (entry->isDirectory && !path_utils::IsDriveLetterRootKey(old_full_path) &&
      !path_utils::IsVolumeRootPath(old_full_path)) {
    // Update all descendants (skip volume-root keys — see Rename).
    const std::string old_prefix = old_full_path + path_utils::kPathSeparatorStr;
    const std::string new_prefix = new_full_path + path_utils::kPathSeparatorStr;
    path_operations_.UpdatePrefix(old_prefix, new_prefix);

    // Sync directory cache
    storage_.RemoveDirectoryFromCache(old_full_path);
    storage_.CacheDirectory(new_full_path, id);
    storage_.ClearDirectoryCache(); // Clear to be safe for descendants
  }

  // Update path arrays (lock already held)
  path_operations_.InsertPath(id, new_full_path, entry->isDirectory);

  return true;
}

