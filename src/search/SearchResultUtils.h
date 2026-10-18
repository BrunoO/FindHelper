#pragma once

/**
 * @file SearchResultUtils.h
 * @brief Utility functions for working with SearchResult data
 *
 * This module provides utilities for:
 * - Lazy loading and formatting of file size and modification time
 * - Time filter cache maintenance (efficient filtering of search results)
 * - Sorting helpers for the results table (with pre-fetching for performance)
 *
 * These functions are shared between the main application and UIRenderer to
 * avoid code duplication and ensure consistent behavior.
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "gui/AsyncSortState.h"
#include "imgui.h"
#include "index/FileIndex.h"
#include "search/SearchTypes.h"
#include "utils/FileAttributeConstants.h"
#include "utils/FileTimeTypes.h"
#include "utils/StringUtils.h"

// Forward declarations
class FolderSizeAggregator;
class GuiState;
class ThreadPool;
struct SearchResult;

namespace recycle_bin {
struct RecycleMetadataSnapshot;
}  // namespace recycle_bin

/**
 * @brief Reusable scratch buffers for MergeAndConvertToSearchResults.
 *
 * Hoisting these buffers across search queries eliminates multi-megabyte heap
 * allocations (file IDs, result indices, and attribute snapshots) on every keystroke.
 * UI-thread confined when owned by SearchController.
 */
struct SearchResultMergeScratch {
  std::vector<uint64_t> file_ids;
  std::vector<size_t> file_result_indices;
  std::vector<std::optional<FileIndex::EntryAttributeSnapshot>> snapshots;
  /**
   * Main-scan hit ids plus every snapshot id the merge resolved and inserted --
   * including the ones it resolved then dropped (already displayed, or no longer
   * under the Recycle Bin); ids absent from the index are rejected before the set
   * is touched, so membership means "resolved, not necessarily accepted". That is
   * exactly what the M6-dedup pass over SearchResultBatch::recycle_hit_file_ids
   * needs, since a dropped id must not be revisited by a later batch. Only
   * populated when the merge actually received snapshot ids (zero cost on the
   * common path); kept here so the node buckets survive across searches.
   */
  std::unordered_set<uint64_t> seen_file_ids;

  void Clear() noexcept {
    file_ids.clear();
    file_result_indices.clear();
    snapshots.clear();
    seen_file_ids.clear();
  }
};

/**
 * @brief Ensure display strings (size, modification time) are populated for a result
 *
 * This function performs lazy loading of file metadata:
 * - Loads file size if not yet loaded (directories have size = 0)
 * - Formats size display string (e.g., "1.5 MB")
 * - Loads modification time if not yet loaded
 * - Formats modification time display string (e.g., "2024-01-15 10:30")
 *
 * Note: Takes const ref because SearchResult's cached fields (fileSize,
 * fileSizeDisplay, lastModificationTime, lastModificationDisplay) are mutable
 * to allow lazy loading through const references.
 *
 * @param result SearchResult to populate (cached fields may be modified)
 * @param file_index File index for loading metadata
 */
void EnsureDisplayStringsPopulated(const SearchResult& result,
                                   const FileIndex& file_index);

/**
 * @brief Handoff folder size and file count stats from aggregator to search results.
 *
 * Enqueues pending directories in one batch, collects computed stats, and updates display strings.
 * Runs whenever the result pool is non-empty (including during in-flight searches that keep
 * the previous result set visible). Triggers a re-sort only when resultsComplete is true and
 * the active sort column is Size or FolderFiles, once all pending directory stats are written.
 *
 * @param state GUI state containing search results and sort options
 * @param aggregator Folder size aggregator instance (nullable)
 * @return Number of results updated with newly completed stats this frame
 */
size_t FlushAggregatorFolderStats(GuiState& state, FolderSizeAggregator* aggregator);

