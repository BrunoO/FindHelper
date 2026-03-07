#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "core/Settings.h"
#include "crawler/IndexOperations.h"
#include "index/FileIndexMaintenance.h"
#include "index/FileIndexStorage.h"
#include "index/ISearchableIndex.h"
#include "index/LazyAttributeLoader.h"
#include "index/LazyValue.h"
#include "index/PathRecomputer.h"
#include "path/DirectoryResolver.h"
#include "path/PathOperations.h"
#include "path/PathStorage.h"
#include "path/PathUtils.h"
#include "search/LoadBalancingStrategyUtils.h"
#include "search/SearchContext.h"
#include "search/SearchContextBuilder.h"
#include "search/SearchThreadPoolManager.h"
#include "search/SearchTypes.h"
#include "utils/FileSystemUtils.h"
#include "utils/HashMapAliases.h"
#include "utils/LightweightCallable.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

// Forward declarations
class ParallelSearchEngine;
class SearchThreadPool;

// LoadBalancingStrategyType is defined in SearchTypes.h (included above)

// Forward declarations for helper functions used in inline methods
namespace file_index_detail {
// ExtensionMatches is now in search_pattern_utils namespace
// Use search_pattern_utils::ExtensionMatches instead

void ExtractFilenameAndExtension(const char *path, size_t filename_start_offset,
                                 size_t extension_start_offset,
                                 std::string &out_filename,
                                 std::string &out_extension);
} // namespace file_index_detail

class FileIndex : public ISearchableIndex {  // NOSONAR(cpp:S1448) - Facade pattern: high method count acceptable; single entry point delegates to components (IndexOperations, PathRecomputer, SearchThreadPoolManager, FileIndexMaintenance, etc.)
public:
  // Friend declarations removed in Step 7 - LoadBalancingStrategy now uses ISearchableIndex interface

  /**
   * @brief Construct FileIndex with optional thread pool injection
   * 
   * @param thread_pool Optional shared pointer to SearchThreadPool. If nullptr,
   *                    a thread pool will be created lazily on first use.
   * 
   * Note: Implementation is in FileIndex.cpp to avoid circular dependency issues
   * with ParallelSearchEngine and SearchThreadPool.
   */
  explicit FileIndex(std::shared_ptr<SearchThreadPool> thread_pool = nullptr);

  // Insert or update a file entry. When file_size is a loaded, non-zero value
  // (e.g. from MFT metadata reading) it is applied under the same lock
  // acquisition, avoiding a second lock acquire via UpdateFileSizeById.
  void Insert(uint64_t id, uint64_t parent_id, std::string_view name,
              bool is_directory = false,
              FILETIME modification_time = kFileTimeNotLoaded,
              uint64_t file_size = kFileSizeNotLoaded) {
    const std::unique_lock lock(index_mutex_);
    InsertLocked(id, parent_id, name, is_directory, modification_time);
    if (!is_directory && file_size != kFileSizeNotLoaded && file_size > 0) {
      storage_.UpdateFileSize(id, file_size);
    }
  }

  // Internal insert logic (caller MUST hold unique_lock)
  void InsertLocked(uint64_t id, uint64_t parent_id, std::string_view name,
                    bool is_directory = false,
                    FILETIME modification_time = kFileTimeNotLoaded) {
    operations_.Insert(id, parent_id, name, is_directory, modification_time);
  }

  // Insert a file entry from a full path.
  // When is_directory is nullopt, infers directory status from trailing slash (used by index-from-file and other callers that only provide paths).
  void InsertPath(std::string_view full_path,
                  std::optional<bool> is_directory = std::nullopt);

  // Insert multiple paths under a single lock (reduces contention during crawl).
  // Each element is (path, is_directory). On exception per path, logs and increments out_error_count if non-null.
  void InsertPaths(const std::vector<std::pair<std::string, bool>>& path_and_dir_flags,  // NOLINT(readability-avoid-const-params-in-decls) - const ref documents read-only; keep for API clarity
                   std::atomic<size_t>* out_error_count = nullptr);

