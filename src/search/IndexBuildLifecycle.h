#pragma once

#include "utils/Logger.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <future>
#include <mutex>
#include <shared_mutex>
#include <string_view>

namespace index_build_lifecycle {

/**
 * Outcome of the derived-class replay step inside AbortBuild.
 */
struct AbortReplayOutcome {
  size_t replayed_count = 0;
  bool postings_empty = true;
};

/**
 * @brief Shared async-build lifecycle for the posting-list indexes.
 *
 * ExtensionIndex and FilenameTrigramIndex ran identical build-lifecycle
 * protocols (Sonar duplication pair on AbortBuild; same MarkBuilding and
 * teardown bodies below threshold). This base owns the shared state and
 * implements the protocol once; each derived index supplies only its
 * posting-specific replay step.
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
 *   Must provide public AbortReplayOutcome ReplayMutationsForAbortLocked()
 *   (caller holds the unique lock): replay buffered mutations, clear the
 *   pending buffer, and report the replayed count plus whether postings
 *   are empty.
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
