#include "search/FolderSizeAggregator.h"

#include "ctrack.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "index/FileIndex.h"
#include "index/NtfsFileReference.h"
#include "utils/FileAttributeConstants.h"
#include "utils/Logger.h"
#include "utils/ThreadUtils.h"

namespace {

// Same cap as PathBuilder::kMaxPathDepth — cycle/corruption guard for parent walks.
constexpr int kMaxAncestorDepth = 64;

// Pass 2 parallel stat tuning. Batches below the threshold run sequentially (thread
// spawn cost would dominate); larger batches stat on up to kMaxStatThreads transient
// threads — the workload is stat() I/O, so a small multiple of cores saturates it.
constexpr size_t kMinMissesForParallelStats = 256;
constexpr unsigned kMaxStatThreads = 8;

[[nodiscard]] bool ContainsVisitedId(const std::array<uint64_t, kMaxAncestorDepth>& visited,
                                     int visited_count, uint64_t id) {
  for (int i = 0; i < visited_count; ++i) {
    if (visited.at(static_cast<size_t>(i)) == id) {
      return true;
    }
  }
  return false;
}

// Stat a chunk of deduplicated Pass 2 miss ids and return (id, size) pairs.
// GetFileSizeById performs the double-check-locked cache update and is safe to call
// concurrently from multiple threads (shared-lock read path, brief unique-lock write).
[[nodiscard]] std::vector<std::pair<uint64_t, uint64_t>> StatMissChunk(
    const std::vector<uint64_t>& misses, size_t begin, size_t end, const FileIndex& index,
    const std::atomic<bool>& cancelled) {
  std::vector<std::pair<uint64_t, uint64_t>> sizes;
    sizes.reserve(end - begin);
    for (size_t i = begin; i < end; ++i) {
      if (cancelled.load()) {
        break;
      }
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - loop-guarded: i < end <= misses.size()
      sizes.emplace_back(misses[i], index.GetFileSizeById(misses[i]));
    }
  return sizes;
}

// Credit file_id to folder_id when that folder is in the current batch. Cache hits
// accumulate immediately; misses store only the id for Pass 2 GetFileSizeById.
void TryCreditFileToBatchFolder(
    uint64_t file_id,
    uint64_t cached_size,
    uint64_t folder_id,
    hash_map_t<uint64_t, FolderSizeAggregator::FolderStats>& stats,
    hash_map_t<uint64_t, std::vector<uint64_t>>& miss_ids_per_dir) {
  const auto it = stats.find(folder_id);
  if (it == stats.end()) {
    return;
  }
  ++it->second.file_count;
  if (cached_size != kFileSizeNotLoaded) {
    it->second.total_size += cached_size;
    return;
  }
  // operator[] is intentional: inserts an empty miss list on first sight of the folder.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - map operator[], not an array index
  miss_ids_per_dir[folder_id].push_back(file_id);
}

// Walk parentID to the volume root and credit every batch folder that contains the file.
// Uses ResolveEntryReference so stale NTFS parent sequence numbers still match job.folder_id_.
// Does not Resolve MFT record 5 (unindexed volume root) — same rule as IndexOperations.
void CreditFileToMatchingAncestors(
    uint64_t file_id,
    uint64_t cached_size,
    uint64_t parent_id,
    const FileIndex& index,
    hash_map_t<uint64_t, FolderSizeAggregator::FolderStats>& stats,
    hash_map_t<uint64_t, std::vector<uint64_t>>& miss_ids_per_dir) {
  uint64_t current = parent_id;
  std::array<uint64_t, kMaxAncestorDepth> visited{};
  int visited_count = 0;
  for (int depth = 0; depth < kMaxAncestorDepth; ++depth) {
    if (current == 0) {
      return;
    }
    // NTFS volume root is usually absent from the index. Resolving it can bind to a
    // synthetic id 5 and credit the wrong folder — credit the raw/record id then stop.
    if (ntfs_file_reference::IsRootDirectoryRecord(current)) {
      TryCreditFileToBatchFolder(file_id, cached_size, current, stats, miss_ids_per_dir);
      if (const uint64_t record = ntfs_file_reference::RecordNumber(current); record != current) {
        TryCreditFileToBatchFolder(file_id, cached_size, record, stats, miss_ids_per_dir);
      }
      return;
    }
    const auto [ancestor, resolved] = index.ResolveEntryReference(current);
    if (ancestor == nullptr) {
      return;
    }
    if (ContainsVisitedId(visited, visited_count, resolved)) {
      return;
    }
    visited.at(static_cast<size_t>(visited_count)) = resolved;
    ++visited_count;
    TryCreditFileToBatchFolder(file_id, cached_size, resolved, stats, miss_ids_per_dir);
    if (resolved == ancestor->parentID.raw) {
      return;  // Self-parented volume root.
    }
    current = ancestor->parentID.raw;
  }
}

}  // namespace

