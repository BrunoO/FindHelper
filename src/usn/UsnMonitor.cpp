#include "usn/UsnMonitor.h"

#include "ctrack.hpp"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#include <cassert>
#include "platform/windows/PrivilegeUtils.h"
#include "utils/LoggingUtils.h"
#include "utils/ScopedHandle.h"
#endif  // _WIN32

#include "index/IndexBuildRun.h"
#include "index/NtfsFileReference.h"
#include "index/RemoveIndexedSubtree.h"
#include "index/SystemPathFilter.h"
#include "index/VolumeState.h"
#include "usn/UsnIntegrityLatch.h"
#include "usn/UsnReason.h"
#include "usn/UsnRecord.h"
#include "usn/UsnRecordUtils.h"
#include "utils/FileSystemUtils.h"
#include "utils/StringUtils.h"

#ifdef _WIN32
// UsnReason bit contract: values below mirror winioctl.h USN_REASON_*.
// Fail loudly on SDK drift instead of silently mis-filtering.
static_assert(static_cast<uint32_t>(usn_reason::UsnReason::DataOverwrite) ==
              USN_REASON_DATA_OVERWRITE);
static_assert(static_cast<uint32_t>(usn_reason::UsnReason::DataExtend) ==
              USN_REASON_DATA_EXTEND);
static_assert(static_cast<uint32_t>(usn_reason::UsnReason::DataTruncation) ==
              USN_REASON_DATA_TRUNCATION);
static_assert(static_cast<uint32_t>(usn_reason::UsnReason::FileCreate) ==
              USN_REASON_FILE_CREATE);
static_assert(static_cast<uint32_t>(usn_reason::UsnReason::FileDelete) ==
              USN_REASON_FILE_DELETE);
static_assert(static_cast<uint32_t>(usn_reason::UsnReason::RenameOldName) ==
              USN_REASON_RENAME_OLD_NAME);
static_assert(static_cast<uint32_t>(usn_reason::UsnReason::RenameNewName) ==
              USN_REASON_RENAME_NEW_NAME);
static_assert(static_cast<uint32_t>(usn_reason::UsnReason::Close) == USN_REASON_CLOSE);
#endif  // _WIN32

