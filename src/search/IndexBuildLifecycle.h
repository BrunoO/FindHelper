#pragma once

#include "utils/Logger.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace index_build_lifecycle {

/**
 * Outcome of the derived-class replay step inside AbortBuild.
 */
struct AbortReplayOutcome {
  size_t replayed_count = 0;
  bool postings_empty = true;
};

/**
 * Outcome of a fresh commit installed by CommitBuildIfFresh.
 */
struct CommitOutcome {
  size_t unique_count = 0;
  size_t pending_count = 0;
};

/**
 * @brief Applies chronologically buffered mutations onto a posting-list map.
 * (Tech-debt D1: single implementation of the loop that ExtensionIndex and
 * FilenameTrigramIndex each duplicated in ReplayPendingMutationsLocked.)
 *
 * Both indexes buffer Add/Remove while a rebuild is in flight and replay the
 * buffer at commit (BuildFromPostings) and on abort — so a mutation must
 * produce the SAME final index state whether it arrived live or buffered.
 * That forces replay's empty-posting policy to mirror each index's own live
 * RemoveEntry contract, and the two contracts legitimately differ:
 *
 * - ExtensionIndex: keys are owning std::strings (one heap allocation per
 *   distinct extension, unbounded domain) and postings are pairwise disjoint,
 *   so a niche extension's posting empties whenever its last file is removed.
 *   Retaining emptied keys would pin string allocations forever ->
 *   EraseEmptyPostingLists = true (mandated by
 *   specs/2026-10-09_EXTENSION_INDEX_CLEAN_IMPLEMENTATION_SPEC.md §3).
 * - FilenameTrigramIndex: keys are packed 24-bit ints (kept-empty postings
 *   cost no allocation) and 3-gram postings are shared across filenames, so
 *   they rarely empty -> EraseEmptyPostingLists = false. Queries treat empty
 *   bitmaps as missing and rebuilds swap the map wholesale, so kept entries
 *   cannot accumulate across rebuilds.
 *
 * ORDER: mutations are applied in chronological (buffer) order — add-then-
 * remove and remove-then-add for the same (key, row_id) yield different
 * results, so the order must not be shuffled.
 *
 * @pre Caller holds the index's unique lock: map and buffer are touched here
 *      with no internal locking.
 *
 * @tparam EraseEmptyPostingLists True: a removal that empties a posting
 *         erases the key (ExtensionIndex policy); false: keep it
 *         (FilenameTrigramIndex policy).
 * @tparam Mutation   Needs public fields `key`, `row_id` (uint32_t),
 *                    `is_add` (bool) — both indexes' PendingMutation qualify.
 * @tparam PostingMap Map of key -> roaring-compatible bitmap (add/remove/
 *                    isEmpty).
 */
template <bool EraseEmptyPostingLists, typename Mutation, typename PostingMap>
void ReplayPendingMutations(PostingMap& posting_lists,
                            const std::vector<Mutation>& pending_mutations) {
  for (const auto& mutation : pending_mutations) {
    if (mutation.is_add) {
      // try_emplace == operator[] for insert-if-missing; avoids the
      // unchecked-access warning while keeping identical semantics.
      posting_lists.try_emplace(mutation.key).first->second.add(mutation.row_id);
    } else if (auto it = posting_lists.find(mutation.key); it != posting_lists.end()) {
      it->second.remove(mutation.row_id);
      if (EraseEmptyPostingLists && it->second.isEmpty()) {
        posting_lists.erase(it);
      }
    }
  }
}

/**
 * @brief Shared async-build lifecycle for the posting-list indexes.
 *
 * ExtensionIndex and FilenameTrigramIndex ran identical build-lifecycle
 * protocols (Sonar duplication pair on AbortBuild; same MarkBuilding and
 * teardown bodies below threshold). This base owns the shared state and
 * implements the protocol once; each derived index supplies only its
 * posting-specific build step plus two locking hooks (install + replay).
 *
 * PERFORMANCE: cold-path only (build setup/teardown/recovery — never the
 * query or extraction loops). No virtuals (CRTP static dispatch), everything
 * header-defined so inlining is identical or better than the previous
 * out-of-line bodies. Same lock type and order, same atomic operations
 * with default memory ordering, no added branches or allocations.
 *
 * LIFETIME: the destructor intentionally does NOT wait for an in-flight
 * build — base destruction runs after derived members (the posting maps
 * the background task writes into) are gone. Each derived destructor body
 * must call RetireInFlightBuild() while its members are still alive.
 *
 * @tparam Derived The concrete index (ExtensionIndex, FilenameTrigramIndex).
 *   Must provide, both public and both called under the unique lock taken by
 *   this base:
 *   - AbortReplayOutcome ReplayMutationsForAbortLocked(): replay buffered
 *     mutations, clear the pending buffer, and report the replayed count plus
 *     whether postings are empty;
 *   - size_t InstallPostingsLocked(PostingMap&&): move a freshly built map into
 *     posting_lists_ and return its size.
 */
