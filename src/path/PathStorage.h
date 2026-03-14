#pragma once

#include "utils/HashMapAliases.h"
#include "utils/Logger.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file PathStorage.h
 * @brief Manages Structure of Arrays (SoA) for high-performance parallel search
 *
 * PathStorage encapsulates the SoA layout used for cache-efficient parallel
 * searching. This design maintains excellent cache locality by storing all paths
 * in a contiguous buffer, enabling searches across 1M+ paths in ~100ms.
 *
 * DESIGN RATIONALE:
 * - Contiguous path storage for cache locality
 * - Structure of Arrays (SoA) for parallel search efficiency
 * - Pre-parsed offsets (filename_start, extension_start) eliminate parsing overhead
 * - Tombstone deletion (is_deleted_) for efficient removal without reallocation
 *
 * PERFORMANCE:
 * - Enables searches across 1M paths in ~100ms
 * - Zero-copy read access via SoAView for search operations
 * - Contiguous memory layout provides optimal cache behavior
 */

/**
 * @class PathStorage
 * @brief Manages SoA arrays for path storage and parallel search
 *
 * This class encapsulates the Structure of Arrays design used for high-performance
 * parallel searching. All paths are stored in a contiguous buffer with parallel
 * arrays tracking offsets, IDs, and metadata.
 */
class PathStorage {
public:
  /**
   * @brief Read-only view of SoA arrays for search operations
   *
   * This struct provides zero-copy access to the SoA arrays for parallel search.
   * The arrays are accessed via raw pointers, maintaining optimal performance.
   *
   * CRITICAL: This view is only valid while the PathStorage instance exists
   * and no modifications are made. Caller must ensure proper synchronization.
   */
  struct SoAView {

    const char *path_storage = nullptr;        // Contiguous path buffer

    const size_t *path_offsets = nullptr;      // Offset into path_storage_ for each entry

    const uint64_t *path_ids = nullptr;        // ID for each path entry

    const size_t *filename_start = nullptr;    // Filename offset in path

    const size_t *extension_start = nullptr;   // Extension offset (SIZE_MAX = no extension)

    const uint8_t *is_deleted = nullptr;       // Tombstone flag (0 = not deleted, 1 = deleted)

    const uint8_t *is_directory = nullptr;     // Directory flag (0 = file, 1 = directory)

    size_t size = 0;                      // Number of entries

    SoAView() = default;
  };

  /**
   * @brief Construct PathStorage with initial capacity
   */
  PathStorage();

  // Default destructor (RAII members handle cleanup)
  ~PathStorage() = default;

  // Delete copy constructor and assignment (PathStorage is not copyable)
  PathStorage(const PathStorage &) = delete;
  PathStorage &operator=(const PathStorage &) = delete;

  // Delete move constructor and assignment (atomic members are not movable)
  PathStorage(PathStorage &&) = delete;
  PathStorage &operator=(PathStorage &&) = delete;

  /**
   * @brief Insert or update a path entry
   *
   * @param id File ID
   * @param path Full path string
   * @param isDirectory True if this is a directory  // NOLINT(readability-identifier-naming) - Public API parameter name
   *
   * If the ID already exists, the old entry is marked as deleted and a new
   * entry is appended. This maintains the SoA layout without complex in-place
   * updates.
   */
  void InsertPath(uint64_t id, const std::string &path, bool isDirectory);  // NOLINT(readability-identifier-naming) - Public API parameter names

  /**
   * @brief Mark a path entry as deleted (tombstone)
   *
   * @param id File ID to mark as deleted
   *
   * Returns true if the entry was found and marked, false if not found.
   */
  [[nodiscard]] bool RemovePath(uint64_t id);

  /**
   * @brief Update a path entry (remove old, insert new)
   *
   * @param id File ID
   * @param newPath New full path string  // NOLINT(readability-identifier-naming) - Public API parameter name
   * @param isDirectory True if this is a directory  // NOLINT(readability-identifier-naming) - Public API parameter name
   */
  void UpdatePath(uint64_t id, const std::string &newPath, bool isDirectory);  // NOLINT(readability-identifier-naming) - Public API parameter names

  /**
   * @brief Update all paths with a given prefix
   *
   * @param oldPrefix Old path prefix to replace
   * @param newPrefix New path prefix
   *
   * Used when renaming/moving directories to update all descendant paths.
   */
  void UpdatePrefix(std::string_view oldPrefix, std::string_view newPrefix);  // NOLINT(readability-identifier-naming) - Public API parameter names

  /**
   * @brief Get a read-only view of SoA arrays for search operations
   *
   * This method provides zero-copy access to the arrays for parallel search.
   * The view is only valid while no modifications are made to PathStorage.
   *
   * @return SoAView containing pointers to all arrays
   */
  [[nodiscard]] SoAView GetReadOnlyView() const;

