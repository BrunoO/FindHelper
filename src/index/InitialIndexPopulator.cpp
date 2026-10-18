#include "index/InitialIndexPopulator.h"

#include "utils/CTrackWrapper.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <winioctl.h>

#include "index/FileIndex.h"
#include "index/IndexBuildRun.h"
#include "index/LazyValue.h"
#include "index/NtfsFileReference.h"
#include "index/SystemPathFilter.h"
#include "index/mft/DoubleBufferedChunkReader.h"
#include "index/mft/MftExtentProvider.h"
#include "index/mft/MftTypes.h"
#include "index/mft/RawMftRecordParser.h"
#include "index/mft/RawVolumeReader.h"
#include "index/recycle/RecycleBinEnricher.h"
#include "index/recycle/RecycleBinMetadataStore.h"
#include "path/PathUtils.h"
#include "usn/UsnRecord.h"
#include "usn/VolumeGateway.h"
#include "utils/FileAttributeConstants.h"
#include "utils/FileSystemUtils.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

namespace {
// Upper-bound MFT record estimate for pre-sizing the index (issue #5).
// NTFS_VOLUME_DATA_BUFFER has no file count; MftValidDataLength /
// BytesPerFileRecordSegment counts all record slots including free ones,
// which is exactly the safe direction for reserve(). Returns 0 when the
// query fails or yields nothing useful (caller skips Reserve).
[[nodiscard]] size_t EstimateMftRecordCount(HANDLE volume_handle,
                                             volume_gateway::VolumeGateway& gateway) {
  NTFS_VOLUME_DATA_BUFFER volume_data = {};
  if (!gateway.GetVolumeData(volume_handle, volume_data)) {
    LOG_DEBUG_BUILD("FSCTL_GET_NTFS_VOLUME_DATA failed, skipping index pre-size");
    return 0;
  }
  if (volume_data.BytesPerFileRecordSegment == 0) {
    return 0;
  }
  // The MFT record parser assumes 512-byte sectors for the USA fixup. Log the
  // real geometry once so a 4Kn volume (systematic fixup/signature failures
  // below, all falling back to lazy load) is directly visible in the log.
  LOG_INFO_BUILD("NTFS geometry: BytesPerSector=" << volume_data.BytesPerSector
                 << " BytesPerCluster=" << volume_data.BytesPerCluster
                 << " BytesPerFileRecordSegment=" << volume_data.BytesPerFileRecordSegment);
  if (volume_data.BytesPerSector != 512) {
    LOG_WARNING_BUILD("Non-512-byte sectors detected; MFT USA fixup assumes 512B, "
                      "metadata reads may systematically fall back to lazy loading");
  }
  const ULONGLONG max_records =
      static_cast<ULONGLONG>(volume_data.MftValidDataLength.QuadPart) /
      volume_data.BytesPerFileRecordSegment;
  if (max_records == 0) {
    return 0;
  }
  // 15% headroom with integer math (avoids float->size_t narrowing).
  const ULONGLONG with_headroom = max_records * 115ULL / 100ULL;
  const ULONGLONG capped =
      with_headroom > static_cast<ULONGLONG>((std::numeric_limits<size_t>::max)())
          ? static_cast<ULONGLONG>((std::numeric_limits<size_t>::max)())
          : with_headroom;
  return static_cast<size_t>(capped);
}

[[nodiscard]] uint64_t ElapsedMicroseconds(
    const std::chrono::steady_clock::time_point start_time) {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now() - start_time)
                                   .count());
}

// Name passed when re-driving a retained non-$ directory. Every policy name predicate
// ($Recycle.Bin, $I/$R shape, $-prefix) is false for a non-$ name, so the empty name
// classifies identically. $-named directories are deferred with their real name.
constexpr std::string_view kUnnamedEntry;

// Cold-path ingestion mode (Phase 2): containers and $R payloads are returned as classified
// and ingested. Mirrors kLiveIngestionMode in usn/UsnMonitor.cpp (the live path).
constexpr system_path_filter::IngestionMode kColdIngestionMode =
    system_path_filter::IngestionMode::kIncludeRecycleBin;

[[nodiscard]] size_t TrackedContainerCount(const system_path_filter::SystemPathFilterPolicy& policy) {
  return policy.TrackedContainerCount();
}
} // namespace

