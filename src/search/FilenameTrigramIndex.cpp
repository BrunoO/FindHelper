#include "search/FilenameTrigramIndex.h"
#include "search/SearchThreadPool.h"
#include "utils/Logger.h"
#include "utils/SortAliases.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <vector>

namespace filename_trigram {
namespace {

// Initial capacity for flat hash map to avoid early rehashing across unique trigrams.
constexpr size_t kInitialPostingMapCapacity = 65'536;

// Upper safety cap for vector reservation to avoid runaway memory allocations on pathological corpora.
// 32,000,000 postings ≈ 256 MB transient allocation, comfortably bounding memory while covering
// up to ~2,000,000 files without vector reallocations.
constexpr size_t kMaxPostingReservation = 32'000'000;

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

  // Pre-pass: Compute exact mathematical upper bound of unique trigrams across all entries.
  // For any filename of length L, max unique trigrams is max(0, L - 2).
  // Pre-allocating this exact bound guarantees zero vector reallocations and eliminates
  // the transient peak memory and memcpy copy penalty on large file indexes.
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

  if (max_postings > kMaxPostingReservation) {
    LOG_IMPORTANT_BUILD("FilenameTrigramIndex::ExtractPostings: clamping reserve from "
                        << max_postings << " to " << kMaxPostingReservation << " postings");
  }

  std::vector<uint64_t> postings;
  postings.reserve((std::min)(max_postings, kMaxPostingReservation));

  for (size_t i = 0; i < soa_view.size; ++i) {
    if (soa_view.is_deleted[i] != 0) {
      continue;
    }

    const size_t filename_offset = soa_view.filename_start[i];
    const size_t path_len = soa_view.GetPathLength(i, storage_size);
    if (filename_offset + 3 > path_len) {
      continue;
    }

    const size_t fn_len = path_len - filename_offset;
    const char* const fn = soa_view.path_storage + soa_view.path_offsets[i] + filename_offset;
    const auto row_id = static_cast<uint64_t>(static_cast<uint32_t>(i));
    uint32_t key = (static_cast<uint32_t>(static_cast<unsigned char>(
                        string_search::ToLowerChar(fn[0]))) << 8U) |
                   static_cast<uint32_t>(static_cast<unsigned char>(
                        string_search::ToLowerChar(fn[1])));
    for (size_t j = 2; j < fn_len; ++j) {
      const auto c = static_cast<uint32_t>(static_cast<unsigned char>(
          string_search::ToLowerChar(fn[j])));
      key = ((key & kLow16BitsMask) << 8U) | c;
      postings.push_back((static_cast<uint64_t>(key) << 32U) | row_id);
    }
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
                      << " MB)");

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

