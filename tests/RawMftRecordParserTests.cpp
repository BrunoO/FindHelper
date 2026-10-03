#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "MftSyntheticRecordHelpers.h"
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
using mft_test_helpers::CreateSyntheticRecord;
using mft_test_helpers::SyntheticRecordParams;

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

TEST_CASE("RawMftRecordParser: Extracts primary name from extension record during parse") {
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
  const auto& stashed = parser.GetExtensionNames();
  REQUIRE(stashed.size() == 1);
  CHECK(stashed[0].base_frn == kBaseRecordNum);
  CHECK(stashed[0].name == "ext.dat");
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
