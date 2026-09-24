#pragma once

// VolumeGateway seam (DDD #8): the kernel ioctl boundary behind an
// injectable interface. Raw FSCTL_* DeviceIoControl calls at multiple
// sites were unmockable and untestable without a volume; the gateway
// isolates the #ifdef _WIN32 surface so cursor/apply logic can be unit
// tested against a fake. Glossary term: VolumeGateway
// (docs/design/USN_MFT_UBIQUITOUS_LANGUAGE.md).
//
// Windows-only by design (HANDLE + winioctl structs); including this
// header off Windows is a no-op.

#ifdef _WIN32

#include <cassert>
#include <cstdint>
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <winioctl.h>

namespace volume_gateway {

// Thin DeviceIoControl translation: one method per FSCTL_* call the
// volume code performs. No logging, no retry policy, no error mapping —
// callers keep their existing handling. On false, GetLastError() holds
// the kernel error (the live impl performs no API calls after the
// DeviceIoControl, so the error is preserved for the caller).
class VolumeGateway {
 public:
  virtual ~VolumeGateway() = default;

  [[nodiscard]] virtual bool QueryJournal(HANDLE volume,
                                         USN_JOURNAL_DATA_V0& out_data) = 0;
  [[nodiscard]] virtual bool ReadJournal(HANDLE volume,
                                        READ_USN_JOURNAL_DATA_V0& params,
                                        char* buffer,
                                        int buffer_size,
                                        DWORD& out_bytes) = 0;
  [[nodiscard]] virtual bool EnumUsnJournal(HANDLE volume,
                                     MFT_ENUM_DATA_V0& params,
                                     char* buffer,
                                     int buffer_size,
                                     DWORD& out_bytes) = 0;
  [[nodiscard]] virtual bool GetFileRecord(HANDLE volume,
                                          uint64_t file_ref_num,
                                          char* buffer,
                                          DWORD buffer_size,
                                          DWORD& out_bytes) = 0;
  [[nodiscard]] virtual bool GetVolumeData(HANDLE volume,
                                          NTFS_VOLUME_DATA_BUFFER& out_data) = 0;
};

// Live implementation: the exact DeviceIoControl calls the seam replaces.
// Header-inline so no translation-unit or CMake change is needed.
class DeviceIoControlVolumeGateway final : public VolumeGateway {
 public:
  [[nodiscard]] bool QueryJournal(HANDLE volume,
                                 USN_JOURNAL_DATA_V0& out_data) override {
    DWORD bytes_returned = 0;
    return DeviceIoControl(volume, FSCTL_QUERY_USN_JOURNAL, nullptr, 0,
                           &out_data, sizeof(out_data), &bytes_returned,
                           nullptr) != 0;
  }

  [[nodiscard]] bool ReadJournal(HANDLE volume,
                                READ_USN_JOURNAL_DATA_V0& params,
                                char* buffer,
                                int buffer_size,
                                DWORD& out_bytes) override {
    assert(buffer_size > 0 && "ReadJournal needs a positive buffer size");
    return DeviceIoControl(volume, FSCTL_READ_USN_JOURNAL, &params,
                           sizeof(params), buffer,
                           static_cast<DWORD>(buffer_size), &out_bytes,
                           nullptr) != 0;
  }

  [[nodiscard]] bool EnumUsnJournal(HANDLE volume,
                            MFT_ENUM_DATA_V0& params,
                            char* buffer,
                            int buffer_size,
                            DWORD& out_bytes) override {
    assert(buffer_size > 0 && "EnumUsnJournal needs a positive buffer size");
    return DeviceIoControl(volume, FSCTL_ENUM_USN_DATA, &params,
                           sizeof(params), buffer,
                           static_cast<DWORD>(buffer_size), &out_bytes,
                           nullptr) != 0;
  }

  [[nodiscard]] bool GetFileRecord(HANDLE volume,
                                  uint64_t file_ref_num,
                                  char* buffer,
                                  DWORD buffer_size,
                                  DWORD& out_bytes) override {
    // NTFS_FILE_RECORD_INPUT_BUFFER is not always defined in winioctl.h,
    // so it is defined here (same shape as the MftMetadataReader site).
    // NOLINTNEXTLINE(readability-identifier-naming,cppcoreguidelines-pro-type-member-init,hicpp-member-init) - Windows API; {} zero-inits FileReferenceNumber
    struct NtfsFileRecordInput {
      ULONGLONG FileReferenceNumber;  // NOLINT(readability-identifier-naming) - Windows API member name
    };
    NtfsFileRecordInput input = {};
    input.FileReferenceNumber = file_ref_num;
    return DeviceIoControl(volume, FSCTL_GET_NTFS_FILE_RECORD, &input,
                           sizeof(input), buffer, buffer_size, &out_bytes,
                           nullptr) != 0;
  }

  [[nodiscard]] bool GetVolumeData(HANDLE volume,
                                  NTFS_VOLUME_DATA_BUFFER& out_data) override {
    DWORD bytes_returned = 0;
    return DeviceIoControl(volume, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0,
                           &out_data, sizeof(out_data), &bytes_returned,
                           nullptr) != 0;
  }
};

}  // namespace volume_gateway

#endif  // _WIN32