  // Clear all entries from the index
  // This is used when rebuilding the index from scratch (e.g., when starting
  // a new folder crawl). Clears both storage and path storage, and resets
  // the file ID counter.
  void Clear();

  // Remove a file entry (implementation in .cpp to update path_to_id_)
  void Remove(uint64_t id);

  // Rename a file (change name only). Implementation in .cpp to update path_to_id_.
  [[nodiscard]] bool Rename(uint64_t id, std::string_view new_name);

  // Move a file (change parent only). Implementation in .cpp to update path_to_id_.
  [[nodiscard]] bool Move(uint64_t id, uint64_t new_parent_id);

  // Iterate over all entries with a single shared lock, invoking the provided
  // functor for each entry. The functor should have the signature:
  // bool(uint64_t id, const FileEntry& entry) and return true to continue
  // iteration or false to stop early.
  template <typename F> void ForEachEntry(F &&fn) const {  // NOLINT(cppcoreguidelines-missing-std-forward) - fn forwarded in loop body
    const std::shared_lock lock(index_mutex_);
    for (const auto &[id, entry] : storage_) {
      if (!std::forward<F>(fn)(id, entry)) {
        break;
      }
    }
  }

  // Iterate entries with path view in a single lock. If ids is nullptr, iterate
  // all; else iterate the given ids. Functor: bool(uint64_t id, const FileEntry&
  // entry, std::string_view path). Return true to continue, false to stop.
  template <typename F>
  void ForEachEntryWithPath(F &&fn,  // NOLINT(cppcoreguidelines-missing-std-forward) - fn forwarded inside lambda
                            const std::vector<uint64_t>* ids = nullptr) const {
    const std::shared_lock lock(index_mutex_);
    auto invoke_with_path = [this, &fn](uint64_t id, const FileEntry& entry) {
      std::string_view path_view = GetPathViewLocked(id);
      return std::forward<F>(fn)(id, entry, path_view);
    };
    if (ids != nullptr) {  // NOLINT(bugprone-branch-clone) - then/else iterate different sources (*ids vs storage_); merging would require allocation
      for (const uint64_t id : *ids) {
        const FileEntry* entry = storage_.GetEntry(id);
        if (entry == nullptr) {
          continue;
        }
        if (!invoke_with_path(id, *entry)) {
          break;
        }
      }
    } else {
      for (const auto& [id, entry] : storage_) {
        if (!invoke_with_path(id, entry)) {
          break;
        }
      }
    }
  }

  // Path components view: use PathOperations type (single definition, no duplication)
  using PathComponentsView = PathOperations::PathComponentsView;

  // Zero-copy path access: use GetPathAccessor() then GetPathView / GetPathsView / GetPathComponentsView.
  struct PathAccessor {
    explicit PathAccessor(const FileIndex* index) : index_(index) {}  // NOLINT(readability-identifier-naming) - project convention: snake_case_
    [[nodiscard]] std::string_view GetPathView(uint64_t id) const {
      const std::shared_lock lock(index_->index_mutex_);
      return index_->GetPathViewLocked(id);
    }
    void GetPathsView(const std::vector<uint64_t> &ids,
                      std::vector<std::string_view> &out) const {
      const std::shared_lock lock(index_->index_mutex_);
      out.clear();
      out.reserve(ids.size());
      for (const uint64_t id : ids) {
        out.push_back(index_->GetPathViewLocked(id));
      }
    }
    [[nodiscard]] PathComponentsView GetPathComponentsView(uint64_t id) const {
      const std::shared_lock lock(index_->index_mutex_);
      return index_->GetPathComponentsViewLocked(id);
    }

   private:
    const FileIndex* index_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  };
  [[nodiscard]] PathAccessor GetPathAccessor() { return PathAccessor(this); }
  [[nodiscard]] PathAccessor GetPathAccessor() const { return PathAccessor(this); }