FolderSizeAggregator::FolderSizeAggregator(FileIndex& index)
    : index_(index) {
  worker_thread_ = std::thread(&FolderSizeAggregator::WorkerThread, this);
}

FolderSizeAggregator::~FolderSizeAggregator() {
  {
    const std::scoped_lock lock(mutex_);
    cancelled_ = true;
  }
  cv_.notify_all();
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
}

void FolderSizeAggregator::Request(uint64_t folder_id, std::string_view folder_path) {
  const std::scoped_lock lock(mutex_);

  // No-op if result is already available or job is already queued.
  if (results_.count(folder_id) > 0) {
    return;
  }
  if (pending_requests_.count(folder_id) > 0) {
    return;
  }

  pending_requests_.emplace(folder_id);
  queue_.push_back({folder_id, std::string(folder_path), generation_});
  cv_.notify_one();
}

void FolderSizeAggregator::RequestBatch(
    const std::vector<std::pair<uint64_t, std::string_view>>& folders) {
  if (folders.empty()) {
    return;
  }
  const std::scoped_lock lock(mutex_);
  bool any_new = false;
  for (const auto& [id, path] : folders) {
    if (results_.count(id) > 0) {
      continue;
    }
    if (pending_requests_.count(id) > 0) {
      continue;
    }
    pending_requests_.emplace(id);
    queue_.push_back({id, std::string(path), generation_});
    any_new = true;
  }
  if (any_new) {
    cv_.notify_one();
  }
}

std::optional<FolderSizeAggregator::FolderStats> FolderSizeAggregator::GetResult(uint64_t folder_id) const {
  const std::shared_lock lock(mutex_);
  if (const auto it = results_.find(folder_id); it != results_.end()) {
    return it->second;
  }
  return std::nullopt;
}

void FolderSizeAggregator::CancelPending() {
  const std::scoped_lock lock(mutex_);
  ++generation_;  // Invalidates any in-flight job result — worker discards on mismatch.
  queue_.clear();
  pending_requests_.clear();
  // results_ intentionally kept: folders reappearing in the next search reuse
  // cached values immediately without queuing a new computation.
}

void FolderSizeAggregator::Reset() {
  const std::scoped_lock lock(mutex_);
  ++generation_;
  queue_.clear();
  pending_requests_.clear();
  results_.clear();
}

bool FolderSizeAggregator::HasPendingWork() const {
  const std::shared_lock lock(mutex_);
  return !queue_.empty() || !pending_requests_.empty();
}

