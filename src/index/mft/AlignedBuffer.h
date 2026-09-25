#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif  // _WIN32

namespace mft {

/**
 * @class AlignedBuffer
 * @brief Sector- and page-aligned RAII buffer.
 *
 * Uses VirtualAlloc on Windows to satisfy FILE_FLAG_NO_BUFFERING alignment requirements,
 * and posix_memalign on POSIX platforms.
 */
class AlignedBuffer {
 public:
  static constexpr size_t kDefaultAlignment = 4096;

  AlignedBuffer() = default;

  explicit AlignedBuffer(size_t size, size_t alignment = kDefaultAlignment) {
    Allocate(size, alignment);
  }

  ~AlignedBuffer() {
    Reset();
  }

  AlignedBuffer(const AlignedBuffer&) = delete;
  AlignedBuffer& operator=(const AlignedBuffer&) = delete;

  AlignedBuffer(AlignedBuffer&& other) noexcept
      : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
  }

  AlignedBuffer& operator=(AlignedBuffer&& other) noexcept {
    if (this != &other) {
      Reset();
      data_ = other.data_;
      size_ = other.size_;
      other.data_ = nullptr;
      other.size_ = 0;
    }
    return *this;
  }

  void Allocate(size_t size, size_t alignment = kDefaultAlignment) {
    Reset();
    if (size == 0) {
      return;
    }
#ifdef _WIN32
    (void)alignment;
    data_ = static_cast<char*>(VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
#else
    void* ptr = nullptr;
    const size_t align = (std::max)(alignment, sizeof(void*));
    if (posix_memalign(&ptr, align, size) == 0) {
      data_ = static_cast<char*>(ptr);
    }
#endif  // _WIN32
    if (data_ != nullptr) {
      size_ = size;
    }
  }

  void Reset() noexcept {
    if (data_ != nullptr) {
#ifdef _WIN32
      VirtualFree(data_, 0, MEM_RELEASE);
#else
      free(data_);  // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory) - Low-level aligned buffer allocation
#endif  // _WIN32
      data_ = nullptr;
      size_ = 0;
    }
  }

  [[nodiscard]] char* Data() noexcept { return data_; }
  [[nodiscard]] const char* Data() const noexcept { return data_; }
  [[nodiscard]] size_t Size() const noexcept { return size_; }
  [[nodiscard]] bool IsAllocated() const noexcept { return data_ != nullptr; }

 private:
  char* data_ = nullptr;
  size_t size_ = 0;
};

}  // namespace mft
