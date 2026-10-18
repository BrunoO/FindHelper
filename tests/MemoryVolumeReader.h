#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "index/mft/RawVolumeReader.h"

namespace mft {

/**
 * @class MemoryVolumeReader
 * @brief In-memory mock block reader for cross-platform unit and benchmark tests.
 */
class MemoryVolumeReader : public IRawVolumeReader {
 public:
  MemoryVolumeReader() = default;

  explicit MemoryVolumeReader(std::vector<char> data) : data_(std::move(data)) {}

  MemoryVolumeReader(const char* data, size_t size)
      : data_(data, data + size) {}

  void SetData(std::vector<char> data) {
    const std::scoped_lock lock(mutex_);
    data_ = std::move(data);
  }

  void SetSimulatedDelayMs(uint32_t delay_ms) {
    const std::scoped_lock lock(mutex_);
    simulated_delay_ms_ = delay_ms;
  }

  void SetFailOnReadIndex(int fail_index) {
    const std::scoped_lock lock(mutex_);
    fail_on_read_index_ = fail_index;
  }

  [[nodiscard]] bool Read(uint64_t disk_offset, char* buffer, size_t bytes_to_read,
                          size_t& bytes_read) override {
    uint32_t delay = 0;
    {
      const std::scoped_lock lock(mutex_);
      delay = simulated_delay_ms_;
    }
    if (delay > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }

    const std::scoped_lock lock(mutex_);
    if (fail_on_read_index_ >= 0 && read_count_ == static_cast<size_t>(fail_on_read_index_)) {
      ++read_count_;
      bytes_read = 0;
      return false;
    }
    ++read_count_;

    if (disk_offset >= data_.size()) {
      bytes_read = 0;
      return true;
    }

    const size_t available = data_.size() - static_cast<size_t>(disk_offset);
    const size_t count = (bytes_to_read < available) ? bytes_to_read : available;
    if (count > 0 && buffer != nullptr) {
      std::memcpy(buffer, data_.data() + disk_offset, count);
    }
    bytes_read = count;
    return true;
  }

  [[nodiscard]] size_t GetReadCount() const {
    const std::scoped_lock lock(mutex_);
    return read_count_;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<char> data_;
  uint32_t simulated_delay_ms_ = 0;
  int fail_on_read_index_ = -1;
  size_t read_count_ = 0;
};

}  // namespace mft
