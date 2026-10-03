/**
 * @file SearchStatisticsCollector.cpp
 * @brief Implementation of SearchStatisticsCollector
 */

#include "search/SearchStatisticsCollector.h"

size_t SearchStatisticsCollector::CalculateChunkBytes(
    const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - matches header, mirrors SoAView
    size_t start_index_,  // NOLINT(readability-identifier-naming)
    size_t end_index_,  // NOLINT(readability-identifier-naming)
    size_t storage_size) {
  if (soaView.size == 0) {
    return 0;
  }

  size_t total_bytes = 0;
  for (size_t i = start_index_; i < end_index_ && i < soaView.size; ++i) {
    if (i + 1 < soaView.size) {
      total_bytes += soaView.path_offsets[i + 1] - soaView.path_offsets[i] - 1;
    } else {
      // Last entry: calculate from offset to end of storage
      if (soaView.path_offsets[i] < storage_size) {
        total_bytes += storage_size - soaView.path_offsets[i] - 1;
      }
    }
  }
  return total_bytes;
}

// RecordThreadTiming is inline in header


