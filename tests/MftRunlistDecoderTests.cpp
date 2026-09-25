#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <vector>

#include "index/mft/MftRunlistDecoder.h"
#include "index/mft/seams/MftTypes.h"

namespace {

using mft::MftRunlistDecoder;
using mft_seams::MftDiskExtent;

TEST_CASE("MftRunlistDecoder: Single positive extent") {
  // Run: length 20 clusters (0x14), starting LCN 0x012345
  // Header: 0x31 -> length 1 byte (0x14), offset 3 bytes (0x45, 0x23, 0x01)
  // Followed by 0x00 (terminator)
  const std::vector<uint8_t> runlist = {0x31, 0x14, 0x45, 0x23, 0x01, 0x00};
  constexpr uint32_t kClusterSize = 4096;

  std::vector<MftDiskExtent> extents;
  size_t bytes_consumed = 0;
  REQUIRE(MftRunlistDecoder::DecodeRunlist(
      runlist.data(), runlist.size(), 0, kClusterSize, extents, &bytes_consumed));

  REQUIRE(extents.size() == 1);
  CHECK(extents[0].start_lcn == 0x012345);
  CHECK(extents[0].cluster_count == 20);
  CHECK(extents[0].byte_offset == 0x012345 * 4096ULL);
  CHECK(extents[0].byte_length == 20 * 4096ULL);
  CHECK(bytes_consumed == 6);
}

TEST_CASE("MftRunlistDecoder: Multiple extents with contiguous merge") {
  // Extent 1: LCN 100, length 10 -> [100, 110)
  // Extent 2: LCN 110 (delta +10), length 20 -> [110, 130) -> Should MERGE into [100, 130)
  // Extent 3: LCN 200 (delta +90), length 5 -> [200, 205) -> Non-contiguous
  // Run 1: header 0x11, len=10 (0x0A), delta=100 (0x64)
  // Run 2: header 0x11, len=20 (0x14), delta=10 (0x0A)
  // Run 3: header 0x11, len=5 (0x05), delta=90 (0x5A)
  // Terminator: 0x00
  const std::vector<uint8_t> runlist = {
      0x11, 0x0A, 0x64,
      0x11, 0x14, 0x0A,
      0x11, 0x05, 0x5A,
      0x00,
  };
  constexpr uint32_t kClusterSize = 4096;

  std::vector<MftDiskExtent> extents;
  REQUIRE(MftRunlistDecoder::DecodeRunlist(
      runlist.data(), runlist.size(), 0, kClusterSize, extents));

  REQUIRE(extents.size() == 2);
  // Merged extent 1 + 2
  CHECK(extents[0].start_lcn == 100);
  CHECK(extents[0].cluster_count == 30);
  CHECK(extents[0].byte_length == 30 * 4096ULL);

  // Extent 3
  CHECK(extents[1].start_lcn == 200);
  CHECK(extents[1].cluster_count == 5);
  CHECK(extents[1].byte_length == 5 * 4096ULL);
}

TEST_CASE("MftRunlistDecoder: Negative LCN delta") {
  // Run 1: LCN 500, len 50 -> [500, 550)
  // Run 2: LCN delta -100 (0xFF9C in 2 bytes), len 20 -> LCN 400
  // Run 1: header 0x21, len=50 (0x32), delta=500 (0x01F4 -> 0xF4, 0x01)
  // Run 2: header 0x21, len=20 (0x14), delta=-100 (-100 = 0xFF9C -> 0x9C, 0xFF)
  // Terminator: 0x00
  const std::vector<uint8_t> runlist = {
      0x21, 0x32, 0xF4, 0x01,
      0x21, 0x14, 0x9C, 0xFF,
      0x00,
  };
  constexpr uint32_t kClusterSize = 4096;

  std::vector<MftDiskExtent> extents;
  REQUIRE(MftRunlistDecoder::DecodeRunlist(
      runlist.data(), runlist.size(), 0, kClusterSize, extents));

  REQUIRE(extents.size() == 2);
  CHECK(extents[0].start_lcn == 500);
  CHECK(extents[0].cluster_count == 50);

  CHECK(extents[1].start_lcn == 400);
  CHECK(extents[1].cluster_count == 20);
}

TEST_CASE("MftRunlistDecoder: Sparse run is skipped") {
  // Run 1: LCN 100, len 10
  // Run 2: Sparse (offset_bytes = 0), len 50
  // Run 3: LCN delta +50, len 20 -> LCN 150
  // Run 1: 0x11, 0x0A, 0x64
  // Run 2: 0x01, 0x32 (sparse)
  // Run 3: 0x11, 0x14, 0x32
  // Terminator: 0x00
  const std::vector<uint8_t> runlist = {
      0x11, 0x0A, 0x64,
      0x01, 0x32,
      0x11, 0x14, 0x32,
      0x00,
  };
  constexpr uint32_t kClusterSize = 4096;

  std::vector<MftDiskExtent> extents;
  REQUIRE(MftRunlistDecoder::DecodeRunlist(
      runlist.data(), runlist.size(), 0, kClusterSize, extents));

  REQUIRE(extents.size() == 2);
  CHECK(extents[0].start_lcn == 100);
  CHECK(extents[0].cluster_count == 10);

  CHECK(extents[1].start_lcn == 150);
  CHECK(extents[1].cluster_count == 20);
}

TEST_CASE("MftRunlistDecoder: Error on invalid / truncated input") {
  constexpr uint32_t kClusterSize = 4096;
  std::vector<MftDiskExtent> extents;

  // Null buffer
  CHECK_FALSE(MftRunlistDecoder::DecodeRunlist(nullptr, 10, 0, kClusterSize, extents));

  // Zero size
  const std::vector<uint8_t> dummy = {0x00};
  CHECK_FALSE(MftRunlistDecoder::DecodeRunlist(dummy.data(), 0, 0, kClusterSize, extents));

  // Unterminated / truncated buffer
  const std::vector<uint8_t> truncated = {0x21, 0x32, 0xF4};  // missing 2nd offset byte
  CHECK_FALSE(MftRunlistDecoder::DecodeRunlist(truncated.data(), truncated.size(), 0, kClusterSize, extents));

  // Zero cluster length
  const std::vector<uint8_t> zero_len = {0x11, 0x00, 0x10, 0x00};
  CHECK_FALSE(MftRunlistDecoder::DecodeRunlist(zero_len.data(), zero_len.size(), 0, kClusterSize, extents));
}

}  // namespace
