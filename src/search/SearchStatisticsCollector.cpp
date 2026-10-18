/**
 * @file SearchStatisticsCollector.cpp
 * @brief Implementation of SearchStatisticsCollector
 */

#include "search/SearchStatisticsCollector.h"

#include <algorithm>

namespace search_statistics {

size_t CalculateChunkBytes(
    const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - matches header, mirrors SoAView
    size_t start_index_,  // NOLINT(readability-identifier-naming)
    size_t end_index_,  // NOLINT(readability-identifier-naming)
    size_t storage_size) {
  if (soaView.size == 0 || soaView.path_offsets == nullptr ||
      start_index_ >= end_index_ || start_index_ >= soaView.size) {
    return 0;
  }

  const size_t clamped_end = (std::min)(end_index_, soaView.size);
  const size_t start_offset = soaView.path_offsets[start_index_];
  const size_t end_offset =
      (clamped_end < soaView.size) ? soaView.path_offsets[clamped_end] : storage_size;
  const size_t item_count = clamped_end - start_index_;

  if (end_offset < start_offset + item_count) {
    return 0;
  }
  return end_offset - start_offset - item_count;
}

// RecordThreadTiming is inline in header

}  // namespace search_statistics


