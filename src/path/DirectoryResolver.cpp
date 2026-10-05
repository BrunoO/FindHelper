#include "path/DirectoryResolver.h"

#include "path/PathUtils.h"
#include "utils/FileTimeTypes.h"
#include <cstdint>
#include <vector>

DirectoryResolver::DirectoryResolver(FileIndexStorage& storage,
                                     IndexOperations& operations,
                                     std::atomic<uint64_t>& next_file_id)
    : storage_(storage), operations_(operations), next_file_id_(next_file_id) {}

uint64_t DirectoryResolver::GetOrCreateDirectoryId(std::string_view path) {
  // Index keys use "C:" / empty root, not "C:\" / "/". Trim so display-form
  // volume roots from GetDirectoryPathView never create an empty-named child.
  path = path_utils::TrimTrailingSeparators(path);
  if (path.empty()) {
    return 0; // Root directory
  }

  // Check cache first
  if (const uint64_t cached_id = storage_.GetDirectoryId(path); cached_id != 0) {
    return cached_id;
  }

  // Collect all missing directory segments from the leaf up to the nearest
  // existing ancestor (or root), then create them iteratively. The probe
  // above already ruled out path itself, so the walk starts at its parent
  // instead of looking the same path up twice.
  std::vector<std::string_view> paths_to_create;
  std::vector<std::string_view> names_to_create;
  paths_to_create.reserve(8);
  names_to_create.reserve(8);

  std::string_view first_parent_path;
  std::string_view first_dir_name;
  ParseDirectoryPath(path, first_parent_path, first_dir_name);
  paths_to_create.push_back(path);
  names_to_create.push_back(first_dir_name);

  std::string_view current_path = first_parent_path;
  uint64_t parent_id = 0;
  while (!current_path.empty()) {
    if (const uint64_t existing_id = storage_.GetDirectoryId(current_path); existing_id != 0) {
      parent_id = existing_id;
      break;
    }

    std::string_view parent_path;
    std::string_view dir_name;
    ParseDirectoryPath(current_path, parent_path, dir_name);
    paths_to_create.push_back(current_path);
    names_to_create.push_back(dir_name);
    current_path = parent_path;
  }

  for (std::size_t i = paths_to_create.size(); i > 0; --i) {
    const std::string_view full_path = paths_to_create[i - 1];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - loop-guarded: i decrements from size() to 1, so i-1 is always valid
    const std::string_view dir_name = names_to_create[i - 1];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - loop-guarded: i decrements from size() to 1, so i-1 is always valid
    const uint64_t dir_id = next_file_id_.fetch_add(1);
    // Synthetic resolver ids (not FRNs): wrapped explicitly — the wrapper
    // is non-validating, so behavior is unchanged; the type names the shape.
    operations_.Insert(ntfs_file_reference::NtfsFileReference(dir_id),
                       ntfs_file_reference::NtfsFileReference(parent_id), dir_name, true,
                       kFileTimeNotLoaded,
                       IndexOperations::InsertOptions{/*register_mft_record=*/false});
    storage_.CacheDirectory(full_path, dir_id);
    parent_id = dir_id;
  }

  return parent_id;
}

// ParseDirectoryPath is now inline in header

