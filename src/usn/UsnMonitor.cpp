#include "usn/UsnMonitor.h"

#include "utils/CTrackWrapper.h"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <cassert>
#include "platform/windows/PrivilegeUtils.h"
#include "utils/LoggingUtils.h"
#include "utils/ScopedHandle.h"
#endif  // _WIN32

#include "index/FileIndex.h"
#include "index/IndexBuildRun.h"
#include "index/NtfsFileReference.h"
#include "index/SystemPathFilter.h"
#include "index/recycle/RecycleBinEnricher.h"
#include "index/recycle/RecycleBinMetadataStore.h"
#include "path/PathUtils.h"
#include "usn/UsnReason.h"
#include "usn/UsnRecord.h"
#include "utils/ExceptionHandling.h"
#include "utils/FileSystemUtils.h"
#include "utils/FileTimeTypes.h"
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
  FileIndex& file_index;
  UsnMonitorMetrics& metrics;
  std::atomic<bool>& integrity_compromised;
  system_path_filter::SystemPathFilterPolicy& filter_policy;  // per-volume ingestion policy (live path)
  UsnActivityTracker* activity_tracker = nullptr;
  index_domain_events::FileEventSink on_file_event;
  index_domain_events::IntegrityEventSink on_integrity_event;  // null disables; RenameDivergence publisher
  // Phase 4 recycle-bin sinks (nullable; owned by ProcessOneBuffer for the duration of
  // one buffer). recycle_sink collects producer reports, evict_sink collects the
  // buffer's eviction batch so one store.EvictMany publish covers all of them (M7).
  RecycleBinBufferSink* recycle_sink = nullptr;
  std::vector<recycle_bin::EvictOp>* evict_sink = nullptr;
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

// Live USN routing mode (Phase 2: $Recycle.Bin payloads are ingested; see SystemPathFilter.h).
// Soft-deleted entries stay indexed as $R and restores return them to their normal name.
constexpr system_path_filter::IngestionMode kLiveIngestionMode =
    system_path_filter::IngestionMode::kIncludeRecycleBin;

// Filename view of a raw USN_RECORD_V2 (bounds validated by ValidateAndParseUsnRecord).
// The view aliases a thread-local UTF-8 buffer: consume it before the next conversion.
[[nodiscard]] std::string_view RecordFilename(const USN_RECORD_V2* record) {
  const wchar_t* const wfilename =
      reinterpret_cast<const wchar_t*>(reinterpret_cast<const std::byte*>(record) + record->FileNameOffset);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR(cpp:S3630) - USN_RECORD_V2 variable-offset filename; bounds checked by ValidateAndParseUsnRecord
  const size_t wchar_len = record->FileNameLength / sizeof(wchar_t);
  return WideToUtf8ThreadLocal(wfilename, wchar_len);
}

