#include "usn/UsnMonitor.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <cassert>
#include "platform/windows/PrivilegeUtils.h"
#include "utils/LoggingUtils.h"
#include "utils/ScopedHandle.h"
#endif  // _WIN32

#include "index/NtfsFileReference.h"
#include "index/RemoveIndexedSubtree.h"
#include "usn/UsnRecordUtils.h"
#include "utils/FileSystemUtils.h"
#include "utils/StringUtils.h"

namespace {

struct UsnRecordContext {
  PUSN_RECORD_V2 record;
  uint64_t file_ref_num;
  uint64_t parent_ref_num;
  std::string_view filename;
  bool is_directory;
};

struct UsnProcessingState {
  FileIndex& file_index;
  UsnMonitorMetrics& metrics;
  std::atomic<bool>& integrity_compromised;
};

// Forward declarations for helper functions
void ProcessUsnRecordReasons(const UsnRecordContext& record_context,
                             UsnProcessingState& processing_state);

void HandleFileRename(const UsnRecordContext& record_context,
                      UsnProcessingState& processing_state);

void HandleRenameNewName(const UsnRecordContext& record_context,
                         UsnProcessingState& processing_state);

// Helper function to update max consecutive errors using atomic compare-and-swap
// Extracted to reduce nesting depth
void UpdateMaxConsecutiveErrors(std::atomic<size_t>& max_consecutive_errors, size_t consecutive_errors) {  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - PascalCase; check misclassifies anonymous-namespace function as global
  size_t current_max = max_consecutive_errors.load();
  while (consecutive_errors > current_max) {
    if (max_consecutive_errors.compare_exchange_weak(
            current_max, consecutive_errors)) {
      break;
    }
  }
}

// Helper function to update max queue depth using atomic compare-and-swap
// Extracted to reduce nesting depth
void UpdateMaxQueueDepth(std::atomic<size_t>& max_queue_depth, size_t queue_size) {  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - PascalCase; check misclassifies anonymous-namespace function as global
  size_t current_max = max_queue_depth.load();
  while (queue_size > current_max) {
    if (max_queue_depth.compare_exchange_weak(
            current_max, queue_size)) {
      break;
    }
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

[[nodiscard]] bool IsSystemPrefixedName(std::string_view filename) {
  return !filename.empty() &&
         filename[0] == usn_monitor_constants::kSystemFilePrefix;
}

// Returns true if the file should be filtered out (name starts with '$').
// Side effects: removes the entry from the index (safety net for any $-file
// that slipped in during initial population) and updates delete metrics.
// RENAME_NEW_NAME to a $-prefixed name is NOT filtered here — that path is a
// Recycle Bin soft-delete and is handled in HandleRenameNewName instead.
// Does NOT touch offset — offset management is the caller's responsibility.
bool HandleSystemFileFilter(PUSN_RECORD_V2 record, std::string_view filename,
                            FileIndex& file_index, UsnMonitorMetrics& metrics) {
  if (!IsSystemPrefixedName(filename)) {
    return false;
  }

  // Soft-delete to $Recycle.Bin arrives as RENAME_NEW_NAME ($R…). Let
  // ProcessUsnRecordReasons / HandleRenameNewName treat it as a logical delete.
  if ((record->Reason & USN_REASON_RENAME_NEW_NAME) != 0) {
    return false;
  }

  // Count genuine delete events even though we skip the record.
  if (record->Reason & USN_REASON_FILE_DELETE) {
    metrics.files_deleted.fetch_add(1);
  }
  // Evict from index on any other $-prefixed event (e.g. $I CREATE): safety net
  // in case a $-prefixed file was indexed (older builds / MFT edge cases).
  file_index.Remove(record->FileReferenceNumber);
  return true;
}

// Helper function to process interesting USN record.
// Returns true if the record was filtered (caller should advance offset and
// continue). Returns false if the record was processed normally.
// Does NOT touch offset — offset management belongs in ProcessOneBuffer.
bool ProcessInterestingUsnRecord(PUSN_RECORD_V2 record,
                                 FileIndex& file_index,
                                 UsnMonitorMetrics& metrics,
                                 std::atomic<bool>& integrity_compromised) {
  const wchar_t* const wfilename =
      reinterpret_cast<wchar_t*>(reinterpret_cast<std::byte*>(record) + record->FileNameOffset);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR(cpp:S3630) - USN_RECORD_V2 variable-offset filename; bounds checked by ValidateAndParseUsnRecord
  const size_t wchar_len = record->FileNameLength / sizeof(wchar_t);
  const std::string_view filename = WideToUtf8ThreadLocal(wfilename, wchar_len);

  // Filter out NTFS system files ($MFT, $LogFile, $Bitmap, $Recycle.Bin
  // artefacts, etc.).  Offset is advanced by the caller.
  if (HandleSystemFileFilter(record, filename, file_index, metrics)) {
    return true;
  }

  const uint64_t file_ref_num = record->FileReferenceNumber;
  const uint64_t parent_ref_num = record->ParentFileReferenceNumber;

  // Update FileIndex based on the reason (extracted to reduce nesting depth)
  const bool is_directory =
      (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  const UsnRecordContext record_context{
      record, file_ref_num, parent_ref_num, filename, is_directory};
  UsnProcessingState processing_state{file_index, metrics, integrity_compromised};
  ProcessUsnRecordReasons(record_context, processing_state);

  metrics.records_processed.fetch_add(1);
  return false;  // Record processed normally
}

// Invalidate cached size for a DATA_* change. No synchronous size I/O on the
// processor thread (stalls journal drain). Misses are expected when DELETE
// preceded DATA_* in the same buffer — throttle the log.
void InvalidateSizeForDataChange(const UsnRecordContext& record_context,
                                 UsnProcessingState& processing_state) {
  if (processing_state.file_index.InvalidateSize(record_context.file_ref_num)) {
    processing_state.metrics.files_modified.fetch_add(1);
    return;
  }
  static std::atomic<size_t> s_invalidate_miss_count{0};
  const size_t miss_count = s_invalidate_miss_count.fetch_add(1) + 1;
  constexpr size_t kInvalidateMissLogInterval = 1000;
  if (miss_count == 1 || (miss_count % kInvalidateMissLogInterval) == 0) {
    LOG_WARNING_BUILD(
        "USN: InvalidateSize missed entry (count=" << miss_count
        << ", ref=" << record_context.file_ref_num
        << ") — expected when DELETE preceded DATA_* in the same buffer");
  }
  processing_state.metrics.files_modified.fetch_add(1);
}

// Helper function to process USN record reasons and update FileIndex
// Extracted to reduce nesting depth in ProcessorThread()
void ProcessUsnRecordReasons(const UsnRecordContext& record_context,
                             UsnProcessingState& processing_state) {
  const DWORD reason = record_context.record->Reason;
  // CREATE|DELETE on one close record (temp file, supersede, atomic replace):
  // DELETE wins. Independent ifs would Insert then Remove and can leave the index
  // wrong for short-lived create+delete; Remove-only matches "file is gone".
  if ((reason & USN_REASON_FILE_DELETE) != 0) {
    processing_state.file_index.Remove(record_context.file_ref_num);
    processing_state.metrics.files_deleted.fetch_add(1);
  } else if ((reason & USN_REASON_FILE_CREATE) != 0) {
    // Modification time will be lazy-loaded from file system on-demand
    // USN record TimeStamp values are always zero, so we can't use them
    // Pass kFileTimeNotLoaded to trigger lazy loading
    processing_state.file_index.Insert(record_context.file_ref_num,
                                       record_context.parent_ref_num,
                                       record_context.filename,
                                       record_context.is_directory,
                                       kFileTimeNotLoaded);
    processing_state.metrics.files_created.fetch_add(1);
  }
  if ((reason & USN_REASON_RENAME_NEW_NAME) != 0) {
    HandleRenameNewName(record_context, processing_state);
  } else if ((reason & USN_REASON_RENAME_OLD_NAME) != 0) {
    // With ReturnOnlyOnClose, close records normally carry NEW_NAME (final path).
    // OLD_NAME-only can appear with split OLD/NEW pairs or another journal reader;
    // we intentionally ignore it (no old-name buffer). Surface for diagnosis.
    LOG_IMPORTANT_BUILD(
        "USN: RENAME_OLD_NAME without RENAME_NEW_NAME (ref="
        << record_context.file_ref_num << ", name=" << record_context.filename
        << ") — index may keep a stale path until a NEW_NAME event arrives");
  }
  if ((reason & (USN_REASON_DATA_EXTEND | USN_REASON_DATA_TRUNCATION |
                 USN_REASON_DATA_OVERWRITE)) != 0) {
    // Same close record often carries DELETE|DATA_*: Remove already ran above.
    // Skip invalidate — entry is gone by design (avoids ERROR spam under delete storms).
    if ((reason & USN_REASON_FILE_DELETE) == 0) {
      InvalidateSizeForDataChange(record_context, processing_state);
    }
  }
}

// Helper: handle the case where a RENAME event arrived for an unknown file ID.
// Returns true to indicate the caller should return early.
bool HandleRenameForUnknownEntry(uint64_t file_ref_num, uint64_t parent_ref_num,
                                 std::string_view filename, bool is_directory,
                                 FileIndex& file_index, UsnMonitorMetrics& metrics) {
  // A RENAME event arrived for a file ID not in the index. During normal
  // monitoring this should not happen: the CREATE event should always precede
  // a RENAME. However, gaps can occur due to journal overflows or filtered indexing.
  LOG_WARNING_BUILD("RENAME for unknown ID " << file_ref_num << " (" << filename
                    << "): treating as CREATE (recovery path)");
  // Treat as create to keep the index consistent.
  file_index.Insert(file_ref_num, parent_ref_num, filename, is_directory, kFileTimeNotLoaded);
  metrics.files_renamed.fetch_add(1);
  return true;
}

// Helper: handle a move (parent changed) and optional rename in one operation.
void HandleMoveAndOptionalRename(uint64_t file_ref_num, uint64_t parent_ref_num,
                                 std::string_view filename, FileIndex& file_index,
                                 std::atomic<bool>& integrity_compromised) {
  // Update parent first, then update name if it also changed.
  if (!file_index.Move(file_ref_num, parent_ref_num)) {
    // entry_exists was true moments ago; Move failing almost always means a
    // DELETE in the same buffer already removed the entry. Do not latch
    // integrity and do not continue to GetPathCopy/Rename on a missing id.
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
  const std::string current_path = file_index.GetPathAccessor().GetPathCopy(file_ref_num);
  const size_t last_sep = current_path.find_last_of("/\\");
  if (const std::string current_name_str = (last_sep != std::string_view::npos)
          ? current_path.substr(last_sep + 1)
          : current_path;
      current_name_str == filename) {
    return;
  }
  if (file_index.Rename(file_ref_num, filename)) {
    return;
  }
  // Rename failed after a successful Move. Concurrent delete between the two
  // calls is expected under delete storms — only latch integrity if the entry
  // is still present (true rename failure / index divergence).
  if (!file_index.TryGetCachedAttributes(file_ref_num).has_value()) {
    return;
  }
  integrity_compromised.store(true);
  LOG_ERROR_BUILD("USN: Rename (during move) failed for ref=" << file_ref_num
                  << ", new_name=" << filename << ". Index may be stale.");
}

// Soft-delete to Recycle Bin is a rename to $R… under $Recycle.Bin, not a
// FILE_DELETE. Evict immediately so the entry does not linger until the bin is
// emptied. Mirrors InitialIndexPopulator's exclusion of $Recycle.Bin contents.
void HandleRenameNewName(const UsnRecordContext& record_context,
                         UsnProcessingState& processing_state) {
  if (IsSystemPrefixedName(record_context.filename)) {
    RemoveIndexedSubtree(processing_state.file_index,
                         record_context.file_ref_num);
    processing_state.metrics.files_deleted.fetch_add(1);
    return;
  }
  HandleFileRename(record_context, processing_state);
}

// Helper function to handle file rename operations
// Extracted to reduce nesting depth
void HandleFileRename(const UsnRecordContext& record_context,
                      UsnProcessingState& processing_state) {
  // Read the current parent ID under a shared lock.
  // GetEntry returns a raw pointer into the hash map; it must not be used after
  // the lock is released because a concurrent unique_lock acquisition (e.g.
  // Maintain, UpdateFileSizeById) could trigger a rehash and invalidate it.
  // Copying parentID before releasing the lock avoids the dangling-pointer risk
  // and protects against the data race with any concurrent writer.
  uint64_t current_parent = 0;
  bool entry_exists = false;
  {
    const std::shared_lock read_lock(processing_state.file_index.GetMutex());
    const FileEntry* const entry = processing_state.file_index.GetEntry(record_context.file_ref_num);
    if (entry != nullptr) {
      entry_exists = true;
      current_parent = entry->parentID;
    }
  }

  if (!entry_exists) {
    HandleRenameForUnknownEntry(record_context.file_ref_num,
                                record_context.parent_ref_num,
                                record_context.filename,
                                record_context.is_directory,
                                processing_state.file_index,
                                processing_state.metrics);
    return;
  }
  if (!ntfs_file_reference::SameRecordNumber(current_parent,
                                             record_context.parent_ref_num)) {
    // Parent MFT record changed - this is a move (with optional rename).
    // Compare by record number only: USN ParentFileReferenceNumber often has a
    // stale sequence while the index stores the canonical parent FRN.
    HandleMoveAndOptionalRename(record_context.file_ref_num,
                                record_context.parent_ref_num,
                                record_context.filename,
                                processing_state.file_index,
                                processing_state.integrity_compromised);
  } else {
    // Same parent record (sequence may differ) - name-only rename.
    if (!processing_state.file_index.Rename(record_context.file_ref_num,
                                            record_context.filename)) {
      // entry_exists was true moments ago; Rename failing means the entry was
      // concurrently removed. If this fires for any other reason it is divergence.
      processing_state.integrity_compromised.store(true);
      LOG_ERROR_BUILD("USN: Rename failed for ref=" << record_context.file_ref_num
                      << ", new_name=" << record_context.filename
                      << ". Index may be stale.");
    }
  }
  processing_state.metrics.files_renamed.fetch_add(1);
}

}  // anonymous namespace

// UsnMonitor class implementation

UsnMonitor::UsnMonitor(FileIndex &file_index, const MonitoringConfig &config)
    : file_index_(file_index), config_(config) {
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
    if (monitoring_active_.load()) {
      LOG_WARNING("Monitoring already active, stopping existing monitoring "
                  "before restart");
    }
  }

  // StopMonitoring acquires its own lock, so we release ours first to avoid
  // deadlock
  if (monitoring_active_.load()) {
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
  initial_population_failed_.store(false);

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
  init_promise_satisfied_.store(false);

  // Start the monitoring threads
  // Initial population will happen in the reader thread before monitoring
  // starts
  monitoring_active_.store(true);
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
    // monitoring_active_ is still true, so Stop() will proceed with cleanup
    Stop();
    return false;
  }

  // Get initialization result
  init_success = init_future_.get();
  if (!init_success) {
    LOG_ERROR("UsnMonitor initialization failed - monitoring not active");
    // Stop() will clean up threads and handle (acquires its own lock)
    // monitoring_active_ is still true, so Stop() will proceed with cleanup
    Stop();
    return false;
  }

  // Initialization succeeded - no need to reacquire lock, we're done

  LOG_INFO("UsnMonitor initialized successfully");
  return true;
}

void UsnMonitor::Stop() {  // NOLINT(readability-make-member-function-const) - Stop() mutates monitoring_active_, volume_handle_, reader_thread_, processor_thread_, and queue_; atomics are technically callable on const but the method has observable side effects
  // Always drain threads/queue even when monitoring_active_ is already false
  // (HandleInitializationFailure clears the flag before Stop/destructor run).
  // Move threads under the mutex so concurrent Stop() calls cannot double-join.
  auto handle_to_close = INVALID_HANDLE_VALUE;
  std::thread reader_to_join;
  std::thread processor_to_join;
  {
    std::scoped_lock guard(mutex_);  // NOLINT(readability-identifier-naming) - project convention snake_case for locals

    const bool had_work = monitoring_active_.load() || reader_thread_.joinable() ||
                          processor_thread_.joinable() || queue_ != nullptr;
    monitoring_active_.store(false);

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

  if (reader_to_join.joinable()) {
    reader_to_join.join();
  }

  // Unblock processor Pop() before joining it (idempotent if reader already stopped the queue).
  if (queue_ != nullptr) {
    queue_->Stop();
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
  bool was_active = monitoring_active_.load();
  if (was_active) {
    Stop();
  }
  config_ = config;
  if (was_active) {
    Start();
  }
}

void UsnMonitor::SignalInitResult(bool success) {
  bool expected = false;
  if (init_promise_satisfied_.compare_exchange_strong(expected, true)) {
    init_promise_.set_value(success);
  }
}

void UsnMonitor::HandleInitializationFailure() {
  monitoring_active_.store(false);
  // May already be satisfied (journal open signaled true before population).
  SignalInitResult(false);
  {
    std::scoped_lock guard(mutex_);  // NOLINT(readability-identifier-naming) - project convention snake_case for locals
    if (volume_handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(volume_handle_);
      volume_handle_ = INVALID_HANDLE_VALUE;
    }
  }
  if (queue_ != nullptr) {
    queue_->Stop();
  }
}

bool UsnMonitor::OpenVolumeAndQueryJournal(HANDLE& out_handle,
                                           USN_JOURNAL_DATA_V0& out_journal_data) {
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

  DWORD bytes_returned = 0;
  if (!DeviceIoControl(handle, FSCTL_QUERY_USN_JOURNAL, nullptr, 0,
                      &out_journal_data, sizeof(out_journal_data),
                      &bytes_returned, nullptr)) {
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

bool UsnMonitor::RunInitialPopulationAndPrivileges(HANDLE handle) {
  LOG_INFO("Starting initial index population");
  is_populating_index_.store(true);
  ScopedTimer timer("Initial index population");
  if (!PopulateInitialIndex(handle, file_index_, &indexed_file_count_,
                            config_.enable_mft_metadata_reading,
                            &index_integrity_compromised_)) {
    LOG_ERROR("Failed to populate initial index - stopping monitoring. Volume: " +
              config_.volume_path);
    is_populating_index_.store(false);
    initial_population_failed_.store(true);
    HandleInitializationFailure();
    return false;
  }
  indexed_file_count_.store(file_index_.Size());
  LOG_INFO_BUILD("Initial index populated with "
                 << indexed_file_count_.load() << " entries");

  // Recompute all paths before marking population complete and before monitoring
  // starts. This resolves every placeholder path (entries whose parent arrived
  // out-of-order during MFT enumeration) so that the processor thread never
  // sees incomplete paths. Calling here eliminates the race window that existed
  // when WindowsIndexBuilder called RecomputeAllPaths via a polling loop: the
  // old design left a gap of up to ~500 ms during which USN events were applied
  // against placeholder paths by the processor thread.
  {
    const ScopedTimer recompute_timer("RecomputeAllPaths (post-population)");
    file_index_.RecomputeAllPaths();
  }

  is_populating_index_.store(false);

#ifdef _WIN32
  // SECURITY: Drop unnecessary privileges AFTER initial index population completes
  // CRITICAL: Must happen AFTER PopulateInitialIndex() because MFT reading
  // (FSCTL_GET_NTFS_FILE_RECORD) requires SE_BACKUP_PRIVILEGE when
  // enable_mft_metadata_reading is true during initial population.
  //
  // See docs/security/PRIVILEGE_DROPPING_STATUS.md for detailed comparison.
  if (privilege_utils::DropUnnecessaryPrivileges()) {
    LOG_INFO("Dropped unnecessary privileges - reduced attack surface");
  } else {
    privilege_drop_failed_.store(true);
    LOG_ERROR("Failed to drop privileges - shutting down for security.");
    is_populating_index_.store(false);
    initial_population_failed_.store(true);
    HandleInitializationFailure();
    return false;
  }
#endif  // _WIN32
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
  metrics_.errors_encountered.fetch_add(1);
  metrics_.consecutive_errors.store(consecutive_errors);
  UpdateMaxConsecutiveErrors(metrics_.max_consecutive_errors, consecutive_errors);

  // Fatal journal loss: wrap, delete, or disable. Events are permanently lost —
  // latch integrity so the UI shows "Index may be stale" and stop the reader.
  if (err == ERROR_JOURNAL_ENTRY_DELETED || err == ERROR_JOURNAL_NOT_ACTIVE ||
      err == ERROR_JOURNAL_DELETE_IN_PROGRESS) {
    metrics_.journal_wrap_errors.fetch_add(1);
    index_integrity_compromised_.store(true);
    if (err == ERROR_JOURNAL_ENTRY_DELETED) {
      LOG_WARNING("USN journal wrapped; updates were lost. Index may be stale. "
                  "Stopping reader — restart the application.");
    } else if (err == ERROR_JOURNAL_NOT_ACTIVE) {
      LOG_WARNING("USN journal is not active (disabled/deleted). Index may be stale. "
                  "Stopping reader — restart the application after re-enabling the journal.");
    } else {
      LOG_WARNING("USN journal delete in progress. Index may be stale. "
                  "Stopping reader — restart the application.");
    }
    monitoring_active_.store(false);
    should_exit = true;
    return;
  }

  if (err == ERROR_INVALID_PARAMETER) {
    metrics_.invalid_param_errors.fetch_add(1);
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

  metrics_.other_errors.fetch_add(1);
  logging_utils::LogWindowsApiError("DeviceIoControl (FSCTL_READ_USN_JOURNAL)",
                                    "Volume: " + config_.volume_path +
                                        ", Retrying with backoff",
                                    err);
  std::this_thread::sleep_for(
      std::chrono::milliseconds(usn_monitor_constants::kRetryDelayMs));

  constexpr size_t max_consecutive_errors_limit = 100;
  if (consecutive_errors >= max_consecutive_errors_limit) {
    LOG_ERROR_BUILD("Too many consecutive errors (" << consecutive_errors
                                                    << "), stopping USN monitoring");
    // Unknown persistent failure: treat as integrity risk (silent stop without latch
    // would leave the UI looking healthy while events are no longer applied).
    index_integrity_compromised_.store(true);
    monitoring_active_.store(false);
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
    monitoring_active_.store(false);
    should_exit = true;
    return;
  }
  metrics_.buffers_read.fetch_add(1);

  if (!queue_) {
    LOG_ERROR("USN queue became null before Push in reader thread. Volume: " + config_.volume_path);
    monitoring_active_.store(false);
    should_exit = true;
    return;
  }

  if (!queue_->Push(std::move(queue_buffer))) {
    // Queue stop during shutdown: no integrity fault, just exit reader loop.
    if (!monitoring_active_.load() || queue_->IsStopped()) {
      should_exit = true;
      return;
    }
    // Defensive fallback: if Push unexpectedly fails while active, preserve the
    // existing integrity-latch behavior because updates may have been lost.
    index_integrity_compromised_.store(true);
    const size_t current_drops = metrics_.buffers_dropped.fetch_add(1) + 1;
    if (current_drops % usn_monitor_constants::kDropLogInterval == 0) {
      LOG_WARNING_BUILD("USN Queue push failed while active. Lost "
                        << current_drops << " buffers (queue size: "
                        << queue_->Size() << "). Index may be stale.");
    }
  }

  if (queue_ != nullptr) {
    const size_t queue_size = queue_->Size();
    metrics_.current_queue_depth.store(queue_size);
    UpdateMaxQueueDepth(metrics_.max_queue_depth, queue_size);
    MaybeLogQueueDepthBandCrossing(metrics_, queue_size, last_queue_depth_band_logged_);
  }

  if (++reader_push_count_ % usn_monitor_constants::kLogIntervalBuffers == 0 &&
      queue_) {
    const size_t queue_size = queue_->Size();
    if (queue_size > usn_monitor_constants::kQueueWarningThreshold) {
      LOG_WARNING_BUILD("USN Queue size: " << queue_size
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
 * - Runs until monitoring_active_ is set to false (via Stop())
 * - Automatically stops on fatal errors or too many consecutive errors
 * - Thread name is set for debugging/profiling tools
 *
 * Thread Safety:
 * - Reads from config_ (immutable after Start())
 * - Writes to monitoring_active_ (atomic, thread-safe)
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
    if (!RunInitialPopulationAndPrivileges(handle)) {
      return;
    }
    LOG_INFO("Initial index population complete, starting USN monitoring");

    // StartUsn was captured before MFT enum. A wrap in that window advances
    // LowestValidUsn past StartUsn; the first READ then fails with
    // ERROR_JOURNAL_ENTRY_DELETED. Detect here for a clear IMPORTANT diagnosis
    // (no auto-recovery yet — restart remains the recovery path).
    {
      USN_JOURNAL_DATA_V0 post_pop_journal{};
      DWORD bytes_returned = 0;
      if (!DeviceIoControl(handle, FSCTL_QUERY_USN_JOURNAL, nullptr, 0,
                           &post_pop_journal, sizeof(post_pop_journal),
                           &bytes_returned, nullptr)) {
        LOG_IMPORTANT_BUILD(
            "USN: post-population QUERY_USN_JOURNAL failed (err="
            << GetLastError()
            << ") — cannot verify StartUsn vs LowestValidUsn before monitoring");
      } else if (post_pop_journal.UsnJournalID != usn_journal_data.UsnJournalID) {
        LOG_IMPORTANT_BUILD(
            "USN: journal ID changed during initial population (was "
            << usn_journal_data.UsnJournalID << ", now "
            << post_pop_journal.UsnJournalID
            << ") — StartUsn may be invalid; index may miss updates until restart");
      } else if (usn_journal_data.NextUsn < post_pop_journal.LowestValidUsn) {
        LOG_IMPORTANT_BUILD(
            "USN: journal wrapped during initial population (StartUsn="
            << usn_journal_data.NextUsn << ", LowestValidUsn="
            << post_pop_journal.LowestValidUsn
            << ") — events during MFT enum were lost; index may be stale until "
               "restart");
      }
    }

    auto read_data = READ_USN_JOURNAL_DATA_V0{};
    read_data.StartUsn = usn_journal_data.NextUsn;
    // Filter at kernel level - only get records we care about
    // This reduces data transfer and processing overhead
    read_data.ReasonMask = usn_monitor_constants::kInterestingReasons;
    // TRUE: kernel accumulates reason bits and delivers one close-time record.
    // Avoids intermediate CREATE/DELETE partial records that ProcessUsnRecordReasons
    // would apply out of order (Insert then Remove → silent eviction of a live file).
    // Requires USN_REASON_CLOSE in ReasonMask (MSDN). Close-only noise is filtered
    // in ProcessOneBuffer via kActionReasons.
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

    while (monitoring_active_.load() && !should_exit) {
      auto read_start = std::chrono::steady_clock::now();

      if (!DeviceIoControl(handle, FSCTL_READ_USN_JOURNAL, &read_data,
                           sizeof(read_data), buffer.data(), buffer_size,
                           &bytes_returned, nullptr)) {
        HandleReadJournalError(GetLastError(), consecutive_errors, should_exit);
        continue;
      }

      // Track read time
      auto read_end = std::chrono::steady_clock::now();
      auto read_duration =
          std::chrono::duration_cast<std::chrono::milliseconds>(read_end -
                                                                read_start);
      metrics_.total_read_time_ms.fetch_add(read_duration.count());

      // Reset error counter on success
      consecutive_errors = 0;
      metrics_.consecutive_errors.store(0);

      ProcessSuccessfulReadAndEnqueue(buffer, buffer_size, bytes_returned,
                                     read_data, should_exit);
    }

    LOG_INFO("USN Reader thread stopping");
  } catch (const std::exception &e) {  // NOSONAR(cpp:S1181) NOLINT(bugprone-empty-catch) - log and continue; cannot rethrow from thread
    logging_utils::LogException("USN Reader thread",
                                "Volume: " + config_.volume_path,
                                e);
    monitoring_active_.store(false);
  } catch (...) {  // NOSONAR(cpp:S2738) NOLINT(bugprone-empty-catch) - log and continue; cannot rethrow from thread
    logging_utils::LogUnknownException("USN Reader thread",
                                      "Volume: " + config_.volume_path);
    monitoring_active_.store(false);
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
      CloseHandle(volume_handle_);
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
void UsnMonitor::ProcessOneBuffer(std::vector<char>& buffer) {  // NOLINT(readability-identifier-naming) - PascalCase member function; check misclassifies as global variable
  auto buffer_process_start = std::chrono::steady_clock::now();
  auto bytes_returned = static_cast<DWORD>(buffer.size());  // NOSONAR(cpp:S1905) - Cast needed for Windows USN API (DWORD is 32-bit, size_t may be 64-bit)
  DWORD offset = usn_record_utils::SizeOfUsn();

  while (offset < bytes_returned) {
    PUSN_RECORD_V2 record = nullptr;
    if (!usn_record_utils::ValidateAndParseUsnRecord(
            buffer.data(), bytes_returned, offset, record)) {
      // Corrupt record: all remaining events in this buffer are permanently
      // lost. Set the integrity latch so the UI shows "Index may be stale".
      index_integrity_compromised_.store(true);
      LOG_ERROR_BUILD("USN record corrupt at offset " << offset
                      << "; remaining buffer records skipped. Index may be stale.");
      break;
    }

    // ReasonMask includes CLOSE (required with ReturnOnlyOnClose). Skip records
    // that have no create/delete/rename/data action — close-only noise.
    if ((record->Reason & usn_monitor_constants::kActionReasons) == 0) {
      offset += record->RecordLength;
      continue;
    }

    if (ProcessInterestingUsnRecord(record, file_index_, metrics_, index_integrity_compromised_)) {
      offset += record->RecordLength;
      continue;
    }

    offset += record->RecordLength;
  }

  indexed_file_count_.store(file_index_.Size());

  auto buffer_process_end = std::chrono::steady_clock::now();
  auto process_duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          buffer_process_end - buffer_process_start);
  metrics_.total_process_time_ms.fetch_add(process_duration.count());

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