  // Update cached file size for a given file ID (no-op for directories).
  // Path is read under shared_lock; the I/O call is made without any lock;
  // the result is written under unique_lock. This avoids blocking searches
  // for the duration of a synchronous stat() call.
  [[nodiscard]] bool UpdateSize(uint64_t id) {
    // Phase 1: read path (shared lock — no I/O)
    std::string path;
    {
      const std::shared_lock read_lock(index_mutex_);
      const FileEntry* const entry = storage_.GetEntry(id);  // NOLINT(cppcoreguidelines-init-variables)
      if (entry == nullptr || entry->isDirectory) {
        return false;
      }
      path = GetPathLocked(id);
    }
    if (path.empty()) {
      return false;
    }
    // Phase 2: query the file system — no lock held
    const uint64_t new_size = LazyAttributeLoader::GetFileSize(path);
    // Phase 3: write result (unique lock)
    const std::unique_lock write_lock(index_mutex_);
    storage_.UpdateFileSize(id, new_size);
    return true;
  }

  // Update modification time for a given file ID
  // Note: USN record timestamps are always zero, so this is not called from USN
  // processing Kept for potential future use or manual updates No-op for
  // directories
  void UpdateModificationTime(uint64_t id, const FILETIME &time) {
    const std::unique_lock lock(index_mutex_);
    const FileEntry* entry = storage_.GetEntry(id);  // NOLINT(cppcoreguidelines-init-variables) - Initialized from GetEntry() return value
    if (entry != nullptr && !entry->isDirectory) {
      storage_.UpdateModificationTime(id, time);
    }
  }

  // Update file size for a given file ID (no-op for directories)
  // Used by MFT metadata reader to set size without path resolution
  // This method can remain even if MFT reading is removed (harmless utility)
  void UpdateFileSizeById(uint64_t id, uint64_t size) {
    const std::unique_lock lock(index_mutex_);
    const FileEntry* entry = storage_.GetEntry(id);  // NOLINT(cppcoreguidelines-init-variables) - Initialized from GetEntry() return value
    if (entry != nullptr && !entry->isDirectory) {
      storage_.UpdateFileSize(id, size);
    }
  }

  // Load file size on-demand (called by UI for visible rows)
  // Returns true if size was loaded, false if already loaded or failed
  // Delegates to LazyAttributeLoader
  [[nodiscard]] bool LoadFileSize(uint64_t id) {
    return lazy_loader_.LoadFileSize(id);
  }

  // Get file size by ID - automatically loads if not yet loaded
  // UI doesn't need to know about sentinel values or loading logic
  // CRITICAL: Never hold unique_lock during I/O operations!
  // OPTIMIZATION: If modification time also needs loading, loads both together
  // Note: Method is const because cache fields are mutable
  // Delegates to LazyAttributeLoader
  [[nodiscard]] uint64_t GetFileSizeById(uint64_t id) const {
    return lazy_loader_.GetFileSize(id);
  }

  // Get file modification time by ID - automatically loads if not yet loaded
  // UI doesn't need to know about sentinel values or loading logic
  // CRITICAL: Never hold unique_lock during I/O operations!
  // OPTIMIZATION: If size also needs loading, loads both together
  // Note: Method is const because cache fields are mutable
  // Delegates to LazyAttributeLoader
  [[nodiscard]] FILETIME GetFileModificationTimeById(uint64_t id) const {
    return lazy_loader_.GetModificationTime(id);
  }

  // Get the number of indexed files (lock-free)
  [[nodiscard]] size_t Size() const { return storage_.Size(); }

  // Search types (SearchStats, SearchResultData, ThreadTiming) defined in search/SearchTypes.h

  // Recompute all paths. Call after bulk insertion (e.g., initial index
  // population) to ensure paths are consistent when entries may have been
  // inserted out of order. Delegates to PathRecomputer (caller holds lock).
  void RecomputeAllPaths() {
    const ScopedTimer timer("FileIndex::RecomputeAllPaths");
    const std::unique_lock lock(index_mutex_);
    path_recomputer_.RecomputeAllPaths();
    RebuildPathToIdMapLocked();
  }