  /**
   * @brief Get path string for a given ID
   *
   * @param id File ID
   * @return Full path string, or empty string if not found
   */
  [[nodiscard]] std::string GetPath(uint64_t id) const;

  /**
   * @brief Get zero-copy path view for a given ID
   *
   * @param id File ID
   * @return String view of path, or empty view if not found
   */
  [[nodiscard]] std::string_view GetPathView(uint64_t id) const;

  /**
   * @brief Get path by array index (for search operations)
   *
   * @param index Array index
   * @return String view of path, or empty view if index is invalid
   */
  [[nodiscard]] std::string_view GetPathByIndex(size_t index) const;

  /**
   * @brief Path components view (zero-copy access to path parts)
   */
  struct PathComponentsView {
    std::string_view full_path{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
    std::string_view filename{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
    std::string_view extension{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
    std::string_view directory_path{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
    bool has_extension = false;
  };

  /**
   * @brief Get path components for a given ID
   *
   * @param id File ID
   * @return PathComponentsView with all path components
   */
  [[nodiscard]] PathComponentsView GetPathComponents(uint64_t id) const;

  /**
   * @brief Get path components by array index
   *
   * @param index Array index
   * @return PathComponentsView with all path components
   */
  [[nodiscard]] PathComponentsView GetPathComponentsByIndex(size_t index) const;

  /**
   * @brief Get array index for a given ID
   *
   * @param id File ID
   * @return Array index, or SIZE_MAX if not found
   */
  [[nodiscard]] size_t GetIndexForId(uint64_t id) const;

  /**
   * @brief Get the number of entries (including deleted)
   */
  [[nodiscard]] size_t GetSize() const { return path_ids_.size(); }

  /**
   * @brief Get the number of deleted entries
   */
  [[nodiscard]] size_t GetDeletedCount() const {
    return deleted_count_.load(std::memory_order_relaxed);
  }

  /**
   * @brief Rebuild path buffer to remove deleted entries
   *
   * This defragments the storage by removing all deleted entries and rebuilding
   * the contiguous buffer. Should be called periodically during maintenance.
   */
  void RebuildPathBuffer();

  /**
   * @brief Clear all entries
   */
  void Clear();

  /**
   * @brief Get statistics for diagnostics
   */
  struct Stats {

    size_t total_entries = 0;  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check

    size_t deleted_entries = 0;  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check

    size_t path_storage_bytes = 0;  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check

    size_t rebuild_count = 0;  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
  };
  [[nodiscard]] Stats GetStats() const;

  /**
   * @brief Get path storage size in bytes
   */
  [[nodiscard]] size_t GetStorageSize() const { return path_storage_.size(); }

private:
  /**
   * @brief Internal helper to clear all storage arrays and reset counters
   *
   * This is used by both Clear() and RebuildPathBuffer() to eliminate code duplication.
   */
  void ClearAll();
  // Structure of Arrays (SoA) for cache-efficient parallel search
  std::vector<char> path_storage_;  // NOLINT(readability-identifier-naming) - project uses snake_case_ for members
  std::vector<size_t> path_offsets_;  // NOLINT(readability-identifier-naming)
  std::vector<uint64_t> path_ids_;  // NOLINT(readability-identifier-naming)
  std::vector<size_t> filename_start_;  // NOLINT(readability-identifier-naming)
  std::vector<size_t> extension_start_;  // NOLINT(readability-identifier-naming)
  std::vector<uint8_t> is_deleted_;  // NOLINT(readability-identifier-naming)
  std::vector<uint8_t> is_directory_;  // NOLINT(readability-identifier-naming)

  // ID -> array index mapping for O(1) lookup
  hash_map_t<uint64_t, size_t> id_to_path_index_;  // NOLINT(readability-identifier-naming)

  // Statistics
  std::atomic<size_t> deleted_count_{0};  // NOLINT(readability-identifier-naming)
  std::atomic<size_t> rebuild_count_{0};  // NOLINT(readability-identifier-naming)

  // Helper to append string to contiguous buffer
  size_t AppendString(const std::string &str);

  // Helper to parse path and extract filename/extension offsets
  void ParsePathOffsets(std::string_view path, size_t &filename_start,
                        size_t &extension_start) const;

  // Assert that all SoA arrays have equal length (invariant check).
  void AssertSoAInvariant(const char* context) const;

  // Constants for initial capacity allocation
  // kInitialPathStorageCapacity: Sized for ~500,000 paths (typical target)
  // Calculation: 500,000 paths × 100 bytes average = 50 MB, with headroom = 64 MB
  // This avoids reallocations during initial population, which would fragment
  // the contiguous buffer and degrade cache locality.
  static constexpr size_t kInitialPathStorageCapacity =
      64ULL * 1024ULL * 1024ULL;  // 64MB for ~500K paths; ULL avoids implicit widening
  static constexpr size_t kInitialPathArrayCapacity =
      500000U;  // Sized for typical target of 500K paths
};

