#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
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
#include "index/FileName.h"
#include "index/IIndexSearch.h"
#include "index/ISearchableIndex.h"
#include "index/LazyAttributeLoader.h"
#include "index/LazyValue.h"
#include "index/NtfsFileReference.h"
#include "index/PathRecomputer.h"
#include "path/DirectoryResolver.h"
#include "path/PathOperations.h"
#include "path/PathStorage.h"
#include "path/PathUtils.h"
#include "search/SearchThreadPoolManager.h"
#include "search/SearchTypes.h"
#include "utils/FileTimeTypes.h"
#include "utils/HashMapAliases.h"

// Forward declarations
class ParallelSearchEngine;
class SearchThreadPool;


class FileIndex : public ISearchableIndex, public IIndexSearch {  // NOSONAR(cpp:S1448) - Facade pattern: high method count acceptable; single entry point delegates to components (IndexOperations, PathRecomputer, SearchThreadPoolManager, FileIndexMaintenance, etc.)
public:
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

  // Insert policy (ingest source matrix) owned by IndexOperations; aliased
  // here so all facade callers name one type. See IndexOperations::InsertOptions.
  using InsertOptions = IndexOperations::InsertOptions;

  // Insert or update a file entry. When file_size is a loaded, non-zero value
  // (e.g. from MFT metadata reading) it is applied under the same lock
  // acquisition, avoiding a second lock acquire via UpdateFileSizeById.
  // ids are NtfsFileReference (full-FRN shape; synthetic crawl ids allowed —
  // the wrapper is non-validating, but the type names what is passed).
  // Read-path queries (GetEntry, paths, sizes) intentionally keep uint64_t:
  // they do exact lookups without masking/stripping, so the confusion class
  // does not apply there. (ResolveEntryReference is the deliberate exception:
  // stale-sequence-tolerant resolution falls back across 48-bit record keys
  // internally — see FileIndexStorage.)
  void Insert(ntfs_file_reference::NtfsFileReference id,
              ntfs_file_reference::NtfsFileReference parent_id, std::string_view name,
              bool is_directory = false,
              FILETIME modification_time = kFileTimeNotLoaded,
              uint64_t file_size = kFileSizeNotLoaded,
              InsertOptions options = InsertOptions{}) {
    const std::unique_lock lock(index_mutex_);
    InsertLocked(id, parent_id, name, is_directory, modification_time, options, file_size);
    if (!is_directory && file_size != kFileSizeNotLoaded && file_size != kFileSizeFailed) {
      storage_.UpdateFileSize(id.raw, file_size);
    }
  }

  // Internal insert logic (caller MUST hold unique_lock)
  void InsertLocked(ntfs_file_reference::NtfsFileReference id,
                    ntfs_file_reference::NtfsFileReference parent_id, std::string_view name,
                    bool is_directory = false,
                    FILETIME modification_time = kFileTimeNotLoaded,
                    InsertOptions options = InsertOptions{},
                    uint64_t file_size = kFileSizeNotLoaded) {
    operations_.Insert(id, parent_id, name, is_directory, modification_time,
                       options, file_size);
    mutation_count_.fetch_add(1);
  }

  // Owned batch entry for bulk population (e.g. baseline journal enumeration).
  // snake_case fields per project convention; name is owned because the
  // thread-local UTF-8 view it comes from is reused per record. The name is
  // a FileName (bare-name intent, explicit construction) rather than a raw
  // string, so batch producers spell the conversion and full paths stand
  // out at the call site (V5).
  struct PopulationBatchEntry {
    ntfs_file_reference::NtfsFileReference id{0};
    ntfs_file_reference::NtfsFileReference parent_id{0};
    file_name::FileName name;
    bool is_directory = false;
    FILETIME modification_time = kFileTimeNotLoaded;
    uint64_t file_size = kFileSizeNotLoaded;
  };

