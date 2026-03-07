#include <algorithm>
#include <chrono>

#include "index/FileIndexMaintenance.h"
#include "utils/Logger.h"

FileIndexMaintenance::FileIndexMaintenance(
    PathStorage& path_storage,
    std::shared_mutex& mutex,
    std::function<size_t()> get_alive_count,
    std::atomic<size_t>& remove_not_in_index_count,
    std::atomic<size_t>& remove_duplicate_count,
    std::atomic<size_t>& remove_inconsistency_count)
    : path_storage_(path_storage),
      index_mutex_ref_(mutex),
      get_alive_count_(std::move(get_alive_count)),
      remove_not_in_index_count_(remove_not_in_index_count),
      remove_duplicate_count_(remove_duplicate_count),
      remove_inconsistency_count_(remove_inconsistency_count) {}

bool FileIndexMaintenance::Maintain() {
  // Need to acquire lock to safely read path storage stats
  std::shared_lock lock(index_mutex_ref_);

  const size_t deleted_count = path_storage_.GetDeletedCount();
  if (deleted_count == 0) {
    return false; // No deleted entries, nothing to do
  }

  // Get total path entries (alive + deleted) for percentage calculation
  const size_t total_path_entries = path_storage_.GetSize();

  // Check if rebuild threshold is exceeded
  bool should_rebuild = false;
  if (deleted_count > kRebuildDeletedCountThreshold) {
    should_rebuild = true;
  } else if (total_path_entries > 0) {
    const double deleted_percentage = static_cast<double>(deleted_count) /
                                     static_cast<double>(total_path_entries);
    if (deleted_percentage > kRebuildDeletedPercentageThreshold) {
      should_rebuild = true;
    }
  }

  // Release lock before calling RebuildPathBuffer (which acquires unique_lock)
  lock.unlock();

  if (should_rebuild) {
    LOG_INFO_BUILD("FileIndexMaintenance::Maintain triggering rebuild: "
                   << "deleted_count=" << deleted_count
                   << ", total_path_entries=" << total_path_entries
                   << ", alive_count=" << get_alive_count_()
                   << ", threshold_absolute=" << (deleted_count > kRebuildDeletedCountThreshold)
                   << ", threshold_percentage="
                   << (total_path_entries > 0 &&
                       static_cast<double>(deleted_count) / static_cast<double>(total_path_entries) >
                           kRebuildDeletedPercentageThreshold));
    RebuildPathBuffer();
    return true;
  }

  return false;
}

FileIndexMaintenance::MaintenanceStats FileIndexMaintenance::GetMaintenanceStats() const {
  const std::shared_lock lock(index_mutex_ref_);
  MaintenanceStats stats;
  auto path_stats = path_storage_.GetStats();
  stats.rebuild_count = path_stats.rebuild_count;
  stats.deleted_count = path_stats.deleted_entries;
  stats.total_entries = path_stats.total_entries;
  stats.remove_not_in_index_count =
      remove_not_in_index_count_.load(std::memory_order_acquire);
  stats.remove_duplicate_count =
      remove_duplicate_count_.load(std::memory_order_acquire);
  stats.remove_inconsistency_count =
      remove_inconsistency_count_.load(std::memory_order_acquire);
  return stats;
}

void FileIndexMaintenance::RebuildPathBuffer() {
  // Simple timer for performance monitoring
  const auto start = std::chrono::high_resolution_clock::now();
  const std::unique_lock lock(index_mutex_ref_);

  if (path_storage_.GetDeletedCount() == 0) {
    LOG_INFO("FileIndexMaintenance::RebuildPathBuffer skipped (no deleted entries)");
    return;
  }

  path_storage_.RebuildPathBuffer();

  const auto end = std::chrono::high_resolution_clock::now();
  const double elapsed_ms{std::chrono::duration<double, std::milli>(end - start).count()};  // NOLINT(cppcoreguidelines-init-variables) NOSONAR(cpp:S1481,cpp:S1854) - Read in LOG_INFO_BUILD macro below
  LOG_INFO_BUILD("FileIndexMaintenance::RebuildPathBuffer completed in " << elapsed_ms << " ms");
}