namespace {

struct UsnProcessingState {
  volume_state::VolumeState& volume_state;
  UsnMonitorMetrics& metrics;
  std::atomic<bool>& integrity_compromised;
  system_path_filter::FilteredDirTracker& filtered_dir_ref_nums;
  UsnActivityTracker* activity_tracker = nullptr;
  index_domain_events::FileEventSink on_file_event;
  index_domain_events::IntegrityEventSink on_integrity_event;  // null disables; RenameDivergence publisher
};
// Metrics subscription for file-lifecycle events: the single counter rule
// (one fetch_add per published fact). Covered pattern-wise by
// IndexDomainEventsTests (local counters); this switch itself is verified
// by the Windows build + metrics assertions.
static_assert(std::variant_size_v<index_domain_events::FileLifecycleEvent> == 4,
              "CountFileLifecycleEvent must handle every lifecycle alternative");
void CountFileLifecycleEvent(UsnMonitorMetrics& metrics,
                             const index_domain_events::FileLifecycleEvent& event) {
  std::visit(
      [&metrics](const auto& file_event) {
        using Event = std::decay_t<decltype(file_event)>;
        if constexpr (std::is_same_v<Event, index_domain_events::FileCreated>) {
          metrics.files_created.fetch_add(1);
        } else if constexpr (std::is_same_v<Event, index_domain_events::FileDeleted>) {
          metrics.files_deleted.fetch_add(1);
        } else if constexpr (std::is_same_v<Event, index_domain_events::FileRenamed>) {
          metrics.files_renamed.fetch_add(1);
        } else if constexpr (std::is_same_v<Event, index_domain_events::FileModified>) {
          metrics.files_modified.fetch_add(1);
        } else {
          assert(false && "unhandled FileLifecycleEvent alternative");
        }
      },
      event);
}

// Guarded publication: an empty sink would throw std::bad_function_call
// mid-mutation (under the index lock). Single choke point so every publish
// site shares the null convention of the evict path.
#ifndef NDEBUG
// Debug-only sink-budget tripwire: synchronous sinks run under the index
// lock, where only atomic / reserved-capacity work is allowed (see
// IndexDomainEvents.h). A sink exceeding the budget logs here — loud in
// debug/tests, zero cost in release. Warning-only (not assert): a context
// switch mid-sink on a loaded machine could trip it spuriously.
// Covers the lifecycle/integrity choke points below (the per-record hot
// path); ParentHealed / NeverArrivingEvicted sinks are invoked directly at
// their rarer sites and rely on the documented contract.
constexpr std::chrono::milliseconds kSinkTimeBudgetMs{5};
inline void CheckSinkBudget(std::string_view family,
                            std::chrono::steady_clock::time_point start) {
  if (const auto elapsed = std::chrono::steady_clock::now() - start;
      elapsed > kSinkTimeBudgetMs) {
    // Throttled to one emission per process: the diagnostic itself runs
    // under the index lock, so it must be bounded even on the violation
    // path. The signal (a sink blew the budget) is preserved; repeats add
    // no information.
    static std::atomic budget_warning_emitted{false};
    if (!budget_warning_emitted.exchange(true)) {
      LOG_WARNING_BUILD("Synchronous domain-event sink (" << family
                        << ") exceeded 5ms under the index lock; sinks must be atomic-only");
    }
  }
}
#endif  // NDEBUG
inline void PublishFileEvent(const index_domain_events::FileEventSink& sink,
                             index_domain_events::FileLifecycleEvent event) {
  if (sink) {
#ifndef NDEBUG
    const auto sink_start = std::chrono::steady_clock::now();
#endif  // NDEBUG
    sink(event);
#ifndef NDEBUG
    CheckSinkBudget("lifecycle", sink_start);
#endif  // NDEBUG
  }
}

// Guarded integrity publication for under-lock paths (rename divergence):
// same null convention; sinks must not throw (see IndexDomainEvents.h).
// Named IfSet (not PublishIntegrityEvent) to avoid colliding with the
// UsnMonitor member below — an unqualified call inside a member function
// would otherwise silently resolve to the member.
inline void PublishIntegrityEventIfSet(const index_domain_events::IntegrityEventSink& sink,
                                        index_domain_events::IntegrityEvent event) {
  if (sink) {
#ifndef NDEBUG
    const auto sink_start = std::chrono::steady_clock::now();
#endif  // NDEBUG
    sink(event);
#ifndef NDEBUG
    CheckSinkBudget("integrity", sink_start);
#endif  // NDEBUG
  }
}

// Forward declarations for helper functions
void ProcessUsnRecordReasons(const usn_record::UsnRecord& record,
                             UsnProcessingState& processing_state);

void HandleFileRename(const usn_record::UsnRecord& record,
                      UsnProcessingState& processing_state);

void HandleRenameNewName(const usn_record::UsnRecord& record,
                         UsnProcessingState& processing_state);

// Phase-0 partition predicate for the per-buffer two-phase apply (see
// ProcessOneBuffer): directory CREATE records establish the parent links
// that same-buffer children join onto. Renames keep the FRN (awaiting buckets
// are keyed by record number, so they need no phase-1 priority), and
// directory DELETEs stay in phase 2 so create-then-delete in one buffer
// still nets to deleted under the single exclusive lock.
[[nodiscard]] bool IsParentEstablishingRecord(const USN_RECORD_V2* record) {  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - PascalCase; check misclassifies anonymous-namespace function as global
  // Same predicate as UsnRecord::IsParentEstablishing (raw form: partition
  // runs before name conversion, so no UsnRecord exists yet).
  return usn_record::EstablishesParent(
      usn_reason::ReasonSet(record->Reason),
      (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

// Helper function to update atomic max using compare-and-swap
template <typename T>
void UpdateAtomicMax(std::atomic<T>& atomic_var, T new_value) {  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - PascalCase; check misclassifies anonymous-namespace function as global
  T current = atomic_var.load();
  // compare_exchange_weak reloads `current` on failure; empty body is intentional.
  while (new_value > current &&
         !atomic_var.compare_exchange_weak(current, new_value)) {
    // Retry until max is stored or another thread wrote a larger value.
  }
}

[[nodiscard]] size_t QueueDepthDiagnosticBand(size_t queue_size) {
  if (queue_size >= usn_monitor_constants::kQueueDepthBand9000) {
    return usn_monitor_constants::kQueueDepthBand9000;
  }
  if (queue_size >= usn_monitor_constants::kQueueDepthBand1000) {
    return usn_monitor_constants::kQueueDepthBand1000;
  }
  if (queue_size >= usn_monitor_constants::kQueueDepthBand100) {
    return usn_monitor_constants::kQueueDepthBand100;
  }
  return 0;
}

// Log once per rising band (100 / 1000 / 9000) with process metrics for backlog repros.
void MaybeLogQueueDepthBandCrossing(const UsnMonitorMetrics& metrics, size_t queue_size,
                                    size_t& last_band_logged) {
  if (queue_size < usn_monitor_constants::kQueueWarningThreshold) {
    last_band_logged = 0;
    return;
  }
  const size_t band = QueueDepthDiagnosticBand(queue_size);
  if (band == 0 || band <= last_band_logged) {
    return;
  }
  last_band_logged = band;
  LOG_WARNING_BUILD(
      "USN queue depth crossed " << band << " (current=" << queue_size
      << ", files_modified=" << metrics.files_modified.load()
      << ", buffers_processed=" << metrics.buffers_processed.load()
      << ", total_process_time_ms=" << metrics.total_process_time_ms.load()
      << ")");
}

// Returns true if the file should be filtered out (name starts with '$' or parent
// is a filtered system directory like $Recycle.Bin / $Extend).
// Side effects: removes the entry from the index (safety net for any $-file
// that slipped in during initial population), updates delete metrics, and maintains
// the filtered_dir_ref_nums tracker.
// RENAME_NEW_NAME to a $-prefixed name is NOT filtered here — that path is a
// Recycle Bin soft-delete and is handled in HandleRenameNewName instead.
// Does NOT touch offset — offset management is the caller's responsibility.
bool HandleSystemFileFilter(const USN_RECORD_V2* record, std::string_view filename,
                            volume_state::VolumeState& volume_state,
                            const index_domain_events::FileEventSink& on_file_event,
                            system_path_filter::FilteredDirTracker& filtered_dir_ref_nums) {
  const bool is_system_name = system_path_filter::IsSystemPrefixedName(filename);
  const uint64_t file_record_num = ntfs_file_reference::RecordNumber(record->FileReferenceNumber);
  const uint64_t parent_record_num = ntfs_file_reference::RecordNumber(record->ParentFileReferenceNumber);

  if (const bool is_child_of_filtered = filtered_dir_ref_nums.IsFilteredChild(
          system_path_filter::FilteredDirectory(parent_record_num));
      !is_system_name && !is_child_of_filtered) {
    return false;
  }

  const bool is_directory = (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

  // Soft-delete to $Recycle.Bin arrives as RENAME_NEW_NAME ($R…). Let
  // ProcessUsnRecordReasons / HandleRenameNewName treat it as a logical delete.
  if (is_system_name &&
      usn_reason::ReasonSet(record->Reason).Includes(usn_reason::UsnReason::RenameNewName)) {
    return false;
  }

  // If this is a directory in a filtered subtree, record it so grandchildren are also filtered.
  // Evict descendants first: with ReturnOnlyOnClose a child CREATE can be
  // processed before its parent is marked filtered, leaving entries that the
  // never-inserted parent can neither heal nor remove (caller holds the
  // unique_lock; Evict...Locked collects before mutating).
  if (is_directory) {
    filtered_dir_ref_nums.MarkFilteredDir(system_path_filter::FilteredDirectory(file_record_num));
    volume_state.ApplyFilteredParentEviction(
        ntfs_file_reference::MftRecordNumber(file_record_num));
  }

  // Count genuine delete events even though we skip the record.
  if (usn_reason::ReasonSet(record->Reason).Includes(usn_reason::UsnReason::FileDelete)) {
    PublishFileEvent(on_file_event, index_domain_events::FileDeleted{record->FileReferenceNumber});
    filtered_dir_ref_nums.EraseOnDelete(system_path_filter::FilteredDirectory(file_record_num));
  }

  // Evict from index on any filtered event (safety net in case a file slipped in).
  volume_state.ApplyFileDeleted(ntfs_file_reference::NtfsFileReference(record->FileReferenceNumber));
  return true;
}

// Helper function to process interesting USN record.
// Returns true if the record was filtered (caller should advance offset and
// continue). Returns false if the record was processed normally.
// Does NOT touch offset — offset management belongs in ProcessOneBuffer.
bool ProcessInterestingUsnRecord(PUSN_RECORD_V2 record,
                                 volume_state::VolumeState& aggregate,
                                 UsnMonitorMetrics& metrics,  // NOLINT(misc-const-correctness) - false positive: metrics atomics are mutated (fetch_add); const would not compile
                                 std::atomic<bool>& integrity_compromised,
                                 system_path_filter::FilteredDirTracker& filtered_dir_ref_nums,
                                 UsnActivityTracker* activity_tracker,
                                 const index_domain_events::IntegrityEventSink& on_integrity_event = nullptr) {
  const wchar_t* const wfilename =
      reinterpret_cast<wchar_t*>(reinterpret_cast<std::byte*>(record) + record->FileNameOffset);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR(cpp:S3630) - USN_RECORD_V2 variable-offset filename; bounds checked by ValidateAndParseUsnRecord
  const size_t wchar_len = record->FileNameLength / sizeof(wchar_t);
  const std::string_view filename = WideToUtf8ThreadLocal(wfilename, wchar_len);

  auto filter_sink = [&metrics](const index_domain_events::FileLifecycleEvent& event) {
    CountFileLifecycleEvent(metrics, event);
  };
  // Filter out NTFS system files ($MFT, $LogFile, $Bitmap, $Recycle.Bin
  // artefacts, etc.) and their subtrees (e.g. per-user SID subfolders).
  if (HandleSystemFileFilter(record, filename, aggregate, filter_sink, filtered_dir_ref_nums)) {
    return true;
  }

  const uint64_t file_ref_num = record->FileReferenceNumber;
  const uint64_t parent_ref_num = record->ParentFileReferenceNumber;

  // Update FileIndex based on the reason (extracted to reduce nesting depth)
  const bool is_directory =
      (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  const usn_record::UsnRecord record_context{
      ntfs_file_reference::NtfsFileReference(file_ref_num),
      ntfs_file_reference::NtfsFileReference(parent_ref_num),
      filename,
      static_cast<int64_t>(record->Usn),
      usn_reason::ReasonSet(record->Reason),
      is_directory,};
  UsnProcessingState processing_state{aggregate, metrics, integrity_compromised, filtered_dir_ref_nums,
                                      activity_tracker,
                                      [&metrics](const index_domain_events::FileLifecycleEvent& event) {
                                        CountFileLifecycleEvent(metrics, event);
                                      },
                                      on_integrity_event};
  ProcessUsnRecordReasons(record_context, processing_state);

  metrics.records_processed.fetch_add(1);
  return false;  // Record processed normally
}

// Invalidate cached size for a DATA_* change. No synchronous size I/O on the
// processor thread (stalls journal drain). Misses are expected when DELETE
// preceded DATA_* in the same buffer — throttle the log. VolumeState verb:
// ApplyDataChange delegates to InvalidateSizeLocked (no I/O under lock).
void InvalidateSizeForDataChange(const usn_record::UsnRecord& record_context,
                                 UsnProcessingState& processing_state) {
  if (processing_state.volume_state.ApplyDataChange(record_context.self)) {
    PublishFileEvent(processing_state.on_file_event,
                     index_domain_events::FileModified{record_context.self.raw});
    if (processing_state.activity_tracker != nullptr) {
      std::string path(processing_state.volume_state.GetPathView(record_context.self));
      if (!path.empty()) {
        processing_state.activity_tracker->RecordChange(UsnChangeType::Modified, std::move(path));
      }
    }
    return;
  }
  static std::atomic<size_t> s_invalidate_miss_count{0};
  const size_t miss_count = s_invalidate_miss_count.fetch_add(1) + 1;
  constexpr size_t kInvalidateMissLogInterval = 1000;
  if (miss_count == 1 || (miss_count % kInvalidateMissLogInterval) == 0) {
    LOG_WARNING_BUILD(
        "USN: InvalidateSize missed entry (count=" << miss_count
        << ", ref=" << record_context.self.raw
        << ") — expected when DELETE preceded DATA_* in the same buffer");
  }
  PublishFileEvent(processing_state.on_file_event,
                     index_domain_events::FileModified{record_context.self.raw});
}

// Helper function to process USN record reasons and update FileIndex
// Extracted to reduce nesting depth in ProcessorThread()
void ProcessUsnRecordReasons(const usn_record::UsnRecord& record_context,
                             UsnProcessingState& processing_state) {
  const usn_reason::ReasonSet reasons = record_context.reasons;
  // VolumeState aggregate: all mutations below run under the single per-buffer
  // unique_lock held by ProcessOneBuffer (see VolumeState contract).
  auto& aggregate = processing_state.volume_state;
  // CREATE|DELETE on one close record (temp file, supersede, atomic replace):
  // DELETE wins. Independent ifs would Insert then Remove and can leave the index
  // wrong for short-lived create+delete; Remove-only matches "file is gone".
  if (reasons.Includes(usn_reason::UsnReason::FileDelete)) {
    std::string path(aggregate.GetPathView(record_context.self));
    if (path.empty()) {
      path = std::string(record_context.name);
    }
    aggregate.ApplyFileDeleted(record_context.self);
    PublishFileEvent(processing_state.on_file_event,
                     index_domain_events::FileDeleted{record_context.self.raw});
    if (processing_state.activity_tracker != nullptr) {
      processing_state.activity_tracker->RecordChange(UsnChangeType::Deleted, std::move(path));
    }
  } else if (reasons.Includes(usn_reason::UsnReason::FileCreate)) {
    aggregate.ApplyFileCreated(record_context);
    PublishFileEvent(processing_state.on_file_event,
                     index_domain_events::FileCreated{record_context.self.raw});
    if (processing_state.activity_tracker != nullptr) {
      std::string path(aggregate.GetPathView(record_context.self));
      if (path.empty()) {
        path = std::string(record_context.name);
      }
      processing_state.activity_tracker->RecordChange(UsnChangeType::Created, std::move(path));
    }
  }
  if (reasons.Includes(usn_reason::UsnReason::RenameNewName)) {
    HandleRenameNewName(record_context, processing_state);
  } else if (reasons.Includes(usn_reason::UsnReason::RenameOldName)) {
    // With ReturnOnlyOnClose, close records normally carry NEW_NAME (final path).
    // OLD_NAME-only can appear with split OLD/NEW pairs or another journal reader;
    // we intentionally ignore it (no old-name buffer). Surface for diagnosis.
    LOG_IMPORTANT_BUILD(
        "USN: RENAME_OLD_NAME without RENAME_NEW_NAME (ref="
        << record_context.self.raw << ", name=" << record_context.name
        << ") — index may keep a stale path until a NEW_NAME event arrives");
  }
  if (reasons.Intersects(usn_reason::kDataChangeReasons) &&
      !reasons.Includes(usn_reason::UsnReason::FileDelete)) {
    // Same close record often carries DELETE|DATA_*: Remove already ran above.
    // Skip invalidate — entry is gone by design (avoids ERROR spam under delete storms).
    InvalidateSizeForDataChange(record_context, processing_state);
  }
}

// Helper: handle the case where a RENAME event arrived for an unknown file ID.
// Returns true if the entry was recovered via a CREATE-like insert; returns
// false when the record was skipped as a system/filtered-dir artifact (the
// caller must then not report the file as created).
bool HandleRenameForUnknownEntry(uint64_t file_ref_num, uint64_t parent_ref_num,
                                 std::string_view filename, bool is_directory,
                                 volume_state::VolumeState& volume_state,
                                 const index_domain_events::FileEventSink& on_file_event,
                                 const system_path_filter::FilteredDirTracker& filtered_dir_ref_nums) {
  // A RENAME event arrived for a file ID not in the index. During normal
  // monitoring this should not happen: the CREATE event should always precede
  // a RENAME. However, gaps can occur due to journal overflows or filtered indexing.
  const bool is_system_name = system_path_filter::IsSystemPrefixedName(filename);
  const uint64_t parent_record_num = ntfs_file_reference::RecordNumber(parent_ref_num);

  if (const bool is_child_of_filtered = filtered_dir_ref_nums.IsFilteredChild(
          system_path_filter::FilteredDirectory(parent_record_num));
      is_system_name || is_child_of_filtered) {
    LOG_WARNING_BUILD("RENAME for unknown ID " << file_ref_num << " (" << filename
                      << "): skipped recovery insert as system/recycled artifact");
    return false;
  }

  LOG_WARNING_BUILD("RENAME for unknown ID " << file_ref_num << " (" << filename
                    << ", parent_ref=" << parent_ref_num
                    << "): treating as CREATE (recovery path - audited)");
  // Treat as create to keep the index consistent. VolumeState verb.
  volume_state.ApplyFileCreatedRaw(ntfs_file_reference::NtfsFileReference(file_ref_num),
                                   ntfs_file_reference::NtfsFileReference(parent_ref_num),
                                   filename, is_directory);
  PublishFileEvent(on_file_event, index_domain_events::FileRenamed{file_ref_num});
  return true;
}

// Helper: handle a move (parent changed) and optional rename in one operation.
void HandleMoveAndOptionalRename(uint64_t file_ref_num, uint64_t parent_ref_num,
                                 std::string_view filename,
                                 volume_state::VolumeState& volume_state,
                                 std::atomic<bool>& integrity_compromised,
                                 const index_domain_events::IntegrityEventSink& on_integrity_event = nullptr) {
  // Update parent first, then update name if it also changed.
  // Caller holds unique_lock (ProcessOneBuffer batch apply). VolumeState verbs.
  if (!volume_state.ApplyFileMoved(ntfs_file_reference::NtfsFileReference(file_ref_num),
                                ntfs_file_reference::NtfsFileReference(parent_ref_num))) {
    // entry_exists was true moments ago; Move failing almost always means a
    // DELETE in the same buffer already removed the entry. Do not latch
    // integrity and do not continue to GetPathView/Rename on a missing id.
    static std::atomic<size_t> s_move_miss_count{0};
    const size_t miss_count = s_move_miss_count.fetch_add(1) + 1;
    if (constexpr size_t kMoveMissLogInterval = 1000;
        miss_count == 1 || (miss_count % kMoveMissLogInterval) == 0) {
      LOG_WARNING_BUILD(
          "USN: Move missed entry (count=" << miss_count << ", ref=" << file_ref_num
          << ", new_parent=" << parent_ref_num
          << ") — expected when DELETE preceded RENAME in the same buffer");
    }
    return;
  }
  // Derive current name from PathStorage and check if it also changed.
  const std::string_view current_path = volume_state.GetPathView(ntfs_file_reference::NtfsFileReference(file_ref_num));
  const size_t last_sep = current_path.find_last_of("/\\");
  if (const std::string_view current_name = (last_sep != std::string_view::npos)
                                                 ? current_path.substr(last_sep + 1)
                                                 : current_path;
      current_name == filename) {
    return;
  }
  if (volume_state.ApplyFileRenamed(ntfs_file_reference::NtfsFileReference(file_ref_num), filename)) {
    return;
  }
  // Rename failed after a successful Move. Concurrent delete between the two
  // calls is expected under delete storms — only latch integrity if the entry
  // is still present (true rename failure / index divergence).
  // NOTE: logging under the FileIndex unique_lock preserves pre-existing
  // behavior; see usn/UsnIntegrityLatch.h.
  if (volume_state.GetEntry(ntfs_file_reference::NtfsFileReference(file_ref_num)) == nullptr) {
    return;
  }
  std::ostringstream reason;
  reason << "USN: Rename (during move) failed for ref=" << file_ref_num
         << ", new_name=" << filename << ". Index may be stale.";
  usn_integrity_latch::LatchIntegrityCompromised(integrity_compromised,
                                                 reason.str());
  PublishIntegrityEventIfSet(on_integrity_event,
                             index_domain_events::RenameDivergence{file_ref_num});
}

// Soft-delete to Recycle Bin is a rename to $R… under $Recycle.Bin, not a
// FILE_DELETE. Evict immediately so the entry does not linger until the bin is
// emptied. Mirrors InitialIndexPopulator's exclusion of $Recycle.Bin contents.
void HandleRenameNewName(const usn_record::UsnRecord& record_context,
                         UsnProcessingState& processing_state) {
  if (system_path_filter::IsSystemPrefixedName(record_context.name) ||
      processing_state.filtered_dir_ref_nums.IsFilteredChild(
          system_path_filter::FilteredDirectory::FromFileReference(
              record_context.parent.raw))) {
    std::string old_path(processing_state.volume_state.GetPathView(record_context.self));
    processing_state.volume_state.ApplySubtreeRemoval(record_context.self);
    PublishFileEvent(processing_state.on_file_event,
                     index_domain_events::FileDeleted{record_context.self.raw});
    if (processing_state.activity_tracker != nullptr) {
      processing_state.activity_tracker->RecordChange(UsnChangeType::Deleted, std::move(old_path));
    }
    return;
  }
  HandleFileRename(record_context, processing_state);
}

// Helper function to handle file rename operations.
// Caller MUST hold unique_lock on FileIndex (ProcessOneBuffer batch apply).
void HandleFileRename(const usn_record::UsnRecord& record_context,
                      UsnProcessingState& processing_state) {
  uint64_t current_parent = 0;
  bool entry_exists = false;
  std::string old_path;
  if (const FileEntry* const entry =
          processing_state.volume_state.GetEntry(record_context.self);
      entry != nullptr) {
    entry_exists = true;
    current_parent = entry->parentID.raw;
    old_path = std::string(processing_state.volume_state.GetPathView(record_context.self));
  }

  if (!entry_exists) {
    const bool inserted_by_recovery =
        HandleRenameForUnknownEntry(record_context.self.raw,
                                record_context.parent.raw,
                                record_context.name,
                                record_context.is_directory,
                                processing_state.volume_state,
                                processing_state.on_file_event,
                                processing_state.filtered_dir_ref_nums);
    if (inserted_by_recovery && processing_state.activity_tracker != nullptr) {
      std::string new_path(processing_state.volume_state.GetPathView(record_context.self));
      if (new_path.empty()) {
        new_path = std::string(record_context.name);
      }
      processing_state.activity_tracker->RecordChange(UsnChangeType::Created, std::move(new_path));
    }
    return;
  }
  if (!ntfs_file_reference::SameRecordNumber(current_parent,
                                             record_context.parent.raw)) {
    // Parent MFT record changed - this is a move (with optional rename).
    // Compare by record number only: USN ParentFileReferenceNumber often has a
    // stale sequence while the index stores the canonical parent FRN.
    HandleMoveAndOptionalRename(record_context.self.raw,
                                record_context.parent.raw,
                                record_context.name,
                                processing_state.volume_state,
                                processing_state.integrity_compromised,
                                processing_state.on_integrity_event);
  } else {
    // Same parent record (sequence may differ) - name-only rename.
    if (!processing_state.volume_state.ApplyFileRenamed(record_context.self,
                                                        record_context.name)) {
      // entry_exists was true moments ago; Rename failing means the entry was
      // concurrently removed. If this fires for any other reason it is divergence.
      // NOTE: logging under the FileIndex unique_lock preserves pre-existing
      // behavior; see usn/UsnIntegrityLatch.h.
      std::ostringstream reason;
      reason << "USN: Rename failed for ref=" << record_context.self.raw
             << ", new_name=" << record_context.name
             << ". Index may be stale.";
      usn_integrity_latch::LatchIntegrityCompromised(
          processing_state.integrity_compromised, reason.str());
      PublishIntegrityEventIfSet(processing_state.on_integrity_event,
                                 index_domain_events::RenameDivergence{record_context.self.raw});
    }
  }
  PublishFileEvent(processing_state.on_file_event,
                     index_domain_events::FileRenamed{record_context.self.raw});
  if (processing_state.activity_tracker != nullptr) {
    std::string new_path(processing_state.volume_state.GetPathView(record_context.self));
    if (new_path.empty()) {
      new_path = std::string(record_context.name);
    }
    processing_state.activity_tracker->RecordChange(UsnChangeType::Renamed, std::move(new_path), std::move(old_path));
  }
}

}  // anonymous namespace

// UsnMonitor class implementation

UsnMonitor::UsnMonitor(FileIndex &file_index, const MonitoringConfig &config)
    : file_index_(file_index), config_(config),
      volume_gateway_(std::make_unique<volume_gateway::DeviceIoControlVolumeGateway>()) {
  // Queue and threads are created in Start()
  // Atomic members are default-initialized
}

UsnMonitor::~UsnMonitor() {
  try {
    Stop();  // Ensure cleanup
  } catch (const std::exception& e) {  // NOLINT(bugprone-empty-catch) - body is LogException (clang-tidy may not expand macro)
    logging_utils::LogException("UsnMonitor destructor", "Stop() during shutdown", e);
  } catch (...) {  // NOLINT(bugprone-empty-catch) - body is LogUnknownException (clang-tidy may not expand macro)
    logging_utils::LogUnknownException("UsnMonitor destructor", "Stop() during shutdown");
  }
}

bool UsnMonitor::Start() {
  // Prevent double-start: if already monitoring, stop first
  {
    std::scoped_lock guard(mutex_);  // NOLINT(readability-identifier-naming) - project convention snake_case for locals
    if (state_.monitoring_active_.load()) {
      LOG_WARNING("Monitoring already active, stopping existing monitoring "
                  "before restart");
    }
  }

  // StopMonitoring acquires its own lock, so we release ours first to avoid
  // deadlock
  if (state_.monitoring_active_.load()) {
    Stop();
  }

  // Now acquire lock for starting
  // Use unique_lock instead of scoped_lock to allow manual unlock before waiting
  std::unique_lock<std::mutex> guard(mutex_);  // NOLINT(readability-identifier-naming) - project convention snake_case for locals

  // Clean up old threads if any
  if (reader_thread_.joinable()) {
    reader_thread_.join();
  }
  if (processor_thread_.joinable()) {
    processor_thread_.join();
  }

  // Reset metrics and internal counters for new monitoring session
  metrics_.Reset();
  reader_push_count_ = 0;
  last_queue_depth_band_logged_ = 0;
  state_.initial_population_failed_.store(false);

  // Create queue with configured size
  queue_ = std::make_unique<UsnJournalQueue>(config_.max_queue_size);
  LOG_INFO_BUILD(
      "Created USN queue with max size: "
      << config_.max_queue_size << " buffers (~"
      << (config_.max_queue_size * config_.buffer_size / (1024 * 1024))
      << " MB)");

  LOG_INFO_BUILD("Starting USN monitoring on volume: " << config_.volume_path);

  // Create promise/future pair for initialization status
  init_promise_ = std::promise<bool>();
  init_future_ = init_promise_.get_future();
  state_.init_promise_satisfied_.store(false);

  // Start the monitoring threads
  // Initial population will happen in the reader thread before monitoring
  // starts
  state_.monitoring_active_.store(true);
  reader_thread_ = std::thread(&UsnMonitor::ReaderThread, this);
  processor_thread_ = std::thread(&UsnMonitor::ProcessorThread, this);

  // Wait for initialization to complete (with timeout)
  // Timeout after 10 seconds to prevent indefinite blocking
  // Release lock before waiting to allow Stop() to be called if needed
  guard.unlock();
  bool init_success = false;  // NOLINT(misc-const-correctness) - init_success is assigned later (line 99), cannot be const
  if (auto status = init_future_.wait_for(std::chrono::seconds(10)); status == std::future_status::timeout) {
    LOG_ERROR("UsnMonitor initialization timed out after 10 seconds");
    // Stop() will clean up threads and handle (acquires its own lock)
    // state_.monitoring_active_ is still true, so Stop() will proceed with cleanup
    Stop();
    return false;
  }

  // Get initialization result
  init_success = init_future_.get();
  if (!init_success) {
    LOG_ERROR("UsnMonitor initialization failed - monitoring not active");
    // Stop() will clean up threads and handle (acquires its own lock)
    // state_.monitoring_active_ is still true, so Stop() will proceed with cleanup
    Stop();
    return false;
  }

  // Initialization succeeded - no need to reacquire lock, we're done

  LOG_INFO("UsnMonitor initialized successfully");
  return true;
}

void UsnMonitor::Stop() noexcept {  // NOLINT(readability-make-member-function-const) - Stop() mutates state_.monitoring_active_, volume_handle_, reader_thread_, processor_thread_, and queue_; atomics are technically callable on const but the method has observable side effects
  try {
    // Always drain threads/queue even when state_.monitoring_active_ is already false
    // (HandleInitializationFailure clears the flag before Stop/destructor run).
    // Move threads under the mutex so concurrent Stop() calls cannot double-join.
    auto handle_to_close = INVALID_HANDLE_VALUE;
    std::thread reader_to_join;
    std::thread processor_to_join;
    {
      std::scoped_lock guard(mutex_);  // NOLINT(readability-identifier-naming) - project convention snake_case for locals

      const bool had_work = state_.monitoring_active_.load() || reader_thread_.joinable() ||
                            processor_thread_.joinable() || queue_ != nullptr;
      state_.monitoring_active_.store(false);

      handle_to_close = volume_handle_;
      volume_handle_ = INVALID_HANDLE_VALUE;

      if (reader_thread_.joinable()) {
        reader_to_join = std::move(reader_thread_);
      }
      if (processor_thread_.joinable()) {
        processor_to_join = std::move(processor_thread_);
      }

      if (had_work) {
        LOG_INFO("Stopping USN monitoring");
      }
    }

    // Cancel any pending I/O operations and close the handle outside the mutex.
    if (handle_to_close != INVALID_HANDLE_VALUE) {
      CancelIoEx(handle_to_close, nullptr);  // Cancel pending I/O operations
      CloseHandle(handle_to_close);          // Close handle to cause immediate I/O failure
    }

    // Discard backlog and wake Push/Pop before joining. Joining the reader first
    // while the queue is full left Push blocked until the processor freed a slot;
    // joining the processor while Pop drained thousands of buffers froze
    // "Stopping..." for minutes. Stop() clears pending buffers immediately.
    if (queue_ != nullptr) {
      queue_->Stop();
    }
    if (reader_to_join.joinable()) {
      reader_to_join.join();
    }
    if (processor_to_join.joinable()) {
      processor_to_join.join();
    }

    // Clean up queue under lock (queue_ is shared with worker threads)
    {
      std::scoped_lock guard(mutex_);  // NOLINT(readability-identifier-naming)
      if (queue_ != nullptr) {
        if (size_t dropped_count = metrics_.buffers_dropped.load(); dropped_count > 0) {
          LOG_WARNING_BUILD("Queue had " << dropped_count
                                         << " dropped buffers during monitoring");
        }
        queue_.reset();
      }
    }

    LOG_INFO("USN monitoring stopped");
    // Postcondition: Stop must leave the monitor inactive.
    assert(!IsActive() && "Monitoring must be inactive after Stop");
  } catch (const std::exception& e) {  // NOLINT(bugprone-empty-catch) - body is LogException (clang-tidy may not expand macro)
    logging_utils::LogException("UsnMonitor::Stop", "shutdown", e);
  } catch (...) {  // NOLINT(bugprone-empty-catch) - body is LogUnknownException (clang-tidy may not expand macro)
    logging_utils::LogUnknownException("UsnMonitor::Stop", "shutdown");
  }
}

size_t UsnMonitor::GetQueueSize() const {
  if (queue_ != nullptr) {
    return queue_->Size();
  }
  return 0;
}

size_t UsnMonitor::GetDroppedBufferCount() const {
  // Use metrics counter instead of queue's internal counter
  // This avoids mutex lock and uses the authoritative source
  return metrics_.buffers_dropped.load();
}

void UsnMonitor::UpdateConfig(const MonitoringConfig &config) {
  std::scoped_lock guard(mutex_);  // NOLINT(readability-identifier-naming) - project convention snake_case for locals
  bool was_active = state_.monitoring_active_.load();
  if (was_active) {
    Stop();
  }
  config_ = config;
  if (was_active) {
    Start();
  }
}

void UsnMonitor::SignalInitResult(bool success) {
  if (!state_.init_promise_satisfied_.exchange(true)) {
    init_promise_.set_value(success);
  }
}

void UsnMonitor::LatchIntegrityCompromised(std::string_view reason) {
  usn_integrity_latch::LatchIntegrityCompromised(index_integrity_compromised_,
                                                 reason);
}

void UsnMonitor::MarkIntegrityCompromised() {
  usn_integrity_latch::MarkIntegrityCompromised(index_integrity_compromised_);
}

void UsnMonitor::PublishIntegrityEvent(index_domain_events::IntegrityEvent event) {
  if (on_integrity_event_) {
    on_integrity_event_(event);
  }
}

void UsnMonitor::HandleInitializationFailure() {
  state_.monitoring_active_.store(false);
  // May already be satisfied (journal open signaled true before population).
  SignalInitResult(false);
  {
    std::scoped_lock guard(mutex_);  // NOLINT(readability-identifier-naming) - project convention snake_case for locals
      if (volume_handle_ != INVALID_HANDLE_VALUE) {
        if (!CloseHandle(volume_handle_)) {
          LOG_ERROR_BUILD("UsnMonitor: CloseHandle failed with error " << GetLastError());
        }
        volume_handle_ = INVALID_HANDLE_VALUE;
      }
  }
  if (queue_ != nullptr) {
    queue_->Stop();
  }
}

bool UsnMonitor::OpenVolumeAndQueryJournal(HANDLE& out_handle,
                                           USN_JOURNAL_DATA_V0& out_journal_data) {
  CTRACK_DEV_NAME("UsnMonitor::OpenVolumeAndQueryJournal");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  // Create volume handle; wrap in ScopedHandle immediately so it is closed on
  // any exception or early return (CWE-404: Improper Resource Shutdown).
  // We transfer ownership to volume_handle_ only after storing under mutex so
  // Stop() can close it to cancel pending I/O.
  // SECURITY: Use FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE
  HANDLE handle = CreateFileA(config_.volume_path.c_str(),
                              FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, 0, nullptr);

  if (handle == INVALID_HANDLE_VALUE) {
    DWORD err = GetLastError();
    logging_utils::LogWindowsApiError("CreateFile (VolumeHandle)",
                                      "Volume: " + config_.volume_path,
                                      err);
    HandleInitializationFailure();
    return false;
  }

  ScopedHandle scoped_handle(handle);  // RAII: closes on any early return or throw

  {
    std::scoped_lock guard(mutex_);  // NOLINT(readability-identifier-naming) - project convention snake_case for locals
    volume_handle_ = scoped_handle.Get();
    scoped_handle.Release();  // Ownership transferred to volume_handle_
  }

  if (!volume_gateway_->QueryJournal(handle, out_journal_data)) {
    DWORD err = GetLastError();
    logging_utils::LogWindowsApiError("DeviceIoControl (FSCTL_QUERY_USN_JOURNAL)",
                                      "Volume: " + config_.volume_path,
                                      err);
    HandleInitializationFailure();
    return false;
  }

  out_handle = handle;
  return true;
}

void UsnMonitor::DrainReplayJournalEvents(HANDLE handle,
                                          const usn_journal::JournalCursor& start_cursor,
                                          USN target_usn,
                                          USN& out_next_usn) {
  CTRACK_NAME("UsnMonitor::DrainReplayJournalEvents");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  if (start_cursor.next_usn >= target_usn) {
    out_next_usn = start_cursor.next_usn;
    return;
  }

  READ_USN_JOURNAL_DATA_V0 read_data{};
  read_data.StartUsn = start_cursor.next_usn;
  read_data.ReasonMask = usn_reason::kInterestingReasons.Mask();
  read_data.ReturnOnlyOnClose = TRUE;
  read_data.Timeout = 0;          // Non-blocking
  read_data.BytesToWaitFor = 0;   // Immediate return of available records
  read_data.UsnJournalID = start_cursor.journal_id;

  const int buffer_size = config_.buffer_size;
  std::vector<char> buffer(buffer_size);
  size_t replay_buffers_processed = 0;

  usn_journal::JournalCursor cursor = start_cursor;
  bool drain_complete = false;
  while (!drain_complete && cursor.next_usn < target_usn && state_.monitoring_active_.load()) {
    DWORD bytes_returned = 0;
    if (!volume_gateway_->ReadJournal(handle, read_data, buffer.data(),
                                      buffer_size, bytes_returned)) {
      const DWORD err = GetLastError();
      if (err != ERROR_HANDLE_EOF && err != ERROR_NO_MORE_ITEMS) {
        LOG_WARNING_BUILD("FSCTL_READ_USN_JOURNAL non-blocking drain returned error: " << err);
      }
      drain_complete = true;
    } else if (bytes_returned <= usn_record_utils::SizeOfUsn()) {
      drain_complete = true;
    } else {
      const USN next_usn = *reinterpret_cast<const USN*>(buffer.data());  // NOSONAR(cpp:S3630)
      if (next_usn <= cursor.next_usn) {
        drain_complete = true;
      } else {
        cursor = cursor.Advance(next_usn);
        read_data.StartUsn = cursor.next_usn;
        std::vector<char> process_buffer(buffer.data(), buffer.data() + bytes_returned);
        ProcessOneBuffer(process_buffer);
        ++replay_buffers_processed;
      }
    }
  }

  out_next_usn = cursor.next_usn;
  if (replay_buffers_processed > 0) {
    LOG_INFO_BUILD("USN replay drain completed: processed "
                   << replay_buffers_processed << " buffers before completing population");
  }
}

bool UsnMonitor::RunInitialPopulationAndPrivileges(HANDLE handle,
                                                    const usn_journal::JournalCursor& pre_pop_cursor,
                                                    USN& out_next_usn) {
  CTRACK_NAME("UsnMonitor::RunInitialPopulationAndPrivileges");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  LOG_INFO("Starting initial index population");
  state_.is_populating_index_.store(true);
  ScopedTimer timer("Initial index population");
  // Input validation (was CRITICAL FIX #2 inside PopulateInitialIndex): the
  // run reads through the handle (MFT reader, Reserve query), so a bad handle
  // must fail before the run is constructed.
  if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
    LOG_ERROR("Invalid volume handle provided for initial index population");
    state_.is_populating_index_.store(false);
    state_.initial_population_failed_.store(true);
    HandleInitializationFailure();
    return false;
  }
  // The run owns the whole baseline lifecycle (reserve/stage/commit/result);
  // it stays alive past PopulateInitialIndex for the post-drain recompute.
  // Tracker preparation (Clear + Reserve) is owned by IndexBuildRun::Reserve.
  index_build_run::IndexBuildRun build_run(handle, *volume_gateway_, file_index_, &indexed_file_count_,
                                           filtered_dir_ref_nums_,
                                           &index_integrity_compromised_);
  if (!PopulateInitialIndex(build_run)) {
    LOG_ERROR("Failed to populate initial index - stopping monitoring. Volume: " +
              config_.volume_path);
    state_.is_populating_index_.store(false);
    state_.initial_population_failed_.store(true);
    HandleInitializationFailure();
    return false;
  }
  indexed_file_count_.store(file_index_.Size());
  LOG_INFO_BUILD("Initial index populated with "
                 << indexed_file_count_.load() << " entries");

  out_next_usn = pre_pop_cursor.next_usn;

  // Verify journal integrity and drain any replay events recorded during initial population.
  // This ensures all file operations that occurred while scanning the MFT are applied
  // and path-resolved BEFORE marking initial population complete.
  {
    CTRACK_NAME("UsnMonitor::PostPopulationReplayDrain");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
    USN_JOURNAL_DATA_V0 post_pop_journal{};
    if (!volume_gateway_->QueryJournal(handle, post_pop_journal)) {
      // Fail-closed: an unverifiable StartUsn must not go live. Mirrors the
      // PopulateInitialIndex failure path below (no latch: monitoring never
      // goes active, failure is surfaced via initial_population_failed_).
      const DWORD query_err = GetLastError();
      std::ostringstream reason;
      reason << "USN: post-population QUERY_USN_JOURNAL failed (err="
             << query_err
             << ") — cannot verify StartUsn vs LowestValidUsn; stopping "
                "monitoring for safety";
      LOG_ERROR_BUILD(reason.str());
      state_.is_populating_index_.store(false);
      state_.initial_population_failed_.store(true);
      HandleInitializationFailure();
      return false;
    }
    if (pre_pop_cursor.HasIdChanged(post_pop_journal.UsnJournalID)) {
      // Journal was deleted/recreated mid-enum: StartUsn belongs to a dead
      // journal, strictly worse than a wrap. Latch and stop; restart is the
      // only recovery (do not continue monitoring on a stale journal ID).
      std::ostringstream reason;
      reason << "USN: journal ID changed during initial population (was "
             << pre_pop_cursor.journal_id << ", now " << post_pop_journal.UsnJournalID
             << ") — StartUsn is invalid; stopping monitoring, restart the "
                "application";
      LatchIntegrityCompromised(reason.str());
      PublishIntegrityEvent(index_domain_events::JournalIdChanged{
          pre_pop_cursor.journal_id, post_pop_journal.UsnJournalID});
      state_.is_populating_index_.store(false);
      state_.initial_population_failed_.store(true);
      HandleInitializationFailure();
      return false;
    }
    if (pre_pop_cursor.IsWrappedBy(post_pop_journal.LowestValidUsn)) {
      std::ostringstream reason;
      reason << "USN: journal wrapped during initial population (StartUsn="
             << pre_pop_cursor.next_usn << ", LowestValidUsn="
             << post_pop_journal.LowestValidUsn
             << ") — events during MFT enum were lost; index may be stale until "
                "restart";
      LatchIntegrityCompromised(reason.str());
      PublishIntegrityEvent(index_domain_events::JournalWrapped{
          pre_pop_cursor.next_usn, post_pop_journal.LowestValidUsn});
    } else {
      DrainReplayJournalEvents(handle, pre_pop_cursor, post_pop_journal.NextUsn, out_next_usn);
    }
  }

  // Recompute all paths before marking population complete and before monitoring
  // starts. This resolves every placeholder path (entries whose parent arrived
  // out-of-order during journal enumeration or replay drain) so that the processor thread never
  // sees incomplete paths. Calling here eliminates the race window that existed
  // when WindowsIndexBuilder called RecomputeAllPaths via a polling loop.
  // Owned by the build run (full baseline lifecycle on one object); the WHEN
  // stays here, after the replay drain.
  {
    CTRACK_NAME("UsnMonitor::PostPopulationRecompute");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
    const ScopedTimer recompute_timer("RecomputeAllPaths (post-population)");
    build_run.RecomputePathsAndPruneOrphans();
  }

  indexed_file_count_.store(file_index_.Size());

#ifdef _WIN32
  // SECURITY: Drop unnecessary privileges AFTER initial index population completes
  // CRITICAL: Must happen AFTER PopulateInitialIndex() because raw MFT streaming
  // requires SE_BACKUP_PRIVILEGE during initial population.
  //
  // See docs/security/PRIVILEGE_DROPPING_STATUS.md for detailed comparison.
  if (privilege_utils::DropUnnecessaryPrivileges()) {
    LOG_INFO("Dropped unnecessary privileges - reduced attack surface");
  } else {
    state_.privilege_drop_failed_.store(true);
    LOG_ERROR("Failed to drop privileges - shutting down for security.");
    state_.is_populating_index_.store(false);
    state_.initial_population_failed_.store(true);
    HandleInitializationFailure();
    return false;
  }
#endif  // _WIN32

  state_.is_populating_index_.store(false);
  return true;
}

void UsnMonitor::HandleReadJournalError(DWORD err, size_t& consecutive_errors,
                                         bool& should_exit) {
  if (err == ERROR_OPERATION_ABORTED) {
    LOG_INFO("FSCTL_READ_USN_JOURNAL aborted (ERROR_OPERATION_ABORTED), exiting reader thread");
    should_exit = true;
    return;
  }

  consecutive_errors++;
  metrics_.errors.errors_encountered.fetch_add(1);
  metrics_.errors.consecutive_errors.store(consecutive_errors);
  UpdateAtomicMax(metrics_.errors.max_consecutive_errors, consecutive_errors);

  // Fatal journal loss: wrap, delete, or disable. Events are permanently lost —
  // latch integrity so the UI shows "Index may be stale" and stop the reader.
  if (err == ERROR_JOURNAL_ENTRY_DELETED || err == ERROR_JOURNAL_NOT_ACTIVE ||
      err == ERROR_JOURNAL_DELETE_IN_PROGRESS) {
    metrics_.errors.journal_wrap_errors.fetch_add(1);
    const char* journal_lost_reason = nullptr;
    if (err == ERROR_JOURNAL_ENTRY_DELETED) {
      journal_lost_reason = "USN journal wrapped; updates were lost. Index may be stale. "
                  "Stopping reader — restart the application.";
    } else if (err == ERROR_JOURNAL_NOT_ACTIVE) {
      journal_lost_reason = "USN journal is not active (disabled/deleted). Index may be stale. "
                  "Stopping reader — restart the application after re-enabling the journal.";
    } else {
      journal_lost_reason = "USN journal delete in progress. Index may be stale. "
                  "Stopping reader — restart the application.";
    }
    LatchIntegrityCompromised(journal_lost_reason);
    PublishIntegrityEvent(index_domain_events::JournalLost{err});
    state_.monitoring_active_.store(false);
    should_exit = true;
    return;
  }

  if (err == ERROR_INVALID_PARAMETER) {
    metrics_.errors.invalid_param_errors.fetch_add(1);
    logging_utils::LogWindowsApiError("DeviceIoControl (FSCTL_READ_USN_JOURNAL)",
                                      "Volume: " + config_.volume_path +
                                          ", Invalid parameter - retrying with backoff",
                                      err);
    std::this_thread::sleep_for(
        std::chrono::milliseconds(usn_monitor_constants::kInvalidParamDelayMs));
    return;
  }

  if (err == ERROR_INVALID_HANDLE) {
    LOG_INFO("Volume handle closed during shutdown, exiting reader thread");
    should_exit = true;
    return;
  }

  metrics_.errors.other_errors.fetch_add(1);
  logging_utils::LogWindowsApiError("DeviceIoControl (FSCTL_READ_USN_JOURNAL)",
                                    "Volume: " + config_.volume_path +
                                        ", Retrying with backoff",
                                    err);
  std::this_thread::sleep_for(
      std::chrono::milliseconds(usn_monitor_constants::kRetryDelayMs));

  constexpr size_t max_consecutive_errors_limit = 100;
  if (consecutive_errors >= max_consecutive_errors_limit) {
    std::ostringstream reason;
    reason << "Too many consecutive errors (" << consecutive_errors
           << "), stopping USN monitoring";
    // Unknown persistent failure: treat as integrity risk (silent stop without latch
    // would leave the UI looking healthy while events are no longer applied).
    LatchIntegrityCompromised(reason.str());
    PublishIntegrityEvent(index_domain_events::ConsecutiveErrorsExceeded{consecutive_errors});
    state_.monitoring_active_.store(false);
    should_exit = true;
  }
}

void UsnMonitor::ProcessSuccessfulReadAndEnqueue(
    const std::vector<char>& buffer,
    int buffer_size,
    DWORD bytes_returned,
    READ_USN_JOURNAL_DATA_V0& read_data,
    bool& should_exit) {
  if (bytes_returned > static_cast<DWORD>(buffer_size)) {
    LOG_ERROR_BUILD("DeviceIoControl returned more bytes ("
                    << bytes_returned << ") than buffer size ("
                    << buffer_size << ")");
    return;
  }
  if (bytes_returned <= usn_record_utils::SizeOfUsn()) {
    return;
  }
  USN next_usn = *reinterpret_cast<const USN*>(buffer.data());  // NOSONAR(cpp:S3630) - Reading POD type (USN/DWORD) from raw buffer bytes is safe and standard Windows API pattern; buffer is validated to contain at least sizeof(USN) bytes above
  read_data.StartUsn = next_usn;

  std::vector<char> queue_buffer(buffer.data(),
                                 buffer.data() + bytes_returned);

  if (!queue_) {
    LOG_ERROR("USN queue is null in reader thread. Volume: " + config_.volume_path);
    state_.monitoring_active_.store(false);
    should_exit = true;
    return;
  }
  metrics_.buffers_read.fetch_add(1);

  if (!queue_->Push(std::move(queue_buffer))) {
    // Queue stop during shutdown: no integrity fault, just exit reader loop.
    if (!state_.monitoring_active_.load() || queue_->IsStopped()) {
      should_exit = true;
      return;
    }
    // Defensive fallback: if Push unexpectedly fails while active, preserve the
    // existing integrity-latch behavior because updates may have been lost.
    // Immediate Mark every drop; throttled Latch for the log (re-store is
    // idempotent). See usn/UsnIntegrityLatch.h.
    MarkIntegrityCompromised();
    const size_t current_drops = metrics_.buffers_dropped.fetch_add(1) + 1;
    if (current_drops % usn_monitor_constants::kDropLogInterval == 0) {
      std::ostringstream reason;  // NOLINT(misc-const-correctness) - false positive: built via operator<< below; const would not compile
      reason << "USN Queue push failed while active. Lost "
             << current_drops << " buffers (queue size: "
             << queue_->Size() << "). Index may be stale.";
      LatchIntegrityCompromised(reason.str());
      PublishIntegrityEvent(index_domain_events::QueueBuffersDropped{current_drops, queue_->Size()});
    }
  }

  // queue_ was verified non-null on entry and outlives the reader thread
  // (Stop() resets it only after joining the reader), so no re-check here.
  assert(queue_ != nullptr && "queue_ outlives the reader thread; verified on entry");
  const size_t queue_size = queue_->Size();
  metrics_.current_queue_depth.store(queue_size);
  UpdateAtomicMax(metrics_.max_queue_depth, queue_size);
  MaybeLogQueueDepthBandCrossing(metrics_, queue_size, last_queue_depth_band_logged_);

  if (++reader_push_count_ % usn_monitor_constants::kLogIntervalBuffers == 0) {
    const size_t queue_depth_snapshot = queue_->Size();
    if (queue_depth_snapshot > usn_monitor_constants::kQueueWarningThreshold) {
      LOG_WARNING_BUILD("USN Queue size: " << queue_depth_snapshot
                                           << " buffers pending"
                                           << " (files_modified="
                                           << metrics_.files_modified.load()
                                           << ", process_ms="
                                           << metrics_.total_process_time_ms.load()
                                           << ")");
    }
  }
}

/**
 * Reader Thread Function
 *
 * Responsibilities:
 * - Opens the volume handle and queries the USN journal
 * - Performs initial index population (if needed) before starting monitoring
 * - Continuously reads USN journal data using
 * DeviceIoControl(FSCTL_READ_USN_JOURNAL)
 * - Filters records at kernel level using ReasonMask to reduce data transfer
 * - Batches records using BytesToWaitFor to balance latency vs. throughput
 * - Pushes complete buffers to the queue for processing
 * - Handles errors gracefully with retry logic and backoff strategies
 * - Updates metrics for monitoring and diagnostics
 *
 * Error Handling:
 * - Transient errors (journal wrap, invalid parameter): Retry with exponential
 * backoff
 * - Fatal errors (invalid handle, too many consecutive errors): Stop monitoring
 * - All exceptions are caught to prevent thread termination and ensure cleanup
 *
 * Lifecycle:
 * - Thread starts when Start() is called
 * - Runs until state_.monitoring_active_ is set to false (via Stop())
 * - Automatically stops on fatal errors or too many consecutive errors
 * - Thread name is set for debugging/profiling tools
 *
 * Thread Safety:
 * - Reads from config_ (immutable after Start())
 * - Writes to state_.monitoring_active_ (atomic, thread-safe)
 * - Pushes to queue_ (thread-safe via UsnJournalQueue)
 * - Updates metrics_ (atomic counters, thread-safe)
 */
void UsnMonitor::ReaderThread() {
  SetThreadName("USN-Reader");

  try {
    LOG_INFO("USN Reader thread started");

    auto handle = INVALID_HANDLE_VALUE;
    auto usn_journal_data = USN_JOURNAL_DATA_V0{};
    if (!OpenVolumeAndQueryJournal(handle, usn_journal_data)) {
      return;
    }
    LOG_INFO("USN Journal queried successfully");
    // Journal open succeeded — Start() can return while population continues.
    // Population/privilege failures must use SignalInitResult (no second set_value).
    SignalInitResult(true);
    USN monitoring_start_usn = usn_journal_data.NextUsn;
    const usn_journal::JournalCursor pre_pop_cursor{usn_journal_data.UsnJournalID,
                                                    usn_journal_data.NextUsn,
                                                    usn_journal_data.LowestValidUsn};
    if (!RunInitialPopulationAndPrivileges(handle, pre_pop_cursor,
                                           monitoring_start_usn)) {
      return;
    }
    LOG_INFO("Initial index population complete, starting USN monitoring");

    auto read_data = READ_USN_JOURNAL_DATA_V0{};
    read_data.StartUsn = monitoring_start_usn;
    // Filter at kernel level - only get records we care about
    // This reduces data transfer and processing overhead
    read_data.ReasonMask = usn_reason::kInterestingReasons.Mask();
    // TRUE: kernel accumulates reason bits and delivers one close-time record.
    // Avoids intermediate CREATE/DELETE partial records that ProcessUsnRecordReasons
    // would apply out of order (Insert then Remove → silent eviction of a live file).
    // Requires USN_REASON_CLOSE in ReasonMask (MSDN). Close-only noise is filtered
    // in ProcessOneBuffer by the apply filter (usn_record::CarriesAction).
    read_data.ReturnOnlyOnClose = TRUE;
    read_data.Timeout = config_.timeout_ms;
    // BytesToWaitFor: Wait for at least configured bytes of unfiltered data
    // before returning This batches multiple records together, reducing call
    // frequency while maintaining low latency. The value is in unfiltered bytes
    // (before ReasonMask).
    read_data.BytesToWaitFor = static_cast<DWORD>(config_.bytes_to_wait_for);
    read_data.UsnJournalID = usn_journal_data.UsnJournalID;

    const int buffer_size = config_.buffer_size;  // NOSONAR(cpp:S1854) - Used in buffer allocation below

    std::vector<char> buffer(buffer_size);
    size_t consecutive_errors = 0;
    bool should_exit = false;  // NOLINT(misc-const-correctness) - modified by reference in HandleReadJournalError/ProcessSuccessfulReadAndEnqueue
    DWORD bytes_returned = 0;

    while (state_.monitoring_active_.load() && !should_exit) {
      const auto read_start = std::chrono::steady_clock::now();

      if (!volume_gateway_->ReadJournal(handle, read_data, buffer.data(),
                                      buffer_size, bytes_returned)) {
        HandleReadJournalError(GetLastError(), consecutive_errors, should_exit);
        continue;
      }

      // Track read time
      const auto read_end = std::chrono::steady_clock::now();
      auto read_duration =
          std::chrono::duration_cast<std::chrono::milliseconds>(read_end -
                                                                read_start);
      metrics_.total_read_time_ms.fetch_add(read_duration.count());

      // Reset error counter on success
      consecutive_errors = 0;
      metrics_.errors.consecutive_errors.store(0);

      ProcessSuccessfulReadAndEnqueue(buffer, buffer_size, bytes_returned,
                                     read_data, should_exit);
    }

    LOG_INFO("USN Reader thread stopping");
  } catch (const std::exception &e) {  // NOSONAR(cpp:S1181) NOLINT(bugprone-empty-catch) - log and continue; cannot rethrow from thread
    logging_utils::LogException("USN Reader thread",
                                "Volume: " + config_.volume_path,
                                e);
    state_.monitoring_active_.store(false);
  } catch (...) {  // NOSONAR(cpp:S2738) NOLINT(bugprone-empty-catch) - log and continue; cannot rethrow from thread
    logging_utils::LogUnknownException("USN Reader thread",
                                      "Volume: " + config_.volume_path);
    state_.monitoring_active_.store(false);
  }

  // Unblock processor Pop() whenever the reader exits (failure, Stop, or catch).
  // Idempotent with Stop()/HandleInitializationFailure.
  if (queue_ != nullptr) {
    queue_->Stop();
  }

  // Clean up handle if it wasn't already closed by Stop()
  {
    std::scoped_lock guard(mutex_);  // NOLINT(readability-identifier-naming) - project convention snake_case for locals
      if (volume_handle_ != INVALID_HANDLE_VALUE) {
        if (!CloseHandle(volume_handle_)) {
          LOG_ERROR_BUILD("UsnMonitor: CloseHandle failed with error " << GetLastError());
        }
        volume_handle_ = INVALID_HANDLE_VALUE;
      }
  }
}

/**
 * Processor Thread Function
 *
 * Responsibilities:
 * - Continuously pops buffers from the queue (blocking wait when queue is
 * empty)
 * - Parses USN_RECORD_V2 structures from each buffer
 * - Validates buffer bounds and record integrity to prevent buffer overreads
 * - Filters out system files (e.g., $Recycle.Bin artifacts)
 * - Updates FileIndex based on USN record reasons (CREATE, DELETE, RENAME,
 * MODIFY)
 * - Updates indexed_file_count_ to reflect current index size
 * - Tracks processing metrics (records processed, file operations, timing)
 * - Yields CPU after each buffer to allow UI thread to acquire locks
 *
 * Error Handling:
 * - Invalid records: Logged and skipped, processing continues
 * - Buffer bounds violations: Logged and buffer processing stops (next buffer
 * continues)
 * - All exceptions are caught to prevent thread termination
 *
 * Lifecycle:
 * - Thread starts when Start() is called
 * - Runs until queue_->Pop() returns false (queue is stopped via Stop())
 * - Thread name is set for debugging/profiling tools
 *
 * Thread Safety:
 * - Reads from queue_ (thread-safe via UsnJournalQueue)
 * - Writes to file_index_ (assumed thread-safe, or protected by FileIndex
 * implementation)
 * - Updates indexed_file_count_ (atomic, thread-safe)
 * - Updates metrics_ (atomic counters, thread-safe)
 *
 * Performance Considerations:
 * - Yields after each buffer to prevent UI starvation
 * - Processes records in batches for efficiency
 * - Metrics updates use relaxed memory ordering for performance
 */
void UsnMonitor::ApplyOneUsnRecord(PUSN_RECORD_V2 record,
                                   volume_state::VolumeState& aggregate) {  // NOLINT(readability-identifier-naming) - PascalCase member function; check misclassifies as global variable
  // ReasonMask includes CLOSE (required with ReturnOnlyOnClose). Skip records
  // that have no create/delete/rename/data action — close-only noise.
  // Same predicate as UsnRecord::IsActionable (raw form: no UsnRecord exists
  // before name conversion).
  if (usn_record::CarriesAction(usn_reason::ReasonSet(record->Reason))) {
    ProcessInterestingUsnRecord(record, aggregate, metrics_,
                                index_integrity_compromised_,
                                filtered_dir_ref_nums_,
                                &activity_tracker_,
                                on_integrity_event_);
  }
}

void UsnMonitor::ProcessBufferForTest(std::vector<char>& buffer) {  // NOLINT(readability-identifier-naming) - PascalCase member function; check misclassifies as global variable
  ProcessOneBuffer(buffer);
}

void UsnMonitor::ProcessOneBuffer(std::vector<char>& buffer) {  // NOLINT(readability-identifier-naming) - PascalCase member function; check misclassifies as global variable
  CTRACK_PROD_NAME("UsnMonitor::ProcessOneBuffer");  // NOLINT(misc-const-correctness) - macro-generated ctrack::EventHandler; cannot be declared const here
  const auto buffer_process_start = std::chrono::steady_clock::now();
  auto bytes_returned = static_cast<DWORD>(buffer.size());  // NOSONAR(cpp:S1905) - Cast needed for Windows USN API (DWORD is 32-bit, size_t may be 64-bit)
  DWORD offset = usn_record_utils::SizeOfUsn();

  // Phase 0 (no lock): single validation walk; stable-partition valid records
  // into parent-establishing (directory CREATE) vs rest. Pointers stay valid:
  // the buffer is only read during apply, never mutated or reallocated.
  // Corrupt record: stop collecting (same loss set as the old immediate
  // break) and remember the offset for the latch + log below.
  //
  // The partition vectors are reused across calls: clear() keeps capacity, so
  // after the first buffers they reach steady-state capacity and cost zero
  // allocations per buffer (fresh reserve(8)/reserve(32) vectors paid 2 allocs
  // plus a growth realloc on `remaining` for typical 24KB buffers with ~75
  // records). thread_local because ProcessOneBuffer runs on three threads —
  // the processor loop, the reader thread (DrainReplayJournalEvents), and
  // ProcessBufferForTest — each gets its own copy, so there is no cross-thread
  // sharing to reason about. Pinned capacity stays at the high-water mark
  // (tens of KB per thread worst case), which is negligible.
  thread_local std::vector<PUSN_RECORD_V2> establish_parents;
  thread_local std::vector<PUSN_RECORD_V2> remaining;
  establish_parents.clear();
  remaining.clear();
  bool has_corrupt_record = false;
  DWORD corrupt_offset = 0;
  while (offset < bytes_returned) {
    PUSN_RECORD_V2 record = nullptr;
    if (!usn_record_utils::ValidateAndParseUsnRecord(
            buffer.data(), bytes_returned, offset, record)) {
      has_corrupt_record = true;
      corrupt_offset = offset;
      break;
    }
    if (IsParentEstablishingRecord(record)) {
      establish_parents.push_back(record);
    } else {
      remaining.push_back(record);
    }
    offset += record->RecordLength;
  }

  // Phases 1+2 under one exclusive lock (as before): parent-establishing
  // records first, so same-buffer children join cleanly instead of parking
  // bare-name placeholders that the heal pass must fix up. Journal order is
  // preserved within each phase; cross-buffer and never-arriving parents
  // still go through awaiting/heal/sweep (unchanged).
  //
  // VolumeState aggregate: one exclusive lock for the whole buffer (entries,
  // paths, awaiting set advance atomically). Create/delete/rename storms used
  // to take a unique_lock per event and starve search via Windows SRWLOCK
  // writer preference; a single per-buffer lock fixes that and makes the
  // invariant a type contract (VolumeState verbs require unique_lock). Release
  // between buffers so search can acquire shared_lock.
  volume_state::VolumeState aggregate(file_index_);
  {
    const std::unique_lock index_lock(aggregate.GetMutex());
    for (const PUSN_RECORD_V2 record : establish_parents) {
      ApplyOneUsnRecord(record, aggregate);
    }
    for (const PUSN_RECORD_V2 record : remaining) {
      ApplyOneUsnRecord(record, aggregate);
    }
  }

  if (has_corrupt_record) {
    // Corrupt record: all remaining events in this buffer are permanently
    // lost. Set the integrity latch so the UI shows "Index may be stale".
    std::ostringstream reason;
    reason << "USN record corrupt at offset " << corrupt_offset
           << "; remaining buffer records skipped. Index may be stale.";
    LatchIntegrityCompromised(reason.str());
    PublishIntegrityEvent(index_domain_events::CorruptBufferTail{corrupt_offset});
  }

  indexed_file_count_.store(aggregate.Size());

  const auto buffer_process_end = std::chrono::steady_clock::now();
  auto process_duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          buffer_process_end - buffer_process_start);
  metrics_.total_process_time_ms.fetch_add(process_duration.count());
  UpdateAtomicMax(metrics_.max_buffer_process_time_ms,
                  static_cast<uint64_t>(process_duration.count()));

  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();
  metrics_.last_update_time_ms.store(now_ms);

  // Prefer draining when backlog is high (InvalidateSize made per-buffer work cheap).
  // When the queue is shallow, yield so search/UI can acquire FileIndex shared locks.
  if (queue_ != nullptr &&
      queue_->Size() < usn_monitor_constants::kQueueDepthBand100) {
    std::this_thread::yield();
  }

  metrics_.buffers_processed.fetch_add(1);

  size_t buffers_processed =
      metrics_.buffers_processed.load();
  // Never-arriving sweep runs far less often than stats logging; both are
  // cheap (O(awaiting)) when nothing is waiting. Wall-clock backstop: on quiet
  // systems buffer-count ticks stall, so also run when the last sweep is old.
  // The buffer's leading 8 bytes are the next-USN (see DrainReplayJournalEvents);
  // the sweep uses it as the journal-order cursor for gap eviction.
  if (buffers_processed % usn_monitor_constants::kNeverArrivingSweepIntervalBuffers == 0 ||
      std::chrono::steady_clock::now() - last_never_arriving_sweep_ >=
          std::chrono::milliseconds(usn_monitor_constants::kNeverArrivingSweepMaxIntervalMs)) {
    const int64_t buffer_usn =
        buffer.size() >= sizeof(int64_t)
            ? *reinterpret_cast<const int64_t*>(buffer.data())  // NOSONAR(cpp:S3630) - Leading next-USN of DeviceIoControl output; size guarded above (same pattern as ProcessSuccessfulReadAndEnqueue)
            : 0;
    SweepNeverArriving(buffer_usn);
  }
  if (buffers_processed % usn_monitor_constants::kLogIntervalBuffers == 0) {
    size_t queue_size = queue_->Size();
    size_t dropped_count = metrics_.buffers_dropped.load();
    size_t total_records =
        metrics_.records_processed.load();
    LOG_INFO_BUILD("Processed "
                   << buffers_processed << " buffers, " << total_records
                   << " total records. Queue size: " << queue_size);
    if (dropped_count > 0) {
      LOG_WARNING_BUILD(
          "Total dropped buffers due to queue full: " << dropped_count);
    }
  }
}

void UsnMonitor::SweepNeverArriving(int64_t current_usn) {
  last_never_arriving_sweep_ = std::chrono::steady_clock::now();
  // Shared-lock snapshot first (cheap O(awaiting)); exclusive lock only when
  // a policy qualifies. Routed through VolumeState for uniform aggregate
  // adoption (queries are lock-free / shared-lock internally).
  volume_state::VolumeState aggregate(file_index_);
  const auto stats = aggregate.GetAwaitingStats();
  metrics_.awaiting_count.store(stats.count);
  metrics_.awaiting_oldest_age_ms.store(stats.oldest_age_ms);
  metrics_.healed_awaiting_total.store(aggregate.GetHealedAwaitingTotal());
  // OR policy: wall-clock age (all systems) or journal gap (steady state
  // only — during population the replay drain advances USNs in bulk and
  // parents legitimately lag buffers, so gap eviction would false-positive).
  const bool wall_qualifies =
      stats.count > 0 &&
      stats.oldest_age_ms >= usn_monitor_constants::kNeverArrivingMaxAgeMs;
  const bool gap_applies =
      stats.count > 0 && !state_.is_populating_index_.load();
  if (!wall_qualifies && !gap_applies) {
    return;
  }
  // Log lines are collected under the lock and emitted after it is released:
  // no I/O under lock (see the sink contract in IndexDomainEvents.h). String
  // building under lock is cheap CPU without syscalls.
  std::vector<std::string> sweep_log_lines;
  {
    const std::unique_lock index_lock(aggregate.GetMutex());
    auto evict_sink = [this](const index_domain_events::NeverArrivingEvicted&) {
      metrics_.never_arriving_evicted.fetch_add(1);
    };
    if (wall_qualifies) {
      const size_t evicted = aggregate.ApplyNeverArrivingEviction(
          usn_monitor_constants::kNeverArrivingMaxAgeMs, evict_sink, &sweep_log_lines);
      if (evicted > 0) {
        sweep_log_lines.push_back("USN never-arriving sweep evicted " +
                                  std::to_string(evicted) + " placeholders waiting >=" +
                                  std::to_string(usn_monitor_constants::kNeverArrivingMaxAgeMs) +
                                  "ms for never-arriving parents");
      }
    }
    if (gap_applies) {
      const size_t evicted = aggregate.ApplyNeverArrivingEvictionByJournalGap(
          current_usn, usn_monitor_constants::kNeverArrivingMaxUsnGap, evict_sink,
          &sweep_log_lines);
      if (evicted > 0) {
        sweep_log_lines.push_back("USN never-arriving sweep evicted " +
                                  std::to_string(evicted) + " placeholders tracked >=" +
                                  std::to_string(usn_monitor_constants::kNeverArrivingMaxUsnGap) +
                                  " USNs behind current_usn=" + std::to_string(current_usn));
      }
    }
  }
  for (const std::string& line : sweep_log_lines) {
    // In Release LOG_WARNING_BUILD compiles out; keep line referenced.
    (void)line;
    LOG_WARNING_BUILD(line);
  }
}

void UsnMonitor::ProcessorThread() {
  SetThreadName("USN-Processor");

  try {
    LOG_INFO("USN Processor thread started");

    std::vector<char> buffer;
    if (!queue_) {
      LOG_ERROR("USN queue is null in processor thread. This should not happen - queue should be valid during monitoring");
      return;
    }

    while (queue_->Pop(buffer)) {
      ProcessOneBuffer(buffer);
    }

    LOG_INFO("USN Processor thread stopping");
  } catch (const std::exception &e) {  // NOSONAR(cpp:S1181) NOLINT(bugprone-empty-catch) - log and continue; cannot rethrow from thread
    auto error_msg =
        std::string(e.what()); // Always reference 'e' to avoid unused variable warning
    logging_utils::LogException("USN Processor thread",
                                "Processing USN records from queue",
                                e);
  } catch (...) {  // NOSONAR(cpp:S2738) NOLINT(bugprone-empty-catch) - log and continue; cannot rethrow from thread
    logging_utils::LogUnknownException("USN Processor thread",
                                      "Processing USN records from queue");
  }
}
