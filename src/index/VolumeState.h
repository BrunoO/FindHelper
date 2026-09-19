#pragma once

#include <cstdint>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

#include "index/FileIndex.h"
#include "index/NtfsFileReference.h"
#include "index/RemoveIndexedSubtree.h"
#include "usn/UsnRecord.h"
#include "utils/FileTimeTypes.h"

namespace volume_state {

// Aggregate root for the in-memory materialized view of one NTFS volume.
// Glossary term: VolumeState
// (docs/design/USN_MFT_UBIQUITOUS_LANGUAGE.md).
//
// Consistency boundary: entries + paths + awaiting set + filter cascade.
// One mutation per USN buffer under a single unique_lock on the underlying
// FileIndex mutex (see docs/design/USN_INGEST_PIPELINE_DESIGN.md and
// UsnMonitor::ProcessOneBuffer). This type makes that rule a method contract
// instead of a comment: per-buffer Apply* verbs require the caller to hold
// unique_lock; queries require shared_lock or unique_lock. No I/O under lock.
//
// Bulk baseline verbs (ApplyBaselineBatch / RecomputePathsAndPruneOrphans)
// are the exception: they acquire their own lock internally via
// FileIndex::InsertBatch / RecomputeAllPaths and MUST NOT be called while
// holding the per-buffer lock (shared_mutex is non-recursive; double-lock
// would deadlock). Do not call them from inside a ProcessOneBuffer lock scope.
//
// Delegates to FileIndex *Locked primitives — no new storage, no lock change.
// Future: cursor (JournalCursor) and integrity latch will join this aggregate;
// for now they remain in UsnMonitor and the per-buffer lock still covers the
// index side atomically.
class VolumeState {
 public:
  explicit VolumeState(FileIndex& file_index) noexcept : file_index_(file_index) {
  }

  VolumeState(const VolumeState&) = delete;
  VolumeState& operator=(const VolumeState&) = delete;
  VolumeState(VolumeState&&) = delete;
  VolumeState& operator=(VolumeState&&) = delete;
  ~VolumeState() = default;

  // Mutex that guards the aggregate. Callers hold unique_lock for per-buffer
  // Apply* and shared_lock/unique_lock for queries. Returned by reference so
  // the call site's lock scope is explicit (one lock per buffer, released
  // between buffers so search can acquire shared_lock). const-qualified on
  // purpose — locking does not mutate the aggregate's logical state.
  [[nodiscard]] std::shared_mutex& GetMutex() const noexcept {  // NOLINT(readability-identifier-naming) - const accessor returning mutable mutex is intentional per design
    return file_index_.GetMutex();
  }

  [[nodiscard]] FileIndex& GetFileIndex() noexcept {
    return file_index_;
  }
  [[nodiscard]] const FileIndex& GetFileIndex() const noexcept {
    return file_index_;
  }

  // Per-buffer domain verbs — caller MUST hold unique_lock on GetMutex().

  // Insert or update a file from a parsed USN record. Used for FILE_CREATE
  // and for rename-recovery inserts (unknown RENAME_NEW_NAME). Delegates to
  // FileIndex::InsertLocked which handles FRN registration, pending heal, and
  // the awaiting→healed path. The record USN rides along as usn_at_track so
  // awaiting entries carry their journal position (see InsertOptions). No I/O.
  void ApplyFileCreated(const usn_record::UsnRecord& record) {
    FileIndex::InsertOptions options{};
    options.usn_at_track = record.usn;
    file_index_.InsertLocked(record.self, record.parent, record.name,
                             record.is_directory, kFileTimeNotLoaded, options);
  }

  // Recovery insert for unknown RENAME_NEW_NAME (no UsnRecord). Same
  // InsertLocked path as ApplyFileCreated but with caller-named references.
  void ApplyFileCreatedRaw(ntfs_file_reference::NtfsFileReference file_reference,
                           ntfs_file_reference::NtfsFileReference parent_reference,
                           std::string_view name,
                           bool is_directory) {
    file_index_.InsertLocked(file_reference, parent_reference,
                             name, is_directory, kFileTimeNotLoaded);
  }

  // Remove a file by FRN. Delegates to FileIndex::RemoveLocked (also clears
  // awaiting buckets and path_to_id chains). No I/O.
  void ApplyFileDeleted(ntfs_file_reference::NtfsFileReference file_reference) {
    file_index_.RemoveLocked(file_reference);
  }

