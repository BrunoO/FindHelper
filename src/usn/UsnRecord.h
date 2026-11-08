#pragma once

#include <cstdint>
#include <string_view>

#include "index/NtfsFileReference.h"
#include "usn/UsnReason.h"

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <winioctl.h>

#include "utils/CTrackWrapper.h"
#include "utils/Logger.h"
#endif  // _WIN32

namespace usn_record {

// One parsed USN_RECORD_V2: identity, name, sequence number, reasons, and
// kind. Built once per record at the ProcessInterestingUsnRecord boundary
// from the validated raw record; downstream stages take this view instead
// of the raw pointer. Glossary term: UsnRecord
// (docs/design/USN_MFT_UBIQUITOUS_LANGUAGE.md).
//
// Lifetime: name is a view into the source USN buffer (or the converted
// thread-local text), valid only while that source is alive — same contract
// as the previous context struct. Journal timestamps are always zero, so no
// time field is carried (sizes/times lazy-load on demand).
//
// Aggregate initialization order is (self, parent, name, usn, reasons,
// is_directory, file_attributes).
struct UsnRecord {
  ntfs_file_reference::NtfsFileReference self;
  ntfs_file_reference::NtfsFileReference parent;
  std::string_view name;
  int64_t usn = 0;
  usn_reason::ReasonSet reasons{0U};
  bool is_directory = false;
  // Raw NTFS attribute bitmask (FILE_ATTRIBUTE_*) from the journal record.
  // Carried (unlike timestamps, which journals never provide) so live inserts
  // and attribute refreshes need no extra I/O.
  uint32_t file_attributes = 0;
};

#ifdef _WIN32

// Expected version for USN_RECORD_V2
constexpr WORD kExpectedMajorVersion = 2;
constexpr WORD kExpectedMinorVersion = 0;

// Convert sizeof() result to DWORD to match Windows API types and avoid C4018 warnings.
[[nodiscard]] constexpr DWORD SizeOfUsnRecordV2() noexcept {
  return static_cast<DWORD>(sizeof(USN_RECORD_V2));  // NOSONAR(cpp:S1905) - Cast needed to match Windows API types and avoid C4018 warnings
}

[[nodiscard]] constexpr DWORD SizeOfUsn() noexcept {
  return static_cast<DWORD>(sizeof(USN));  // NOSONAR(cpp:S1905) - Cast needed to match Windows API types and avoid C4018 warnings
}

// Safe addition of WORD values that returns DWORD.
[[nodiscard]] constexpr DWORD AddWords(WORD a, WORD b) noexcept {
  return static_cast<DWORD>(a) + static_cast<DWORD>(b);
}

// Validates buffer bounds, RecordLength, MajorVersion/MinorVersion (2.0), and
// filename offset/length before exposing a USN_RECORD_V2 pointer.
[[nodiscard]] inline bool ValidateAndParseUsnRecord(const char* buffer, DWORD buffer_size,
                                                    const DWORD& offset,
                                                    PUSN_RECORD_V2& record_out) {
  CTRACK_DEV_NAME("UsnRecord::ValidateAndParseUsnRecord");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  record_out = nullptr;

  if (offset + SizeOfUsnRecordV2() > buffer_size) {
    LOG_WARNING_BUILD("Incomplete USN record at offset " << offset
                      << ", buffer size " << buffer_size);
    return false;
  }

  auto record =
      reinterpret_cast<PUSN_RECORD_V2>(const_cast<char*>(buffer) + offset);  // NOLINT(cppcoreguidelines-pro-type-const-cast) NOSONAR(cpp:S859) - required after bounds validation; buffer is API-const

  if (record->RecordLength == 0) {
    LOG_WARNING_BUILD("Invalid USN record at offset " << offset
                      << ": RecordLength is zero");
    return false;
  }

  if (const DWORD min_record_length = SizeOfUsnRecordV2();
      record->RecordLength < min_record_length) {
    LOG_WARNING_BUILD("Invalid USN record at offset " << offset
                      << ": RecordLength (" << record->RecordLength
                      << ") is less than minimum (" << min_record_length << ")");
    return false;
  }

  if (offset + record->RecordLength > buffer_size) {
    LOG_WARNING_BUILD("Invalid USN record at offset " << offset
                      << ": RecordLength (" << record->RecordLength
                      << ") exceeds buffer bounds (buffer size: " << buffer_size << ")");
    return false;
  }

  if (record->MajorVersion != kExpectedMajorVersion) {
    LOG_WARNING_BUILD("Invalid USN record at offset " << offset
                      << ": MajorVersion (" << record->MajorVersion
                      << ") does not match expected version (" << kExpectedMajorVersion << ")");
    return false;
  }

  if (record->MinorVersion != kExpectedMinorVersion) {
    LOG_WARNING_BUILD("Invalid USN record at offset " << offset
                      << ": MinorVersion (" << record->MinorVersion
                      << ") does not match expected version (" << kExpectedMinorVersion << ")");
    return false;
  }

  if (AddWords(record->FileNameOffset, record->FileNameLength) > record->RecordLength) {
    LOG_WARNING_BUILD("Invalid USN record at offset " << offset
                      << ": filename extends beyond record (FileNameOffset: "
                      << record->FileNameOffset << ", FileNameLength: " << record->FileNameLength
                      << ", RecordLength: " << record->RecordLength << ")");
    return false;
  }

  record_out = record;
  return true;
}

#endif  // _WIN32

}  // namespace usn_record
