#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "index/FileIndex.h"
#include "index/FileName.h"
#include "index/NtfsFileReference.h"
#include "index/mft/seams/FakeChunkReader.h"
#include "index/mft/seams/FakeExtentProvider.h"
#include "index/mft/seams/FileIndexIngestor.h"
#include "index/mft/seams/IChunkReader.h"
#include "index/mft/seams/IExtentProvider.h"
#include "index/mft/seams/IIngestor.h"
#include "index/mft/seams/IRecordParser.h"
#include "index/mft/seams/MftTypes.h"
#include "path/PathUtils.h"

namespace {

using mft_seams::ChunkBuffer;
using mft_seams::ChunkSpan;
using mft_seams::FakeChunkReader;
using mft_seams::FakeExtentProvider;
using mft_seams::FileIndexIngestor;
using mft_seams::IRecordParser;
using mft_seams::MftDiskExtent;
using mft_seams::ParserStats;
using mft_seams::ReaderStats;
using mft_seams::VolumeGeometry;

// Test fake parser that yields predetermined entries from synthetic chunks
class ScriptedRecordParser : public IRecordParser {
 public:
  void SetEntriesToEmit(std::vector<FileIndex::PopulationBatchEntry> entries) {
    entries_to_emit_ = std::move(entries);
  }

  void SetFailParse(bool fail) { fail_parse_ = fail; }

  [[nodiscard]] bool ParseChunk(
      const ChunkSpan& chunk_span,
      std::vector<FileIndex::PopulationBatchEntry>& out_entries,
      ParserStats& out_stats) override {
    if (fail_parse_) {
      ++out_stats.parse_errors;
      return false;
    }

    out_entries.insert(out_entries.end(), entries_to_emit_.begin(), entries_to_emit_.end());
    out_stats.records_parsed += entries_to_emit_.size();
    out_stats.base_records += entries_to_emit_.size();
    (void)chunk_span;
    return true;
  }

 private:
  std::vector<FileIndex::PopulationBatchEntry> entries_to_emit_{};
  bool fail_parse_ = false;
};

}  // namespace

TEST_CASE("FakeExtentProvider returns configured geometry and extents") {
  VolumeGeometry geom{};
  geom.bytes_per_sector = 4096;
  geom.bytes_per_cluster = 4096;
  geom.bytes_per_file_record = 1024;
  geom.mft_start_lcn = 786432;
  geom.total_clusters = 244190645;

  std::vector<MftDiskExtent> extents;
  extents.push_back({786432, 16384, 786432ULL * 4096ULL, 16384ULL * 4096ULL});
  extents.push_back({900000, 8192, 900000ULL * 4096ULL, 8192ULL * 4096ULL});

  FakeExtentProvider provider(geom, extents);

  VolumeGeometry read_geom{};
  REQUIRE(provider.GetVolumeGeometry(read_geom));
  CHECK(read_geom.bytes_per_sector == 4096);
  CHECK(read_geom.bytes_per_cluster == 4096);
  CHECK(read_geom.bytes_per_file_record == 1024);
  CHECK(read_geom.mft_start_lcn == 786432);
  CHECK(read_geom.total_clusters == 244190645);

  std::vector<MftDiskExtent> read_extents;
  REQUIRE(provider.GetMftExtents(read_extents));
  REQUIRE(read_extents.size() == 2);
  CHECK(read_extents[0].start_lcn == 786432);
  CHECK(read_extents[0].cluster_count == 16384);
  CHECK(read_extents[1].start_lcn == 900000);
  CHECK(read_extents[1].cluster_count == 8192);
}

TEST_CASE("FakeExtentProvider respects failure simulation flags") {
  FakeExtentProvider provider;
  provider.SetFailGeometry(true);
  provider.SetFailExtents(true);

  VolumeGeometry geom{};
  CHECK_FALSE(provider.GetVolumeGeometry(geom));

  std::vector<MftDiskExtent> extents;
  CHECK_FALSE(provider.GetMftExtents(extents));
}

TEST_CASE("FakeChunkReader streams enqueued chunks sequentially and reports EOF") {
  FakeChunkReader reader;
  std::vector<char> chunk1(4096, 'A');
  std::vector<char> chunk2(2048, 'B');

  reader.EnqueueChunk(std::move(chunk1));
  reader.EnqueueChunk(std::move(chunk2));

  std::vector<MftDiskExtent> extents{{100, 10, 409600, 40960}};
  REQUIRE(reader.StartStreaming(extents, 4096));

  ChunkBuffer buffer1{};
  REQUIRE(reader.GetNextChunk(buffer1));
  CHECK(buffer1.size == 4096);
  CHECK(buffer1.stream_offset == 0);
  CHECK(buffer1.first_record_number == 0);
  CHECK_FALSE(buffer1.is_final);
  CHECK(buffer1.data[0] == 'A');

  ChunkBuffer buffer2{};
  REQUIRE(reader.GetNextChunk(buffer2));
  CHECK(buffer2.size == 2048);
  CHECK(buffer2.stream_offset == 4096);
  CHECK(buffer2.first_record_number == 4);
  CHECK(buffer2.is_final);
  CHECK(buffer2.data[0] == 'B');

  // Next call should report EOF
  ChunkBuffer buffer3{};
  CHECK_FALSE(reader.GetNextChunk(buffer3));

  const ReaderStats stats = reader.GetStats();
  CHECK(stats.total_bytes_read == 6144);
  CHECK(stats.total_chunks_read == 2);
}

