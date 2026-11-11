#pragma once

#include "path/PathStorage.h"
#include "search/FilenameTrigramExtractor.h"  // kLow16BitsMask
#include "utils/StringSearch.h"               // string_search::ToLowerChar
#include <cstddef>
#include <cstdint>
#include <vector>

namespace filename_trigram {

// Upper safety cap for the trigram postings *reservation* on pathological
// corpora: 32,000,000 postings ≈ 256 MB transient allocation, covering up to
// ~2,000,000 files without vector reallocations.
//
// Allocation hint only: reserve() cannot cap vector growth, so a corpus past
// ~2M files still grows the postings vector to its true size (8 bytes per
// posting). The cap avoids one giant upfront allocation, not the actual peak.
//
// Single source of truth (tech-debt T3): the filename-trigram index and the
// combined extractor both reserve against this same bound.
inline constexpr size_t kMaxPostingReservation = 32'000'000;

/**
 * Exact mathematical upper bound of trigram postings across all live rows: for
 * a filename of length L the trigram count is max(0, L - 2). Used to pre-reserve
 * the postings vector, clamped to kMaxPostingReservation.
 *
 * Shared by the filename-trigram index and the combined extractor (tech-debt T3)
 * so the two cannot drift.
 */
[[nodiscard]] inline size_t ComputeMaxTrigramPostings(const PathStorage::SoAView& soa_view,
                                                     size_t storage_size) noexcept {
  size_t max_postings = 0;
  for (size_t i = 0; i < soa_view.size; ++i) {
    if (soa_view.is_deleted[i] != 0) {
      continue;
    }
    const size_t fn_start = soa_view.filename_start[i];
    if (const size_t path_len = soa_view.GetPathLength(i, storage_size);
        fn_start + 3 <= path_len) {
      max_postings += (path_len - fn_start - 2);
    }
  }
  return max_postings;
}

/**
 * Appends this row's packed (trigram << 32 | row_id) postings to `out`,
 * lowercasing ASCII as it packs. The filename-trigram index and the combined
 * extractor must produce byte-identical postings, so both call this one loop
 * (tech-debt T3; pinned by the "combined extractor trigram half matches
 * ExtractPostings" differential test).
 *
 * No-op when filename_length < 3 (no trigram fits).
 */
inline void AppendFilenameTrigramPostings(const char* filename,
                                         size_t filename_length,
                                         uint32_t row_id,
                                         std::vector<uint64_t>& out) {
  if (filename_length < 3) {
    return;
  }
  uint32_t key = (static_cast<uint32_t>(static_cast<unsigned char>(
                      string_search::ToLowerChar(filename[0]))) << 8U) |
                 static_cast<uint32_t>(static_cast<unsigned char>(
                      string_search::ToLowerChar(filename[1])));
  for (size_t j = 2; j < filename_length; ++j) {
    const auto c = static_cast<uint32_t>(static_cast<unsigned char>(
        string_search::ToLowerChar(filename[j])));
    key = ((key & kLow16BitsMask) << 8U) | c;
    out.push_back((static_cast<uint64_t>(key) << 32U) | row_id);
  }
}

}  // namespace filename_trigram
