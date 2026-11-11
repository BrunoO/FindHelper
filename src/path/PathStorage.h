#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * @file PathStorage.h
 * @brief Manages Structure of Arrays (SoA) for high-performance parallel search
 *
 * PathStorage encapsulates the SoA layout used for cache-efficient parallel
 * searching. This design maintains excellent cache locality by storing all paths
 * in a contiguous buffer, enabling searches across 1M+ paths in ~100ms.
 *
 * DESIGN RATIONALE:
 * - Contiguous path storage for cache locality
 * - Structure of Arrays (SoA) for parallel search efficiency
 * - Pre-parsed offsets (filename_start, extension_start) eliminate parsing overhead
 * - Tombstone deletion (is_deleted_) for efficient removal without reallocation
 *
 * PERFORMANCE:
 * - Enables searches across 1M paths in ~100ms
 * - Zero-copy read access via SoAView for search operations
 * - Contiguous memory layout provides optimal cache behavior
 */

/**
 * @class PathStorage
 * @brief Manages SoA arrays for path storage and parallel search
 *
 * This class encapsulates the Structure of Arrays design used for high-performance
 * parallel searching. All paths are stored in a contiguous buffer with parallel
 * arrays tracking offsets, IDs, and metadata.
 */
class PathStorage {
public:
  /**
   * @brief Read-only view of SoA arrays for search operations
   *
   * This struct provides zero-copy access to the SoA arrays for parallel search.
   * The arrays are accessed via raw pointers, maintaining optimal performance.
   *
   * CRITICAL: This view is only valid while the PathStorage instance exists
   * and no modifications are made. Caller must ensure proper synchronization.
   */
  struct SoAView {

    const char *path_storage = nullptr;        // Contiguous path buffer

    const size_t *path_offsets = nullptr;      // Offset into path_storage_ for each entry

    const uint64_t *path_ids = nullptr;        // ID for each path entry

    const size_t *filename_start = nullptr;    // Filename offset in path

    const size_t *extension_start = nullptr;   // Extension offset (SIZE_MAX = no extension)

    const uint8_t *is_deleted = nullptr;       // Tombstone flag (0 = not deleted, 1 = deleted)

    const uint8_t *is_directory = nullptr;     // Directory flag (0 = file, 1 = directory)

    size_t size = 0;                      // Number of entries

    SoAView() = default;

    /**
     * @brief Computes path length excluding null terminator for entry at index
     * @param index Index of the path entry (must be < size)
     * @param storage_size Total size of path storage (for last entry calculation)
     * @return Path length excluding null terminator
     */
    [[nodiscard]] size_t GetPathLength(size_t index, size_t storage_size) const noexcept {
      assert(index < size && "Path index must be within SoA bounds");
      if (index + 1 < size) {
        return path_offsets[index + 1] - path_offsets[index] - 1;
      }
      return storage_size - path_offsets[index] - 1;
    }
  };

  /**
   * @brief Construct PathStorage with initial capacity
   */
  PathStorage();

  // Default destructor (RAII members handle cleanup)
  ~PathStorage() = default;

  // Delete copy constructor and assignment (PathStorage is not copyable)
  PathStorage(const PathStorage &) = delete;
  PathStorage &operator=(const PathStorage &) = delete;

  // Delete move constructor and assignment (atomic members are not movable)
  PathStorage(PathStorage &&) = delete;
  PathStorage &operator=(PathStorage &&) = delete;

  /**
   * @brief Insert or update a path entry
   *
   * @param id File ID
   * @param path Full path string
   * @param isDirectory True if this is a directory  // NOLINT(readability-identifier-naming) - Public API parameter name
   * @param existing_index If set, try in-place update at this index (e.g. rename); else append
   * @return SoA index of the entry (existing or newly appended)
   */
  [[nodiscard]] size_t InsertPath(uint64_t id, std::string_view path, bool isDirectory,  // NOLINT(readability-identifier-naming) - Public API parameter names
                                  std::optional<size_t> existing_index = std::nullopt);

  /**
   * @brief Fast sequential bulk append of a path during initial index recomputation
   *
   * Bypasses tombstone checks and in-place updates. If filename_start_offset and
   * extension_start_offset are provided, bypasses path string scanning as well.
   *
   * @param id File ID
   * @param path Full path string
   * @param is_directory True if directory
   * @param filename_start_offset Pre-calculated filename start offset
   * @param extension_start_offset Pre-calculated extension start offset
   * @return SoA index of the newly appended entry
   */
  [[nodiscard]] size_t BulkAppendPath(
      uint64_t id, std::string_view path, bool is_directory,
      std::optional<size_t> filename_start_offset = std::nullopt,
      std::optional<size_t> extension_start_offset = std::nullopt);

