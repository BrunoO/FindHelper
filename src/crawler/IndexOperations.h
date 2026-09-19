#pragma once

#include <atomic>  // NOLINT(clang-diagnostic-error) - System header, unavoidable on macOS (header-only analysis limitation)
#include <cstdint>
#include <functional>
#include <string>

#include "index/FileIndexStorage.h"
#include "index/IndexDomainEvents.h"
#include "path/PathOperations.h"
#include "path/PathUtils.h"
#include "utils/FileTimeTypes.h"
#include "utils/HashMapAliases.h"
#include <chrono>
#include <vector>

/**
 * @file IndexOperations.h
 * @brief Encapsulates coordination logic for index operations (Insert, Remove, Rename, Move)
 *
 * This class handles the coordination between FileIndexStorage and PathStorage
 * for index operations. It encapsulates the business logic that was previously
 * in FileIndex, making FileIndex a cleaner facade.
 *
 * DESIGN:
 * - Takes references to necessary components (storage, path_operations)
 * - Provides clean interface: Insert, Remove, Rename, Move
 * - All methods assume lock is already held (caller responsible for locking)
 * - Handles path computation, directory cache updates
 * - Tracks statistics (remove counts) via atomic references
 *
 * THREAD SAFETY:
 * - All methods assume caller holds unique_lock on mutex
 * - No internal locking (lock is managed by FileIndex)
 * - Statistics updates use atomic operations
 */
class IndexOperations {
public:
  /**
   * Insert policy: which ingest path an Insert belongs to. Single source of
   * truth for the flag matrix (behavior identical for all existing callers).
   *
   * The two flags vary independently (kept as bools, not an enum):
   * - LiveUsn      (register, resolve): USN journal applies; the id claims
   *                FRN identity and close-time order is repaired via
   *                track/heal. FileIndex::Insert and UsnMonitor defaults.
   * - BulkMft      (register, no resolve): MFT enumeration; FRN identity is
   *                claimed but per-insert repair is O(N^2) — RecomputeAllPaths
   *                resolves out-of-order parents in one topological pass.
   *                FileIndex::InsertBatch.
   * - Synthetic    (no register, no resolve): path-crawl file inserts whose
   *                DirectoryResolver parents are always resolved.
   *                FileIndex::InsertPathUnderLock.
   * - ResolverDir  (no register, resolve): DirectoryResolver-created
   *                directories keep the default heal attempt (preserved as-is;
   *                misses in practice — small synthetic ids don't collide with
   *                awaiting MFT-record buckets).
   * - TestPoison   (no register, resolve): live-path inserts of ids that must
   *                not claim FRN identity (record-number collision tests).
   *
   * usn_at_track records the journal position of the record that caused this
   * insert (0 = unknown: bulk MFT, synthetic, move paths). Stored on the
   * UnresolvedReference when the insert parks awaiting, so sweeps and eviction
   * forensics can reason in journal order alongside wall-clock age. The sweep
   * policy itself still uses wall-clock age only (see CollectNeverArriving).
   */
  struct InsertOptions {
    bool register_mft_record;
    bool resolve_awaiting;
    int64_t usn_at_track = 0;
    explicit constexpr InsertOptions(bool register_mft_record = true,
                                     bool resolve_awaiting = true)
        : register_mft_record(register_mft_record),
          resolve_awaiting(resolve_awaiting) {}
  };

  using PathHashRefreshCallback =
      std::function<void(uint64_t id, std::string_view old_path, std::string_view new_path)>;

  /**
   * @brief Construct IndexOperations
   *
   * @param storage Reference to FileIndexStorage
   * @param path_operations Reference to PathOperations
   * @param remove_not_in_index_count Reference to atomic counter for diagnostics
   * @param remove_inconsistency_count Reference to atomic counter for diagnostics
   * @param on_parent_healed Sink for ParentHealed domain events (invoked once
   *        per awaiting child re-resolved on parent arrival; FileIndex
   *        subscribes its healed total here, replacing counter plumbing)
   * @param on_path_refreshed Optional callback invoked whenever an entry's full
   *        path changes due to subtree healing, rename, or move, allowing
   *        path-to-id hash indexes to stay synchronized.
   */
  IndexOperations(FileIndexStorage& storage,
                  PathOperations& path_operations,
                  std::atomic<size_t>& remove_not_in_index_count,
                  std::atomic<size_t>& remove_inconsistency_count,
                  index_domain_events::ParentHealedSink on_parent_healed,
                  PathHashRefreshCallback on_path_refreshed = nullptr);