/**
 * @brief Merge worker-task batches into a path pool and convert to SearchResult.
 *
 * Appends each hit's arena bytes to pool (with trailing null for .c_str()
 * use); each SearchResult.fullPath is a string_view into the pool. Batches
 * must outlive the call (their arenas are read throughout). Cached file
 * size/time are copied via a single FillCachedAttributeSnapshots batch lookup.
 * Caller must clear pool before calling when replacing all results.
 *
 * Also resolves each batch's SearchResultBatch::recycle_hit_file_ids (Approach-B
 * secondary snapshot pass, spec §5 Phase 4 item 3): ids still present in the
 * index and still under the Recycle Bin are appended as physical $R rows after
 * the primary rows, deduped against the primary hits (rule M6-dedup). The
 * snapshot's ORIGINAL strings never enter the pool.
 *
 * @param pool Destination path pool for string_view backing
 * @param batches Worker batches containing hits and path arenas
 * @param file_index File index for loading cached attributes
 * @param scratch Optional reusable scratch buffer to eliminate per-search heap allocations
 */
std::vector<SearchResult> MergeAndConvertToSearchResults(
    std::vector<char>& pool,
    const std::vector<SearchResultBatch>& batches,
    const FileIndex& file_index,
    SearchResultMergeScratch* scratch = nullptr);

/**
 * @brief Secondary scan of the Recycle Bin metadata snapshot (Approach B).
 *
 * The primary SoA scan matches physical `$R` names; the snapshot indexes the
 * same items by their ORIGINAL name/path, which is what the user searched for.
 * This scan replays the primary pass's filters against the snapshot and appends
 * the fileIds of matching rows to @p out_ids (appended, caller owns the buffer).
 *
 * The matchers are built from exactly the inputs the engine receives
 * (filename query on the snapshot's original_name, path query on original_path,
 * one shared case flag, and the same extension normalization as
 * SearchContext::PrepareExtensionSet). The item-type filter is replicated per
 * entry through @p item_type, because it is engine-side on the primary scan
 * (ParallelSearchEngine.h:415-419 and :615-618, ParallelSearchEngine.cpp:69-74)
 * and no later pass can remove a row the engine never emitted. Size and time are
 * NOT replicated here: they are evaluated after the merge, on merged rows
 * (MatchesTimeFilter / MatchesSizeFilter in SearchResultUtils.cpp), so they apply
 * to these rows like any other.
 *
 * Pure and imgui-free: takes the snapshot by reference and touches nothing else.
 *
 * @param snapshot Published snapshot (immutable for the duration of the call)
 * @param filename_search Trimmed filename query, as passed to the engine
 * @param path_search Trimmed path query, as passed to the engine
 * @param case_sensitive Case flag the engine used (quick-filter forcing included)
 * @param extensions Parsed extension list, as passed to the engine
 * @param out_ids Destination fileIds (appended)
 * @param item_type Item-type filter the primary scan ran under; entries that
 *        disagree with it are skipped (default All reproduces the pre-filter
 *        behaviour and keeps existing call sites unchanged)
 */
void ScanRecycleSnapshot(const recycle_bin::RecycleMetadataSnapshot& snapshot,
                         std::string_view filename_search,
                         std::string_view path_search,
                         bool case_sensitive,
                         const std::vector<std::string>& extensions,
                         std::vector<uint64_t>& out_ids,
                         ItemTypeFilter item_type = ItemTypeFilter::All);

/**
 * @brief Lightweight helper: ensure only modification time is loaded
 *
 * This is a performance optimization for time filtering. It only loads the
 * modification time (not size) to avoid unnecessary I/O when filtering by time.
 *
 * @param result SearchResult to populate (lastModificationTime may be modified)
 * @param file_index File index for loading modification time
 */
void EnsureModTimeLoaded(const SearchResult &result, const FileIndex &file_index);

/**
 * @brief Whether filter-cache rebuild should be deferred for one frame
 *
 * Returns true only when an active time/size filter would need lazy attribute
 * loads (unloaded mod-times or file sizes). Used to avoid an unfiltered
 * display flash when filters are inactive or attributes are already cached.
 *
 * @param state GUI state (filters + result_pool_ read-only)
 * @return true if CommitNewSearchResults should set deferFilterCacheRebuild
 */
