#include "index/FileIndexStorage.h"

#include "index/LazyValue.h"
#include "utils/FileSystemUtils.h"
#include "utils/FileTimeTypes.h"
#include "utils/Logger.h"

// Insert a new entry (assumes lock is already held)
void FileIndexStorage::InsertLocked(uint64_t id, uint64_t parent_id,
                                     std::string_view name, bool is_directory,
                                     FILETIME modification_time) {
  // OPTIMIZATION: Skip file size lookup during initial indexing
  // UI will load sizes on-demand for visible rows
  const LazyFileSize file_size = is_directory ? LazyFileSize(0) : LazyFileSize();

  // Modification time handling:
  // - Directories: Use provided time (typically {0,0} for directories)
  // - Files: Use provided time if valid (e.g., from MFT), otherwise lazy-load
  //   USN record timestamps are always zero, so we can't use them
  //   MFT reading provides valid modification times that should be used
  FILETIME last_mod_time;
  if (is_directory) {
    // Directories: Use provided time (typically {0,0})
    last_mod_time = modification_time;
  } else {
    // Files: Check if modification_time is a valid (non-sentinel) time
    // If it's kFileTimeNotLoaded or {0,0}, we need to lazy-load it
    // Otherwise, it's a valid time (e.g., from MFT) and should be used
    if (IsSentinelTime(modification_time) ||
        (modification_time.dwLowDateTime == 0 && modification_time.dwHighDateTime == 0)) {
      // Sentinel or zero time - needs lazy loading
      last_mod_time = kFileTimeNotLoaded;
    } else {
      // Valid time provided (e.g., from MFT) - use it directly
      last_mod_time = modification_time;
    }
  }

  // Store name in the temporary cache for use by PathRecomputer::RecomputeAllPaths.
  name_cache_.InsertOrAssign(id, name);

  // OPTIMIZATION: Use try_emplace() instead of find()+operator[] to avoid double hash lookup
  // Aggregate init order matches FileEntry member declaration (alignment-ordered layout).
  const FileEntry new_entry{
      parent_id, file_size, LazyFileTime(last_mod_time), static_cast<size_t>(-1), is_directory};
  auto [it, inserted] = id_to_entry_.try_emplace(id, new_entry);

  // Update atomic counter if this is a new entry
  if (inserted) {
    entry_count_.fetch_add(1);
  }
}

// Remove an entry (assumes lock is already held)
void FileIndexStorage::RemoveLocked(uint64_t id) {
  auto it = id_to_entry_.find(id);
  if (it != id_to_entry_.end()) {
    // Directory cache cleanup is done by caller (IndexOperations::Remove) before calling RemoveLocked
    id_to_entry_.erase(it);
    // Update atomic counter
    entry_count_.fetch_sub(1);
  }
}

void FileIndexStorage::SetPathStorageIndex(uint64_t id, size_t index) {
  if (FileEntry* entry = GetEntryMutable(id)) {
    entry->path_storage_index = index;
  }
}

// Rename an entry (assumes lock is already held)
// Returns true if rename was successful, and fills old_full_path and new_full_path
// for caller to update PathStorage
bool FileIndexStorage::RenameLocked(uint64_t id, std::string_view new_name,
                                    [[maybe_unused]] std::string& old_full_path,
                                    [[maybe_unused]] std::string& new_full_path) {
  if (auto it = id_to_entry_.find(id); it == id_to_entry_.end()) {
    return false;
  }

  // Name cache may still be alive during USN maintenance
  name_cache_.UpdateIfPresent(id, new_name);

  // Note: old_full_path and new_full_path need to be computed by caller
  // (FileIndex) because they require path building logic which needs
  // access to both FileIndexStorage and PathStorage
  // This method just updates the entry in id_to_entry_

  return true;
}

// Move an entry (change parent) (assumes lock is already held)
// Returns true if move was successful, and fills old_full_path and new_full_path
// for caller to update PathStorage
bool FileIndexStorage::MoveLocked(uint64_t id, uint64_t new_parent_id,
                                  [[maybe_unused]] std::string& old_full_path,
                                  [[maybe_unused]] std::string& new_full_path) {
  if (auto it = id_to_entry_.find(id); it != id_to_entry_.end()) {
    // Update parent ID
    it->second.parentID = new_parent_id;

    // Note: old_full_path and new_full_path need to be computed by caller
    // (FileIndex) because they require path building logic which needs
    // access to both FileIndexStorage and PathStorage
    // This method just updates the entry in id_to_entry_

    return true;
  }
  return false;
}

// Get entry by ID (assumes lock is already held for const access)
const FileEntry* FileIndexStorage::GetEntry(uint64_t id) const {
  if (auto it = id_to_entry_.find(id); it != id_to_entry_.end()) {
    return &it->second;
  }
  return nullptr;
}

// Get entry by ID for modification (assumes lock is already held)
// Note: Method is non-const because it's used for modifications
FileEntry* FileIndexStorage::GetEntryMutable(uint64_t id) {
  if (auto it = id_to_entry_.find(id); it != id_to_entry_.end()) {
    return &it->second;
  }
  return nullptr;
}

// Update file size (assumes lock is already held)
void FileIndexStorage::UpdateFileSize(uint64_t id, uint64_t size) {
  FileEntry* entry = GetEntryMutable(id);  // NOLINT(misc-const-correctness) - Initialized from GetEntryMutable() return value; entry is modified (SetValue()), pointee must be mutable
  if (entry != nullptr) {
    entry->fileSize.SetValue(size);
  }
}

// Update modification time (assumes lock is already held)
void FileIndexStorage::UpdateModificationTime(uint64_t id, const FILETIME& time) {
  FileEntry* entry = GetEntryMutable(id);  // NOLINT(misc-const-correctness) - Initialized from GetEntryMutable() return value; entry is modified (SetValue()), pointee must be mutable
  if (entry != nullptr) {
    entry->lastModificationTime.SetValue(time);
  }
}

// GetDirectoryId is now inline in header

// Cache directory path -> ID mapping
void FileIndexStorage::CacheDirectory(std::string_view path, uint64_t id) {
  // OPTIMIZATION: Allocate string once and use try_emplace() to avoid double lookup
  // Convert string_view to string for storage (map uses std::string as key)
  std::string path_str(path);
  directory_path_to_id_.try_emplace(std::move(path_str), id);
}

// Remove directory from cache
void FileIndexStorage::RemoveDirectoryFromCache(std::string_view path) {
#ifdef FAST_LIBS_BOOST
  // Transparent lookup avoids string allocation; erase by iterator
  if (auto it = directory_path_to_id_.find(path); it != directory_path_to_id_.end()) {
    directory_path_to_id_.erase(it);
  }
#else
  const std::string path_str(path);
  directory_path_to_id_.erase(path_str);
#endif  // FAST_LIBS_BOOST
}

// Clear directory cache
void FileIndexStorage::ClearDirectoryCache() {
  directory_path_to_id_.clear();
}


// Clear all entries from the index
void FileIndexStorage::ClearLocked() {
  id_to_entry_.clear();
  directory_path_to_id_.clear();
  name_cache_.Release();
  entry_count_.store(0);
}
