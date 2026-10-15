#pragma once

#include <vector>

#include "index/mft/seams/IExtentProvider.h"

namespace mft_seams {

/**
 * @class FakeExtentProvider
 * @brief Scripted fake implementation of IExtentProvider for testing.
 */
class FakeExtentProvider : public IExtentProvider {
 public:
  FakeExtentProvider() = default;

  explicit FakeExtentProvider(VolumeGeometry geometry, std::vector<MftDiskExtent> extents)
      : geometry_(geometry), extents_(std::move(extents)) {}

  void SetGeometry(const VolumeGeometry& geometry) { geometry_ = geometry; }
  void SetExtents(std::vector<MftDiskExtent> extents) { extents_ = std::move(extents); }
  void SetFailGeometry(bool fail) { fail_geometry_ = fail; }
  void SetFailExtents(bool fail) { fail_extents_ = fail; }

  [[nodiscard]] bool GetVolumeGeometry(VolumeGeometry& out_geometry) override {
    if (fail_geometry_) {
      return false;
    }
    out_geometry = geometry_;
    return true;
  }

  [[nodiscard]] bool GetMftExtents(std::vector<MftDiskExtent>& out_extents) override {
    if (fail_extents_) {
      return false;
    }
    out_extents = extents_;
    return true;
  }

 private:
  VolumeGeometry geometry_{};
  std::vector<MftDiskExtent> extents_;
  bool fail_geometry_ = false;
  bool fail_extents_ = false;
};

}  // namespace mft_seams