namespace index_build_run {

IndexBuildRun::IndexBuildRun(HANDLE volume_handle, volume_gateway::VolumeGateway& gateway,
                             FileIndex& file_index,
                             std::atomic<size_t>* indexed_file_count,
                             system_path_filter::SystemPathFilterPolicy& policy,
                             std::atomic<bool>* integrity_latch)
    : volume_handle_(volume_handle),
      volume_gateway_(gateway),
      file_index_(file_index),
      indexed_file_count_(indexed_file_count),
      policy_(policy),
      integrity_latch_(integrity_latch),
      last_progress_update_time_(std::chrono::steady_clock::now()) {
}

void IndexBuildRun::Reserve() {
  policy_.ClearContainers();
  policy_.ReserveContainers(64);
  retained_directories_.clear();
  retained_directories_.reserve(1024U);  // NOLINT(readability-magic-numbers) - heuristic: small initial estimate; vector grows geometrically
  deferred_entries_.clear();
  r_triples_.clear();
  i_triples_.clear();
  sid_triples_.clear();
  // Pre-size index maps + name arena from the MFT upper bound so the build
  // loop does not pay geometric rehash pauses (issue #5). Skipped when the
  // volume query fails (estimate 0). Single-threaded here: no contention.
  if (const size_t estimated_entries = EstimateMftRecordCount(volume_handle_, volume_gateway_);
      estimated_entries > 0) {
    file_index_.ReserveForPopulation(estimated_entries);
    LOG_INFO_BUILD("Pre-sized index for ~" << estimated_entries << " MFT records");
  }
}

void IndexBuildRun::NoteStaged() {
  ++total_files_;
}

void IndexBuildRun::NoteIteration() {
  ++iterations_;
}

void IndexBuildRun::NoteFiltered() {
  ++filtered_count_;
}

void IndexBuildRun::CommitBatch(const std::vector<FileIndex::PopulationBatchEntry>& batch) {
  // Defer path indexing: this run always ends with RecomputePathsAndPruneOrphans
  // (UsnMonitor::RunInitialPopulationAndPrivileges, before is_populating clears
  // and search is ungated), which Clear()s PathStorage and indexes every path
  // exactly once. Per-insert joins + InsertPath here would be pure waste.
  // Search cannot observe the gap (gated on IsIndexBuilding()); replay-drain
  // mutators (delete/rename/move) tolerate pathless entries and the recompute
  // discards their intermediates.
  file_index_.InsertBatch(batch, /*defer_path_indexing=*/true);
}

void IndexBuildRun::CommitExtensionNames(const std::vector<mft::RawMftRecordParser::ExtensionName>& names) {
  if (names.empty()) {
    return;
  }
  
  // Extension records are merged into the index via UpdateNameForDeferredEntry
  // (which updates the NameArena directly) before RecomputeAllPaths builds the PathStorage.
  for (const auto& ext : names) {
    // We use the fast path to update the long name for the entry without triggering
    // O(N) path rebuilds. The ext.name is the Win32 long name, so it correctly overwrites the DOS name.
    file_index_.UpdateNameForDeferredEntry(ntfs_file_reference::NtfsFileReference(ext.base_frn), ext.name);
  }
}

void IndexBuildRun::NoteProgress() {
  if (const auto now = std::chrono::steady_clock::now();
      now - last_progress_update_time_ >= std::chrono::seconds(1)) {
    if (indexed_file_count_ != nullptr) {
      indexed_file_count_->store(static_cast<size_t>(total_files_));
    }
    LOG_INFO_BUILD("Enumerated " << total_files_ << " files...");
    last_progress_update_time_ = now;
  }
}

void IndexBuildRun::NoteCorruptRecord(DWORD offset) {
  integrity_hit_ = true;
  std::ostringstream reason;
  reason << "USN record corrupt at offset " << offset
         << " during initial journal enumeration; remaining buffer records "
            "skipped. Index may be incomplete/stale.";
  if (integrity_latch_ != nullptr) {
    integrity_latch_->store(true);
  }
  LOG_ERROR_BUILD(reason.str());
}

IndexBuildRun::Result IndexBuildRun::GetResult() const {
  return {total_files_, iterations_, filtered_count_, integrity_hit_};
}

void IndexBuildRun::LogOutcome() const {
  LOG_INFO_BUILD("Index population outcome: " << total_files_ << " entries indexed, "
                 << filtered_count_ << " filtered, in " << iterations_ << " chunk iterations");
  LOG_DEBUG_BUILD("Recycle $I metadata skipped (handed to the enricher by DrainRecycleBinProducers): "
                  << recycle_metadata_skipped_);
}

void IndexBuildRun::FinalizeProgress() {
  // Ensure the counter matches the final index size after all inserts.
  if (indexed_file_count_ != nullptr) {
    indexed_file_count_->store(file_index_.Size());
  }
}

void IndexBuildRun::RecomputePathsAndPruneOrphans() {
  file_index_.RecomputeAllPaths();
}

bool IndexBuildRun::RetainForFinalize(std::string_view name, uint64_t file_reference,
                                      uint64_t parent_file_reference, bool is_directory) {
  if (system_path_filter::IsSystemPrefixedName(name)) {
    // Children of a tracked <SID>/$R-folder classify in-stream. Any other $-named
    // entry is deferred: a $-named directory may be a container ($Recycle.Bin, $I/$R)
    // that later entries depend on, so it is re-driven with its real name.
    if (policy_.IsTrackedSidOrRecycledFolder(
            ntfs_file_reference::MftRecordNumber::FromFileReference(parent_file_reference))) {
      return false;
    }
    deferred_entries_.push_back(DeferredEntry{
        std::string(name), ntfs_file_reference::RecordNumber(file_reference),
        ntfs_file_reference::RecordNumber(parent_file_reference), file_reference,
        parent_file_reference, is_directory});
    return true;
  }
  // Non-$ directories can become <SID>/$R-folder containers or cascade parents once their
  // own parent is tracked (enumeration order is allocation order), so keep them for re-drive.
  if (is_directory) {
    retained_directories_.push_back(RetainedDirectory{
        ntfs_file_reference::RecordNumber(file_reference),
        ntfs_file_reference::RecordNumber(parent_file_reference)});
  }
  return false;
}

void IndexBuildRun::SetRecycleBinSink(recycle_bin::RecycleBinMetadataStore* store,
                                      recycle_bin::RecycleBinEnricher* enricher) noexcept {
  recycle_store_ = store;
  recycle_enricher_ = enricher;
}

void IndexBuildRun::CollectRecycleProducer(std::string_view name, uint64_t file_reference,
                                           uint64_t parent_file_reference, bool is_directory,
                                           system_path_filter::IngestionDisposition disposition) {
  // $I sidecars: queued for the enricher (read off-thread by the worker, never here).
  if (disposition == system_path_filter::IngestionDisposition::SkipRecycleMetadata) {
    i_triples_.push_back(RecycleProducerTriple{std::string(name), file_reference,
                                               parent_file_reference, is_directory});
    return;
  }
  if (disposition != system_path_filter::IngestionDisposition::IncludeRecycleBin) {
    return;  // Normal and excluded entries have no recycle-producer role.
  }
  // <SID> directory: the enricher expands it into $I tasks (cold path, M8).
  if (is_directory && policy_.IsTrackedSid(
                          ntfs_file_reference::MftRecordNumber::FromFileReference(file_reference))) {
    sid_triples_.push_back(RecycleProducerTriple{std::string(), file_reference,
                                                 parent_file_reference, is_directory});
    return;
  }
  // $R payload: the store pairs it with its $I side. Containers that are not $R-shaped are not
  // payloads and are not reported.
  if (const auto token = recycle_bin::TokenizeRecycleName(name);
      token && token->prefix == recycle_bin::RecycleNamePrefix::kR) {
    r_triples_.push_back(RecycleProducerTriple{std::string(name), file_reference,
                                               parent_file_reference, is_directory});
  }
}

void IndexBuildRun::DrainRecycleBinProducers() {
  // Runs after RecomputePathsAndPruneOrphans (sequencing): every path read below is resolved.
  // Path lookups are self-locking (GetPathCopy), so no index lock is held across the sinks.
  if (recycle_store_ == nullptr || recycle_enricher_ == nullptr) {
    if (!r_triples_.empty() || !i_triples_.empty() || !sid_triples_.empty()) {
      LOG_DEBUG_BUILD("Recycle producer drain: sink not set (store=" << (recycle_store_ != nullptr)
                      << ", enricher=" << (recycle_enricher_ != nullptr)
                      << "); collected recycle entries not handed off");
    }
  }
  if (recycle_store_ != nullptr) {
    // One batched call for the whole drain: each single-item IngestRFile copies the published maps
    // and bumps the version, so a per-triple loop would cost O(N^2) on a real bin and re-trigger
    // the enricher's re-read once per $R. The batch keeps the triples' order and publishes at most
    // once. No file I/O happens here, and the store takes its own lock.
    std::vector<recycle_bin::RFileInfo> r_batch;
    r_batch.reserve(r_triples_.size());
    for (RecycleProducerTriple& triple : r_triples_) {
      recycle_bin::RFileInfo info;
      info.r_frn = triple.file_reference;
      info.parent_frn = triple.parent_file_reference;
      info.name = std::move(triple.name);
      info.is_directory = triple.is_directory;
      r_batch.push_back(std::move(info));
    }
    recycle_store_->IngestRFiles(r_batch);
  }
  if (recycle_enricher_ != nullptr) {
    size_t unresolved = 0;
    for (RecycleProducerTriple& triple : i_triples_) {
      std::string sid_path = file_index_.GetPathAccessor().GetPathCopy(triple.parent_file_reference);
      if (sid_path.empty()) {
        ++unresolved;
        continue;
      }
      std::string absolute_path = path_utils::JoinPath(sid_path, triple.name);
      // Drops (full queue / stopped) are counted by the enricher's DropCount().
      static_cast<void>(recycle_enricher_->EnqueueIFile(triple.file_reference,
                                                        triple.parent_file_reference,
                                                        std::move(triple.name),
                                                        std::move(absolute_path)));
    }
    for (const RecycleProducerTriple& triple : sid_triples_) {
      std::string sid_path = file_index_.GetPathAccessor().GetPathCopy(triple.file_reference);
      if (sid_path.empty()) {
        ++unresolved;
        continue;
      }
      static_cast<void>(recycle_enricher_->EnqueueSidMarker(triple.file_reference, std::move(sid_path)));
    }
    if (unresolved > 0) {
      LOG_DEBUG_BUILD("Recycle producer drain: " << unresolved
                      << " entries had no resolved parent path and were not queued");
    }
  }
  r_triples_.clear();
  i_triples_.clear();
  sid_triples_.clear();
}

size_t IndexBuildRun::FinalizeDeferredRecycleEntries() {
  // C1 order independence. Enumeration order is allocation order, so a child can be
  // seen before the $Recycle.Bin / <SID> / $R-folder that tracks it (MFT record slot
  // reuse). Re-drive everything retained in FRN order and repeat until the trackers
  // stop growing. Erases can shrink the trackers (a rename-out via ClassifyOutsideSubtree
  // sweeps a subtree), so the size is not monotone; the pass bound, not monotonicity,
  // guarantees termination. 4 is ample: each pass resolves at least one container
  // level and real recycle-bin depths are < 4.
  constexpr int kMaxFinalizePasses = 4;
  std::sort(retained_directories_.begin(), retained_directories_.end(),
            [](const RetainedDirectory& a, const RetainedDirectory& b) {
              return a.record_number < b.record_number;
            });
  std::sort(deferred_entries_.begin(), deferred_entries_.end(),
            [](const DeferredEntry& a, const DeferredEntry& b) {
              return a.record_number < b.record_number;
            });
  size_t tracked_before = 0;
  size_t tracked_after = TrackedContainerCount(policy_);
  int pass = 0;
  do {
    if (++pass > kMaxFinalizePasses) {
      // Trackers may be incomplete. Stop re-driving; the deferred entries below are still
      // classified once with the trackers as they stand (no exclusion is forced).
      LOG_WARNING_BUILD("Recycle finalize did not converge after " << kMaxFinalizePasses
                        << " passes; trackers may be incomplete, deferred entries classified as-is");
      break;
    }
    tracked_before = tracked_after;
    // Re-drive only updates tracker state, which is identical in every mode; the returned
    // disposition is discarded here.
    for (const RetainedDirectory& dir : retained_directories_) {
      static_cast<void>(policy_.ClassifyEntry(kUnnamedEntry, dir.record_number, dir.parent_number,
                                              /*is_directory=*/true, kColdIngestionMode));
    }
    for (const DeferredEntry& entry : deferred_entries_) {
      static_cast<void>(policy_.ClassifyEntry(entry.name, entry.record_number, entry.parent_number,
                                              entry.is_directory, kColdIngestionMode));
    }
    tracked_after = TrackedContainerCount(policy_);
  } while (tracked_after != tracked_before);

  // Phase 2 (classify-and-insert): deferred entries were never staged, so each is classified
  // once more in kColdIngestionMode and accounted for exactly once here. IncludeNormal and
  // IncludeRecycleBin survivors are staged and committed before return, so they precede
  // RecomputePathsAndPruneOrphans. SkipRecycleMetadata and ExcludeSystem are counted as
  // filtered. Retained directories were counted in-stream and are not revisited.
  // Tracker-miss diagnostics: deferred entries are the entries no tracker claimed. The loop
  // only records the first kMissSampleLimit parent record numbers (no formatting); the
  // summary is formatted and logged once, inside the guard, not per entry.
  constexpr size_t kMissSampleLimit = 5;
  std::array<uint64_t, kMissSampleLimit> miss_parent_sample{};
  size_t miss_sampled = 0;
  std::vector<FileIndex::PopulationBatchEntry> survivor_batch;
  survivor_batch.reserve(deferred_entries_.size());
  size_t survivors_staged = 0;
  for (const DeferredEntry& entry : deferred_entries_) {
    const system_path_filter::IngestionDisposition disposition = policy_.ClassifyEntry(
        entry.name, entry.record_number, entry.parent_number, entry.is_directory,
        kColdIngestionMode);
    CollectRecycleProducer(entry.name, entry.file_reference, entry.parent_file_reference,
                           entry.is_directory, disposition);
    switch (disposition) {
      case system_path_filter::IngestionDisposition::IncludeNormal:
      case system_path_filter::IngestionDisposition::IncludeRecycleBin: {
        // Same entry shape as StageRecord. Full FRNs (sequence bits) form the FileIndex key.
        FileIndex::PopulationBatchEntry survivor;
        survivor.id = ntfs_file_reference::NtfsFileReference(entry.file_reference);
        survivor.parent_id = ntfs_file_reference::NtfsFileReference(entry.parent_file_reference);
        survivor.name = entry.name;
        survivor.is_directory = entry.is_directory;
        survivor.modification_time = kFileTimeNotLoaded;
        survivor.file_size = kFileSizeNotLoaded;
        survivor_batch.push_back(std::move(survivor));
        NoteStaged();
        ++survivors_staged;
        break;
      }
      case system_path_filter::IngestionDisposition::SkipRecycleMetadata:
        // INV-2 + A1: $I sidecars never enter FileIndex; the staged triple is handed to the
        // enricher by DrainRecycleBinProducers (queue-first, no $I reads on this thread).
        ++recycle_metadata_skipped_;
        NoteFiltered();
        break;
      case system_path_filter::IngestionDisposition::ExcludeSystem:
        NoteFiltered();
        break;
    }
    if (miss_sampled < kMissSampleLimit) {
      miss_parent_sample[miss_sampled] = entry.parent_number;
      ++miss_sampled;
    }
  }
  if (!survivor_batch.empty()) {
    CommitBatch(survivor_batch);
  }
  if (!deferred_entries_.empty()) {
    std::ostringstream miss_parents;
    for (size_t i = 0; i < miss_sampled; ++i) {
      miss_parents << (i == 0 ? "" : ",") << miss_parent_sample[i];
    }
    LOG_DEBUG_BUILD("Recycle finalize deferred " << deferred_entries_.size()
                    << " entries; first parent record numbers: [" << miss_parents.str() << "]");
  }
  // Idempotency: a second call must not re-count or re-drive the same entries.
  deferred_entries_.clear();
  retained_directories_.clear();
  return survivors_staged;
}

bool IndexBuildRun::StageRecord(PUSN_RECORD_V2 record,
                               std::vector<FileIndex::PopulationBatchEntry>& out_batch) {
  CTRACK_DEV_NAME("IndexBuildRun::StageRecord");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  // Extract filename (wide string, not null-terminated) and convert via thread-local buffer.
  const wchar_t* const wfilename =
      reinterpret_cast<wchar_t*>(reinterpret_cast<std::byte*>(record) +  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR(cpp:S3630) - USN_RECORD_V2 variable-offset filename; bounds checked by ValidateAndParseUsnRecord
                                   record->FileNameOffset);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  const size_t wchar_len = record->FileNameLength / sizeof(wchar_t);
  const std::string_view filename = WideToUtf8ThreadLocal(wfilename, wchar_len);

  // Skip empty filenames (shouldn't happen, but be defensive)
  if (filename.empty()) {
    LOG_WARNING("Encountered file with empty filename, skipping");
    return true; // Continue processing
  }

  // Skip volume root directory record (MFT record 5) and pseudo-directory names (. and ..).
  // Record 5 must NEVER reach a policy tracker (it would prune the entire volume).
  if (const bool is_record_dir = (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
      system_path_filter::IsExcludedIndexEntry(filename, record->FileReferenceNumber, is_record_dir)) {
    NoteFiltered();
    return true; // Continue processing
  }

  uint64_t file_ref_num = record->FileReferenceNumber;
  uint64_t parent_ref_num = record->ParentFileReferenceNumber;
  bool is_directory = false;
  if (record != nullptr && (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    is_directory = true;
  }

  // Shared ingestion policy (index/SystemPathFilter.h), also used by real-time USN
  // monitoring. It records recycle-bin and system containers as it sees them and
  // applies the $-prefix and filtered-parent cascade. Phase 2 (kColdIngestionMode =
  // kIncludeRecycleBin) ingests containers and $R payloads as classified.
  const system_path_filter::IngestionDisposition disposition = policy_.ClassifyEntry(
      filename, file_ref_num, parent_ref_num, is_directory, kColdIngestionMode);
  // Deferred $-named entries are classified and staged or counted by FinalizeDeferredRecycleEntries.
  if (RetainForFinalize(filename, file_ref_num, parent_ref_num, is_directory)) {
    return true; // Continue processing
  }

  // Producer triples are staged here (no sink or store call in-stream); DrainRecycleBinProducers
  // hands them off after the path recompute.
  CollectRecycleProducer(filename, file_ref_num, parent_ref_num, is_directory, disposition);
  switch (disposition) {
    case system_path_filter::IngestionDisposition::IncludeNormal:
    case system_path_filter::IngestionDisposition::IncludeRecycleBin:
      break; // Stage below (containers and $R payloads are ingested, like normal entries)
    case system_path_filter::IngestionDisposition::ExcludeSystem:
      NoteFiltered();
      return true; // Continue processing
    case system_path_filter::IngestionDisposition::SkipRecycleMetadata:
      // INV-2: $I sidecars never enter FileIndex; counted here, read by the enricher via the drain.
      ++recycle_metadata_skipped_;
      NoteFiltered();
      return true; // Continue processing
  }

  // Modification time initialization:
  // Both directories and files start as kFileTimeNotLoaded so the lazy loader
  // fetches the real timestamp on first access via GetFileAttributesExW.
  // FILETIME{0,0} must NOT be used — it is the Windows epoch (1601-01-01) and
  const FILETIME mod_time = kFileTimeNotLoaded;
  const uint64_t file_size = kFileSizeNotLoaded;

  // Stage into the batch. Copies the thread-local filename into an owned
  // string (the thread-local buffer is reused per record). The batch is
  // committed under one lock per enumeration buffer; file_size rides along so
  // the commit applies it atomically with the insert (no second lock).
  // kFileSizeNotLoaded / MFT failure mean "skip the update" (lazy loading will
  // handle them); zero-byte files have a valid size of 0.
  FileIndex::PopulationBatchEntry entry;
  entry.id = ntfs_file_reference::NtfsFileReference(file_ref_num);
  entry.parent_id = ntfs_file_reference::NtfsFileReference(parent_ref_num);
  entry.name.assign(filename);
  entry.is_directory = is_directory;
  entry.modification_time = mod_time;
  entry.file_size = file_size;
  out_batch.push_back(std::move(entry));
  NoteStaged();

  // Periodically update the indexed file count and log progress
  // so the UI can display live progress during initial enumeration.
  NoteProgress();

  return true; // Success
}

bool IndexBuildRun::ProcessBuffer(const std::vector<char>& buf, DWORD bytes_ret,
                                  usn_journal_enum_position::UsnJournalEnumerationPosition& position) {
  CTRACK_DEV_NAME("IndexBuildRun::ProcessBuffer");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  if (bytes_ret < usn_record::SizeOfUsn()) {
    return false; // Signal end of enumeration
  }

  // The first 8 bytes contain the next USN to continue from
  // Cast to const USN* first (since buf is const), then dereference
  USN next_usn = *reinterpret_cast<const USN *>(buf.data()); // NOSONAR(cpp:S3630) - Windows API requires reinterpret_cast to read USN from buffer

  // Parse the USN_RECORD_V2 structures
  // Use DWORD for offset to match the type of bytes_returned and avoid
  // signed/unsigned comparison warnings.
  //
  // Records are staged into a batch (parsing/filtering/MFT reads need no lock)
  // and committed with one lock acquisition per buffer via InsertBatch.
  std::vector<FileIndex::PopulationBatchEntry> batch;
  batch.reserve(1024U);  // NOLINT(readability-magic-numbers) - heuristic: ~500-1000 records per 256KB buffer
  DWORD offset = usn_record::SizeOfUsn();
  while (offset < bytes_ret) {
    // Use centralized safe parsing function with comprehensive validation
    // (buffer bounds, RecordLength, MajorVersion/MinorVersion, filename bounds)
    PUSN_RECORD_V2 record = nullptr;
    if (!usn_record::ValidateAndParseUsnRecord(
            buf.data(), bytes_ret, offset,
            record)) {
      // Conservative: same as UsnMonitor::ProcessOneBuffer. Do NOT skip ahead by
      // sizeof(USN_RECORD_V2) — that can land mid-record, pass a false MajorVersion=2
      // header, and Insert phantom FRNs that USN replay cannot clean up.
      // Drop the rest of this buffer; continue enumeration from the buffer's next USN.
      NoteCorruptRecord(offset);
      break;
    }

    // Record is validated and safe to use
    if (!StageRecord(record, batch)) {
      return false; // Error processing record
    }

    // Safe to advance: we've validated record->RecordLength fits within
    // buffer
    offset += record->RecordLength;
  }

  // Single lock acquisition for the whole buffer. Lock-wait vs. insert work is
  // split inside InsertBatch so profiles separate contention from hash cost.
  // MFT reads stay in the per-record span above (no lock held during I/O).
  if (!batch.empty()) {
    CommitBatch(batch);
  }

  // Update for next iteration
  position.Advance(next_usn);
  return true; // Continue enumeration
}

bool IndexBuildRun::Run(std::vector<char>& buffer,
                        usn_journal_enum_position::UsnJournalEnumerationPosition& position) {
  CTRACK_DEV_NAME("IndexBuildRun::Run");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  const auto buffer_size = static_cast<int>(buffer.size());
  bool enumeration_complete = false;
  while (!enumeration_complete) {
    DWORD bytes_returned = 0;
    if (!volume_gateway_.EnumUsnJournal(volume_handle_, position.Data(), buffer.data(),
                                 buffer_size, bytes_returned)) {
      if (const DWORD err = GetLastError(); err == ERROR_HANDLE_EOF) {
        LOG_INFO("Reached end of USN journal enumeration");
        enumeration_complete = true;
      } else {
        LOG_ERROR_BUILD("FSCTL_ENUM_USN_DATA failed with error: " << err);
        return false;
      }
    } else if (!ProcessBuffer(buffer, bytes_returned, position)) {
      enumeration_complete = true;
    } else {
      NoteIteration();
    }
  }
  // Before the caller's replay drain and RecomputePathsAndPruneOrphans.
  FinalizeDeferredRecycleEntries();
  return true;
}

bool IndexBuildRun::RunRawMft() {
  CTRACK_DEV_NAME("IndexBuildRun::RunRawMft");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  if (volume_handle_ == INVALID_HANDLE_VALUE || volume_handle_ == nullptr) {
    LOG_ERROR_BUILD("Invalid volume handle provided for raw MFT streaming");
    return false;
  }

  mft::MftExtentProvider extent_provider(volume_handle_, volume_gateway_);
  std::vector<mft::MftDiskExtent> extents;
  if (!extent_provider.GetMftExtents(extents)) {
    LOG_ERROR_BUILD("Failed to query MFT extents for raw indexing");
    return false;
  }

  mft::VolumeGeometry geom{};
  if (!extent_provider.GetVolumeGeometry(geom)) {
    LOG_ERROR_BUILD("Failed to query volume geometry for raw indexing");
    return false;
  }

  uint64_t total_extent_clusters = 0;
  for (const auto& extent : extents) {
    total_extent_clusters += extent.cluster_count;
  }
  const uint64_t total_mft_mb =
      (total_extent_clusters * geom.bytes_per_cluster) / (1024ULL * 1024ULL);
  LOG_IMPORTANT_BUILD("ColdIndexMftGeometry: bytes_per_sector=" << geom.bytes_per_sector
                      << ", bytes_per_cluster=" << geom.bytes_per_cluster
                      << ", bytes_per_file_record=" << geom.bytes_per_file_record);
  LOG_IMPORTANT_BUILD("ColdIndexMftGeometry: extent_count=" << extents.size()
                      << ", total_extent_clusters=" << total_extent_clusters
                      << ", total_mft_mb=" << total_mft_mb);

  auto volume_reader = std::make_unique<mft::Win32VolumeReader>(volume_handle_);
  mft::DoubleBufferedChunkReader chunk_reader(std::move(volume_reader));
  mft::RawMftRecordParser parser(geom.bytes_per_sector, geom.bytes_per_file_record);

  constexpr size_t kChunkSize = 4 * 1024 * 1024;  // 4 MB chunks (optimal for NVMe sequential DMA)
  if (!chunk_reader.StartStreaming(extents, kChunkSize)) {
    LOG_ERROR_BUILD("Failed to start raw MFT chunk streaming");
    return false;
  }

  mft::ChunkBuffer chunk{};
  mft::ParserStats stats{};
  while (chunk_reader.GetNextChunk(chunk)) {
    ++stats.chunks_seen;
    const mft::ChunkSpan span{chunk.data, chunk.size, chunk.first_record_number};
    std::vector<FileIndex::PopulationBatchEntry> batch_entries;
    const auto parse_start = std::chrono::steady_clock::now();
    if (!parser.ParseChunk(span, batch_entries, stats)) {
      LOG_WARNING_BUILD("Chunk parse error encountered at record " << chunk.first_record_number);
    }
    stats.parse_time_microseconds += ElapsedMicroseconds(parse_start);

    if (!batch_entries.empty()) {
      std::vector<FileIndex::PopulationBatchEntry> filtered_batch;
      filtered_batch.reserve(batch_entries.size());
      const auto filter_start = std::chrono::steady_clock::now();
      for (auto& entry : batch_entries) {
        // Note: pseudo-directory names and the volume root record are already
        // dropped by RawMftRecordParser (EmitEntriesForNames / ParseSingleRecord),
        // so no second filter pass is needed here.
        const std::string_view name_sv = entry.name;
        const system_path_filter::IngestionDisposition disposition = policy_.ClassifyEntry(
            name_sv, entry.id.raw, entry.parent_id.raw, entry.is_directory, kColdIngestionMode);
        // Deferred $-named entries are classified and staged or counted by
        // FinalizeDeferredRecycleEntries.
        if (RetainForFinalize(name_sv, entry.id.raw, entry.parent_id.raw, entry.is_directory)) {
          continue;
        }
        CollectRecycleProducer(name_sv, entry.id.raw, entry.parent_id.raw, entry.is_directory,
                               disposition);
        switch (disposition) {
          case system_path_filter::IngestionDisposition::IncludeNormal:
          case system_path_filter::IngestionDisposition::IncludeRecycleBin:
            break; // Stage below (containers and $R payloads are ingested, like normal entries)
          case system_path_filter::IngestionDisposition::ExcludeSystem:
            NoteFiltered();
            continue;
          case system_path_filter::IngestionDisposition::SkipRecycleMetadata:
            // INV-2 + A1: $I sidecars never enter FileIndex; the staged triple is handed to the
            // enricher by DrainRecycleBinProducers (queue-first, no $I reads on this thread).
            ++recycle_metadata_skipped_;
            NoteFiltered();
            continue;
        }
        filtered_batch.push_back(std::move(entry));
        ++stats.entries_staged;
        NoteStaged();
      }
      stats.filter_time_microseconds += ElapsedMicroseconds(filter_start);
      if (!filtered_batch.empty()) {
        const auto commit_start = std::chrono::steady_clock::now();
        CommitBatch(filtered_batch);
        stats.commit_time_microseconds += ElapsedMicroseconds(commit_start);
        ++stats.batches_committed;
        NoteProgress();
      }
      stats.entries_filtered += batch_entries.size() - filtered_batch.size();
    }
    NoteIteration();
  }

  // After all chunks are parsed, resolve spilled Win32 long names from extension records.
  // This heals folders like OneDrive that have complex attributes pushing their long name
  // into an extension record, leaving only the DOS name in the base record.
  if (const auto& extension_names = parser.GetExtensionNames(); !extension_names.empty()) {
    LOG_IMPORTANT_BUILD("Merging " << extension_names.size() << " long names from extension records...");
    const auto commit_ext_start = std::chrono::steady_clock::now();
    CommitExtensionNames(extension_names);
    stats.commit_time_microseconds += ElapsedMicroseconds(commit_ext_start);
  }

  // Before the telemetry below and before the caller's replay drain and
  // RecomputePathsAndPruneOrphans: the trackers and filtered counts must be final.
  // Finalize survivors were already counted as filtered per chunk (deferred entries sit in
  // batch_entries but not filtered_batch), so move them from filtered to staged here.
  const size_t finalize_survivors = FinalizeDeferredRecycleEntries();
  stats.entries_staged += finalize_survivors;
  stats.entries_filtered -= (std::min)(finalize_survivors, stats.entries_filtered);

  const auto reader_stats = chunk_reader.GetStats();
  LOG_IMPORTANT_BUILD("ColdIndexMftTelemetry: bytes_read_mb="
                      << reader_stats.total_bytes_read / (1024 * 1024)
                      << ", elapsed_s=" << std::fixed << std::setprecision(2)
                      << reader_stats.elapsed_seconds
                      << ", consumer_mb_s=" << reader_stats.transfer_rate_mb_s);
  LOG_IMPORTANT_BUILD("ColdIndexMftTelemetry: read_mb_s=" << reader_stats.read_rate_mb_s
                      << ", read_calls=" << reader_stats.read_calls
                      << ", read_time_ms=" << (reader_stats.read_time_microseconds / 1000.0));
  LOG_IMPORTANT_BUILD("ColdIndexMftTelemetry: records_parsed=" << stats.records_parsed
                      << ", base_records=" << stats.base_records
                      << ", extension_records=" << stats.extension_records);
  LOG_IMPORTANT_BUILD("ColdIndexMftTelemetry: chunks=" << stats.chunks_seen
                      << ", batches=" << stats.batches_committed
                      << ", entries_staged=" << stats.entries_staged);
  LOG_IMPORTANT_BUILD("ColdIndexMftTelemetry: total_files=" << total_files_
                      << ", filtered_files=" << filtered_count_
                      << ", filtered_entries=" << stats.entries_filtered);
  LOG_IMPORTANT_BUILD("ColdIndexMftTelemetry: fixup_failures=" << stats.fixup_failures
                      << ", parse_errors=" << stats.parse_errors
                      << ", short_reads=" << reader_stats.short_reads);
  LOG_IMPORTANT_BUILD("ColdIndexMftTelemetry: read_errors=" << reader_stats.read_errors
                      << ", producer_wait_ms="
                      << (reader_stats.producer_wait_microseconds / 1000.0)
                      << ", consumer_wait_ms="
                      << (reader_stats.consumer_wait_microseconds / 1000.0));
  LOG_IMPORTANT_BUILD("ColdIndexMftTelemetry: parse_time_ms="
                      << (stats.parse_time_microseconds / 1000.0)
                      << ", filter_time_ms=" << (stats.filter_time_microseconds / 1000.0)
                      << ", commit_time_ms=" << (stats.commit_time_microseconds / 1000.0));

  return true;
}

}  // namespace index_build_run

// Populates the FileIndex with all existing files on the volume
// using high-performance raw MFT chunk streaming.
// Returns true on success, false on failure.
bool PopulateInitialIndex(index_build_run::IndexBuildRun& run) {
  CTRACK_NAME("PopulateInitialIndex");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  ScopedTimer total_timer("PopulateInitialIndex - Total");

  run.Reserve();
  if (!run.RunRawMft()) {
    return false;
  }

  const index_build_run::IndexBuildRun::Result result = run.GetResult();
  LOG_INFO_BUILD("Index population completed - Total files: "
                 << result.files << ", Iterations: " << result.iterations
                 << ", Filtered: " << result.filtered);

  run.LogOutcome();
  run.FinalizeProgress();

  return true;
}
