#pragma once

#include "path/PathStorage.h"
#include "search/FilenameTrigramExtractor.h"
#include "search/IndexBuildLifecycle.h"
#include "utils/HashMapAliases.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244)  // conversion from '__int64' to 'uint32_t', possible loss of data in CRoaring array.h
#endif
#include <roaring/roaring.hh>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#include <string_view>
#include <utility>
#include <vector>

class SearchThreadPool;

namespace filename_trigram {

/**
 * Move-only snapshot produced by FilenameTrigramIndex::BeginRebuild() and consumed
 * by FilenameTrigramIndex::SubmitBuild(). Keeping the generation and the postings
 * in one non-copyable value makes it impossible to pair a generation with the
 * postings extracted for a different rebuild.
 */
class TrigramBuildTicket {
public:
  TrigramBuildTicket(uint64_t generation, std::vector<uint64_t> postings)
      : generation_(generation), postings_(std::move(postings)) {}

  TrigramBuildTicket(const TrigramBuildTicket&) = delete;
  TrigramBuildTicket& operator=(const TrigramBuildTicket&) = delete;
  TrigramBuildTicket(TrigramBuildTicket&&) noexcept = default;
  TrigramBuildTicket& operator=(TrigramBuildTicket&&) noexcept = default;
  ~TrigramBuildTicket() = default;

  [[nodiscard]] uint64_t Generation() const noexcept { return generation_; }
  [[nodiscard]] std::vector<uint64_t> TakePostings() noexcept { return std::move(postings_); }

private:
  uint64_t generation_ = 0;
  std::vector<uint64_t> postings_;
};

class FilenameTrigramIndex : public index_build_lifecycle::IndexBuildLifecycle<FilenameTrigramIndex> {
public:
  FilenameTrigramIndex() = default;
  ~FilenameTrigramIndex();

  FilenameTrigramIndex(const FilenameTrigramIndex&) = delete;
  FilenameTrigramIndex& operator=(const FilenameTrigramIndex&) = delete;
  FilenameTrigramIndex(FilenameTrigramIndex&&) = delete;
  FilenameTrigramIndex& operator=(FilenameTrigramIndex&&) = delete;

  /**
   * Returns true if the trigram index is ready for queries.
   */
  [[nodiscard]] bool IsReady() const noexcept {
    return GetReadyFlag().load();
  }

  /**
   * Returns true if the trigram index is currently being rebuilt in the background.
   */
  [[nodiscard]] bool IsBuilding() const noexcept {
    return GetBuildingFlag().load();
  }

  /**
   * Aborts an in-flight build on exception or failure, clearing buffered mutations
   * and restoring is_building_ flag. Shared implementation in
   * IndexBuildLifecycle::AbortBuild.
   */
  void AbortBuild(uint64_t generation) noexcept;

  /**
   * Internal build-protocol hook for IndexBuildLifecycle::AbortBuild:
   * replays buffered mutations, clears the pending buffer, and reports
   * the outcome for flag updates and logging.
   *
   * @pre Caller must hold a unique lock on mutex_.
   */
  [[nodiscard]] index_build_lifecycle::AbortReplayOutcome ReplayMutationsForAbortLocked();

  /**
   * Phase 1 of an async rebuild. MUST be called while holding the SoA/index lock:
   * marks the index as building and snapshots the postings to extract.
   *
   * Pair with SubmitBuild(), which MUST be called after releasing that lock.
   *
   * Does not throw: if extraction fails to allocate, the index state is restored
   * here and an invalid ticket is returned. SubmitBuild() ignores such a ticket,
   * so the pair is still safe to make.
   *
   * Lifetime invariant: The FilenameTrigramIndex instance must outlive every
   * ticket produced here, and every task submitted to thread_pool.
   */
  [[nodiscard]] TrigramBuildTicket BeginRebuild(
      const PathStorage::SoAView& soa_view,
      size_t storage_size);

  /**
   * Phase 2 of an async rebuild. MUST be called after releasing the SoA/index lock:
   * waits for any in-flight build to retire, then offloads the snapshot to
   * thread_pool (or builds synchronously if thread_pool == nullptr).
   *
   * The wait is deliberately outside the index lock: holding it across the
   * wait would block every search for the duration of the previous build.
   */
  void SubmitBuild(
      TrigramBuildTicket ticket,
      const std::shared_ptr<SearchThreadPool>& thread_pool);

