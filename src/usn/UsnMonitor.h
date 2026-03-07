#pragma once

/**
 * UsnMonitor - Windows USN Journal File System Monitor
 *
 * This module provides real-time monitoring of file system changes on Windows
 * by reading from the USN (Update Sequence Number) Journal. It maintains an
 * in-memory index of files and directories that is kept synchronized with the
 * actual file system state.
 *
 * THREADING ARCHITECTURE:
 * The implementation uses a producer-consumer pattern with two dedicated
 * threads:
 *
 * 1. Reader Thread (UsnReaderThread):
 *    - Blocks on DeviceIoControl() calls to read USN journal entries from the
 * kernel
 *    - Uses kernel-level filtering (ReasonMask) to reduce data transfer
 *    - Batches records using BytesToWaitFor (8KB) to balance latency vs
 * efficiency
 *    - Pushes complete buffers to a thread-safe queue for processing
 *    - Also performs initial index population before monitoring begins
 *
 * 2. Processor Thread (UsnProcessorThread):
 *    - Continuously pops buffers from the queue and parses USN records
 *    - Updates the FileIndex based on file create/delete/rename/size change
 * events
 *    - Yields after each buffer to allow UI thread to acquire FileIndex locks
 *    - Filters out system files (starting with '$') to reduce noise
 *
 * The two-thread design separates I/O blocking from CPU-intensive processing,
 * allowing the reader to continue fetching new journal entries while the
 * processor handles backlog. This prevents the kernel from dropping journal
 * entries during processing spikes.
 *
 * DESIGN TRADE-OFFS:
 *
 * 1. Two-thread vs single-thread:
 *    - PRO: Reader can continue fetching while processor handles backlog
 *    - PRO: UI remains responsive during processing bursts
 *    - CON: Added complexity with queue synchronization and thread coordination
 *    - CON: Memory overhead from queued buffers during high activity
 *
 * 2. Queue-based communication:
 *    - PRO: Decouples I/O from processing, handles bursts gracefully
 *    - PRO: Allows batching multiple records per buffer
 *    - CON: Can accumulate backlog if processor falls behind (monitored via
 * logs)
 *    - NOTE: Queue size is now limited with backpressure (50ms delay when full)
 *
 * 3. Kernel-level filtering (ReasonMask):
 *    - PRO: Reduces data transfer and processing overhead
 *    - PRO: Only fetches events we actually care about
 *    - CON: Less flexible - requires reconfiguration to change event types
 *
 * 4. BytesToWaitFor batching (8KB):
 *    - PRO: Reduces DeviceIoControl call frequency
 *    - PRO: Batches 2-4 typical records together for efficiency
 *    - CON: Adds ~1 second latency during low activity (Timeout parameter)
 *    - CON: Fixed size may not be optimal for all workloads
 *
 * 5. Initial population in reader thread:
 *    - PRO: Keeps GUI thread responsive during initial scan
 *    - PRO: Natural sequencing - populate then monitor
 *    - CON: Delays start of real-time monitoring until population completes
 *
 * 6. Thread yielding after each buffer:
 *    - PRO: Ensures UI can acquire FileIndex locks for searches
 *    - PRO: Prevents processor from starving UI thread
 *    - CON: Slightly reduces processing throughput
 *
 * POSSIBLE FUTURE IMPROVEMENTS:
 *
 * 1. Backpressure mechanism: Add queue size limits and flow control to prevent
 *    unbounded memory growth during sustained high activity.
 *
 * 2. Configurable parameters: Make buffer size, BytesToWaitFor, and Timeout
 *    configurable to tune for different workloads (SSD vs HDD, high vs low
 * activity).
 *
 * 3. Metrics and monitoring: Add detailed metrics for queue depth, processing
 *    latency, records per second, and backlog trends to aid performance tuning.
 *
 * 4. Multi-volume support: Extend to monitor multiple volumes simultaneously
 *    with separate reader/processor pairs per volume.
 *
 * 5. Error recovery: Implement more sophisticated error handling and recovery
 *    strategies for journal wrap-around, volume disconnection, and permission
 * issues.
 *
 * 6. Batch index updates: Optimize FileIndex operations by batching multiple
 *    updates together to reduce lock contention with UI thread.
 *
 * 7. Filtering configuration: Make file filtering rules (e.g., '$' prefix)
 *    configurable rather than hardcoded.
 *
 * 8. Adaptive batching: Dynamically adjust BytesToWaitFor based on observed
 *    activity levels to optimize for both low-latency and high-throughput
 * scenarios.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <winioctl.h>
#include "index/FileIndex.h"
#include "index/InitialIndexPopulator.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"
#include "utils/ThreadUtils.h"
#else
// macOS stub: minimal forward declarations and stub class
#include "index/FileIndex.h"
#include "utils/Logger.h"
#include <cstddef>
#endif  // _WIN32

#ifdef _WIN32
// RAII wrapper for Windows HANDLE to ensure proper cleanup
class VolumeHandle {
public:
  explicit VolumeHandle(const char *path) {
    // SECURITY: Use FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE
    // This provides:
    // - FILE_READ_DATA: Required for FSCTL_GET_NTFS_FILE_RECORD (MFT reading)
    // - FILE_READ_ATTRIBUTES: Required for file attribute queries
    // - SYNCHRONIZE: Required for synchronous I/O operations
    // All USN Journal operations (FSCTL_READ_USN_JOURNAL, etc.) work with these permissions
    // This is more explicit and correct than GENERIC_READ for volume handles
    handle_ = CreateFileA(path, FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                          OPEN_EXISTING, 0, nullptr);
  }

  ~VolumeHandle() {
    if (handle_ != INVALID_HANDLE_VALUE) {
      try {
        if (!CloseHandle(handle_)) {
          // Log CloseHandle failure (don't throw from destructor)
          LOG_ERROR_BUILD("VolumeHandle: CloseHandle failed with error " << GetLastError());
        }
      } catch (...) {  // NOSONAR(cpp:S2738, cpp:S2486) - Destructors should never throw. Log error but don't propagate exception. This prevents std::terminate if destructor is called during stack unwinding.
        LOG_ERROR("VolumeHandle: Unknown exception during CloseHandle");
      }
    }
  }

  // Non-copyable
  VolumeHandle(const VolumeHandle &) = delete;
  VolumeHandle &operator=(const VolumeHandle &) = delete;

  // Movable
  VolumeHandle(VolumeHandle &&other) noexcept : handle_(other.handle_) {
    other.handle_ = INVALID_HANDLE_VALUE;
  }

  VolumeHandle &operator=(VolumeHandle &&other) noexcept {
    if (this != &other) {
      if (handle_ != INVALID_HANDLE_VALUE) {
        try {
          CloseHandle(handle_);
        } catch (...) {  // NOSONAR(cpp:S2738, cpp:S2486) - Destructors should never throw. Log error but don't propagate exception.
        }
      }
      handle_ = other.handle_;
      other.handle_ = INVALID_HANDLE_VALUE;
    }
    return *this;
  }

  HANDLE get() const { return handle_; }
  bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }
  operator HANDLE() const { return handle_; }  // NOSONAR(cpp:S1709) - Implicit conversion intentionally used for handle convenience (similar to std::unique_ptr::get())

private:
  HANDLE handle_;
};

// Configuration constants for USN monitoring
namespace usn_monitor_constants {
// Buffer and queue configuration
constexpr int kBufferSizeBytes = 64 * 1024; // 64KB read buffer
constexpr int kBytesToWaitFor = 24 * 1024; // 24KB batching threshold
constexpr DWORD kTimeoutMs = 1000;           // 1 second timeout
constexpr size_t kDefaultMaxQueueSize =
    1000 * 10; // ~640MB max queue memory (1000 * 64KB)
constexpr size_t kQueueWarningThreshold = 10; // Warn if queue exceeds this
constexpr size_t kLogIntervalBuffers = 1000;  // Log stats every N buffers
constexpr size_t kDropLogInterval = 10;       // Log every Nth drop to avoid spam

// Retry delays (milliseconds)
constexpr DWORD kRetryDelayMs = 250;          // General retry delay
constexpr DWORD kJournalWrapDelayMs = 50;    // Journal wrap-around delay
constexpr DWORD kInvalidParamDelayMs = 1000; // Invalid parameter retry delay

// USN reason mask - events we care about
constexpr DWORD kInterestingReasons =
    USN_REASON_FILE_CREATE | USN_REASON_FILE_DELETE |
    USN_REASON_RENAME_OLD_NAME | USN_REASON_RENAME_NEW_NAME |
    USN_REASON_DATA_EXTEND | USN_REASON_DATA_TRUNCATION |
    USN_REASON_DATA_OVERWRITE | USN_REASON_CLOSE;

// File filtering
constexpr char kSystemFilePrefix = '$'; // Filter files starting with this
constexpr const char *kDefaultVolumePath =
    "\\\\.\\C:"; // Default volume to monitor  // NOSONAR(cpp:S3628) - Raw string literal would be less readable for Windows path

// Maintenance configuration
constexpr int kMaintenanceCheckIntervalSeconds = 5; // Check every N seconds during idle time
} // namespace usn_monitor_constants

// Configuration structure for monitoring parameters
struct MonitoringConfig {
  std::string volume_path = usn_monitor_constants::kDefaultVolumePath;
  int buffer_size = usn_monitor_constants::kBufferSizeBytes;
  DWORD timeout_ms = usn_monitor_constants::kTimeoutMs;
  size_t bytes_to_wait_for = usn_monitor_constants::kBytesToWaitFor;
  size_t max_queue_size = usn_monitor_constants::kDefaultMaxQueueSize;
};

// Metrics structure for monitoring performance and health
// All metrics are thread-safe (using atomics) and can be queried without
// locking
struct UsnMonitorMetrics {
  // Processing statistics
  std::atomic<size_t> buffers_read{0}; // Total buffers read from USN journal
  std::atomic<size_t> buffers_processed{0}; // Total buffers processed
  std::atomic<size_t> records_processed{0}; // Total USN records processed
  std::atomic<size_t> files_created{0};     // Files created events
  std::atomic<size_t> files_deleted{0};     // Files deleted events
  std::atomic<size_t> files_renamed{0};     // Files renamed events
  std::atomic<size_t> files_modified{0}; // Files modified (size change) events

  // Queue statistics
  std::atomic<size_t> max_queue_depth{0}; // Maximum queue depth reached
  std::atomic<size_t> buffers_dropped{0}; // Buffers dropped due to queue full
  std::atomic<size_t> current_queue_depth{
      0}; // Current queue depth (updated periodically)

  // Error statistics
  std::atomic<size_t> errors_encountered{0};   // Total errors encountered
  std::atomic<size_t> journal_wrap_errors{0};  // Journal wrap-around events
  std::atomic<size_t> invalid_param_errors{0}; // Invalid parameter errors
  std::atomic<size_t> other_errors{0};         // Other types of errors
  std::atomic<size_t> consecutive_errors{0}; // Current consecutive error count
  std::atomic<size_t> max_consecutive_errors{0}; // Maximum consecutive errors

  // Timing statistics (in milliseconds)
  std::atomic<uint64_t> total_read_time_ms{
      0}; // Total time spent reading from journal
  std::atomic<uint64_t> total_process_time_ms{
      0}; // Total time spent processing buffers
  std::atomic<uint64_t> last_update_time_ms{
      0}; // Last metrics update time (for rate calculation)

  // Reset all metrics to zero
  void Reset() {
    buffers_read.store(0);
    buffers_processed.store(0);
    records_processed.store(0);
    files_created.store(0);
    files_deleted.store(0);
    files_renamed.store(0);
    files_modified.store(0);
    max_queue_depth.store(0);
    buffers_dropped.store(0);
    current_queue_depth.store(0);
    errors_encountered.store(0);
    journal_wrap_errors.store(0);
    invalid_param_errors.store(0);
    other_errors.store(0);
    consecutive_errors.store(0);
    max_consecutive_errors.store(0);
    total_read_time_ms.store(0);
    total_process_time_ms.store(0);
    last_update_time_ms.store(0);
  }

  // Get a snapshot of all metrics (thread-safe)
  // Returns a struct with regular (non-atomic) values for easy reading
  struct Snapshot {
    size_t buffers_read;
    size_t buffers_processed;
    size_t records_processed;
    size_t files_created;
    size_t files_deleted;
    size_t files_renamed;
    size_t files_modified;
    size_t max_queue_depth;
    size_t buffers_dropped;
    size_t current_queue_depth;
    size_t errors_encountered;
    size_t journal_wrap_errors;
    size_t invalid_param_errors;
    size_t other_errors;
    size_t consecutive_errors;
    size_t max_consecutive_errors;
    uint64_t total_read_time_ms;
    uint64_t total_process_time_ms;
    uint64_t last_update_time_ms;
  };

  [[nodiscard]] Snapshot GetSnapshot() const {
    Snapshot snapshot;
    snapshot.buffers_read = buffers_read.load(std::memory_order_acquire);
    snapshot.buffers_processed =
        buffers_processed.load(std::memory_order_acquire);
    snapshot.records_processed =
        records_processed.load(std::memory_order_acquire);
    snapshot.files_created = files_created.load(std::memory_order_acquire);
    snapshot.files_deleted = files_deleted.load(std::memory_order_acquire);
    snapshot.files_renamed = files_renamed.load(std::memory_order_acquire);
    snapshot.files_modified = files_modified.load(std::memory_order_acquire);
    snapshot.max_queue_depth = max_queue_depth.load(std::memory_order_acquire);
    snapshot.buffers_dropped = buffers_dropped.load(std::memory_order_acquire);
    snapshot.current_queue_depth =
        current_queue_depth.load(std::memory_order_acquire);
    snapshot.errors_encountered =
        errors_encountered.load(std::memory_order_acquire);
    snapshot.journal_wrap_errors =
        journal_wrap_errors.load(std::memory_order_acquire);
    snapshot.invalid_param_errors =
        invalid_param_errors.load(std::memory_order_acquire);
    snapshot.other_errors = other_errors.load(std::memory_order_acquire);
    snapshot.consecutive_errors =
        consecutive_errors.load(std::memory_order_acquire);
    snapshot.max_consecutive_errors =
        max_consecutive_errors.load(std::memory_order_acquire);
    snapshot.total_read_time_ms =
        total_read_time_ms.load(std::memory_order_acquire);
    snapshot.total_process_time_ms =
        total_process_time_ms.load(std::memory_order_acquire);
    snapshot.last_update_time_ms =
        last_update_time_ms.load(std::memory_order_acquire);
    return snapshot;
  }
};

// Thread-safe queue for USN Journal buffers with size limit
class UsnJournalQueue {
public:
  explicit UsnJournalQueue(
      size_t max_size = usn_monitor_constants::kDefaultMaxQueueSize)
      : max_size_(max_size) {}

  // Push a buffer to the queue. Returns true if successful, false if queue is
  // full. When queue is full, the buffer is dropped and a warning is logged.
  bool Push(std::vector<char> buffer) {
    std::scoped_lock lock(mutex_);
    if (queue_.size() >= max_size_) {
      // Queue full - drop buffer to prevent unbounded growth
      dropped_count_++;
      return false;
    }
    queue_.push(std::move(buffer));
    size_.fetch_add(1, std::memory_order_relaxed); // Update atomic counter
    cv_.notify_one();
    return true;
  }

  bool Pop(std::vector<char> &buffer) {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty() || stop_; });

    if (queue_.empty() && stop_) {
      return false;
    }

    buffer = std::move(queue_.front());
    queue_.pop();
    size_.fetch_sub(1, std::memory_order_relaxed); // Update atomic counter
    return true;
  }

  void Stop() {
    std::scoped_lock lock(mutex_);
    stop_ = true;
    cv_.notify_all();
  }

  // Fast, lock-free size query using atomic counter
  // This avoids mutex contention for frequent size queries (e.g., logging every
  // 100 buffers) The counter is maintained in sync with the actual queue size
  // via Push()/Pop()
  [[nodiscard]] size_t Size() const { return size_.load(std::memory_order_acquire); }

  [[nodiscard]] size_t GetDroppedCount() const {
    std::scoped_lock lock(mutex_);
    return dropped_count_;
  }

  void ResetDroppedCount() {
    std::scoped_lock lock(mutex_);
    dropped_count_ = 0;
  }

private:
  std::queue<std::vector<char>> queue_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool stop_ = false;
  size_t max_size_;
  size_t dropped_count_ = 0;    // Count of dropped buffers due to queue full
  std::atomic<size_t> size_{0}; // Atomic counter for lock-free size queries
};

// UsnMonitor class - encapsulates all monitoring state and functionality
class UsnMonitor {
public:
  // Constructor - takes dependencies via constructor injection
  explicit UsnMonitor(FileIndex &file_index, // Reference to file index
                      const MonitoringConfig &config = MonitoringConfig{}
                      // Configuration
  );

  // Destructor - ensures proper cleanup
  ~UsnMonitor();

  // Non-copyable, movable
  UsnMonitor(const UsnMonitor &) = delete;
  UsnMonitor &operator=(const UsnMonitor &) = delete;
  UsnMonitor(UsnMonitor &&) noexcept = default;
  UsnMonitor &operator=(UsnMonitor &&) noexcept = default;

  // Start/Stop monitoring
  // Returns true if initialization succeeded, false if it failed (e.g., insufficient privileges)
  // If initialization fails, monitoring will not be active and the application should
  // notify the user and gracefully degrade to manual-refresh mode.
  bool Start();
  void Stop();

  // Status queries - thread-safe
  [[nodiscard]] bool IsActive() const {
    return monitoring_active_.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool IsPopulatingIndex() const {
    return is_populating_index_.load(std::memory_order_acquire);
  }
  [[nodiscard]] size_t GetIndexedFileCount() const {
    return indexed_file_count_.load(std::memory_order_acquire);
  }
  [[nodiscard]] size_t GetQueueSize() const;
  [[nodiscard]] size_t GetDroppedBufferCount() const;
  
  /**
   * @brief Check if privilege dropping failed during initialization
   * 
   * This is a security-critical check. If privileges cannot be dropped after
   * acquiring the volume handle, the application should exit immediately to
   * prevent privilege escalation vulnerabilities.
   * 
   * @return true if privilege dropping failed, false otherwise
   */
  bool DidPrivilegeDropFail() const {
    return privilege_drop_failed_.load(std::memory_order_acquire);
  }

  // Metrics access - thread-safe
  const UsnMonitorMetrics &GetMetrics() const { return metrics_; }
  [[nodiscard]] UsnMonitorMetrics::Snapshot GetMetricsSnapshot() const {
    return metrics_.GetSnapshot();
  }
  void ResetMetrics() { metrics_.Reset(); }

  // Configuration
  const MonitoringConfig &GetConfig() const { return config_; }
  void UpdateConfig(const MonitoringConfig &config);

