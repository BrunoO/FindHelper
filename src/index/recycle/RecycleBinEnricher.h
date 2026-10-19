#pragma once

/**
 * @file RecycleBinEnricher.h
 * @brief Background worker that reads $I records off-thread and publishes them to the metadata store.
 *
 * Spec section 5 Phase 4 item 1, with sections 3.4 (skip-and-continue), 3.5.5 (empty-bin race),
 * 3.5.7 (bounded queue), M7 (stale-enrichment guard) and M8 (cold-path SID markers).
 *
 * Data flow: producers (USN monitor, cold index) enqueue owned work items. The single worker thread
 * drains up to kEnrichDrainBatch items per wake, reads and parses each $I file, and hands the whole
 * drain to RecycleBinMetadataStore::PublishIEntries once (one publish per drain).
 *
 *   EnqueueIFile(...)      --+                       +-- ReadRecycleIndexFile + ParseRecycleBinIndex
 *   EnqueueSidMarker(...)  --+-> bounded queue -> worker (drain <= 64) --+
 *                                                                       +-- PublishIEntries (once)
 *
 * Cold path (M8): an SID marker is expanded by the worker, not the producer. The worker enumerates
 * $I* files under that SID directory off the producer thread and treats each one as an EnrichTask.
 *
 * Queue policy (spec 3.5.7): bounded at max_queue entries. A full queue drops the NEWEST item,
 * increments DropCount(), and logs every kDropLogInterval drops (no per-drop spam). An enqueue of a
 * path that is already queued is coalesced into the queued entry and does not consume a slot.
 *
 * Staleness:
 *  - Queued tasks: CancelByPath() removes them before they run.
 *  - The store manages snapshot versioning and entry replacement atomically under its mutex.
 *
 * Lifetime and threading invariant (documented here on purpose):
 *  - The store pointer is non-owning. The store must outlive the enricher. In production it is an
 *    application-lifetime object.
 *  - The destructor calls Stop(), which joins the worker before returning. The worker is therefore
 *    never touching the store after the enricher is destroyed, provided the store is destroyed
 *    after the enricher.
 *  - Construction, Start(), Stop() and destruction belong to one owner thread. EnqueueIFile,
 *    EnqueueSidMarker, CancelByPath, DropCount and QueueDepth are safe from any thread.
 *  - Scheduling: one std::thread worker. Thread priority, such as background I/O on Windows, is out
 *    of scope here and belongs to app wiring (task 4.2b).
 *
 * Platform: the Windows path reads through CreateFileW with FRN taken from the read handle (N2). The
 * POSIX path reads through std::ifstream and has no FRN (synthetic FRNs are used). POSIX trash needs
 * no enrichment, so the POSIX path exists only for Tier-1 tests.
 */

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