  // Rename within the same parent (bare-name change only). Returns false when
  // the entry was absent (concurrent delete in the same buffer). Delegates to
  // RenameLocked which rewrites the path and cascades to descendants.
  [[nodiscard]] bool ApplyFileRenamed(ntfs_file_reference::NtfsFileReference file_reference,
                                      std::string_view new_name) {
    return file_index_.RenameLocked(file_reference, new_name);
  }

  // Move to a different parent (record-number comparison handles stale
  // sequences). Returns false on miss (same-buffer delete). Delegates to
  // MoveLocked which rewrites the subtree paths.
  [[nodiscard]] bool ApplyFileMoved(ntfs_file_reference::NtfsFileReference file_reference,
                                    ntfs_file_reference::NtfsFileReference new_parent_reference) {
    return file_index_.MoveLocked(file_reference, new_parent_reference);
  }

  // Invalidate cached size after a DATA_* reason. No synchronous GetFileSize
  // — size reloads lazily on the display path. Returns false for directories
  // or missing entries. Delegates to InvalidateSizeLocked.
  [[nodiscard]] bool ApplyDataChange(ntfs_file_reference::NtfsFileReference file_reference) {
    return file_index_.InvalidateSizeLocked(file_reference.raw);
  }

  // Evict entries that slipped in before their parent was marked
  // $-filtered (live out-of-order CREATE). Delegates to
  // EvictDescendantsOfFilteredParentLocked; no-op for record 0 / root 5.
  void ApplyFilteredParentEviction(ntfs_file_reference::MftRecordNumber filtered_parent_record) {
    file_index_.EvictDescendantsOfFilteredParentLocked(filtered_parent_record);
  }

  // Remove a file and its indexed subtree (soft-delete to $Recycle.Bin).
  // Delegates to RemoveIndexedSubtreeLocked (bulk directory evacuation).
  void ApplySubtreeRemoval(ntfs_file_reference::NtfsFileReference file_reference) {
    RemoveIndexedSubtreeLocked(file_index_, file_reference);
  }

  // Evict placeholders that outlived the never-arriving threshold. Caller
  // MUST hold unique_lock (SweepNeverArriving acquires it before calling).
  // Log lines collected into out_log_lines (when non-null), never emitted.
  size_t ApplyNeverArrivingEviction(
      uint64_t max_age_ms,
      const index_domain_events::NeverArrivingEvictedSink& on_evicted = nullptr,
      std::vector<std::string>* out_log_lines = nullptr) {
    return file_index_.EvictNeverArrivingLocked(max_age_ms, on_evicted, out_log_lines);
  }

  // Evict placeholders tracked at least max_usn_gap behind current_usn in
  // journal order (OR-policy counterpart). Caller MUST hold unique_lock.
  // Log lines collected, never emitted (see above).
  size_t ApplyNeverArrivingEvictionByJournalGap(
      int64_t current_usn,
      int64_t max_usn_gap,
      const index_domain_events::NeverArrivingEvictedSink& on_evicted = nullptr,
      std::vector<std::string>* out_log_lines = nullptr) {
    return file_index_.EvictNeverArrivingByJournalGapLocked(current_usn, max_usn_gap,
                                                            on_evicted, out_log_lines);
  }

  // Bulk baseline (self-locking): inserts a batch and recomputes paths
  // under their own unique_lock acquisitions. MUST NOT be called while
  // holding the per-buffer lock (see class comment).
  void ApplyBaselineBatch(const std::vector<FileIndex::PopulationBatchEntry>& batch) {
    file_index_.InsertBatch(batch);
  }

  void RecomputePathsAndPruneOrphans() {
    file_index_.RecomputeAllPaths();
  }

  // Queries — caller MUST hold shared_lock or unique_lock on GetMutex()
  // (except Size() / GetAwaitingStats() which are lock-free / shared-lock
  // internally; routed through VolumeState for uniform aggregate adoption).
  [[nodiscard]] const FileEntry* GetEntry(
      ntfs_file_reference::NtfsFileReference file_reference) const {
    return file_index_.GetEntry(file_reference.raw);
  }

  [[nodiscard]] std::string_view GetPathView(
      ntfs_file_reference::NtfsFileReference file_reference) const {
    return file_index_.GetPathViewLockHeld(file_reference.raw);
  }

  [[nodiscard]] size_t Size() const {
    return file_index_.Size();
  }

  [[nodiscard]] IndexOperations::AwaitingStats GetAwaitingStats() const {
    return file_index_.GetAwaitingStats();
  }

  [[nodiscard]] size_t GetHealedAwaitingTotal() const {
    return file_index_.GetHealedAwaitingTotal();
  }

 private:
  FileIndex& file_index_;  // NOLINT(readability-identifier-naming) - project convention
};

}  // namespace volume_state