private:
  /**
   * Internal thread functions
   *
   * These are private member functions that run in separate threads.
   * See implementation comments in UsnMonitor.cpp for detailed documentation
   * about responsibilities, error handling, lifecycle, and thread safety.
   */
  void ReaderThread();
  void ProcessorThread();

  // Process a single buffer from the queue (extracted to reduce ProcessorThread cognitive complexity)
  void ProcessOneBuffer(std::vector<char>& buffer);

  /**
   * Helper function to handle initialization failure cleanup
   * 
   * Common pattern used in ReaderThread error handling to:
   * - Set monitoring_active_ to false
   * - Signal initialization failure via init_promise_
   * - Stop the queue to allow processor thread to exit gracefully
   * 
   * This eliminates duplication across multiple error paths.
   */
  void HandleInitializationFailure();

  // ReaderThread helpers (extracted to reduce cognitive complexity cpp:S3776)
  bool OpenVolumeAndQueryJournal(HANDLE& out_handle,
                                 USN_JOURNAL_DATA_V0& out_journal_data);
  bool RunInitialPopulationAndPrivileges(HANDLE handle);
  void HandleReadJournalError(DWORD err, size_t& consecutive_errors,
                             bool& should_exit);
  void ProcessSuccessfulReadAndEnqueue(const std::vector<char>& buffer,
                                       int buffer_size,
                                       DWORD bytes_returned,
                                       READ_USN_JOURNAL_DATA_V0& read_data,
                                       bool& should_exit);

  // Member variables (encapsulated state)
  FileIndex &file_index_;   // NOLINT(readability-identifier-naming) - snake_case_ per project convention; reference to file index (dependency)
  MonitoringConfig config_; // Current configuration
  std::unique_ptr<UsnJournalQueue> queue_; // Queue (owned by class)
  std::thread reader_thread_;              // Reader thread
  std::thread processor_thread_;           // Processor thread
  mutable std::mutex mutex_;               // Protects start/stop operations
  HANDLE volume_handle_{INVALID_HANDLE_VALUE}; // Volume handle for I/O cancellation (protected by mutex_)

  // Initialization status communication
  std::promise<bool> init_promise_;        // Promise to signal initialization status
  std::future<bool> init_future_;          // Future to wait for initialization status

  // Internal state (no longer global)
  std::atomic<bool> monitoring_active_{false};   // Monitoring active flag
  std::atomic<size_t> indexed_file_count_{0};    // Indexed file count
  std::atomic<bool> is_populating_index_{false}; // Population in progress flag
  std::atomic<bool> privilege_drop_failed_{false}; // Security: privilege drop failure flag

  // Metrics collection
  mutable UsnMonitorMetrics metrics_; // Performance and health metrics
};

