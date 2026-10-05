#pragma once

#include "path/PathStorage.h"
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>

class SearchThreadPool;

namespace filename_trigram {
class FilenameTrigramIndex;
}  // namespace filename_trigram

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
    size_t rebuild_count = 0;             // NOLINT(readability-identifier-naming) - public stats API used by Metrics/JSON-style display
    size_t deleted_count = 0;             // NOLINT(readability-identifier-naming) - public stats API used by Metrics/JSON-style display
    size_t total_entries = 0;             // NOLINT(readability-identifier-naming) - public stats API used by Metrics/JSON-style display
    size_t remove_not_in_index_count = 0; // NOLINT(readability-identifier-naming) - public stats API used by Metrics/JSON-style display
    size_t remove_duplicate_count = 0;    // NOLINT(readability-identifier-naming) - public stats API used by Metrics/JSON-style display
    size_t remove_inconsistency_count = 0; // NOLINT(readability-identifier-naming) - public stats API used by Metrics/JSON-style display
  };

  /**
   * Removal diagnostic counters, owned by FileIndex and shared by reference.
   * Bundled so the constructor stays within the parameter-count rule.
   */
  struct RemovalCounters {
    std::atomic<size_t>& remove_not_in_index_count;  // NOLINT(readability-identifier-naming) - plain data fields
    std::atomic<size_t>& remove_duplicate_count;  // NOLINT(readability-identifier-naming) - plain data fields
    std::atomic<size_t>& remove_inconsistency_count;  // NOLINT(readability-identifier-naming) - plain data fields
  };

  /**
   * @brief Construct FileIndexMaintenance
   *
   * @param path_storage Reference to PathStorage (for stats and rebuild)
   * @param mutex Reference to shared_mutex (for thread safety)
   * @param get_alive_count Function to get current alive entry count
   * @param set_path_storage_index Callback (file_id, index) to update FileEntry.path_storage_index after rebuild
   * @param counters Removal diagnostic counters (see RemovalCounters)
   * @param trigram_index Reference to the filename trigram index: compaction
   *        reassigns SoA row-ids, so the rebuild snapshots fresh postings
   *        under the same lock that performs the compaction (no TOCTOU gap
   *        where searches could observe compacted rows with stale postings)
   * @param get_thread_pool Provider for the pool used to build the trigram
   *        index off-lock; may return nullptr (synchronous build, used by tests)
   */
  FileIndexMaintenance(
      PathStorage& path_storage,
      std::shared_mutex& mutex,
      std::function<size_t()> get_alive_count,
      std::function<void(uint64_t file_id, size_t index)> set_path_storage_index,
      RemovalCounters counters,
      filename_trigram::FilenameTrigramIndex& trigram_index,
      std::function<std::shared_ptr<SearchThreadPool>()> get_thread_pool);

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
   * @brief Best-effort stats for UI — never blocks on a writer
   *
   * Returns nullopt when the index unique_lock is held (USN Remove / Maintain
   * rebuild). Callers should keep showing the last successful snapshot.
   */
  [[nodiscard]] std::optional<MaintenanceStats> TryGetMaintenanceStats() const;

  // Constants for maintenance thresholds
  static constexpr size_t kRebuildDeletedCountThreshold =
      1000; // Rebuild if deleted count exceeds this
  static constexpr double kRebuildDeletedPercentageThreshold =
      0.10; // Rebuild if deleted count > 10% of total

private:
  // Rebuilds the path buffer to remove deleted entries. Private: must only
  // run via Maintain(). Compaction reassigns every surviving SoA row-id, so
  // FileIndex::Maintain() refreshes the trigram index afterwards; calling
  // this directly would leave trigram posting row-ids stale.
  void RebuildPathBuffer();

  PathStorage& path_storage_;
  std::shared_mutex& index_mutex_ref_;
  std::function<size_t()> get_alive_count_{};  // NOLINT(readability-redundant-member-init)
  std::function<void(uint64_t file_id, size_t index)> set_path_storage_index_{};  // NOLINT(readability-redundant-member-init)
  RemovalCounters counters_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  filename_trigram::FilenameTrigramIndex& trigram_index_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  std::function<std::shared_ptr<SearchThreadPool>()> get_thread_pool_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members

  [[nodiscard]] MaintenanceStats BuildMaintenanceStatsUnlocked() const;
};

