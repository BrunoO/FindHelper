#pragma once

#include <vector>

#include "index/mft/seams/MftTypes.h"

namespace mft_seams {

/**
 * @class IExtentProvider
 * @brief Seam 1: Volume geometry discovery and $MFT disk extent mapping.
 */
class IExtentProvider {
 public:
  virtual ~IExtentProvider() = default;

  /**
   * Discovers volume sector, cluster, and record geometry.
   * @param out_geometry Output geometry structure.
   * @return true on success, false otherwise.
   */
  [[nodiscard]] virtual bool GetVolumeGeometry(VolumeGeometry& out_geometry) = 0;

  /**
   * Resolves the disk extents (runs of contiguous clusters) comprising the $MFT.
   * @param out_extents Output list of extents.
   * @return true on success, false otherwise.
   */
  [[nodiscard]] virtual bool GetMftExtents(std::vector<MftDiskExtent>& out_extents) = 0;
};

}  // namespace mft_seams
