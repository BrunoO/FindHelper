#include "utils/FileAttributeConstants.h"
// Windows-only unit test for MftMetadataReader: verifies that file size is
// accurately sourced from the $DATA (0x80) attribute (both resident and
// non-resident) rather than the unmaintained $FILE_NAME (0x30) attribute.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#ifdef _WIN32

#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only test
#include <winioctl.h>

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "index/mft/MftMetadataReader.h"
#include "usn/VolumeGateway.h"
#include "utils/FileTimeTypes.h"

namespace {

#pragma pack(push, 1)

// MFT attribute type codes
constexpr ULONG kAttributeStandardInformation = 0x10;
constexpr ULONG kAttributeFileName = 0x30;
constexpr ULONG kAttributeData = 0x80;
constexpr ULONG kAttributeEnd = 0xFFFFFFFF;

struct RawFileRecordHeader {
  ULONG Signature = 0x454C4946;  // 'FILE'
  USHORT UsaOffset = 0;
  USHORT UsaCount = 0;
  ULONGLONG Lsn = 0;
  USHORT SequenceNumber = 1;
  USHORT LinkCount = 1;
  USHORT FirstAttributeOffset = 56;
  USHORT Flags = 0x0001;  // In-use
  ULONG FirstFreeByte = 1024;
  ULONG BytesAvailable = 1024;
  ULONGLONG BaseFileRecordNumber : 48;
  ULONGLONG BaseFileRecordSequence : 16;
  USHORT NextAttributeNumber = 4;
  USHORT SegmentNumberHighPart = 0;
  ULONG SegmentNumberLowPart = 0;
  UCHAR Padding[8] = {0};  // Pad to 56 bytes
};

struct RawAttributeHeader {
  ULONG TypeCode;
  ULONG RecordLength;
  UCHAR FormCode;
  UCHAR NameLength = 0;
  USHORT NameOffset = 0;
  USHORT Flags = 0;
  USHORT Instance = 0;
};

struct RawResidentForm {
  ULONG ValueLength;
  USHORT ValueOffset;
  UCHAR Reserved[2] = {0};
};

struct RawNonresidentForm {
  LONGLONG LowestVcn = 0;
  LONGLONG HighestVcn = 0;
  USHORT DataRunOffset = 64;
  USHORT CompressionSize = 0;
  UCHAR Padding[4] = {0};
  ULONGLONG AllocatedLength = 0;
  ULONGLONG FileSize = 0;
  ULONGLONG ValidDataLength = 0;
  ULONGLONG Compressed = 0;
};

struct RawStandardInfo {
  FILETIME CreationTime{0, 0};
  FILETIME LastModificationTime{123456, 789012};
  FILETIME MftChangeTime{0, 0};
  FILETIME LastAccessTime{0, 0};
  ULONG FileAttributes = 0x20;
};

struct RawFileName {
  ULONGLONG ParentDirectory : 48;
  ULONGLONG ParentSequence : 16;
  LONGLONG CreationTime = 0;
  LONGLONG LastModificationTime = 0;
  LONGLONG MftChangeTime = 0;
  LONGLONG LastAccessTime = 0;
  LONGLONG AllocatedLength = 0;
  LONGLONG FileSize = 0;  // RealSize is 0 in MFT record for files created empty
  ULONG FileAttributes = 0x20;
  USHORT PackedEaSize = 0;
  USHORT Reserved = 0;
  UCHAR FileNameLength = 4;
  UCHAR Flags = 1;  // Win32 namespace
  wchar_t FileName[4] = {L't', L'e', L's', L't'};
};

#pragma pack(pop)

class FakeMftGateway : public volume_gateway::VolumeGateway {
 public:
  void SetRecord(uint64_t file_ref, std::vector<char> buffer) {
    records_[file_ref] = std::move(buffer);
  }

  [[nodiscard]] bool QueryJournal(HANDLE, USN_JOURNAL_DATA_V0&) override { return false; }
  [[nodiscard]] bool ReadJournal(HANDLE, READ_USN_JOURNAL_DATA_V0&, char*, int, DWORD&) override { return false; }
  [[nodiscard]] bool EnumUsnJournal(HANDLE, MFT_ENUM_DATA_V0&, char*, int, DWORD&) override { return false; }
  [[nodiscard]] bool GetVolumeData(HANDLE, NTFS_VOLUME_DATA_BUFFER&) override { return false; }

  [[nodiscard]] bool GetFileRecord(HANDLE, uint64_t file_ref_num, char* buffer,
                                   DWORD buffer_size, DWORD& out_bytes) override {
    const auto it = records_.find(file_ref_num);
    if (it == records_.end()) {
      ::SetLastError(ERROR_FILE_NOT_FOUND);
      return false;
    }
    if (buffer_size < it->second.size()) {
      ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return false;
    }
    std::memcpy(buffer, it->second.data(), it->second.size());
    out_bytes = static_cast<DWORD>(it->second.size());
    return true;
  }