// M2 tracker-priming pre-pass (see ProcessOneBuffer). Journal-order tracker update
// only: the result is discarded and the apply phases re-classify idempotently. Gated
// like the live apply path (CarriesAction + IsExcludedIndexEntry) so it records
// nothing the live path would never classify.
void PrimeFilterTrackers(const USN_RECORD_V2* record,
                         system_path_filter::SystemPathFilterPolicy& filter_policy) {
  if (!usn_reason::CarriesAction(usn_reason::ReasonSet(record->Reason))) {
    return;
  }
  const std::string_view filename = RecordFilename(record);
  const bool is_directory = (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  if (system_path_filter::IsExcludedIndexEntry(
          filename, ntfs_file_reference::RecordNumber(record->FileReferenceNumber), is_directory)) {
    return;
  }
  static_cast<void>(filter_policy.ClassifyEntry(filename, record->FileReferenceNumber,
                                                record->ParentFileReferenceNumber, is_directory,
                                                kLiveIngestionMode));
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
  return usn_reason::EstablishesParent(
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

// True when parent_file_reference identifies a tracked <SID> or $R-folder: the only
// parents whose artifact children carry Recycle Bin meaning (INV-5).
[[nodiscard]] bool IsTrackedRecycleParent(
    uint64_t parent_file_reference,
    const system_path_filter::SystemPathFilterPolicy& filter_policy) {
  return filter_policy.IsTrackedSidOrRecycledFolder(
      ntfs_file_reference::MftRecordNumber::FromFileReference(parent_file_reference));
}

// True when this entry is an $R payload: an "$R<token>" name under a tracked
// <SID>/$R-folder. Only these live entries are reported as the $R side of a pair.
[[nodiscard]] bool IsTrackedRecyclePayload(
    std::string_view name, uint64_t parent_file_reference,
    const system_path_filter::SystemPathFilterPolicy& filter_policy) {
  const std::optional<recycle_bin::RecycleNameToken> token = recycle_bin::TokenizeRecycleName(name);
  return token.has_value() && token->prefix == recycle_bin::RecycleNamePrefix::kR &&
         IsTrackedRecycleParent(parent_file_reference, filter_policy);
}

// Stages the $R side of one live payload event (bin payload create, soft-delete
// rename, in-bin rename). No path is needed: the store pairs by (parent FRN, token)
// and the $I side supplies name/time/size. Full FRNs (sequence bits intact) are the
// store's keys, so the $I delete eviction resolves the same pair.
void StageRFileSide(std::string_view name, uint64_t file_reference, uint64_t parent_file_reference,
                    bool is_directory, RecycleBinBufferSink& sink) {
  recycle_bin::RFileInfo info;
  info.r_frn = file_reference;
  info.parent_frn = parent_file_reference;
  info.name.assign(name);
  info.is_directory = is_directory;
  sink.r_sides.push_back(std::move(info));
}

// Stages one $I sidecar event. CREATE queues the $I path for the off-thread reader
// (A1 queue-first, INV-3: nothing is read on this thread); DELETE cancels a
// still-queued task and evicts the published pair (Empty Bin, post-restore cleanup).
// A record carrying both bits (ReturnOnlyOnClose) is delete-wins, mirroring
// ProcessUsnRecordReasons. The parent path is resolved here because GetPathViewLockHeld
// requires the index lock that the caller holds.
void StageRecycleMetadataSidecar(const USN_RECORD_V2* record, std::string_view filename,
                                 const FileIndex& file_index,
                                 const usn_reason::ReasonSet& reasons,
                                 RecycleBinBufferSink& sink,
                                 std::vector<recycle_bin::EvictOp>& evict_sink) {
  const bool is_delete = reasons.Includes(usn_reason::UsnReason::FileDelete);
  if (is_delete) {
    evict_sink.push_back(
        recycle_bin::EvictOp{recycle_bin::EvictKind::kByI, record->FileReferenceNumber});
  }
  const std::string_view parent_path =
      file_index.GetPathViewLockHeld(record->ParentFileReferenceNumber);
  if (parent_path.empty()) {
    // Skip-and-continue (spec 3.4/3.6): the $I sidecar stays out of the index (INV-2)
    // and the next cold index reports it. Nothing is queued for an unresolvable path.
    LOG_DEBUG_BUILD("USN: recycle $I '" << filename
                    << "' has no resolved parent path; sidecar not queued");
    return;
  }
  std::string absolute_path = path_utils::JoinPath(parent_path, filename);
  if (is_delete) {
    sink.i_cancel_paths.push_back(std::move(absolute_path));  // Eviction already staged above.
    return;
  }
  if (reasons.Includes(usn_reason::UsnReason::FileCreate)) {
    sink.i_enqueue.push_back(recycle_bin::EnrichTask{record->FileReferenceNumber,
                                                     record->ParentFileReferenceNumber,
                                                     std::string(filename),
                                                     std::move(absolute_path)});
  }
}

// Flushes one buffer's staged recycle-bin producers. The CALLER MUST NOT hold the
// FileIndex lock: the store and the enricher take their own mutexes (lock-ordering
// rule) and the store may copy its published maps here. No $I file I/O happens on
// this thread (INV-3) — the enricher's worker owns the reads. The $R sides go to the
// store in ONE batch call (one publish for the whole buffer, M7 coalescing).
void FlushRecycleBinProducers(RecycleBinBufferSink& sink,
                              std::vector<recycle_bin::EvictOp>& evictions,
                              recycle_bin::RecycleBinMetadataStore& store,
                              recycle_bin::RecycleBinEnricher& enricher) {
  // One batch call for the whole buffer: the store pairs or pends every $R side and
  // publishes at most once (same epoch/version rules as the single-item form). Skipped
  // when empty so the common buffer costs nothing. Passed by reference: the caller's
  // staging vector is reused per buffer and must keep its capacity.
  if (!sink.r_sides.empty()) {
    store.IngestRFiles(sink.r_sides);
  }
  for (recycle_bin::EnrichTask& task : sink.i_enqueue) {
    // Drops (full queue or stopped) are counted by the enricher's DropCount().
    static_cast<void>(enricher.EnqueueIFile(task.i_frn, task.parent_frn, std::move(task.name),
                                            std::move(task.absolute_path)));
  }
  for (const std::string& path : sink.i_cancel_paths) {
    static_cast<void>(enricher.CancelByPath(path));
  }
  if (!evictions.empty()) {
    store.EvictMany(evictions);  // One publish for the whole buffer (M7 coalescing).
  }
}

// Live system-file filter for one USN record, routed through
// SystemPathFilterPolicy::ClassifyLiveEntry (kIncludeRecycleBin). Returns true when the
// record is consumed here (skip body). Returns false when it continues on the normal
// path: kProcess ($R payload creates/deletes/changes, normal entries), or
// kEvictAsRecycleDelete (filtered-subtree $-rename; HandleRenameNewName routes it).
// Side effects on the skip path (kSkip, kSkipMetadata): evicts directory descendants,
// publishes FileDeleted on FILE_DELETE, drops tracker membership, and runs the
// RemoveLocked safety net. Tracker updates happen inside ClassifyLiveEntry.
// Recycle producer reports ($R side on create/delete, $I sidecar create/delete) are
// STAGED into recycle_sink/evict_sink only; ProcessOneBuffer flushes them after the
// index lock is released. Null sinks disable staging (tests).
// Does NOT touch offset — offset management is the caller's responsibility.
bool HandleSystemFileFilter(const USN_RECORD_V2* record, std::string_view filename,
                            FileIndex& file_index,
                            const index_domain_events::FileEventSink& on_file_event,
                            system_path_filter::SystemPathFilterPolicy& filter_policy,
                            RecycleBinBufferSink* recycle_sink,
                            std::vector<recycle_bin::EvictOp>* evict_sink) {
  const bool is_directory = (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  const uint64_t file_record_num = ntfs_file_reference::RecordNumber(record->FileReferenceNumber);
  if (system_path_filter::IsExcludedIndexEntry(filename, file_record_num, is_directory)) {
    return true;
  }

  const usn_reason::ReasonSet reasons(record->Reason);
  const bool is_delete = reasons.Includes(usn_reason::UsnReason::FileDelete);
  const system_path_filter::LiveDecision decision = filter_policy.ClassifyLiveEntry(
      filename, record->FileReferenceNumber, record->ParentFileReferenceNumber, is_directory,
      reasons.Includes(usn_reason::UsnReason::RenameNewName), kLiveIngestionMode);

  if (decision.tracker_miss) {
    // Spec §3.6: artifact-shaped name, parent unknown to both trackers. Debug-only, one line.
    LOG_DEBUG_BUILD("USN: artifact '" << filename << "' parent-tracker miss (parent_ref="
                    << ntfs_file_reference::RecordNumber(record->ParentFileReferenceNumber)
                    << ", spec 3.6)");
  }

  // Normal path (kProcess), or RENAME_NEW_NAME soft-delete routed to HandleRenameNewName.
  if (decision.action == system_path_filter::LiveAction::kProcess ||
      decision.action == system_path_filter::LiveAction::kEvictAsRecycleDelete) {
    // Every directory FILE_DELETE drops its tracker membership (idempotent). Accepted
    // follow-up: $R-folder deletes take normal-directory parity (single-entry remove;
    // NTFS journals child deletes; Tier-2 verifies AC-4 nested Brainstorm).
    if (is_directory && is_delete) {
      filter_policy.OnDirectoryDeleted(ntfs_file_reference::MftRecordNumber(file_record_num));
    }
    // Phase 4 $R-side producer (only for real payloads: kProcess under a tracked
    // <SID>/$R-folder). A create reports the $R side so the store can pair it with the
    // $I side; a delete evicts the pair (Empty Bin, belt-and-braces for $R). The
    // soft-delete rename is reported by HandleRenameNewName. Staged only — the store
    // call happens after the index lock is released.
    if (recycle_sink != nullptr && evict_sink != nullptr &&
        decision.action == system_path_filter::LiveAction::kProcess &&
        IsTrackedRecyclePayload(filename, record->ParentFileReferenceNumber, filter_policy)) {
      if (is_delete) {
        evict_sink->push_back(recycle_bin::EvictOp{recycle_bin::EvictKind::kByR,
                                                   record->FileReferenceNumber});
      } else if (reasons.Includes(usn_reason::UsnReason::FileCreate)) {
        StageRFileSide(filename, record->FileReferenceNumber, record->ParentFileReferenceNumber,
                       is_directory, *recycle_sink);
      }
    }
    return false;
  }

  // $I sidecar (INV-2: never ingested). Producers: a create queues the $I path for the
  // enricher's off-thread reader (A1: no $I file I/O on this thread), a delete cancels
  // any still-queued task and evicts the pair. Both are staged and flushed after the
  // index lock is released.
  if (decision.action == system_path_filter::LiveAction::kSkipMetadata &&
      recycle_sink != nullptr && evict_sink != nullptr) {
    StageRecycleMetadataSidecar(record, filename, file_index, reasons, *recycle_sink, *evict_sink);
  }

  // If this is a directory in a filtered subtree, record it so grandchildren are also filtered.
  // Evict descendants only when newly marked: with ReturnOnlyOnClose a child CREATE can be
  // processed before its parent is first marked filtered, leaving entries that the
  // never-inserted parent can neither heal nor remove (caller holds the
  // unique_lock; Evict...Locked collects before mutating). Once tracked in
  // filter_policy, subsequent child events are rejected by IsFilteredChild above,
  // so re-scanning the full index on every repeat directory event in $Extend/$Recycle.Bin
  // is pure O(N) waste.
  if (is_directory &&
      filter_policy.MarkFilteredDir(system_path_filter::FilteredDirectory(file_record_num))) {
    file_index.EvictDescendantsOfFilteredParentLocked(
        ntfs_file_reference::MftRecordNumber(file_record_num));
  }

  // Count genuine delete events even though we skip the record.
  if (is_delete) {
    PublishFileEvent(on_file_event, index_domain_events::FileDeleted{record->FileReferenceNumber});
    // Idempotent for files: files never occupy the container sets.
    filter_policy.OnDirectoryDeleted(ntfs_file_reference::MftRecordNumber(file_record_num));
  }

  // Evict from index on any filtered event (safety net in case a file slipped in).
  file_index.RemoveLocked(ntfs_file_reference::NtfsFileReference(record->FileReferenceNumber));
  return true;
}

// Helper function to process interesting USN record.
// Returns true if the record was filtered (caller should advance offset and
// continue). Returns false if the record was processed normally.
// Does NOT touch offset — offset management belongs in ProcessOneBuffer.
bool ProcessInterestingUsnRecord(PUSN_RECORD_V2 record,  // NOSONAR(cpp:S107) - USN per-record processing intentionally aggregates buffer context
                                 FileIndex& file_index,
                                 UsnMonitorMetrics& metrics,  // NOLINT(misc-const-correctness) - false positive: metrics atomics are mutated (fetch_add); const would not compile
                                 std::atomic<bool>& integrity_compromised,
                                 system_path_filter::SystemPathFilterPolicy& filter_policy,
                                 UsnActivityTracker* activity_tracker,
                                 const index_domain_events::IntegrityEventSink& on_integrity_event = nullptr,
                                 RecycleBinBufferSink* recycle_sink = nullptr,
                                 std::vector<recycle_bin::EvictOp>* evict_sink = nullptr) {
  const std::string_view filename = RecordFilename(record);

  auto filter_sink = [&metrics](const index_domain_events::FileLifecycleEvent& event) {
    CountFileLifecycleEvent(metrics, event);
  };
  // Filter out NTFS system files ($MFT, $LogFile, $Bitmap, $Recycle.Bin
  // artefacts, etc.) and their subtrees (e.g. per-user SID subfolders).
  if (HandleSystemFileFilter(record, filename, file_index, filter_sink, filter_policy, recycle_sink,
                             evict_sink)) {
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
      is_directory,
      record->FileAttributes,};
  UsnProcessingState processing_state{file_index, metrics, integrity_compromised, filter_policy,
                                      activity_tracker,
                                      [&metrics](const index_domain_events::FileLifecycleEvent& event) {
                                        CountFileLifecycleEvent(metrics, event);
                                      },
                                      on_integrity_event,
                                      recycle_sink,
                                      evict_sink};
  ProcessUsnRecordReasons(record_context, processing_state);

  // Refresh the attribute bitmask from the journal record (offline
  // transitions included). Null-safe: entries deleted by this same record
  // are already gone. Creates set the mask at insert; this covers renames,
  // moves, and attribute changes on existing entries.
  processing_state.file_index.UpdateFileAttributesLocked(record_context.self.raw,
                                                         record_context.file_attributes);

  metrics.records_processed.fetch_add(1);
  return false;  // Record processed normally
}

// Invalidate cached size for a DATA_* change. No synchronous size I/O on the
// processor thread (stalls journal drain). Misses are expected when DELETE
// preceded DATA_* in the same buffer — throttle the log.
void InvalidateSizeForDataChange(const usn_record::UsnRecord& record_context,
                                 UsnProcessingState& processing_state) {
  if (processing_state.file_index.InvalidateSizeLocked(record_context.self.raw)) {
    PublishFileEvent(processing_state.on_file_event,
                     index_domain_events::FileModified{record_context.self.raw});
    if (processing_state.activity_tracker != nullptr) {
      std::string path(processing_state.file_index.GetPathViewLockHeld(record_context.self.raw));
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
  // All mutations below run under the single per-buffer unique_lock held by
  // ProcessOneBuffer.
  FileIndex& file_index = processing_state.file_index;
  // CREATE|DELETE on one close record (temp file, supersede, atomic replace):
  // DELETE wins. Independent ifs would Insert then Remove and can leave the index
  // wrong for short-lived create+delete; Remove-only matches "file is gone".
  if (reasons.Includes(usn_reason::UsnReason::FileDelete)) {
    std::string path(file_index.GetPathViewLockHeld(record_context.self.raw));
    if (path.empty()) {
      path = std::string(record_context.name);
    }
    file_index.RemoveLocked(record_context.self);
    PublishFileEvent(processing_state.on_file_event,
                     index_domain_events::FileDeleted{record_context.self.raw});
    if (processing_state.activity_tracker != nullptr) {
      processing_state.activity_tracker->RecordChange(UsnChangeType::Deleted, std::move(path));
    }
  } else if (reasons.Includes(usn_reason::UsnReason::FileCreate)) {
    FileIndex::InsertOptions options{};
    options.usn_at_track = record_context.usn;
    file_index.InsertLocked(record_context.self, record_context.parent, record_context.name,
                            record_context.is_directory, kFileTimeNotLoaded, options,
                            kFileSizeNotLoaded, record_context.file_attributes);
    PublishFileEvent(processing_state.on_file_event,
                     index_domain_events::FileCreated{record_context.self.raw});
    if (processing_state.activity_tracker != nullptr) {
      std::string path(file_index.GetPathViewLockHeld(record_context.self.raw));
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
                                 FileIndex& file_index,
                                 const index_domain_events::FileEventSink& on_file_event,
                                 system_path_filter::SystemPathFilterPolicy& filter_policy) {
  // A RENAME event arrived for a file ID not in the index. During normal
  // monitoring this should not happen: the CREATE event should always precede
  // a RENAME. However, gaps can occur due to journal overflows or filtered indexing.
  // Only a normal (kProcess) outcome may recover. Under kIncludeRecycleBin a $R payload
  // under a tracked SID is kProcess and may recover; $I (INV-2, kSkipMetadata) and excluded
  // names (kSkip) are still rejected.
  if (const auto decision = filter_policy.ClassifyLiveEntry(
          filename, file_ref_num, parent_ref_num, is_directory,
          /*is_rename_new_name=*/false, kLiveIngestionMode);
      decision.action != system_path_filter::LiveAction::kProcess) {  // kSkip / kSkipMetadata: rejected
    LOG_WARNING_BUILD("RENAME for unknown ID " << file_ref_num << " (" << filename
                      << "): skipped recovery insert as system/recycled artifact");
    return false;
  }

  LOG_WARNING_BUILD("RENAME for unknown ID " << file_ref_num << " (" << filename
                    << ", parent_ref=" << parent_ref_num
                    << "): treating as CREATE (recovery path - audited)");
  // Treat as create to keep the index consistent.
  file_index.InsertLocked(ntfs_file_reference::NtfsFileReference(file_ref_num),
                          ntfs_file_reference::NtfsFileReference(parent_ref_num),
                          filename, is_directory, kFileTimeNotLoaded);
  PublishFileEvent(on_file_event, index_domain_events::FileRenamed{file_ref_num});
  return true;
}

// Helper: handle a move (parent changed) and optional rename in one operation.
void HandleMoveAndOptionalRename(uint64_t file_ref_num, uint64_t parent_ref_num,
                                 std::string_view filename,
                                 FileIndex& file_index,
                                 std::atomic<bool>& integrity_compromised,
                                 const index_domain_events::IntegrityEventSink& on_integrity_event = nullptr) {
  // Update parent first, then update name if it also changed.
  // Caller holds unique_lock (ProcessOneBuffer batch apply).
  const auto file_ref = ntfs_file_reference::NtfsFileReference(file_ref_num);
  if (!file_index.MoveLocked(file_ref,
                             ntfs_file_reference::NtfsFileReference(parent_ref_num))) {
    // entry_exists was true moments ago; Move failing almost always means a
    // DELETE in the same buffer already removed the entry. Do not latch
    // integrity and do not continue to GetPathViewLockHeld/Rename on a missing id.
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
  const std::string_view current_path = file_index.GetPathViewLockHeld(file_ref_num);
  if (const std::string_view current_name = path_utils::GetFilename(current_path);
      current_name == filename) {
    return;
  }
  if (file_index.RenameLocked(file_ref, filename)) {
    return;
  }
  // Rename failed after a successful Move. Concurrent delete between the two
  // calls is expected under delete storms — only latch integrity if the entry
  // is still present (true rename failure / index divergence).
  // NOTE: logging under the FileIndex unique_lock preserves pre-existing
  // behavior.
  if (file_index.GetEntry(file_ref_num) == nullptr) {
    return;
  }
  std::ostringstream reason;
  reason << "USN: Rename (during move) failed for ref=" << file_ref_num
         << ", new_name=" << filename << ". Index may be stale.";
  integrity_compromised.store(true);
  LOG_ERROR_BUILD(reason.str());
  PublishIntegrityEventIfSet(on_integrity_event,
                             index_domain_events::RenameDivergence{file_ref_num});
}

// Only kEvictAsRecycleDelete reaches here. kSkip and kSkipMetadata are consumed by
// HandleSystemFileFilter first. kProcess is a normal rename or move, including $R
// soft-delete and restore.
void HandleRenameNewName(const usn_record::UsnRecord& record_context,
                         UsnProcessingState& processing_state) {
  const system_path_filter::LiveDecision decision = processing_state.filter_policy.ClassifyLiveEntry(
      record_context.name, record_context.self.raw, record_context.parent.raw,
      record_context.is_directory, /*is_rename_new_name=*/true, kLiveIngestionMode);
  // Metadata lifecycle (M6): a RENAME_NEW_NAME whose NEW parent is not a tracked
  // <SID>/$R-folder has left the bin — that is a restore, or a plain live->live rename.
  // Evict the fileId so a restored file cannot keep rendering with the stale original
  // name and [Recycled] badge (the companion $I delete ordering is not guaranteed
  // relative to UI reads). An in-bin rename keeps a tracked parent and evicts nothing;
  // the store no-ops for fileIds it never published. Staged here, applied after the
  // index lock is released (ProcessOneBuffer).
  if (processing_state.evict_sink != nullptr &&
      !IsTrackedRecycleParent(record_context.parent.raw, processing_state.filter_policy)) {
    processing_state.evict_sink->push_back(
        recycle_bin::EvictOp{recycle_bin::EvictKind::kRenamedOut, record_context.self.raw});
  }
  if (decision.action == system_path_filter::LiveAction::kEvictAsRecycleDelete) {
    std::string old_path(processing_state.file_index.GetPathViewLockHeld(record_context.self.raw));
    RemoveIndexedSubtreeLocked(processing_state.file_index, record_context.self);
    PublishFileEvent(processing_state.on_file_event,
                     index_domain_events::FileDeleted{record_context.self.raw});
    if (processing_state.activity_tracker != nullptr) {
      processing_state.activity_tracker->RecordChange(UsnChangeType::Deleted, std::move(old_path));
    }
    return;
  }
  // $R side of a soft delete (rename onto $R... under a tracked <SID>) or of an in-bin
  // rename: report it so the store can pair it with the $I side the $I create producer
  // queued (spec 3.5.1). Nested children of a $R-folder are not artifact-shaped names
  // and are not reported — their original paths derive from the folder's sidecar.
  if (processing_state.recycle_sink != nullptr &&
      IsTrackedRecyclePayload(record_context.name, record_context.parent.raw,
                              processing_state.filter_policy)) {
    StageRFileSide(record_context.name, record_context.self.raw, record_context.parent.raw,
                   record_context.is_directory, *processing_state.recycle_sink);
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
  uint64_t canonical_self = record_context.self.raw;
  if (const auto [entry, canonical_id] =
          processing_state.file_index.ResolveEntryReference(record_context.self.raw);
      entry != nullptr) {
    entry_exists = true;
    canonical_self = canonical_id;
    current_parent = entry->parentID.raw;
    old_path = std::string(processing_state.file_index.GetPathViewLockHeld(canonical_self));
  }

  if (!entry_exists) {
    const bool inserted_by_recovery =
        HandleRenameForUnknownEntry(record_context.self.raw,
                                record_context.parent.raw,
                                record_context.name,
                                record_context.is_directory,
                                processing_state.file_index,
                                processing_state.on_file_event,
                                processing_state.filter_policy);
    if (inserted_by_recovery && processing_state.activity_tracker != nullptr) {
      std::string new_path(processing_state.file_index.GetPathViewLockHeld(record_context.self.raw));
      if (new_path.empty()) {
        new_path = std::string(record_context.name);
      }
      processing_state.activity_tracker->RecordChange(UsnChangeType::Created, std::move(new_path));
    }
    return;
  }
  const auto canonical_self_ref = ntfs_file_reference::NtfsFileReference(canonical_self);
  if (!ntfs_file_reference::SameRecordNumber(current_parent,
                                             record_context.parent.raw)) {
    // Parent MFT record changed - this is a move (with optional rename).
    // Compare by record number only: USN ParentFileReferenceNumber often has a
    // stale sequence while the index stores the canonical parent FRN.
    HandleMoveAndOptionalRename(canonical_self,
                                record_context.parent.raw,
                                record_context.name,
                                processing_state.file_index,
                                processing_state.integrity_compromised,
                                processing_state.on_integrity_event);
  } else {
    // Same parent record (sequence may differ) - name-only rename.
    if (!processing_state.file_index.RenameLocked(canonical_self_ref,
                                                  record_context.name)) {
      // entry_exists was true moments ago; Rename failing means the entry was
      // concurrently removed. If this fires for any other reason it is divergence.
      // NOTE: logging under the FileIndex unique_lock preserves pre-existing
      // behavior.
      std::ostringstream reason;
      reason << "USN: Rename failed for ref=" << canonical_self
             << ", new_name=" << record_context.name
             << ". Index may be stale.";
      processing_state.integrity_compromised.store(true);
      LOG_ERROR_BUILD(reason.str());
      PublishIntegrityEventIfSet(processing_state.on_integrity_event,
                                 index_domain_events::RenameDivergence{canonical_self});
    }
  }
  PublishFileEvent(processing_state.on_file_event,
                     index_domain_events::FileRenamed{canonical_self});
  if (processing_state.activity_tracker != nullptr) {
    std::string new_path(processing_state.file_index.GetPathViewLockHeld(canonical_self));
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
    exception_handling::LogException("UsnMonitor destructor", "Stop() during shutdown", e);
  } catch (...) {  // NOLINT(bugprone-empty-catch) - body is LogUnknownException (clang-tidy may not expand macro)
    exception_handling::LogUnknownException("UsnMonitor destructor", "Stop() during shutdown");
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

    // Recycle Bin $I reader (Phase 4): joined AFTER the monitor threads, so no
    // producer — a live USN buffer flush or the cold drain — can enqueue into a
    // stopped queue. Idempotent; queued-but-unrun $I tasks are counted as drops.
    metadata_enricher_.Stop();

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
    exception_handling::LogException("UsnMonitor::Stop", "shutdown", e);
  } catch (...) {  // NOLINT(bugprone-empty-catch) - body is LogUnknownException (clang-tidy may not expand macro)
    exception_handling::LogUnknownException("UsnMonitor::Stop", "shutdown");
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
  index_integrity_compromised_.store(true);
  LOG_ERROR_BUILD(reason);
}

void UsnMonitor::MarkIntegrityCompromised() {
  index_integrity_compromised_.store(true);
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
      if (const DWORD err = GetLastError(); err != ERROR_HANDLE_EOF && err != ERROR_NO_MORE_ITEMS) {
        LOG_WARNING_BUILD("FSCTL_READ_USN_JOURNAL non-blocking drain returned error: " << err);
      }
      drain_complete = true;
    } else if (bytes_returned <= usn_record::SizeOfUsn()) {
      drain_complete = true;
    } else {
      const USN next_usn = *reinterpret_cast<const USN*>(buffer.data());  // NOSONAR(cpp:S3630)
      if (next_usn <= cursor.next_usn) {
        drain_complete = true;
      } else {
        cursor = cursor.Advance(next_usn);
        read_data.StartUsn = cursor.next_usn;
        metrics_.buffers_read.fetch_add(1);
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
                                           filter_policy_,
                                           &index_integrity_compromised_);
  // Recycle Bin producers (Phase 4): the run stages $R/$I/<SID> triples during the
  // stream and hands them to these sinks in DrainRecycleBinProducers(). Both are
  // application-lifetime members that outlive the run; passing them here (not from
  // ReaderThread) keeps the run's lifecycle in one place.
  build_run.SetRecycleBinSink(&metadata_store_, &metadata_enricher_);
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

  // Phase 4 sequencing: the cold producer drain MUST run after the recompute above,
  // because it resolves each $I sidecar's absolute path from FileIndex (GetPathCopy),
  // which only knows the SID/container paths once recompute has run. It must also run
  // before the run goes out of scope, and while metadata_enricher_ is started.
  // Pure queue/store work on this (reader) thread — the $I reads stay on the worker
  // (INV-3), so the journal drain never blocks on disk I/O.
  build_run.DrainRecycleBinProducers();

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
  if (bytes_returned <= usn_record::SizeOfUsn()) {
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
    // idempotent).
    MarkIntegrityCompromised();
    if (const size_t current_drops = metrics_.buffers_dropped.fetch_add(1) + 1;
        current_drops % usn_monitor_constants::kDropLogInterval == 0) {
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
    if (const size_t queue_depth_snapshot = queue_->Size();
        queue_depth_snapshot > usn_monitor_constants::kQueueWarningThreshold) {
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
    // Restart correctness (Phase 4): a stop/start cycle can miss the $R delete or
    // kRenamedOut report for a pair published in the previous run (the journal is only
    // drained between Start and Stop), which would pin a stale name/path to a fileId the
    // bin no longer owns. Wiping the store makes the restart rebuild from current truth:
    // the cold drain below re-reports every $R/$I pair that still exists, so anything not
    // re-reported is genuinely gone. Reset() bumps the epoch of every cleared fileId (M7
    // guard), so an enrichment stamped before the restart can never be applied, and the
    // version bump intentionally triggers a re-search through the 4.5 mechanism.
    metadata_store_.Reset();
    // The $I reader must be running before any producer can enqueue (cold drain or
    // live USN). Start() is idempotent and re-opens the queue after a Stop().
    metadata_enricher_.Start();
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
    // in ProcessOneBuffer by the apply filter (usn_reason::CarriesAction).
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
    exception_handling::LogException("USN Reader thread",
                                "Volume: " + config_.volume_path,
                                e);
    state_.monitoring_active_.store(false);
  } catch (...) {  // NOSONAR(cpp:S2738) NOLINT(bugprone-empty-catch) - log and continue; cannot rethrow from thread
    exception_handling::LogUnknownException("USN Reader thread",
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
                                   RecycleBinBufferSink* recycle_sink,
                                   std::vector<recycle_bin::EvictOp>* evict_sink) {  // NOLINT(readability-identifier-naming) - PascalCase member function; check misclassifies as global variable
  // ReasonMask includes CLOSE (required with ReturnOnlyOnClose). Skip records
  // that have no create/delete/rename/data action — close-only noise.
  if (usn_reason::CarriesAction(usn_reason::ReasonSet(record->Reason))) {
    ProcessInterestingUsnRecord(record, file_index_, metrics_,
                                index_integrity_compromised_,
                                filter_policy_,
                                &activity_tracker_,
                                on_integrity_event_,
                                recycle_sink,
                                evict_sink);
  }
}

void UsnMonitor::ProcessBufferForTest(std::vector<char>& buffer) {  // NOLINT(readability-identifier-naming) - PascalCase member function; check misclassifies as global variable
  ProcessOneBuffer(buffer);
}

// Test-only Recycle Bin enricher lifecycle (see UsnMonitor.h). Production starts the
// enricher on the reader thread and stops it from Stop() after the thread joins; the
// test binary never starts monitoring, so it drives the worker itself. Both calls
// forward to the enricher's own idempotent Start()/Stop().
void UsnMonitor::StartRecycleEnricherForTest() {  // NOLINT(readability-identifier-naming) - PascalCase member function; check misclassifies as global variable
  metadata_enricher_.Start();
}

void UsnMonitor::StopRecycleEnricherForTest() {  // NOLINT(readability-identifier-naming) - PascalCase member function; check misclassifies as global variable
  metadata_enricher_.Stop();
}

void UsnMonitor::ProcessOneBuffer(std::vector<char>& buffer) {  // NOLINT(readability-identifier-naming) - PascalCase member function; check misclassifies as global variable
  CTRACK_PROD_NAME("UsnMonitor::ProcessOneBuffer");  // NOLINT(misc-const-correctness) - macro-generated ctrack::EventHandler; cannot be declared const here
  const auto buffer_process_start = std::chrono::steady_clock::now();
  auto bytes_returned = static_cast<DWORD>(buffer.size());  // NOSONAR(cpp:S1905) - Cast needed for Windows USN API (DWORD is 32-bit, size_t may be 64-bit)
  DWORD offset = usn_record::SizeOfUsn();

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
  // Phase 4 recycle-bin staging for this buffer (same thread_local rationale as the
  // partition vectors above): handlers push while the index lock is held, and the
  // flush below runs right after it is released, so both live exactly one buffer.
  // clear() keeps capacity, so steady-state buffers allocate nothing.
  thread_local RecycleBinBufferSink recycle_sink;
  thread_local std::vector<recycle_bin::EvictOp> evict_sink;
  recycle_sink.r_sides.clear();
  recycle_sink.i_enqueue.clear();
  recycle_sink.i_cancel_paths.clear();
  evict_sink.clear();
  bool has_corrupt_record = false;
  DWORD corrupt_offset = 0;
  while (offset < bytes_returned) {
    PUSN_RECORD_V2 record = nullptr;
    if (!usn_record::ValidateAndParseUsnRecord(
            buffer.data(), bytes_returned, offset, record)) {
      has_corrupt_record = true;
      corrupt_offset = offset;
      break;
    }
    // M2 tracker-priming pre-pass, folded into this journal-order walk (no second
    // parse). Containers recorded later in the buffer must already be known when
    // the phased apply below classifies SID-child RENAME_NEW_NAMEs and same-buffer
    // $R payloads; the apply phases then re-classify idempotently.
    PrimeFilterTrackers(record, filter_policy_);
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
  // Single exclusive lock for the whole buffer (entries, paths, awaiting set
  // advance atomically). Create/delete/rename storms used to take a unique_lock
  // per event and starve search via Windows SRWLOCK writer preference; a single
  // per-buffer lock fixes that. Release between buffers so search can acquire
  // shared_lock.
  {
    // Split lock-wait out of the total buffer time: waiting for search
    // shared_lock holders is contention, the rest is CPU. Existing totals
    // keep their semantics (wait included); lock_wait isolates the share.
    const auto lock_wait_start = std::chrono::steady_clock::now();
    const std::unique_lock index_lock(file_index_.GetMutex());
    const auto lock_acquired = std::chrono::steady_clock::now();
    const auto lock_wait_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            lock_acquired - lock_wait_start)
            .count());
    metrics_.lock_wait.total_ms.fetch_add(lock_wait_ms);
    UpdateAtomicMax(metrics_.lock_wait.max_ms, lock_wait_ms);
    for (const PUSN_RECORD_V2 record : establish_parents) {
      ApplyOneUsnRecord(record, &recycle_sink, &evict_sink);
    }
    for (const PUSN_RECORD_V2 record : remaining) {
      ApplyOneUsnRecord(record, &recycle_sink, &evict_sink);
    }
  }

  // Phase 4 flush (index lock released above, so the store/enricher mutexes never
  // nest inside it — .agents/rules/lock-ordering-no-io-under-lock.md). Producers are
  // pure queue/store work: no $I file I/O on this thread (INV-3), the enricher worker
  // owns the reads. One IngestRFiles + one EvictMany per buffer keep the store's
  // publishes coalesced (M7). Empty staging (the common case) is a no-op, and the
  // staged vector is passed by reference so its capacity survives the call.
  FlushRecycleBinProducers(recycle_sink, evict_sink, metadata_store_, metadata_enricher_);

  if (has_corrupt_record) {
    // Corrupt record: all remaining events in this buffer are permanently
    // lost. Set the integrity latch so the UI shows "Index may be stale".
    std::ostringstream reason;
    reason << "USN record corrupt at offset " << corrupt_offset
           << "; remaining buffer records skipped. Index may be stale.";
    LatchIntegrityCompromised(reason.str());
    PublishIntegrityEvent(index_domain_events::CorruptBufferTail{corrupt_offset});
  }

  indexed_file_count_.store(file_index_.Size());

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

  // Refresh current_queue_depth on the consumer side too: ProcessSuccessfulReadAndEnqueue
  // only runs on Push (when size >= 1), which left current_queue_depth frozen while
  // draining without new reads and stuck at 1 after popping the final buffer.
  // Prefer draining when backlog is high (InvalidateSize made per-buffer work cheap).
  // When the queue is shallow, yield so search/UI can acquire FileIndex shared locks.
  size_t queue_size = 0;
  if (queue_ != nullptr) {
    queue_size = queue_->Size();
    metrics_.current_queue_depth.store(queue_size);
    if (queue_size < usn_monitor_constants::kQueueDepthBand100) {
      std::this_thread::yield();
    }
  }

  const size_t buffers_processed =
      metrics_.buffers_processed.fetch_add(1) + 1;
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
    const size_t dropped_count = metrics_.buffers_dropped.load();
    const size_t total_records =
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
  // a policy qualifies.
  const auto stats = file_index_.GetAwaitingStats();
  metrics_.awaiting_count.store(stats.count);
  metrics_.awaiting_oldest_age_ms.store(stats.oldest_age_ms);
  metrics_.healed_awaiting_total.store(file_index_.GetHealedAwaitingTotal());
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
    const std::unique_lock index_lock(file_index_.GetMutex());
    auto evict_sink = [this](const index_domain_events::NeverArrivingEvicted&) {
      metrics_.never_arriving_evicted.fetch_add(1);
    };
    if (wall_qualifies) {
      const size_t evicted = file_index_.EvictNeverArrivingLocked(
          usn_monitor_constants::kNeverArrivingMaxAgeMs, evict_sink, &sweep_log_lines);
      if (evicted > 0) {
        sweep_log_lines.push_back("USN never-arriving sweep evicted " +
                                  std::to_string(evicted) + " placeholders waiting >=" +
                                  std::to_string(usn_monitor_constants::kNeverArrivingMaxAgeMs) +
                                  "ms for never-arriving parents");
      }
    }
    if (gap_applies) {
      const size_t evicted = file_index_.EvictNeverArrivingByJournalGapLocked(
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
    exception_handling::LogException("USN Processor thread",
                                "Processing USN records from queue",
                                e);
  } catch (...) {  // NOSONAR(cpp:S2738) NOLINT(bugprone-empty-catch) - log and continue; cannot rethrow from thread
    exception_handling::LogUnknownException("USN Processor thread",
                                      "Processing USN records from queue");
  }
}
