#pragma once

#include <cstdint>

namespace ntfs_file_reference {

// Lower 48 bits of an NTFS 64-bit file reference (upper 16 bits = sequence number).
constexpr uint64_t kFileRecordNumberMask = 0x0000FFFFFFFFFFFFULL;

// MFT record 5 is the NTFS volume root directory ("."). USN enumeration often skips it
// (empty name or never emitted), but every indexed file ultimately parents to this FRN.
constexpr uint64_t kRootDirectoryRecordNumber = 5ULL;

[[nodiscard]] constexpr uint64_t RecordNumber(uint64_t file_reference) noexcept {
  return file_reference & kFileRecordNumberMask;
}

// True when two FRNs refer to the same MFT record (sequence number may differ).
// Used for USN rename vs move: ParentFileReferenceNumber often carries a stale
// sequence while the indexed parentID holds the canonical FRN.
[[nodiscard]] constexpr bool SameRecordNumber(uint64_t a, uint64_t b) noexcept {
  return RecordNumber(a) == RecordNumber(b);
}

[[nodiscard]] constexpr bool IsRootDirectoryRecord(uint64_t file_reference) noexcept {
  return RecordNumber(file_reference) == kRootDirectoryRecordNumber;
}

}  // namespace ntfs_file_reference