  // Pre-size index maps, name arena and dedup chains before a
  // bulk population. Safe to call when empty before the build loop; never
  // shrinks. Acquires unique_lock internally; do NOT hold the lock when calling.
  // Contract: the caller must use deferred path indexing (InsertBatch with
  // defer_path_indexing) followed by RecomputeAllPaths, which rebuilds
  // PathStorage (reserving exactly for full paths itself) and the path_to_id
  // maps (reusing this reservation). Without a recompute, PathStorage grows
  // without pre-size (perf only, not correctness).
  // PathStorage is excluded on purpose: RecomputeAllPaths Clear()s it (freeing
  // the reservation) and re-reserves exactly for full paths itself.
  void ReserveForPopulation(size_t expected_entries);

  // Insert a batch of entries under a single unique_lock acquisition.
  // Replaces N per-record Insert() lock/unlock pairs during bulk population.
  // No I/O inside; all parsing/filtering/MFT reads must happen before the call
  // (see docs/design/LOCK_ORDERING_AND_CRITICAL_SECTIONS.md).
  // When defer_path_indexing is true, entries are stored without path
  // indexing (no join, no InsertPath): the caller must run RecomputeAllPaths
  // before the index is searched, which indexes every path exactly once
  // instead of twice (per-insert paths are discarded by the recompute Clear).
  void InsertBatch(const std::vector<PopulationBatchEntry>& batch,
                   bool defer_path_indexing = false);

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
  void Remove(ntfs_file_reference::NtfsFileReference id);

  // Caller MUST hold unique_lock on index_mutex_ (batch USN apply path).
  void RemoveLocked(ntfs_file_reference::NtfsFileReference id);

  // Rename a file (change name only). Implementation in .cpp to update path_to_id_.
  [[nodiscard]] bool Rename(ntfs_file_reference::NtfsFileReference id, std::string_view new_name);

  // Caller MUST hold unique_lock on index_mutex_ (batch USN apply path).
  [[nodiscard]] bool RenameLocked(ntfs_file_reference::NtfsFileReference id,
                                  std::string_view new_name);

  // Move a file (change parent only). Implementation in .cpp to update path_to_id_.
  [[nodiscard]] bool Move(ntfs_file_reference::NtfsFileReference id,
                          ntfs_file_reference::NtfsFileReference new_parent_id);

  // Caller MUST hold unique_lock on index_mutex_ (batch USN apply path).
  [[nodiscard]] bool MoveLocked(ntfs_file_reference::NtfsFileReference id,
                                ntfs_file_reference::NtfsFileReference new_parent_id);

  // Evict every entry descending from a newly-filtered directory record.
  // Live USN case: a child CREATE processed before its parent was marked
  // $-filtered stays indexed (the parent is filtered, never inserted, so
  // awaiting-heal can never fix it — the entries must go, not heal). Call
  // right after marking the record filtered. No-op for id 0 and the volume
  // root (record 5). Takes MftRecordNumber (not a full FRN): the stripped
  // key is what the descendant walk matches on, and the type enforces the
  // strip at the call site instead of asserting it in Debug only.
  // Caller MUST hold unique_lock on index_mutex_ (batch USN apply path).
  void EvictDescendantsOfFilteredParentLocked(ntfs_file_reference::MftRecordNumber parent_record);

  // Invariant: every bare (separator-less) path is tracked awaiting or is a
  // synthetic drive-letter key. Diagnostic/test hook for index-drift work.
  [[nodiscard]] bool CheckBareNameInvariant() const {
    const std::shared_lock lock(index_mutex_);
    return operations_.CheckBareNameInvariant();
  }

  // True when id waits in its stored parent's awaiting bucket (bare-name
  // placeholder for a not-yet-indexed parent). The search-result commit
  // (MergeAndConvertToSearchResults) skips such entries: internal backlog,
  // not display truth. Shared-lock read; safe for UI/search threads.
  // Typed NtfsFileReference (not uint64_t like other read queries): awaiting
  // buckets are keyed by stripped record number, so the full-FRN-vs-record
  // confusion class applies here — same deliberate exception as
  // ResolveEntryReference.
  [[nodiscard]] bool IsAwaitingChild(ntfs_file_reference::NtfsFileReference id) const {
    const std::shared_lock lock(index_mutex_);
    return operations_.IsAwaitingChild(id);
  }

