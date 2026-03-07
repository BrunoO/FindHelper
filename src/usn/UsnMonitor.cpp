#include "usn/UsnMonitor.h"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include "platform/windows/PrivilegeUtils.h"
#include "utils/LoggingUtils.h"
#include "utils/ScopedHandle.h"
#endif  // _WIN32

#include "usn/UsnRecordUtils.h"
#include "utils/FileSystemUtils.h"

namespace {

// Forward declarations for helper functions
void ProcessUsnRecordReasons(PUSN_RECORD_V2 record,
                             uint64_t file_ref_num,
                             uint64_t parent_ref_num,
                             std::string_view filename,
                             bool is_directory,
                             FileIndex& file_index,
                             UsnMonitorMetrics& metrics);

void HandleFileRename(PUSN_RECORD_V2 record,
                      uint64_t file_ref_num,
                      uint64_t parent_ref_num,
                      std::string_view filename,
                      bool is_directory,
                      FileIndex& file_index,
                      UsnMonitorMetrics& metrics);

// Helper function to update max consecutive errors using atomic compare-and-swap
// Extracted to reduce nesting depth
void UpdateMaxConsecutiveErrors(std::atomic<size_t>& max_consecutive_errors, size_t consecutive_errors) {  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - PascalCase; check misclassifies anonymous-namespace function as global
  size_t current_max = max_consecutive_errors.load(std::memory_order_relaxed);
  while (consecutive_errors > current_max) {
    if (max_consecutive_errors.compare_exchange_weak(
            current_max, consecutive_errors, std::memory_order_relaxed)) {
      break;
    }
  }
}

// Helper function to update max queue depth using atomic compare-and-swap
// Extracted to reduce nesting depth
void UpdateMaxQueueDepth(std::atomic<size_t>& max_queue_depth, size_t queue_size) {  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - PascalCase; check misclassifies anonymous-namespace function as global
  size_t current_max = max_queue_depth.load(std::memory_order_relaxed);
  while (queue_size > current_max) {
    if (max_queue_depth.compare_exchange_weak(
            current_max, queue_size, std::memory_order_relaxed)) {
      break;
    }
  }
}

// Returns true if the file should be filtered out (name starts with '$').
// Side effects: removes the entry from the index (safety net for any $-file
// that slipped in during initial population) and updates delete metrics.
// Does NOT touch offset — offset management is the caller's responsibility.
bool HandleSystemFileFilter(PUSN_RECORD_V2 record, std::string_view filename,
                            FileIndex& file_index, UsnMonitorMetrics& metrics) {
  if (filename.empty() || filename[0] != usn_monitor_constants::kSystemFilePrefix) {
    return false;
  }

  // Count genuine delete events even though we skip the record.
  if (record->Reason & USN_REASON_FILE_DELETE) {
    metrics.files_deleted.fetch_add(1, std::memory_order_relaxed);
  }
  // Evict from index on any event type: acts as a safety net in case a
  // $-prefixed file was indexed (e.g. via MFT enumeration on an older build).
  file_index.Remove(record->FileReferenceNumber);
  return true;
}

// Helper function to process interesting USN record.
// Returns true if the record was filtered (caller should advance offset and
// continue). Returns false if the record was processed normally.
// Does NOT touch offset — offset management belongs in ProcessOneBuffer.
bool ProcessInterestingUsnRecord(PUSN_RECORD_V2 record,
                                 FileIndex& file_index,
                                 UsnMonitorMetrics& metrics) {
  std::wstring wfilename(
      reinterpret_cast<wchar_t*>(reinterpret_cast<std::byte*>(record) + record->FileNameOffset),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR(cpp:S3630) - USN_RECORD_V2 variable-offset filename; bounds checked by ValidateAndParseUsnRecord
      record->FileNameLength / sizeof(wchar_t));

  auto filename = WideToUtf8(wfilename);

  // Filter out NTFS system files ($MFT, $LogFile, $Bitmap, $Recycle.Bin
  // artefacts, etc.).  Offset is advanced by the caller.
  if (HandleSystemFileFilter(record, filename, file_index, metrics)) {
    return true;
  }

  uint64_t file_ref_num = record->FileReferenceNumber;
  uint64_t parent_ref_num = record->ParentFileReferenceNumber;

  // Update FileIndex based on the reason (extracted to reduce nesting depth)
  bool is_directory =  // NOLINT(cppcoreguidelines-init-variables) - Initialized from boolean expression
      (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  ProcessUsnRecordReasons(record, file_ref_num, parent_ref_num, filename,
                          is_directory, file_index, metrics);

  metrics.records_processed.fetch_add(1, std::memory_order_relaxed);
  return false;  // Record processed normally
}

// Helper function to process USN record reasons and update FileIndex
// Extracted to reduce nesting depth in ProcessorThread()
void ProcessUsnRecordReasons(PUSN_RECORD_V2 record,
                             uint64_t file_ref_num,
                             uint64_t parent_ref_num,
                             std::string_view filename,
                             bool is_directory,
                             FileIndex& file_index,
                             UsnMonitorMetrics& metrics) {
  if (record->Reason & USN_REASON_FILE_CREATE) {
    // Modification time will be lazy-loaded from file system on-demand
    // USN record TimeStamp values are always zero, so we can't use them
    // Pass kFileTimeNotLoaded to trigger lazy loading
    file_index.Insert(file_ref_num, parent_ref_num, filename,
                      is_directory, kFileTimeNotLoaded);
    metrics.files_created.fetch_add(1, std::memory_order_relaxed);
  }
  if (record->Reason & USN_REASON_FILE_DELETE) {
    file_index.Remove(file_ref_num);
    metrics.files_deleted.fetch_add(1, std::memory_order_relaxed);
  }
  if (record->Reason & USN_REASON_RENAME_NEW_NAME) {
    HandleFileRename(record, file_ref_num, parent_ref_num, filename, is_directory,
                     file_index, metrics);
  }
  if (record->Reason &
      (USN_REASON_DATA_EXTEND | USN_REASON_DATA_TRUNCATION |
       USN_REASON_DATA_OVERWRITE)) {
    // Update file size (modification time is lazy-loaded from file
    // system) NOTE: USN record TimeStamp values are always zero, so we
    // can't use them Modification time will be loaded on-demand via
    // GetFileModificationTimeById()
    if (!file_index.UpdateSize(file_ref_num)) {
      LOG_WARNING_BUILD("Failed to update file size in index: ref=" << file_ref_num);
    }
    metrics.files_modified.fetch_add(1, std::memory_order_relaxed);
  }
}

// Helper function to handle file rename operations
// Extracted to reduce nesting depth
void HandleFileRename(PUSN_RECORD_V2 record, uint64_t file_ref_num, uint64_t parent_ref_num,
                      std::string_view filename, bool is_directory, FileIndex& file_index,
                      UsnMonitorMetrics& metrics) {
  // Read the current parent ID under a shared lock.
  // GetEntry returns a raw pointer into the hash map; it must not be used after
  // the lock is released because a concurrent unique_lock acquisition (e.g.
  // Maintain, UpdateFileSizeById) could trigger a rehash and invalidate it.
  // Copying parentID before releasing the lock avoids the dangling-pointer risk
  // and protects against the data race with any concurrent writer.
  uint64_t current_parent = 0;
  bool entry_exists = false;
  {
    const std::shared_lock read_lock(file_index.GetMutex());
    const FileEntry* const entry = file_index.GetEntry(file_ref_num);
    if (entry != nullptr) {
      entry_exists = true;
      current_parent = entry->parentID;
    }
  }

  if (!entry_exists) {
    // Entry doesn't exist yet - treat as create (shouldn't happen, but handle gracefully)
    file_index.Insert(file_ref_num, parent_ref_num, filename, is_directory, kFileTimeNotLoaded);
    metrics.files_renamed.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (current_parent != parent_ref_num) {
    // Parent changed - this is a move operation
    // Update parent first, then update name if it also changed
    if (!file_index.Move(file_ref_num, parent_ref_num)) {
      LOG_WARNING_BUILD("Failed to move file in index: ref=" << file_ref_num
                  << ", new_parent=" << parent_ref_num);
    }
    // Derive current name from PathStorage and check if it also changed
    std::string current_name_str;
    {
      const auto accessor = file_index.GetPathAccessor();
      const std::string_view current_path = accessor.GetPathView(file_ref_num);
      const size_t last_sep = current_path.find_last_of("/\\");
      current_name_str = (last_sep != std::string_view::npos)
          ? std::string(current_path.substr(last_sep + 1))
          : std::string(current_path);
    }
    if (current_name_str != filename && !file_index.Rename(file_ref_num, filename)) {
      LOG_WARNING_BUILD("Failed to rename file in index: ref=" << file_ref_num
                  << ", new_name=" << filename);
    }
  } else {
    // Only name changed (same parent) - simple rename
    if (!file_index.Rename(file_ref_num, filename)) {
      LOG_WARNING_BUILD("Failed to rename file in index: ref=" << file_ref_num 
                  << ", new_name=" << filename);
    }
  }
  metrics.files_renamed.fetch_add(1, std::memory_order_relaxed);
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
    if (monitoring_active_.load(std::memory_order_acquire)) {
      LOG_WARNING("Monitoring already active, stopping existing monitoring "
                  "before restart");
    }
  }

  // StopMonitoring acquires its own lock, so we release ours first to avoid
  // deadlock
  if (monitoring_active_.load(std::memory_order_acquire)) {
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

  // Start the monitoring threads
  // Initial population will happen in the reader thread before monitoring
  // starts
  monitoring_active_.store(true, std::memory_order_release);
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

void UsnMonitor::Stop() {
  auto handle_to_close = INVALID_HANDLE_VALUE;
  {
    std::scoped_lock guard(mutex_);  // NOLINT(readability-identifier-naming) - project convention snake_case for locals

    // Prevent double-stop: if not monitoring, do nothing
    if (!monitoring_active_.load(std::memory_order_acquire)) {
      return;
    }

    LOG_INFO("Stopping USN monitoring");
    monitoring_active_.store(false, std::memory_order_release);

    // Capture handle under lock, but close it outside the critical section to
    // avoid deadlock with ReaderThread's final cleanup block.
    handle_to_close = volume_handle_;
    volume_handle_ = INVALID_HANDLE_VALUE;
  }

  // Cancel any pending I/O operations and close the handle outside the mutex.
  if (handle_to_close != INVALID_HANDLE_VALUE) {
    CancelIoEx(handle_to_close, nullptr);  // Cancel pending I/O operations
    CloseHandle(handle_to_close);          // Close handle to cause immediate I/O failure
  }

  // Wait for reader thread to finish
  // Threads will see monitoring_active_ flag on their next loop iteration
  // No sleep needed - join() will wait for actual thread completion
  if (reader_thread_.joinable()) {
    reader_thread_.join();
  }

  // Signal queue to stop and wait for processor thread
  if (queue_ != nullptr) {
    queue_->Stop();
  }
  if (processor_thread_.joinable()) {
    processor_thread_.join();
  }

  // Clean up queue under lock (queue_ is shared with worker threads)
  {
    std::scoped_lock guard(mutex_);  // NOLINT(readability-identifier-naming)
    if (queue_ != nullptr) {
      if (size_t dropped_count = metrics_.buffers_dropped.load(std::memory_order_relaxed); dropped_count > 0) {
        LOG_WARNING_BUILD("Queue had " << dropped_count
                                       << " dropped buffers during monitoring");
      }
      queue_.reset();
    }
  }

  LOG_INFO("USN monitoring stopped");
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
  return metrics_.buffers_dropped.load(std::memory_order_relaxed);
}

void UsnMonitor::UpdateConfig(const MonitoringConfig &config) {
  std::scoped_lock guard(mutex_);  // NOLINT(readability-identifier-naming) - project convention snake_case for locals
  bool was_active = monitoring_active_.load();  // NOLINT(cppcoreguidelines-init-variables) - Initialized from atomic load() return value
  if (was_active) {
    Stop();
  }
  config_ = config;
  if (was_active) {
    Start();
  }
}

void UsnMonitor::HandleInitializationFailure() {
  monitoring_active_.store(false, std::memory_order_release);
  init_promise_.set_value(false);
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
  is_populating_index_.store(true, std::memory_order_release);
  ScopedTimer timer("Initial index population");
  if (!PopulateInitialIndex(handle, file_index_, &indexed_file_count_)) {
    LOG_ERROR("Failed to populate initial index - stopping monitoring. Volume: " +
              config_.volume_path);
    is_populating_index_.store(false, std::memory_order_release);
    HandleInitializationFailure();
    return false;
  }
  indexed_file_count_.store(file_index_.Size(), std::memory_order_release);
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

  is_populating_index_.store(false, std::memory_order_release);

#ifdef _WIN32
  // SECURITY: Drop unnecessary privileges AFTER initial index population completes
  // CRITICAL: Must happen AFTER PopulateInitialIndex() because MFT reading
  // (FSCTL_GET_NTFS_FILE_RECORD) requires SE_BACKUP_PRIVILEGE which is needed
  // during initial population when MFT metadata reading is enabled.
  //
  // See docs/security/PRIVILEGE_DROPPING_STATUS.md for detailed comparison.
  if (privilege_utils::DropUnnecessaryPrivileges()) {
    LOG_INFO("Dropped unnecessary privileges - reduced attack surface");
  } else {
    privilege_drop_failed_.store(true, std::memory_order_release);
    LOG_ERROR("Failed to drop privileges - shutting down for security.");
    HandleInitializationFailure();
    return false;
  }
#endif  // _WIN32
  return true;
}

void UsnMonitor::HandleReadJournalError(DWORD err, size_t& consecutive_errors,
                                         bool& should_exit) {
  consecutive_errors++;
  metrics_.errors_encountered.fetch_add(1, std::memory_order_relaxed);
  metrics_.consecutive_errors.store(consecutive_errors, std::memory_order_relaxed);
  UpdateMaxConsecutiveErrors(metrics_.max_consecutive_errors, consecutive_errors);

  if (err == ERROR_JOURNAL_ENTRY_DELETED) {
    metrics_.journal_wrap_errors.fetch_add(1, std::memory_order_relaxed);
    LOG_INFO("USN journal entry deleted (journal wrapped), continuing "
             "from current position");
    std::this_thread::sleep_for(
        std::chrono::milliseconds(usn_monitor_constants::kJournalWrapDelayMs));
    consecutive_errors = 0;
    metrics_.consecutive_errors.store(0, std::memory_order_relaxed);
    return;
  }

  if (err == ERROR_INVALID_PARAMETER) {
    metrics_.invalid_param_errors.fetch_add(1, std::memory_order_relaxed);
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

  metrics_.other_errors.fetch_add(1, std::memory_order_relaxed);
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
    monitoring_active_.store(false, std::memory_order_release);
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
    monitoring_active_.store(false, std::memory_order_release);
    should_exit = true;
    return;
  }
  metrics_.buffers_read.fetch_add(1, std::memory_order_relaxed);

  if (!queue_) {
    LOG_ERROR("USN queue became null before Push in reader thread. Volume: " + config_.volume_path);
    monitoring_active_.store(false, std::memory_order_release);
    should_exit = true;
    return;
  }

  if (!queue_->Push(std::move(queue_buffer))) {
    size_t current_drops = metrics_.buffers_dropped.fetch_add(1, std::memory_order_relaxed) + 1;  // NOLINT(cppcoreguidelines-init-variables) NOSONAR(cpp:S6004) - initialized by fetch_add+1; used after if
    if (current_drops % usn_monitor_constants::kDropLogInterval == 0) {
      LOG_WARNING_BUILD("USN Queue full! Dropped "
                        << current_drops << " buffers (queue size: "
                        << queue_->Size() << ")");
    }
  }

  if (queue_ != nullptr) {
    size_t queue_size = queue_->Size();  // NOLINT(cppcoreguidelines-init-variables) - initialized by Size()
    metrics_.current_queue_depth.store(queue_size,
                                       std::memory_order_relaxed);
    UpdateMaxQueueDepth(metrics_.max_queue_depth, queue_size);
  }

  if (++reader_push_count_ % usn_monitor_constants::kLogIntervalBuffers == 0 &&
      queue_) {
    size_t queue_size = queue_->Size();  // NOLINT(cppcoreguidelines-init-variables) - initialized by Size()
    if (queue_size > usn_monitor_constants::kQueueWarningThreshold) {
      LOG_WARNING_BUILD("USN Queue size: " << queue_size
                                           << " buffers pending");
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
    init_promise_.set_value(true);
    if (!RunInitialPopulationAndPrivileges(handle)) {
      return;
    }
    LOG_INFO("Initial index population complete, starting USN monitoring");

    auto read_data = READ_USN_JOURNAL_DATA_V0{};
    read_data.StartUsn = usn_journal_data.NextUsn;
    // Filter at kernel level - only get records we care about
    // This reduces data transfer and processing overhead
    read_data.ReasonMask = usn_monitor_constants::kInterestingReasons;
    read_data.ReturnOnlyOnClose = FALSE;
    read_data.Timeout = config_.timeout_ms;
    // BytesToWaitFor: Wait for at least configured bytes of unfiltered data
    // before returning This batches multiple records together, reducing call
    // frequency while maintaining low latency. The value is in unfiltered bytes
    // (before ReasonMask).
    read_data.BytesToWaitFor = static_cast<DWORD>(config_.bytes_to_wait_for);
    read_data.UsnJournalID = usn_journal_data.UsnJournalID;

    const int buffer_size = config_.buffer_size;  // NOSONAR(cpp:S1854) - Used in buffer allocation below // NOLINT(cppcoreguidelines-init-variables) - Initialized from config member

    std::vector<char> buffer(buffer_size);
    size_t consecutive_errors = 0;
    bool should_exit = false;  // NOLINT(misc-const-correctness) - modified by reference in HandleReadJournalError/ProcessSuccessfulReadAndEnqueue
    DWORD bytes_returned = 0;

    while (monitoring_active_.load(std::memory_order_acquire) && !should_exit) {
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
      metrics_.total_read_time_ms.fetch_add(read_duration.count(),
                                            std::memory_order_relaxed);

      // Reset error counter on success
      consecutive_errors = 0;
      metrics_.consecutive_errors.store(0, std::memory_order_relaxed);

      ProcessSuccessfulReadAndEnqueue(buffer, buffer_size, bytes_returned,
                                     read_data, should_exit);
    }

    LOG_INFO("USN Reader thread stopping");
  } catch (const std::exception &e) {  // NOSONAR(cpp:S1181) NOLINT(bugprone-empty-catch) - log and continue; cannot rethrow from thread
    logging_utils::LogException("USN Reader thread",
                                "Volume: " + config_.volume_path,
                                e);
    monitoring_active_.store(false, std::memory_order_release);
  } catch (...) {  // NOSONAR(cpp:S2738) NOLINT(bugprone-empty-catch) - log and continue; cannot rethrow from thread
    logging_utils::LogUnknownException("USN Reader thread",
                                      "Volume: " + config_.volume_path);
    monitoring_active_.store(false, std::memory_order_release);
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
      LOG_WARNING_BUILD("Invalid USN record at offset " << offset
                        << ", stopping buffer processing");
      break;
    }

    if (!(record->Reason & usn_monitor_constants::kInterestingReasons)) {
      offset += record->RecordLength;
      continue;
    }

    if (ProcessInterestingUsnRecord(record, file_index_, metrics_)) {
      offset += record->RecordLength;
      continue;
    }

    offset += record->RecordLength;
  }

  indexed_file_count_.store(file_index_.Size(), std::memory_order_release);

  auto buffer_process_end = std::chrono::steady_clock::now();
  auto process_duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          buffer_process_end - buffer_process_start);
  metrics_.total_process_time_ms.fetch_add(process_duration.count(),
                                           std::memory_order_relaxed);

  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();
  metrics_.last_update_time_ms.store(now_ms, std::memory_order_relaxed);

  std::this_thread::yield();

  metrics_.buffers_processed.fetch_add(1, std::memory_order_relaxed);

  size_t buffers_processed =  // NOLINT(cppcoreguidelines-init-variables) - initialized by load()
      metrics_.buffers_processed.load(std::memory_order_relaxed);
  if (buffers_processed % usn_monitor_constants::kLogIntervalBuffers == 0) {
    size_t queue_size = queue_->Size();  // NOLINT(cppcoreguidelines-init-variables) - initialized by Size()
    size_t dropped_count = metrics_.buffers_dropped.load(std::memory_order_relaxed);  // NOLINT(cppcoreguidelines-init-variables) - initialized by load()
    size_t total_records =  // NOLINT(cppcoreguidelines-init-variables) - initialized by load()
        metrics_.records_processed.load(std::memory_order_relaxed);
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
