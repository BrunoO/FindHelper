#pragma once

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only TU; SDK system header
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
   * @param buffer Sector-aligned destination buffer (byte buffer, never null on success).
   * @param bytes_to_read Requested number of bytes (multiple of sector size).
   * @param bytes_read Output parameter receiving the actual number of bytes read.
   * @return true on success, false on I/O error.
   */
  [[nodiscard]] virtual bool Read(uint64_t disk_offset, char* buffer, size_t bytes_to_read,
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

  [[nodiscard]] bool Read(uint64_t disk_offset, char* buffer, size_t bytes_to_read,
                           size_t& bytes_read) override {
    if (volume_ == INVALID_HANDLE_VALUE || buffer == nullptr || bytes_to_read == 0) {
      bytes_read = 0;
      return false;
    }

    OVERLAPPED overlapped = {};
    overlapped.Offset = static_cast<DWORD>(disk_offset & 0xFFFFFFFFULL);
    overlapped.OffsetHigh = static_cast<DWORD>((disk_offset >> 32ULL) & 0xFFFFFFFFULL);

    DWORD read_bytes = 0;
    if (const BOOL ok = ReadFile(volume_, buffer, static_cast<DWORD>(bytes_to_read), &read_bytes,
                                 &overlapped);
        !ok) {
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

}  // namespace mft
