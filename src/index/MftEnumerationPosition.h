#pragma once

// Windows-only: MFT baseline enumeration (FSCTL_ENUM_USN_DATA) runs on
// Windows only; do not include this header from cross-platform translation
// units (see the Windows-only sources in CMakeLists.txt). Including off
// Windows is a no-op (same pattern as usn/VolumeGateway.h).

#ifdef _WIN32

#include <cstdint>
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <winioctl.h>

namespace mft_enum_position {

// One FSCTL_ENUM_USN_DATA cursor: where enumeration stands
// (StartFileReferenceNumber) plus the fixed USN range (LowUsn/HighUsn).
// Wraps MFT_ENUM_DATA_V0 so the advancing cursor is not a loose variable
// and StartFileReferenceNumber cannot be confused with a record identity
// (contrast NtfsFileReference). Glossary term: MftEnumerationPosition
// (docs/design/USN_MFT_UBIQUITOUS_LANGUAGE.md).
class MftEnumerationPosition {
 public:
  // Cursor at the start: first record, full USN range.
  [[nodiscard]] static MftEnumerationPosition Start() {
    MftEnumerationPosition position;
    // MAXLONGLONG is defined in winioctl.h (included above).
    // If not available, fall back to the maximum 64-bit USN value.
#ifndef MAXLONGLONG
    // Fallback: maximum 64-bit signed integer value (max USN)
    static constexpr int64_t kMaxUsnFallback = 0x7FFFFFFFFFFFFFFFLL;
    position.data_.HighUsn = kMaxUsnFallback;
#else
    position.data_.HighUsn = MAXLONGLONG;
#endif  // MAXLONGLONG
    return position;
  }

  // Advance the cursor to the next USN (buffer's leading value).
  void Advance(USN next_usn) { data_.StartFileReferenceNumber = next_usn; }

  // Raw struct, forwarded to the gateway EnumMft call only. Do not mutate
  // the fields directly: cursor movement goes through Advance() so the
  // cursor-vs-identity distinction stays in one place. (A mutable reference
  // is required because DeviceIoControl takes LPVOID.)
  MFT_ENUM_DATA_V0& Data() noexcept { return data_; }

 private:
  MFT_ENUM_DATA_V0 data_{};  // Value-init: cursor at 0, LowUsn 0; HighUsn set by Start()
};

}  // namespace mft_enum_position

#endif  // _WIN32
