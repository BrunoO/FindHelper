#include <algorithm>
#include <chrono>

#include "index/FileIndexMaintenance.h"
#include "search/FilenameTrigramIndex.h"
#include "utils/Logger.h"

FileIndexMaintenance::FileIndexMaintenance(
    PathStorage& path_storage,
    std::shared_mutex& mutex,
    std::function<size_t()> get_alive_count,
    std::function<void(uint64_t file_id, size_t index)> set_path_storage_index,
    RemovalCounters counters,
    filename_trigram::FilenameTrigramIndex& trigram_index,
    std::function<std::shared_ptr<SearchThreadPool>()> get_thread_pool)
    : path_storage_(path_storage),
      index_mutex_ref_(mutex),
      get_alive_count_(std::move(get_alive_count)),
      set_path_storage_index_(std::move(set_path_storage_index)),
      counters_(counters),
      trigram_index_(trigram_index),
      get_thread_pool_(std::move(get_thread_pool)) {}

bool FileIndexMaintenance::Maintain() {
  size_t deleted_count = 0;
  size_t total_path_entries = 0;
  bool should_rebuild = false;
  {
    const std::shared_lock lock(index_mutex_ref_);
    deleted_count = path_storage_.GetDeletedCount();
    if (deleted_count == 0) {
      return false;
    }
    total_path_entries = path_storage_.GetSize();
    should_rebuild =
        deleted_count > kRebuildDeletedCountThreshold ||
        (total_path_entries > 0 &&
         static_cast<double>(deleted_count) / static_cast<double>(total_path_entries) >
             kRebuildDeletedPercentageThreshold);
  }

  if (!should_rebuild) {
    return false;
  }

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

FileIndexMaintenance::MaintenanceStats FileIndexMaintenance::GetMaintenanceStats() const {
  const std::shared_lock lock(index_mutex_ref_);
  return BuildMaintenanceStatsUnlocked();
}

std::optional<FileIndexMaintenance::MaintenanceStats>
FileIndexMaintenance::TryGetMaintenanceStats() const {
  if (const std::shared_lock lock(index_mutex_ref_, std::try_to_lock); lock.owns_lock()) {
    return BuildMaintenanceStatsUnlocked();
  }
  return std::nullopt;
}

FileIndexMaintenance::MaintenanceStats
FileIndexMaintenance::BuildMaintenanceStatsUnlocked() const {
  MaintenanceStats stats;
  const auto path_stats = path_storage_.GetStats();
  stats.rebuild_count = path_stats.rebuild_count;
  stats.deleted_count = path_stats.deleted_entries;
  stats.total_entries = path_stats.total_entries;
  stats.remove_not_in_index_count = counters_.remove_not_in_index_count.load();
  stats.remove_duplicate_count = counters_.remove_duplicate_count.load();
  stats.remove_inconsistency_count = counters_.remove_inconsistency_count.load();
  return stats;
}

void FileIndexMaintenance::RebuildPathBuffer() {
  // In-memory rebuild only; no I/O under lock (see docs/design/LOCK_ORDERING_AND_CRITICAL_SECTIONS.md).
  const auto start = std::chrono::high_resolution_clock::now();
  filename_trigram::TrigramBuildTicket ticket{0U, {}};
  {
    const std::unique_lock lock(index_mutex_ref_);

    if (path_storage_.GetDeletedCount() == 0) {
      LOG_INFO("FileIndexMaintenance::RebuildPathBuffer skipped (no deleted entries)");
      return;
    }

    path_storage_.RebuildPathBuffer(set_path_storage_index_);

    // Same lock scope as the compaction above: snapshot fresh trigram
    // postings before any search or mutator can observe compacted rows with
    // stale posting row-ids. Splitting this into "compact now, snapshot later"
    // reopens a TOCTOU window where IsReady()==true but postings reference
    // pre-compact rows (transient false negatives). SubmitBuild below waits
    // for any in-flight build to retire, so it must run after unlock —
    // waiting under the index lock would stall every concurrent search for
    // the duration of the previous build.
    ticket = trigram_index_.BeginRebuild(path_storage_.GetReadOnlyView(),
                                         path_storage_.GetStorageSize());
  }
  trigram_index_.SubmitBuild(std::move(ticket), get_thread_pool_());

  const auto end = std::chrono::high_resolution_clock::now();
  // [[maybe_unused]]: read only by LOG_INFO_BUILD below (compiled out under NDEBUG).
  [[maybe_unused]] const double elapsed_ms{std::chrono::duration<double, std::milli>(end - start).count()};
  LOG_INFO_BUILD("FileIndexMaintenance::RebuildPathBuffer completed in " << elapsed_ms << " ms");
}