 private:
  std::unordered_map<uint64_t, std::vector<char>> records_;
};

enum class DataAttrKind {
  kNonresident,
  kResidentNonEmpty,
  kResidentEmpty,
  kNone
};

std::vector<char> BuildSyntheticRecord(uint64_t file_ref, DataAttrKind data_kind,
                                       uint64_t non_resident_size = 54321) {
  // Output buffer layout:
  // [0..7]   FileReferenceNumber (ULONGLONG)
  // [8..11]  FileRecordLength (ULONG) = 1024
  // [12..]   FileRecord (1024 bytes)
  constexpr size_t kRecordSize = 1024;
  constexpr size_t kWrapperHeaderSize = 12;
  std::vector<char> buffer(kWrapperHeaderSize + kRecordSize, 0);

  *reinterpret_cast<ULONGLONG*>(buffer.data()) = file_ref;
  *reinterpret_cast<ULONG*>(buffer.data() + 8) = static_cast<ULONG>(kRecordSize);

  char* mft = buffer.data() + kWrapperHeaderSize;

  // 1. FileRecord header
  RawFileRecordHeader fr_header; fr_header.BaseFileRecordNumber = 0; fr_header.BaseFileRecordSequence = 0;
  std::memcpy(mft, &fr_header, sizeof(fr_header));

  size_t offset = fr_header.FirstAttributeOffset;

  // 2. $STANDARD_INFORMATION (0x10)
  {
    RawAttributeHeader attr;
    attr.TypeCode = kAttributeStandardInformation;
    attr.FormCode = 0;  // Resident
    attr.Instance = 1;
    const ULONG val_offset = sizeof(RawAttributeHeader) + sizeof(RawResidentForm);
    const ULONG attr_len = val_offset + sizeof(RawStandardInfo);
    attr.RecordLength = (attr_len + 7) & ~7;

    RawResidentForm resident;
    resident.ValueLength = sizeof(RawStandardInfo);
    resident.ValueOffset = static_cast<USHORT>(val_offset);

    RawStandardInfo std_info;

    std::memcpy(mft + offset, &attr, sizeof(attr));
    std::memcpy(mft + offset + sizeof(attr), &resident, sizeof(resident));
    std::memcpy(mft + offset + val_offset, &std_info, sizeof(std_info));
    offset += attr.RecordLength;
  }

  // 3. $FILE_NAME (0x30) with FileSize = 0 (the unmaintained directory cache value)
  {
    RawAttributeHeader attr;
    attr.TypeCode = kAttributeFileName;
    attr.FormCode = 0;  // Resident
    attr.Instance = 2;
    const ULONG val_offset = sizeof(RawAttributeHeader) + sizeof(RawResidentForm);
    const ULONG attr_len = val_offset + sizeof(RawFileName);
    attr.RecordLength = (attr_len + 7) & ~7;

    RawResidentForm resident;
    resident.ValueLength = sizeof(RawFileName);
    resident.ValueOffset = static_cast<USHORT>(val_offset);

    RawFileName fn; fn.ParentDirectory = 5; fn.ParentSequence = 1;
    fn.FileSize = 0;  // Unmaintained in MFT!

    std::memcpy(mft + offset, &attr, sizeof(attr));
    std::memcpy(mft + offset + sizeof(attr), &resident, sizeof(resident));
    std::memcpy(mft + offset + val_offset, &fn, sizeof(fn));
    offset += attr.RecordLength;
  }

  // 4. $DATA (0x80)
  if (data_kind == DataAttrKind::kNonresident) {
    RawAttributeHeader attr;
    attr.TypeCode = kAttributeData;
    attr.FormCode = 1;  // Non-resident
    attr.Instance = 3;
    attr.RecordLength = 72;

    RawNonresidentForm non_res;
    non_res.LowestVcn = 0;
    non_res.HighestVcn = 10;
    non_res.AllocatedLength = (non_resident_size + 4095) & ~4095;
    non_res.FileSize = non_resident_size;
    non_res.ValidDataLength = non_resident_size;

    std::memcpy(mft + offset, &attr, sizeof(attr));
    std::memcpy(mft + offset + sizeof(attr), &non_res, sizeof(non_res));
    offset += attr.RecordLength;
  } else if (data_kind == DataAttrKind::kResidentNonEmpty) {
    RawAttributeHeader attr;
    attr.TypeCode = kAttributeData;
    attr.FormCode = 0;  // Resident
    attr.Instance = 3;
    const ULONG val_offset = sizeof(RawAttributeHeader) + sizeof(RawResidentForm);
    constexpr ULONG kDataValLen = 42;
    const ULONG attr_len = val_offset + kDataValLen;
    attr.RecordLength = (attr_len + 7) & ~7;

    RawResidentForm resident;
    resident.ValueLength = kDataValLen;
    resident.ValueOffset = static_cast<USHORT>(val_offset);

    std::memcpy(mft + offset, &attr, sizeof(attr));
    std::memcpy(mft + offset + sizeof(attr), &resident, sizeof(resident));
    // Content bytes initialized to 0
    offset += attr.RecordLength;
  } else if (data_kind == DataAttrKind::kResidentEmpty) {
    RawAttributeHeader attr;
    attr.TypeCode = kAttributeData;
    attr.FormCode = 0;  // Resident
    attr.Instance = 3;
    const ULONG val_offset = sizeof(RawAttributeHeader) + sizeof(RawResidentForm);
    attr.RecordLength = (val_offset + 7) & ~7;

    RawResidentForm resident;
    resident.ValueLength = 0;
    resident.ValueOffset = static_cast<USHORT>(val_offset);

    std::memcpy(mft + offset, &attr, sizeof(attr));
    std::memcpy(mft + offset + sizeof(attr), &resident, sizeof(resident));
    offset += attr.RecordLength;
  }

  // 5. End marker (0xFFFFFFFF)
  if (offset + sizeof(ULONG) <= kRecordSize) {
    const ULONG end_code = kAttributeEnd;
    std::memcpy(mft + offset, &end_code, sizeof(end_code));
  }

  return buffer;
}

TEST_CASE("MftMetadataReader: reads non-resident $DATA size even when $FILE_NAME size is 0") {
  constexpr uint64_t kTestFrn = 0x10000000000B0ULL;
  constexpr uint64_t kExpectedSize = 54321ULL;

  FakeMftGateway gateway;
  gateway.SetRecord(kTestFrn, BuildSyntheticRecord(kTestFrn, DataAttrKind::kNonresident, kExpectedSize));

  MftMetadataReader reader(reinterpret_cast<HANDLE>(1), gateway);
  FILETIME mod_time = kFileTimeNotLoaded;
  uint64_t file_size = kFileSizeNotLoaded;

  CHECK(reader.TryGetMetadata(kTestFrn, &mod_time, &file_size));
  CHECK(file_size == kExpectedSize);
  CHECK(mod_time.dwLowDateTime == 123456);
  CHECK(mod_time.dwHighDateTime == 789012);
}

TEST_CASE("MftMetadataReader: reads resident $DATA size accurately") {
  constexpr uint64_t kTestFrn = 0x10000000000B1ULL;

  FakeMftGateway gateway;
  gateway.SetRecord(kTestFrn, BuildSyntheticRecord(kTestFrn, DataAttrKind::kResidentNonEmpty));

  MftMetadataReader reader(reinterpret_cast<HANDLE>(1), gateway);
  FILETIME mod_time = kFileTimeNotLoaded;
  uint64_t file_size = kFileSizeNotLoaded;

  CHECK(reader.TryGetMetadata(kTestFrn, &mod_time, &file_size));
  CHECK(file_size == 42ULL);
}

TEST_CASE("MftMetadataReader: genuine 0-byte file with resident $DATA yields size 0") {
  constexpr uint64_t kTestFrn = 0x10000000000B2ULL;

  FakeMftGateway gateway;
  gateway.SetRecord(kTestFrn, BuildSyntheticRecord(kTestFrn, DataAttrKind::kResidentEmpty));

  MftMetadataReader reader(reinterpret_cast<HANDLE>(1), gateway);
  FILETIME mod_time = kFileTimeNotLoaded;
  uint64_t file_size = kFileSizeNotLoaded;

  CHECK(reader.TryGetMetadata(kTestFrn, &mod_time, &file_size));
  CHECK(file_size == 0ULL);
}

TEST_CASE("MftMetadataReader: file missing $DATA in base record preserves kFileSizeNotLoaded") {
  constexpr uint64_t kTestFrn = 0x10000000000B3ULL;

  FakeMftGateway gateway;
  gateway.SetRecord(kTestFrn, BuildSyntheticRecord(kTestFrn, DataAttrKind::kNone));

  MftMetadataReader reader(reinterpret_cast<HANDLE>(1), gateway);
  FILETIME mod_time = kFileTimeNotLoaded;
  uint64_t file_size = kFileSizeNotLoaded;

  // mod_time is found, but file_size should remain the sentinel for lazy loading fallback
  CHECK(reader.TryGetMetadata(kTestFrn, &mod_time, &file_size));
  CHECK(file_size == kFileSizeNotLoaded);
  CHECK(mod_time.dwLowDateTime == 123456);
}

}  // namespace

#endif  // _WIN32
