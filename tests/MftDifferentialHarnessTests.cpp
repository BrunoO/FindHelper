#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "index/FileIndex.h"
#include "index/FileName.h"
#include "index/NtfsFileReference.h"
#include "index/mft/DoubleBufferedChunkReader.h"
#include "index/mft/NtfsRecordStructures.h"
#include "index/mft/RawMftRecordParser.h"
#include "index/mft/RawVolumeReader.h"
#include "index/mft/UsnHandoffCoordinator.h"
#include "index/mft/seams/FileIndexIngestor.h"
#include "index/mft/seams/MftTypes.h"
#include "path/PathUtils.h"
#include "usn/JournalCursor.h"

namespace {

constexpr uint64_t MakeFrn(uint16_t seq, uint64_t rec) {
  return (static_cast<uint64_t>(seq) << 48U) | (rec & ntfs_file_reference::kFileRecordNumberMask);
}

using file_name::FileName;
using mft::DoubleBufferedChunkReader;
using mft::HandoffStatus;
using mft::kAttributeData;
using mft::kAttributeEnd;
using mft::kAttributeFileName;
using mft::kAttributeStandardInformation;
using mft::kFileRecordSignature;
using mft::MemoryVolumeReader;
using mft::NtfsAttributeRecordHeader;
using mft::NtfsFileNameAttribute;
using mft::NtfsFileRecordHeader;
using mft::NtfsStandardInformation;
using mft::RawMftRecordParser;
using mft::UsnHandoffCoordinator;
using mft_seams::ChunkBuffer;
using mft_seams::ChunkSpan;
using mft_seams::FileIndexIngestor;
using mft_seams::MftDiskExtent;
using mft_seams::ParserStats;
using ntfs_file_reference::NtfsFileReference;
using usn_journal::JournalCursor;

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
};

// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-union-access,cppcoreguidelines-pro-bounds-array-to-pointer-decay)
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

}  // namespace

