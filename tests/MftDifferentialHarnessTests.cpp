#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "MftSyntheticRecordHelpers.h"
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
using mft_test_helpers::CreateSyntheticRecord;
using mft_test_helpers::SyntheticRecordParams;
using mft_test_helpers::kTestAttributeArchive;
using mft_test_helpers::kTestAttributeOffline;
using ntfs_file_reference::NtfsFileReference;
using usn_journal::JournalCursor;

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
      {37, 0, MakeFrn(1, 36), u"document.txt", 1, 4096, 0x01DA900000000000ULL, false, true,
       kTestAttributeOffline,},
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
          spec.file_attributes,
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
    CHECK(entry_raw->fileAttributes == entry_control->fileAttributes);
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
