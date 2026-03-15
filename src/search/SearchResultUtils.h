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
 * @param allow_sync_load If false, skip synchronous disk I/O (use during streaming to keep UI responsive; size/date show "..." until search completes)
 */
void EnsureDisplayStringsPopulated(const SearchResult& result,
                                   const FileIndex& file_index,
                                   bool allow_sync_load = true);

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
 * @brief Ensure the cached time-filtered results in GuiState are up to date
 *
 * This function maintains a cache of time-filtered results to avoid
 * re-filtering on every frame. The cache is rebuilt only when:
 * - Search results changed (resultsUpdated == true), or
 * - The time filter value changed, or
 * - Cache was never built (timeFilterCacheValid == false)
 *
 * If no time filter is active (TimeFilter::None), filteredResults is cleared
 * and filteredCount is set to the full results count.
 *
 * @param state GUI state (filteredResults and cache flags are modified)
 * @param file_index File index for loading modification times during filtering
 * @param thread_pool Optional thread pool for background loading of cloud files (nullptr if not available)
 */
void UpdateTimeFilterCacheIfNeeded(GuiState &state, const FileIndex &file_index, ThreadPool *thread_pool = nullptr);

/**
 * @brief Update the cached total size of displayed results if needed
 *
 * Computes the sum of file sizes for the currently displayed results and caches it
 * in state.displayedTotalSizeBytes. Skipped during streaming. Uses cached value when
 * displayedTotalSizeValid is true.
 *
 * @param state GUI state (displayedTotalSizeBytes, displayedTotalSizeValid modified)
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
 * - Cache was never built (sizeFilterCacheValid == false)
 *
 * If no size filter is active (SizeFilter::None), sizeFilteredResults is cleared
 * and sizeFilteredCount is set to the full results count.
 *
 * Note: This function works on the time-filtered results if a time filter is active,
 * otherwise it works on the raw search results. This creates a filter chain:
 * searchResults -> timeFilteredResults -> sizeFilteredResults
 *
 * @param state GUI state (sizeFilteredResults and cache flags are modified)
 * @param file_index File index for loading file sizes during filtering
 */
void UpdateSizeFilterCacheIfNeeded(GuiState &state, const FileIndex &file_index);

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
                           int column_index) {
  switch (column_index) {
  case ResultColumn::Filename:
    // Compare by the same string shown in the Name column (filename + extension).
    // This uses the filename slice of fullPath so sorting matches the UI display.
    return a.fullPath.substr(a.filename_offset).compare(
      b.fullPath.substr(b.filename_offset));
  case ResultColumn::Size: { // tie-break by path so equal sizes order deterministically
    // Treat sentinels (not-loaded / failed) as smaller than any real size so they
    // sort last in both ascending and descending order via CreateSearchResultComparator.
    // Without this, kFileSizeNotLoaded == UINT64_MAX would appear at the top of a
    // descending sort (e.g. uncomputed folder sizes above large files).
    const uint64_t a_size = (a.fileSize == kFileSizeNotLoaded || a.fileSize == kFileSizeFailed)
                                ? 0
                                : a.fileSize;
    const uint64_t b_size = (b.fileSize == kFileSizeNotLoaded || b.fileSize == kFileSizeFailed)
                                ? 0
                                : b.fileSize;
    if (a_size < b_size) {
      return -1;
    }
    if (a_size > b_size) {
      return 1;
    }
    return a.fullPath.compare(b.fullPath);
  }
  case ResultColumn::LastModified: { // tie-break by path so equal times order deterministically
    const LONG compare_result =
        CompareFileTime(&a.lastModificationTime, &b.lastModificationTime);
    if (compare_result < 0) {
      return -1;
    }
    if (compare_result > 0) {
      return 1;
    }
    return a.fullPath.compare(b.fullPath);
  }
  case ResultColumn::FullPath:
    return a.fullPath.compare(b.fullPath);
  case ResultColumn::Extension:
    return a.GetExtension().compare(b.GetExtension());
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
inline auto CreateSearchResultComparator(int column_index, ImGuiSortDirection direction) {
  return [column_index, direction](const SearchResult &a, const SearchResult &b) {
    const int compare = CompareByColumn(a, b, column_index);
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
 * - FolderFileCount (6, uses aggregated folder stats)
 * - FolderTotalSize (7, uses aggregated folder stats)
 *
 * @param results Search results to sort (modified in place)
 * @param column_index Column index to sort by (Mark to FolderTotalSize)
 * @param direction Sort direction (Ascending or Descending)
 * @param file_index File index for pre-fetching metadata (columns 2 and 3)
 * @param thread_pool Thread pool for parallel pre-fetching of metadata
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - function, not global variable; PascalCase public API
void SortSearchResults(std::vector<SearchResult> &results,
                       int column_index, ImGuiSortDirection direction,
                       FileIndex &file_index, ThreadPool &thread_pool);

/**
 * @brief Start async attribute loading for sorting (non-blocking)
 *
 * Starts loading file attributes asynchronously for sorting by Size or Last Modified.
 * The futures are stored in GuiState and should be checked each frame until complete.
 *
 * @param state GUI state (futures stored in attributeLoadingFutures)
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
 * @param state GUI state (futures checked and cleared from attributeLoadingFutures)
 * @param results Search results to sort (modified if loading complete)
 * @param column_index Column index to sort by (Mark to Extension)
 * @param direction Sort direction (Ascending or Descending)
 * @return True if sort was completed, false if still loading
 */
bool CheckSortAttributeLoadingAndSort(GuiState &state,
                                       std::vector<SearchResult> &results,
                                       int column_index,
                                       ImGuiSortDirection direction,
                                       SortGeneration sort_generation);

/**
 * @brief Clean up attribute loading futures and reset loading state
 *
 * This helper function eliminates duplicate future cleanup code across
 * SearchResultUtils and SearchController. It properly cleans up futures
 * to prevent memory leaks and resets the loading state.
 *
 * PERFORMANCE: This is cleanup code, not in hot path. Called only when
 * updating search results or cancelling attribute loading.
 *
 * @param state GUI state containing attributeLoadingFutures to clean up
 * @param blocking If true, waits for futures that aren't ready. If false,
 *                 only cleans up futures that are already ready.
 */
void CleanupAttributeLoadingFutures(GuiState &state, bool blocking = true);


