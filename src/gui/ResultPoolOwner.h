#pragma once

#include <cstdint>
#include <vector>

#include "search/SearchTypes.h"

// Bundles the search result path pool, the result vector that depends on it,
// and the batch-change counter. The only safe way to clear both is through
// Clear(), which always clears results before pool — eliminating the
// pool-lifecycle bug where pool was cleared while results held string_views into it.
struct ResultPoolOwner {
  // Clear results_ first (safe — no dangling views), then pool_, then bump batch_number_.
  void Clear() {
    results_.clear();
    results_.shrink_to_fit();
    pool_.clear();
    pool_.shrink_to_fit();
    ++batch_number_;
  }

  // Increment batch_number_ without clearing results or pool (invalidates consumers that
  // compare batch numbers, e.g. after in-place result replacement). See SearchController.
  void BumpBatchNumber() { ++batch_number_; }

  [[nodiscard]] const std::vector<SearchResult>& Results() const { return results_; }
  [[nodiscard]] std::vector<SearchResult>& Results() { return results_; }
  [[nodiscard]] const std::vector<char>& Pool() const { return pool_; }
  // Mutable pool access for MergeAndConvertToSearchResults and path-pool growth on apply.
  [[nodiscard]] std::vector<char>& Pool() { return pool_; }
  [[nodiscard]] uint64_t BatchNumber() const { return batch_number_; }

 private:
  // NOLINTNEXTLINE(readability-identifier-naming)
  std::vector<SearchResult> results_;
  // NOLINTNEXTLINE(readability-identifier-naming)
  std::vector<char> pool_;
  // NOLINTNEXTLINE(readability-identifier-naming)
  uint64_t batch_number_ = 0;
};
