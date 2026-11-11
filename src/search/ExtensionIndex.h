#pragma once

#include "path/PathStorage.h"
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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class ThreadPool;

namespace extension_index {

/**
 * One normalized extension with its member row-ids in ascending order.
 * Produced by extraction (grouped during the single SoA pass: one owning
 * string per distinct extension) and consumed by BuildFromPostings.
 */
struct ExtensionGroup {
  std::string extension{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
  std::vector<uint32_t> row_ids{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
};

using ExtensionBuildTicket =
    index_build_lifecycle::IndexBuildTicket<std::vector<ExtensionGroup>>;

class ExtensionIndex : public index_build_lifecycle::IndexBuildLifecycle<ExtensionIndex> {
public:
  ExtensionIndex() = default;
  ~ExtensionIndex();

  ExtensionIndex(const ExtensionIndex&) = delete;
  ExtensionIndex& operator=(const ExtensionIndex&) = delete;
  ExtensionIndex(ExtensionIndex&&) = delete;
  ExtensionIndex& operator=(ExtensionIndex&&) = delete;

  /**
   * Returns true if the extension index is ready for queries.
   */
  [[nodiscard]] bool IsReady() const noexcept {
    return GetReadyFlag().load();
  }

  /**
   * Returns true if the extension index is currently being rebuilt in the background.
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
   * Commit-path hook for IndexBuildLifecycle::CommitBuildIfFresh: replaces the
   * live posting map with the freshly built one and returns the installed size
   * (for the build summary log).
   *
   * @pre Caller holds a unique lock on mutex_ — the same scope as the
   *   stale-generation guard, so a stale map can never be installed.
   */
  [[nodiscard]] size_t InstallPostingsLocked(
      flat_hash_map_t<std::string, roaring::Roaring>&& new_postings);

  /**
   * Phase 2 of an async rebuild. MUST be called after releasing the caller's
   * index lock: waits for any in-flight build to retire, then offloads the
   * snapshot to thread_pool (or builds synchronously if thread_pool == nullptr).
   *
   * The wait is deliberately outside the index lock: holding it across the
   * wait would block every search for the duration of the previous build.
   */
  void SubmitBuild(
      ExtensionBuildTicket ticket,
      const std::shared_ptr<ThreadPool>& thread_pool);

  /**
   * Runs mark + extract + submit back to back. Standalone convenience only
   * (tests, Build()): performs its own extension-only extraction. Wired
   * call sites (maintenance, full recompute) MUST NOT use this — they run
   * one combined extraction for both indexes and call each index's
   * MarkBuilding + ExtensionBuildTicket + SubmitBuild manually.
   *
   * ONLY safe to call while holding the index lock when
   * thread_pool == nullptr: with a pool, SubmitBuild blocks on
   * build_future_.wait() and every search waiting on the index lock stalls
   * for the duration of the previous build.
   */
  void RebuildAsync(
      const PathStorage::SoAView& soa_view,
      size_t storage_size,
      const std::shared_ptr<ThreadPool>& thread_pool);

  /**
   * Phase 2 of async build: sorts groups and populates bitmaps off the index
   * lock. Atomically integrates pending mutations and flips IsReady to true
   * upon completion. Discards the build if generation does not match the
   * active build_generation_.
   */
  void BuildFromPostings(uint64_t generation, std::vector<ExtensionGroup> groups);

  /**
   * Build the index synchronously from a PathStorage SoAView.
   */
  void Build(const PathStorage::SoAView& soa_view, size_t storage_size);

  /**
   * Add a single entry to the index.
   */
  void AddEntry(uint32_t row_id, std::string_view extension);

  /**
   * Remove a single entry from the index. Emptied postings are erased
   * (FilenameTrigramIndex keeps them); see the definition for why.
   */
  void RemoveEntry(uint32_t row_id, std::string_view extension);

  /**
   * Clear all entries from the index and invalidate in-flight builds.
   */
  void Clear();

  /**
   * Query the index for candidates matching ANY of the specified extensions
   * (UNION). Returns nullopt when the index is not ready or the extensions
   * vector is empty. Returns an empty roaring bitmap (definite empty) when
   * no posting exists for any term.
   *
   * Case contract: the index stores lowercase-normalized keys and every
   * query term is normalized the same way, regardless of the search's
   * case_sensitive flag. The index is a candidate generator (superset);
   * exact case is enforced downstream by the verifier.
   */
  [[nodiscard]] std::optional<roaring::Roaring> QueryCandidates(
      const std::vector<std::string>& query_extensions) const;

  /**
   * Normalized-input variants of QueryCandidates / UnionCardinality for the
   * search engine's hot path. Precondition: every term is already
   * lowercase-normalized, dot-stripped, non-empty, and unique (see
   * NormalizeIndexableExtensions, the sole producer). Skips
   * re-normalization so a query normalizes exactly once across gate +
   * filter.
   */
  [[nodiscard]] std::optional<roaring::Roaring> QueryCandidatesNormalized(
      const std::vector<std::string>& normalized_terms) const;
  [[nodiscard]] std::optional<uint64_t> UnionCardinalityNormalized(
      const std::vector<std::string>& normalized_terms) const;

  /**
   * Directly unpacks candidate row IDs for normalized extension query terms
   * without materializing an intermediate Roaring union. Because extension
   * posting lists are pairwise disjoint by construction, unpacking each bitmap
   * directly into consecutive slices of out_ids is equivalent to
   * fastunion + toUint32Array, avoiding priority-queue overhead and container
   * allocations.
   *
   * Note: candidate IDs within each extension's slice are sorted in ascending
   * row order, but the overall vector is partitioned by extension rather than
   * globally sorted.
   *
   * Returns false if the index is not ready (caller falls back to scan).
   * Populates out_ids with candidate row IDs (empty out_ids means definite empty).
   */
  [[nodiscard]] bool ExtractCandidateIdsNormalized(
      const std::vector<std::string>& normalized_terms,
      std::vector<uint32_t>& out_ids) const;

  /**
   * Non-normalized variant of ExtractCandidateIdsNormalized.
   */
  [[nodiscard]] bool ExtractCandidateIds(
      const std::vector<std::string>& query_extensions,
      std::vector<uint32_t>& out_ids) const;

  /**
   * Normalizes raw query terms for index lookup: lowercase, strip one
   * leading dot, sort + unique. Returns an empty vector when ANY term
   * normalizes to empty (all-terms-indexable rule — see
   * UnionCardinalityNormalized); callers treat empty as unusable and fall
   * back to scan. Sole producer of Normalized-variant inputs.
   */
  [[nodiscard]] static std::vector<std::string> NormalizeIndexableExtensions(
      const std::vector<std::string>& extensions);

  /**
   * Union cardinality without materializing the union bitmap.
   *
   * Same contract as QueryCandidates (nullopt = not ready or empty
   * filter; 0 with value = definite empty). Exact: posting lists are
   * pairwise disjoint by construction (see UnionCardinalityNormalized),
   * so the union size is the sum of per-term cardinalities — no bitmap
   * is ever built here. Used by the cost-model gate, which needs only
   * the count — the filter re-queries the bitmap itself under its own
   * lock scope.
   */
  [[nodiscard]] std::optional<uint64_t> UnionCardinality(
      const std::vector<std::string>& query_extensions) const;

  [[nodiscard]] size_t GetMemoryUsageBytes() const;

  /**
   * Normalizes an extension view: strips one leading '.', lowercases ASCII.
   * The index stores keys in this form unconditionally (see QueryCandidates
   * for the case contract).
   */
  [[nodiscard]] static std::string NormalizeExtension(std::string_view extension);

  /**
   * Allocation-free normalization into a reused scratch buffer (cleared
   * first, capacity preserved). Public so the combined extractor — the
   * only sanctioned bulk caller — shares the exact normalization without
   * per-row allocations. Used by the extraction loop so the only
   * allocations are one owning string per distinct extension.
   */
  static void NormalizeInto(std::string_view extension, std::string& out);

 private:
  /**
   * Replays mutations buffered during an in-flight build onto posting_lists_.
   * Caller must hold a unique lock on mutex_. Mutations are applied in
   * chronological (buffer) order — add-then-remove and remove-then-add for
   * the same row_id yield different results, so the order must not be
   * shuffled.
   */
  void ReplayPendingMutationsLocked();

  struct PendingMutation {
    std::string key{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
                        // Normalized extension; field name `key` shared with
                        // index_build_lifecycle::ReplayPendingMutations.
    uint32_t row_id = 0;
    bool is_add = false;
  };

  /**
   * Collect posting bitmaps for all query terms present in the index,
   * sorted ascending by cardinality. Terms with no postings contribute
   * nothing (union semantics). Returns false when no term has postings
   * (the union will be empty). Caller must hold shared_lock on mutex_:
   * the returned pointers stay valid only under that lock.
   */
  [[nodiscard]] bool CollectSortedBitmaps(
      const std::vector<std::string>& normalized_terms,
      std::vector<const roaring::Roaring*>& bitmaps) const;

  // Lifecycle state (mutex_, build_generation_, is_ready_, is_building_,
  // build_submit_mutex_, build_future_) lives in IndexBuildLifecycle.
  flat_hash_map_t<std::string, roaring::Roaring> posting_lists_;
  std::vector<PendingMutation> pending_mutations_;
};

} // namespace extension_index
