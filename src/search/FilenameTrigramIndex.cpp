#include "search/FilenameTrigramIndex.h"
#include "search/FilenameTrigramPostings.h"
#include "search/SearchThreadPool.h"
#include "utils/Logger.h"
#include "utils/SortAliases.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <future>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <vector>

namespace filename_trigram {
namespace {

// Initial capacity for flat hash map to avoid early rehashing across unique trigrams.
constexpr size_t kInitialPostingMapCapacity = 65'536;

// Sentinel generation for a TrigramBuildTicket whose extraction failed. Safe
// because MarkBuilding() pre-increments, so a real generation is always >= 1.
constexpr uint64_t kInvalidGeneration = 0;

void PopulatePostingBitmap(const std::vector<uint64_t>& postings,
                           size_t run_start,
                           size_t run_end,
                           roaring::Roaring& bitmap) {
  roaring::BulkContext context;
  auto prev_row_id = static_cast<uint32_t>(postings[run_start]);
  bitmap.addBulk(context, prev_row_id);
  for (size_t k = run_start + 1; k < run_end; ++k) {
    if (const auto row_id = static_cast<uint32_t>(postings[k]); row_id != prev_row_id) {
      bitmap.addBulk(context, row_id);
      prev_row_id = row_id;
    }
  }
  bitmap.runOptimize();
}

}  // namespace

std::vector<uint64_t> FilenameTrigramIndex::ExtractPostings(
    const PathStorage::SoAView& soa_view, size_t storage_size) {
  if (soa_view.size == 0) {
    return {};
  }

  const auto extract_start = std::chrono::steady_clock::now();

  // Pre-pass bound is shared with the combined extractor (tech-debt T3) so the
  // two halves cannot drift; see FilenameTrigramPostings.h for the clamp note.
  const size_t max_postings = ComputeMaxTrigramPostings(soa_view, storage_size);

  if (max_postings > kMaxPostingReservation) {
    LOG_IMPORTANT_BUILD("FilenameTrigramIndex::ExtractPostings: clamping reserve from "
                        << max_postings << " to " << kMaxPostingReservation << " postings");
  }

  std::vector<uint64_t> postings;
  const bool reservation_clamped = max_postings > kMaxPostingReservation;
  const size_t reserved_postings = (std::min)(max_postings, kMaxPostingReservation);
  postings.reserve(reserved_postings);

  for (size_t i = 0; i < soa_view.size; ++i) {
    if (soa_view.is_deleted[i] != 0) {
      continue;
    }

    const size_t filename_offset = soa_view.filename_start[i];
    const size_t path_len = soa_view.GetPathLength(i, storage_size);
    if (filename_offset + 3 > path_len) {
      continue;
    }

    AppendFilenameTrigramPostings(soa_view.path_storage + soa_view.path_offsets[i] + filename_offset,
                                  path_len - filename_offset, static_cast<uint32_t>(i), postings);
  }

  const auto extract_microseconds = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - extract_start)
          .count());

  LogBenchmarkDuration("FilenameTrigramIndex extraction", extract_microseconds);
  LOG_IMPORTANT_BUILD("FilenameTrigramIndex::ExtractPostings: extracted "
                      << postings.size() << " postings in "
                      << (static_cast<double>(extract_microseconds) /
                          static_cast<double>(logger_constants::kMicrosecondsPerMillisecond))
                      << " ms ("
                      << (postings.size() * sizeof(uint64_t) / logger_constants::kBytesPerMB)
                      << " MB peak transient)");
  if (reservation_clamped) {
    LOG_IMPORTANT_BUILD("FilenameTrigramIndex::ExtractPostings: reservation clamped to "
                        << reserved_postings << " postings; the peak above is the true cost "
                        << "(the cap is allocation-hint-only, the vector grows to actual size)");
  }

  return postings;
}

