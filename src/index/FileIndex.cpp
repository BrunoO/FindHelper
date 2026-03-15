#include "index/FileIndex.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <functional>
#include <future>
#include <mutex>
#include <string_view>
#include <thread>

#include "core/Settings.h"
#include "index/FileIndexMaintenance.h"
#include "search/ParallelSearchEngine.h"
#include "search/SearchContext.h"
#include "search/SearchThreadPool.h"
#include "utils/LightweightCallable.h"
#include "utils/Logger.h"
#include "utils/StdRegexUtils.h"
#include "utils/StringSearch.h"

// FileIndex constructor: thread pool lifecycle delegated to SearchThreadPoolManager (Option C)
FileIndex::FileIndex(std::shared_ptr<SearchThreadPool> thread_pool)  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - members use in-class initialization; index_mutex_ is initialized in header
    : storage_(index_mutex_),
      lazy_loader_(storage_, path_storage_, index_mutex_),
      path_operations_(storage_, path_storage_),
      path_recomputer_(storage_, path_storage_, path_operations_),
      maintenance_(path_storage_, index_mutex_,
                   [this]() { return this->Size(); },
                   [this](uint64_t id, size_t idx) { storage_.SetPathStorageIndex(id, idx); },
                   remove_not_in_index_count_,
                   remove_duplicate_count_,
                   remove_inconsistency_count_),
      operations_(storage_, path_operations_,
                  remove_not_in_index_count_,
                  remove_inconsistency_count_),
      directory_resolver_(storage_, operations_, next_file_id_),
      thread_pool_manager_(std::move(thread_pool)),
      search_engine_(std::make_shared<ParallelSearchEngine>(
          thread_pool_manager_.GetPoolSharedPtrWithoutCreating()
              ? thread_pool_manager_.GetPoolSharedPtrWithoutCreating()
              : std::make_shared<SearchThreadPool>(0))) {}

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
    out_error_count->fetch_add(1, std::memory_order_relaxed);
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
  const uint64_t file_id = next_file_id_.fetch_add(1, std::memory_order_relaxed);

  InsertLocked(file_id, parent_id, filename, is_directory, kFileTimeNotLoaded);
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
  const std::unique_lock lock(index_mutex_);
  for (const auto& [path, is_dir] : path_and_dir_flags) {
    (void)TryInsertPathUnderLockAndCountError(path, is_dir, out_error_count);
  }
}

