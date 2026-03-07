#pragma once

#include "index/FileIndexStorage.h"
#include "crawler/IndexOperations.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

/**
 * @file DirectoryResolver.h
 * @brief Encapsulates directory ID resolution and creation logic
 *
 * This class handles the recursive creation of directory hierarchies and
 * coordinates with FileIndexStorage for caching. It resolves directory IDs
 * from paths, creating directories recursively if they don't exist.
 *
 * DESIGN:
 * - Takes references to necessary components (storage, operations, next_file_id)
 * - Provides clean interface: GetOrCreateDirectoryId(path)
 * - All methods assume lock is already held (caller responsible for locking)
 * - Handles recursive directory creation
 * - Coordinates with FileIndexStorage for caching
 *
 * THREAD SAFETY:
 * - All methods assume caller holds unique_lock on mutex
 * - No internal locking (lock is managed by FileIndex)
 */
class DirectoryResolver {
public:
  /**
   * @brief Construct DirectoryResolver
   *
   * @param storage Reference to FileIndexStorage (for cache operations)
   * @param operations Reference to IndexOperations (for directory insertion)
   * @param next_file_id Atomic counter for ID generation (owned by FileIndex)
   */
  DirectoryResolver(FileIndexStorage& storage,
                    IndexOperations& operations,
                    std::atomic<uint64_t>& next_file_id);

  // Default destructor (holds references, no cleanup needed)
  ~DirectoryResolver() = default;
  
  // Delete copy and move (holds references)
  DirectoryResolver(const DirectoryResolver&) = delete;
  DirectoryResolver& operator=(const DirectoryResolver&) = delete;
  DirectoryResolver(DirectoryResolver&&) = delete;
  DirectoryResolver& operator=(DirectoryResolver&&) = delete;

  /**
   * @brief Get or create directory ID for a given path
   *
   * Recursively creates parent directories if they don't exist.
   * Uses cache for fast lookup of existing directories.
   *
   * @param path Directory path (e.g., "C:\\Users\\John\\Documents")
   * @return Directory ID (0 for root, >0 for created/cached directory)
   *
   * @pre Caller must hold unique_lock on mutex
   */
  uint64_t GetOrCreateDirectoryId(std::string_view path);

private:
  /**
   * @brief Parse path into parent path and directory name
   *
   * @param path Full directory path
   * @param out_parent_path Output: parent path (empty if root)
   * @param out_dir_name Output: directory name
   */
  inline void ParseDirectoryPath(std::string_view path,
                                 std::string& out_parent_path,
                                 std::string& out_dir_name) const {
    const size_t last_slash = path.find_last_of("\\/");
    if (last_slash != std::string_view::npos) {
      out_parent_path = path.substr(0, last_slash);
      out_dir_name = path.substr(last_slash + 1);
    } else {
      // No directory separator, file is at root
      out_parent_path.clear();
      out_dir_name = path;
    }
  }

  // NOLINTNEXTLINE(readability-identifier-naming) - project convention: snake_case_ for members
  FileIndexStorage& storage_;
  // NOLINTNEXTLINE(readability-identifier-naming)
  IndexOperations& operations_;
  // NOLINTNEXTLINE(readability-identifier-naming)
  std::atomic<uint64_t>& next_file_id_;
};

