#include "search/ExtensionIndex.h"
#include "search/CombinedIndexPostings.h"
#include "utils/Logger.h"
#include "utils/StringSearch.h"
#include "utils/ThreadPool.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <future>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <vector>

namespace extension_index {
namespace {

// Initial capacity for flat hash map: distinct extensions number in the
// hundreds on real corpora; 1024 avoids early rehashing with headroom.
constexpr size_t kInitialPostingMapCapacity = 1024;

// Sentinel generation for an ExtensionBuildTicket whose handoff failed. Safe
// because MarkBuilding() pre-increments, so a real generation is always >= 1.
constexpr uint64_t kInvalidGeneration = 0;

// Per-mutation normalization scratch: AddEntry/RemoveEntry run per mutation
// (hundreds of thousands during crawl). Normalizing into a thread-local
// buffer avoids one owning-string allocation per call; only the map insert
// and the pending-mutation buffer take ownership. One shared buffer is safe:
// every use is strictly scoped (normalize, then consume under lock — no
// nesting, no view escapes) and each thread gets its own copy.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - thread-local scratch: cleared per use, never shared across threads
thread_local std::string t_normalize_scratch;  // NOSONAR(cpp:S5421) - per-thread mutable scratch buffer cleared and reused on every AddEntry/RemoveEntry

void PopulatePostingBitmap(const std::vector<uint32_t>& row_ids,
                           roaring::Roaring& bitmap) {
  roaring::BulkContext context;
  for (const uint32_t row_id : row_ids) {
    bitmap.addBulk(context, row_id);
  }
  bitmap.runOptimize();
}

}  // namespace

std::string ExtensionIndex::NormalizeExtension(std::string_view extension) {
  std::string result;
  NormalizeInto(extension, result);
  return result;
}

void ExtensionIndex::NormalizeInto(std::string_view extension, std::string& out) {
  out.clear();
  if (!extension.empty() && extension[0] == '.') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by !extension.empty() check above
    extension.remove_prefix(1);
  }
  if (out.capacity() < extension.size()) {
    out.reserve(extension.size());
  }
  for (const char c : extension) {
    out.push_back(string_search::ToLowerChar(static_cast<unsigned char>(c)));
  }
}

void ExtensionIndex::BuildFromPostings(uint64_t generation,
                                       std::vector<ExtensionGroup> groups) {
  const auto start_time = std::chrono::steady_clock::now();
  size_t total_row_ids = 0;
  for (const auto& group : groups) {
    total_row_ids += group.row_ids.size();
  }

  flat_hash_map_t<std::string, roaring::Roaring> new_posting_lists;
  size_t pending_mutations_count = 0;
  size_t unique_extensions = 0;

  try {
    if (!groups.empty()) {
      new_posting_lists.reserve(kInitialPostingMapCapacity);

      // Deterministic commit order; row-ids within a group are ascending
      // by construction (rows visited in SoA order during extraction).
      std::sort(groups.begin(), groups.end(),  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
                [](const ExtensionGroup& a, const ExtensionGroup& b) {
                  return a.extension < b.extension;
                });
      for (const auto& group : groups) {
        // Intentional insert-if-missing: groups carry distinct extensions.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - operator[] inserts on miss by design
        roaring::Roaring& bitmap = new_posting_lists[group.extension];
        PopulatePostingBitmap(group.row_ids, bitmap);
      }
    }

    // Single lock scope: stale-generation guard + install + replay (tech-debt
    // T2, shared CommitBuildIfFresh). nullopt means a newer build (or Clear)
    // already owns the flags, so return without touching them.
    if (const auto outcome = CommitBuildIfFresh("ExtensionIndex", generation, start_time,
                                                std::move(new_posting_lists));
        outcome.has_value()) {
      unique_extensions = outcome->unique_count;
      pending_mutations_count = outcome->pending_count;
    } else {
      return;
    }
  } catch (const std::bad_alloc& e) {
    (void)e;
    LOG_ERROR_BUILD("ExtensionIndex::BuildFromPostings out of memory after "
                    << total_row_ids << " row-ids");
    AbortBuild(generation);
    return;
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - catch-all safety net; AbortBuild restores flags for any build failure
    (void)e;
    LOG_ERROR_BUILD("ExtensionIndex::BuildFromPostings exception: " << e.what());
    AbortBuild(generation);
    return;
  }

  const auto elapsed_microseconds = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start_time)
          .count());

  LogBenchmarkDuration("ExtensionIndex build", elapsed_microseconds);
  LOG_IMPORTANT_BUILD("ExtensionIndex: row_ids=" << total_row_ids
                      << ", unique_extensions=" << unique_extensions
                      << ", pending_mutations=" << pending_mutations_count
                      << ", index_mb=" << (GetMemoryUsageBytes() / logger_constants::kBytesPerMB)
                      << ", ready=true");
}