TEST_CASE("FileIndexIngestor commits batches and finalizes index") {
  FileIndex index;
  FileIndexIngestor ingestor(index);

  constexpr uint64_t kVolumeRootFrn = 0x0001000000000005ULL;
  constexpr uint64_t kDirId = 0x0001000000000100ULL;
  constexpr uint64_t kFileId = 0x0001000000000200ULL;

  std::vector<FileIndex::PopulationBatchEntry> batch;
  batch.push_back({ntfs_file_reference::NtfsFileReference(kDirId),
                   ntfs_file_reference::NtfsFileReference(kVolumeRootFrn),
                   file_name::FileName("Projects"), true,});
  batch.push_back({ntfs_file_reference::NtfsFileReference(kFileId),
                   ntfs_file_reference::NtfsFileReference(kDirId),
                   file_name::FileName("build.ninja"), false,});

  ingestor.IngestBatch(batch);
  ingestor.ResolveExtensions();
  ingestor.FinalizeIndex();

  REQUIRE(index.Size() == 2);
  const std::string file_path = index.GetPathAccessor().GetPathCopy(kFileId);
  CHECK_FALSE(file_path.empty());
  CHECK(file_path.find("Projects") != std::string::npos);
  CHECK(file_path.find("build.ninja") != std::string::npos);
}

TEST_CASE("End-to-end MFT pipeline integration across all 4 seams") {
  // 1. Extent Provider
  VolumeGeometry geom{};
  geom.bytes_per_sector = 512;
  geom.bytes_per_cluster = 4096;
  geom.bytes_per_file_record = 1024;
  geom.mft_start_lcn = 1000;
  geom.total_clusters = 50000;

  std::vector<MftDiskExtent> extents{{1000, 10, 4096000, 40960}};
  FakeExtentProvider extent_provider(geom, extents);

  // 2. Chunk Reader
  FakeChunkReader chunk_reader;
  std::vector<char> raw_chunk(4096, 0);
  chunk_reader.EnqueueChunk(std::move(raw_chunk));

  // 3. Record Parser
  ScriptedRecordParser parser;
  constexpr uint64_t kVolumeRootFrn = 0x0001000000000005ULL;
  constexpr uint64_t kDocsDirId = 0x0001000000000110ULL;
  constexpr uint64_t kReadmeFileId = 0x0001000000000120ULL;

  std::vector<FileIndex::PopulationBatchEntry> parsed_entries;
  parsed_entries.push_back({ntfs_file_reference::NtfsFileReference(kDocsDirId),
                            ntfs_file_reference::NtfsFileReference(kVolumeRootFrn),
                            file_name::FileName("Documents"), true,});
  parsed_entries.push_back({ntfs_file_reference::NtfsFileReference(kReadmeFileId),
                            ntfs_file_reference::NtfsFileReference(kDocsDirId),
                            file_name::FileName("readme.txt"), false,});
  parser.SetEntriesToEmit(parsed_entries);

  // 4. Ingestor
  FileIndex index;
  FileIndexIngestor ingestor(index);

  // --- Execute Pipeline ---
  std::vector<MftDiskExtent> mft_extents;
  REQUIRE(extent_provider.GetMftExtents(mft_extents));
  REQUIRE(chunk_reader.StartStreaming(mft_extents, 4096));

  ChunkBuffer chunk_buf{};
  ParserStats parse_stats{};
  while (chunk_reader.GetNextChunk(chunk_buf)) {
    ChunkSpan span{chunk_buf.data, chunk_buf.size, chunk_buf.first_record_number};
    std::vector<FileIndex::PopulationBatchEntry> batch;
    REQUIRE(parser.ParseChunk(span, batch, parse_stats));
    ingestor.IngestBatch(batch);
  }
  chunk_reader.StopStreaming();

  ingestor.ResolveExtensions();
  ingestor.FinalizeIndex();

  // --- Verify Resulting Index ---
  CHECK(parse_stats.records_parsed == 2);
  REQUIRE(index.Size() == 2);

  const std::string doc_path = index.GetPathAccessor().GetPathCopy(kDocsDirId);
  const std::string readme_path = index.GetPathAccessor().GetPathCopy(kReadmeFileId);

  CHECK(doc_path.find("Documents") != std::string::npos);
  CHECK(readme_path.find("Documents") != std::string::npos);
  CHECK(readme_path.find("readme.txt") != std::string::npos);
}
