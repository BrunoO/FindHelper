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
    Job job;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] { return !queue_.empty() || cancelled_; });

      if (cancelled_) {
        return;
      }

      job = std::move(queue_.front());
      queue_.pop_front();
    }

    const uint64_t size = ComputeSize(job.folder_path);

    {
      const std::scoped_lock lock(mutex_);
      if (cancelled_) {
        return;
      }
      // Discard result if Reset() was called while we were computing.
      if (job.generation == generation_) {
        // A folder_id must never appear twice: Request() is a no-op when
        // folder_id is already in results_ or pending_requests_.
        assert(results_.find(job.folder_id) == results_.end() &&
               "FolderSizeAggregator: duplicate result for folder_id");
        results_.emplace(job.folder_id, size);
      }
      pending_requests_.erase(job.folder_id);
    }
  }
}

uint64_t FolderSizeAggregator::ComputeSize(std::string_view folder_path) const {
  std::vector<uint64_t> file_ids;
  const std::string prefix = std::string(folder_path) + "/";

  // Pass 1: collect descendant file IDs under the shared lock held by ForEachEntryWithPath.
  // GetFileSizeById must NOT be called inside this callback — it acquires the same lock.
  index_.ForEachEntryWithPath(
      [this, &file_ids, &prefix](uint64_t id, const FileEntry& entry,
                                  std::string_view path) {
        if (cancelled_.load(std::memory_order_relaxed)) {
          return false;  // Stop iteration on shutdown.
        }
        if (!entry.isDirectory && path.size() > prefix.size() &&
            path.compare(0, prefix.size(), prefix) == 0) {
          file_ids.push_back(id);
        }
        return true;
      },
      nullptr);

  // Pass 2: load sizes outside the lock and sum.
  uint64_t total_size = 0;
  for (const uint64_t id : file_ids) {
    if (cancelled_.load(std::memory_order_relaxed)) {
      break;
    }
    const uint64_t size = index_.GetFileSizeById(id);
    if (size != kFileSizeNotLoaded && size != kFileSizeFailed) {
      total_size += size;
    }
  }

  return total_size;
}
