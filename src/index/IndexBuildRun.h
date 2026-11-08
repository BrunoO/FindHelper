#pragma once

// Windows-only: USN journal baseline enumeration (FSCTL_ENUM_USN_DATA) runs on
// Windows only; do not include this header from cross-platform translation
// units (see the Windows-only sources in CMakeLists.txt). Including off
// Windows is a no-op (same pattern as usn/VolumeGateway.h).

#ifdef _WIN32

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <winioctl.h>

#include "index/IndexBatchTypes.h"
#include "index/SystemPathFilter.h"
#include "index/mft/DoubleBufferedChunkReader.h"
#include "index/mft/MftExtentProvider.h"
#include "index/mft/RawMftRecordParser.h"
#include "index/mft/RawVolumeReader.h"
#include "usn/VolumeGateway.h"

namespace recycle_bin {
class RecycleBinMetadataStore;
class RecycleBinEnricher;
}  // namespace recycle_bin

namespace usn_journal_enum_position {

// One FSCTL_ENUM_USN_DATA cursor: where enumeration stands
// (StartFileReferenceNumber) plus the fixed USN range (LowUsn/HighUsn).
// Wraps MFT_ENUM_DATA_V0 so the advancing cursor is not a loose variable
// and StartFileReferenceNumber cannot be confused with a record identity.
class UsnJournalEnumerationPosition {
 public:
  [[nodiscard]] static UsnJournalEnumerationPosition Start() {
    UsnJournalEnumerationPosition position;
#ifndef MAXLONGLONG
    static constexpr int64_t kMaxUsnFallback = 0x7FFFFFFFFFFFFFFFLL;
    position.data_.HighUsn = kMaxUsnFallback;
#else
    position.data_.HighUsn = MAXLONGLONG;
#endif  // MAXLONGLONG
    return position;
  }

  void Advance(USN next_usn) { data_.StartFileReferenceNumber = next_usn; }

  MFT_ENUM_DATA_V0& Data() noexcept { return data_; }

 private:
  MFT_ENUM_DATA_V0 data_{};
};

}  // namespace usn_journal_enum_position

namespace index_build_run {

// One MFT baseline run: reserve, stage batches, commit, hand over to the
// live tail. Glossary term: IndexBuildRun
// (docs/design/USN_MFT_UBIQUITOUS_LANGUAGE.md).
//
// Replaces the PopulationContext aggregate plus the scattered locals in
// PopulateInitialIndex (MFT counters, file/iteration totals, progress time).
// Out-param progress becomes Result: callers read GetResult() instead of
// reaching into the run. The live indexed_file_count atomic stays caller-owned
// (the UI polls it during enumeration); the run updates it via NoteProgress.
// Recompute/Prune stay with the caller (UsnMonitor runs them post-population);
// staging and commit cross the lock boundary via CommitBatch, which applies
// one InsertBatch per enumeration buffer with no I/O under lock.
class IndexBuildRun {
 public:
  // Point-in-time outcome of the run. Copyable; replaces out-param progress.
  // filtered counts staged-but-skipped records: $-prefixed entries plus
  // transitive children of filtered directories (files and directories).
  struct Result {
    int files = 0;
    int iterations = 0;
    size_t filtered = 0;
    bool integrity_compromised = false;
  };

  // All references/pointers are caller-owned and must outlive the run:
  // file_index and policy for the whole call; the volume handle for
  // the whole call; the gateway for the whole call;
  // indexed_file_count and integrity_latch may be nullptr (progress/logging
  // degrade gracefully).
  IndexBuildRun(HANDLE volume_handle, volume_gateway::VolumeGateway& gateway,
                FileIndex& file_index,
                std::atomic<size_t>* indexed_file_count,
                system_path_filter::SystemPathFilterPolicy& policy,
                std::atomic<bool>* integrity_latch);

  IndexBuildRun(const IndexBuildRun&) = delete;
  IndexBuildRun& operator=(const IndexBuildRun&) = delete;
  IndexBuildRun(IndexBuildRun&&) = delete;
  IndexBuildRun& operator=(IndexBuildRun&&) = delete;
  ~IndexBuildRun() = default;

