#pragma once

#include "path/PathStorage.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

/**
 * @file PathOperations.h
 * @brief Encapsulates path-related operations, wrapping PathStorage with a higher-level API
 *
 * This class provides a clean interface for path operations, handling type conversions
 * and providing a consistent API for FileIndex and other components. It wraps PathStorage
 * and provides methods that match FileIndex's public interface.
 *
 * DESIGN:
 * - Takes reference to PathStorage (doesn't own it)
 * - Provides path access methods (GetPath, GetPathView, GetPathComponentsView)
 * - Provides path modification methods (InsertPath, UpdatePrefix)
 * - Handles type conversions between PathStorage and FileIndex types
 * - All methods assume lock is already held (caller responsible for locking)
 *
 * THREAD SAFETY:
 * - All methods assume caller holds appropriate lock on mutex
 * - No internal locking (lock is managed by FileIndex)
 */
class PathOperations {
public:
  /**
   * @brief Path components view (canonical type; FileIndex uses alias to this)
   *
   * Provides zero-copy access to path components for efficient search operations.
   */
  struct PathComponentsView {
    std::string_view full_path{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
    std::string_view filename{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
    std::string_view extension{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
    std::string_view directory_path{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
    bool has_extension = false;
  };

  /**
   * @brief Construct PathOperations
   *
   * @param path_storage Reference to PathStorage
   */
  explicit PathOperations(PathStorage& path_storage);

  // Default destructor (holds reference, no cleanup needed)
  ~PathOperations() = default;

  // Delete copy and move (holds reference)
  PathOperations(const PathOperations&) = delete;
  PathOperations& operator=(const PathOperations&) = delete;
  PathOperations(PathOperations&&) = delete;
  PathOperations& operator=(PathOperations&&) = delete;

  /**
   * @brief Insert or update a path entry
   *
   * @param id File ID
   * @param path Full path string (string_view for flexibility)
   * @param isDirectory True if this is a directory
   *
   * @pre Caller must hold unique_lock on mutex
   */
  inline void InsertPath(uint64_t id, std::string_view path, bool isDirectory) {  // NOLINT(readability-identifier-naming) - Public API parameter names
    // Convert string_view to string for storage (PathStorage stores as std::string)
    path_storage_.InsertPath(id, std::string(path), isDirectory);
  }

  /**
   * @brief Get full path as string
   *
   * @param id File ID
   * @return Full path string (empty if not found)
   *
   * @pre Caller must hold shared_lock or unique_lock on mutex
   */
  [[nodiscard]] inline std::string GetPath(uint64_t id) const {
    return path_storage_.GetPath(id);
  }

  /**
   * @brief Get full path as string_view (zero-copy)
   *
   * @param id File ID
   * @return Full path view (empty if not found)
   *
   * @pre Caller must hold shared_lock or unique_lock on mutex
   */
  [[nodiscard]] inline std::string_view GetPathView(uint64_t id) const {
    return path_storage_.GetPathView(id);
  }

  /**
   * @brief Get path components as views (zero-copy)
   *
   * @param id File ID
   * @return PathComponentsView with all path components
   *
   * @pre Caller must hold shared_lock or unique_lock on mutex
   */
  [[nodiscard]] PathComponentsView GetPathComponentsView(uint64_t id) const;

  /**
   * @brief Get path components by array index (for search loops)
   *
   * @param idx Array index (not file ID)
   * @return PathComponentsView with all path components
   *
   * @pre Caller must hold shared_lock or unique_lock on mutex
   */
  [[nodiscard]] PathComponentsView GetPathComponentsViewByIndex(size_t idx) const;

  /**
   * @brief Update prefix for all descendant paths
   *
   * Used when renaming or moving directories to update all descendant paths.
   *
   * @param oldPrefix Old path prefix
   * @param newPrefix New path prefix
   *
   * @pre Caller must hold unique_lock on mutex
   */
  void UpdatePrefix(std::string_view oldPrefix, std::string_view newPrefix);  // NOLINT(readability-identifier-naming) - Public API parameter names

  /**
   * @brief Get read-only view of SoA arrays for search operations
   *
   * @return SoAView providing zero-copy access to path arrays
   *
   * @pre Caller must hold shared_lock or unique_lock on mutex
   */
  [[nodiscard]] PathStorage::SoAView GetSearchableView() const;

  /**
   * @brief Mark a path entry as deleted (tombstone)
   *
   * @param id File ID to mark as deleted
   * @return true if the entry was found and marked, false if not found
   *
   * @pre Caller must hold unique_lock on mutex
   */
  [[nodiscard]] inline bool RemovePath(uint64_t id) {
    return path_storage_.RemovePath(id);
  }

private:
  /**
   * @brief Convert PathStorage::PathComponentsView to PathOperations::PathComponentsView
   *
   * @param storage_view Source view from PathStorage
   * @return Converted view
   */
  static PathComponentsView ConvertPathComponentsView(
      const PathStorage::PathComponentsView& storage_view);

  PathStorage& path_storage_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
};