  /**
   * Runs both phases back to back. ONLY safe to call while holding the index
   * lock when thread_pool == nullptr: with a pool, SubmitBuild blocks on
   * build_future_.wait() and every search waiting on index.GetMutex() stalls
   * for the duration of the previous build. Prefer BeginRebuild()/SubmitBuild(),
   * which keeps the wait outside the lock.
   */
  void RebuildAsync(
      const PathStorage::SoAView& soa_view,
      size_t storage_size,
      const std::shared_ptr<SearchThreadPool>& thread_pool);

  /**
   * Phase 1 of async build: extracts packed (trigram, row_id) postings from SoAView.
   * Safe to call under index lock; does not mutate internal posting maps.
   */
  [[nodiscard]] std::vector<uint64_t> ExtractPostings(
      const PathStorage::SoAView& soa_view, size_t storage_size);

  /**
   * Phase 2 of async build: sorts postings and populates bitmaps off the index lock.
   * Atomically integrates pending mutations and flips IsReady to true upon completion.
   * Discards the build if generation does not match the active build_generation_.
   */
  void BuildFromPostings(uint64_t generation, std::vector<uint64_t> postings);

  /**
   * Build the index synchronously from a PathStorage SoAView.
   */
  void Build(const PathStorage::SoAView& soa_view, size_t storage_size);

  /**
   * Add a single entry to the index.
   */
  void AddEntry(uint32_t row_id, std::string_view filename);

  /**
   * Remove a single entry from the index.
   */
  void RemoveEntry(uint32_t row_id, std::string_view filename);

  /**
   * Clear all entries from the index and invalidate in-flight builds.
   */
  void Clear();

  /**
   * Query the index for candidates containing ALL specified trigrams.
   * Returns an empty optional if ANY trigram has 0 postings (early exit).
   * Returns an empty roaring bitmap if no intersection exists.
   */
  [[nodiscard]] std::optional<roaring::Roaring> QueryCandidates(
      const std::vector<TrigramKey>& required_trigrams) const;

  /**
   * Intersection cardinality without materializing the bitmap.
   *
   * Same early-exit contract as QueryCandidates (nullopt = unknown or
   * definitely empty), but avoids copying posting bitmaps: a single trigram
   * reads cardinality directly, two trigrams use and_cardinality. Used by
   * the cost-model gate, which needs only the count — the filter re-queries
   * the bitmap itself under its own lock scope.
   */
  [[nodiscard]] std::optional<uint64_t> IntersectionCardinality(
      const std::vector<TrigramKey>& required_trigrams) const;

  [[nodiscard]] size_t GetMemoryUsageBytes() const;

private:
  /**
   * Replays mutations buffered during an in-flight build onto posting_lists_.
   * Caller must hold a unique lock on mutex_. Mutations for the same trigram
   * are applied in chronological (buffer) order — add-then-remove and
   * remove-then-add for the same row_id yield different results, so the order
   * must not be shuffled.
   */
  void ReplayPendingMutationsLocked();

  struct PendingMutation {
    TrigramKey trigram;
    uint32_t row_id;
    bool is_add;
  };

  /**
   * Collect posting bitmaps for all required trigrams, sorted ascending by
   * cardinality. Returns false when any trigram has no postings (the
   * intersection will be empty). Shared prologue of QueryCandidates and
   * IntersectionCardinality. Caller must hold shared_lock on mutex_: the
   * returned pointers stay valid only under that lock.
   */
  [[nodiscard]] bool CollectSortedBitmaps(
      const std::vector<TrigramKey>& required_trigrams,
      std::vector<const roaring::Roaring*>& bitmaps) const;

  // Lifecycle state (mutex_, build_generation_, is_ready_, is_building_,
  // build_submit_mutex_, build_future_) lives in IndexBuildLifecycle.
  flat_hash_map_t<TrigramKey, roaring::Roaring> posting_lists_;
  std::vector<PendingMutation> pending_mutations_;
};

} // namespace filename_trigram