  // Parallel search
  // Returns a list of IDs that match the query
  // thread_count: -1 = auto (hardware concurrency)
  // query: Filename query (searches in filename part of path)
  // path_query: Optional path query (searches in directory part of path)
  // extensions: Optional list of allowed extensions (empty = no filter)
  // Parallel search that returns futures for incremental processing
  // Returns futures that will yield vectors of matching IDs
  // This allows processing results as they become available instead of waiting
  // for all folders_only: If true, only return directory entries (filtering
  // happens in parallel) case_sensitive: If true, perform case-sensitive
  // search; if false, case-insensitive (default)
  std::vector<std::future<std::vector<uint64_t>>>
  SearchAsync(std::string_view query, int thread_count = -1,
              SearchStats *stats = nullptr, std::string_view path_query = "",
              const std::vector<std::string> *extensions = nullptr,
              bool folders_only = false, bool case_sensitive = false);

  // Parallel search that returns futures with extracted data (filename,
  // extension, path) This eliminates the need for FileEntry lookups in
  // post-processing Returns futures that will yield vectors of SearchResultData
  // thread_timings: Optional output parameter to collect per-thread timing
  // information
  //                 for load balancing analysis (nullptr to disable timing
  //                 collection)
  std::vector<std::future<std::vector<SearchResultData>>>
  SearchAsyncWithData(std::string_view query, int thread_count = -1,
                      SearchStats *stats = nullptr,
                      std::string_view path_query = "",
                      const std::vector<std::string> *extensions = nullptr,
                      bool folders_only = false, bool case_sensitive = false,
                      std::vector<ThreadTiming> *thread_timings = nullptr,
                      const std::atomic<bool> *cancel_flag = nullptr,
                      const AppSettings *optional_settings = nullptr);

  // Thread pool lifecycle (Facade: thin wrappers; implementation in SearchThreadPoolManager)
  [[nodiscard]] size_t GetSearchThreadPoolCount();
  void SetThreadPool(std::shared_ptr<SearchThreadPool> pool);
  void ResetThreadPool();

  // Get mutex for worker threads to acquire shared_lock
  // This allows worker threads to prevent race conditions with Insert/Remove
  // operations
  [[nodiscard]] std::shared_mutex &GetMutex() const override { return index_mutex_; }

  // ISearchableIndex interface implementation
  [[nodiscard]] PathStorage::SoAView GetSearchableView() const override {
    return path_operations_.GetSearchableView();
  }

  // ISearchableIndex::GetEntry implementation
  [[nodiscard]] const FileEntry* GetEntry(uint64_t id) const override {
    return storage_.GetEntry(id);
  }
  
  [[nodiscard]] size_t GetTotalItems() const override {
    return path_storage_.GetSize();
  }

  [[nodiscard]] size_t GetStorageSize() const override {
    return path_storage_.GetStorageSize();
  }

  // Maintenance (Facade: thin wrappers; implementation in FileIndexMaintenance)
  [[nodiscard]] bool Maintain() { return maintenance_.Maintain(); }
  using MaintenanceStats = FileIndexMaintenance::MaintenanceStats;
  [[nodiscard]] MaintenanceStats GetMaintenanceStats() const {
    return maintenance_.GetMaintenanceStats();
  }

private:
  // Maintenance operations are handled by FileIndexMaintenance
  // Constants are now in FileIndexMaintenance class

  // Constants for initial capacity allocation
  // kInitialPathStorageCapacity: Sized for ~500,000 paths (typical target)
  // Calculation: 500,000 paths × 100 bytes average = 50 MB, with headroom = 64
  // MB This avoids reallocations during initial population, which would
  // fragment the contiguous buffer and degrade cache locality.
  static constexpr size_t kInitialPathStorageCapacity =
      static_cast<size_t>(64) * 1024 * 1024;  // 64MB for ~500K paths
  static constexpr size_t kInitialPathArrayCapacity =
      500000; // Sized for typical target of 500K paths
  // Build full path by walking parent chain.
  // Assumes mutex is already locked.
  // OPTIMIZED: Calculates total length first, then builds the string in a
  // single allocation.
  //
  // IMPORTANT:

