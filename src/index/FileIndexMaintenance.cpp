#include "index/FileIndexMaintenance.h"

#include <chrono>
#include <new>
#include <utility>

#include "search/CombinedIndexPostings.h"
#include "search/ExtensionIndex.h"
#include "search/FilenameTrigramIndex.h"
#include "utils/Logger.h"
#include "utils/ThreadPool.h"

FileIndexMaintenance::FileIndexMaintenance(
    PathStorage& path_storage,
    std::shared_mutex& mutex,
    std::function<size_t()> get_alive_count,
    std::function<void(uint64_t file_id, size_t index)> set_path_storage_index,
    RemovalCounters counters,
    IndexRefs indexes,
    std::function<std::shared_ptr<ThreadPool>()> get_thread_pool)
    : path_storage_(path_storage),
      index_mutex_ref_(mutex),
      get_alive_count_(std::move(get_alive_count)),
      set_path_storage_index_(std::move(set_path_storage_index)),
      counters_(counters),
      trigram_index_(indexes.trigram_index),
      extension_index_(indexes.extension_index),
      get_thread_pool_(std::move(get_thread_pool)) {}

bool FileIndexMaintenance::Maintain() {
  // Fast lock-free check on atomic deleted_count_ so periodic UI-thread polls
  // do not contend with USN/index writers when nothing has been deleted.
  if (path_storage_.GetDeletedCount() == 0) {
    return false;
  }

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
  filename_trigram::TrigramBuildTicket trigram_ticket{0U, {}};
  extension_index::ExtensionBuildTicket extension_ticket{0U, {}};
  {
    const std::unique_lock lock(index_mutex_ref_);

    if (path_storage_.GetDeletedCount() == 0) {
      LOG_INFO("FileIndexMaintenance::RebuildPathBuffer skipped (no deleted entries)");
      return;
    }

    path_storage_.RebuildPathBuffer(set_path_storage_index_);

    // Same lock scope as the compaction above: snapshot fresh postings for
    // BOTH indexes before any search or mutator can observe compacted rows
    // with stale posting row-ids. Splitting this into "compact now, snapshot
    // later" reopens a TOCTOU window where IsReady()==true but postings
    // reference pre-compact rows (transient false negatives). One combined
    // extraction serves both indexes from the same SoAView — exactly one
    // pass over the compacted buffer. The trigram ticket is constructed
    // directly from the marked generation (public ticket ctor +
    // BuildFromPostings path); FilenameTrigramIndex itself is untouched.
    // SubmitBuild below waits for any in-flight build to retire, so it must
    // run after unlock — waiting under the index lock would stall every
    // concurrent search for the duration of the previous build.
    const uint64_t trigram_generation = trigram_index_.MarkBuilding();
    const uint64_t extension_generation = extension_index_.MarkBuilding();
    (void)extension_generation;
    // Extraction reserves up to filename_trigram::kMaxPostingReservation (256 MB) and
    // pushes per row, so std::bad_alloc is the only realistic failure here.
    // AbortBuild restores is_building_/is_ready_ and replays mutations
    // buffered since MarkBuilding(); without it the indexes would never
    // report ready again and pending_mutations_ would grow without bound.
    // The tickets below stay invalid, so both SubmitBuild calls after unlock
    // are safe no-ops on this path.
    try {
      CombinedIndexPostings combined = ExtractCombinedPostings(path_storage_.GetReadOnlyView(),
                                                               path_storage_.GetStorageSize());
      trigram_ticket = filename_trigram::TrigramBuildTicket{trigram_generation,
                                                            std::move(combined.trigram_postings)};
      extension_ticket = extension_index_.BeginRebuild(std::move(combined.extension_groups));
    } catch (const std::bad_alloc& e) {
      (void)e;
      LOG_ERROR_BUILD("FileIndexMaintenance::RebuildPathBuffer: postings extraction ran out of "
                      "memory; index builds aborted, searches fall back to scan");
      trigram_index_.AbortBuild(trigram_generation);
      extension_index_.AbortBuild(extension_generation);
    }
  }
  trigram_index_.SubmitBuild(std::move(trigram_ticket), get_thread_pool_());
  extension_index_.SubmitBuild(std::move(extension_ticket), get_thread_pool_());

  const auto end = std::chrono::high_resolution_clock::now();
  // [[maybe_unused]]: read only by LOG_INFO_BUILD below (compiled out under NDEBUG).
  [[maybe_unused]] const double elapsed_ms{std::chrono::duration<double, std::milli>(end - start).count()};
  LOG_INFO_BUILD("FileIndexMaintenance::RebuildPathBuffer completed in " << elapsed_ms << " ms");
}

