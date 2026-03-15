#include "crawler/IndexOperations.h"

#include "utils/FileTimeTypes.h"
#include "utils/Logger.h"
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
                            FILETIME modification_time) {
  // Compute full path. Three cases:
  //   1. Parent path known (parent already inserted): join parent + name.
  //   2. parent_id == 0 (true root parent, returned by DirectoryResolver for
  //      an empty parent path on non-Windows): prepend "/" so root-level dirs
  //      get "/home" instead of bare "home". Without this, stat() fails and
  //      RecomputeAllPaths must do an O(N×depth) parent-chain walk to fix all
  //      paths. On Windows this branch is skipped: drive letters like "C:" ARE
  //      the root (not children of it), so JoinPath("C:\\","C:") = "C:\\C:" is
  //      wrong. Children of "C:" build correct paths because JoinPath("C:","Users")
  //      = "C:\\Users", so the Windows crawler path does not need this fix.
  //   3. parent_id != 0 but path not yet in PathStorage (USN journal out-of-order
  //      insertion or Windows root entry): store just the name as a placeholder;
  //      RecomputeAllPaths will rebuild the correct full path once all entries
  //      are present.
  const auto parent_path = path_operations_.GetPathView(parent_id);
  std::string full_path;
  if (!parent_path.empty()) {
    full_path = path_utils::JoinPath(parent_path, name);
#ifndef _WIN32
  } else if (parent_id == 0) {
    // Non-Windows root-level entry: prepend "/" to get "/home" not "home".
    full_path = path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), name);
#endif  // _WIN32
  } else {
    full_path = std::string(name);  // USN out-of-order / Windows root placeholder
  }

  // Extract extension for interning
  std::string extension;
  if (!is_directory) {
    extension = GetExtension(name);
  }

  // Insert into storage (handles extension interning and size tracking)
  storage_.InsertLocked(id, parent_id, name, is_directory, modification_time, extension);

  // Update path arrays (lock already held)
  path_operations_.InsertPath(id, full_path, is_directory);
}

void IndexOperations::Remove(uint64_t id) {
  const FileEntry* entry = storage_.GetEntry(id);
  if (entry != nullptr) {
    // If it's a directory, remove from path cache to prevent memory leak
    if (entry->isDirectory) {
      const std::string path = path_operations_.GetPath(id);
      if (!path.empty()) {
        storage_.RemoveDirectoryFromCache(path);
      }
    }

    // Remove from storage
    storage_.RemoveLocked(id);

    // Mark path entry as deleted (lock already held)
    if (path_operations_.RemovePath(id)) {
      // Successfully marked as deleted
    } else {
      // File exists in storage but not in path_storage_ - data inconsistency
      // Track this for diagnostics
      remove_inconsistency_count_.fetch_add(1, std::memory_order_relaxed);
      LOG_WARNING_BUILD("IndexOperations::Remove: File "
                       << id
                       << " in storage but not in path_storage_ (data inconsistency)");
    }
  } else {
    // File not in index - delete event for file that was never indexed
    // This is expected for files created/deleted before initial indexing, or
    // filtered files. Track this for diagnostics.
    remove_not_in_index_count_.fetch_add(1, std::memory_order_relaxed);
    LOG_INFO_BUILD("IndexOperations::Remove: File "
                   << id
                   << " not in index (delete event for unindexed file)");
  }
}

bool IndexOperations::Rename(uint64_t id, std::string_view new_name) {
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

  // Update entry in storage (handles extension interning)
  // Convert string_view to string when storing (ensures lifetime)
  const std::string new_name_str(new_name);
  std::string old_path_dummy;
  std::string new_path_dummy;
  if (!storage_.RenameLocked(id, new_name_str, old_path_dummy, new_path_dummy)) {  // NOSONAR(cpp:S6004) - Output parameter, must be declared before function call
    return false;
  }

  // If directory, update all descendants
  if (entry->isDirectory) {
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

  // Derive current filename from PathStorage and build new full path.
  const size_t last_sep = old_full_path.find_last_of("/\\");
  const std::string_view current_name = (last_sep != std::string::npos)
      ? std::string_view(old_full_path).substr(last_sep + 1)
      : std::string_view(old_full_path);
  const std::string new_parent_path = path_operations_.GetPath(new_parent_id);
  const std::string new_full_path = path_utils::JoinPath(new_parent_path, current_name);

  // Update entry in storage
  std::string old_path_dummy;
  std::string new_path_dummy;
  if (!storage_.MoveLocked(id, new_parent_id, old_path_dummy, new_path_dummy)) {  // NOSONAR(cpp:S6004) - Output parameter, must be declared before function call
    return false;
  }

  if (entry->isDirectory) {
    // Update all descendants
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

