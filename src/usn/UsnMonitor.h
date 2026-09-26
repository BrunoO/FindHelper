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
 * 6. Batch index updates: FileIndex mutations for one USN buffer share a single
 *    unique_lock (InsertLocked/RemoveLocked/RenameLocked/MoveLocked) so delete/
 *    rename storms do not thrash Windows SRWLOCK against search shared locks.
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
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "usn/JournalCursor.h"
#include "usn/UsnActivityTracker.h"
#include "usn/UsnJournalQueue.h"

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <winioctl.h>
#include "index/FileIndex.h"
#include "index/IndexDomainEvents.h"
#include "index/InitialIndexPopulator.h"
#include "index/SystemPathFilter.h"
#include "usn/VolumeGateway.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"
#include "utils/ThreadUtils.h"
#else
// macOS stub: minimal forward declarations and stub class
#include "index/FileIndex.h"
#include "utils/Logger.h"
#include <cstddef>
#endif  // _WIN32

namespace volume_state {
class VolumeState;
}  // namespace volume_state

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
        } catch (...) {  // NOSONAR(cpp:S2738, cpp:S2486) - Move assignment must not throw; log and swallow to prevent std::terminate
          LOG_ERROR("VolumeHandle: Unknown exception during CloseHandle in move assignment");
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
constexpr size_t kQueueWarningThreshold = 10; // Warn if queue exceeds this
constexpr size_t kLogIntervalBuffers = 1000;  // Log stats every N buffers
constexpr size_t kDropLogInterval = 10;       // Log every Nth drop to avoid spam
// Queue-depth bands for backlog diagnostics (FPS / search contention repros).
constexpr size_t kQueueDepthBand100 = 100;
constexpr size_t kQueueDepthBand1000 = 1000;
constexpr size_t kQueueDepthBand9000 = 9000;
// Status bar / correlation logging: show queue when at or above this depth.
constexpr size_t kQueueStatusDisplayThreshold = 100;

// Never-arriving sweep (parents that never arrive): refresh awaiting metrics
// and evict placeholders older than the max age every N buffers. The age
// threshold is in minutes, not seconds — a directory handle held open (shell
// cwd, scanner) can legitimately delay a parent CLOSE record for a long
// time, and a wrongful eviction only resurrects on the next USN event for
// that file or full population.
constexpr size_t kNeverArrivingSweepIntervalBuffers = 250;
constexpr uint64_t kNeverArrivingMaxAgeMs = 300000;  // 5 minutes
// Wall-clock backstop for the sweep cadence above: buffer-count ticks stall
// on quiet systems (empty reads are never queued), freezing both metrics
// and eviction. Sweep when this much wall time passed since the last run.
constexpr uint64_t kNeverArrivingSweepMaxIntervalMs = 60000;  // 1 minute
// Journal-order eviction gap: entries tracked more than this many USNs behind
// the current journal position are evicted even when wall-clock young (OR
// policy with the age threshold above). USNs are byte offsets; 1MB is roughly
// 5 minutes of journal activity on a moderately busy system — symmetric with
// the wall clock — while close-time parents normally arrive within a buffer
// or two. Entries with unknown position (usn_at_track == 0: bulk, synthetic,
// move paths) are exempt and fall back to wall-clock only.
constexpr int64_t kNeverArrivingMaxUsnGap = 1024 * 1024;  // 1MB of journal activity

// Retry delays (milliseconds)
constexpr DWORD kRetryDelayMs = 250;          // General retry delay
constexpr DWORD kJournalWrapDelayMs = 50;    // Journal wrap-around delay
constexpr DWORD kInvalidParamDelayMs = 1000; // Invalid parameter retry delay

// USN reason sets live in usn_reason::kInterestingReasons / kActionReasons
// (src/usn/UsnReason.h): same bits, typed. The kernel filter must include
// Close when ReturnOnlyOnClose is nonzero (MSDN READ_USN_JOURNAL_DATA);
// close-only records carry no action and are skipped by the apply filter
// (usn_record::CarriesAction — the raw form of UsnRecord::IsActionable).

// File filtering
// System-file prefix now lives in the shared filter (index/SystemPathFilter.h).
constexpr const char *kDefaultVolumePath =
    "\\\\.\\C:"; // Default volume to monitor  // NOSONAR(cpp:S3628) - Raw string literal would be less readable for Windows path

} // namespace usn_monitor_constants

