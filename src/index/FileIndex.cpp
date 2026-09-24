#include "index/FileIndex.h"

#include "ctrack.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <functional>
#include <future>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#include "core/Settings.h"
#include "index/FileIndexMaintenance.h"
#include "index/IndexDomainEvents.h"
#include "index/NtfsFileReference.h"
#include "search/ParallelSearchEngine.h"
#include "search/SearchContext.h"
#include "search/SearchContextBuilder.h"
#include "search/SearchThreadPool.h"
#include "utils/HashMapAliases.h"
#include "utils/Logger.h"
#include "utils/StdRegexUtils.h"
#include "utils/StringSearch.h"

#include <queue>
#include <vector>

// FileIndex constructor: thread pool lifecycle delegated to SearchThreadPoolManager (Option C)
FileIndex::FileIndex(std::shared_ptr<SearchThreadPool> thread_pool)  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - members use in-class initialization; index_mutex_ is initialized in header
    : storage_(index_mutex_),
      lazy_loader_(storage_, path_storage_, index_mutex_),
      path_operations_(storage_, path_storage_),
      path_recomputer_(storage_, path_storage_, path_operations_),
      maintenance_(path_storage_, index_mutex_,
                    [this] { return this->Size(); },
                   [this](uint64_t id, size_t idx) { storage_.SetPathStorageIndex(id, idx); },
                   remove_not_in_index_count_,
                   remove_duplicate_count_,
                   remove_inconsistency_count_),
      operations_(storage_, path_operations_,
                  remove_not_in_index_count_,
                  remove_inconsistency_count_,
                  [this](const index_domain_events::ParentHealed&) {
                    healed_awaiting_total_.fetch_add(1);
                  },
                  [this](uint64_t id, std::string_view old_path, std::string_view new_path) {
                    this->RefreshPathToIdAfterPathUpdateLocked(id, old_path, new_path);
                  }),
      directory_resolver_(storage_, operations_, next_file_id_),
      thread_pool_manager_(std::move(thread_pool)),
      search_engine_(std::make_shared<ParallelSearchEngine>(
          thread_pool_manager_.GetPoolSharedPtrWithoutCreating()
              ? thread_pool_manager_.GetPoolSharedPtrWithoutCreating()
              : std::make_shared<SearchThreadPool>(0))) {
  // Pre-reserve path dedup structures for the typical 500K-file target.
  // Matches PathStorage::kInitialPathArrayCapacity (private). Without this,
  // path_to_id_chain_ undergoes ~19 geometric reallocs (each a memmove of all
  // previous entries) while crawling 500K files.
  static constexpr size_t kInitialCapacity = 500000U;
  path_to_id_chain_.reserve(kInitialCapacity);
  path_to_id_index_.reserve(kInitialCapacity);
}