// Replays mutations buffered during an in-flight build onto posting_lists_.
// Reached only through ReplayMutationsForAbortLocked(), which runs under the
// unique lock on both the abort path and CommitBuildIfFresh's commit path (see
// the declaration in ExtensionIndex.h). Shared loop lives in
// index_build_lifecycle::ReplayPendingMutations (tech-debt D1); this index's
// policy is erase-emptied-postings (mirrors live RemoveEntry, mandated by
// specs/2026-10-09_EXTENSION_INDEX_CLEAN_IMPLEMENTATION_SPEC.md §3).
void ExtensionIndex::ReplayPendingMutationsLocked() {
  index_build_lifecycle::ReplayPendingMutations</*EraseEmptyPostingLists=*/true>(
      posting_lists_, pending_mutations_);
}

void ExtensionIndex::AbortBuild(uint64_t generation) noexcept {
  IndexBuildLifecycle<ExtensionIndex>::AbortBuild(generation, "ExtensionIndex");
}

index_build_lifecycle::AbortReplayOutcome ExtensionIndex::ReplayMutationsForAbortLocked() {
  ReplayPendingMutationsLocked();
  index_build_lifecycle::AbortReplayOutcome outcome{pending_mutations_.size(), posting_lists_.empty()};
  pending_mutations_.clear();
  return outcome;
}

size_t ExtensionIndex::InstallPostingsLocked(
    flat_hash_map_t<std::string, roaring::Roaring>&& new_postings) {
  posting_lists_ = std::move(new_postings);
  return posting_lists_.size();
}

ExtensionIndex::~ExtensionIndex() {
  // Wait while posting maps are still alive (see IndexBuildLifecycle).
  RetireInFlightBuild();
}

ExtensionBuildTicket ExtensionIndex::BeginRebuild(std::vector<ExtensionGroup> groups) {
  uint64_t generation = 0;
  bool marked = false;
  {
    const std::unique_lock lock(GetMutex());
    generation = GetBuildGeneration().load();
    marked = GetBuildingFlag().load();
  }
  if (!marked) {
    // No MarkBuilding() preceded this ticket: the caller bypassed the protocol
    // (wired call sites always mark first). Refuse rather than commit unguarded
    // — without a marked generation the commit cannot distinguish stale from
    // fresh. The log is I/O, so it runs outside the critical section (see
    // .agents/rules/lock-ordering-no-io-under-lock.md).
    LOG_ERROR_BUILD("ExtensionIndex::BeginRebuild without a marked building generation; ticket refused");
    return ExtensionBuildTicket{kInvalidGeneration, {}};
  }
  // The handoff is a noexcept move of the group vector (no allocation, no
  // failure path), so there is nothing to abort here. Done OUTSIDE the index
  // lock on purpose: AbortBuild re-locks this non-recursive shared_mutex, so
  // aborting while the lock is held would self-deadlock. The trigram twin ends
  // up unlocked for the same reason — its extraction, unlike this handoff, can
  // actually allocate and therefore needs a real abort path.
  return ExtensionBuildTicket{generation, std::move(groups)};
}

