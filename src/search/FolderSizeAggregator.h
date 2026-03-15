#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include "utils/HashMapAliases.h"

class FileIndex;

/**
 * Asynchronously computes the total size of files within a directory by summing
 * all indexed descendants. Uses a single background worker thread.
 *
 * Directories are queued via Request() and results polled via GetResult().
 * Call CancelPending() when a new search starts (preserves the results cache so
 * folders that reappear in the next search do not need to be recomputed).
 * Call Reset() only when the index itself changes and cached sizes may be stale.
 *
 * --- Per-directory state machine ---
 *
 *  Each directory in SearchResult can be in one of four states:
 *
 *   UNQUEUED  fileSize == kFileSizeNotLoaded
 *             fileSizeDisplay == "" (or "..." after EnsureDisplayStringsPopulated)
 *             Not in pending_requests_ or results_.
 *
 *   PENDING   fileSize == kFileSizeNotLoaded, fileSizeDisplay == "..."
 *             In pending_requests_; job is in queue_ or being processed by worker.
 *             Reached via Request().
 *
 *   COMPUTED  fileSize == kFileSizeNotLoaded, fileSizeDisplay == "..."
 *             In results_ (worker wrote the value), not in pending_requests_.
 *             Transient — lasts at most one frame until the flush loop calls GetResult().
 *
 *   WRITTEN   fileSize == real value, fileSizeDisplay == formatted string
 *             In results_. Reached when the flush loop (or per-row render) calls
 *             GetResult() and writes the value into SearchResult.
 *
 *  Allowed transitions:
 *
 *    UNQUEUED  --[Request()]-->          PENDING
 *    PENDING   --[worker done]-->        COMPUTED
 *    COMPUTED  --[GetResult() + write]--> WRITTEN
 *
 *  The COMPUTED → WRITTEN transition happens inside the per-frame flush loop in
 *  ResultsTable::Render(). The sort is triggered only when every directory in
 *  searchResults is in WRITTEN state (pending_after == 0), ensuring the sort
 *  sees real values for all rows rather than firing once per arriving size.
 *
 *  Assertions document these invariants in debug builds:
 *    - WorkerThread: no duplicate result per folder_id (PENDING → COMPUTED once)
 *    - EnsureDisplayStringsPopulated: "..." only when fileSize == kFileSizeNotLoaded
 *    - RenderResultsTableRow: after writing, fileSizeDisplay != "..."
 *    - Flush loop: sort pre-condition — no kFileSizeNotLoaded dirs remain
 */
class FolderSizeAggregator {
public:
  explicit FolderSizeAggregator(FileIndex& index);
  ~FolderSizeAggregator();  // Signals shutdown and joins the worker thread.

  // Non-copyable, non-movable.
  FolderSizeAggregator(const FolderSizeAggregator&) = delete;
  FolderSizeAggregator& operator=(const FolderSizeAggregator&) = delete;
  FolderSizeAggregator(FolderSizeAggregator&&) = delete;
  FolderSizeAggregator& operator=(FolderSizeAggregator&&) = delete;

  // Queue folder for background size computation. No-op if already queued or done.
  void Request(uint64_t folder_id, std::string_view folder_path);

  // Non-blocking poll. Returns nullopt if not yet computed.
  [[nodiscard]] std::optional<uint64_t> GetResult(uint64_t folder_id) const;

  // Cancel pending jobs without clearing the results cache. Call when a new search
  // starts. Folders that appear again in the new results reuse their cached size
  // immediately instead of being recomputed.
  void CancelPending();

  // Cancel pending jobs AND clear the results cache. Call when the index changes
  // (file additions / deletions) so stale aggregate sizes are not shown.
  void Reset();

  // Returns true if there are jobs queued or currently being computed by the worker.
  // Use this to defer a size-column re-sort until all pending work is done, avoiding
  // per-arrival sort churn when many folders are computed asynchronously.
  [[nodiscard]] bool HasPendingWork() const;

private:
  struct Job {
    uint64_t folder_id;
    std::string folder_path;
    uint64_t generation;  // Matches generation_ at enqueue time; used to detect Reset().
  };

  void WorkerThread();
  // Compute sizes for all jobs in a single index scan (O(N_index × depth) instead of
  // O(K × N_index) for K separate scans). Returns folder_id → total_size.
  hash_map_t<uint64_t, uint64_t> ComputeSizeBatch(const std::vector<Job>& jobs) const;

  FileIndex& index_;                                     // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members

  mutable std::mutex mutex_;                             // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  std::condition_variable cv_;                           // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  std::atomic<bool> cancelled_{false};                   // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  std::thread worker_thread_;                            // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members

  // Protected by mutex_:
  std::deque<Job> queue_;                                // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  std::unordered_set<uint64_t> pending_requests_;        // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  hash_map_t<uint64_t, uint64_t> results_;               // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  uint64_t generation_ = 0;                              // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
};