namespace {

// Hash a raw path for dedup without allocating: normalizes (backslash→slash,
// no trailing slash) inline using FNV-1a-64. Zero heap allocation per call.
// Replaces the previous NormalizePathForDedup(path) + PathHash(norm) pattern
// which heap-allocated a std::string on every insert (fix #6).
inline size_t PathHashInline(std::string_view raw) {
  size_t n = raw.size();
  while (n > 0 && (raw[n - 1] == '/' || raw[n - 1] == '\\')) { --n; }  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - n > 0 guarantees n-1 is valid
  // FNV-1a 64-bit constants (used for both 32-bit and 64-bit size_t;
  // path_to_id_ is never persisted so the hash function is internal-only).
  static constexpr size_t kFnvOffsetBasis = 14695981039346656037ULL;
  static constexpr size_t kFnvPrime       = 1099511628211ULL;
  size_t hash = kFnvOffsetBasis;
  for (size_t i = 0; i < n; ++i) {
    const auto c = static_cast<unsigned char>((raw[i] == '\\') ? '/' : raw[i]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < n loop invariant
    hash ^= c;  // integral promotion to size_t
    hash *= kFnvPrime;
  }
  return hash;
}

[[nodiscard]] bool IsTrueVolumeRootParent(uint64_t parent_id, uint64_t id) noexcept {
  return parent_id == 0 || parent_id == id;
}

void EnqueueOrphanChildrenForRemoval(
    const flat_hash_map_t<uint64_t, std::vector<uint64_t>>& children,
    uint64_t parent_id,
    hash_set_t<uint64_t>& to_remove,
    std::queue<uint64_t>& pending) {
  const auto it = children.find(parent_id);
  if (it == children.end()) {
    return;
  }
  for (const uint64_t child_id : it->second) {
    if (to_remove.insert(child_id).second) {
      pending.push(child_id);
    }
  }
}

void PruneBrokenParentSubtreesLocked(const FileIndexStorage& storage,
                                     IndexOperations& operations) {
  flat_hash_map_t<uint64_t, std::vector<uint64_t>> children;
  children.reserve(storage.Size() / 8U);  // NOLINT(readability-magic-numbers) - heuristic
  std::vector<uint64_t> orphan_roots;
  orphan_roots.reserve(64U);  // NOLINT(readability-magic-numbers) - expected small orphan count

  for (const auto& [id, entry] : storage) {
    if (IsTrueVolumeRootParent(entry.parentID.raw, id.raw)) {
      continue;
    }
    // Always exempt volume-root children (parent MFT record 5). Do this before
    // ResolveEntryReference: resolving can bind to a synthetic id or a poisoned
    // "." volume-root entry; if that parent is later pruned as an orphan, every
    // C:\* drive-root file would be evacuated with it.
    if (ntfs_file_reference::IsRootDirectoryRecord(entry.parentID.raw)) {
      continue;
    }
    const auto [parent_entry, resolved_parent_id] = storage.ResolveEntryReference(entry.parentID.raw);
    if (parent_entry == nullptr) {
      orphan_roots.push_back(id.raw);
      continue;
    }
    children[resolved_parent_id].push_back(id.raw);
  }

  if (orphan_roots.empty()) {
    return;
  }

  hash_set_t<uint64_t> to_remove;
  to_remove.reserve(orphan_roots.size());
  std::queue<uint64_t> pending;
  for (const uint64_t root_id : orphan_roots) {
    if (to_remove.insert(root_id).second) {
      pending.push(root_id);
    }
  }
  while (!pending.empty()) {
    const uint64_t current_id = pending.front();
    pending.pop();
    EnqueueOrphanChildrenForRemoval(children, current_id, to_remove, pending);
  }

  LOG_WARNING_BUILD("PruneOrphanSubtrees: removing "
                    << to_remove.size()
                    << " entries with broken parent chains (e.g. phantom C:\\\\name paths)");

  for (const uint64_t id : to_remove) {
    operations.Remove(ntfs_file_reference::NtfsFileReference(id));
  }
}

// Compare two raw (un-normalized) paths for equality after normalization
// (backslash→slash, trim trailing separators). Both sides are normalized
// on-the-fly — no allocation needed. Used in the InsertPathUnderLock
// collision chain (fix #6).
inline bool PathViewsEqualNormalized(std::string_view a, std::string_view b) {
  size_t a_len = a.size();
  while (a_len > 0 && (a[a_len - 1] == '/' || a[a_len - 1] == '\\')) { --a_len; }  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - a_len > 0 guarantees a_len-1 is valid
  size_t b_len = b.size();
  while (b_len > 0 && (b[b_len - 1] == '/' || b[b_len - 1] == '\\')) { --b_len; }  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - b_len > 0 guarantees b_len-1 is valid
  if (a_len != b_len) { return false; }
  for (size_t i = 0; i < a_len; ++i) {
    if (((a[i] == '\\') ? '/' : a[i]) != ((b[i] == '\\') ? '/' : b[i])) { return false; }  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < a_len == b_len loop invariant
  }
  return true;
}

void LogInsertPathErrorAndIncrement(
    [[maybe_unused]] std::string_view message, [[maybe_unused]] std::string_view path,
    [[maybe_unused]] const char* detail, std::atomic<size_t>* out_error_count) {
  LOG_WARNING_BUILD("FileIndex: " << message << " inserting path: " << path
                    << (detail != nullptr && *detail != '\0' ? std::string(" - ") + detail
                                                              : ""));
  if (out_error_count != nullptr) {
    out_error_count->fetch_add(1);
  }
}

}  // namespace

// AppendString is now handled by PathStorage - removed

// Insert a path into the contiguous storage buffer (Structure of Arrays
// design). This method maintains the contiguous memory layout that enables
// high-performance parallel searches with excellent cache locality.
//
// PERFORMANCE NOTE: Using contiguous std::vector<char> instead of
// std::vector<std::string> provides:
// - Single allocation vs. millions of separate allocations
// - Excellent cache locality for parallel search threads
// - Reduced memory overhead (no per-string object overhead)
// InsertPathLocked now delegates to PathOperations
void FileIndex::InsertPathLocked(uint64_t id, std::string_view path,
                                 bool is_directory) {
  path_operations_.InsertPath(id, path, is_directory);
}

// GetPathLocked now delegates to PathOperations
std::string FileIndex::GetPathLocked(uint64_t id) const {
  return path_operations_.GetPath(id);
}

// GetPathViewLocked now delegates to PathOperations
std::string_view FileIndex::GetPathViewLocked(uint64_t id) const {
  return path_operations_.GetPathView(id);
}

// GetPathComponentsViewLocked now delegates to PathOperations (PathComponentsView is alias for PathOperations::PathComponentsView)
FileIndex::PathComponentsView
FileIndex::GetPathComponentsViewLocked(uint64_t id) const {
  return path_operations_.GetPathComponentsView(id);
}

// GetPathComponentsViewByIndexLocked now delegates to PathOperations
FileIndex::PathComponentsView
FileIndex::GetPathComponentsViewByIndexLocked(size_t idx) const {
  return path_operations_.GetPathComponentsViewByIndex(idx);
}

// UpdatePrefixLocked now delegates to PathOperations
void FileIndex::UpdatePrefixLocked(std::string_view old_prefix,
                                   std::string_view new_prefix) {
  path_operations_.UpdatePrefix(old_prefix, new_prefix);
}

// RebuildPathBuffer now delegates to PathStorage
// Note: RebuildPathBuffer() has been moved to FileIndexMaintenance

void FileIndex::InsertPathUnderLock(std::string_view full_path, bool is_directory) {
  const std::string_view path_to_use = path_utils::TrimTrailingSeparators(full_path);
  const size_t h = PathHashInline(path_to_use);  // zero allocation (fix #6)
  if (const auto idx_it = path_to_id_index_.find(h); idx_it != path_to_id_index_.end()) {
    size_t i = idx_it->second;
    while (i != FileIndex::kPathToIdEnd) {
      const PathToIdEntry& e = path_to_id_chain_[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i validated via path_to_id_index_ lookup and kPathToIdEnd sentinel
      if (PathViewsEqualNormalized(GetPathViewLocked(e.id), path_to_use)) {
        InsertPathLocked(e.id, path_to_use, is_directory);
        return;
      }
      i = e.next;
    }
  }

  // If this is a directory that was previously created via DirectoryResolver,
  // FileIndexStorage's directory cache may already know its ID even though
  // path_to_id_index_ has no entry yet. Reuse that ID instead of creating a
  // duplicate directory entry.
  if (is_directory) {
    if (const uint64_t existing_dir_id = storage_.GetDirectoryId(path_to_use);
        existing_dir_id != 0U) {
      const auto head_it = path_to_id_index_.find(h);
      const size_t head =
          (head_it != path_to_id_index_.end()) ? head_it->second : FileIndex::kPathToIdEnd;
      path_to_id_chain_.push_back({h, existing_dir_id, head});
      path_to_id_index_[h] = path_to_id_chain_.size() - 1;
      InsertPathLocked(existing_dir_id, path_to_use, true);
      return;
    }
  }

  std::string_view directory_path;
  std::string_view filename;
  if (const size_t last_slash = path_to_use.find_last_of("\\/"); last_slash != std::string_view::npos) {
    directory_path = path_to_use.substr(0, last_slash);
    filename = path_to_use.substr(last_slash + 1);
  } else {
    directory_path = std::string_view{};
    filename = path_to_use;
  }

  const uint64_t parent_id = directory_resolver_.GetOrCreateDirectoryId(directory_path);
  const uint64_t file_id = next_file_id_.fetch_add(1);

  // Synthetic path-derived ids must not enter record_number_to_id_ (would shadow MFT FRNs).
  // Parents from DirectoryResolver are always resolved — skip awaiting healing.
  InsertLocked(ntfs_file_reference::NtfsFileReference(file_id),
               ntfs_file_reference::NtfsFileReference(parent_id), filename, is_directory,
               kFileTimeNotLoaded,
               InsertOptions{/*register_mft_record=*/false, /*resolve_awaiting=*/false});
  if (is_directory) {
    storage_.CacheDirectory(path_to_use, file_id);
  }
  const auto head_it = path_to_id_index_.find(h);
  const size_t head = (head_it != path_to_id_index_.end()) ? head_it->second : FileIndex::kPathToIdEnd;
  path_to_id_chain_.push_back({ h, file_id, head });
  path_to_id_index_[h] = path_to_id_chain_.size() - 1;
}

void FileIndex::InsertPath(std::string_view full_path,  // NOLINT(readability-identifier-naming) - method name follows project convention; clang-tidy misclassifies as global
                           std::optional<bool> is_directory) {
  CTRACK_NAME("FileIndex::InsertPath");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  const std::unique_lock lock(index_mutex_);
  const bool resolved_is_directory = is_directory.value_or(
      !full_path.empty() && (full_path.back() == '\\' || full_path.back() == '/'));
  InsertPathUnderLock(full_path, resolved_is_directory);
}

bool FileIndex::TryInsertPathUnderLockAndCountError(std::string_view path, bool is_directory,
                                                    std::atomic<size_t>* out_error_count) {
  try {
    InsertPathUnderLock(path, is_directory);
    return false;
  } catch (const std::bad_alloc& e) {
    (void)e;
    LogInsertPathErrorAndIncrement("Memory allocation failure", path, nullptr, out_error_count);
    return true;
  } catch (const std::system_error& e) {
    LogInsertPathErrorAndIncrement("System error", path, e.what(), out_error_count);
    return true;
  } catch (const std::runtime_error& e) {  // NOSONAR(cpp:S1181) - Part of exception hierarchy
    LogInsertPathErrorAndIncrement("Runtime error", path, e.what(), out_error_count);
    return true;
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all after specific types
    LogInsertPathErrorAndIncrement("Exception", path, e.what(), out_error_count);
    return true;
  }
}

void FileIndex::InsertPaths(const std::vector<std::pair<std::string, bool>>& path_and_dir_flags,  // NOLINT(hicpp-named-parameter,readability-named-parameter) - all parameters are named and used; warning stems from macro analysis
                            std::atomic<size_t>* out_error_count) {
  CTRACK_DEV_NAME("FileIndex::InsertPaths");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  const std::unique_lock lock(index_mutex_);
  for (const auto& [path, is_dir] : path_and_dir_flags) {
    (void)TryInsertPathUnderLockAndCountError(path, is_dir, out_error_count);
  }
}

void FileIndex::ReserveForPopulation(size_t expected_entries) {
  if (expected_entries == 0) {
    return;
  }
  // Cap to bound worst-case memory: 2M entries cover ~2.5x the largest profiled
  // volume (824k); beyond that the remaining rehashes are logarithmic and cheap.
  // Average name length 32B bounds the arena at ~64MB at the cap.
  static constexpr size_t kMaxReserveEntries = 2000000U;
  static constexpr size_t kAvgNameBytes = 32U;
  const size_t capped =
      expected_entries > kMaxReserveEntries ? kMaxReserveEntries : expected_entries;
  const std::unique_lock lock(index_mutex_);
  storage_.Reserve(capped, capped * kAvgNameBytes);
  // NOTE: PathStorage is deliberately NOT reserved here. Deferred bulk inserts
  // skip InsertPath entirely, and RecomputeAllPaths starts with Clear()
  // (which shrink_to_fits everything) followed by its own exact
  // Reserve(n, n * 64U) for full paths — so any reservation here would be
  // allocated, discarded, and reallocated. The maps below are different:
  // nothing writes them during population either, but RebuildPathToIdMapLocked
  // (end of RecomputeAllPaths) only clear()s them — capacity persists — so
  // this reservation serves the rebuild instead of being thrown away.
  path_to_id_index_.reserve(capped);
  path_to_id_chain_.reserve(capped);
}

void FileIndex::InsertBatch(const std::vector<PopulationBatchEntry>& batch,
                            bool defer_path_indexing) {
  if (batch.empty()) {
    return;
  }
  // Split lock-wait from insert work so profiles distinguish mutex contention
  // from hash/arena cost (see issue #5: IndexInsert CV). The explicit
  // lock()/unlock() pair keeps the two CTRACK scopes separate while balancing
  // acquisition and release in this function (cpp:S8473).
  std::unique_lock lock(index_mutex_, std::defer_lock);
  {
    CTRACK_DEV_NAME("FileIndex::InsertBatchLockWait");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
    lock.lock();
  }
  {
    CTRACK_DEV_NAME("FileIndex::InsertBatchInsertLocked");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
    for (const auto& entry : batch) {
      // Bulk MFT population: skip per-insert awaiting healing (O(N^2) prefix
      // cascades); the post-population RecomputeAllPaths resolves all
      // out-of-order parents in one topological pass. Optionally also skip
      // per-insert path indexing (the recompute Clear()s it anyway), so each
      // path is joined + hashed exactly once.
      operations_.Insert(entry.id, entry.parent_id, entry.name.View(),
                         entry.is_directory, entry.modification_time,
                         InsertOptions{/*register_mft_record=*/true,
                                       /*resolve_awaiting=*/false,
                                       /*defer_path_indexing=*/defer_path_indexing},
                         entry.file_size);
    }
    mutation_count_.fetch_add(batch.size());
  }
  lock.unlock();
}

void FileIndex::Clear() {
  const std::unique_lock lock(index_mutex_);
  storage_.ClearLocked();
  operations_.ClearAwaiting();
  mutation_count_.fetch_add(1);
  path_storage_.Clear();
  path_to_id_index_.clear();
  path_to_id_chain_.clear();
  next_file_id_.store(1);
  LOG_INFO_BUILD("FileIndex::Clear: Cleared all entries from index");
}

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
// All path_to_id_chain_ accesses use indices that originate from
// path_to_id_index_ (a validated map lookup) or are traversed via the
// .next sentinel chain (kPathToIdEnd). Bounds are guaranteed by chain
// invariants maintained by InsertPathUnderLock / UnlinkPathToIdEntryLocked.
void FileIndex::UpdatePathToIdChainAfterSwapLocked(size_t old_index, size_t new_index) {
  const size_t moved_h = path_to_id_chain_[new_index].hash;
  if (const auto moved_it = path_to_id_index_.find(moved_h);
      moved_it != path_to_id_index_.end() && moved_it->second == old_index) {
    moved_it->second = new_index;
  } else {
    size_t j = path_to_id_index_[moved_h];
    while (path_to_id_chain_[j].next != old_index) {
      j = path_to_id_chain_[j].next;
    }
    path_to_id_chain_[j].next = new_index;
  }
}
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
// path_to_id_chain_ accesses are guarded by explicit index checks (i != kPathToIdEnd,
// prev != kPathToIdEnd, last = size()-1) — bounds are validated before each access.
void FileIndex::UnlinkPathToIdEntryLocked(size_t h, uint64_t id) {
  const auto idx_it = path_to_id_index_.find(h);
  if (idx_it == path_to_id_index_.end()) {
    return;
  }
  size_t i = idx_it->second;
  size_t prev = FileIndex::kPathToIdEnd;
  while (i != FileIndex::kPathToIdEnd) {
    if (path_to_id_chain_[i].id != id) {
      prev = i;
      i = path_to_id_chain_[i].next;
      continue;
    }
    if (const size_t next_idx = path_to_id_chain_[i].next;
        prev == FileIndex::kPathToIdEnd && next_idx == FileIndex::kPathToIdEnd) {
      path_to_id_index_.erase(h);
    } else if (prev == FileIndex::kPathToIdEnd) {
      path_to_id_index_[h] = next_idx;
    } else {
      path_to_id_chain_[prev].next = next_idx;
    }
    if (path_to_id_chain_.empty()) {
      return;
    }
    if (const size_t last = path_to_id_chain_.size() - 1; i != last) {
      path_to_id_chain_[i] = path_to_id_chain_[last];
      UpdatePathToIdChainAfterSwapLocked(last, i);
    }
    path_to_id_chain_.pop_back();
    return;
  }
}
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

void FileIndex::Remove(ntfs_file_reference::NtfsFileReference id) {
  CTRACK_NAME("FileIndex::Remove");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  const std::unique_lock lock(index_mutex_);
  RemoveLocked(id);
}

void FileIndex::RemoveLocked(ntfs_file_reference::NtfsFileReference id) {
  const std::string old_path = GetPathLocked(id.raw);
  operations_.Remove(id);
  if (!old_path.empty()) {
    UnlinkPathToIdEntryLocked(PathHashInline(old_path), id.raw);
  }
  mutation_count_.fetch_add(1);
}

void FileIndex::EvictDescendantsOfFilteredParentLocked(
    ntfs_file_reference::MftRecordNumber parent_record) {
  // Collect first: RemoveLocked mutates storage_, invalidating iteration.
  // Each removal goes through RemoveLocked so awaiting buckets and the
  // path_to_id_ map stay consistent (R1).
  std::vector<uint64_t> to_evict;
  operations_.CollectDescendantIds(parent_record, to_evict);
  for (const uint64_t id : to_evict) {
    RemoveLocked(ntfs_file_reference::NtfsFileReference(id));
  }
}

size_t FileIndex::EvictNeverArrivingLocked(
    uint64_t max_age_ms,
    const index_domain_events::NeverArrivingEvictedSink& on_evicted,
    std::vector<std::string>* out_log_lines) {
  // Collect first: RemoveLocked mutates storage_, invalidating iteration.
  std::vector<IndexOperations::NeverArriving> stale;
  operations_.CollectNeverArriving(max_age_ms, stale);
  const size_t evicted = EvictCollectedNeverArrivingLocked(stale, on_evicted, out_log_lines);
#ifndef NDEBUG
  // Debug-only diagnostics: LOG_WARNING_BUILD compiles out in release, so
  // building these strings in release would lengthen the critical section
  // for zero benefit.
  if (evicted > 0 && out_log_lines != nullptr) {
    std::ostringstream summary;
    summary << "FileIndex::EvictNeverArrivingLocked: evicted "
            << evicted << " stale placeholders (max_age_ms=" << max_age_ms << ")";
    out_log_lines->push_back(summary.str());
  }
#endif  // NDEBUG
  return evicted;
}

size_t FileIndex::EvictNeverArrivingByJournalGapLocked(
    int64_t current_usn,
    int64_t max_usn_gap,
    const index_domain_events::NeverArrivingEvictedSink& on_evicted,
    std::vector<std::string>* out_log_lines) {
  // Collect first: RemoveLocked mutates storage_, invalidating iteration.
  std::vector<IndexOperations::NeverArriving> stale;
  operations_.CollectNeverArrivingByJournalGap(current_usn, max_usn_gap, stale);
  const size_t evicted = EvictCollectedNeverArrivingLocked(stale, on_evicted, out_log_lines);
#ifndef NDEBUG
  // Debug-only diagnostics (see above): no string work in release.
  if (evicted > 0 && out_log_lines != nullptr) {
    std::ostringstream summary;
    summary << "FileIndex::EvictNeverArrivingByJournalGapLocked: evicted "
            << evicted << " placeholders tracked >=" << max_usn_gap
            << " USNs behind current_usn=" << current_usn;
    out_log_lines->push_back(summary.str());
  }
#endif  // NDEBUG
  return evicted;
}

size_t FileIndex::EvictCollectedNeverArrivingLocked(
    const std::vector<IndexOperations::NeverArriving>& stale,
    const index_domain_events::NeverArrivingEvictedSink& on_evicted,
    std::vector<std::string>* out_log_lines) {
#ifndef NDEBUG
  // Debug-only diagnostics (see EvictNeverArrivingLocked). One reserve up
  // front so N entries cost one allocation, not N, under the exclusive lock.
  if (out_log_lines != nullptr) {
    out_log_lines->reserve(out_log_lines->size() + stale.size());
  }
#endif  // NDEBUG
  for (const auto& stale_entry : stale) {
    // Copy before RemoveLocked: FileEntry* has no stability across map inserts.
    // parent_frn identifies the never-arriving producer (correlate with USN logs).
    const FileEntry* const file_entry = storage_.GetEntry(stale_entry.id);
    const uint64_t parent_frn = file_entry != nullptr ? file_entry->parentID.raw : 0;
#ifndef NDEBUG
    if (out_log_lines != nullptr) {
      std::ostringstream line;
      line << "FileIndex::EvictCollectedNeverArrivingLocked: evicting never-arriving placeholder \""
           << GetPathLocked(stale_entry.id) << "\" (id=" << stale_entry.id
           << ", parent_frn=" << parent_frn
           << ", waited " << stale_entry.age_ms << "ms for parent"
           << ", usn_at_track=" << stale_entry.usn_at_track << ")";
      out_log_lines->push_back(line.str());
    }
#endif  // NDEBUG
    RemoveLocked(ntfs_file_reference::NtfsFileReference(stale_entry.id));
    if (on_evicted) {
      on_evicted(index_domain_events::NeverArrivingEvicted{stale_entry.id, stale_entry.age_ms,
                                                           stale_entry.usn_at_track,});
    }
  }
  return stale.size();
}

void FileIndex::RefreshPathToIdAfterPathUpdateLocked(uint64_t id,
                                                     std::string_view old_path,
                                                     std::string_view new_path) {
  if (!old_path.empty() && old_path.find_first_of("\\/") != std::string_view::npos) {
    UnlinkPathToIdEntryLocked(PathHashInline(old_path), id);
  }
  if (!new_path.empty()) {
    AppendPathToIdMapLocked(id, new_path);
  }
}

void FileIndex::RefreshPathToIdAfterRenameOrMoveLocked(uint64_t id,
                                                       std::string_view old_path,
                                                       const FileEntry* entry) {
  // Directory Rename/Move calls UpdatePrefix and rewrites all descendant paths.
  // Rebuild path_to_id_ so it stays consistent; single-id update for files.
  if (entry != nullptr && entry->isDirectory) {
    RebuildPathToIdMapLocked();
    return;
  }
  const std::string new_path = GetPathLocked(id);
  RefreshPathToIdAfterPathUpdateLocked(id, old_path, new_path);
}

bool FileIndex::Rename(ntfs_file_reference::NtfsFileReference id, std::string_view new_name) {
  const std::unique_lock lock(index_mutex_);
  return RenameLocked(id, new_name);
}

bool FileIndex::RenameLocked(ntfs_file_reference::NtfsFileReference id, std::string_view new_name) {
  const std::string old_path = GetPathLocked(id.raw);
  const FileEntry* const entry = storage_.GetEntry(id.raw);
  if (!operations_.Rename(id, new_name)) {
    return false;
  }
  RefreshPathToIdAfterRenameOrMoveLocked(id.raw, old_path, entry);
  mutation_count_.fetch_add(1);
  return true;
}

bool FileIndex::Move(ntfs_file_reference::NtfsFileReference id,
                     ntfs_file_reference::NtfsFileReference new_parent_id) {
  const std::unique_lock lock(index_mutex_);
  return MoveLocked(id, new_parent_id);
}

bool FileIndex::MoveLocked(ntfs_file_reference::NtfsFileReference id,
                           ntfs_file_reference::NtfsFileReference new_parent_id) {
  const std::string old_path = GetPathLocked(id.raw);
  const FileEntry* const entry = storage_.GetEntry(id.raw);
  if (!operations_.Move(id, new_parent_id)) {
    return false;
  }
  RefreshPathToIdAfterRenameOrMoveLocked(id.raw, old_path, entry);
  mutation_count_.fetch_add(1);
  return true;
}

void FileIndex::AppendPathToIdMapLocked(uint64_t id, std::string_view path) {
  if (path.empty()) {
    return;
  }
  const size_t h = PathHashInline(path);
  const auto head_it = path_to_id_index_.find(h);
  const size_t head = (head_it != path_to_id_index_.end()) ? head_it->second : FileIndex::kPathToIdEnd;
  path_to_id_chain_.push_back({ h, id, head });
  path_to_id_index_[h] = path_to_id_chain_.size() - 1;
}

void FileIndex::PruneOrphanSubtreesLocked() {
  PruneBrokenParentSubtreesLocked(storage_, operations_);
}

void FileIndex::RebuildPathToIdMapLocked() {
  path_to_id_index_.clear();
  path_to_id_chain_.clear();
  path_to_id_chain_.reserve(storage_.Size());
  for (const auto& [id, entry] : storage_) {
    (void)entry;
    // Zero-copy path view from PathStorage — avoids one std::string allocation per
    // entry; PathHashInline only needs string_view (same as InsertPathUnderLock).
    const std::string_view path = GetPathViewLocked(id.raw);
    AppendPathToIdMapLocked(id.raw, path);
  }
}

void FileIndex::FinalizeFolderCrawlIndexing() {
  CTRACK_NAME("FileIndex::FinalizeFolderCrawlIndexing");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  const ScopedTimer timer("FileIndex::FinalizeFolderCrawlIndexing");
  const std::unique_lock lock(index_mutex_);
#ifdef _WIN32
  size_t onedrive_reset_count = 0;
  for (const auto& [id, entry] : storage_) {
    if (entry.isDirectory) {
      continue;
    }
    const std::string_view path = GetPathViewLocked(id.raw);
    if (path.empty()) {
      continue;
    }
    if (IsOneDriveFile(path)) {
      storage_.UpdateFileSize(id.raw, kFileSizeNotLoaded);
      storage_.UpdateModificationTime(id.raw, kFileTimeNotLoaded);
      onedrive_reset_count++;
    }
  }
  if (onedrive_reset_count > 0) {
    LOG_INFO_BUILD("FileIndex::FinalizeFolderCrawlIndexing: Reset "
                   << onedrive_reset_count
                   << " OneDrive files to sentinel values for lazy loading");
  }
#endif  // _WIN32
  storage_.ReleaseNameCache();
  LOG_INFO_BUILD("FileIndex::FinalizeFolderCrawlIndexing: released name cache "
                 "(skipped full PathStorage rebuild after folder crawl)");
}

// Note: get_or_create_directory_id has been moved to DirectoryResolver

// Note: Maintain() has been moved to FileIndexMaintenance

// Parallel search across the contiguous path storage buffer.
//
// PERFORMANCE: This method benefits significantly from the contiguous
// std::vector<char> design (path_storage_). The contiguous memory layout
// enables:
// - Excellent cache locality: Each thread accesses sequential memory locations
// - Efficient parallel processing: Multiple threads can scan their chunks
// without
//   cache conflicts
// - Low memory overhead: Single allocation vs. millions of separate std::string
// allocations
//
// For 1M paths, this typically completes in ~100ms with optimal cache behavior.
// Using std::vector<std::string> would fragment memory and degrade performance
// significantly.
//
// This API is intentionally explicit with multiple parameters to mirror search configuration.
std::vector<std::future<SearchResultBatch>> FileIndex::SearchAsyncWithData(  // NOSONAR(cpp:S107) - Parallel search API has many parameters to mirror search configuration; NOLINT(readability-function-size) - pre-existing parallel fan-out, extraction is separate work
    std::string_view query, int thread_count,
                               [[maybe_unused]] SearchStats *stats,
                               std::string_view path_query,
                               const std::vector<std::string> *extensions,
                               ItemTypeFilter item_type_filter, bool case_sensitive,
                               std::vector<ThreadTiming> *thread_timings,
                               const std::atomic<bool> *cancel_flag,
                               const AppSettings *optional_settings) {
  CTRACK_NAME("FileIndex::SearchAsyncWithData");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  GetThreadPool();  // Ensures search_engine_ has current pool
  SearchContext context = SearchContextBuilder::Build(
      query, path_query, extensions, item_type_filter, case_sensitive,
      cancel_flag);

  if (optional_settings != nullptr) {
    context.dynamic_chunk_size = static_cast<size_t>(optional_settings->dynamicChunkSize);
    context.hybrid_initial_percent = optional_settings->hybridInitialWorkPercent;
    context.search_thread_pool_size = optional_settings->searchThreadPoolSize;
    context.guided_scheduling_divisor = optional_settings->guidedSchedulingDivisor;
  } else {
    AppSettings settings;
    LoadSettings(settings);
    context.dynamic_chunk_size = static_cast<size_t>(settings.dynamicChunkSize);
    context.hybrid_initial_percent = settings.hybridInitialWorkPercent;
    context.search_thread_pool_size = settings.searchThreadPoolSize;
    context.guided_scheduling_divisor = settings.guidedSchedulingDivisor;
  }

  context.ValidateAndClamp();

  return search_engine_->SearchAsyncWithData(*this, std::string_view(query), thread_count, context, thread_timings, cancel_flag);
}

std::vector<std::future<SearchResultBatch>> FileIndex::SearchAsyncWithData(  // NOSONAR(cpp:S107) - Parallel search API has many parameters to mirror search configuration; NOLINT(readability-function-size)
    std::string_view query, int thread_count,
    SearchStats *stats,
    std::string_view path_query,
    const std::vector<std::string> *extensions,
    bool folders_only, bool case_sensitive,
    std::vector<ThreadTiming> *thread_timings,
    const std::atomic<bool> *cancel_flag,
    const AppSettings *optional_settings) {
  return SearchAsyncWithData(query, thread_count, stats, path_query, extensions,
                             folders_only ? ItemTypeFilter::FoldersOnly : ItemTypeFilter::All,
                             case_sensitive, thread_timings, cancel_flag, optional_settings);
}

// Get or create the thread pool; updates search_engine_ when pool is (re)created
SearchThreadPool& FileIndex::GetThreadPool() {
  const std::shared_ptr<SearchThreadPool> pool_ptr = thread_pool_manager_.GetPoolSharedPtr();
  if (!search_engine_ || &search_engine_->GetThreadPool() != pool_ptr.get()) {
    search_engine_ = std::make_shared<ParallelSearchEngine>(pool_ptr);
  }
  return *pool_ptr;
}

size_t FileIndex::GetSearchThreadPoolCount() {
  return thread_pool_manager_.GetThreadPoolCount();
}

void FileIndex::SetThreadPool(std::shared_ptr<SearchThreadPool> pool) {  // NOLINT(readability-identifier-naming)
  thread_pool_manager_.SetThreadPool(std::move(pool));
  const std::shared_ptr<SearchThreadPool> p = thread_pool_manager_.GetPoolSharedPtrWithoutCreating();
  search_engine_ = std::make_shared<ParallelSearchEngine>(
      p ? p : std::make_shared<SearchThreadPool>(0));
}

void FileIndex::ResetThreadPool() {
  thread_pool_manager_.ResetThreadPool();
  // Re-initialize search_engine_ eagerly so concurrent callers of SearchAsync
  // never race to lazy-init it. Without this, multiple threads entering
  // GetThreadPool() simultaneously after a reset would all find search_engine_
  // stale and write it concurrently — a data race leading to heap-use-after-free.
  // ASSUMPTION: ResetThreadPool() is not called concurrently with searches.
  const auto pool_ptr = thread_pool_manager_.GetPoolSharedPtr();
  search_engine_ = std::make_shared<ParallelSearchEngine>(pool_ptr);
}

// Note: ProcessChunkRangeForSearchIds has been removed as dead code.
// It was replaced by ParallelSearchEngine::ProcessChunkRangeIds, which is
// used by LoadBalancingStrategy implementations.