[[nodiscard]] bool ShouldDeferFilterCacheRebuild(const GuiState& state);

/**
 * @brief Ensure the cached time-filtered results in GuiState are up to date
 *
 * This function maintains a cache of time-filtered results to avoid
 * re-filtering on every frame. The cache is rebuilt only when:
 * - Search results changed (resultsUpdated == true), or
 * - The time filter value changed, or
 * - Cache was never built (filter_caches.time.valid == false)
 *
 * If no time filter is active (TimeFilter::None), filter_caches.time.results is cleared
 * and filter_caches.time.count is set to the full results count.
 *
 * @param state GUI state (filter_caches.time.results and cache flags are modified)
 * @param file_index File index for loading modification times during filtering
 * @param thread_pool Optional thread pool for background loading of cloud files (nullptr if not available)
 */
void UpdateTimeFilterCacheIfNeeded(GuiState &state, const FileIndex &file_index, ThreadPool *thread_pool = nullptr);

/**
 * @brief Update the cached total size of displayed results if needed
 *
 * Computes the sum of file sizes for the currently displayed results and caches it
 * in state.filter_caches.total_size.bytes. Skipped while state.search_pipeline.results_complete is false (search
 * still running). Uses cached value when filter_caches.total_size.valid is true.
 *
 * @param state GUI state (filter_caches.total_size.bytes, filter_caches.total_size.valid modified)
 * @param file_index File index for loading file sizes
 */
void UpdateDisplayedTotalSizeIfNeeded(GuiState& state, const FileIndex& file_index);

/**
 * @brief Ensure the cached size-filtered results in GuiState are up to date
 *
 * This function maintains a cache of size-filtered results to avoid
 * re-filtering on every frame. The cache is rebuilt only when:
 * - Search results changed (resultsUpdated == true), or
 * - The size filter value changed, or
 * - Cache was never built (filter_caches.size.valid == false)
 *
 * If no size filter is active (SizeFilter::None), filter_caches.size.results is cleared
 * and filter_caches.size.count is set to the full results count.
 *
 * Note: This function works on the time-filtered results if a time filter is active,
 * otherwise it works on the raw search results. This creates a filter chain:
 * searchResults -> timeFilteredResults -> filter_caches.size.results
 *
 * @param state GUI state (filter_caches.size.results and cache flags are modified)
 * @param file_index File index for loading file sizes during filtering
 */
void UpdateSizeFilterCacheIfNeeded(GuiState &state, const FileIndex &file_index);

/**
 * @brief Deterministic tie-break for equal primary sort keys.
 *
 * Parallel search can deliver identical primary keys (e.g. same extension) in
 * different relative orders. Path then fileId keeps std::sort order stable
 * across runs so positional equality (double-buffer skip) stays meaningful.
 *
 * @return negative if a < b, positive if a > b, zero if equal
 */
inline int ComparePathThenFileId(const SearchResult& a, const SearchResult& b) {
  if (const int path_cmp = a.fullPath.compare(b.fullPath); path_cmp != 0) {
    return path_cmp;
  }
  if (a.fileId < b.fileId) {
    return -1;
  }
  if (a.fileId > b.fileId) {
    return 1;
  }
  return 0;
}

inline int CompareUint64ThenPathFileId(uint64_t a_primary, uint64_t b_primary,
                                       const SearchResult& a, const SearchResult& b) {
  if (a_primary < b_primary) {
    return -1;
  }
  if (a_primary > b_primary) {
    return 1;
  }
  return ComparePathThenFileId(a, b);
}

inline int CompareByFilenameColumn(const SearchResult& a, const SearchResult& b,
                                   bool show_hierarchy) {
  if (show_hierarchy) {
    // Hierarchical view: full path groups items by folder structure.
    return ComparePathThenFileId(a, b);
  }
  // Flat view: same string shown in the Name column (filename + extension).
  if (const int name_cmp = a.fullPath.substr(a.filename_offset)
                               .compare(b.fullPath.substr(b.filename_offset));
      name_cmp != 0) {
    return name_cmp;
  }
  return ComparePathThenFileId(a, b);
}