template <typename Derived>
class IndexBuildLifecycle {
 public:
  /**
   * Marks the index as currently undergoing a background rebuild.
   * Increments and returns the new build generation.
   *
   * Serialized with the BuildFromPostings commit check under mutex_: without
   * this, a build could pass its generation check, then a concurrent
   * MarkBuilding bumps the generation, then the build still swaps its stale
   * postings and flips ready=true (a stale commit served as fresh until the
   * next build commits). Lock order everywhere is caller-index -> this
   * index; this takes only this index's lock, so no inversion.
   *
   * Callers that bump the generation this way MUST also submit or abort a build
   * for it; otherwise is_building_ stays true forever and pending_mutations_
   * grows unbounded. Prefer the BeginRebuild()/SubmitBuild() pair.
   */
  [[nodiscard]] uint64_t MarkBuilding() noexcept {
    const std::unique_lock lock(mutex_);
    const uint64_t generation = ++build_generation_;
    is_ready_.store(false);
    is_building_.store(true);
    return generation;
  }

  /**
   * Aborts an in-flight build on exception or failure: replays buffered
   * mutations onto the live postings, clears the pending buffer, and
   * restores the building/ready flags. Never throws.
   *
   * @param generation Build generation being aborted; stale generations
   *   are ignored (a newer build already owns the flags).
   * @param log_tag Index name for log lines (e.g. "ExtensionIndex").
   */
  void AbortBuild(uint64_t generation, std::string_view log_tag) noexcept {
    try {
      const std::unique_lock lock(mutex_);
      if (generation == build_generation_.load()) {
        AbortReplayOutcome outcome = static_cast<Derived*>(this)->ReplayMutationsForAbortLocked();
        is_building_.store(false);
        is_ready_.store(!outcome.postings_empty);
        LOG_IMPORTANT_BUILD(log_tag << ": build generation " << generation
                                    << " aborted; replayed " << outcome.replayed_count
                                    << " mutations, ready=" << (!outcome.postings_empty));
      }
    } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - catch-all safety net; must not throw out of a noexcept recovery path
      (void)e;
      LOG_ERROR_BUILD(log_tag << "::AbortBuild exception: " << e.what());
    } catch (...) {  // NOSONAR(cpp:S2738) - catch-all required: AbortBuild is noexcept and must not propagate
      LOG_ERROR_BUILD(log_tag << "::AbortBuild unknown exception");
    }
  }

 protected:
  IndexBuildLifecycle() = default;
  // Default: never wait here — see the LIFETIME note above. Derived
  // destructors call RetireInFlightBuild() from their own body.
  ~IndexBuildLifecycle() = default;

  IndexBuildLifecycle(const IndexBuildLifecycle&) = delete;
  IndexBuildLifecycle& operator=(const IndexBuildLifecycle&) = delete;
  IndexBuildLifecycle(IndexBuildLifecycle&&) = delete;
  IndexBuildLifecycle& operator=(IndexBuildLifecycle&&) = delete;

  /**
   * Invalidates any in-flight build (so it discards its results) and waits
   * for it to retire. Must be called from the derived destructor body,
   * while derived members are still alive.
   */
  void RetireInFlightBuild() {
    // Invalidate any in-flight build before waiting so it discards its results.
    build_generation_.fetch_add(1);
    // std::future::wait() throws only std::future_error when the future has no
    // associated shared state, which valid() already excludes, so no try/catch
    // is needed here.
    const std::scoped_lock submit_lock(build_submit_mutex_);
    if (build_future_.valid()) {
      build_future_.wait();
    }
  }

  /**
   * @brief Atomically installs a freshly built posting map if its generation is
   * still active. (Tech-debt T2: single implementation of the BuildFromPostings
   * commit head that ExtensionIndex and FilenameTrigramIndex duplicated.)
   *
   * Guard + install + replay deliberately share ONE lock acquisition: splitting
   * the stale-generation guard into a helper that unlocks before the commit
   * re-locks would reopen a TOCTOU window in which a concurrent MarkBuilding
   * bumps the generation between check and install, committing stale postings as
   * fresh (transient false negatives until the next rebuild). The stale-build log
   * lines are emitted after the lock is released, since logging is I/O
   * (.agents/rules/lock-ordering-no-io-under-lock.md).
   *
   * @param log_tag      Index name for the stale-build lines (e.g. "ExtensionIndex").
   * @param generation   Build generation the map was produced for.
   * @param start_time   When the build began (for the stale benchmark duration).
   * @param new_postings Built map, moved into the derived index on success.
   * @return std::nullopt when the generation is stale (caller returns straight
   *   away — the newer build owns the flags); otherwise the installed posting
   *   count and the number of buffered mutations replayed on top of it.
   *
   * @pre The caller must NOT hold mutex_. The derived index provides:
   *   - InstallPostingsLocked(PostingMap&&): moves the map into posting_lists_
   *     and returns its size;
   *   - ReplayMutationsForAbortLocked(): replays then clears buffered mutations.
   *   Both run under the single lock taken here. Reusing the abort-named replay
   *   hook is intentional: it is exactly the "replay buffer in order, then
   *   clear" step both paths need. Its postings_empty result is ignored here —
   *   a committed map is authoritative even when empty (it replaces the old map),
   *   whereas abort keeps the pre-build map and must report !postings_empty.
   */
  template <typename PostingMap>
  [[nodiscard]] std::optional<CommitOutcome> CommitBuildIfFresh(
      std::string_view log_tag,
      uint64_t generation,
      std::chrono::steady_clock::time_point start_time,
      PostingMap&& new_postings) {
    size_t installed_count = 0;
    size_t pending_count = 0;
    bool stale = false;
    {
      const std::unique_lock lock(mutex_);
      if (generation != build_generation_.load()) {
        stale = true;
      } else {
        installed_count =
            static_cast<Derived*>(this)->InstallPostingsLocked(std::forward<PostingMap>(new_postings));
        const AbortReplayOutcome replay =
            static_cast<Derived*>(this)->ReplayMutationsForAbortLocked();
        is_building_.store(false);
        is_ready_.store(true);
        pending_count = replay.replayed_count;
      }
    }
    if (stale) {
      // Logging is I/O, so it runs after the lock is released (see
      // .agents/rules/lock-ordering-no-io-under-lock.md). build_generation_ is
      // re-read here for the diagnostic only; it may have advanced again.
      const auto elapsed = static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - start_time)
              .count());
      LogBenchmarkDuration(std::string(log_tag) + " build (stale)", elapsed);
      LOG_IMPORTANT_BUILD(log_tag << ": build generation " << generation
                          << " is stale (active: " << build_generation_.load()
                          << "), discarding results");
      return std::nullopt;
    }
    return CommitOutcome{installed_count, pending_count};
  }

  // Controlled access to the shared lifecycle state for derived indexes
  // (Sonar cpp:S3656/cpp:S8379: state stays private; no protected or
  // mutable-exposed variables). Trivially inline: identical codegen to
  // direct member access, so no performance impact.
  [[nodiscard]] std::shared_mutex& GetMutex() const noexcept { return mutex_; }
  [[nodiscard]] std::atomic<uint64_t>& GetBuildGeneration() noexcept { return build_generation_; }
  [[nodiscard]] std::atomic<bool>& GetReadyFlag() noexcept { return is_ready_; }
  [[nodiscard]] const std::atomic<bool>& GetReadyFlag() const noexcept { return is_ready_; }
  [[nodiscard]] std::atomic<bool>& GetBuildingFlag() noexcept { return is_building_; }
  [[nodiscard]] const std::atomic<bool>& GetBuildingFlag() const noexcept { return is_building_; }
  [[nodiscard]] std::mutex& GetSubmitMutex() noexcept { return build_submit_mutex_; }
  [[nodiscard]] std::future<void>& GetBuildFuture() noexcept { return build_future_; }

 private:
  mutable std::shared_mutex mutex_;
  std::atomic<uint64_t> build_generation_{0};
  std::atomic<bool> is_ready_{true};
  std::atomic<bool> is_building_{false};
  // Guards build_future_ only. Separate from mutex_ because SubmitBuild touches
  // it outside the index lock, and from the index lock because waiting there
  // would stall searches.
  std::mutex build_submit_mutex_;
  std::future<void> build_future_;
};

}  // namespace index_build_lifecycle