void FilenameTrigramIndex::BuildFromPostings(uint64_t generation,
                                             std::vector<uint64_t> postings) {
  const auto start_time = std::chrono::steady_clock::now();
  const size_t total_postings = postings.size();

  flat_hash_map_t<TrigramKey, roaring::Roaring> new_posting_lists;
  size_t pending_mutations_count = 0;
  size_t unique_trigrams = 0;

  try {
    if (!postings.empty()) {
      new_posting_lists.reserve(kInitialPostingMapCapacity);

      SortUint64InPlace(postings);

      size_t run_start = 0;

      while (run_start < total_postings) {
        const uint64_t first_item = postings[run_start];
        const auto current_trigram = static_cast<TrigramKey>(first_item >> 32U);

        size_t run_end = run_start + 1;
        while (run_end < total_postings &&
               (static_cast<TrigramKey>(postings[run_end] >> 32U) == current_trigram)) {
          ++run_end;
        }

        roaring::Roaring& bitmap = new_posting_lists[current_trigram];
        PopulatePostingBitmap(postings, run_start, run_end, bitmap);

        run_start = run_end;
      }
    }

    // Single lock scope: stale-generation guard + install + replay (tech-debt
    // T2, shared CommitBuildIfFresh). nullopt means a newer build (or Clear)
    // already owns the flags, so return without touching them.
    if (const auto outcome = CommitBuildIfFresh("FilenameTrigramIndex", generation, start_time,
                                                std::move(new_posting_lists));
        outcome.has_value()) {
      unique_trigrams = outcome->unique_count;
      pending_mutations_count = outcome->pending_count;
    } else {
      return;
    }
  } catch (const std::bad_alloc& e) {
    (void)e;
    LOG_ERROR_BUILD("FilenameTrigramIndex::BuildFromPostings out of memory after "
                    << total_postings << " postings");
    AbortBuild(generation);
    return;
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - catch-all safety net; AbortBuild restores flags for any build failure
    (void)e;
    LOG_ERROR_BUILD("FilenameTrigramIndex::BuildFromPostings exception: " << e.what());
    AbortBuild(generation);
    return;
  }

  const auto elapsed_microseconds = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start_time)
          .count());

  LogBenchmarkDuration("FilenameTrigramIndex build", elapsed_microseconds);
  LOG_IMPORTANT_BUILD("FilenameTrigramIndex: postings=" << total_postings
                      << ", unique_trigrams=" << unique_trigrams
                      << ", pending_mutations=" << pending_mutations_count
                      << ", transient_mb=" << (total_postings * sizeof(uint64_t) / logger_constants::kBytesPerMB)
                      << ", index_mb=" << (GetMemoryUsageBytes() / logger_constants::kBytesPerMB)
                      << ", ready=true");
}

// Replays mutations buffered during an in-flight build onto posting_lists_.
// Reached only through ReplayMutationsForAbortLocked(), which runs under the
// unique lock on both the abort path and CommitBuildIfFresh's commit path (see
// the declaration in FilenameTrigramIndex.h). Shared loop lives in
// index_build_lifecycle::ReplayPendingMutations (tech-debt D1); this index's
// policy keeps emptied postings (mirrors live RemoveEntry: packed-int keys
// make kept-empty postings allocation-free, and shared n-gram postings
// rarely empty).
void FilenameTrigramIndex::ReplayPendingMutationsLocked() {
  index_build_lifecycle::ReplayPendingMutations</*EraseEmptyPostingLists=*/false>(
      posting_lists_, pending_mutations_);
}

void FilenameTrigramIndex::AbortBuild(uint64_t generation) noexcept {
  IndexBuildLifecycle<FilenameTrigramIndex>::AbortBuild(generation, "FilenameTrigramIndex");
}

index_build_lifecycle::AbortReplayOutcome FilenameTrigramIndex::ReplayMutationsForAbortLocked() {
  ReplayPendingMutationsLocked();
  index_build_lifecycle::AbortReplayOutcome outcome{pending_mutations_.size(), posting_lists_.empty()};
  pending_mutations_.clear();
  return outcome;
}

size_t FilenameTrigramIndex::InstallPostingsLocked(
    flat_hash_map_t<TrigramKey, roaring::Roaring>&& new_postings) {
  posting_lists_ = std::move(new_postings);
  return posting_lists_.size();
}

FilenameTrigramIndex::~FilenameTrigramIndex() {
  // Wait while posting maps are still alive (see IndexBuildLifecycle).
  RetireInFlightBuild();
}

TrigramBuildTicket FilenameTrigramIndex::BeginRebuild(
    const PathStorage::SoAView& soa_view,
    size_t storage_size) {
  const uint64_t generation = MarkBuilding();
  try {
    return TrigramBuildTicket{generation, ExtractPostings(soa_view, storage_size)};
  } catch (const std::bad_alloc& e) {
    // Extraction reserves up to kMaxPostingReservation (256 MB) and pushes per
    // filename, so std::bad_alloc is the only realistic failure here. AbortBuild
    // restores is_building_/is_ready_ and replays mutations buffered since
    // MarkBuilding(); without it the index would never report ready again and
    // pending_mutations_ would grow without bound.
    (void)e;
    LOG_ERROR_BUILD("FilenameTrigramIndex::BeginRebuild: postings extraction ran out of memory");
    AbortBuild(generation);
    return TrigramBuildTicket{kInvalidGeneration, {}};
  }
}