inline int CompareBySizeColumn(const SearchResult& a, const SearchResult& b) {
  // Map sentinels (not-loaded / failed) to 0 so UINT64_MAX does not sort as "largest".
  // Ascending: sentinels group with zero-byte files at the start.
  // Descending (via CreateSearchResultComparator): those same values appear at the end.
  const uint64_t a_size = (a.fileSize == kFileSizeNotLoaded || a.fileSize == kFileSizeFailed)
                              ? 0
                              : a.fileSize;
  const uint64_t b_size = (b.fileSize == kFileSizeNotLoaded || b.fileSize == kFileSizeFailed)
                              ? 0
                              : b.fileSize;
  return CompareUint64ThenPathFileId(a_size, b_size, a, b);
}

inline int CompareByLastModifiedColumn(const SearchResult& a, const SearchResult& b) {
  const LONG compare_result =
      CompareFileTime(&a.lastModificationTime, &b.lastModificationTime);
  if (compare_result < 0) {
    return -1;
  }
  if (compare_result > 0) {
    return 1;
  }
  return ComparePathThenFileId(a, b);
}

inline int CompareByExtensionColumn(const SearchResult& a, const SearchResult& b) {
  if (const int ext_cmp = a.GetExtension().compare(b.GetExtension()); ext_cmp != 0) {
    return ext_cmp;
  }
  return ComparePathThenFileId(a, b);
}

inline int CompareByFolderFilesColumn(const SearchResult& a, const SearchResult& b) {
  // Map not-loaded sentinel to 0 so UINT64_MAX does not sort as "largest".
  // Ascending: uncomputed folders group at the start; descending: at the end.
  const uint64_t a_count =
      (a.folderFileCount == kFolderFileCountNotLoaded) ? 0 : a.folderFileCount;
  const uint64_t b_count =
      (b.folderFileCount == kFolderFileCountNotLoaded) ? 0 : b.folderFileCount;
  return CompareUint64ThenPathFileId(a_count, b_count, a, b);
}

/**
 * @brief Helper function to compare two SearchResults by a specific column.
 *
 * @param a First result
 * @param b Second result
 * @param column_index Column index to compare by (see ResultColumn namespace)
 * @return negative if a < b, positive if a > b, zero if equal
 */
inline int CompareByColumn(const SearchResult& a,
                           const SearchResult& b,
                           int column_index,
                           bool show_hierarchy = false) {
  switch (column_index) {
  case ResultColumn::Filename:
    return CompareByFilenameColumn(a, b, show_hierarchy);
  case ResultColumn::Size:
    return CompareBySizeColumn(a, b);
  case ResultColumn::LastModified:
    return CompareByLastModifiedColumn(a, b);
  case ResultColumn::FullPath:
    return ComparePathThenFileId(a, b);
  case ResultColumn::Extension:
    return CompareByExtensionColumn(a, b);
  case ResultColumn::FolderFiles:
    return CompareByFolderFilesColumn(a, b);
  default:
    // Unknown column index - should not happen, but handle gracefully
    return 0;
  }
}

/**
 * @brief Helper to create a sorting comparator lambda for SearchResult
 *
 * PERFORMANCE: Returns lambda directly (not std::function) to avoid type erasure
 * overhead. This allows the compiler to fully inline the comparator, which is
 * critical since it's called O(n log n) times during sorting.
 *
 * Note: Must be inline in header to allow return type deduction across TUs.
 *
 * @param column_index Column index to sort by (Mark to Extension)
 * @param direction Sort direction (Ascending or Descending)
 * @return Comparator function for std::sort
 */