// Configuration structure for monitoring parameters
struct MonitoringConfig {
  std::string volume_path = usn_monitor_constants::kDefaultVolumePath;
  int buffer_size = usn_monitor_constants::kBufferSizeBytes;
  DWORD timeout_ms = usn_monitor_constants::kTimeoutMs;
  size_t bytes_to_wait_for = usn_monitor_constants::kBytesToWaitFor;
  size_t max_queue_size = usn_queue_constants::kDefaultMaxQueueSize;
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

  // Error statistics (grouped into a nested struct to stay within the sonar-cpp
  // cpp:S1820 20-field limit; see UsnMonitorMetrics below).
  struct UsnMonitorErrorStats {
    std::atomic<size_t> errors_encountered{0};   // Total errors encountered
    std::atomic<size_t> journal_wrap_errors{0};  // Journal wrap-around events
    std::atomic<size_t> invalid_param_errors{0}; // Invalid parameter errors
    std::atomic<size_t> other_errors{0};         // Other types of errors
    std::atomic<size_t> consecutive_errors{0};   // Current consecutive error count
    std::atomic<size_t> max_consecutive_errors{0}; // Maximum consecutive errors

    // Clear all error counters, including the max-consecutive high-water mark.
    void Reset() noexcept {
      errors_encountered.store(0);
      journal_wrap_errors.store(0);
      invalid_param_errors.store(0);
      other_errors.store(0);
      consecutive_errors.store(0);
      max_consecutive_errors.store(0);
    }
  };

  // Queue statistics
  std::atomic<size_t> max_queue_depth{0}; // Maximum queue depth reached
  std::atomic<size_t> buffers_dropped{0}; // Buffers dropped due to queue full
  std::atomic<size_t> current_queue_depth{
      0}; // Current queue depth (updated periodically)

  // Awaiting-placeholder statistics (index-drift monitoring, updated by the
  // processor thread every kNeverArrivingSweepIntervalBuffers buffers).
  std::atomic<size_t> awaiting_count{
      0}; // Children waiting on a missing parent (0 normally, >0 in bursts)
  std::atomic<uint64_t> awaiting_oldest_age_ms{
      0}; // Age of the longest-waiting placeholder; old values identify
           // never-arriving parents (filtered, dropped, deleted)
  std::atomic<size_t> never_arriving_evicted{
      0}; // Placeholders evicted by the never-arriving sweep (never-arriving parents)
  std::atomic<size_t> healed_awaiting_total{
      0}; // Placeholders re-resolved on parent arrival (heal path); with
           // awaiting_count + never_arriving_evicted completes the
           // awaiting flow equation (created ~= healed + evicted + outstanding)
  std::atomic<uint64_t> max_buffer_process_time_ms{
      0}; // Worst single-buffer USN apply time; spikes identify
           // placeholder-churn bottlenecks that the average alone hides

  UsnMonitorErrorStats errors{};

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
    awaiting_count.store(0);
    awaiting_oldest_age_ms.store(0);
    never_arriving_evicted.store(0);
    healed_awaiting_total.store(0);
    max_buffer_process_time_ms.store(0);
    errors.Reset();
    total_read_time_ms.store(0);
    total_process_time_ms.store(0);
    last_update_time_ms.store(0);
  }

  // Get a snapshot of all metrics (thread-safe)
  // Returns a struct with regular (non-atomic) values for easy reading.
  // Error counters are grouped into a nested struct to stay within the
  // sonar-cpp cpp:S1820 20-field limit (mirrors UsnMonitorErrorStats above).
  struct Snapshot {
    struct ErrorSnapshot {
      size_t errors_encountered;
      size_t journal_wrap_errors;
      size_t invalid_param_errors;
      size_t other_errors;
      size_t consecutive_errors;
      size_t max_consecutive_errors;
    };
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
    size_t awaiting_count;
    uint64_t awaiting_oldest_age_ms;
    size_t never_arriving_evicted;
    size_t healed_awaiting_total;
    uint64_t max_buffer_process_time_ms;
    ErrorSnapshot errors;
    uint64_t total_read_time_ms;
    uint64_t total_process_time_ms;
    uint64_t last_update_time_ms;
  };

  [[nodiscard]] Snapshot GetSnapshot() const {
    Snapshot snapshot;
    snapshot.buffers_read = buffers_read.load();
    snapshot.buffers_processed =
        buffers_processed.load();
    snapshot.records_processed =
        records_processed.load();
    snapshot.files_created = files_created.load();
    snapshot.files_deleted = files_deleted.load();
    snapshot.files_renamed = files_renamed.load();
    snapshot.files_modified = files_modified.load();
    snapshot.max_queue_depth = max_queue_depth.load();
    snapshot.buffers_dropped = buffers_dropped.load();
    snapshot.current_queue_depth =
        current_queue_depth.load();
    snapshot.awaiting_count =
        awaiting_count.load();
    snapshot.awaiting_oldest_age_ms =
        awaiting_oldest_age_ms.load();
    snapshot.never_arriving_evicted =
        never_arriving_evicted.load();
    snapshot.healed_awaiting_total =
        healed_awaiting_total.load();
    snapshot.max_buffer_process_time_ms =
        max_buffer_process_time_ms.load();
    snapshot.errors.errors_encountered =
        errors.errors_encountered.load();
    snapshot.errors.journal_wrap_errors =
        errors.journal_wrap_errors.load();
    snapshot.errors.invalid_param_errors =
        errors.invalid_param_errors.load();
    snapshot.errors.other_errors = errors.other_errors.load();
    snapshot.errors.consecutive_errors =
        errors.consecutive_errors.load();
    snapshot.errors.max_consecutive_errors =
        errors.max_consecutive_errors.load();
    snapshot.total_read_time_ms =
        total_read_time_ms.load();
    snapshot.total_process_time_ms =
        total_process_time_ms.load();
    snapshot.last_update_time_ms =
        last_update_time_ms.load();
    return snapshot;
  }
};