void FilenameTrigramIndex::SubmitBuild(
    TrigramBuildTicket ticket,
    const std::shared_ptr<SearchThreadPool>& thread_pool) {
  const uint64_t generation = ticket.Generation();
  if (generation == kInvalidGeneration) {
    return;  // BeginRebuild failed and already restored the index flags.
  }
  std::vector<uint64_t> postings = ticket.TakePostings();

  if (!thread_pool) {
    LOG_IMPORTANT_BUILD("FilenameTrigramIndex::SubmitBuild: thread pool unavailable, building synchronously");
    try {
      BuildFromPostings(generation, std::move(postings));
    } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - catch-all safety net; AbortBuild restores flags for any build failure
      (void)e;
      LOG_ERROR_BUILD("FilenameTrigramIndex::SubmitBuild synchronous exception: " << e.what());
      AbortBuild(generation);
    }
    return;
  }

  // Serialize the wait-and-assign against any concurrent SubmitBuild. This is a
  // dedicated mutex rather than the index mutex because SubmitBuild must run
  // outside the index lock: waiting here for a still-running build would
  // otherwise block every search for the duration of that build.
  const std::scoped_lock submit_lock(GetSubmitMutex());
  // At most one build may be in flight, so that ~FilenameTrigramIndex waiting on
  // build_future_ is sufficient to guarantee no task outlives this object.
  // std::future::wait() throws only std::future_error when the future has no
  // associated shared state, which valid() already excludes.
  if (GetBuildFuture().valid()) {
    GetBuildFuture().wait();
  }
  // Another BeginRebuild (or Clear) advanced the generation while we waited.
  // Enqueueing now would burn a full sort-and-populate cycle on a task that
  // BuildFromPostings would discard on its first line. Every generation advance
  // either has a matching SubmitBuild from its own caller, or is Clear()/the
  // destructor -- and both of those already want nothing committed.
  if (generation != GetBuildGeneration().load()) {
    return;
  }
  LOG_IMPORTANT_BUILD("FilenameTrigramIndex::SubmitBuild: offloading trigram index build ("
                      << postings.size() << " postings, "
                      << (postings.size() * sizeof(uint64_t) / logger_constants::kBytesPerMB)
                      << " MB) to background thread pool");
  GetBuildFuture() = thread_pool->Enqueue([this, gen = generation, p = std::move(postings)]() mutable {
    try {
      BuildFromPostings(gen, std::move(p));
    } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - catch-all safety net; AbortBuild restores flags for any build failure
      (void)e;
      LOG_ERROR_BUILD("FilenameTrigramIndex::SubmitBuild task exception: " << e.what());
      AbortBuild(gen);
    }
  });
}

void FilenameTrigramIndex::RebuildAsync(
    const PathStorage::SoAView& soa_view,
    size_t storage_size,
    const std::shared_ptr<SearchThreadPool>& thread_pool) {
  SubmitBuild(BeginRebuild(soa_view, storage_size), thread_pool);
}

void FilenameTrigramIndex::Build(const PathStorage::SoAView& soa_view, size_t storage_size) {
  RebuildAsync(soa_view, storage_size, nullptr);
}

void FilenameTrigramIndex::Clear() {
  const std::unique_lock lock(GetMutex());
  ++GetBuildGeneration();
  posting_lists_.clear();
  pending_mutations_.clear();
  GetBuildingFlag().store(false);
  GetReadyFlag().store(true);
}

void FilenameTrigramIndex::AddEntry(uint32_t row_id, std::string_view filename) {
  std::vector<TrigramKey> trigrams;
  ExtractTrigrams(filename, trigrams);
  const std::unique_lock lock(GetMutex());
  if (GetBuildingFlag().load()) {
    for (const TrigramKey trigram : trigrams) {
      pending_mutations_.push_back({trigram, row_id, true});
    }
    return;
  }
  for (const TrigramKey trigram : trigrams) {
    posting_lists_[trigram].add(row_id);
  }
}