  // Phase: Reserve. Pre-size index maps + name arena from the MFT upper bound
  // and clear/reserve the policy trackers. Skips silently when the
  // volume query fails. Single-threaded; no contention.
  void Reserve();

  // Phase: Stage. Parse + filter one validated record into the batch
  // (parsing/filtering/MFT reads need no lock). Returns false to abort the
  // run; true records one staged entry (NoteStaged) via the caller.
  bool StageRecord(PUSN_RECORD_V2 record,
                   std::vector<index_batch::PopulationBatchEntry>& batch);

  // Phase: Stage + Commit one enumeration buffer. Stages each validated
  // record, commits the batch under a single lock acquisition per buffer via
  // CommitBatch, and advances the enumeration cursor. A corrupt record drops
  // the rest of the buffer (same loss set as the live apply path) and latches.
  // Returns false to end enumeration.
  bool ProcessBuffer(const std::vector<char>& buf, DWORD bytes_ret,
                     usn_journal_enum_position::UsnJournalEnumerationPosition& position);

  // Phase: Run. Streams the MFT using DoubleBufferedChunkReader + RawMftRecordParser.
  [[nodiscard]] bool RunRawMft();

  // Phase: Run (legacy USN enumeration). FSCTL_ENUM_USN_DATA until EOF or ProcessBuffer stops.
  // Returns false on ioctl failure. Kept for FakeVolumeGateway seam tests.
  [[nodiscard]] bool Run(std::vector<char>& buffer,
                         usn_journal_enum_position::UsnJournalEnumerationPosition& position);

  // CommitBatch applies a staged batch under a single lock acquisition
  // (no I/O inside — parsing/filtering/MFT reads happen before).
  // Path indexing is deferred: entries are stored without paths and the
  // caller must run RecomputePathsAndPruneOrphans — which delegates to
  // FileIndex::RecomputeAllPaths and indexes every path exactly once —
  // before the index is searched.
  void CommitBatch(const std::vector<index_batch::PopulationBatchEntry>& batch);

  // Directly merges recovered long names from extension records into the index
  void CommitExtensionNames(const std::vector<mft::RawMftRecordParser::ExtensionName>& names);

  void NoteStaged();
  void NoteIteration();
  void NoteFiltered();

  // Live progress for the UI (throttled to 1 Hz internally). No-op when the
  // caller passed a null counter.
  void NoteProgress();

  // Corrupt-record path: latch the caller-owned flag when present, always
  // ERROR-log (mirrors the central latch helper). Records the outcome flag.
  void NoteCorruptRecord(DWORD offset);

  // Outcome snapshot + statistics log.
  [[nodiscard]] Result GetResult() const;
  void LogOutcome() const;

  // Sync the live progress counter to the final index size after all inserts.
  // No-op when the caller passed a null counter.
  void FinalizeProgress();

  // Phase: Finalize (end of Run/RunRawMft, before the replay drain). Re-drives
  // retained directories and deferred $-named entries in FRN order until the trackers
  // are stable, then classifies each deferred entry once in kColdIngestionMode:
  // IncludeNormal and IncludeRecycleBin survivors are staged and committed; $I and
  // excluded entries are counted as filtered. Returns the number of survivors staged.
  size_t FinalizeDeferredRecycleEntries();

  // Recycle-bin producer sinks (Phase 4). Both pointers are non-owning and nullable; the
  // caller owns them and they must outlive the run. Null sinks make the drain a logged no-op.
  void SetRecycleBinSink(recycle_bin::RecycleBinMetadataStore* store,
                         recycle_bin::RecycleBinEnricher* enricher) noexcept;

  // Phase: Recycle producer drain (post-population). Feeds the triples collected during the
  // stream into the sinks: $R payloads to store.IngestRFiles, $I sidecars to
  // enricher.EnqueueIFile (absolute path resolved from FileIndex), <SID> directories to
  // enricher.EnqueueSidMarker. Clears the triples. Pure path/queue work: no $I file I/O here.
  // MUST run after RecomputePathsAndPruneOrphans: the paths it reads are only resolved there.
  void DrainRecycleBinProducers();

