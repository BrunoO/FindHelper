#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "index/FileIndex.h"
#include "index/mft/NtfsRecordStructures.h"
#include "index/mft/seams/IRecordParser.h"
#include "index/mft/seams/MftTypes.h"

namespace mft {

/**
 * @class RawMftRecordParser
 * @brief High-performance raw MFT record parser implementing seam 3.
 *
 * Performs in-place idempotent USA fixup, sequential attribute traversal,
 * fast-ASCII transcoding, authoritative $DATA file sizing, and extension
 * record filtering.
 */
class RawMftRecordParser : public mft_seams::IRecordParser {
 public:
  struct ExtensionRecordEntry {
    uint64_t base_record_number = 0;
    uint64_t record_number = 0;
    std::vector<char> record_data;
  };

  explicit RawMftRecordParser(uint32_t bytes_per_sector = 512,
                              uint32_t bytes_per_record = 1024);

  /**
   * Parses raw record bytes from a chunk span into flat FileIndex population entries.
   */
  [[nodiscard]] bool ParseChunk(
      const mft_seams::ChunkSpan& chunk_span,
      std::vector<FileIndex::PopulationBatchEntry>& out_entries,
      mft_seams::ParserStats& out_stats) override;

  /**
   * Applies Update Sequence Array (USA) fixup in-place.
   * Idempotent: safe to run on pre-fixed records (e.g. from FSCTL_GET_NTFS_FILE_RECORD)
   * and detects torn writes / sector corruption.
   *
   * @param record_buffer Pointer to mutable record bytes.
   * @param record_size Total record size (typically 1024 or 4096 bytes).
   * @param bytes_per_sector Sector size (typically 512 or 4096 bytes).
   * @return true if fixup succeeded or was already valid, false on torn write.
   */
  [[nodiscard]] static bool ApplyIdempotentUsaFixup(
      char* record_buffer,
      size_t record_size,
      size_t bytes_per_sector);

  /**
   * Fast ASCII transcoding from UTF-16 to UTF-8.
   * Unrolls/vectorizes branchless conversion for ASCII code units (< 128)
   * with RFC 3629 UTF-8 fallback for non-ASCII.
   */
  [[nodiscard]] static std::string TranscodeUtf16ToUtf8(
      const char16_t* utf16_chars,
      size_t length);

  [[nodiscard]] const std::vector<ExtensionRecordEntry>& GetExtensionRecords() const noexcept {
    return extension_records_;
  }

  void ClearExtensionRecords() noexcept {
    extension_records_.clear();
  }

  void SetBytesPerSector(uint32_t bytes_per_sector) noexcept {
    bytes_per_sector_ = bytes_per_sector;
  }

  void SetBytesPerRecord(uint32_t bytes_per_record) noexcept {
    bytes_per_record_ = bytes_per_record;
  }

 private:
  void ParseSingleRecord(
      const mft_seams::ChunkSpan& chunk_span,
      size_t record_idx,
      std::vector<FileIndex::PopulationBatchEntry>& out_entries,
      mft_seams::ParserStats& out_stats);

  uint32_t bytes_per_sector_ = 512;
  uint32_t bytes_per_record_ = 1024;
  std::vector<ExtensionRecordEntry> extension_records_;
};

}  // namespace mft
