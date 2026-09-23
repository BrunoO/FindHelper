#pragma once

#ifdef _WIN32

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <vector>
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include
#include <winioctl.h>

namespace usn_test_helpers {

// Synthetic root for test fixtures (MFT record 5 is NTFS volume root).
constexpr uint64_t kSynthVolumeRoot = 5;

// Append one 8-byte-aligned USN_RECORD_V2 carrying a UTF-16 name into buffer.
inline void AppendSyntheticRecord(std::vector<char>& buffer, uint64_t file_ref,
                                  uint64_t parent_ref, DWORD reason, DWORD attributes,
                                  const wchar_t* name) {
  const size_t name_len_bytes = std::wcslen(name) * sizeof(wchar_t);
  REQUIRE(name_len_bytes <= 0xFF00U);
  const size_t name_offset = offsetof(USN_RECORD_V2, FileName);
  const size_t record_len =
      (name_offset + name_len_bytes + 7U) & ~static_cast<size_t>(7U);
  const size_t start = buffer.size();
  buffer.resize(start + record_len, 0);
  auto* record = reinterpret_cast<USN_RECORD_V2*>(buffer.data() + start);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) - synthetic test buffer; 8-byte aligned by construction
  record->RecordLength = static_cast<DWORD>(record_len);
  record->MajorVersion = 2;
  record->MinorVersion = 0;
  record->FileReferenceNumber = file_ref;
  record->ParentFileReferenceNumber = parent_ref;
  record->Reason = reason;
  record->FileAttributes = attributes;
  record->FileNameLength = static_cast<WORD>(name_len_bytes);
  record->FileNameOffset = static_cast<WORD>(name_offset);
  std::memcpy(buffer.data() + start + name_offset, name, name_len_bytes);
}

}  // namespace usn_test_helpers

#endif  // _WIN32