  // Update all descendant paths - DELETED (Delegated to ContiguousStringBuffer)
  // void UpdateDescendantPaths(...)

  // Insert one path (caller MUST hold unique_lock on index_mutex_). Used by InsertPath and InsertPaths.
  void InsertPathUnderLock(std::string_view full_path, bool is_directory);

  // Try InsertPathUnderLock; on exception log and optionally increment out_error_count. Returns true if error.
  [[nodiscard]] bool TryInsertPathUnderLockAndCountError(std::string_view path, bool is_directory,
                                                         std::atomic<size_t>* out_error_count);

  // Rebuild path_to_id_ from current storage and path_operations (caller holds unique_lock).
  void RebuildPathToIdMapLocked();

  // Unlink (h, id) from path_to_id_ chain; caller holds lock.
  void UnlinkPathToIdEntryLocked(size_t h, uint64_t id);

  // Update index/chain after swapping entry at old_index to new_index (caller did the swap).
  void UpdatePathToIdChainAfterSwapLocked(size_t old_index, size_t new_index);

  // Helper methods for path management (assume lock is already held)
  // These now delegate to PathStorage
  void InsertPathLocked(uint64_t id, std::string_view path, bool is_directory);
  void UpdatePrefixLocked(std::string_view old_prefix,
                          std::string_view new_prefix);
  [[nodiscard]] std::string GetPathLocked(uint64_t id) const; // Assumes lock is held

  // Zero-copy path access helpers (assume lock is already held)
  [[nodiscard]] std::string_view
  GetPathViewLocked(uint64_t id) const; // Zero-copy path access
  [[nodiscard]] PathComponentsView
  GetPathComponentsViewLocked(uint64_t id) const; // Zero-copy components
  [[nodiscard]] PathComponentsView GetPathComponentsViewByIndexLocked(
      size_t idx) const; // By array index (for search loops)

  mutable std::shared_mutex index_mutex_;  // NOLINT(readability-identifier-naming) - snake_case_ per project convention; clang-tidy flags multi-word with suffix

  // Core data storage (ID -> metadata, string pool, directory cache)
  FileIndexStorage storage_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // Lazy attribute loader (handles file size and modification time loading)
  mutable LazyAttributeLoader lazy_loader_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // ============================================================================
  // PERFORMANCE-CRITICAL: Structure of Arrays (SoA) Design for Parallel Search
  // ============================================================================
  //
  // RATIONALE: Why use std::vector<char> instead of std::vector<std::string>?
  //
  // This design is a deliberate architectural choice for high-performance file
  // indexing. Replacing path_storage_ with std::vector<std::string> would
  // remove critical optimizations:
  //
  // 1. CACHE LOCALITY (Critical for Performance):
  //    - All paths stored in a single contiguous memory region
  //    - Enables excellent cache locality during parallel search operations
  //    - std::vector<std::string> would fragment memory across many separate
  //      allocations, destroying cache locality
  //
  // 2. MEMORY EFFICIENCY:
  //    - Current: ~N × average_path_length bytes (minimal overhead)
  //    - std::vector<std::string>: ~N × (average_path_length + 24-32 bytes
  //    overhead)
  //    - For 1M entries with 100-byte paths: Current = ~100MB; Proposed =
  //    ~124-132MB
  //    - std::string SSO doesn't help since paths are typically > 15 bytes
  //
  // 3. ALLOCATION EFFICIENCY:
  //    - Current: One large contiguous allocation for all paths
  //    - std::vector<std::string>: N separate allocations (one per string)
  //    - For 1M entries: Current = 1 allocation; Proposed = 1M allocations
  //    - Dramatically reduces memory fragmentation and allocation overhead
  //
  // 4. PARALLEL SEARCH PERFORMANCE:
  //    - Contiguous buffer enables efficient parallel searching across threads
  //    - Each thread accesses its chunk with excellent cache locality
  //    - With std::vector<std::string>, threads would access scattered memory
  //      locations, causing cache misses
  //    - This is a HOT PATH executed millions of times per search operation
  //
  // 5. TYPE SAFETY:
  //    - Uses bounds-checked C-style functions (strcpy_s, strcat_s) which are
  //    safe
  //    - path_offsets_ provides safe indexing into path_storage_
  //    - All access is validated (see GetPathLocked(), InsertPathLocked())
  //
  // PERFORMANCE IMPACT:
  //    - Enables searches across 1M paths in ~100ms
  //    - Sequential access to contiguous memory provides optimal cache behavior
  //    - SoA layout allows efficient parallel processing across multiple CPU
  //    cores
  //
  // See DESIGN_REVIEW 18 DEC 2025.md section 3 for detailed analysis.
  // ============================================================================