  // Age snapshot of the parent-missing population. Young entries are healthy
  // (close-time parents arrive milliseconds later); old ones identify
  // never-arriving parents (filtered, dropped, deleted).
  [[nodiscard]] IndexOperations::AwaitingStats GetAwaitingStats() const {
    const std::shared_lock lock(index_mutex_);
    return operations_.GetAwaitingStats();
  }

  // Cumulative placeholders re-resolved on parent arrival (heal path).
  // Diagnostic only; lock-free atomic load. Completes the awaiting flow
  // equation with GetAwaitingStats().count and the never-arriving sweep evicted total:
  // created ~= healed + evicted + removed + outstanding.
  [[nodiscard]] size_t GetHealedAwaitingTotal() const {
    return healed_awaiting_total_.load();
  }

  // Evict placeholders waiting at least max_age_ms for a parent that is not
  // coming (each removal goes through RemoveLocked, keeping awaiting buckets
  // and the path_to_id_ map consistent). Returns the evicted count.
  // Trade-off: a merely-late parent arriving afterwards cannot resurrect the
  // entry — it returns on its next USN event or full population. Keep the
  // threshold in minutes, not seconds.
  // No logging inside: in debug builds per-entry and summary lines are
  // appended to out_log_lines for the caller to emit after releasing the
  // lock — no I/O under lock (see IndexDomainEvents.h sink contract). In
  // release no strings are built (LOG_WARNING_BUILD compiles out); passing
  // nullptr disables diagnostics entirely.
  // Caller MUST hold unique_lock on index_mutex_ (batch USN apply path).
  size_t EvictNeverArrivingLocked(
      uint64_t max_age_ms,
      const index_domain_events::NeverArrivingEvictedSink& on_evicted = nullptr,
      std::vector<std::string>* out_log_lines = nullptr);

  // Evict placeholders tracked at least max_usn_gap behind current_usn in
  // journal order (OR-policy counterpart to the wall-clock above). Entries
  // with unknown position are exempt (wall-clock only). Same removals,
  // events, and trade-off as above. Log lines collected, never emitted
  // (see EvictNeverArrivingLocked).
  // Caller MUST hold unique_lock on index_mutex_ (batch USN apply path).
  size_t EvictNeverArrivingByJournalGapLocked(
      int64_t current_usn,
      int64_t max_usn_gap,
      const index_domain_events::NeverArrivingEvictedSink& on_evicted = nullptr,
      std::vector<std::string>* out_log_lines = nullptr);

  // Shared tail of the never-arriving evictions: remove each collected id,
  // publish its event, and collect a per-entry log line (debug builds only;
  // nullptr disables). Callers collect first
  // (RemoveLocked mutates storage_, invalidating iteration) and emit their
  // own summary after releasing the lock. Caller MUST hold unique_lock on index_mutex_.
  size_t EvictCollectedNeverArrivingLocked(
      const std::vector<IndexOperations::NeverArriving>& stale,
      const index_domain_events::NeverArrivingEvictedSink& on_evicted,
      std::vector<std::string>* out_log_lines = nullptr);

  // Iterate over all entries with a single shared lock, invoking the provided
  // functor for each entry. The functor should have the signature:
  // bool(uint64_t id, const FileEntry& entry) and return true to continue
  // iteration or false to stop early.
  template <typename F> void ForEachEntry(F &&fn) const {  // NOLINT(cppcoreguidelines-missing-std-forward) - fn forwarded in loop body
    const std::shared_lock lock(index_mutex_);
    ForEachEntryLockHeld(std::forward<F>(fn));
  }

  // Same as ForEachEntry but caller MUST already hold index_mutex_ (shared or unique).
  template <typename F> void ForEachEntryLockHeld(F &&fn) const {  // NOLINT(cppcoreguidelines-missing-std-forward) - fn forwarded in loop body
    for (const auto &[id, entry] : storage_) {
      // Read-path rule: iteration functors take bare uint64_t (exact lookup,
      // no strip); unwrap at the forwarder.
      if (!std::forward<F>(fn)(id.raw, entry)) {
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
        if (!invoke_with_path(id.raw, entry)) {
          break;
        }
      }
    }
  }

  // Path components view: use PathOperations type (single definition, no duplication)
  using PathComponentsView = PathOperations::PathComponentsView;