  // Default destructor (holds references, no cleanup needed)
  ~IndexOperations() = default;

  // Delete copy and move (holds references)
  IndexOperations(const IndexOperations&) = delete;
  IndexOperations& operator=(const IndexOperations&) = delete;
  IndexOperations(IndexOperations&&) = delete;
  IndexOperations& operator=(IndexOperations&&) = delete;

  /**
   * @brief Insert or update a file entry
   *
   * Coordinates between FileIndexStorage and PathStorage:
   * 1. Computes full path by walking parent chain
   * 2. Inserts into storage
   * 3. Updates path arrays
   *
   * @param id File reference (full-FRN shape; synthetic crawl ids allowed —
   *        the wrapper is non-validating, but the type names what is passed)
   * @param parent_id Parent directory reference, same shape as id
   * @param name File name
   * @param is_directory Whether this is a directory
   * @param modification_time File modification time
   * @param options Ingest-source policy (see InsertOptions): controls FRN-map
   *        registration and awaiting track/heal. Live USN heals previously
   *        inserted children waiting on this id; bulk MFT skips healing
   *        (RecomputeAllPaths covers it); synthetic ids opt out of both.
   *
   * @pre Caller must hold unique_lock on mutex
   */
  void Insert(ntfs_file_reference::NtfsFileReference id,
              ntfs_file_reference::NtfsFileReference parent_id,
              std::string_view name,
              bool is_directory = false,
              FILETIME modification_time = {UINT32_MAX, UINT32_MAX},
              InsertOptions options = InsertOptions{});

  /**
   * @brief Remove a file entry
   *
   * Coordinates between FileIndexStorage and PathStorage:
   * 1. Checks if entry exists
   * 2. Removes from directory cache if directory
   * 3. Removes from storage
   * 4. Marks path entry as deleted
   * 5. Tracks statistics for diagnostics
   *
   * @param id File reference to remove
   *
   * @pre Caller must hold unique_lock on mutex
   */
  void Remove(ntfs_file_reference::NtfsFileReference id);

  /**
   * @brief Rename a file (change name only)
   *
   * Coordinates between FileIndexStorage and PathStorage:
   * 1. Retrieves old full path
   * 2. Computes new full path
   * 3. Updates entry in storage
   * 4. Updates all descendant paths if directory
   * 5. Updates directory cache
   * 6. Updates path arrays
   *
   * @param id File reference
   * @param newName New file name
   * @return true if rename succeeded, false if file not found
   *
   * @pre Caller must hold unique_lock on mutex
   */
  bool Rename(ntfs_file_reference::NtfsFileReference id, std::string_view new_name);

  /**
   * @brief Move a file (change parent only)
   *
   * Coordinates between FileIndexStorage and PathStorage:
   * 1. Retrieves old full path
   * 2. Computes new full path
   * 3. Updates entry in storage
   * 4. Updates all descendant paths if directory
   * 5. Updates directory cache
   * 6. Updates path arrays
   *
   * @param id File reference
   * @param newParentID New parent directory reference
   * @return true if move succeeded, false if file not found
   *
   * @pre Caller must hold unique_lock on mutex
   */
  bool Move(ntfs_file_reference::NtfsFileReference id,
            ntfs_file_reference::NtfsFileReference new_parent_id);

  /**
   * @brief Drop all awaiting (unresolved-parent) state, e.g. after Clear() or
   * RecomputeAllPaths() which resolves or prunes every orphan in one pass.
   *
   * @pre Caller must hold unique_lock on mutex
   */
  void ClearAwaiting();

  /**
   * @brief Number of child ids currently waiting on a missing parent.
   *
   * Diagnostic/test hook. Counts entries across all parent buckets.
   *
   * @pre Caller must hold unique_lock or shared_lock on mutex
   */
  [[nodiscard]] size_t AwaitingCount() const;

  /**
   * @brief Age snapshot of the awaiting (parent-missing) population.
   *
   * A young population is healthy (parents arrive milliseconds later via
   * close-time records). An old one identifies never-arriving parents:
   * filtered, dropped, or deleted before their CREATE was consumed.
   */
  struct AwaitingStats {
    size_t count = 0;
    uint64_t oldest_age_ms = 0;
  };