// UsnMonitor class - encapsulates all monitoring state and functionality
class UsnMonitor {  // NOSONAR(cpp:S1448) - Coordinator: reader/processor threads, queue, volume handle, metrics, filter tracker and test hook form one thread-handoff unit; splitting would scatter single-owner invariants across classes
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
  void Stop() noexcept;

  // Status queries - thread-safe
  [[nodiscard]] bool IsActive() const {
    return state_.monitoring_active_.load();
  }
  // True once index integrity is compromised (journal wrap or queue drop). Set-once;
  // survives ResetMetrics(). The only recovery is a full application restart.
  [[nodiscard]] bool IsIndexIntegrityCompromised() const {
    return index_integrity_compromised_.load();
  }
  [[nodiscard]] bool IsPopulatingIndex() const {
    return state_.is_populating_index_.load();
  }
  /**
   * @brief True if initial population or post-population step (e.g. privilege drop) failed.
   * Used by WindowsIndexBuilder to call MarkFailed() instead of MarkCompleted() when
   * population ends due to failure rather than success.
   */
  [[nodiscard]] bool InitialPopulationFailed() const {
    return state_.initial_population_failed_.load();
  }
  [[nodiscard]] size_t GetIndexedFileCount() const {
    return indexed_file_count_.load();
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
    return state_.privilege_drop_failed_.load();
  }

  // Metrics access - thread-safe
  const UsnMonitorMetrics& GetMetrics() const { return metrics_; }  // NOSONAR(cpp:S8379) - metrics_ uses atomics; thread-safe without mutex_
  [[nodiscard]] UsnMonitorMetrics::Snapshot GetMetricsSnapshot() const {
    return metrics_.GetSnapshot();  // NOSONAR(cpp:S8379) - metrics_ uses atomics; thread-safe without mutex_
  }
  void ResetMetrics() const { metrics_.Reset(); }  // NOSONAR(cpp:S8379) - metrics_ uses atomics; thread-safe without mutex_

  // Activity tracking - thread-safe
  [[nodiscard]] std::vector<UsnChangeEntry> GetRecentChangesSnapshot() const {
    return activity_tracker_.GetSnapshot();
  }
  void ClearRecentChanges() {
    activity_tracker_.Clear();
  }

  // Integrity-event subscription (DDD #7): forensics sink receiving one
  // IntegrityEvent per latch with the cause (wrap/ID/loss/drop/corrupt/
  // errors/divergence). Null by default (publication disabled); tests and
  // future metrics subscribe here. Sinks must not throw and must not
  // re-enter the index (see IndexDomainEvents.h contract).
  void SetIntegrityEventSink(index_domain_events::IntegrityEventSink sink) {
    on_integrity_event_ = std::move(sink);
  }