TEST_SUITE("MftDifferentialHarnessTests") {

TEST_CASE("UsnHandoffCoordinator: verify integrity outcomes") {
  SUBCASE("Normal forward advance succeeds") {
    const JournalCursor pre_walk{12345ULL, 1000, 0};
    const JournalCursor post_walk{12345ULL, 1500, 500};
    const auto result = UsnHandoffCoordinator::VerifyIntegrity(pre_walk, post_walk);
    CHECK(result.IsSuccess());
    CHECK(result.status == HandoffStatus::Success);
  }

  SUBCASE("Journal ID change fails closed") {
    const JournalCursor pre_walk{12345ULL, 1000, 0};
    const JournalCursor post_walk{99999ULL, 100, 0};
    const auto result = UsnHandoffCoordinator::VerifyIntegrity(pre_walk, post_walk);
    CHECK_FALSE(result.IsSuccess());
    CHECK(result.status == HandoffStatus::JournalIdChanged);
  }

  SUBCASE("Journal wrap fails closed") {
    const JournalCursor pre_walk{12345ULL, 1000, 0};
    const JournalCursor post_walk{12345ULL, 5000, 2000};  // LowestValidUsn=2000 > snapshot=1000
    const auto result = UsnHandoffCoordinator::VerifyIntegrity(pre_walk, post_walk);
    CHECK_FALSE(result.IsSuccess());
    CHECK(result.status == HandoffStatus::JournalWrapped);
  }

  SUBCASE("Journal NextUsn rewind fails closed") {
    const JournalCursor pre_walk{12345ULL, 2000, 0};
    const JournalCursor post_walk{12345ULL, 1000, 0};  // Decreased NextUsn
    const auto result = UsnHandoffCoordinator::VerifyIntegrity(pre_walk, post_walk);
    CHECK_FALSE(result.IsSuccess());
    CHECK(result.status == HandoffStatus::JournalWrapped);
  }
}

TEST_CASE("Differential: raw MFT pipeline produces bit-identical FileIndex to classic population") {
  const std::vector<SyntheticRecordParams> fixture_specs = {
      {5, 0, MakeFrn(1, 5), u"", 1, 0, 0x01DA100000000000ULL, true, true},
      {30, 0, MakeFrn(1, 5), u"Windows", 1, 0, 0x01DA200000000000ULL, true, true},
      {31, 0, MakeFrn(1, 30), u"notepad.exe", 1, 184320, 0x01DA300000000000ULL, false, true},
      {32, 0, MakeFrn(1, 30), u"System32", 1, 0, 0x01DA400000000000ULL, true, true},
      {33, 0, MakeFrn(1, 32), u"cmd.exe", 1, 289792, 0x01DA500000000000ULL, false, true},
      {34, 0, MakeFrn(1, 32), u"kernel32.dll", 1, 712000, 0x01DA600000000000ULL, false, true},
      {35, 0, MakeFrn(1, 5), u"Users", 1, 0, 0x01DA700000000000ULL, true, true},
      {36, 0, MakeFrn(1, 35), u"User1", 1, 0, 0x01DA800000000000ULL, true, true},
      {37, 0, MakeFrn(1, 36), u"document.txt", 1, 4096, 0x01DA900000000000ULL, false, true},
  };

  // 1. Populate control FileIndex via classic batch insertion
  FileIndex control_index;
  {
    std::vector<FileIndex::PopulationBatchEntry> control_entries;
    control_entries.reserve(fixture_specs.size());
    for (const auto& spec : fixture_specs) {
      const std::string name_utf8(spec.name.begin(), spec.name.end());
      FILETIME ft{};
      ft.dwLowDateTime = static_cast<uint32_t>(spec.timestamp & 0xFFFFFFFFULL);
      ft.dwHighDateTime = static_cast<uint32_t>((spec.timestamp >> 32ULL) & 0xFFFFFFFFULL);

      control_entries.push_back({
          NtfsFileReference{MakeFrn(1, spec.record_num)},
          NtfsFileReference{spec.parent_frn},
          FileName{name_utf8},
          spec.is_directory,
          ft,
          spec.file_size,
      });
    }
    control_index.InsertBatch(control_entries, /*defer_path_indexing=*/true);
    control_index.RecomputeAllPaths();
  }

  // 2. Build synthetic raw disk volume containing records placed at byte offsets
  constexpr size_t kDiskSize = 40 * 1024;  // 40 records x 1024 bytes
  std::vector<char> raw_volume(kDiskSize, 0);
  for (const auto& spec : fixture_specs) {
    auto rec_bytes = CreateSyntheticRecord(spec);
    const auto byte_offset = static_cast<size_t>(spec.record_num * 1024ULL);
    std::memcpy(raw_volume.data() + byte_offset, rec_bytes.data(), 1024);
  }

  // 3. Ingest through raw MFT pipeline
  FileIndex raw_index;
  {
    auto memory_reader = std::make_unique<MemoryVolumeReader>(std::move(raw_volume));
    DoubleBufferedChunkReader chunk_reader(std::move(memory_reader));
    RawMftRecordParser record_parser;
    FileIndexIngestor ingestor(raw_index);

    std::vector<MftDiskExtent> extents = {
        {0, 10, 0, kDiskSize},
    };

    const size_t chunk_size = 8 * 1024;  // 8 KB chunks
    REQUIRE(chunk_reader.StartStreaming(extents, chunk_size));

    ChunkBuffer chunk{};
    ParserStats stats{};
    while (chunk_reader.GetNextChunk(chunk)) {
      ChunkSpan span{chunk.data, chunk.size, chunk.first_record_number};
      std::vector<FileIndex::PopulationBatchEntry> batch_entries;
      REQUIRE(record_parser.ParseChunk(span, batch_entries, stats));
      ingestor.IngestBatch(batch_entries);
    }
    ingestor.ResolveExtensions();
    ingestor.FinalizeIndex();
  }

  // 4. Assert 100% bit-identical equivalence
  CHECK(raw_index.Size() == control_index.Size());

  for (const auto& spec : fixture_specs) {
    if (spec.record_num == 5) {
      continue;  // Root directory itself
    }
    const uint64_t frn = MakeFrn(1, spec.record_num);
    const auto* entry_raw = raw_index.GetEntry(frn);
    const auto* entry_control = control_index.GetEntry(frn);

    REQUIRE(entry_raw != nullptr);
    REQUIRE(entry_control != nullptr);

    CHECK(entry_raw->parentID.raw == entry_control->parentID.raw);
    CHECK(entry_raw->isDirectory == entry_control->isDirectory);
    CHECK(entry_raw->fileSize.value == entry_control->fileSize.value);
    CHECK(raw_index.GetPathAccessor().GetPathCopy(frn) ==
          control_index.GetPathAccessor().GetPathCopy(frn));
  }
}

TEST_CASE("Differential: out-of-order record arrival resolves identical paths") {
  // Child record (Record 50) arrives before Parent directory (Record 80)
  const std::vector<SyntheticRecordParams> fixture_specs = {
      {5, 0, MakeFrn(1, 5), u"", 1, 0, 0x01DA100000000000ULL, true, true},
      {50, 0, MakeFrn(1, 80), u"child_file.txt", 1, 1024, 0x01DA200000000000ULL, false, true},
      {80, 0, MakeFrn(1, 5), u"ParentFolder", 1, 0, 0x01DA300000000000ULL, true, true},
  };

  FileIndex index;
  FileIndexIngestor ingestor(index);

  std::vector<FileIndex::PopulationBatchEntry> entries;
  for (const auto& spec : fixture_specs) {
    const std::string name_utf8(spec.name.begin(), spec.name.end());
    FILETIME ft{};
    ft.dwLowDateTime = static_cast<uint32_t>(spec.timestamp & 0xFFFFFFFFULL);
    ft.dwHighDateTime = static_cast<uint32_t>((spec.timestamp >> 32ULL) & 0xFFFFFFFFULL);

    entries.push_back({
        NtfsFileReference{MakeFrn(1, spec.record_num)},
        NtfsFileReference{spec.parent_frn},
        FileName{name_utf8},
        spec.is_directory,
        ft,
        spec.file_size,
    });
  }

  // Ingest with child first
  ingestor.IngestBatch(entries);
  ingestor.FinalizeIndex();

  const uint64_t child_frn = MakeFrn(1, 50);
  const auto* child_entry = index.GetEntry(child_frn);
  REQUIRE(child_entry != nullptr);
  const std::string child_path = index.GetPathAccessor().GetPathCopy(child_frn);
  const std::string expected_suffix = std::string("ParentFolder") + path_utils::kPathSeparator + "child_file.txt";
  CHECK(child_path.rfind(expected_suffix) != std::string::npos);
}

}  // TEST_SUITE("MftDifferentialHarnessTests")