  // PathStorage manages all SoA arrays for parallel search
  // This encapsulates the Structure of Arrays design for cache-efficient searching
  PathStorage path_storage_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // PathOperations provides higher-level path operations API
  // Wraps PathStorage and handles type conversions
  PathOperations path_operations_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // Path recomputer (rebuilds all paths from storage; caller holds lock)
  PathRecomputer path_recomputer_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // Idempotent insert (Option B): one path = one entry. Single flat chain + index to avoid
  // N separate vector allocations. See docs/analysis/2026-02-19_MEMORY_IMPACT_FIX_DUPLICATE_PATH_INDEX.md.
  struct PathToIdEntry {
    size_t hash = 0;
    uint64_t id = 0;
    size_t next = static_cast<size_t>(-1);  // index into path_to_id_chain_, -1 = end
  };
  static constexpr size_t kPathToIdEnd = static_cast<size_t>(-1);
  hash_map_t<size_t, size_t> path_to_id_index_;       // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::vector<PathToIdEntry> path_to_id_chain_;      // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // Statistics for diagnostics (live counters used by FileIndexMaintenance and diagnostics)
  // These are passed to FileIndexMaintenance for statistics collection and exposed in maintenance stats
  std::atomic<size_t> remove_not_in_index_count_{  // NOLINT(readability-identifier-naming) - project convention: snake_case_
      0};  // Remove() called for files not in index
  std::atomic<size_t> remove_duplicate_count_{  // NOLINT(readability-identifier-naming) - project convention: snake_case_
      0};  // Remove() called for already-deleted files
  std::atomic<size_t> remove_inconsistency_count_{  // NOLINT(readability-identifier-naming) - project convention: snake_case_
      0};  // Remove() called but file missing from path storage

  // Maintenance operations handler
  // Initialized in constructor to reference path_storage_, index_mutex_, and counters
  FileIndexMaintenance maintenance_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // Index operations handler
  // Initialized in constructor to reference storage_, path_storage_, index_mutex_, and counters
  IndexOperations operations_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // Directory resolver for ID resolution and creation
  // Initialized in constructor to reference storage_, operations_, and next_file_id_
  DirectoryResolver directory_resolver_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // For populating from file
  std::atomic<uint64_t> next_file_id_{1};  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // Thread pool lifecycle delegated to SearchThreadPoolManager (Option C)
  SearchThreadPoolManager thread_pool_manager_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  // Note: ParallelSearchEngine is forward declared; full definition in FileIndex.cpp
  std::shared_ptr<ParallelSearchEngine> search_engine_{nullptr};  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // Get or create the thread pool (private - internal use only); updates search_engine_ if needed
  SearchThreadPool& GetThreadPool();

  // Note: ProcessChunkRangeForSearch, ProcessChunkRangeForSearchIds, CalculateChunkBytes,
  // and RecordThreadTiming have been removed as dead code. They were replaced by
  // ParallelSearchEngine::ProcessChunkRange and SearchStatisticsCollector methods.

  // Note: get_or_create_directory_id has been moved to DirectoryResolver
};

// ============================================================================
// Note: Template implementation for ProcessChunkRangeForSearch has been removed
// as dead code. It was replaced by ParallelSearchEngine::ProcessChunkRange.
// ============================================================================