  /**
   * @brief Count awaiting children and the age of the longest-waiting one.
   *
   * Feeds the USN monitor metrics (count + oldest age distinguish late
   * parents from never-arriving ones). O(awaiting).
   *
   * @pre Caller must hold unique_lock or shared_lock on mutex
   * (FileIndex::GetAwaitingStats acquires shared_lock itself, so direct
   * FileIndex callers satisfy this without locking manually).
   */
  [[nodiscard]] AwaitingStats GetAwaitingStats() const;

  /**
   * @brief An awaiting child at or beyond the never-arriving age threshold.
   *
   * usn_at_track rides along from the UnresolvedReference (0 = unknown);
   * eviction forensics correlate it with USN logs in journal order.
   */
  struct NeverArriving {
    uint64_t id = 0;
    uint64_t age_ms = 0;
    int64_t usn_at_track = 0;
  };

  /**
   * @brief Collect awaiting children waiting at least max_age_ms.
   *
   * Pure query (no mutation): the caller evicts via RemoveLocked so the
   * path_to_id_ map stays consistent. O(awaiting).
   *
   * @pre Caller must hold unique_lock or shared_lock on mutex
   */
  void CollectNeverArriving(uint64_t max_age_ms, std::vector<NeverArriving>& out) const;

  /**
   * @brief Collect awaiting children tracked at least max_usn_gap behind
   * current_usn in journal order.
   *
   * Journal-order counterpart to CollectNeverArriving: on a busy journal many
   * megabytes can pass in seconds, so a large USN gap without parent arrival
   * is a never-arriving signal even when wall-clock age is young. Entries
   * with unknown position (usn_at_track == 0) are skipped — they fall back to
   * wall-clock policy. Pure query (no mutation), O(awaiting).
   *
   * @pre Caller must hold unique_lock or shared_lock on mutex
   */
  void CollectNeverArrivingByJournalGap(int64_t current_usn, int64_t max_usn_gap,
                                        std::vector<NeverArriving>& out) const;

  /**
   * @brief Collect ids descending from a parent record (never-inserted OK).
   *
   * Unlike RemoveIndexedSubtree (exact-id membership), matches by 48-bit MFT
   * record number so children of a filtered directory that was never indexed
   * are still found. Used to evict entries that slipped in before their
   * parent was marked filtered (live out-of-order CREATE). Never collects
   * through the volume root (record 5) or id 0.
   *
   * @pre Caller must hold unique_lock or shared_lock on mutex
   */
  void CollectDescendantIds(ntfs_file_reference::MftRecordNumber parent_record,
                            std::vector<uint64_t>& out) const;

  /**
   * @brief Invariant: every bare (separator-less) path is tracked awaiting.
   *
   * Bare names are legitimate only as out-of-order placeholders waiting on
   * their parent (or synthetic "C:" drive-letter keys, which are exempt).
   * Anything else is index drift. Diagnostic/test hook.
   *
   * @pre Caller must hold unique_lock or shared_lock on mutex
   */
  [[nodiscard]] bool CheckBareNameInvariant() const;

  /**
   * @brief True when child_id waits in the awaiting bucket of its stored parent.
   *
   * Public for the search-result commit (MergeAndConvertToSearchResults skips
   * awaiting entries: bare-name placeholders are internal backlog, not
   * display truth).
   *
   * @pre Caller must hold unique_lock or shared_lock on mutex.
   */
  [[nodiscard]] bool IsAwaitingChild(ntfs_file_reference::NtfsFileReference child_id) const;

   private:
    /**
     * @brief Record a child inserted with an unresolvable parent.
     *
     * Keyed by 48-bit MFT record number so a later parent INSERT with a
     * different sequence number still matches (ParentFileReferenceNumber often
     * carries a stale sequence). Volume-root (record 5) and zero parents are
     * never tracked — they resolve to the volume root immediately.
     * usn_at_track is stored on the reference (0 = unknown); re-tracking an
     * already-tracked child keeps the original value.
     *
     * @pre Caller must hold unique_lock on mutex (lock already held).
     */
    void AwaitParent(ntfs_file_reference::NtfsFileReference parent_id,
                     ntfs_file_reference::NtfsFileReference child_id,
                     int64_t usn_at_track = 0);

    /**
     * @brief Remove a child from its parent's awaiting bucket (healed, moved, removed).
     *
     * Takes the pre-stripped record key (all callers strip via RecordNumber()
     * first); a full FRN here would miss the bucket and leak the entry, so the
     * type enforces the strip at the call site instead of trusting it.
     *
     * @pre Caller must hold unique_lock on mutex (lock already held).
     */
    void ForgetUnresolved(ntfs_file_reference::MftRecordNumber parent_record,
                          uint64_t child_id);

