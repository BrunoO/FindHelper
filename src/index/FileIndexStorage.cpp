#include "index/FileIndexStorage.h"

#include "index/LazyValue.h"
#include "utils/FileSystemUtils.h"
#include "utils/FileTimeTypes.h"
#include "utils/Logger.h"

void FileIndexStorage::InsertLocked(ntfs_file_reference::NtfsFileReference id,
                                     ntfs_file_reference::NtfsFileReference parent_id,
                                     std::string_view name, bool is_directory,
                                     FILETIME modification_time,
                                     bool register_mft_record,
                                     uint64_t file_size) {
  LazyFileSize initial_file_size;
  if (is_directory) {
    initial_file_size = LazyFileSize(0);
  } else if (file_size != kFileSizeNotLoaded && file_size != kFileSizeFailed) {
    initial_file_size = LazyFileSize(file_size);
  }

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
  name_cache_.InsertOrAssign(id.raw, name);

  // OPTIMIZATION: Use try_emplace() instead of find()+operator[] to avoid double hash lookup
  // Aggregate init order matches FileEntry member declaration (alignment-ordered layout).
  const FileEntry new_entry{
      parent_id, initial_file_size, LazyFileTime(last_mod_time), static_cast<size_t>(-1), is_directory,};
  auto [it, inserted] = id_to_entry_.try_emplace(id, new_entry);

  // USN/FRN ids only: synthetic InsertPath ids must not shadow real MFT records
  // (RecordNumber(id) == id for small sequential next_file_id_ values).
  if (register_mft_record) {
    record_number_to_id_.insert_or_assign(
        ntfs_file_reference::MftRecordNumber::FromFileReference(id.raw), id.raw);
  }

  if (inserted) {
    entry_count_.fetch_add(1);
  } else {
    // Baseline journal enumeration should not revisit the same FRN, but refresh parent linkage if it does.
    it->second.parentID = parent_id;
    it->second.isDirectory = is_directory;
    if (!is_directory && file_size != kFileSizeNotLoaded && file_size != kFileSizeFailed) {
      it->second.fileSize.SetValue(file_size);
    }
  }
}

// Remove an entry (assumes lock is already held)
void FileIndexStorage::RemoveLocked(ntfs_file_reference::NtfsFileReference id) {
  if (const auto it = id_to_entry_.find(id); it != id_to_entry_.end()) {
    // Directory cache cleanup is done by caller (IndexOperations::Remove) before calling RemoveLocked
    id_to_entry_.erase(it);
    // Update atomic counter
    entry_count_.fetch_sub(1);
    if (const auto record_it = record_number_to_id_.find(
            ntfs_file_reference::MftRecordNumber::FromFileReference(id.raw));
        record_it != record_number_to_id_.end() && record_it->second == id.raw) {
      record_number_to_id_.erase(record_it);
    }
  }
}

void FileIndexStorage::SetPathStorageIndex(uint64_t id, size_t index) {
  if (FileEntry* entry = GetEntryMutable(id)) {
    entry->path_storage_index = index;
  }
}

// Rename an entry (assumes lock is already held)
// Returns true if rename was successful
bool FileIndexStorage::RenameLocked(ntfs_file_reference::NtfsFileReference id,
                                    std::string_view new_name) {
  if (const auto it = id_to_entry_.find(id); it == id_to_entry_.end()) {
    return false;
  }

  // Name cache may still be alive during USN maintenance
  name_cache_.UpdateIfPresent(id.raw, new_name);
  return true;
}

// Move an entry (change parent) (assumes lock is already held)
// Returns true if move was successful
bool FileIndexStorage::MoveLocked(ntfs_file_reference::NtfsFileReference id,
                                  ntfs_file_reference::NtfsFileReference new_parent_id) {
  if (const auto it = id_to_entry_.find(id); it != id_to_entry_.end()) {
    // Update parent ID
    it->second.parentID = new_parent_id;
    return true;
  }
  return false;
}

// Get entry by ID (assumes lock is already held for const access).
// Keeps uint64_t by the read-path rule (exact lookup, no strip); wraps
// internally so callers never unpack.
const FileEntry* FileIndexStorage::GetEntry(uint64_t id) const {
  if (const auto it = id_to_entry_.find(ntfs_file_reference::NtfsFileReference(id));
      it != id_to_entry_.end()) {
    return &it->second;
  }
  return nullptr;
}

// Get entry by ID for modification (assumes lock is already held)
// Note: Method is non-const because it's used for modifications
FileEntry* FileIndexStorage::GetEntryMutable(uint64_t id) {
  if (const auto it = id_to_entry_.find(ntfs_file_reference::NtfsFileReference(id));
      it != id_to_entry_.end()) {
    return &it->second;
  }
  return nullptr;
}

std::pair<const FileEntry*, uint64_t> FileIndexStorage::ResolveEntryReference(
    uint64_t file_reference) const {
  if (file_reference == 0) {
    return {nullptr, 0};
  }
  // Prefer the canonical indexed id for this MFT record number. ParentFileReferenceNumber
  // often carries a stale sequence in the upper 16 bits; an exact FRN match can bind to a
  // different live file that happens to share that stale key.
  if (const auto record_it = record_number_to_id_.find(
          ntfs_file_reference::MftRecordNumber::FromFileReference(file_reference));
      record_it != record_number_to_id_.end()) {
    if (const FileEntry* entry = GetEntry(record_it->second); entry != nullptr) {
      return {entry, record_it->second};
    }
  }
  if (const FileEntry* entry = GetEntry(file_reference); entry != nullptr) {
    return {entry, file_reference};
  }
  return {nullptr, 0};
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
  record_number_to_id_.clear();
  directory_path_to_id_.clear();
  name_cache_.Release();
  entry_count_.store(0);
}
