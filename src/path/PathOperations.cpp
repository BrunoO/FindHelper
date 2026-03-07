#include "path/PathOperations.h"

PathOperations::PathOperations(PathStorage& path_storage)
    : path_storage_(path_storage) {}

// InsertPath, GetPath, GetPathView, and RemovePath are now inline in header

PathOperations::PathComponentsView PathOperations::GetPathComponentsView(uint64_t id) const {
  const PathStorage::PathComponentsView storage_view = path_storage_.GetPathComponents(id);
  return ConvertPathComponentsView(storage_view);
}

PathOperations::PathComponentsView PathOperations::GetPathComponentsViewByIndex(size_t idx) const {
  const PathStorage::PathComponentsView storage_view = path_storage_.GetPathComponentsByIndex(idx);
  return ConvertPathComponentsView(storage_view);
}

void PathOperations::UpdatePrefix(std::string_view oldPrefix, std::string_view newPrefix) {  // NOLINT(readability-identifier-naming) - Public API parameter names
  path_storage_.UpdatePrefix(oldPrefix, newPrefix);
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

// RemovePath is now inline in header