  // Volume-gateway seam (DDD #8): kernel ioctl translation behind an
  // injectable interface. Live DeviceIoControl impl by default; tests
  // substitute a fake. Never null after construction (setter guards).
  // Not thread-safe: call before Start(); swapping mid-run races the
  // reader thread's unsynchronized use.
  void SetVolumeGateway(std::unique_ptr<volume_gateway::VolumeGateway> gateway) {
    if (gateway != nullptr) {
      volume_gateway_ = std::move(gateway);
    }
  }

  // Test hook for the Windows-only synthetic-buffer regression test
  // (usn_two_phase_apply_tests): applies one raw journal buffer exactly as
  // the processor thread would, two-phase apply included. Not part of the
  // production API; the buffer must start with the leading next-USN value
  // like DeviceIoControl output. Never starts monitoring threads.
  void ProcessBufferForTest(std::vector<char>& buffer);

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

  // Apply one validated USN record to the index (caller MUST hold unique_lock
  // on FileIndex via the VolumeState aggregate). Close-only records (no action
  // bits) are skipped. Single shared body for both phases of the per-buffer
  // two-phase apply (see ProcessOneBuffer); the bool from
  // ProcessInterestingUsnRecord only drove offset advancement, so it is
  // intentionally not propagated. The VolumeState aggregate is the single
  // per-buffer boundary — caller passes the outer aggregate, no per-record
  // re-creation.
  void ApplyOneUsnRecord(PUSN_RECORD_V2 record,
                         volume_state::VolumeState& aggregate);

  // Refresh awaiting-placeholder metrics and evict never-arriving entries.
  // Runs on the processor thread every kNeverArrivingSweepIntervalBuffers buffers or
  // kNeverArrivingSweepMaxIntervalMs of wall time (whichever first); acquires the
  // index locks itself (must NOT hold them on entry). current_usn is the
  // buffer's leading journal position: entries tracked max_usn_gap behind it
  // are evicted even when wall-clock young (OR policy; steady state only —
  // skipped while populating, where replay advances USNs in bulk).
  void SweepNeverArriving(int64_t current_usn);

  /**
   * Helper function to handle initialization failure cleanup
   *
   * Common pattern used in ReaderThread error handling to:
   * - Set monitoring_active_ to false
   * - Signal initialization failure via init_promise_ (no-op if already set —
   *   journal-open success is signaled before population, so a later failure
   *   must not throw std::future_error and skip queue_->Stop())
   * - Stop the queue to allow processor thread to exit gracefully
   *
   * This eliminates duplication across multiple error paths.
   */
  void HandleInitializationFailure();

  /**
   * @brief Set init_promise_ at most once per Start() (journal-open or failure).
   * Safe when population fails after an earlier success signal.
   */
  void SignalInitResult(bool success);

  /**
   * Central integrity latch (R7): store-first set-once latch that always emits
   * an ERROR log (LOG_WARNING is compiled out in Release). Throttled paths use
   * MarkIntegrityCompromised() per event plus LatchIntegrityCompromised() on the
   * log interval. See usn/UsnIntegrityLatch.h.
   */
  void LatchIntegrityCompromised(std::string_view reason);
  void MarkIntegrityCompromised();
  // Guarded integrity publication: null sink disables (same convention as
  // PublishFileEvent in UsnMonitor.cpp). Called alongside every latch so the
  // event stream and the set-once flag never diverge.
  void PublishIntegrityEvent(index_domain_events::IntegrityEvent event);

  // ReaderThread helpers (extracted to reduce cognitive complexity cpp:S3776)
  bool OpenVolumeAndQueryJournal(HANDLE& out_handle,
                                 USN_JOURNAL_DATA_V0& out_journal_data);
  bool RunInitialPopulationAndPrivileges(HANDLE handle,
                                         const usn_journal::JournalCursor& pre_pop_cursor,
                                         USN& out_next_usn);
  void DrainReplayJournalEvents(HANDLE handle,
                                const usn_journal::JournalCursor& start_cursor,
                                USN target_usn,
                                USN& out_next_usn);
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
  // Kernel ioctl seam (DDD #8); live impl by default, fake under test.
  std::unique_ptr<volume_gateway::VolumeGateway> volume_gateway_;
  std::thread reader_thread_;              // Reader thread
  std::thread processor_thread_;           // Processor thread
  // Protects start/stop state, volume_handle_, queue_; I/O (e.g. CloseHandle) done outside lock (see LOCK_ORDERING doc).
  mutable std::mutex mutex_;               // NOLINT(readability-identifier-naming) - project convention
  HANDLE volume_handle_{INVALID_HANDLE_VALUE}; // Volume handle for I/O cancellation (protected by mutex_)

