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

#include <functional>
#include <vector>

#include "gui/GuiState.h"
#include "index/FileIndex.h"
#include "search/SearchTypes.h"
#include "utils/FileAttributeConstants.h"
#include "utils/FileTimeTypes.h"
#include "utils/StringUtils.h"

// Forward declarations
struct SearchResult;
class ThreadPool;
class FolderSizeAggregator;

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
 */
std::vector<SearchResult> MergeAndConvertToSearchResults(
    std::vector<char>& pool,
    const std::vector<SearchResultBatch>& batches,
    const FileIndex& file_index);

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
                       bool show_hierarchy = false);

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


