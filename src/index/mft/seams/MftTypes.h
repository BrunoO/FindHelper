#pragma once

#include <cstddef>
#include <cstdint>

namespace mft_seams {

inline constexpr size_t kDefaultChunkSizeBytes = 4ULL * 1024ULL * 1024ULL;

/**
 * @struct VolumeGeometry
 * @brief Basic physical and logical layout of an NTFS volume.
 */
struct VolumeGeometry {
  uint32_t bytes_per_sector = 512;
  uint32_t bytes_per_cluster = 4096;
  uint32_t bytes_per_file_record = 1024;
  uint64_t mft_start_lcn = 0;
  uint64_t total_clusters = 0;
};

/**
 * @struct MftDiskExtent
 * @brief Represents a contiguous sequence of clusters on disk for $MFT $DATA.
 */
struct MftDiskExtent {
  uint64_t start_lcn = 0;
  uint64_t cluster_count = 0;
  uint64_t byte_offset = 0;
  uint64_t byte_length = 0;
};

/**
 * @struct ChunkBuffer
 * @brief Owns or views a sector-aligned buffer of raw bytes read from the volume.
 */
struct ChunkBuffer {
  char* data = nullptr;
  size_t size = 0;
  uint64_t stream_offset = 0;
  uint64_t first_record_number = 0;
  bool is_final = false;
};

/**
 * @struct ChunkSpan
 * @brief Non-owning view into a raw memory chunk for parsing.
 */
struct ChunkSpan {
  char* data = nullptr;
  size_t size = 0;
  uint64_t first_record_number = 0;
};

/**
 * @struct ReaderStats
 * @brief Telemetry metrics for chunk streaming throughput.
 */
struct ReaderStats {
  uint64_t total_bytes_read = 0;
  uint64_t total_chunks_read = 0;
  double elapsed_seconds = 0.0;
  double transfer_rate_mb_s = 0.0;
};

/**
 * @struct ParserStats
 * @brief Telemetry metrics for raw MFT record parsing.
 */
struct ParserStats {
  size_t records_parsed = 0;
  size_t base_records = 0;
  size_t extension_records = 0;
  size_t records_filtered = 0;
  size_t fixup_failures = 0;
  size_t parse_errors = 0;
};

}  // namespace mft_seams
