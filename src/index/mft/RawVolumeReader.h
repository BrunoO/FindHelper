#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif  // _WIN32

namespace mft {

/**
 * @class IRawVolumeReader
 * @brief Low-level block reader interface for volume / disk offset reading.
 */
class IRawVolumeReader {
 public:
  virtual ~IRawVolumeReader() = default;

  /**
   * Reads raw bytes from a specified volume disk offset.
   * @param disk_offset Physical byte offset on the volume.
   * @param buffer Sector-aligned destination buffer.
   * @param bytes_to_read Requested number of bytes (multiple of sector size).
   * @param bytes_read Output parameter receiving the actual number of bytes read.
   * @return true on success, false on I/O error.
   */
  [[nodiscard]] virtual bool Read(uint64_t disk_offset, void* buffer, size_t bytes_to_read,
                                  size_t& bytes_read) = 0;
};

#ifdef _WIN32
/**
 * @class Win32VolumeReader
 * @brief Production implementation reading from a Win32 volume handle.
 */
class Win32VolumeReader : public IRawVolumeReader {
 public:
  explicit Win32VolumeReader(HANDLE volume) : volume_(volume) {}

  [[nodiscard]] bool Read(uint64_t disk_offset, void* buffer, size_t bytes_to_read,
                          size_t& bytes_read) override {
    if (volume_ == INVALID_HANDLE_VALUE || buffer == nullptr || bytes_to_read == 0) {
      bytes_read = 0;
      return false;
    }

    OVERLAPPED overlapped = {};
    overlapped.Offset = static_cast<DWORD>(disk_offset & 0xFFFFFFFFULL);
    overlapped.OffsetHigh = static_cast<DWORD>((disk_offset >> 32ULL) & 0xFFFFFFFFULL);

    DWORD read_bytes = 0;
    BOOL ok = ReadFile(volume_, buffer, static_cast<DWORD>(bytes_to_read), &read_bytes, &overlapped);
    if (!ok) {
      if (const DWORD err = GetLastError(); err == ERROR_IO_PENDING) {
        if (!GetOverlappedResult(volume_, &overlapped, &read_bytes, TRUE)) {
          bytes_read = 0;
          return false;
        }
      } else if (err == ERROR_HANDLE_EOF) {
        bytes_read = 0;
        return true;
      } else {
        bytes_read = 0;
        return false;
      }
    }

    bytes_read = static_cast<size_t>(read_bytes);
    return true;
  }

 private:
  HANDLE volume_ = INVALID_HANDLE_VALUE;
};
#endif  // _WIN32

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

  [[nodiscard]] bool Read(uint64_t disk_offset, void* buffer, size_t bytes_to_read,
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
