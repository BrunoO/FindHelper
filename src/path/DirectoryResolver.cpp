#include "path/DirectoryResolver.h"

#include "utils/FileTimeTypes.h"
#include <cstdint>
#include <vector>

DirectoryResolver::DirectoryResolver(FileIndexStorage& storage,
                                     IndexOperations& operations,
                                     std::atomic<uint64_t>& next_file_id)
    : storage_(storage), operations_(operations), next_file_id_(next_file_id) {}

uint64_t DirectoryResolver::GetOrCreateDirectoryId(std::string_view path) {
  if (path.empty()) {
    return 0; // Root directory
  }

  // Check cache first
  if (const uint64_t cached_id = storage_.GetDirectoryId(path); cached_id != 0) {
    return cached_id;
  }

  // Collect all missing directory segments from the leaf up to the nearest
  // existing ancestor (or root), then create them iteratively.
  std::vector<std::string> paths_to_create;
  std::vector<std::string> names_to_create;

  std::string current_path(path);
  while (!current_path.empty()) {
    if (const uint64_t existing_id = storage_.GetDirectoryId(current_path); existing_id != 0) {
      uint64_t parent_id = existing_id;
      for (std::size_t i = paths_to_create.size(); i > 0; --i) {
        const std::string& full_path = paths_to_create[i - 1];
        const std::string& dir_name = names_to_create[i - 1];
        const uint64_t dir_id = next_file_id_.fetch_add(1, std::memory_order_relaxed);
        operations_.Insert(dir_id, parent_id, dir_name, true, kFileTimeNotLoaded);
        storage_.CacheDirectory(full_path, dir_id);
        parent_id = dir_id;
      }
      return parent_id;
    }

    std::string parent_path;
    std::string dir_name;
    ParseDirectoryPath(current_path, parent_path, dir_name);
    paths_to_create.emplace_back(current_path);
    names_to_create.emplace_back(dir_name);
    current_path = parent_path;
  }

  uint64_t parent_id = 0;
  for (std::size_t i = paths_to_create.size(); i > 0; --i) {
    const std::string& full_path = paths_to_create[i - 1];
    const std::string& dir_name = names_to_create[i - 1];
    const uint64_t dir_id = next_file_id_.fetch_add(1, std::memory_order_relaxed);
    operations_.Insert(dir_id, parent_id, dir_name, true, kFileTimeNotLoaded);
    storage_.CacheDirectory(full_path, dir_id);
    parent_id = dir_id;
  }

  return parent_id;
}

// ParseDirectoryPath is now inline in header