namespace recycle_bin {

class RecycleBinMetadataStore;

/// Queue capacity used when the caller does not pass one (spec 3.5.7).
inline constexpr std::size_t kDefaultEnrichQueueCapacity = 16384;

/// Maximum number of queued items the worker takes per wake (one publish per drain).
inline constexpr std::size_t kEnrichDrainBatch = 64;

/// One $I file to read. Owned strings only: producers never pass views into their own buffers.
struct EnrichTask {
  /// $I FRN. 0 means "resolve from the read handle on Windows, or synthesize on POSIX".
  /// On Windows the handle FRN wins whenever it is available (N2).
  std::uint64_t i_frn = 0;
  /// FRN of the <SID> directory that holds the $I file.
  std::uint64_t parent_frn = 0;
  /// Bare $I file name, e.g. "$IABC123.txt". Non-$I names are skipped by the worker.
  std::string name;
  /// Full path to the $I file.
  std::string absolute_path;
};

/// Raw bytes of one $I file plus the FRN observed on the read handle (0 when unavailable).
struct RecycleFileBytes {
  std::vector<std::uint8_t> bytes;
  std::uint64_t file_frn = 0;
};

/**
 * Reads one $I file in full. Never throws on I/O failure.
 *
 * On Windows, the file is opened with GENERIC_READ and FILE_SHARE_READ | FILE_SHARE_WRITE |
 * FILE_SHARE_DELETE, so the Recycle Bin is never locked against the OS. file_frn is taken from
 * GetFileInformationByHandle on that same handle, composed as (high << 32) | low. That is the NTFS
 * 64-bit file reference, sequence number included, so it matches NtfsFileReference.
 *
 * On failure, returns false and sets error_kind to one of "access_denied", "not_found",
 * "open_failed", "stat_failed", "read_failed", "too_large". The caller logs it.
 *
 * @param absolute_path Full path to the $I file. Must be UTF-8.
 * @param out Receives the bytes and FRN on success. Cleared on every call.
 * @param error_kind Receives a short failure class on failure.
 */
[[nodiscard]] bool ReadRecycleIndexFile(std::string_view absolute_path, RecycleFileBytes& out, std::string& error_kind);

/// Decoded fields of one $I metadata record (v1 fixed 544-byte or v2 variable-path layout).
struct RecycleBinMetadata {
  std::uint64_t version = 0;
  std::uint64_t original_size = 0;
  /// RAW Win32 FILETIME (100-ns ticks since 1601-01-01 UTC). Not converted.
  std::uint64_t deletion_time_filetime = 0;
  /// Original absolute path as UTF-8, without a trailing NUL.
  std::string original_path_utf8;
};

enum class ParseError : std::uint8_t {
  kOk,                // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
  kTruncated,         ///< Null/empty input, or fewer bytes than the layout requires. // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
  kBadVersion,        ///< Version field is neither 1 nor 2. // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
  kPathTooLong,       ///< v2 pathLength is 0 or exceeds the bytes actually present. // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
  kUnterminatedPath,  ///< v1: no NUL in the 260-unit field. v2: last unit is not NUL. // NOLINT(readability-identifier-naming) - k prefix is project convention for enum constants
};

struct ParseResult {
  bool ok = false;
  ParseError error = ParseError::kTruncated;
  RecycleBinMetadata metadata;
};

/**
 * Parses one $I metadata record from a raw byte buffer.
 * Never reads outside [data, data + size). Lone UTF-16 surrogates decode to U+FFFD.
 */
[[nodiscard]] ParseResult ParseRecycleBinIndex(const std::uint8_t* data, std::size_t size);

class RecycleBinEnricher {
 public:
  /// @param store Non-owning. Must not be null, and must outlive this object (see file header).
  /// @param max_queue Queue capacity. Values below 1 are raised to 1.
  explicit RecycleBinEnricher(RecycleBinMetadataStore* store, std::size_t max_queue = kDefaultEnrichQueueCapacity);
  /// Calls Stop(). Joins the worker before returning.
  ~RecycleBinEnricher();
  RecycleBinEnricher(const RecycleBinEnricher&) = delete;
  RecycleBinEnricher& operator=(const RecycleBinEnricher&) = delete;
  RecycleBinEnricher(RecycleBinEnricher&&) = delete;
  RecycleBinEnricher& operator=(RecycleBinEnricher&&) = delete;

  /// Starts the worker. Idempotent while running. Re-opens the queue after a Stop().
  void Start();

  /**
   * Signals the worker, lets the in-flight batch finish, and joins it. Queued items that never ran
   * are dropped and added to DropCount(). Enqueues after Stop() are refused and counted as drops
   * until Start() re-opens the queue. Idempotent.
   */
  void Stop();

  /**
   * Queues one $I file for enrichment.
   * @return true if the item is queued or coalesced into an identical queued path. false if it was
   *         dropped (queue full, or enricher stopped).
   */
  [[nodiscard]] bool EnqueueIFile(std::uint64_t i_frn, std::uint64_t parent_frn, std::string name,
                                  std::string absolute_path);

  /**
   * Queues one cold-path <SID> marker. The worker expands it into $I tasks.
   * @return true if queued, false if dropped (queue full, or enricher stopped).
   */
  [[nodiscard]] bool EnqueueSidMarker(std::uint64_t sid_frn, std::string sid_path);

  /**
   * Removes a queued EnrichTask with this exact path. Does not touch a task that is already in
   * flight (the re-read rule covers that case). Not counted as a drop.
   * @return true if a queued task was removed.
   */
  [[nodiscard]] bool CancelByPath(std::string_view absolute_path);

  /// Total items dropped (full queue, enqueue after Stop, or queue remainder at Stop). Lock-free read.
  [[nodiscard]] std::uint64_t DropCount() const noexcept;

  /// Items currently queued, not counting the in-flight batch. Lock-free read for UI polling.
  [[nodiscard]] std::size_t QueueDepth() const noexcept;

 private:
  void WorkerLoop();
  /// Blocks until work or stop. Moves up to kEnrichDrainBatch items into batch. false means stop.
  [[nodiscard]] bool DequeueBatch(std::vector<EnrichTask>& batch);

  RecycleBinMetadataStore* const store_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for member variables
  const std::size_t max_queue_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for member variables

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<EnrichTask> queue_;
  // Paths of queued EnrichTasks, for O(1) coalescing. Kept in sync with queue_.
  std::unordered_set<std::string> queued_paths_;
  bool stop_ = false;
  std::thread worker_;

  std::atomic<std::uint64_t> dropped_{0};
  std::atomic<std::size_t> queue_depth_{0};
};

}  // namespace recycle_bin
