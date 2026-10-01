#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "index/mft/seams/MftTypes.h"

namespace mft {

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
   * @param out_bytes_consumed Optional output receiving the total bytes of runlist consumed.
   * @return true if decoding succeeded cleanly, false on malformed data or buffer overrun.
   */
  [[nodiscard]] static bool DecodeRunlist(
      const uint8_t* runlist_bytes,
      size_t max_bytes,
      int64_t initial_lcn,
      uint32_t bytes_per_cluster,
      std::vector<mft_seams::MftDiskExtent>& out_extents,
      size_t* out_bytes_consumed = nullptr);
};

}  // namespace mft
