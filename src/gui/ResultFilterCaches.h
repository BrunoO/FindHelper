#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "filters/SizeFilter.h"
#include "filters/TimeFilter.h"
#include "search/SearchTypes.h"

// Monotonic version of the search-result set (god-object Phase 3).
// Bumped whenever the result content changes (new batch applied, cloud
// attrinas complete, sort invalidation). Filter caches capture it when they
// rebuild; validity requires the version to still match.
using ResultsVersion = uint64_t;

// One filter cache slice. Writers: SearchResultUtils cache-update helpers only.
// snake_case POD public members (same rule as other GuiState substates).
struct TimeFilterCacheSlice {
  std::vector<SearchResult> results;
  size_t count = 0;
  TimeFilter cached_filter = TimeFilter::None;
  bool valid = false;
  ResultsVersion captured_version = 0;

  // Validity: stored AND the results version has not changed since.
  [[nodiscard]] bool IsValidFor(ResultsVersion version) const {
    return valid && captured_version == version;
  }
  // Record a (re)build against the current results version.
  void MarkStoredVersion(ResultsVersion version) {
    valid = true;
    captured_version = version;
  }
};

struct SizeFilterCacheSlice {
  std::vector<SearchResult> results;
  size_t count = 0;
  SizeFilter cached_filter = SizeFilter::None;
  bool valid = false;
  ResultsVersion captured_version = 0;

  // Validity / store contract identical to TimeFilterCacheSlice.
  [[nodiscard]] bool IsValidFor(ResultsVersion version) const {
    return valid && captured_version == version;
  }
  // Record a (re)build against the current results version.
  void MarkStoredVersion(ResultsVersion version) {
    valid = true;
    captured_version = version;
  }
};

// Progressive "total size of displayed results" accumulator + valid flag.
struct TotalSizeProgressState {
  uint64_t bytes = 0;         // Sum of file sizes of the displayed set
  bool valid = false;         // False ⇒ computation is (re)starting
  size_t computation_index = 0; // Next display-result index to scan
  uint64_t computation_bytes = 0; // Partial sum accumulated so far

  void ResetProgress() {
    computation_index = 0;
    computation_bytes = 0;
  }
  // Invalidate: mark the sum stale and clear partial progress.
  void Invalidate() {
    valid = false;
    ResetProgress();
  }
};

// Bundles time-filter / size-filter caches and the displayed-total-size
// progressive state (Phase 3: replaces four independent *_valid bools; a slice
// is stale when its captured_version no longer matches GetResultsVersion()).
struct ResultFilterCaches {
  TimeFilterCacheSlice time;
  SizeFilterCacheSlice size;
  TotalSizeProgressState total_size;
};
