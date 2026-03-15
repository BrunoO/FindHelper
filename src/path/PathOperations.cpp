#include "path/PathOperations.h"

PathOperations::PathOperations(FileIndexStorage& storage, PathStorage& path_storage)
    : storage_(storage), path_storage_(path_storage) {}

void PathOperations::InsertPath(uint64_t id, std::string_view path, bool isDirectory) {  // NOLINT(readability-identifier-naming) - Public API parameter names
  const FileEntry* entry = storage_.GetEntry(id);
  std::optional<size_t> existing_index;
  if (entry != nullptr && entry->path_storage_index != kPathStorageIndexInvalid) {
    existing_index = entry->path_storage_index;
  }
  const size_t idx = path_storage_.InsertPath(id, std::string(path), isDirectory, existing_index);
  storage_.SetPathStorageIndex(id, idx);
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
  const PathStorage::PathComponentsView storage_view =
      path_storage_.GetPathComponentsByIndex(entry->path_storage_index);
  return ConvertPathComponentsView(storage_view);
}

PathOperations::PathComponentsView PathOperations::GetPathComponentsViewByIndex(size_t idx) const {
  const PathStorage::PathComponentsView storage_view = path_storage_.GetPathComponentsByIndex(idx);
  return ConvertPathComponentsView(storage_view);
}

void PathOperations::UpdatePrefix(std::string_view oldPrefix, std::string_view newPrefix) {  // NOLINT(readability-identifier-naming) - Public API parameter names
  path_storage_.UpdatePrefix(oldPrefix, newPrefix,
                             [this](uint64_t id, size_t index) { storage_.SetPathStorageIndex(id, index); });
}

PathStorage::SoAView PathOperations::GetSearchableView() const {
  return path_storage_.GetReadOnlyView();
}

PathOperations::PathComponentsView PathOperations::ConvertPathComponentsView(
    const PathStorage::PathComponentsView& storage_view) {
  PathComponentsView result;
  result.full_path = storage_view.full_path;
  result.filename = storage_view.filename;
  result.extension = storage_view.extension;
  result.directory_path = storage_view.directory_path;
  result.has_extension = storage_view.has_extension;
  return result;
}