void FolderSizeAggregator::WorkerThread() {
  SetThreadName("FolderSizeAgg");

  while (true) {
    std::vector<Job> batch;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] { return !queue_.empty() || cancelled_; });

      if (cancelled_) {
        return;
      }

      // Drain all currently queued jobs so the entire batch is processed in one
      // index scan rather than one scan per directory (O(K × N) → O(N × depth)).
      while (!queue_.empty()) {
        batch.push_back(std::move(queue_.front()));
        queue_.pop_front();
      }
    }

    // Single index scan for the whole batch.
    const auto sizes = ComputeSizeBatch(batch);

    {
      const std::scoped_lock lock(mutex_);
      if (cancelled_) {
        return;
      }
      for (const auto& job : batch) {
        // Discard result if Reset()/CancelPending() was called while we were computing.
        // Only erase from pending_requests_ on a generation match: if CancelPending() was
        // called and the same folder_id was re-requested (new generation), an unconditional
        // erase would remove the new request's entry, causing HasPendingWork() to return
        // false prematurely while the new job is still in-flight.
        if (job.generation_ == generation_) {
          // A folder_id must never appear twice: Request() is a no-op when
          // folder_id is already in results_ or pending_requests_.
          assert(results_.find(job.folder_id_) == results_.end() &&
                 "FolderSizeAggregator: duplicate result for folder_id");
          const auto it = sizes.find(job.folder_id_);
          results_.try_emplace(job.folder_id_, it != sizes.end() ? it->second : FolderStats{});
          pending_requests_.erase(job.folder_id_);
        }
        // Stale-generation jobs: pending_requests_ was already cleared by CancelPending()/
        // Reset(), so no cleanup is needed.
      }
    }
  }
}

// Deduplicate cached-miss file ids across folders, sorted unique (in-place output).
[[nodiscard]] std::vector<uint64_t> CollectUniqueMissIds(
    const hash_map_t<uint64_t, std::vector<uint64_t>>& miss_ids_per_dir) {
  size_t total_misses = 0;
  for (const auto& [_, miss_ids] : miss_ids_per_dir) {
    total_misses += miss_ids.size();
  }
  std::vector<uint64_t> unique_misses;
  unique_misses.reserve(total_misses);
  for (const auto& [_, miss_ids] : miss_ids_per_dir) {
    unique_misses.insert(unique_misses.end(), miss_ids.begin(), miss_ids.end());
  }
  std::sort(unique_misses.begin(), unique_misses.end());
  unique_misses.erase(std::unique(unique_misses.begin(), unique_misses.end()),
                      unique_misses.end());
  return unique_misses;
}

// Stat every unique miss id and return size by file id.
//
// INVARIANT: transient std::thread, NOT the shared SearchThreadPool — deliberate.
// The pool is shared with sort-attribute tasks, and the UI spin-waits for those tasks
// to drain (WaitForAllAttributeLoadingFutures / WaitForCancelledTasksFully). Aggregator
// chunk tasks do not check the sort cancellation token, so pool-queued stat chunks
// would stall a user-triggered sort drain (and queue user searches behind ~10k stat
// tasks) for the batch duration. Transient threads keep the batch isolated in both
// directions; the pool is sized to the same thread count, so there is no throughput
// gain from using it. Thread cost (~8 spawns per multi-second batch) is negligible.
[[nodiscard]] hash_map_t<uint64_t, uint64_t> StatUniqueMisses(
    const FileIndex& index, const std::vector<uint64_t>& unique_misses,
    const std::atomic<bool>& cancelled) {
  hash_map_t<uint64_t, uint64_t> miss_size_by_id;
  if (unique_misses.size() < kMinMissesForParallelStats) {
    for (const uint64_t fid : unique_misses) {
      if (cancelled.load()) {
        break;
      }
      miss_size_by_id.emplace(fid, index.GetFileSizeById(fid));
    }
    return miss_size_by_id;
  }

  const unsigned thread_count = std::clamp(std::thread::hardware_concurrency(), 1U, kMaxStatThreads);
  std::vector<std::thread> stat_threads;
  std::vector<std::vector<std::pair<uint64_t, uint64_t>>> chunk_results(thread_count);
  const size_t chunk_size = (unique_misses.size() + thread_count - 1) / thread_count;
  try {
    for (unsigned t = 0; t < thread_count; ++t) {
      const size_t begin = t * chunk_size;
      if (begin >= unique_misses.size()) {
        break;
      }
      const size_t end = (std::min)(begin + chunk_size, unique_misses.size());
      stat_threads.emplace_back([&unique_misses, &chunk_results, t, begin, end, &index, &cancelled] {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - loop-guarded: t < thread_count == chunk_results.size()
        chunk_results[t] = StatMissChunk(unique_misses, begin, end, index, cancelled);
      });
    }
  } catch (const std::system_error& e) {
    // std::thread::emplace_back / join failures are the only exceptions this
    // loop can raise; drain stat threads (never onto destroyed chunk_results),
    // then propagate.
    LOG_ERROR_BUILD("FolderSizeAggregator stat thread launch failed: " << e.what());
    for (std::thread& thread : stat_threads) {
      if (thread.joinable()) {
        thread.join();
      }
    }
    throw;
  }
  for (std::thread& thread : stat_threads) {
    thread.join();
  }
  for (const std::vector<std::pair<uint64_t, uint64_t>>& chunk : chunk_results) {
    miss_size_by_id.insert(chunk.begin(), chunk.end());
  }
  return miss_size_by_id;
}