inline auto CreateSearchResultComparator(int column_index, ImGuiSortDirection direction,
                                          bool show_hierarchy = false) {
  return [column_index, direction, show_hierarchy](const SearchResult &a, const SearchResult &b) {
    const int compare = CompareByColumn(a, b, column_index, show_hierarchy);
    // Apply sort direction
    return (direction == ImGuiSortDirection_Ascending) ? (compare < 0)
                                                       : (compare > 0);
  };
}

/**
 * @brief Extract sorting logic to remove duplication (used by UIRenderer)
 *
 * Sorts search results by the specified column and direction. For Size and
 * Last Modified columns, this function pre-fetches all necessary data using
 * the thread pool to avoid blocking the UI thread during sorting.
 *
 * Supported columns (see ResultColumn namespace in SearchTypes.h):
 * - Mark (0, sorting disabled in UI)
 * - Filename (1)
 * - Size (2, pre-fetches file sizes)
 * - Last Modified (3, pre-fetches modification times)
 * - Full Path (4)
 * - Extension (5)
 * - FolderFiles (6, folder file count — no pre-fetch needed, computed by FolderSizeAggregator)
 *
 * @param results Search results to sort (modified in place)
 * @param column_index Column index to sort by (Filename to Extension)
 * @param direction Sort direction (Ascending or Descending)
 * @param file_index File index for pre-fetching metadata (columns 2 and 3)
 * @param thread_pool Thread pool for parallel pre-fetching of metadata
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - function, not global variable; PascalCase public API
void SortSearchResults(std::vector<SearchResult> &results,
                       int column_index, ImGuiSortDirection direction,
                       const FileIndex &file_index, ThreadPool &thread_pool,
                       bool show_hierarchy = false,
                       std::vector<uint32_t>* order_scratch = nullptr);

/**
 * @brief Fast comparator-based sort for SearchResult collections.
 *
 * Fast path: skips the sort if rows already satisfy the comparator (std::is_sorted).
 * When sorting is needed, performs an index sort on 4-byte uint32_t indices followed
 * by an in-place cycle-chase permutation (ApplyRowPermutation), moving each whole
 * row (168 bytes on libc++/libstdc++, 200 on MSVC) at most once. Falls back to
 * direct SortLarge only if size exceeds uint32_t.
 *
 * No attribute prefetch: unlike SortSearchResults, this function never loads
 * sizes/times. The caller must ensure the sort keys are loaded first — sorting
 * Size/LastModified on unloaded rows silently orders sentinel values
 * (kFileSizeNotLoaded/kFileTimeNotLoaded) as if they were real data.
 *
 * @param results Search results to sort (modified in place)
 * @param column_index Column index to sort by (Filename to Extension)
 * @param direction Sort direction (Ascending or Descending)
 * @param show_hierarchy Whether to consider hierarchy indentation
 * @param order_scratch Optional reusable scratch buffer for indices to eliminate per-sort allocations
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - function, not global variable; PascalCase public API
void SortSearchResultsFast(std::vector<SearchResult>& results,
                           int column_index, ImGuiSortDirection direction,
                           bool show_hierarchy = false,
                           std::vector<uint32_t>* order_scratch = nullptr);

/**
 * @brief Cumulative is_sorted fast-path diagnostics for SortSearchResults.
 *
 * UI-thread confined (all production callers run on the UI thread; tests are
 * single-threaded). Default sequential consistency; exact cross-thread counts
 * are not needed, only hit-rate trends. Use the skip rate to judge whether the fast path earns
 * its O(N) check tax in real flows; sort-phase timing itself comes from
 * search_benchmark (fresh sorts always miss by construction).
 */
struct SortSkipStats {
  uint64_t checks = 0;        // SortSearchResults calls that ran the is_sorted check
  uint64_t skipped = 0;       // ...of which skipped the sort entirely
  uint64_t rows_checked = 0;  // Total rows scanned by the check
  uint64_t rows_skipped = 0;  // ...of which in skipped sorts
};
[[nodiscard]] SortSkipStats GetSortSkipStats();
void ResetSortSkipStats();  // Test hook: zero the counters between cases