void FileIndex::Clear() {
  const std::unique_lock lock(index_mutex_);
  storage_.ClearLocked();
  path_storage_.Clear();
  path_to_id_index_.clear();
  path_to_id_chain_.clear();
  next_file_id_.store(1, std::memory_order_relaxed);
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

void FileIndex::Remove(uint64_t id) {
  const std::unique_lock lock(index_mutex_);
  const std::string old_path = GetPathLocked(id);
  operations_.Remove(id);
  if (!old_path.empty()) {
    UnlinkPathToIdEntryLocked(PathHashInline(old_path), id);
  }
}

bool FileIndex::Rename(uint64_t id, std::string_view new_name) {
  const std::unique_lock lock(index_mutex_);
  const std::string old_path = GetPathLocked(id);
  const FileEntry* const entry = storage_.GetEntry(id);
  if (!operations_.Rename(id, new_name)) {
    return false;
  }
  // Directory Rename calls UpdatePrefix and rewrites all descendant paths.
  // Rebuild path_to_id_ so it stays consistent; single-id update for files.
  if (entry != nullptr && entry->isDirectory) {
    RebuildPathToIdMapLocked();
  } else {
    const std::string new_path = GetPathLocked(id);
    if (!old_path.empty()) {
      UnlinkPathToIdEntryLocked(PathHashInline(old_path), id);
    }
    const size_t h_new = PathHashInline(new_path);
    const auto head_it = path_to_id_index_.find(h_new);
    const size_t head = (head_it != path_to_id_index_.end()) ? head_it->second : FileIndex::kPathToIdEnd;
    path_to_id_chain_.push_back({ h_new, id, head });
    path_to_id_index_[h_new] = path_to_id_chain_.size() - 1;
  }
  return true;
}

bool FileIndex::Move(uint64_t id, uint64_t new_parent_id) {
  const std::unique_lock lock(index_mutex_);
  const std::string old_path = GetPathLocked(id);
  const FileEntry* const entry = storage_.GetEntry(id);
  if (!operations_.Move(id, new_parent_id)) {
    return false;
  }
  // Directory Move calls UpdatePrefix and rewrites all descendant paths.
  // Rebuild path_to_id_ so it stays consistent; single-id update for files.
  if (entry != nullptr && entry->isDirectory) {
    RebuildPathToIdMapLocked();
  } else {
    const std::string new_path = GetPathLocked(id);
    if (!old_path.empty()) {
      UnlinkPathToIdEntryLocked(PathHashInline(old_path), id);
    }
    const size_t h_new = PathHashInline(new_path);
    const auto head_it = path_to_id_index_.find(h_new);
    const size_t head = (head_it != path_to_id_index_.end()) ? head_it->second : FileIndex::kPathToIdEnd;
    path_to_id_chain_.push_back({ h_new, id, head });
    path_to_id_index_[h_new] = path_to_id_chain_.size() - 1;
  }
  return true;
}

void FileIndex::RebuildPathToIdMapLocked() {
  path_to_id_index_.clear();
  path_to_id_chain_.clear();
  path_to_id_chain_.reserve(storage_.Size());
  for (const auto& [id, entry] : storage_) {
    const std::string path = GetPathLocked(id);
    if (!path.empty()) {
      const size_t h = PathHashInline(path);
      const auto head_it = path_to_id_index_.find(h);
      const size_t head = (head_it != path_to_id_index_.end()) ? head_it->second : FileIndex::kPathToIdEnd;
      path_to_id_chain_.push_back({ h, id, head });
      path_to_id_index_[h] = path_to_id_chain_.size() - 1;
    }
  }
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
std::vector<std::future<std::vector<uint64_t>>> FileIndex::SearchAsync(  // NOSONAR(cpp:S107) - Parallel search API has many parameters to mirror search configuration
    std::string_view query, int thread_count, SearchStats *stats,
    std::string_view path_query, const std::vector<std::string> *extensions,
    bool folders_only, bool case_sensitive) {
  GetThreadPool();  // Ensures search_engine_ has current pool
  const SearchContext context = SearchContextBuilder::Build(
      query, path_query, extensions, folders_only, case_sensitive);
  // Delegate to ParallelSearchEngine
  // Note: ParallelSearchEngine sets num_threads_used_ and total_items_scanned_
  // total_matches_found_ and duration_milliseconds_ should be populated by caller after collecting results
  return search_engine_->SearchAsync(*this, std::string_view(query), thread_count, context, stats);
}

// This API is intentionally explicit with multiple parameters to mirror search configuration.
std::vector<std::future<std::vector<SearchResultData>>> FileIndex::SearchAsyncWithData(  // NOSONAR(cpp:S107) - Parallel search API has many parameters to mirror search configuration
    std::string_view query, int thread_count,
                               [[maybe_unused]] SearchStats *stats,
                               std::string_view path_query,
                               const std::vector<std::string> *extensions,
                               bool folders_only, bool case_sensitive,
                               std::vector<ThreadTiming> *thread_timings,
                               const std::atomic<bool> *cancel_flag,
                               const AppSettings *optional_settings) {
  GetThreadPool();  // Ensures search_engine_ has current pool
  SearchContext context = SearchContextBuilder::Build(
      query, path_query, extensions, folders_only, case_sensitive,
      cancel_flag);

  if (optional_settings != nullptr) {
    context.dynamic_chunk_size = static_cast<size_t>(optional_settings->dynamicChunkSize);
    context.hybrid_initial_percent = optional_settings->hybridInitialWorkPercent;
    context.load_balancing_strategy = optional_settings->loadBalancingStrategy;
    context.search_thread_pool_size = optional_settings->searchThreadPoolSize;
  } else {
    AppSettings settings;
    LoadSettings(settings);
    context.dynamic_chunk_size = static_cast<size_t>(settings.dynamicChunkSize);
    context.hybrid_initial_percent = settings.hybridInitialWorkPercent;
    context.load_balancing_strategy = settings.loadBalancingStrategy;
    context.search_thread_pool_size = settings.searchThreadPoolSize;
  }

  context.ValidateAndClamp();

  return search_engine_->SearchAsyncWithData(*this, std::string_view(query), thread_count, context, thread_timings, cancel_flag);
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
}

// Note: ProcessChunkRangeForSearchIds has been removed as dead code.
// It was replaced by ParallelSearchEngine::ProcessChunkRangeIds, which is
// used by LoadBalancingStrategy implementations.
