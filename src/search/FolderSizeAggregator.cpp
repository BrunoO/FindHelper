#include "search/FolderSizeAggregator.h"

#include <cassert>
#include <vector>

#include "index/FileIndex.h"
#include "utils/FileAttributeConstants.h"
#include "utils/ThreadUtils.h"

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

std::optional<uint64_t> FolderSizeAggregator::GetResult(uint64_t folder_id) const {
  const std::scoped_lock lock(mutex_);
  if (auto it = results_.find(folder_id); it != results_.end()) {
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
  const std::scoped_lock lock(mutex_);
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
        if (job.generation == generation_) {
          // A folder_id must never appear twice: Request() is a no-op when
          // folder_id is already in results_ or pending_requests_.
          assert(results_.find(job.folder_id) == results_.end() &&
                 "FolderSizeAggregator: duplicate result for folder_id");
          const auto it = sizes.find(job.folder_id);
          results_.emplace(job.folder_id, it != sizes.end() ? it->second : uint64_t{0});
          pending_requests_.erase(job.folder_id);
        }
        // Stale-generation jobs: pending_requests_ was already cleared by CancelPending()/
        // Reset(), so no cleanup is needed.
      }
    }
  }
}

hash_map_t<uint64_t, uint64_t> FolderSizeAggregator::ComputeSizeBatch(
    const std::vector<Job>& jobs) const {
  // Map prefix (folder_path + "/") → folder_id for O(1) lookup per path level.
  hash_map_t<std::string, uint64_t> prefix_to_id;
  prefix_to_id.reserve(jobs.size());
  for (const auto& job : jobs) {
    prefix_to_id.emplace(job.folder_path + "/", job.folder_id);
  }

  // Pass 1: single index scan — for each file, walk up its path to find all
  // batch directories that contain it and collect their descendant file IDs.
  // GetFileSizeById must NOT be called inside this callback — it acquires the
  // same lock held by ForEachEntryWithPath.
  // Reuse a string buffer across calls to avoid per-level heap allocations.
  hash_map_t<uint64_t, std::vector<uint64_t>> file_ids_per_dir;
  std::string lookup_key;
  lookup_key.reserve(256);

  index_.ForEachEntryWithPath(
      [this, &file_ids_per_dir, &prefix_to_id, &lookup_key](
          uint64_t id, const FileEntry& entry, std::string_view path) {
        if (cancelled_.load(std::memory_order_relaxed)) {
          return false;
        }
        if (entry.isDirectory) {
          return true;
        }
        // Walk up path levels, checking each ancestor against the batch prefix map.
        std::string_view remaining = path;
        while (true) {
          const auto slash = remaining.rfind('/');
          if (slash == std::string_view::npos) {
            break;
          }
          lookup_key.assign(remaining.data(), slash + 1);  // NOLINT(bugprone-suspicious-stringview-data-usage) - assign(ptr, count) is safe; count is always ≤ remaining.size()
          if (const auto it = prefix_to_id.find(lookup_key); it != prefix_to_id.end()) {
            file_ids_per_dir[it->second].push_back(id);
          }
          remaining = remaining.substr(0, slash);
        }
        return true;
      },
      nullptr);

  // Pass 2: load sizes outside the lock and sum per directory.
  hash_map_t<uint64_t, uint64_t> result;
  result.reserve(jobs.size());
  for (const auto& [folder_id, file_ids] : file_ids_per_dir) {
    uint64_t total = 0;
    for (const uint64_t fid : file_ids) {
      if (cancelled_.load(std::memory_order_relaxed)) {
        break;
      }
      const uint64_t size = index_.GetFileSizeById(fid);
      if (size != kFileSizeNotLoaded && size != kFileSizeFailed) {
        total += size;
      }
    }
    result.emplace(folder_id, total);
  }
  // Ensure all job folder_ids have an entry (directories with no files get 0).
  for (const auto& job : jobs) {
    result.try_emplace(job.folder_id, uint64_t{0});
  }
  return result;
}