void ExtensionIndex::SubmitBuild(
    ExtensionBuildTicket ticket,
    const std::shared_ptr<ThreadPool>& thread_pool) {
  const uint64_t generation = ticket.Generation();
  if (generation == kInvalidGeneration) {
    return;  // Invalid ticket: BeginRebuild refused an unmarked build; index flags untouched.
  }
  std::vector<ExtensionGroup> groups = ticket.TakeGroups();

  if (!thread_pool) {
    LOG_IMPORTANT_BUILD("ExtensionIndex::SubmitBuild: thread pool unavailable, building synchronously");
    try {
      BuildFromPostings(generation, std::move(groups));
    } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - catch-all safety net; AbortBuild restores flags for any build failure
      (void)e;
      LOG_ERROR_BUILD("ExtensionIndex::SubmitBuild synchronous exception: " << e.what());
      AbortBuild(generation);
    }
    return;
  }

  // Serialize the wait-and-assign against any concurrent SubmitBuild. This is a
  // dedicated mutex rather than the index mutex because SubmitBuild must run
  // outside the index lock: waiting here for a still-running build would
  // otherwise block every search for the duration of that build.
  const std::scoped_lock submit_lock(GetSubmitMutex());
  // At most one build may be in flight, so that ~ExtensionIndex waiting on
  // build_future_ is sufficient to guarantee no task outlives this object.
  // std::future::wait() throws only std::future_error when the future has no
  // associated shared state, which valid() already excludes.
  if (GetBuildFuture().valid()) {
    GetBuildFuture().wait();
  }
  // Another MarkBuilding (or Clear) advanced the generation while we waited.
  // Enqueueing now would burn a full populate cycle on a task that
  // BuildFromPostings would discard on its first line. Every generation advance
  // either has a matching SubmitBuild from its own caller, or is Clear()/the
  // destructor -- and both of those already want nothing committed.
  if (generation != GetBuildGeneration().load()) {
    return;
  }
  const size_t group_count = groups.size();
  LOG_IMPORTANT_BUILD("ExtensionIndex::SubmitBuild: offloading extension index build ("
                      << group_count << " groups) to background thread pool");
  GetBuildFuture() = thread_pool->Enqueue([this, gen = generation, g = std::move(groups)]() mutable {
    try {
      BuildFromPostings(gen, std::move(g));
    } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - catch-all safety net; AbortBuild restores flags for any build failure
      (void)e;
      LOG_ERROR_BUILD("ExtensionIndex::SubmitBuild task exception: " << e.what());
      AbortBuild(gen);
    }
  });
}

void ExtensionIndex::RebuildAsync(
    const PathStorage::SoAView& soa_view,
    size_t storage_size,
    const std::shared_ptr<ThreadPool>& thread_pool) {
  // Standalone path only (see header): mark, run the combined extraction,
  // keep the extension half. Wired call sites replicate this sequence around
  // one shared extraction instead of calling here.
  const uint64_t generation = MarkBuilding();
  CombinedIndexPostings combined;
  try {
    combined = ExtractCombinedPostings(soa_view, storage_size);
  } catch (const std::bad_alloc& e) {
    (void)e;
    LOG_ERROR_BUILD("ExtensionIndex::RebuildAsync: extraction ran out of memory");
    AbortBuild(generation);
    return;
  }
  // Re-mark check: MarkBuilding above is the generation BeginRebuild binds.
  // BeginRebuild refuses when no build is marked; here one always is.
  SubmitBuild(BeginRebuild(std::move(combined.extension_groups)), thread_pool);
}

void ExtensionIndex::Build(const PathStorage::SoAView& soa_view, size_t storage_size) {
  RebuildAsync(soa_view, storage_size, nullptr);
}