  // Initialization status communication
  std::promise<bool> init_promise_;        // Promise to signal initialization status
  std::future<bool> init_future_;          // Future to wait for initialization status

  // Volatile start/population state flags grouped into a nested struct to stay
  // within the sonar-cpp cpp:S1820 20-field limit; see UsnMonitorStateFlags.
  // All flags are UI/logic-thread read via public getters; written by the
  // start/stop/reader/processor paths with plain atomics (no mutex needed).
  struct UsnMonitorStateFlags {
    // Guards against double set_value after journal-open success then population failure.
    std::atomic<bool> init_promise_satisfied_{false};
    std::atomic<bool> monitoring_active_{false};         // Monitoring active flag
    std::atomic<bool> is_populating_index_{false};       // Population in progress flag
    std::atomic<bool> privilege_drop_failed_{false};     // Security: privilege drop failure flag
    std::atomic<bool> initial_population_failed_{false}; // True if population or post-population (e.g. privilege drop) failed; lets WindowsIndexBuilder show failed state
  };
  // Set-once latch: index integrity is compromised (journal wrap or queue drop).
  // NOT cleared by ResetMetrics() — restart is the only recovery.
  std::atomic<bool> index_integrity_compromised_{false};
  std::atomic<size_t> indexed_file_count_{0};    // Indexed file count

  UsnMonitorStateFlags state_{};

  // Metrics collection
  mutable UsnMonitorMetrics metrics_; // Performance and health metrics

  // Internal reader-thread counter used to throttle periodic queue-depth logs.
  // Stored as a member (not a static local) so it resets correctly on each
  // Start() call and avoids the SonarQube S3010 static-inside-loop violation.
  size_t reader_push_count_{0};  // NOLINT(readability-identifier-naming) - snake_case_ per project convention
  // Highest queue-depth diagnostic band already logged (0 / 100 / 1000 / 9000).
  // Resets when the queue drains below kQueueWarningThreshold.
  size_t last_queue_depth_band_logged_{0};  // NOLINT(readability-identifier-naming) - snake_case_ per project convention
  // Last stale-pending sweep time (processor thread only). The sweep also runs
  // on a wall-clock cadence so quiet systems — where buffer-count ticks take
  // hours — don't hold stale placeholders (or frozen metrics) indefinitely.
  std::chrono::steady_clock::time_point last_never_arriving_sweep_{};  // NOLINT(readability-identifier-naming) - snake_case_ per project convention

  // Filtered $-prefixed directories (e.g. $Recycle.Bin, $Extend) and their subtrees
  // (e.g. per-user SID subfolders). Seeded from initial index population and
  // maintained during real-time USN monitoring. Shared implementation:
  // system_path_filter::FilteredDirTracker (index/SystemPathFilter.h).
  system_path_filter::FilteredDirTracker filtered_dir_ref_nums_;

  // Ring buffer tracking recent filesystem activity events
  UsnActivityTracker activity_tracker_;

  // Integrity-event sink (DDD #7); see SetIntegrityEventSink. Null disables.
  index_domain_events::IntegrityEventSink on_integrity_event_;
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
  void Stop() noexcept {}  // NOSONAR(cpp:S5817) - Signature must match non-const Windows implementation

  [[nodiscard]] bool IsActive() const { return false; }
  [[nodiscard]] bool IsIndexIntegrityCompromised() const { return false; }
  [[nodiscard]] bool IsPopulatingIndex() const { return false; }
  [[nodiscard]] size_t GetIndexedFileCount() const { return 0; }
  [[nodiscard]] size_t GetQueueSize() const { return 0; }
  [[nodiscard]] size_t GetDroppedBufferCount() const { return 0; }

  [[nodiscard]] std::vector<UsnChangeEntry> GetRecentChangesSnapshot() const { return {}; }
  // Intentionally empty: the macOS build has no USN Journal to clear — change history
  // lives only in the Windows implementation (separate class, not a shared signature).
  // Mutates nothing, hence const.
  void ClearRecentChanges() const {
    // Intentionally empty: macOS stub keeps no change history to clear.
  }

private:
  FileIndex &file_index_;   // NOLINT(readability-identifier-naming) NOSONAR(cpp:S1068) - Stub keeps same layout as Windows impl; reference unused in stub
};

#endif  // _WIN32