/**
 * @brief Reorder rows in place according to a sorted index array.
 *
 * Cycle-chase permutation with a single temp row: O(N) moves of whole rows
 * regardless of sort complexity. `order[i]` must hold the source index of the
 * row that belongs at position i (as produced by sorting indices with the row
 * comparator). Entries are marked fixed as they are written, so every row
 * moves exactly once. Used by SortSearchResults to keep the O(N log N) phase
 * on 4-byte indices instead of 168-byte rows.
 *
 * @param rows Search results to reorder (modified in place)
 * @param order Source-index permutation (modified: entries marked fixed)
 */
void ApplyRowPermutation(std::vector<SearchResult> &rows,
                         std::vector<uint32_t> &order);

/**
 * @brief Wait until all cancelled tasks have fully drained
 *
 * Blocks on the sort-task latch until every batch task has arrived (each
 * task arrives on completion, including on cancellation), so a zero count
 * proves no task can still write into the results buffer. Woken by the
 * final arrival — no polling. Single source of truth for the
 * sort-cancellation drain; all result-replacement and sort-restart paths
 * must go through this instead of open-coding the loop.
 *
 * @param counter Latch for the cancelled batch (null-safe)
 */
void WaitForCancelledTasksFully(const std::shared_ptr<SortTaskLatch>& counter);

/**
 * @brief Bounded variant of WaitForCancelledTasksFully.
 *
 * Waits on the latch until it drains or the timeout expires. Returns true
 * when fully drained. Use for best-effort yields where the caller keeps the
 * buffers alive and a later WaitForCancelledTasksFully provides the hard
 * barrier (see StartSortAttributeLoading); never Reset/reuse staging
 * buffers on a false return.
 *
 * @param counter Latch for the batch (null-safe, treated as drained)
 * @param timeout Maximum time to wait
 * @return True if the latch drained before the deadline
 */
[[nodiscard]] bool WaitForCancelledTasksWithTimeout(
    const std::shared_ptr<SortTaskLatch>& counter, std::chrono::milliseconds timeout);

/**
 * @brief Start async attribute loading for sorting (non-blocking)
 *
 * Starts loading file attributes asynchronously for sorting by Size or Last Modified.
 * The futures are stored in GuiState and should be checked each frame until complete.
 *
 * @param state GUI state (counter stored in attributeLoadingCounter)
 * @param results Search results to load attributes for
 * @param column_index Column index to sort by (ResultColumn::Size or ResultColumn::LastModified)
 * @param file_index File index for loading metadata
 * @param thread_pool Thread pool for parallel pre-fetching
 */
void StartSortAttributeLoading(GuiState &state,
                                std::vector<SearchResult> &results,
                                int column_index,
                                const FileIndex &file_index,
                                ThreadPool &thread_pool);

/**
 * @brief Check if async attribute loading is complete and perform sort if ready
 *
 * Checks if attribute loading futures are complete. If complete, formats display
 * strings, performs the sort, and clears the futures. Returns true if sort was
 * completed, false if still loading.
 *
 * @param state GUI state (counter checked and reset from attributeLoadingCounter)
 * @param results Search results to sort (modified if loading complete)
 * @param column_index Column index to sort by (Mark to Extension)
 * @param direction Sort direction (Ascending or Descending)
 * @return True if sort was completed, false if still loading
 */
bool CheckSortAttributeLoadingAndSort(GuiState &state,
                                       std::vector<SearchResult> &results,
                                       int column_index,
                                       ImGuiSortDirection direction,
                                       SortGeneration sort_generation,
                                       bool show_hierarchy = false);


/**
 * @brief Sum file sizes in a result collection, skipping directories and unloaded sizes.
 *
 * Shared by SearchResultUtils and SearchResultsService to avoid duplicating the
 * accumulation loop.  Returns 0 for empty or all-directory collections.
 */
[[nodiscard]] uint64_t ComputeTotalFileBytes(const std::vector<SearchResult>& results);


