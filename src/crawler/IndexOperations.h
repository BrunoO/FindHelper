#pragma once

#include <atomic>  // NOLINT(clang-diagnostic-error) - System header, unavoidable on macOS (header-only analysis limitation)
#include <cstdint>
#include <string>

#include "index/FileIndexStorage.h"
#include "path/PathOperations.h"
#include "path/PathUtils.h"
#include "utils/FileTimeTypes.h"
#include "utils/StringUtils.h"

/**
 * @file IndexOperations.h
 * @brief Encapsulates coordination logic for index operations (Insert, Remove, Rename, Move)
 *
 * This class handles the coordination between FileIndexStorage and PathStorage
 * for index operations. It encapsulates the business logic that was previously
 * in FileIndex, making FileIndex a cleaner facade.
 *
 * DESIGN:
 * - Takes references to necessary components (storage, path_operations)
 * - Provides clean interface: Insert, Remove, Rename, Move
 * - All methods assume lock is already held (caller responsible for locking)
 * - Handles extension extraction, path computation, directory cache updates
 * - Tracks statistics (remove counts) via atomic references
 *
 * THREAD SAFETY:
 * - All methods assume caller holds unique_lock on mutex
 * - No internal locking (lock is managed by FileIndex)
 * - Statistics updates use atomic operations
 */
class IndexOperations {
public:
  /**
   * @brief Construct IndexOperations
   *
   * @param storage Reference to FileIndexStorage
   * @param path_operations Reference to PathOperations
   * @param remove_not_in_index_count Reference to atomic counter for diagnostics
   * @param remove_inconsistency_count Reference to atomic counter for diagnostics
   */
  IndexOperations(FileIndexStorage& storage,
                 PathOperations& path_operations,
                 std::atomic<size_t>& remove_not_in_index_count,
                 std::atomic<size_t>& remove_inconsistency_count);

  // Default destructor (holds references, no cleanup needed)
  ~IndexOperations() = default;
  
  // Delete copy and move (holds references)
  IndexOperations(const IndexOperations&) = delete;
  IndexOperations& operator=(const IndexOperations&) = delete;
  IndexOperations(IndexOperations&&) = delete;
  IndexOperations& operator=(IndexOperations&&) = delete;

  /**
   * @brief Insert or update a file entry
   *
   * Coordinates between FileIndexStorage and PathStorage:
   * 1. Computes full path by walking parent chain
   * 2. Extracts extension for interning
   * 3. Inserts into storage
   * 4. Updates path arrays
   *
   * @param id File ID
   * @param parentID Parent directory ID
   * @param name File name
   * @param isDirectory Whether this is a directory
   * @param modificationTime File modification time
   *
   * @pre Caller must hold unique_lock on mutex
   */
  void Insert(uint64_t id,
              uint64_t parent_id,
              std::string_view name,
              bool is_directory = false,
              FILETIME modification_time = {UINT32_MAX, UINT32_MAX});

  /**
   * @brief Remove a file entry
   *
   * Coordinates between FileIndexStorage and PathStorage:
   * 1. Checks if entry exists
   * 2. Removes from directory cache if directory
   * 3. Removes from storage
   * 4. Marks path entry as deleted
   * 5. Tracks statistics for diagnostics
   *
   * @param id File ID to remove
   *
   * @pre Caller must hold unique_lock on mutex
   */
  void Remove(uint64_t id);

  /**
   * @brief Rename a file (change name only)
   *
   * Coordinates between FileIndexStorage and PathStorage:
   * 1. Retrieves old full path
   * 2. Computes new full path
   * 3. Updates entry in storage
   * 4. Updates all descendant paths if directory
   * 5. Updates directory cache
   * 6. Updates path arrays
   *
   * @param id File ID
   * @param newName New file name
   * @return true if rename succeeded, false if file not found
   *
   * @pre Caller must hold unique_lock on mutex
   */
  bool Rename(uint64_t id, std::string_view new_name);

  /**
   * @brief Move a file (change parent only)
   *
   * Coordinates between FileIndexStorage and PathStorage:
   * 1. Retrieves old full path
   * 2. Computes new full path
   * 3. Updates entry in storage
   * 4. Updates all descendant paths if directory
   * 5. Updates directory cache
   * 6. Updates path arrays
   *
   * @param id File ID
   * @param newParentID New parent directory ID
   * @return true if move succeeded, false if file not found
   *
   * @pre Caller must hold unique_lock on mutex
   */
  bool Move(uint64_t id, uint64_t new_parent_id);

private:
  FileIndexStorage& storage_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  PathOperations& path_operations_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::atomic<size_t>& remove_not_in_index_count_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::atomic<size_t>& remove_inconsistency_count_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
};