  // Safe path copy: use GetPathAccessor() then GetPathCopy().
  // GetPathCopy() acquires shared_lock internally and returns an owned std::string,
  // so the caller never holds a view into PathStorage past the lock boundary.
  struct PathAccessor {
    explicit PathAccessor(const FileIndex* index) : index_(index) {}  // NOLINT(readability-identifier-naming) - project convention: snake_case_
    [[nodiscard]] std::string GetPathCopy(uint64_t id) const {
      const std::shared_lock lock(index_->index_mutex_);
      return std::string(index_->GetPathViewLocked(id));
    }

   private:
    const FileIndex* index_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  };
  [[nodiscard]] PathAccessor GetPathAccessor() { return PathAccessor(this); }
  [[nodiscard]] PathAccessor GetPathAccessor() const { return PathAccessor(this); }

  /**
   * @brief Path view for callers that already hold index_mutex_ (shared or unique)
   *
   * Do not call from code that does not hold the lock — use GetPathAccessor().
   * Intended for ForEachEntry callbacks and other locked critical sections.
   */
  [[nodiscard]] std::string_view GetPathViewLockHeld(uint64_t id) const {
    return GetPathViewLocked(id);
  }

  // Update cached file size for a given file ID (no-op for directories).
  // Path is read under shared_lock; the I/O call is made without any lock;
  // the result is written under unique_lock. This avoids blocking searches
  // for the duration of a synchronous stat() call.
  [[nodiscard]] bool UpdateSize(uint64_t id) {
    // Phase 1: read path (shared lock — no I/O)
    std::string path;
    {
      const std::shared_lock read_lock(index_mutex_);
      if (const FileEntry* const entry = storage_.GetEntry(id);
          entry == nullptr || entry->isDirectory) {
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
    if (const FileEntry* const entry = storage_.GetEntry(id);
        entry != nullptr && !entry->isDirectory) {
      storage_.UpdateModificationTime(id, time);
    }
  }

  // Update file size for a given file ID (no-op for directories)
  // Used by MFT metadata reader to set size without path resolution
  // This method can remain even if MFT reading is removed (harmless utility)
  void UpdateFileSizeById(uint64_t id, uint64_t size) {
    const std::unique_lock lock(index_mutex_);
    if (const FileEntry* const entry = storage_.GetEntry(id);
        entry != nullptr && !entry->isDirectory) {
      storage_.UpdateFileSize(id, size);
    }
  }

  // Mark cached file size as not loaded (in-memory only; no filesystem I/O).
  // Used by the USN processor on DATA_* events so size is refreshed lazily on
  // the UI/display path instead of blocking the journal drain with GetFileSize.
  // Returns false if the entry is absent or is a directory.
  [[nodiscard]] bool InvalidateSize(uint64_t id) {
    const std::unique_lock lock(index_mutex_);
    return InvalidateSizeLocked(id);
  }

  // Caller MUST hold unique_lock on index_mutex_ (batch USN apply path).
  [[nodiscard]] bool InvalidateSizeLocked(uint64_t id) {
    if (const FileEntry* const entry = storage_.GetEntry(id);
        entry == nullptr || entry->isDirectory) {
      return false;
    }
    storage_.UpdateFileSize(id, kFileSizeNotLoaded);
    return true;
  }

  // Load file size on-demand (called by UI for visible rows)
  // Returns true if size was loaded, false if already loaded or failed
  // Delegates to LazyAttributeLoader
  [[nodiscard]] bool LoadFileSize(uint64_t id) {
    return lazy_loader_.LoadFileSize(id);  // NOSONAR(cpp:S8379) - lazy_loader_ uses index_mutex_ref_ internally
  }

  // Snapshot of the cached (already-loaded) size and modification time for an entry.
  // Both fields may carry sentinel values (kFileSizeNotLoaded, kFileTimeNotLoaded, etc.)
  // when the attribute has not yet been loaded from disk.
  struct EntryAttributeSnapshot {
    uint64_t file_size;  // NOLINT(readability-identifier-naming) - public snapshot fields use snake_case
    FILETIME last_modification_time;  // NOLINT(readability-identifier-naming) - public snapshot fields use snake_case
  };

  // Return a snapshot of the cached size and modification time for id, copied under
  // shared_lock so the caller never holds a raw pointer into the index storage.
  // Returns std::nullopt when the entry does not exist.
  // Use this instead of GetEntry() whenever only size/time fields are needed at a
  // call site that does not already hold the index lock.
  [[nodiscard]] std::optional<EntryAttributeSnapshot> TryGetCachedAttributes(uint64_t id) const {
    const std::shared_lock lock(index_mutex_);
    const FileEntry* const entry = storage_.GetEntry(id);
    if (entry == nullptr) {
      return std::nullopt;
    }
    return EntryAttributeSnapshot{entry->fileSize.value,
                                  entry->lastModificationTime.value,};
  }

  // Batch counterpart to TryGetCachedAttributes: one shared_lock for all ids.
  // out[i] is std::nullopt when ids[i] has no entry; otherwise copies cached values
  // (including sentinels when not yet loaded). ids.size() must equal out.size().
  void FillCachedAttributeSnapshots(const std::vector<uint64_t>& ids,
                                    std::vector<std::optional<EntryAttributeSnapshot>>& out) const {
    assert(ids.size() == out.size());
    const std::shared_lock lock(index_mutex_);
    for (size_t i = 0; i < ids.size(); ++i) {
      const FileEntry* const entry = storage_.GetEntry(ids.at(i));
      if (entry == nullptr) {
        out.at(i) = std::nullopt;
      } else {
        out.at(i) = EntryAttributeSnapshot{entry->fileSize.value,
                                           entry->lastModificationTime.value,};
      }
    }
  }

  // Get file size by ID - automatically loads if not yet loaded
  // UI doesn't need to know about sentinel values or loading logic
  // CRITICAL: Never hold unique_lock during I/O operations!
  // OPTIMIZATION: If modification time also needs loading, loads both together
  // Note: Method is const because cache fields are mutable
  // Delegates to LazyAttributeLoader
  [[nodiscard]] uint64_t GetFileSizeById(uint64_t id) const {
    return lazy_loader_.GetFileSize(id);  // NOSONAR(cpp:S8379) - lazy_loader_ uses index_mutex_ref_ internally
  }

  // Get file modification time by ID - automatically loads if not yet loaded
  // UI doesn't need to know about sentinel values or loading logic
  // CRITICAL: Never hold unique_lock during I/O operations!
  // OPTIMIZATION: If size also needs loading, loads both together
  // Note: Method is const because cache fields are mutable
  // Delegates to LazyAttributeLoader
  [[nodiscard]] FILETIME GetFileModificationTimeById(uint64_t id) const {
    return lazy_loader_.GetModificationTime(id);  // NOSONAR(cpp:S8379) - lazy_loader_ uses index_mutex_ref_ internally
  }

  // Get both file size and modification time by ID - loads if not yet loaded
  // Avoids duplicate locks and lookups when both attributes are needed
  // Note: Method is const because cache fields are mutable
  // Delegates to LazyAttributeLoader
  [[nodiscard]] std::pair<uint64_t, FILETIME> GetAttributesById(uint64_t id) const {
    const FileAttributes attrs = lazy_loader_.GetAttributes(id);  // NOSONAR(cpp:S8379) - lazy_loader_ uses index_mutex_ref_ internally
    return {attrs.fileSize, attrs.lastModificationTime};
  }

  // Get the number of indexed files (lock-free)
  [[nodiscard]] size_t Size() const { return storage_.Size(); }

  // Mutation version: increments on every membership/path change. Drives
  // auto-refresh (which must also fire on heals/renames that keep Size()).
  // Lock-free; monotonic (wraps only after 2^64 mutations).
  // Out of scope: attribute-only changes (size/mtime via UpdateFileSize,
  // UpdateModificationTime, InvalidateSize, lazy loads) do NOT bump this.
  // Size/time-filtered or sorted views can therefore stay stale across an
  // attribute threshold crossing until the next manual search or membership
  // change; keeping attributes live would need a separate attr version
  // (see SearchController auto-refresh note).
  [[nodiscard]] uint64_t MutationCount() const { return mutation_count_.load(); }

  // Search types (SearchStats, SearchResultData, ThreadTiming) defined in search/SearchTypes.h

  // Recompute all paths. Call after bulk insertion (e.g., initial index
  // population) to ensure paths are consistent when entries may have been
  // inserted out of order. Two steps: fill PathStorage, then rebuild path_to_id.
  void RecomputeAllPaths() {
    const ScopedTimer timer("FileIndex::RecomputeAllPaths");
    const std::unique_lock lock(index_mutex_);
    path_recomputer_.RecomputeAllPaths();
    PruneOrphanSubtreesLocked();
    operations_.ClearAwaiting();
    RebuildPathToIdMapLocked();
    // Paths were rewritten wholesale (orphans resolved or pruned): refresh
    // listeners that only watch the version, not the size.
    mutation_count_.fetch_add(1);
  }

  /**
   * After FolderCrawler finished via InsertPaths in tree order: release the temporary
   * name arena without clearing/rebuilding PathStorage. On Windows, also resets OneDrive
   * lazy-load sentinels from existing paths (same effect as PathRecomputer for those rows).
   * Do not use after USN/MFT/out-of-order population — use RecomputeAllPaths() instead.
   */
  void FinalizeFolderCrawlIndexing();

  // Parallel search
  // Returns a list of IDs that match the query
  // thread_count: -1 = auto (hardware concurrency)
  // query: Filename query (searches in filename part of path)
  // path_query: Optional path query (searches in directory part of path)
  // extensions: Optional list of allowed extensions (empty = no filter)
  // Parallel search that returns futures with per-task result batches
  // (arena-owned path bytes plus offset hits). This eliminates the need for
  // FileEntry lookups in post-processing Returns futures that will yield
  // SearchResultBatch objects; callers convert with AppendBatchToResultData
  // thread_timings: Optional output parameter to collect per-thread timing
  // information
  //                 for load balancing analysis (nullptr to disable timing
  //                 collection)
  std::vector<std::future<SearchResultBatch>>
  SearchAsyncWithData(std::string_view query, int thread_count,
                      SearchStats *stats,
                      std::string_view path_query,
                      const std::vector<std::string> *extensions,
                      ItemTypeFilter item_type_filter, bool case_sensitive,
                      std::vector<ThreadTiming> *thread_timings,
                      const std::atomic<bool> *cancel_flag,
                      const AppSettings *optional_settings) override;

  std::vector<std::future<SearchResultBatch>>
  SearchAsyncWithData(std::string_view query, int thread_count,
                      SearchStats *stats,
                      std::string_view path_query,
                      const std::vector<std::string> *extensions,
                      bool folders_only, bool case_sensitive,
                      std::vector<ThreadTiming> *thread_timings,
                      const std::atomic<bool> *cancel_flag,
                      const AppSettings *optional_settings) override;

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

  // ISearchableIndex::GetEntry implementation.
  // CAUTION: returns a raw pointer into the internal flat_hash_map; the pointer is
  // valid only while the caller holds index_mutex_ (shared or unique).  Outside a
  // lock-proven internal scope, prefer TryGetCachedAttributes() instead.
  [[nodiscard]] const FileEntry* GetEntry(uint64_t id) const override {
    return storage_.GetEntry(id);
  }

  // Non-const entry access for lock-held mutation (e.g. tests corrupting parentID).
  // Same lock caveat as GetEntry: caller must hold index_mutex_.
  [[nodiscard]] FileEntry* GetEntryMutable(uint64_t id) {
    return storage_.GetEntryMutable(id);
  }

  // Resolve an NTFS file reference to the canonical indexed id (handles stale
  // sequence numbers via record_number_to_id_). Same lock caveat as GetEntry.
  [[nodiscard]] std::pair<const FileEntry*, uint64_t> ResolveEntryReference(
      uint64_t file_reference) const {
    return storage_.ResolveEntryReference(file_reference);
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
  [[nodiscard]] std::optional<MaintenanceStats> TryGetMaintenanceStats() const {
    return maintenance_.TryGetMaintenanceStats();
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

  // Append one (id, path) to path_to_id_ chains; skips empty path. Caller holds unique_lock.
  void AppendPathToIdMapLocked(uint64_t id, std::string_view path);

  // Sync path_to_id_ after a path update (unlinks old_path if non-empty, links new_path if non-empty).
  void RefreshPathToIdAfterPathUpdateLocked(uint64_t id, std::string_view old_path,
                                            std::string_view new_path);

  // After Rename/Move succeed: sync path_to_id_ (directory → full rebuild; file → unlink old + link new).
  void RefreshPathToIdAfterRenameOrMoveLocked(uint64_t id, std::string_view old_path,
                                              const FileEntry* entry);

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

  // Protects path_storage_, storage_, path_to_id_*, operations_; shared_lock for reads,
  // unique_lock for writes. No I/O or heavy work under this lock (see docs/design/LOCK_ORDERING_AND_CRITICAL_SECTIONS.md).
  mutable std::shared_mutex index_mutex_;  // NOLINT(readability-identifier-naming) - snake_case_ per project convention; clang-tidy flags multi-word with suffix

  // Core data storage (ID -> metadata, string pool, directory cache)
  FileIndexStorage storage_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_

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

  // Lazy loader holds references to storage_, path_storage_, index_mutex_; must follow path_storage_.
  mutable LazyAttributeLoader lazy_loader_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // PathOperations provides higher-level path operations API
  // Wraps PathStorage and handles type conversions
  PathOperations path_operations_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // Path recomputer (rebuilds all paths from storage; caller holds lock)
  PathRecomputer path_recomputer_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // Idempotent insert (Option B): one path = one entry. Single flat chain + index to avoid
  // N separate vector allocations. See docs/analysis/2026-02-19_MEMORY_IMPACT_FIX_DUPLICATE_PATH_INDEX.md.
  struct PathToIdEntry {
    size_t hash = 0;  // NOLINT(readability-identifier-naming) - plain chain node fields
    uint64_t id = 0;  // NOLINT(readability-identifier-naming) - plain chain node fields
    size_t next = static_cast<size_t>(-1);  // NOLINT(readability-identifier-naming) - index into path_to_id_chain_, -1 = end
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

  // Cumulative heal count, owned here and referenced by operations_ (same
  // pattern as the remove_* counters above). Incremented once per awaiting
  // child re-resolved on parent arrival; read by the USN monitor sweep.
  std::atomic<size_t> healed_awaiting_total_{  // NOLINT(readability-identifier-naming) - project convention: snake_case_
      0};

  // Maintenance operations handler
  // Initialized in constructor to reference path_storage_, index_mutex_, and counters
  FileIndexMaintenance maintenance_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // Index operations handler
  // Initialized in constructor to reference storage_, path_storage_, index_mutex_, and counters
  IndexOperations operations_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // ID counter must be declared before DirectoryResolver: resolver holds a reference to next_file_id_
  // (member construction order is declaration order, not ctor initializer list order).
  std::atomic<uint64_t> next_file_id_{1};  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // Mutation version for auto-refresh: bumped on every index membership/path
  // change (insert/remove/rename/move/recompute/clear) so the UI refreshes on
  // heals and renames too, not just size changes. Lock-free; monotonic.
  std::atomic<uint64_t> mutation_count_{0};  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // Directory resolver for ID resolution and creation
  // Initialized in constructor to reference storage_, operations_, and next_file_id_
  DirectoryResolver directory_resolver_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // Thread pool lifecycle delegated to SearchThreadPoolManager (Option C)
  SearchThreadPoolManager thread_pool_manager_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  // Note: ParallelSearchEngine is forward declared; full definition in FileIndex.cpp
  std::shared_ptr<ParallelSearchEngine> search_engine_{nullptr};  // NOLINT(readability-identifier-naming) - project convention: snake_case_

  // Get or create the thread pool (private - internal use only); updates search_engine_ if needed
  SearchThreadPool& GetThreadPool();

  // Remove entries whose parent is absent from the index (and their descendants).
  // Prevents phantom volume-root paths like C:\origin from git remote refs.
  void PruneOrphanSubtreesLocked();
};
