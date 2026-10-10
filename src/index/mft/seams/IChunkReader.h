#pragma once

#include <cstddef>
#include <vector>

#include "index/mft/seams/MftTypes.h"

namespace mft_seams {

/**
 * @class IChunkReader
 * @brief Seam 2: Unbuffered high-throughput raw chunk streaming.
 */
class IChunkReader {
 public:
  virtual ~IChunkReader() = default;

  /**
   * Initializes streaming over the given disk extents with the specified chunk size.
   * @param extents The list of disk extents to read.
   * @param chunk_size Target sector-aligned buffer size in bytes (e.g. 4 MB or 8 MB).
   * @return true if streaming initialized successfully, false otherwise.
   */
  [[nodiscard]] virtual bool StartStreaming(const std::vector<MftDiskExtent>& extents,
                                            size_t chunk_size) = 0;

  /**
   * Fetches the next sequential chunk buffer from the stream.
   * @param out_chunk Output chunk buffer.
   * @return true if a chunk was retrieved, false on EOF or read error.
   */
  [[nodiscard]] virtual bool GetNextChunk(ChunkBuffer& out_chunk) = 0;

  /**
   * Stops streaming and releases I/O buffers and worker threads.
   */
  virtual void StopStreaming() = 0;

  /**
   * Returns cumulative I/O telemetry statistics.
   */
  [[nodiscard]] virtual ReaderStats GetStats() const = 0;
};

}  // namespace mft_seams