  /**
   * @brief Mark the path entry at the given SoA index as deleted (tombstone)
   *
   * @param index SoA array index
   * @return true if the slot was marked deleted, false if index invalid or already deleted
   */
  [[nodiscard]] bool RemovePathByIndex(size_t index);

  /**
   * @brief Update all paths with a given prefix
   *
   * @param oldPrefix Old path prefix to replace
   * @param newPrefix New path prefix
   * @param on_index_changed Called with (file_id, old_index, new_index) when a
   *        path is re-allocated at a new index (new path longer than old slot).
   *        Caller should update FileEntry.path_storage_index so it points to the
   *        new slot; otherwise GetPathByIndex will return {} for those entries.
   *        The old index is provided so trigram/posting stores keyed by row-id
   *        can move their entries to the new row.
   *
   * Used when renaming/moving directories to update all descendant paths.
   */
  template<typename OnIndexChanged>
  void UpdatePrefix(std::string_view oldPrefix, std::string_view newPrefix,  // NOLINT(readability-identifier-naming) - Public API parameter names
                    const OnIndexChanged& on_index_changed);  // NOLINT(readability-identifier-naming) - Public API parameter names

  /**
   * @brief Get a read-only view of SoA arrays for search operations
   *
   * This method provides zero-copy access to the arrays for parallel search.
   * The view is only valid while no modifications are made to PathStorage.
   *
   * @return SoAView containing pointers to all arrays
   */
  [[nodiscard]] SoAView GetReadOnlyView() const;

  /**
   * @brief Get path by array index (for search operations and id→path via FileEntry.path_storage_index)
   *
   * @param index Array index
   * @return String view of path, or empty view if index is invalid
   */
  [[nodiscard]] std::string_view GetPathByIndex(size_t index) const;

