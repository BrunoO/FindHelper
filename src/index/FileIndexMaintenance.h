#pragma once

#include "path/PathStorage.h"
#include <atomic>
#include <cstddef>
#include <functional>
#include <shared_mutex>

/**
 * @file FileIndexMaintenance.h
 * @brief Encapsulates maintenance operations for FileIndex
 *
 * This class handles periodic maintenance operations such as defragmentation
 * and cleanup. It encapsulates the logic for determining when maintenance is
 * needed and performing the actual maintenance work.
 *
 * DESIGN:
 * - Takes references to necessary components (PathStorage, mutex, counters)
 * - Provides clean interface: Maintain() and GetMaintenanceStats()
 * - Encapsulates maintenance thresholds and logic
 * - Thread-safe (uses shared_mutex for coordination)
 */
class FileIndexMaintenance {
public:
  /**
   * @brief Maintenance statistics for debugging/monitoring
   */
  struct MaintenanceStats {
    size_t rebuild_count = 0;             // Number of times buffer has been rebuilt
    size_t deleted_count = 0;             // Current count of deleted entries
    size_t total_entries = 0;             // Total path entries
    size_t remove_not_in_index_count = 0; // Remove() called for files not in index
    size_t remove_duplicate_count = 0;    // Remove() called for already-deleted files
    size_t remove_inconsistency_count = 0; // Remove() called but file missing from path storage
  };

  /**
   * @brief Construct FileIndexMaintenance
   *
   * @param path_storage Reference to PathStorage (for stats and rebuild)
   * @param mutex Reference to shared_mutex (for thread safety)
   * @param get_alive_count Function to get current alive entry count
   * @param set_path_storage_index Callback (file_id, index) to update FileEntry.path_storage_index after rebuild
   * @param remove_not_in_index_count Reference to atomic counter
   * @param remove_duplicate_count Reference to atomic counter
   * @param remove_inconsistency_count Reference to atomic counter
   */
  FileIndexMaintenance(
      PathStorage& path_storage,
      std::shared_mutex& mutex,
      std::function<size_t()> get_alive_count,
      std::function<void(uint64_t file_id, size_t index)> set_path_storage_index,
      std::atomic<size_t>& remove_not_in_index_count,
      std::atomic<size_t>& remove_duplicate_count,
      std::atomic<size_t>& remove_inconsistency_count);

  // Default destructor (holds references, no cleanup needed)
  ~FileIndexMaintenance() = default;

  // Delete copy and move (holds references)
  FileIndexMaintenance(const FileIndexMaintenance&) = delete;
  FileIndexMaintenance& operator=(const FileIndexMaintenance&) = delete;
  FileIndexMaintenance(FileIndexMaintenance&&) = delete;
  FileIndexMaintenance& operator=(FileIndexMaintenance&&) = delete;

  /**
   * @brief Perform periodic maintenance operations
   *
   * Checks if maintenance is needed based on deleted entry thresholds.
   * If thresholds are exceeded, triggers a rebuild of the path buffer.
   *
   * Should be called periodically during idle time (e.g., from UI thread).
   *
   * @return true if maintenance was performed, false otherwise
   */
  [[nodiscard]] bool Maintain();

  /**
   * @brief Get maintenance statistics
   *
   * @return MaintenanceStats struct with current statistics
   */
  [[nodiscard]] MaintenanceStats GetMaintenanceStats() const;

  /**
   * @brief Rebuild the path buffer to remove deleted entries
   *
   * Internal method called by Maintain() when thresholds are exceeded.
   * Can also be called directly if needed.
   */
  void RebuildPathBuffer();

  // Constants for maintenance thresholds
  static constexpr size_t kRebuildDeletedCountThreshold =
      1000; // Rebuild if deleted count exceeds this
  static constexpr double kRebuildDeletedPercentageThreshold =
      0.10; // Rebuild if deleted count > 10% of total

private:
  PathStorage& path_storage_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::shared_mutex& index_mutex_ref_;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members,readability-identifier-naming) - ref to FileIndex's index_mutex_; name follows project snake_case_ convention
  std::function<size_t()> get_alive_count_{};  // NOLINT(readability-identifier-naming,readability-redundant-member-init) - project convention; in-class init satisfies member-init check
  std::function<void(uint64_t file_id, size_t index)> set_path_storage_index_{};  // NOLINT(readability-identifier-naming,readability-redundant-member-init) - project convention; in-class init for default-empty callback
  std::atomic<size_t>& remove_not_in_index_count_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::atomic<size_t>& remove_duplicate_count_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::atomic<size_t>& remove_inconsistency_count_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
};

