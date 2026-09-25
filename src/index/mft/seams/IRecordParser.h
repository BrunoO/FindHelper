#pragma once

#include <vector>

#include "index/FileIndex.h"
#include "index/mft/seams/MftTypes.h"

namespace mft_seams {

/**
 * @class IRecordParser
 * @brief Seam 3: Idempotent in-place parsing of raw MFT records.
 */
class IRecordParser {
 public:
  virtual ~IRecordParser() = default;

  /**
   * Parses raw record bytes from a chunk span into flat FileIndex population entries.
   * Performs idempotent sector fixup, attribute traversal, fast-ASCII name extraction,
   * and authoritative $DATA sizing.
   *
   * @param chunk_span Non-owning view into the chunk memory buffer.
   * @param out_entries Output vector populated with base file entries.
   * @param out_stats Cumulative parsing statistics.
   * @return true on success, false if fatal corruption encountered.
   */
  [[nodiscard]] virtual bool ParseChunk(
      const ChunkSpan& chunk_span,
      std::vector<FileIndex::PopulationBatchEntry>& out_entries,
      ParserStats& out_stats) = 0;
};

}  // namespace mft_seams