  /**
   * @brief Path components view (zero-copy access to path parts)
   */
  struct PathComponentsView {
    std::string_view full_path{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
    std::string_view filename{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
    std::string_view extension{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
    std::string_view directory_path{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
    bool has_extension = false;
  };

  /**
   * @brief Get path components by array index
   *
   * @param index Array index
   * @return PathComponentsView with all path components
   */
  [[nodiscard]] PathComponentsView GetPathComponentsByIndex(size_t index) const;

  /**
   * @brief Get the number of entries (including deleted)
   */
  [[nodiscard]] size_t GetSize() const { return path_ids_.size(); }

  /**
   * @brief Get the number of deleted entries
   */
  [[nodiscard]] size_t GetDeletedCount() const {
    return deleted_count_.load();
  }

  /**
   * @brief Rebuild path buffer to remove deleted entries
   *
   * Defragments storage by removing deleted entries and rebuilding the buffer.
   * Calls on_rebuilt_entry(file_id, new_index) for each kept entry so caller can
   * update FileEntry.path_storage_index.
   */
  template<typename OnRebuiltEntry>
  void RebuildPathBuffer(const OnRebuiltEntry& on_rebuilt_entry);

  /**
   * @brief Clear all entries
   */
  void Clear();

  /**
   * @brief Pre-size SoA arrays before a bulk population.
   *
   * Never shrinks (vector::reserve is a no-op below capacity). Caller must
   * guarantee exclusive access (e.g. hold FileIndex index_mutex_).
   */
    void Reserve(size_t expected_entries, size_t expected_path_bytes);

    /**
     * @brief Begin a sequential bulk append after clearing the storage.
     *
     * Pre-sizes all SoA arrays so BulkAppendPath can write entries directly.
     * FinishBulkAppend must be called after the final append.
     */
    void BeginBulkAppend(size_t expected_entries, size_t expected_path_bytes);

    /**
     * @brief Finish a sequential bulk append and validate the SoA invariant.
     */
    void FinishBulkAppend();

  /**
   * @brief Get statistics for diagnostics
   */
  struct Stats {

    size_t total_entries = 0;  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check

    size_t deleted_entries = 0;  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check

    size_t path_storage_bytes = 0;  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
    size_t path_storage_capacity = 0;  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
    size_t path_array_capacity = 0;  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check

    size_t rebuild_count = 0;  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
  };
  [[nodiscard]] Stats GetStats() const;

  /**
   * @brief Get path storage size in bytes
   */
  [[nodiscard]] size_t GetStorageSize() const { return path_storage_.size(); }

private:
  /**
   * @brief Internal helper to clear all storage arrays and reset counters
   *
   * This is used by both Clear() and RebuildPathBuffer() to eliminate code duplication.
   */
  void ClearAll();
  // Structure of Arrays (SoA) for cache-efficient parallel search
  std::vector<char> path_storage_;  // NOLINT(readability-identifier-naming) - project uses snake_case_ for members
  // Monotonic-offsets invariant: path_offsets_ is STRICTLY increasing by row
  // index, so row i owns the byte span [offset_i, offset_{i+1}) (last row: up
  // to the storage size) and every span is at least 1 byte (the NUL terminator).
  // Proven from the mutation surface: AppendString allocates spans sequentially
  // at append time; in-place InsertPath updates keep equal lengths (offsets
  // unchanged); length-changing updates tombstone + append at the tail; bulk
  // append assigns in index order. Two consumers derive lengths from offset
  // deltas and never strlen: SoAView::GetPathLength (search hot path) and
  // RebuildPathBuffer (compaction byte spans). Debug builds assert the
  // invariant at compaction time so a future mutation path that reorders or
  // reuses offsets fails loudly instead of silently corrupting both.
  std::vector<size_t> path_offsets_;  // NOLINT(readability-identifier-naming) - retained suppression
  std::vector<uint64_t> path_ids_;  // NOLINT(readability-identifier-naming) - retained suppression
  std::vector<size_t> filename_start_;  // NOLINT(readability-identifier-naming) - retained suppression
  std::vector<size_t> extension_start_;  // NOLINT(readability-identifier-naming) - retained suppression
  std::vector<uint8_t> is_deleted_;  // NOLINT(readability-identifier-naming) - retained suppression
  std::vector<uint8_t> is_directory_;  // NOLINT(readability-identifier-naming) - retained suppression

  // Statistics
    std::atomic<size_t> deleted_count_{0};  // NOLINT(readability-identifier-naming) - retained suppression
    std::atomic<size_t> rebuild_count_{0};  // NOLINT(readability-identifier-naming) - retained suppression
    bool bulk_append_active_ = false;  // NOLINT(readability-identifier-naming) - retained suppression
    size_t bulk_append_next_index_ = 0;  // NOLINT(readability-identifier-naming) - retained suppression
    size_t bulk_append_expected_entries_ = 0;  // NOLINT(readability-identifier-naming) - retained suppression

  // Helper to append string to contiguous buffer
  size_t AppendString(std::string_view str);

  // Helper to parse path and extract filename/extension offsets
  void ParsePathOffsets(std::string_view path, size_t &filename_start,
                        size_t &extension_start) const;

  // Assert that all SoA arrays have equal length (invariant check).
  void AssertSoAInvariant(const char* context) const;

  // Constants for initial capacity allocation
  // kInitialPathStorageCapacity: Sized for ~500,000 paths (typical target)
  // Calculation: 500,000 paths × 100 bytes average = 50 MB, with headroom = 64 MB
  // This avoids reallocations during initial population, which would fragment
  // the contiguous buffer and degrade cache locality.
  static constexpr size_t kInitialPathStorageCapacity =
      64ULL * 1024ULL * 1024ULL;  // 64MB for ~500K paths; ULL avoids implicit widening
  static constexpr size_t kInitialPathArrayCapacity =
      500000U;  // Sized for typical target of 500K paths
};

// Template implementations (S5213: avoid std::function for zero-overhead callables)
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - SoA indices bounded by size(); same as .cpp
template<typename OnIndexChanged>
void PathStorage::UpdatePrefix(std::string_view oldPrefix,  // NOLINT(readability-identifier-naming) - retained suppression
                               std::string_view newPrefix,  // NOLINT(readability-identifier-naming) - retained suppression
                               const OnIndexChanged& on_index_changed) {  // NOLINT(readability-identifier-naming) - retained suppression
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init) - path default-initialized then assigned in push_back
  struct UpdateInfo {
    uint64_t file_id = 0;
    std::string path;
    bool is_directory = false;
    size_t index = 0;
  };
  std::vector<UpdateInfo> updates_full{};
  updates_full.reserve(100);  // NOLINT(readability-magic-numbers) - Heuristic capacity

  const size_t count = path_ids_.size();
  const size_t old_len = oldPrefix.length();

  for (size_t i = 0; i < count; ++i) {
    if (is_deleted_[i] != 0) {
      continue;
    }
    const char* path = &path_storage_[path_offsets_[i]];
    const size_t path_len = std::strlen(path);  // NOSONAR(cpp:S1081) - Safe: path_storage_ entries are null-terminated
    if (path_len < old_len) {
      continue;
    }
    if (std::string_view(path, old_len) == oldPrefix) {
      const size_t suffix_len = path_len - old_len;
      std::string new_path_str;
      new_path_str.reserve(newPrefix.length() + suffix_len);
      new_path_str.append(newPrefix);
      new_path_str.append(path + old_len, suffix_len);
      updates_full.push_back(
          {path_ids_[i], std::move(new_path_str), (is_directory_[i] == 1), i});
    }
  }

  for (const auto& update : updates_full) {
    if (const size_t new_index =
            InsertPath(update.file_id, update.path, update.is_directory, update.index);
        new_index != update.index) {
      on_index_changed(update.file_id, update.index, new_index);
    }
  }
}

template<typename OnRebuiltEntry>
void PathStorage::RebuildPathBuffer(const OnRebuiltEntry& on_rebuilt_entry) {
  assert(!bulk_append_active_ && "Cannot rebuild PathStorage during bulk append");
  rebuild_count_.fetch_add(1);
  const size_t entry_count = path_ids_.size();
  const size_t live_count = entry_count - deleted_count_.load();
  const size_t old_storage_size = path_storage_.size();

  // BYTE-SPAN RULE: a row owns its path bytes plus the NUL terminator, from
  // its offset up to the next row's offset (or the end of the buffer for the
  // last row). AppendString allocates spans strictly in SoA index order and
  // in-place updates keep equal lengths, so offsets stay strictly increasing
  // and this derivation needs no strlen pass per row (same rule as
  // SoAView::GetPathLength).
  //
  // Compaction streams live rows straight into fresh destination buffers with
  // exact-size reserves and zero per-row allocations. The previous
  // collect-into-std::string -> ClearAll -> AppendString round trip made one
  // heap allocation per surviving path (millions at 1M+ row scale) and
  // dominated the index_mutex_ exclusive hold in FileIndex::RebuildPathBuffer,
  // which showed up in Metrics as multi-second UI stalls with the write lock
  // held.
#ifndef NDEBUG
  // Fail loudly if the monotonic-offsets invariant (see path_offsets_ doc) is
  // ever broken: both the span sums below and SoAView::GetPathLength derive
  // lengths from offset deltas and would silently corrupt on reordered or
  // reused offsets. O(total) debug-only scan at the one operation that
  // consumes the invariant across all rows.
  for (size_t verify = 1; verify < entry_count; ++verify) {
    assert(path_offsets_[verify] > path_offsets_[verify - 1] &&
           "path_offsets_ must be strictly increasing by row index");
  }
  assert((entry_count == 0 || path_offsets_[entry_count - 1] < old_storage_size) &&
         "last row must own at least its NUL byte within the path buffer");
#endif  // NDEBUG
  size_t total_bytes = 0;
  for (size_t i = 0; i < entry_count; ++i) {
    if (is_deleted_[i] == 0) {
      const size_t end = (i + 1 < entry_count) ? path_offsets_[i + 1] : old_storage_size;
      total_bytes += end - path_offsets_[i];
    }
  }

  std::vector<char> compacted_storage;
  compacted_storage.reserve(total_bytes);
  std::vector<size_t> compacted_offsets;
  compacted_offsets.reserve(live_count);
  std::vector<uint64_t> compacted_ids;
  compacted_ids.reserve(live_count);
  std::vector<size_t> compacted_filename_start;
  compacted_filename_start.reserve(live_count);
  std::vector<size_t> compacted_extension_start;
  compacted_extension_start.reserve(live_count);
  std::vector<uint8_t> compacted_is_directory;
  compacted_is_directory.reserve(live_count);

  for (size_t i = 0; i < entry_count; ++i) {
    if (is_deleted_[i] != 0) {
      continue;
    }
    const size_t begin = path_offsets_[i];
    const size_t end = (i + 1 < entry_count) ? path_offsets_[i + 1] : old_storage_size;
    const size_t new_index = compacted_offsets.size();
    compacted_offsets.push_back(compacted_storage.size());
    // Spans include the NUL terminator, so the compacted buffer stays
    // byte-identical per row (null-terminated, offsets preserved).
    compacted_storage.insert(compacted_storage.end(),
                             path_storage_.begin() + static_cast<ptrdiff_t>(begin),
                             path_storage_.begin() + static_cast<ptrdiff_t>(end));
    compacted_ids.push_back(path_ids_[i]);
    compacted_filename_start.push_back(filename_start_[i]);
    compacted_extension_start.push_back(extension_start_[i]);
    compacted_is_directory.push_back(is_directory_[i]);
    on_rebuilt_entry(path_ids_[i], new_index);
  }

  // Move-assign frees the old buffers as a side effect (no shrink_to_fit
  // churn) and leaves exact-size capacities, matching ClearAll + reserve.
  path_storage_ = std::move(compacted_storage);
  path_offsets_ = std::move(compacted_offsets);
  path_ids_ = std::move(compacted_ids);
  filename_start_ = std::move(compacted_filename_start);
  extension_start_ = std::move(compacted_extension_start);
  is_directory_ = std::move(compacted_is_directory);
  is_deleted_.assign(live_count, 0);

  deleted_count_.store(0);
  AssertSoAInvariant("SoA arrays must remain synchronized after RebuildPathBuffer");
}
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