// Credit stat()ed miss sizes to each folder that requested them.
void CreditMissSizesToDirectories(
    const hash_map_t<uint64_t, std::vector<uint64_t>>& miss_ids_per_dir,
    const hash_map_t<uint64_t, uint64_t>& miss_size_by_id,
    hash_map_t<uint64_t, FolderSizeAggregator::FolderStats>& result,
    const std::atomic<bool>& cancelled) {
  for (const auto& [folder_id, miss_ids] : miss_ids_per_dir) {
    const auto stats_it = result.find(folder_id);
    if (stats_it == result.end()) {
      continue;
    }
    for (const uint64_t fid : miss_ids) {
      if (cancelled.load()) {
        break;
      }
      const auto size_it = miss_size_by_id.find(fid);
      if (size_it != miss_size_by_id.end() && size_it->second != kFileSizeNotLoaded &&
          size_it->second != kFileSizeFailed) {
        stats_it->second.total_size += size_it->second;
      }
    }
  }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
hash_map_t<uint64_t, FolderSizeAggregator::FolderStats> FolderSizeAggregator::ComputeSizeBatch(
    const std::vector<Job>& jobs) const {
  CTRACK_NAME("FolderSizeAggregator::ComputeSizeBatch");  // NOLINT(misc-const-correctness)
  hash_map_t<uint64_t, FolderStats> result;
  result.reserve(jobs.size());
  for (const auto& job : jobs) {
    result.try_emplace(job.folder_id_, FolderStats{});
  }

  // Pass 1: single index scan — walk parentID (not path prefixes). Cache hits
  // accumulate here. GetFileSizeById must NOT be called inside this callback —
  // it would acquire the same lock already held by ForEachEntry.
  hash_map_t<uint64_t, std::vector<uint64_t>> miss_ids_per_dir;
  index_.ForEachEntry(
      [this, &result, &miss_ids_per_dir](uint64_t id, const FileEntry& entry) {
        if (cancelled_.load()) {
          return false;
        }
        if (entry.isDirectory) {
          return true;
        }
        const uint64_t cached_size =
            entry.fileSize.IsLoaded() ? entry.fileSize.GetValue() : kFileSizeNotLoaded;
        CreditFileToMatchingAncestors(id, cached_size, entry.parentID.raw, index_, result,
                                      miss_ids_per_dir);
        return true;
      });

  // Pass 2: stat() only for files that were not cached when Pass 1 ran.
  // Nested batch folders share descendants — dedupe miss ids, then stat each once.
  // A large requested folder spans most of the index, so this pass dominates the
  // batch; run the stats in parallel when there is enough work to amortize the
  // thread spawns.
  const std::vector<uint64_t> unique_misses = CollectUniqueMissIds(miss_ids_per_dir);
  const hash_map_t<uint64_t, uint64_t> miss_size_by_id =
      StatUniqueMisses(index_, unique_misses, cancelled_);

  // Credit folders from the deduplicated results.
  CreditMissSizesToDirectories(miss_ids_per_dir, miss_size_by_id, result, cancelled_);
  return result;
}
