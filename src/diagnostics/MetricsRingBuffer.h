#pragma once

/**
 * @file diagnostics/MetricsRingBuffer.h
 * @brief Fixed-capacity, zero-allocation circular buffer for MetricsSample pre-roll history.
 */

#include <array>
#include <cassert>
#include <cstddef>

#include "diagnostics/MetricsSample.h"

namespace diagnostics {

/**
 * @class MetricsRingBuffer
 * @brief Fixed-size circular buffer storing the most recent Capacity samples.
 *
 * Invariants:
 * - Zero heap allocations: backed by inline std::array<MetricsSample, Capacity>.
 * - count_ <= Capacity and head_ < Capacity at all times.
 * - When count_ < Capacity, oldest element is at index 0 and newest is at count_ - 1.
 * - When count_ == Capacity, oldest element is at head_ and newest is at (head_ + Capacity - 1) % Capacity.
 */
template <size_t Capacity = flight_recorder_constants::kPreRollSampleCapacity>
class MetricsRingBuffer {
  static_assert(Capacity > 0U, "MetricsRingBuffer Capacity must be positive");

 public:
  constexpr MetricsRingBuffer() noexcept = default;

  /**
   * @brief Push a new sample, overwriting the oldest sample if full.
   */
  void Push(const MetricsSample& sample) noexcept {
    assert(head_ < Capacity);
    assert(count_ <= Capacity);
    buffer_[head_] = sample;  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index) - head_ < Capacity invariant enforced
    head_ = (head_ + 1U) % Capacity;
    if (count_ < Capacity) {
      ++count_;
    }
  }

  /**
   * @brief Reset the buffer to empty without deallocating storage.
   */
  void Clear() noexcept {
    head_ = 0U;
    count_ = 0U;
  }

  [[nodiscard]] constexpr size_t Size() const noexcept { return count_; }
  [[nodiscard]] constexpr bool Empty() const noexcept { return count_ == 0U; }
  [[nodiscard]] constexpr size_t MaxCapacity() const noexcept { return Capacity; }

  /**
   * @brief Access the oldest sample in the ring buffer.
   * @pre !Empty()
   */
  [[nodiscard]] const MetricsSample& Oldest() const noexcept {
    assert(count_ > 0U);
    const size_t oldest_idx = (count_ < Capacity) ? 0U : head_;
    return buffer_[oldest_idx];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index) - oldest_idx < Capacity
  }

  /**
   * @brief Access the most recently pushed sample in the ring buffer.
   * @pre !Empty()
   */
  [[nodiscard]] const MetricsSample& Newest() const noexcept {
    assert(count_ > 0U);
    const size_t newest_idx = (head_ + Capacity - 1U) % Capacity;
    return buffer_[newest_idx];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index) - newest_idx < Capacity
  }

  /**
   * @brief Iterate all stored samples in chronological order (oldest to newest).
   */
  template <typename Fn>
  void ForEachChronological(const Fn& fn) const {
    assert(count_ <= Capacity);
    const size_t start_idx = (count_ < Capacity) ? 0U : head_;
    for (size_t i = 0U; i < count_; ++i) {
      const size_t idx = (start_idx + i) % Capacity;
      fn(buffer_[idx]);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index) - idx < Capacity
    }
  }

 private:
  std::array<MetricsSample, Capacity> buffer_{};
  size_t head_ = 0U;
  size_t count_ = 0U;
};

}  // namespace diagnostics
