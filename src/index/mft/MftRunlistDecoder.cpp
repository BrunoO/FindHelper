#include "index/mft/MftRunlistDecoder.h"

namespace mft {

namespace {

inline uint64_t ReadClusterCount(const uint8_t* bytes, uint8_t count) {
  uint64_t count_val = 0;
  for (uint8_t i = 0; i < count; ++i) {
    count_val |= static_cast<uint64_t>(bytes[i]) << (i * 8U);
  }
  return count_val;
}

inline int64_t ReadLcnDelta(const uint8_t* bytes, uint8_t count) {
  if (count == 0) {
    return 0;
  }
  uint64_t raw_val = 0;
  for (uint8_t i = 0; i < count; ++i) {
    raw_val |= static_cast<uint64_t>(bytes[i]) << (i * 8U);
  }
  if (count < 8U) {
    const uint64_t sign_bit = 1ULL << ((count * 8U) - 1U);
    if ((raw_val & sign_bit) != 0U) {
      const uint64_t mask = (1ULL << (count * 8U)) - 1ULL;
      raw_val |= ~mask;
    }
  }
  return static_cast<int64_t>(raw_val);
}

inline void AddOrMergeExtent(
    std::vector<mft_seams::MftDiskExtent>& out_extents,
    uint64_t start_lcn,
    uint64_t cluster_count,
    uint32_t bytes_per_cluster) {
  const uint64_t byte_offset = start_lcn * bytes_per_cluster;
  const uint64_t byte_length = cluster_count * bytes_per_cluster;

  if (!out_extents.empty()) {
    auto& prev = out_extents.back();
    if (prev.start_lcn + prev.cluster_count == start_lcn) {
      prev.cluster_count += cluster_count;
      prev.byte_length += byte_length;
      return;
    }
  }

  out_extents.push_back({start_lcn, cluster_count, byte_offset, byte_length});
}

}  // namespace

bool MftRunlistDecoder::DecodeRunlist(
    const uint8_t* runlist_bytes,
    size_t max_bytes,
    int64_t initial_lcn,
    uint32_t bytes_per_cluster,
    std::vector<mft_seams::MftDiskExtent>& out_extents,
    size_t* out_bytes_consumed) {
  if (runlist_bytes == nullptr || max_bytes == 0 || bytes_per_cluster == 0) {
    return false;
  }

  size_t offset = 0;
  int64_t current_lcn = initial_lcn;

  while (offset < max_bytes) {
    const uint8_t header = runlist_bytes[offset++];
    if (header == 0) {
      if (out_bytes_consumed != nullptr) {
        *out_bytes_consumed = offset;
      }
      return true;
    }

    const uint32_t header_u32 = header;
    const auto length_bytes = static_cast<uint8_t>(header_u32 & 0x0FU);
    const auto offset_bytes = static_cast<uint8_t>((header_u32 >> 4U) & 0x0FU);

    if (length_bytes == 0 || length_bytes > 8 || offset_bytes > 8) {
      return false;
    }

    if (offset + length_bytes + offset_bytes > max_bytes) {
      return false;
    }

    const uint64_t cluster_count = ReadClusterCount(&runlist_bytes[offset], length_bytes);
    offset += length_bytes;

    if (cluster_count == 0) {
      return false;
    }

    if (offset_bytes == 0) {
      continue;
    }

    const int64_t lcn_delta = ReadLcnDelta(&runlist_bytes[offset], offset_bytes);
    offset += offset_bytes;

    current_lcn += lcn_delta;
    if (current_lcn < 0) {
      return false;
    }

    const auto start_lcn = static_cast<uint64_t>(current_lcn);
    AddOrMergeExtent(out_extents, start_lcn, cluster_count, bytes_per_cluster);
  }

  return false;
}

}  // namespace mft