void ExtensionIndex::AddEntry(uint32_t row_id, std::string_view extension) {
  NormalizeInto(extension, t_normalize_scratch);
  if (t_normalize_scratch.empty()) {
    return;
  }
  const std::unique_lock lock(GetMutex());
  if (GetBuildingFlag().load()) {
    pending_mutations_.push_back({t_normalize_scratch, row_id, true});
    return;
  }
  // Find-first: the common hit path allocates nothing (the key is copied
  // only on insert). Intentional insert-if-missing on miss.
  if (const auto it = posting_lists_.find(t_normalize_scratch); it != posting_lists_.end()) {
    it->second.add(row_id);
  } else {
    posting_lists_.try_emplace(t_normalize_scratch).first->second.add(row_id);
  }
}

void ExtensionIndex::RemoveEntry(uint32_t row_id, std::string_view extension) {
  NormalizeInto(extension, t_normalize_scratch);
  if (t_normalize_scratch.empty()) {
    return;
  }
  const std::unique_lock lock(GetMutex());
  if (GetBuildingFlag().load()) {
    pending_mutations_.push_back({t_normalize_scratch, row_id, false});
    return;
  }
  if (const auto it = posting_lists_.find(t_normalize_scratch); it != posting_lists_.end()) {
    it->second.remove(row_id);
    if (it->second.isEmpty()) {
      // Erase the emptied key: keys are owning std::strings over an unbounded
      // domain, so a dead key would pin its allocation until the next rebuild.
      // The replay path mirrors this choice (EraseEmptyPostingLists = true);
      // the trigram twin deliberately keeps empty postings — see
      // FilenameTrigramIndex::RemoveEntry for the rationale.
      posting_lists_.erase(it);
    }
  }
}

bool ExtensionIndex::CollectSortedBitmaps(
    const std::vector<std::string>& normalized_terms,
    std::vector<const roaring::Roaring*>& bitmaps) const {
  bitmaps.clear();
  bitmaps.reserve(normalized_terms.size());
  for (const std::string& term : normalized_terms) {
    const auto it = posting_lists_.find(term);
    if (it == posting_lists_.end() || it->second.isEmpty()) {
      continue;  // Union semantics: absent terms contribute nothing
    }
    bitmaps.push_back(&it->second);
  }
  if (bitmaps.empty()) {
    return false;
  }
  // Smallest-first keeps intermediate unions small.
  // NOLINTNEXTLINE(bugprone-nondeterministic-pointer-iteration-order) - union is commutative; order affects only probe order, never the result
  std::sort(bitmaps.begin(), bitmaps.end(),  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
            [](const roaring::Roaring* a, const roaring::Roaring* b) {
              return a->cardinality() < b->cardinality();
            });
  return true;
}

std::optional<roaring::Roaring> ExtensionIndex::QueryCandidates(
    const std::vector<std::string>& query_extensions) const {
  if (!GetReadyFlag().load() || query_extensions.empty()) {
    return std::nullopt;
  }

  return QueryCandidatesNormalized(NormalizeIndexableExtensions(query_extensions));
}

std::optional<roaring::Roaring> ExtensionIndex::QueryCandidatesNormalized(
    const std::vector<std::string>& normalized_terms) const {
  if (!GetReadyFlag().load() || normalized_terms.empty()) {
    return std::nullopt;
  }

  const std::shared_lock lock(GetMutex());
  std::vector<const roaring::Roaring*> bitmaps;
  if (!CollectSortedBitmaps(normalized_terms, bitmaps)) {
    return roaring::Roaring{};
  }

  if (bitmaps.size() == 1) {
    return *bitmaps[0];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size == 1 established
  }
  return roaring::Roaring::fastunion(bitmaps.size(), bitmaps.data());
}

bool ExtensionIndex::ExtractCandidateIdsNormalized(
    const std::vector<std::string>& normalized_terms,
    std::vector<uint32_t>& out_ids) const {
  out_ids.clear();
  if (!GetReadyFlag().load()) {
    return false;
  }
  if (normalized_terms.empty()) {
    return true;
  }

  const std::shared_lock lock(GetMutex());
  if (!GetReadyFlag().load()) {
    return false;
  }

  std::vector<const roaring::Roaring*> bitmaps;
  if (!CollectSortedBitmaps(normalized_terms, bitmaps)) {
    return true;  // Definite empty: none of the terms exist in the index
  }

  uint64_t total_cardinality = 0;
  for (const auto* bm : bitmaps) {
    total_cardinality += bm->cardinality();
  }

  if (total_cardinality == 0) {
    return true;
  }

  out_ids.resize(static_cast<size_t>(total_cardinality));
  size_t offset = 0;
  for (const auto* bm : bitmaps) {
    if (const uint64_t card = bm->cardinality(); card > 0) {
      bm->toUint32Array(out_ids.data() + offset);
      offset += static_cast<size_t>(card);
    }
  }
  return true;
}