#else // !_WIN32 (macOS stub)

// macOS stub: Minimal UsnMonitor class for compilation compatibility
// USN Journal monitoring is Windows-only, so this is a no-op stub
class UsnMonitor {
public:
  // Constructor - takes FileIndex reference (required for interface compatibility)
  explicit UsnMonitor(FileIndex &file_index) : file_index_(file_index) {}
  
  // Destructor
  ~UsnMonitor() = default;
  
  // Non-copyable, non-movable (contains reference member)
  UsnMonitor(const UsnMonitor &) = delete;
  UsnMonitor &operator=(const UsnMonitor &) = delete;
  UsnMonitor(UsnMonitor &&) noexcept = delete;
  UsnMonitor &operator=(UsnMonitor &&) noexcept = delete;
  
  // Stub methods - always return false/0 on macOS (no monitoring)
  bool Start() { return false; }  // NOSONAR(cpp:S5817) - Signature must match non-const Windows implementation
  void Stop() {}                  // NOSONAR(cpp:S5817) - Signature must match non-const Windows implementation
  
  [[nodiscard]] bool IsActive() const { return false; }
  [[nodiscard]] bool IsPopulatingIndex() const { return false; }
  [[nodiscard]] size_t GetIndexedFileCount() const { return 0; }
  [[nodiscard]] size_t GetQueueSize() const { return 0; }
  [[nodiscard]] size_t GetDroppedBufferCount() const { return 0; }
  
private:
  FileIndex &file_index_;   // NOLINT(readability-identifier-naming) NOSONAR(cpp:S1068) - Stub keeps same layout as Windows impl; reference unused in stub
};

#endif  // _WIN32