    {
      const std::unique_lock lock(mutex_);
      if (generation != build_generation_.load()) {
        const auto elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start_time)
                .count());
        LogBenchmarkDuration("FilenameTrigramIndex build (stale)", elapsed);
        LOG_IMPORTANT_BUILD("FilenameTrigramIndex: build generation "
                            << generation << " is stale (active: "
                            << build_generation_.load() << "), discarding results");
        return;
      }
      posting_lists_ = std::move(new_posting_lists);
      unique_trigrams = posting_lists_.size();
      pending_mutations_count = pending_mutations_.size();
      for (const auto& m : pending_mutations_) {
        if (m.is_add) {
          posting_lists_[m.trigram].add(m.row_id);
        } else if (const auto it = posting_lists_.find(m.trigram); it != posting_lists_.end()) {
          it->second.remove(m.row_id);
        }
      }
      pending_mutations_.clear();
      is_building_.store(false);
      is_ready_.store(true);
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

void FilenameTrigramIndex::AbortBuild(uint64_t generation) noexcept {
  try {
    const std::unique_lock lock(mutex_);
    if (generation == build_generation_.load()) {
      for (const auto& m : pending_mutations_) {
        if (m.is_add) {
          posting_lists_[m.trigram].add(m.row_id);
        } else if (const auto it = posting_lists_.find(m.trigram); it != posting_lists_.end()) {
          it->second.remove(m.row_id);
        }
      }
      const size_t replayed_count = pending_mutations_.size();
      pending_mutations_.clear();
      is_building_.store(false);
      is_ready_.store(!posting_lists_.empty());
      LOG_IMPORTANT_BUILD("FilenameTrigramIndex: build generation " << generation
                          << " aborted; replayed " << replayed_count
                          << " mutations, ready=" << (!posting_lists_.empty()));
    }
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - catch-all safety net; must not throw out of a noexcept recovery path
    (void)e;
    LOG_ERROR_BUILD("FilenameTrigramIndex::AbortBuild exception: " << e.what());
  } catch (...) {  // NOSONAR(cpp:S2738) - catch-all required: AbortBuild is noexcept and must not propagate
    LOG_ERROR_BUILD("FilenameTrigramIndex::AbortBuild unknown exception");
  }
}

FilenameTrigramIndex::~FilenameTrigramIndex() {
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
  const std::scoped_lock submit_lock(build_submit_mutex_);
  // At most one build may be in flight, so that ~FilenameTrigramIndex waiting on
  // build_future_ is sufficient to guarantee no task outlives this object.
  // std::future::wait() throws only std::future_error when the future has no
  // associated shared state, which valid() already excludes.
  if (build_future_.valid()) {
    build_future_.wait();
  }
  // Another BeginRebuild (or Clear) advanced the generation while we waited.
  // Enqueueing now would burn a full sort-and-populate cycle on a task that
  // BuildFromPostings would discard on its first line. Every generation advance
  // either has a matching SubmitBuild from its own caller, or is Clear()/the
  // destructor -- and both of those already want nothing committed.
  if (generation != build_generation_.load()) {
    return;
  }
  LOG_IMPORTANT_BUILD("FilenameTrigramIndex::SubmitBuild: offloading trigram index build ("
                      << postings.size() << " postings, "
                      << (postings.size() * sizeof(uint64_t) / logger_constants::kBytesPerMB)
                      << " MB) to background thread pool");
  build_future_ = thread_pool->Enqueue([this, gen = generation, p = std::move(postings)]() mutable {
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
  const std::unique_lock lock(mutex_);
  ++build_generation_;
  posting_lists_.clear();
  pending_mutations_.clear();
  is_building_.store(false);
  is_ready_.store(true);
}

void FilenameTrigramIndex::AddEntry(uint32_t row_id, std::string_view filename) {
  std::vector<TrigramKey> trigrams;
  ExtractTrigrams(filename, trigrams);
  const std::unique_lock lock(mutex_);
  if (is_building_.load()) {
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
  const std::unique_lock lock(mutex_);
  if (is_building_.load()) {
    for (const TrigramKey trigram : trigrams) {
      pending_mutations_.push_back({trigram, row_id, false});
    }
    return;
  }
  for (const TrigramKey trigram : trigrams) {
    if (const auto it = posting_lists_.find(trigram); it != posting_lists_.end()) {
      it->second.remove(row_id);
    }
  }
}

std::optional<roaring::Roaring> FilenameTrigramIndex::QueryCandidates(
    const std::vector<TrigramKey>& required_trigrams) const {
  if (!is_ready_.load() || required_trigrams.empty()) {
    return std::nullopt;
  }

  const std::shared_lock lock(mutex_);
  std::vector<const roaring::Roaring*> bitmaps;
  bitmaps.reserve(required_trigrams.size());

  for (const TrigramKey trigram : required_trigrams) {
    const auto it = posting_lists_.find(trigram);
    if (it == posting_lists_.end() || it->second.isEmpty()) {
      return std::nullopt; // Early exit: intersection will be empty
    }
    bitmaps.push_back(&it->second);
  }

  // Sort bitmaps by cardinality for efficient intersection
  std::sort(bitmaps.begin(), bitmaps.end(),
            [](const roaring::Roaring* a, const roaring::Roaring* b) {
              return a->cardinality() < b->cardinality();
            });

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

size_t FilenameTrigramIndex::GetMemoryUsageBytes() const {
  const std::shared_lock lock(mutex_);
  size_t total = 0;
  for (const auto& [key, bitmap] : posting_lists_) {
    total += sizeof(TrigramKey) + sizeof(roaring::Roaring);
    total += bitmap.getSizeInBytes();
  }
  return total;
}

} // namespace filename_trigram
