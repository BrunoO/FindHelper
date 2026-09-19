#pragma once

// Windows-only: MFT baseline enumeration (FSCTL_ENUM_USN_DATA) runs on
// Windows only; do not include this header from cross-platform translation
// units (see the Windows-only sources in CMakeLists.txt). Including off
// Windows is a no-op (same pattern as usn/VolumeGateway.h and
// index/MftEnumerationPosition.h).

#ifdef _WIN32

#include <atomic>
#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <winioctl.h>

#include "index/FileIndex.h"
#include "index/MftEnumerationPosition.h"
#include "index/SystemPathFilter.h"
#include "index/mft/MftMetadataReader.h"
#include "usn/VolumeGateway.h"

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
    size_t mft_hits = 0;
    size_t mft_misses = 0;
    size_t mft_total = 0;
    bool integrity_compromised = false;
  };

  // All references/pointers are caller-owned and must outlive the run:
  // file_index and filtered_dirs for the whole call; the volume handle for
  // the whole call (the emplaced MftMetadataReader reads through it);
  // the gateway for the whole call (reserve query, enum, MFT reads);
  // indexed_file_count and integrity_latch may be nullptr (progress/logging
  // degrade gracefully).
  IndexBuildRun(HANDLE volume_handle, volume_gateway::VolumeGateway& gateway,
                FileIndex& file_index,
                std::atomic<size_t>* indexed_file_count,
                system_path_filter::FilteredDirTracker& filtered_dirs,
                std::atomic<bool>* integrity_latch,
                bool enable_mft_metadata_reading);

  IndexBuildRun(const IndexBuildRun&) = delete;
  IndexBuildRun& operator=(const IndexBuildRun&) = delete;
  IndexBuildRun(IndexBuildRun&&) = delete;
  IndexBuildRun& operator=(IndexBuildRun&&) = delete;
  ~IndexBuildRun() = default;

  // Phase: Reserve. Pre-size index maps + name arena from the MFT upper bound
  // and clear/reserve the filtered-directory tracker. Skips silently when the
  // volume query fails. Single-threaded; no contention.
  void Reserve();

  // Phase: Stage. Parse + filter one validated record into the batch
  // (parsing/filtering/MFT reads need no lock). Returns false to abort the
  // run; true records one staged entry (NoteStaged) via the caller.
  bool StageRecord(PUSN_RECORD_V2 record,
                   std::vector<FileIndex::PopulationBatchEntry>& batch);

  // Phase: Stage + Commit one enumeration buffer. Stages each validated
  // record, commits the batch under a single lock acquisition per buffer via
  // CommitBatch, and advances the enumeration cursor. A corrupt record drops
  // the rest of the buffer (same loss set as the live apply path) and latches.
  // Returns false to end enumeration.
  bool ProcessBuffer(const std::vector<char>& buf, DWORD bytes_ret,
                     mft_enum_position::MftEnumerationPosition& position);

  // Phase: Run. FSCTL_ENUM_USN_DATA until EOF or ProcessBuffer stops.
  // Returns false on ioctl failure.
  [[nodiscard]] bool Run(std::vector<char>& buffer,
                         mft_enum_position::MftEnumerationPosition& position);

  // CommitBatch applies a staged batch under a single lock acquisition
  // (no I/O inside — parsing/filtering/MFT reads happen before).
  void CommitBatch(const std::vector<FileIndex::PopulationBatchEntry>& batch);

  void NoteStaged();
  void NoteIteration();
  void NoteFiltered();

  // Live progress for the UI (throttled to 1 Hz internally). No-op when the
  // caller passed a null counter.
  void NoteProgress();

  // Optional MFT metadata read. Callers skip directories (metadata is for
  // files only); a null reader (disabled) also skips. Returns true on a
  // metadata hit; updates hit/miss/total counters.
  bool TryReadMetadata(uint64_t mft_record_number, FILETIME* out_time, uint64_t* out_size);

  // Corrupt-record path: latch the caller-owned flag when present, always
  // ERROR-log (mirrors the central latch helper). Records the outcome flag.
  void NoteCorruptRecord(DWORD offset);

  // Outcome snapshot + MFT statistics log. LogOutcome is a no-op when
  // metadata reading was disabled.
  [[nodiscard]] Result GetResult() const;
  void LogOutcome() const;

  // Sync the live progress counter to the final index size after all inserts.
  // No-op when the caller passed a null counter.
  void FinalizeProgress();

  // Phase: Recompute/Prune (post-population, after the replay drain).
  // Resolves every placeholder path and prunes orphans in one pass.
  // Owned here so the full baseline lifecycle reads on one object; the
  // caller (UsnMonitor) decides WHEN, after DrainReplayJournalEvents and
  // before clearing is_populating. Must NOT run concurrently with live
  // USN applies.
  void RecomputePathsAndPruneOrphans();

  [[nodiscard]] system_path_filter::FilteredDirTracker& FilteredDirs() noexcept {
    return filtered_dirs_;
  }

 private:
  HANDLE volume_handle_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  volume_gateway::VolumeGateway& volume_gateway_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  FileIndex& file_index_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::atomic<size_t>* indexed_file_count_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  system_path_filter::FilteredDirTracker& filtered_dirs_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::atomic<bool>* integrity_latch_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  bool enable_mft_metadata_reading_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::optional<MftMetadataReader> mft_reader_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::chrono::steady_clock::time_point last_progress_update_time_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  int total_files_ = 0;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  int iterations_ = 0;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  size_t filtered_count_ = 0;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  size_t mft_success_count_ = 0;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  size_t mft_failure_count_ = 0;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  size_t mft_total_files_ = 0;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  bool integrity_hit_ = false;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
};

}  // namespace index_build_run

#endif  // _WIN32
