#pragma once

/**
 * @file UsnActivityTracker.h
 * @brief Circular ring buffer to track recent filesystem change events from USN Journal
 */

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

enum class UsnChangeType {
  Created,
  Deleted,
  Renamed,
  Modified,
  Other
};

/**
 * @struct UsnChangeEntry
 * @brief Represents a single filesystem change event recorded by the monitor
 */
struct UsnChangeEntry {
  std::chrono::system_clock::time_point timestamp;
  UsnChangeType change_type;
  std::string path;
  std::string old_path;  // Populated for Renamed events
};

/**
 * @class UsnActivityTracker
 * @brief Thread-safe ring buffer storing recent USN change entries
 */
class UsnActivityTracker {
 public:
  explicit UsnActivityTracker(size_t capacity = 500) : capacity_(capacity) {}

  // Record a change event in the ring buffer
  void RecordChange(UsnChangeType type, std::string path, std::string old_path = "") {
    if (path.empty() && old_path.empty()) {
      return;
    }
    const std::scoped_lock lock(mutex_);
    if (buffer_.size() >= capacity_) {
      buffer_.pop_front();
    }
    buffer_.push_back(UsnChangeEntry{std::chrono::system_clock::now(), type, std::move(path), std::move(old_path)});
  }

  // Clear all recorded entries
  void Clear() {
    const std::scoped_lock lock(mutex_);
    buffer_.clear();
  }

  // Get a snapshot copy of all current entries (from oldest to newest)
  [[nodiscard]] std::vector<UsnChangeEntry> GetSnapshot() const {
    const std::scoped_lock lock(mutex_);
    return std::vector<UsnChangeEntry>(buffer_.begin(), buffer_.end());
  }

  // Current entry count
  [[nodiscard]] size_t Size() const {
    const std::scoped_lock lock(mutex_);
    return buffer_.size();
  }

  // Get capacity limit
  [[nodiscard]] size_t Capacity() const {
    return capacity_;
  }

 private:
  mutable std::mutex mutex_;
  std::deque<UsnChangeEntry> buffer_;
  size_t capacity_;
};