bool ExtensionIndex::ExtractCandidateIds(
    const std::vector<std::string>& query_extensions,
    std::vector<uint32_t>& out_ids) const {
  if (!GetReadyFlag().load()) {
    out_ids.clear();
    return false;
  }
  if (query_extensions.empty()) {
    out_ids.clear();
    return true;
  }
  return ExtractCandidateIdsNormalized(NormalizeIndexableExtensions(query_extensions), out_ids);
}

std::optional<uint64_t> ExtensionIndex::UnionCardinality(
    const std::vector<std::string>& query_extensions) const {
  if (!GetReadyFlag().load() || query_extensions.empty()) {
    return std::nullopt;
  }

  return UnionCardinalityNormalized(NormalizeIndexableExtensions(query_extensions));
}

std::vector<std::string> ExtensionIndex::NormalizeIndexableExtensions(
    const std::vector<std::string>& extensions) {
  std::vector<std::string> normalized;
  normalized.reserve(extensions.size());
  for (const auto& raw : extensions) {
    std::string term = NormalizeExtension(raw);
    if (term.empty()) {
      return {};
    }
    normalized.push_back(std::move(term));
  }
  // Terms feed an exact-sum cardinality (see UnionCardinalityNormalized):
  // duplicates would double-count, so unique here once.
  std::sort(normalized.begin(), normalized.end());  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
  normalized.erase(std::unique(normalized.begin(), normalized.end()),  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
                   normalized.end());
  return normalized;
}

std::optional<uint64_t> ExtensionIndex::UnionCardinalityNormalized(
    const std::vector<std::string>& normalized_terms) const {
  if (!GetReadyFlag().load() || normalized_terms.empty()) {
    return std::nullopt;
  }

  // Exact sum with zero bitmap materialization: posting lists are pairwise
  // disjoint by construction — each row carries at most one extension and
  // extension-less rows are indexed nowhere (see AddEntry, RemoveEntry, and
  // the extractor, all of which move a row between at most one key). Terms
  // are unique per the precondition, so no double count. A precondition
  // violation can only overestimate (toward scan fallback), never produce a
  // wrong candidate set — the filter materializes the real union.
  const std::shared_lock lock(GetMutex());
  uint64_t total = 0;
  for (const std::string& term : normalized_terms) {
    if (const auto it = posting_lists_.find(term); it != posting_lists_.end()) {
      total += it->second.cardinality();
    }
  }
  return total;
}

size_t ExtensionIndex::GetMemoryUsageBytes() const {
  const std::shared_lock lock(GetMutex());
  // Postings storage plus a rough estimate of the map's own structure (bucket
  // array + node headers). It is 0 for an empty index (see
  // EstimateHashMapStructureBytes for the convention).
  size_t total = EstimateHashMapStructureBytes(posting_lists_);
  for (const auto& [key, bitmap] : posting_lists_) {
    // Key accounting matches the trigram twin: inline string object + its heap
    // bytes, then the bitmap contents.
    total += sizeof(std::string) + key.capacity() + sizeof(roaring::Roaring);
    total += bitmap.getSizeInBytes();
  }
  return total;
}

void ExtensionIndex::Clear() {
  const std::unique_lock lock(GetMutex());
  ++GetBuildGeneration();
  posting_lists_.clear();
  pending_mutations_.clear();
  GetBuildingFlag().store(false);
  GetReadyFlag().store(true);
}

} // namespace extension_index
