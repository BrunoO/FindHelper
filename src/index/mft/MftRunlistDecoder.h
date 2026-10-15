#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "index/mft/seams/MftTypes.h"

namespace mft {

// Closed failure set for runlist decoding. Truncation is distinguished from
// corruption so callers can request more bytes instead of discarding extents.
enum class RunlistError : uint8_t {
  kNone,            // Decoded cleanly (terminator reached).
  kInvalidArgument,  // Null input, zero length, or zero cluster size.
  kBadHeader,       // Malformed mapping-pair header or impossible LCN.
  kTruncated,       // Buffer overrun or stream ended without terminator.
};

// Result of DecodeRunlist: bytes consumed on success, error otherwise.
// [[nodiscard]] forces handling; replaces bool + nullable size_t* out-param
// (whose forgotten null check was a crash risk and whose false conflated
// every failure mode).
struct DecodeRunlistResult {
  size_t bytes_consumed = 0;
  RunlistError error = RunlistError::kNone;

  [[nodiscard]] bool ok() const { return error == RunlistError::kNone; }
};

/**
 * @class MftRunlistDecoder
 * @brief Decodes NTFS compressed mapping pairs (runlists) into contiguous disk extents.
 */
class MftRunlistDecoder {
 public:
  /**
   * Decodes a raw NTFS runlist byte stream into a list of MftDiskExtent structures.
   * Merges contiguous clusters automatically.
   *
   * @param runlist_bytes Pointer to the beginning of the runlist mapping pairs.
   * @param max_bytes Maximum available bytes in the buffer to prevent buffer overruns.
   * @param initial_lcn Starting reference LCN (normally 0 for the first fragment).
   * @param bytes_per_cluster Cluster size in bytes.
   * @param out_extents Output vector populated with decoded disk extents.
   * @return Result with bytes consumed on success, specific error otherwise.
   */
  [[nodiscard]] static DecodeRunlistResult DecodeRunlist(
      const uint8_t* runlist_bytes,
      size_t max_bytes,
      int64_t initial_lcn,
      uint32_t bytes_per_cluster,
      std::vector<mft_seams::MftDiskExtent>& out_extents);
};

}  // namespace mft