void FilenameTrigramIndex::RemoveEntry(uint32_t row_id, std::string_view filename) {
  std::vector<TrigramKey> trigrams;
  ExtractTrigrams(filename, trigrams);
  const std::unique_lock lock(GetMutex());
  if (GetBuildingFlag().load()) {
    for (const TrigramKey trigram : trigrams) {
      pending_mutations_.push_back({trigram, row_id, false});
    }
    return;
  }
  for (const TrigramKey trigram : trigrams) {
    if (const auto it = posting_lists_.find(trigram); it != posting_lists_.end()) {
      it->second.remove(row_id);
      // The emptied posting is intentionally kept (ExtensionIndex erases its
      // emptied keys — see ExtensionIndex::RemoveEntry). Packed 24-bit keys
      // carry no heap allocation, shared n-gram postings rarely empty, and the
      // next rebuild swaps the whole map, so kept entries cannot pile up. The
      // replay path mirrors this choice (EraseEmptyPostingLists = false), and
      // queries treat an empty bitmap as missing.
    }
  }
}

bool FilenameTrigramIndex::CollectSortedBitmaps(
    const std::vector<TrigramKey>& required_trigrams,
    std::vector<const roaring::Roaring*>& bitmaps) const {
  bitmaps.clear();
  bitmaps.reserve(required_trigrams.size());
  for (const TrigramKey trigram : required_trigrams) {
    const auto it = posting_lists_.find(trigram);
    if (it == posting_lists_.end() || it->second.isEmpty()) {
      return false;  // Early exit: intersection will be empty
    }
    bitmaps.push_back(&it->second);
  }
  // Sort bitmaps by cardinality for efficient intersection
  // NOLINTNEXTLINE(bugprone-nondeterministic-pointer-iteration-order) - intersection is commutative; order affects only probe order, never the result
  std::sort(bitmaps.begin(), bitmaps.end(),
            [](const roaring::Roaring* a, const roaring::Roaring* b) {
              return a->cardinality() < b->cardinality();
            });
  return true;
}

std::optional<roaring::Roaring> FilenameTrigramIndex::QueryCandidates(
    const std::vector<TrigramKey>& required_trigrams) const {
  if (!GetReadyFlag().load() || required_trigrams.empty()) {
    return std::nullopt;
  }

  const std::shared_lock lock(GetMutex());
  std::vector<const roaring::Roaring*> bitmaps;
  if (!CollectSortedBitmaps(required_trigrams, bitmaps)) {
    return std::nullopt;
  }

  if (bitmaps.size() == 1) {
    return *bitmaps[0];
  }

  // Intersect the two smallest directly without deep-copying bitmaps[0] first
  roaring::Roaring result = (*bitmaps[0]) & (*bitmaps[1]);

  for (size_t i = 2; i < bitmaps.size(); ++i) {
    if (result.isEmpty()) {
      break;
    }
    result &= *bitmaps[i];
  }

  return result;
}

std::optional<uint64_t> FilenameTrigramIndex::IntersectionCardinality(
    const std::vector<TrigramKey>& required_trigrams) const {
  if (!GetReadyFlag().load() || required_trigrams.empty()) {
    return std::nullopt;
  }

  const std::shared_lock lock(GetMutex());
  std::vector<const roaring::Roaring*> bitmaps;
  if (!CollectSortedBitmaps(required_trigrams, bitmaps)) {
    return std::nullopt;
  }

  if (bitmaps.size() == 1) {
    return bitmaps[0]->cardinality();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size == 1 checked above
  }
  if (bitmaps.size() == 2) {
    return bitmaps[0]->and_cardinality(*bitmaps[1]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size == 2 checked above
  }

  roaring::Roaring result = (*bitmaps[0]) & (*bitmaps[1]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - reached only when size >= 2 (smaller sizes returned above)
  for (size_t i = 2; i < bitmaps.size(); ++i) {
    if (result.isEmpty()) {
      break;
    }
    result &= *bitmaps[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < bitmaps.size() loop invariant
  }
  return result.cardinality();
}

size_t FilenameTrigramIndex::GetMemoryUsageBytes() const {
  const std::shared_lock lock(GetMutex());
  // Postings storage plus a rough estimate of the map's own structure (bucket
  // array + node headers). Container overhead is not knowable exactly, so treat
  // this as metric-quality; it is 0 for an empty index (see
  // EstimateHashMapStructureBytes for the convention).
  size_t total = EstimateHashMapStructureBytes(posting_lists_);
  for (const auto& [key, bitmap] : posting_lists_) {
    // Inline key + bitmap objects, then the bitmap contents.
    total += sizeof(TrigramKey) + sizeof(roaring::Roaring);
    total += bitmap.getSizeInBytes();
  }
  return total;
}

} // namespace filename_trigram
