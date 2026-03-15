#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "search/SearchTypes.h"

namespace streaming_results_collector_constants {
constexpr size_t kDefaultBatchSize = 500;
constexpr uint32_t kDefaultNotificationIntervalMs = 50;
/** Max results to process per UI frame during streaming; keeps UI responsive when many batches arrive.
 *  Set high enough that drain after search completion finishes in a few frames even for large result
 *  sets (100k / 5000 = 20 frames ≈ 330ms). Per-frame work (MergeAndConvertToSearchResults) is
 *  O(batch_size) path-string copies — well within a 16ms frame budget at this size. */
constexpr size_t kMaxResultsPerFrame = 5000;
}  // namespace streaming_results_collector_constants

/**
 * StreamingResultsCollector - Buffers and batches search results for the UI
 *
 * This class collects SearchResultData from producer threads (via SearchWorker)
 * and provides them to the consumer (UI thread) in batches.
 *
 * Thread-safe collection: results are pushed into current_batch_; when batch_size or
 * notification_interval_ms is reached, current_batch_ is flushed into pending_batches_
 * for UI consumption via GetAllPendingBatches / GetPendingBatchesUpTo. The controller
 * accumulates consumed batches for finalization (no duplicate full copy in the collector).
 */
class StreamingResultsCollector {
public:
  /**
   * Constructs the collector with optional batch and notification interval.
   * @param batch_size Number of results per batch before flushing (default 500).
   * @param notification_interval_ms Max ms between flushes (default 50).
   */
  explicit StreamingResultsCollector(
    size_t batch_size = streaming_results_collector_constants::kDefaultBatchSize,
    uint32_t notification_interval_ms = streaming_results_collector_constants::kDefaultNotificationIntervalMs);

  /** Producer: add a single result (called from SearchWorker thread). */
  void AddResult(SearchResultData&& result);
  /** Producer: add a single result by copy (avoids redundant std::move for trivially-copyable type). */
  void AddResult(const SearchResultData& result);
  /** Producer: bulk-add all results from one search future — acquires mutex once instead of once per result. */
  void AddResults(const std::vector<SearchResultData>& batch);
  /** Producer: mark search as complete (called from SearchWorker thread). */
  void MarkSearchComplete();
  /** Producer: set error message if search failed (called from SearchWorker thread). */
  void SetError(std::string_view error_message);

  /** Consumer: true if a new batch is available (called from UI thread). */
  [[nodiscard]] bool HasNewBatch() const;

  /**
   * Returns all pending result data since the last call and clears the pending buffer.
   * UI should call MergeAndConvertToSearchResults(pool, data, file_index) then append to partialResults.
   */
  std::vector<SearchResultData> GetAllPendingBatches();

  /**
   * Returns up to max_results pending items, leaving the rest for the next frame.
   * Use this to limit UI-thread work per frame and keep the UI responsive during streaming.
   * If pending size <= max_results, behaves like GetAllPendingBatches (returns all, clears).
   * If pending size > max_results, returns first max_results; remainder stay in pending, has_new_batch_ stays true.
   */
  std::vector<SearchResultData> GetPendingBatchesUpTo(size_t max_results);

  /** True when the producer has called MarkSearchComplete(). */
  [[nodiscard]] bool IsSearchComplete() const;
  /** True if SetError() was called. */
  [[nodiscard]] bool HasError() const;
  /** Returns the error message set by SetError(), or empty view. */
  [[nodiscard]] std::string_view GetError() const;

private:
  void FlushCurrentBatch();

  // Set in constructor only (avoid const data members per cppcoreguidelines)
  size_t batch_size_;  // NOLINT(readability-identifier-naming) - project uses snake_case_ for members
  std::chrono::milliseconds notification_interval_;  // NOLINT(readability-identifier-naming)

  mutable std::mutex mutex_;  // NOLINT(readability-identifier-naming) - project uses snake_case_ for members
  std::vector<SearchResultData> current_batch_;  // NOLINT(readability-identifier-naming)
  std::vector<SearchResultData> pending_batches_;  // NOLINT(readability-identifier-naming)

  std::chrono::steady_clock::time_point last_flush_time_;  // NOLINT(readability-identifier-naming)

  std::atomic<bool> has_new_batch_{false};  // NOLINT(readability-identifier-naming)
  std::atomic<bool> search_complete_{false};  // NOLINT(readability-identifier-naming)
  std::string error_message_;  // NOLINT(readability-identifier-naming)
  std::atomic<bool> has_error_{false};  // NOLINT(readability-identifier-naming)
};
