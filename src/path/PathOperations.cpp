#include "path/PathOperations.h"

#include "index/LazyValue.h"
#include "path/PathUtils.h"
#include "utils/FileTimeTypes.h"

PathOperations::PathOperations(FileIndexStorage& storage, PathStorage& path_storage)
    : storage_(storage), path_storage_(path_storage) {}

void PathOperations::InsertPath(uint64_t id, std::string_view path, bool isDirectory) {  // NOLINT(readability-identifier-naming) - Public API parameter names
  const FileEntry* entry = storage_.GetEntry(id);
  std::optional<size_t> existing_index;
  if (entry != nullptr && entry->path_storage_index != kPathStorageIndexInvalid) {
    existing_index = entry->path_storage_index;
  }
  const size_t idx = path_storage_.InsertPath(id, path, isDirectory, existing_index);
  storage_.SetPathStorageIndex(id, idx);
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

bool PathOperations::RemovePath(uint64_t id) {
  const FileEntry* entry = storage_.GetEntry(id);
  if (entry == nullptr || entry->path_storage_index == kPathStorageIndexInvalid) {
    return false;
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
  path_storage_.UpdatePrefix(oldPrefix, newPrefix,
                             [this](uint64_t id, size_t index) { storage_.SetPathStorageIndex(id, index); });
}

PathStorage::SoAView PathOperations::GetSearchableView() const {
  return path_storage_.GetReadOnlyView();
}