  // Phase: Recompute/Prune (post-population, after the replay drain).
  // Resolves every placeholder path and prunes orphans in one pass.
  // Owned here so the full baseline lifecycle reads on one object; the
  // caller (UsnMonitor) decides WHEN, after DrainReplayJournalEvents and
  // before clearing is_populating. Must NOT run concurrently with live
  // USN applies.
  void RecomputePathsAndPruneOrphans();

  [[nodiscard]] system_path_filter::SystemPathFilterPolicy& Policy() noexcept {
    return policy_;
  }

 private:
  // Directory retained during enumeration for FinalizeDeferredRecycleEntries.
  // Two stripped 48-bit record numbers: 16 bytes, no name. Only non-$ directories
  // are retained; their names carry no policy predicate (see RetainForFinalize).
  struct RetainedDirectory {
    uint64_t record_number;
    uint64_t parent_number;
  };
  static_assert(sizeof(RetainedDirectory) == 16, "RetainedDirectory must stay 16 bytes");

  // $-named entry whose parent is not a tracked <SID>/$R-folder. Its name is owned
  // (the parser and thread-local buffers are reused) and is needed for re-driving.
  // record_number / parent_number are stripped keys (ordering, tracker lookups).
  // file_reference / parent_file_reference are the raw FRNs with sequence bits: the
  // FileIndex key of a survivor's id / parent_id is the full FRN, so stripped values
  // must never be used for staging.
  struct DeferredEntry {
    std::string name;
    uint64_t record_number;
    uint64_t parent_number;
    uint64_t file_reference;
    uint64_t parent_file_reference;
    bool is_directory;
  };

  // Retains a non-$ directory, or defers a $-named entry whose parent is not a
  // tracked <SID>/$R-folder. Returns true when the entry was deferred: the caller
  // must neither classify-and-stage nor count it; FinalizeDeferredRecycleEntries does both.
  [[nodiscard]] bool RetainForFinalize(std::string_view name, uint64_t file_reference,
                                       uint64_t parent_file_reference, bool is_directory);

  // Recycle producer triple: owned name plus the FULL FRNs (sequence bits), the FileIndex
  // keys the drain uses to resolve paths. Staged in-stream, never acted on in-stream.
  struct RecycleProducerTriple {
    std::string name;
    uint64_t file_reference;
    uint64_t parent_file_reference;
    bool is_directory;
  };

  // Records one classified entry's recycle-producer role (see the .cpp for the mapping).
  // Called at every site that classifies an entry in kColdIngestionMode.
  void CollectRecycleProducer(std::string_view name, uint64_t file_reference,
                              uint64_t parent_file_reference, bool is_directory,
                              system_path_filter::IngestionDisposition disposition);

  HANDLE volume_handle_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  volume_gateway::VolumeGateway& volume_gateway_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  FileIndex& file_index_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::atomic<size_t>* indexed_file_count_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  system_path_filter::SystemPathFilterPolicy& policy_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::atomic<bool>* integrity_latch_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::chrono::steady_clock::time_point last_progress_update_time_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  int total_files_ = 0;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  int iterations_ = 0;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  size_t filtered_count_ = 0;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  size_t recycle_metadata_skipped_ = 0;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  bool integrity_hit_ = false;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::vector<RetainedDirectory> retained_directories_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::vector<DeferredEntry> deferred_entries_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  // Recycle producer sinks (non-owning, nullable; see SetRecycleBinSink).
  recycle_bin::RecycleBinMetadataStore* recycle_store_ = nullptr;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  recycle_bin::RecycleBinEnricher* recycle_enricher_ = nullptr;    // NOLINT(readability-identifier-naming) - project convention: snake_case_
  // $R payloads (IngestRFiles), $I sidecars (EnqueueIFile), <SID> directories (EnqueueSidMarker).
  // Filled in-stream by CollectRecycleProducer; drained once by DrainRecycleBinProducers.
  std::vector<RecycleProducerTriple> r_triples_;    // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::vector<RecycleProducerTriple> i_triples_;    // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::vector<RecycleProducerTriple> sid_triples_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
};

}  // namespace index_build_run

#endif  // _WIN32
