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

#ifdef _WIN32
 private:
  HANDLE volume_ = INVALID_HANDLE_VALUE;
  volume_gateway::VolumeGateway* gateway_ = nullptr;
#endif  // _WIN32
};

}  // namespace mft
