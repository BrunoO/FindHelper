#include "path/PathOperations.h"

#include "index/LazyValue.h"
#include "path/PathUtils.h"
#include "utils/FileTimeTypes.h"
#include "utils/Logger.h"

#include <limits>

PathOperations::PathOperations(FileIndexStorage& storage, PathStorage& path_storage)
    : storage_(storage), path_storage_(path_storage) {}

void PathOperations::InsertPath(uint64_t id, std::string_view path, bool isDirectory) {  // NOLINT(readability-identifier-naming) - Public API parameter names
  const FileEntry* entry = storage_.GetEntry(id);
  std::optional<size_t> existing_index;
  if (entry != nullptr && entry->path_storage_index != kPathStorageIndexInvalid) {
    existing_index = entry->path_storage_index;
    if (*existing_index < (std::numeric_limits<uint32_t>::max)()) {
      std::string_view old_path = path_storage_.GetPathByIndex(*existing_index);
      std::string_view old_filename = path_utils::GetFilename(old_path);
      storage_.GetTrigramIndex().RemoveEntry(static_cast<uint32_t>(*existing_index), old_filename);
    } else {
      LOG_ERROR_BUILD("PathOperations::InsertPath: SoA index "
                      << *existing_index << " exceeds trigram row-id range; stale postings kept");
    }
  }
  const size_t idx = path_storage_.InsertPath(id, path, isDirectory, existing_index);
  storage_.SetPathStorageIndex(id, idx);
  if (idx < (std::numeric_limits<uint32_t>::max)()) {
    std::string_view filename = path_utils::GetFilename(path);
    storage_.GetTrigramIndex().AddEntry(static_cast<uint32_t>(idx), filename);
  } else {
    LOG_ERROR_BUILD("PathOperations::InsertPath: new SoA index "
                    << idx << " exceeds trigram row-id range; file invisible to trigram search");
  }
  // Paths are populated late by RecomputeAllPaths. Any earlier load attempt against a
  // placeholder index would have MarkFailed(); clear that once a real path exists.
  if (!path.empty()) {
    if (FileEntry* mutable_entry = storage_.GetEntryMutable(id); mutable_entry != nullptr) {
      if (mutable_entry->fileSize.IsFailed()) {
        mutable_entry->fileSize = LazyFileSize();
      }
      if (mutable_entry->lastModificationTime.IsFailed()) {
        mutable_entry->lastModificationTime = LazyFileTime(kFileTimeNotLoaded);
      }
    }
  }
}

std::string PathOperations::GetPath(uint64_t id) const {
  const FileEntry* entry = storage_.GetEntry(id);
  if (entry == nullptr || entry->path_storage_index == kPathStorageIndexInvalid) {
    return "";
  }
  const std::string_view view = path_storage_.GetPathByIndex(entry->path_storage_index);
  return std::string(view);
}

std::string_view PathOperations::GetPathView(uint64_t id) const {
  const FileEntry* entry = storage_.GetEntry(id);
  if (entry == nullptr || entry->path_storage_index == kPathStorageIndexInvalid) {
    return {};
  }
  return path_storage_.GetPathByIndex(entry->path_storage_index);
}

bool PathOperations::HasPath(uint64_t id) const {
  const FileEntry* entry = storage_.GetEntry(id);
  return entry != nullptr && entry->path_storage_index != kPathStorageIndexInvalid;
}

bool PathOperations::RemovePath(uint64_t id) {
  const FileEntry* entry = storage_.GetEntry(id);
  if (entry == nullptr || entry->path_storage_index == kPathStorageIndexInvalid) {
    return false;
  }
  if (entry->path_storage_index < (std::numeric_limits<uint32_t>::max)()) {
    std::string_view filename = path_utils::GetFilename(path_storage_.GetPathByIndex(entry->path_storage_index));
    storage_.GetTrigramIndex().RemoveEntry(static_cast<uint32_t>(entry->path_storage_index), filename);
  } else {
    LOG_ERROR_BUILD("PathOperations::RemovePath: SoA index "
                    << entry->path_storage_index << " exceeds trigram row-id range; stale postings kept");
  }
  return path_storage_.RemovePathByIndex(entry->path_storage_index);
}

PathOperations::PathComponentsView PathOperations::GetPathComponentsView(uint64_t id) const {
  const FileEntry* entry = storage_.GetEntry(id);
  if (entry == nullptr || entry->path_storage_index == kPathStorageIndexInvalid) {
    return {};
  }
  return path_storage_.GetPathComponentsByIndex(entry->path_storage_index);
}

PathOperations::PathComponentsView PathOperations::GetPathComponentsViewByIndex(size_t idx) const {
  return path_storage_.GetPathComponentsByIndex(idx);
}

void PathOperations::UpdatePrefix(std::string_view oldPrefix, std::string_view newPrefix) {  // NOLINT(readability-identifier-naming) - Public API parameter names
  // Refuse volume-wide prefixes ("C:\\", "/"): they match every path on the volume
  // and would relocate the entire index (e.g. Rename of a DirectoryResolver "C:" key).
  if (path_utils::IsVolumeWidePathPrefix(oldPrefix)) {
    return;
  }
  // Filenames are unchanged by a prefix rewrite, but a length-changing rename
  // relocates rows (tombstone + append): move each trigram posting to the new
  // row-id so the index keeps pointing at live rows. Stale row-ids are hard
  // false negatives for trigram-accelerated search (same failure as a path
  // buffer rebuild without a trigram refresh).
  path_storage_.UpdatePrefix(oldPrefix, newPrefix,
                             [this](uint64_t id, size_t old_index, size_t new_index) {
                               storage_.SetPathStorageIndex(id, new_index);
                               if (old_index >= std::numeric_limits<uint32_t>::max() ||
                                   new_index >= std::numeric_limits<uint32_t>::max()) {
                                 LOG_ERROR_BUILD("PathOperations::UpdatePrefix: row-id move "
                                                 << old_index << " -> " << new_index
                                                 << " exceeds trigram row-id range; postings not moved");
                                 return;
                               }
                               const std::string_view new_path = path_storage_.GetPathByIndex(new_index);
                               const std::string_view filename = path_utils::GetFilename(new_path);
                               if (filename.empty()) {
                                 return;
                               }
                               storage_.GetTrigramIndex().RemoveEntry(static_cast<uint32_t>(old_index), filename);
                               storage_.GetTrigramIndex().AddEntry(static_cast<uint32_t>(new_index), filename);
                             });
}

PathStorage::SoAView PathOperations::GetSearchableView() const {
  return path_storage_.GetReadOnlyView();
}


