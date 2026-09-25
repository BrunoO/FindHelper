#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "index/FileIndex.h"
#include "index/mft/NtfsRecordStructures.h"
#include "index/mft/RawMftRecordParser.h"
#include "index/mft/seams/MftTypes.h"

namespace {

// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-union-access,cppcoreguidelines-pro-bounds-array-to-pointer-decay)

using mft::kAttributeData;
using mft::kAttributeEnd;
using mft::kAttributeFileName;
using mft::kAttributeStandardInformation;
using mft::kFileRecordSignature;
using mft::NtfsAttributeRecordHeader;
using mft::NtfsFileNameAttribute;
using mft::NtfsFileRecordHeader;
using mft::NtfsStandardInformation;
using mft::RawMftRecordParser;
using mft_seams::ChunkSpan;
using mft_seams::ParserStats;

struct SyntheticRecordParams {
  uint64_t record_num = 0;
  uint64_t base_record_num = 0;
  uint64_t parent_frn = 0;
  std::u16string name;
  uint8_t file_namespace = 1;
  uint64_t file_size = 0;
  bool is_directory = false;
  bool in_use = true;
};

// Helper to construct a synthetic 1024-byte NTFS record
std::vector<char> CreateSyntheticRecord(const SyntheticRecordParams& params) {
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
  fixup_array[0] = 0xABCDU;  // USN
  fixup_array[1] = 0x1111U;  // original sector 1 trailer
  fixup_array[2] = 0x2222U;  // original sector 2 trailer

  // Place USN at sector ends (byte 510 and byte 1022)
  *reinterpret_cast<uint16_t*>(record.data() + 510) = 0xABCDU;
  *reinterpret_cast<uint16_t*>(record.data() + 1022) = 0xABCDU;

  size_t offset = (header->usa_offset + 3 * sizeof(uint16_t) + 7U) & ~7U;
  header->first_attribute_offset = static_cast<uint16_t>(offset);

  // 1. $STANDARD_INFORMATION (0x10)
  auto* std_attr = reinterpret_cast<NtfsAttributeRecordHeader*>(record.data() + offset);
  std_attr->type_code = kAttributeStandardInformation;
  std_attr->form_code = 0;  // Resident
  std_attr->form.resident.value_offset = sizeof(NtfsAttributeRecordHeader);
  std_attr->form.resident.value_length = sizeof(NtfsStandardInformation);
  std_attr->record_length = sizeof(NtfsAttributeRecordHeader) + sizeof(NtfsStandardInformation);

  auto* std_info = reinterpret_cast<NtfsStandardInformation*>(
      record.data() + offset + std_attr->form.resident.value_offset);
  std_info->last_modification_time = 0x01DA123456789ABCULL;
  offset += std_attr->record_length;

  // 2. $FILE_NAME (0x30)
  auto* fn_attr = reinterpret_cast<NtfsAttributeRecordHeader*>(record.data() + offset);
  fn_attr->type_code = kAttributeFileName;
  fn_attr->form_code = 0;  // Resident
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
    data_attr->form_code = 0;  // Resident
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

TEST_CASE("RawMftRecordParser: ApplyIdempotentUsaFixup restores sector trailers") {
  SyntheticRecordParams params{};
  params.record_num = 10;
  params.parent_frn = 5;
  params.name = u"test.txt";
  params.file_namespace = 1;
  params.file_size = 100;
  params.is_directory = false;
  std::vector<char> record = CreateSyntheticRecord(params);

  // Before fixup: sector ends have USN (0xABCD)
  CHECK(*reinterpret_cast<uint16_t*>(record.data() + 510) == 0xABCDU);
  CHECK(*reinterpret_cast<uint16_t*>(record.data() + 1022) == 0xABCDU);

  // First call: restores original sector trailers (0x1111 and 0x2222)
  REQUIRE(RawMftRecordParser::ApplyIdempotentUsaFixup(record.data(), record.size(), 512));
  CHECK(*reinterpret_cast<uint16_t*>(record.data() + 510) == 0x1111U);
  CHECK(*reinterpret_cast<uint16_t*>(record.data() + 1022) == 0x2222U);

  // Second call (Idempotency): returns true and keeps sector trailers unchanged
  REQUIRE(RawMftRecordParser::ApplyIdempotentUsaFixup(record.data(), record.size(), 512));
  CHECK(*reinterpret_cast<uint16_t*>(record.data() + 510) == 0x1111U);
  CHECK(*reinterpret_cast<uint16_t*>(record.data() + 1022) == 0x2222U);
}

TEST_CASE("RawMftRecordParser: ApplyIdempotentUsaFixup rejects torn writes") {
  SyntheticRecordParams params{};
  params.record_num = 10;
  params.parent_frn = 5;
  params.name = u"test.txt";
  params.file_namespace = 1;
  params.file_size = 100;
  params.is_directory = false;
  std::vector<char> record = CreateSyntheticRecord(params);

  // Corrupt sector 1 trailer so it matches neither USN (0xABCD) nor original (0x1111)
  *reinterpret_cast<uint16_t*>(record.data() + 510) = 0x9999U;

  CHECK_FALSE(RawMftRecordParser::ApplyIdempotentUsaFixup(record.data(), record.size(), 512));
}

TEST_CASE("RawMftRecordParser: TranscodeUtf16ToUtf8 handles ASCII and UTF-8") {
  // Pure ASCII
  const std::u16string ascii = u"Hello_World.cpp";
  CHECK(RawMftRecordParser::TranscodeUtf16ToUtf8(ascii.data(), ascii.size()) == "Hello_World.cpp");

  // Non-ASCII (accented characters: "café")
  const std::u16string non_ascii = u"caf\u00E9.txt";
  CHECK(RawMftRecordParser::TranscodeUtf16ToUtf8(non_ascii.data(), non_ascii.size()) == "café.txt");

  // Empty string
  CHECK(RawMftRecordParser::TranscodeUtf16ToUtf8(nullptr, 0).empty());
}

TEST_CASE("RawMftRecordParser: ParseChunk parses base file records correctly") {
  RawMftRecordParser parser(512, 1024);

  constexpr uint64_t kRecordNum = 1234;
  constexpr uint64_t kParentFrn = 0x0001000000000005ULL;
  constexpr uint64_t kFileSize = 40960;

  SyntheticRecordParams params{};
  params.record_num = kRecordNum;
  params.parent_frn = kParentFrn;
  params.name = u"document.pdf";
  params.file_namespace = 1;
  params.file_size = kFileSize;
  params.is_directory = false;
  std::vector<char> record = CreateSyntheticRecord(params);

  ChunkSpan span{};
  span.data = record.data();
  span.size = record.size();
  span.first_record_number = kRecordNum;

  std::vector<FileIndex::PopulationBatchEntry> entries;
  ParserStats stats{};
  REQUIRE(parser.ParseChunk(span, entries, stats));

  REQUIRE(entries.size() == 1);
  const auto& entry = entries[0];
  CHECK(entry.name.View() == "document.pdf");
  CHECK(entry.parent_id.raw == kParentFrn);
  CHECK(entry.id.RecordNumber() == kRecordNum);
  CHECK(entry.file_size == kFileSize);
  CHECK_FALSE(entry.is_directory);

  CHECK(stats.records_parsed == 1);
  CHECK(stats.base_records == 1);
  CHECK(stats.extension_records == 0);
  CHECK(stats.fixup_failures == 0);
  CHECK(stats.parse_errors == 0);
}

TEST_CASE("RawMftRecordParser: Filters extension records into orphan stash") {
  RawMftRecordParser parser(512, 1024);

  constexpr uint64_t kBaseRecordNum = 100;
  constexpr uint64_t kExtRecordNum = 105;

  SyntheticRecordParams params{};
  params.record_num = kExtRecordNum;
  params.base_record_num = kBaseRecordNum;
  params.parent_frn = 5;
  params.name = u"ext.dat";
  params.file_namespace = 1;
  params.file_size = 0;
  params.is_directory = false;
  std::vector<char> ext_record = CreateSyntheticRecord(params);

  ChunkSpan span{};
  span.data = ext_record.data();
  span.size = ext_record.size();
  span.first_record_number = kExtRecordNum;

  std::vector<FileIndex::PopulationBatchEntry> entries;
  ParserStats stats{};
  REQUIRE(parser.ParseChunk(span, entries, stats));

  // Should NOT emit into base entries
  CHECK(entries.empty());
  CHECK(stats.base_records == 0);
  CHECK(stats.extension_records == 1);

  // Should be stashed in parser's extension list
  const auto& stashed = parser.GetExtensionRecords();
  REQUIRE(stashed.size() == 1);
  CHECK(stashed[0].base_record_number == kBaseRecordNum);
  CHECK(stashed[0].record_number == kExtRecordNum);
  CHECK(stashed[0].record_data.size() == 1024);
}

TEST_CASE("RawMftRecordParser: Skips unallocated / deleted records") {
  RawMftRecordParser parser(512, 1024);

  // Deleted record (in_use = false)
  SyntheticRecordParams params{};
  params.record_num = 200;
  params.parent_frn = 5;
  params.name = u"deleted.tmp";
  params.file_namespace = 1;
  params.file_size = 10;
  params.is_directory = false;
  params.in_use = false;
  std::vector<char> deleted_record = CreateSyntheticRecord(params);

  ChunkSpan span{};
  span.data = deleted_record.data();
  span.size = deleted_record.size();
  span.first_record_number = 200;

  std::vector<FileIndex::PopulationBatchEntry> entries;
  ParserStats stats{};
  REQUIRE(parser.ParseChunk(span, entries, stats));

  CHECK(entries.empty());
  CHECK(stats.records_filtered == 1);
  CHECK(stats.records_parsed == 0);
}

// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-union-access,cppcoreguidelines-pro-bounds-array-to-pointer-decay)

}  // namespace
