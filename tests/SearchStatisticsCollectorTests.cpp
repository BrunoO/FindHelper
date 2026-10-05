#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "path/PathStorage.h"
#include "search/SearchStatisticsCollector.h"

namespace {

// Build a SoAView backed by local vectors.
// path_offsets encodes where each path string starts in the storage buffer.
// storage_size is the total byte length of the path storage buffer.
PathStorage::SoAView MakeSoaView(const std::vector<size_t>& offsets, size_t storage_size,
                                 size_t& size_out, const char* storage = nullptr) {
    PathStorage::SoAView view;
    view.path_offsets = offsets.empty() ? nullptr : offsets.data();
    view.path_storage = storage;
    view.size = offsets.size();
    size_out = storage_size;
    return view;
}

}  // namespace

TEST_SUITE("SearchStatisticsCollector::CalculateChunkBytes") {

  TEST_CASE("empty SoAView returns 0") {
    PathStorage::SoAView view;  // size == 0, all pointers null
    CHECK(SearchStatisticsCollector::CalculateChunkBytes(view, 0, 0, 0) == 0);
    CHECK(SearchStatisticsCollector::CalculateChunkBytes(view, 0, 10, 100) == 0);
  }

  TEST_CASE("start equals end returns 0") {
    const std::vector<size_t> offsets = {0, 10, 25};
    size_t storage_size = 0;
    const PathStorage::SoAView view = MakeSoaView(offsets, 40, storage_size);
    CHECK(SearchStatisticsCollector::CalculateChunkBytes(view, 1, 1, 40) == 0);
  }

  TEST_CASE("start past end returns 0") {
    const std::vector<size_t> offsets = {0, 10, 25};
    size_t storage_size = 0;
    const PathStorage::SoAView view = MakeSoaView(offsets, 40, storage_size);
    CHECK(SearchStatisticsCollector::CalculateChunkBytes(view, 5, 2, 40) == 0);
  }

  TEST_CASE("single entry — last item uses storage_size") {
    // One entry at offset 0; storage is 10 bytes total.
    // bytes = storage_size - offset[0] - 1 = 10 - 0 - 1 = 9
    const std::vector<size_t> offsets = {0};
    size_t storage_size = 0;
    const PathStorage::SoAView view = MakeSoaView(offsets, 10, storage_size);
    CHECK(SearchStatisticsCollector::CalculateChunkBytes(view, 0, 1, 10) == 9);
  }

  TEST_CASE("multiple entries — inter-entry gaps computed correctly") {
    // offsets: [0, 5, 12, 20], storage_size: 30
    // entry 0: 5 - 0 - 1 = 4
    // entry 1: 12 - 5 - 1 = 6
    // entry 2: 20 - 12 - 1 = 7
    // entry 3 (last): 30 - 20 - 1 = 9
    // total [0,4) = 26
    const std::vector<size_t> offsets = {0, 5, 12, 20};
    size_t storage_size = 0;
    const PathStorage::SoAView view = MakeSoaView(offsets, 30, storage_size);
    CHECK(SearchStatisticsCollector::CalculateChunkBytes(view, 0, 4, 30) == 26);
  }

  TEST_CASE("partial range — first half only") {
    // Same offsets as above; range [0, 2) → 4 + 6 = 10
    const std::vector<size_t> offsets = {0, 5, 12, 20};
    size_t storage_size = 0;
    const PathStorage::SoAView view = MakeSoaView(offsets, 30, storage_size);
    CHECK(SearchStatisticsCollector::CalculateChunkBytes(view, 0, 2, 30) == 10);
  }

  TEST_CASE("partial range — last half only") {
    // range [2, 4) → 7 + 9 = 16
    const std::vector<size_t> offsets = {0, 5, 12, 20};
    size_t storage_size = 0;
    const PathStorage::SoAView view = MakeSoaView(offsets, 30, storage_size);
    CHECK(SearchStatisticsCollector::CalculateChunkBytes(view, 2, 4, 30) == 16);
  }

  TEST_CASE("end_index beyond SoAView size is clamped to size") {
    // end_index = 10, size = 4 → loop clamps at size
    const std::vector<size_t> offsets = {0, 5, 12, 20};
    size_t storage_size = 0;
    const PathStorage::SoAView view = MakeSoaView(offsets, 30, storage_size);
    CHECK(SearchStatisticsCollector::CalculateChunkBytes(view, 0, 10, 30) == 26);
  }

}