   /**
    * @brief Publish one ParentHealed event for a re-resolved child.
    *
    * Extracted from HealAwaiting to keep it under the cognitive-complexity
    * budget; null sink disables publication.
    *
    * @pre Caller must hold unique_lock on mutex (lock already held).
    */
    void PublishParentHealed(uint64_t child_id, uint64_t parent_id) const;

   /**
    * @brief Heal children waiting on a newly inserted parent (BFS, transitive).
    *
    * For each direct awaiting child: rebuild its path as Join(parent_path,
    * leaf_name) and cascade to its subtree via SyncHealedChildSubtree
    * (covers grandchildren that joined onto the placeholder). Healed
    * directory children are enqueued so grandchildren that arrived before
    * the child (still bare) are fixed in the same pass.
    *
    * @pre Caller must hold unique_lock on mutex (lock already held).
    */
    void HealAwaiting(ntfs_file_reference::NtfsFileReference new_parent_id);

   /**
    * @brief True when entry_id descends from ancestor_id via stored parent links.
    *
    * Bounded walk (stale-sequence tolerant: compares 48-bit MFT record
    * numbers, like ResolveEntryReference). Used to scope the heal cascade —
    * bare placeholders are not unique, so a global string-prefix rewrite
    * could mis-graft an unrelated subtree sharing the same dir name.
    *
    * @pre Caller must hold unique_lock or shared_lock on mutex.
    */
   [[nodiscard]] bool IsDescendantOf(uint64_t entry_id, uint64_t ancestor_id) const;

   /**
    * @brief Rewrite an entry's path and its true descendants only.
    *
    * Shared tail of Insert-heal, Rename() and Move(): descendant updates apply
    * only to entries whose parent chain passes through child_id (see
    * IsDescendantOf) instead of every path sharing the old string prefix —
    * bare placeholders are not unique, so a global rewrite could mis-graft
    * an unrelated same-named subtree. Skips volume-wide prefixes ("C:\\",
    * "/") that would relocate a whole volume, and syncs the directory cache.
    *
    * @pre Caller must hold unique_lock on mutex (lock already held).
    */
    void SyncHealedChildSubtree(std::string_view old_full_path,
                                std::string_view new_full_path,
                                ntfs_file_reference::NtfsFileReference child_id,
                                bool is_directory);

  // A child inserted before its parent, waiting for re-resolution.
  // Glossary term: UnresolvedReference
  // (docs/design/USN_MFT_UBIQUITOUS_LANGUAGE.md). Lifecycle: tracked here on
  // insert, removed on parent arrival / move / delete, reported by
  // GetAwaitingStats / CollectNeverArriving. Age math lives here so the
  // sweep policy reads as domain logic instead of chrono arithmetic.
  // usn_at_track is the journal position of the tracking record (0 = unknown:
  // bulk/synthetic/move paths); re-tracking preserves it, never refreshes it.
  struct UnresolvedReference {
    uint64_t id = 0;
    std::chrono::steady_clock::time_point tracked_at;
    int64_t usn_at_track = 0;

    [[nodiscard]] uint64_t AgeMs(std::chrono::steady_clock::time_point now) const noexcept {
      return static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(now - tracked_at).count());
    }
  };

  // Children inserted before their parent (bare-name placeholders), keyed by
  // 48-bit MFT record number of the missing parent. Small live-only map:
  // bulk population skips healing and RecomputeAllPaths clears it.
  // tracked_at identifies never-arriving parents (see GetAwaitingStats).
  // Keyed by MftRecordNumber so a full FRN cannot key a stripped bucket
  // without an explicit strip (see AwaitParent).
  flat_hash_map_t<ntfs_file_reference::MftRecordNumber, std::vector<UnresolvedReference>>
      awaiting_parent_;

   FileIndexStorage& storage_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  PathOperations& path_operations_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::atomic<size_t>& remove_not_in_index_count_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  std::atomic<size_t>& remove_inconsistency_count_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  // ParentHealed sink: invoked once per awaiting child re-resolved via
  // HealAwaiting (parent arrival). Diagnostic only: completes the awaiting
  // flow equation (created ~= healed + evicted + removed + outstanding).
  // Entries that leave the buckets via Remove/ClearAwaiting are not heals
  // and emit nothing. Null sink disables publication.
  index_domain_events::ParentHealedSink on_parent_healed_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  PathHashRefreshCallback on_path_refreshed_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_
};

