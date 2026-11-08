#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "search/SearchTypes.h"

// Bundles the search result path pool, the result vector that depends on it,
// and the batch-change counter. The only safe way to clear both is through
// Clear(), which always clears results before pool — eliminating the
// pool-lifecycle bug where pool was cleared while results held string_views into it.
struct ResultPoolOwner {
  // Clear results_ first (safe — no dangling views), then pool_, then bump batch_number_.
  // PERF NOTE (future): unconditional shrink_to_fit keeps memory bounded after
  // large result sets (e.g. 463k-hit 'e' query) but forces a full regrow on the
  // next large search. Retain-by-default (clear only) + conditional trim
  // (shrink only when bloated, e.g. size < cap/4 or cap > 64MB) would trade a
  // bounded high-water mark for faster repeat searches. Needs a microbenchmark
  // (clear->refill cycles, alternating large/small) before changing.
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

  // Commit a freshly built result set + path pool in one call: moves both in,
  // reclaims excess results capacity (never the pool — every committed
  // SearchResult.fullPath views into it, so any reallocation is a
  // use-after-free), then bumps batch_number_ so CheckBatchNumber consumers
  // drop stale views. The only sanctioned way to replace the live set.
  void CommitResults(std::vector<SearchResult>&& results, std::vector<char>&& pool) {
    const size_t prev_results_cap = results_.capacity();
    results_ = std::move(results);
    pool_ = std::move(pool);
    // Shrink results only: SearchResult.fullPath views point into Pool(), not
    // into the results vector, so moving SearchResult objects is harmless.
    if (results_.size() < prev_results_cap / 2) {
      results_.shrink_to_fit();
    }
    ++batch_number_;
  }

  // Release the pool out of a local back buffer (see SearchController
  // double-buffered PollResults): moves the bytes out, leaving an empty pool.
  [[nodiscard]] std::vector<char> ReleasePool() {
    std::vector<char> released = std::move(pool_);
    pool_.clear();  // Moved-from state is valid but unspecified; leave it empty.
    return released;
  }

  [[nodiscard]] const std::vector<SearchResult>& Results() const { return results_; }
  // Mutable row access for in-place edits that never touch the pool (sort,
  // display-string formatting, folder-stat writes). Replacing the whole set
  // must go through CommitResults(), not this accessor.
  [[nodiscard]] std::vector<SearchResult>& Results() { return results_; }
  [[nodiscard]] const std::vector<char>& Pool() const { return pool_; }
  // Mutable pool access for back-buffer construction only
  // (MergeAndConvertToSearchResults fills a local ResultPoolOwner before
  // commit). Named explicitly so live-state pool writes stand out in review;
  // there is intentionally no plain mutable Pool().
  [[nodiscard]] std::vector<char>& MutablePoolForBuild() { return pool_; }
  [[nodiscard]] uint64_t BatchNumber() const { return batch_number_; }

 private:
  // NOLINTNEXTLINE(readability-identifier-naming)
  std::vector<SearchResult> results_;
  // NOLINTNEXTLINE(readability-identifier-naming)
  std::vector<char> pool_;
  // NOLINTNEXTLINE(readability-identifier-naming)
  uint64_t batch_number_ = 0;
};
