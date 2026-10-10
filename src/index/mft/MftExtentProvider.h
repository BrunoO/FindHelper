#pragma once

#include <cstdint>
#include <vector>

#include "index/mft/seams/IExtentProvider.h"
#include "index/mft/seams/MftTypes.h"

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only TU; SDK system header
#include "usn/VolumeGateway.h"
#endif  // _WIN32

namespace mft {

/**
 * @class MftExtentProvider
 * @brief Production extent provider implementing seam 1.
 *
 * Queries volume geometry and decodes $MFT non-resident runlist extents.
 */
class MftExtentProvider : public mft_seams::IExtentProvider {
 public:
#ifdef _WIN32
  explicit MftExtentProvider(HANDLE volume, volume_gateway::VolumeGateway& gateway);
#else
  MftExtentProvider();
#endif  // _WIN32

  [[nodiscard]] bool GetVolumeGeometry(mft_seams::VolumeGeometry& out_geometry) override;
  [[nodiscard]] bool GetMftExtents(std::vector<mft_seams::MftDiskExtent>& out_extents) override;

  // VCN-space completeness predicate (pure, cross-platform for tests): the base
  // $DATA runlist covers VCNs [lowest_vcn, highest_vcn]; VCN space is dense
  // even with sparse runs, so coverage must span the whole stream (RealSize).
  // Byte sums cannot prove this (the decoder skips sparse runs).
  [[nodiscard]] static bool BaseDataRunCoversStream(int64_t lowest_vcn, int64_t highest_vcn,
                                                    uint64_t data_real_size,
                                                    uint32_t bytes_per_cluster) {
    if (bytes_per_cluster == 0U || lowest_vcn != 0 || highest_vcn < 0) {
      return false;
    }
    const uint64_t total_clusters =
        (data_real_size + bytes_per_cluster - 1U) / bytes_per_cluster;
    return static_cast<uint64_t>(highest_vcn) + 1U >= total_clusters;
  }

 private:
  // Cached geometry: GetMftExtents already queries it, so a second
  // FSCTL_GET_NTFS_VOLUME_DATA per run is redundant (immutable for the run).
  bool EnsureGeometry(mft_seams::VolumeGeometry& out_geometry);
  mft_seams::VolumeGeometry cached_geometry_{};
  bool geometry_cached_ = false;

#ifdef _WIN32
 private:
  HANDLE volume_ = INVALID_HANDLE_VALUE;
  volume_gateway::VolumeGateway* gateway_ = nullptr;
#endif  // _WIN32
};

}  // namespace mft
