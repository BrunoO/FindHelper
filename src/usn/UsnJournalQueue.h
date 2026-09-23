#pragma once

// UsnJournalQueue — thread-safe bounded queue for USN Journal read buffers.
//
// Producer/consumer model:
//   - Push() blocks when the queue is at capacity (backpressure), rather than
//     dropping. This preserves every USN event and prevents index desync on
//     high-activity bursts (e.g. bulk deletions generating hundreds of records).
//   - Pop() blocks when the queue is empty, waking only when a buffer arrives
//     or Stop() is called.
//   - Stop() discards any pending buffers and wakes both sides immediately so
//     shutdown does not wait to apply a multi-GB backlog (app is exiting).
//
// All public methods are thread-safe. One std::mutex and one
// std::condition_variable are shared between producers and consumers.
//
// This class has no Windows-specific dependencies and can be tested
// cross-platform (macOS/Linux) without stubs.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <utility>
#include <vector>

namespace usn_queue_constants {

constexpr size_t kDefaultMaxQueueSize = static_cast<size_t>(1000) * 20;  // ~1.25 GB max (20000 × 64 KB)
constexpr size_t kQueuePushWaitMs = 50;              // Backpressure wait when queue is full

}  // namespace usn_queue_constants

class UsnJournalQueue {
public:
  explicit UsnJournalQueue(
      size_t max_size = usn_queue_constants::kDefaultMaxQueueSize)
      : max_size_(max_size) {}

  // Push a buffer to the queue. Blocks when the queue is at capacity until
  // space is freed by a Pop(), or Stop() is called.
  // Returns true if the buffer was enqueued, false only when Stop() has been
  // called (clean shutdown — the buffer is not enqueued).
  bool Push(std::vector<char> buffer) {
    // Critical section: queue and counters only; no I/O
    // (see docs/design/LOCK_ORDERING_AND_CRITICAL_SECTIONS.md).
    std::unique_lock lock(mutex_);
    while (!stop_ && queue_.size() >= max_size_) {
      // Backpressure instead of drop: wait for processor thread to free space.
      const bool woke_with_space = cv_.wait_for(
          lock, std::chrono::milliseconds(usn_queue_constants::kQueuePushWaitMs),
          [this] { return stop_ || queue_.size() < max_size_; });
      if (!woke_with_space && queue_.size() >= max_size_) {
        // Keep trying while monitor is active; this prevents event loss.
        continue;
      }
    }
    if (stop_) {
      return false;
    }
    queue_.push(std::move(buffer));
    size_.fetch_add(1);  // Update atomic counter
    cv_.notify_one();
    return true;
  }

  // Pop the oldest buffer. Blocks until a buffer is available or Stop() is
  // called. After Stop(), returns false immediately (pending buffers were
  // discarded — see Stop()).
  bool Pop(std::vector<char>& buffer) {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty() || stop_; });

    if (stop_) {
      return false;
    }

    buffer = std::move(queue_.front());
    queue_.pop();
    size_.fetch_sub(1);  // Update atomic counter
    // Wake a blocked producer waiting for free queue space.
    cv_.notify_one();
    return true;
  }

  // Signal shutdown. Discards pending buffers and unblocks all Push()/Pop().
  // Discard is intentional: Stop() is only used when monitoring ends (app exit
  // or fatal journal failure); applying a full backlog on the joining thread
  // freezes "Stopping..." for minutes.
  void Stop() {
    const std::scoped_lock lock(mutex_);
    stop_ = true;
    DiscardPendingLocked();
    cv_.notify_all();
  }

  // Fast, lock-free size query (approximate; may lag by one operation).
  [[nodiscard]] size_t Size() const { return size_.load(); }

  [[nodiscard]] bool IsStopped() const {
    const std::scoped_lock lock(mutex_);
    return stop_;
  }

private:
  void DiscardPendingLocked() {
    while (!queue_.empty()) {
      queue_.pop();
    }
    size_.store(0);
  }

  std::queue<std::vector<char>> queue_;
  // Guards queue_, stop_; no I/O under this lock.
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool stop_ = false;
  size_t max_size_;
  std::atomic<size_t> size_{0}; // Atomic counter for lock-free size queries
};
