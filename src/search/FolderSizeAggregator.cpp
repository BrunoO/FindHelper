#include "search/FolderSizeAggregator.h"

#include <cassert>
#include <shared_mutex>
#include <vector>

#include "index/FileIndex.h"
#include "path/PathUtils.h"
#include "utils/FileAttributeConstants.h"
#include "utils/ThreadUtils.h"

namespace {

#ifdef _WIN32
// Win32 accepts '/' and '\\' in paths; PathBuilder uses '\\' but some callers may mix.
// Normalize to '\\' so prefix_to_id keys match lookup_key (see ComputeSizeBatch).
void NormalizeWindowsSeparatorsInPlace(std::string& path) {
  for (char& c : path) {
    if (c == '/') {
      c = '\\';
    }
  }
}
#endif  // _WIN32

// Walk up path levels and add (file_id, cached_size) to every batch directory that contains it.
// cached_size is the size read from FileEntry under the index read lock (kFileSizeNotLoaded if not
// yet cached). Pass 2 uses cached_size directly to avoid stat() for already-loaded files.
// Reuses lookup_key to avoid per-level allocations. Used by ComputeSizeBatch Pass 1.
void AccumulateAncestorDirsForFile(
    uint64_t id,
    uint64_t cached_size,
    std::string_view path,
    const hash_map_t<std::string, uint64_t>& prefix_to_id,
    hash_map_t<uint64_t, std::vector<std::pair<uint64_t, uint64_t>>>& file_data_per_dir,
    std::string& lookup_key) {
#ifdef _WIN32
  // Canonical paths from PathBuilder use '\\' only — skip per-level '/' scans when possible.
  const bool path_has_forward_slash = (path.find('/') != std::string_view::npos);
#endif  // _WIN32
  std::string_view remaining = path;
  while (true) {
    // Match PathUtils::GetFilename — treat '/' and '\\' as separators (mixed paths on Windows).
    const auto slash = remaining.find_last_of("/\\");
    if (slash == std::string_view::npos) {
      break;
    }
    lookup_key.assign(remaining.data(), slash + 1);  // NOLINT(bugprone-suspicious-stringview-data-usage) - assign(ptr, count) is safe; count is always ≤ remaining.size()
#ifdef _WIN32
    if (path_has_forward_slash) {
      NormalizeWindowsSeparatorsInPlace(lookup_key);
    }
#endif  // _WIN32
    if (const auto it = prefix_to_id.find(lookup_key); it != prefix_to_id.end()) {
      file_data_per_dir[it->second].emplace_back(id, cached_size);
    }
    remaining = remaining.substr(0, slash);
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
        if (job.generation == generation_) {
          // A folder_id must never appear twice: Request() is a no-op when
          // folder_id is already in results_ or pending_requests_.
          assert(results_.find(job.folder_id) == results_.end() &&
                 "FolderSizeAggregator: duplicate result for folder_id");
          const auto it = sizes.find(job.folder_id);
          results_.emplace(job.folder_id, it != sizes.end() ? it->second : FolderStats{});
          pending_requests_.erase(job.folder_id);
        }
        // Stale-generation jobs: pending_requests_ was already cleared by CancelPending()/
        // Reset(), so no cleanup is needed.
      }
    }
  }
}

hash_map_t<uint64_t, FolderSizeAggregator::FolderStats> FolderSizeAggregator::ComputeSizeBatch(
    const std::vector<Job>& jobs) const {
  // Map prefix (folder_path + "/") → folder_id for O(1) lookup per path level.
  hash_map_t<std::string, uint64_t> prefix_to_id;
  prefix_to_id.reserve(jobs.size());
  for (const auto& job : jobs) {
    std::string prefix_key;
    prefix_key.reserve(job.folder_path.size() + 1U);
    prefix_key.append(job.folder_path);
    prefix_key.append(path_utils::kPathSeparatorStr);
#ifdef _WIN32
    if (job.folder_path.find('/') != std::string::npos) {
      NormalizeWindowsSeparatorsInPlace(prefix_key);
    }
#endif  // _WIN32
    prefix_to_id.emplace(std::move(prefix_key), job.folder_id);
  }

  // Pass 1: single index scan — for each file, walk up its path to find all
  // batch directories that contain it and collect their descendant (file_id, cached_size) pairs.
  // cached_size is read from FileEntry under the index read lock held by ForEachEntryWithPath.
  // If the size is already loaded (e.g. pre-loaded by sort attribute tasks), Pass 2 uses it
  // directly without a stat() call. GetFileSizeById must NOT be called inside this callback —
  // it would acquire the same lock already held by ForEachEntryWithPath.
  // Reuse a string buffer across calls to avoid per-level heap allocations.
  hash_map_t<uint64_t, std::vector<std::pair<uint64_t, uint64_t>>> file_data_per_dir;
  std::string lookup_key;
  lookup_key.reserve(256);

  index_.ForEachEntryWithPath(
      [this, &file_data_per_dir, &prefix_to_id, &lookup_key](
          uint64_t id, const FileEntry& entry, std::string_view path) {
        if (cancelled_.load()) {
          return false;
        }
        if (entry.isDirectory) {
          return true;
        }
        // Read the cached size while under the index read lock. IsLoaded() is true when
        // another thread has already stat()'d this file (e.g. sort attribute loading).
        const uint64_t cached_size =
            entry.fileSize.IsLoaded() ? entry.fileSize.GetValue() : kFileSizeNotLoaded;
        AccumulateAncestorDirsForFile(id, cached_size, path, prefix_to_id, file_data_per_dir, lookup_key);
        return true;
      },
      nullptr);

  // Pass 2: sum sizes and count files — use cached values captured in Pass 1 where available,
  // stat() only for files that were not yet loaded when Pass 1 ran.
  hash_map_t<uint64_t, FolderStats> result;
  result.reserve(jobs.size());
  for (const auto& [folder_id, file_data] : file_data_per_dir) {
    uint64_t total = 0;
    const uint64_t count = file_data.size();  // Each entry is one non-directory file.
    for (const auto& [fid, cached_size] : file_data) {
      if (cancelled_.load()) {
        break;
      }
      const uint64_t size = (cached_size != kFileSizeNotLoaded)
                                ? cached_size           // Cache hit: already loaded, no stat().
                                : index_.GetFileSizeById(fid);  // Cache miss: lazy load via stat().
      if (size != kFileSizeNotLoaded && size != kFileSizeFailed) {
        total += size;
      }
    }
    result.emplace(folder_id, FolderStats{total, count});
  }
  // Ensure all job folder_ids have an entry (directories with no files get zeros).
  for (const auto& job : jobs) {
    result.try_emplace(job.folder_id, FolderStats{});
  }
  return result;
}
