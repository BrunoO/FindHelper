#pragma once

// Shared synthetic NTFS record builder for MFT parser tests.
// Single source of truth for SyntheticRecordParams / CreateSyntheticRecord
// (previously duplicated in RawMftRecordParserTests.cpp and
// MftDifferentialHarnessTests.cpp).

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "index/mft/NtfsRecordStructures.h"

namespace mft_test_helpers {

using mft::NtfsAttributeRecordHeader;
using mft::NtfsFileNameAttribute;
using mft::NtfsFileRecordHeader;
using mft::NtfsStandardInformation;
using mft::kAttributeData;
using mft::kAttributeEnd;
using mft::kAttributeFileName;
using mft::kAttributeStandardInformation;
using mft::kFileRecordSignature;

// NTFS attribute bits (Windows SDK values, kept local for cross-platform tests).
inline constexpr uint32_t kTestAttributeArchive = 0x20U;
inline constexpr uint32_t kTestAttributeOffline = 0x1000U;

struct SyntheticRecordParams {
  uint64_t record_num = 0;
  uint64_t base_record_num = 0;
  uint64_t parent_frn = 0;
  std::u16string name;
  uint8_t file_namespace = 1;
  uint64_t file_size = 0;
  uint64_t timestamp = 0x01DA123456789ABCULL;
  bool is_directory = false;
  bool in_use = true;
  // NTFS SI file-attribute bitmask (FILE_ATTRIBUTE_* values, SDK-independent).
  uint32_t file_attributes = kTestAttributeArchive;
};

// Helper to construct a synthetic 1024-byte NTFS record
// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-union-access,cppcoreguidelines-pro-bounds-array-to-pointer-decay)
inline std::vector<char> CreateSyntheticRecord(const SyntheticRecordParams& params) {
  std::vector<char> record(1024, 0);

  auto* header = reinterpret_cast<NtfsFileRecordHeader*>(record.data());
  header->signature = kFileRecordSignature;
  header->usa_offset = static_cast<uint16_t>(sizeof(NtfsFileRecordHeader));
  header->usa_count = 3;  // 1 USN + 2 sector trailers for 1024-byte record
  header->sequence_number = 1;
  header->flags = (params.in_use ? 0x0001U : 0x0000U) | (params.is_directory ? 0x0002U : 0x0000U);
  header->base_file_record_number = params.base_record_num;
  header->segment_number_low_part = static_cast<uint32_t>(params.record_num & 0xFFFFFFFFU);
  header->segment_number_high_part = static_cast<uint16_t>(params.record_num >> 32U);

  // Setup USA fixup array
  auto* fixup_array = reinterpret_cast<uint16_t*>(record.data() + header->usa_offset);
  fixup_array[0] = 0xABCDU;
  fixup_array[1] = 0x1111U;
  fixup_array[2] = 0x2222U;

  *reinterpret_cast<uint16_t*>(record.data() + 510) = 0xABCDU;
  *reinterpret_cast<uint16_t*>(record.data() + 1022) = 0xABCDU;

  size_t offset = (header->usa_offset + 3 * sizeof(uint16_t) + 7U) & ~7U;
  header->first_attribute_offset = static_cast<uint16_t>(offset);

  // 1. $STANDARD_INFORMATION (0x10)
  auto* std_attr = reinterpret_cast<NtfsAttributeRecordHeader*>(record.data() + offset);
  std_attr->type_code = kAttributeStandardInformation;
  std_attr->form_code = 0;
  std_attr->form.resident.value_offset = sizeof(NtfsAttributeRecordHeader);
  std_attr->form.resident.value_length = sizeof(NtfsStandardInformation);
  std_attr->record_length = sizeof(NtfsAttributeRecordHeader) + sizeof(NtfsStandardInformation);

  auto* std_info = reinterpret_cast<NtfsStandardInformation*>(
      record.data() + offset + std_attr->form.resident.value_offset);
  std_info->last_modification_time = params.timestamp;
  std_info->file_attributes = params.file_attributes;
  offset += std_attr->record_length;

  // 2. $FILE_NAME (0x30)
  auto* fn_attr = reinterpret_cast<NtfsAttributeRecordHeader*>(record.data() + offset);
  fn_attr->type_code = kAttributeFileName;
  fn_attr->form_code = 0;
  fn_attr->form.resident.value_offset = sizeof(NtfsAttributeRecordHeader);

  const size_t name_bytes = params.name.size() * sizeof(char16_t);
  const size_t fn_payload_size = sizeof(NtfsFileNameAttribute) + name_bytes;
  fn_attr->form.resident.value_length = static_cast<uint32_t>(fn_payload_size);
  fn_attr->record_length = (sizeof(NtfsAttributeRecordHeader) + fn_payload_size + 7U) & ~7U;

  auto* fn_payload = reinterpret_cast<NtfsFileNameAttribute*>(
      record.data() + offset + fn_attr->form.resident.value_offset);
  fn_payload->parent_directory = params.parent_frn;
  fn_payload->file_name_length = static_cast<uint8_t>(params.name.size());
  fn_payload->file_name_namespace = params.file_namespace;
  fn_payload->real_size = params.file_size;
  std::memcpy(fn_payload->file_name, params.name.data(), name_bytes);
  offset += fn_attr->record_length;

  // 3. $DATA (0x80) - only for files
  if (!params.is_directory) {
    auto* data_attr = reinterpret_cast<NtfsAttributeRecordHeader*>(record.data() + offset);
    data_attr->type_code = kAttributeData;
    data_attr->form_code = 0;
    data_attr->name_length = 0;
    data_attr->form.resident.value_length = static_cast<uint32_t>(params.file_size);
    data_attr->form.resident.value_offset = sizeof(NtfsAttributeRecordHeader);
    data_attr->record_length = (sizeof(NtfsAttributeRecordHeader) + sizeof(uint64_t) + 7U) & ~7U;
    offset += data_attr->record_length;
  }

  // 4. $END (0xFFFFFFFF)
  auto* end_attr = reinterpret_cast<NtfsAttributeRecordHeader*>(record.data() + offset);
  end_attr->type_code = kAttributeEnd;
  end_attr->record_length = sizeof(NtfsAttributeRecordHeader);

  return record;
}
// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-union-access,cppcoreguidelines-pro-bounds-array-to-pointer-decay)

}  // namespace mft_test_helpers
