/**
 * @file SearchResultsService.cpp
 * @brief Implementation of SearchResultsService for managing search results
 */

#include <cassert>
#include <vector>

#include "filters/SizeFilter.h"
#include "filters/TimeFilter.h"
#include "gui/GuiState.h"
#include "imgui.h"
#include "index/FileIndex.h"
#include "search/SearchResultUtils.h"
#include "search/SearchResultsService.h"
#include "utils/FileAttributeConstants.h"
#include "utils/Logger.h"
#include "utils/ThreadPool.h"

namespace search {

namespace {

// Updates displayed total size after Size/LastModified sort (all attributes loaded).
// Extracted to reduce nesting in CheckAndCompleteAsyncSort (S134).
void UpdateDisplayedTotalSizeAfterSort(GuiState& state) {
  if (state.lastSortColumn != ResultColumn::Size &&
      state.lastSortColumn != ResultColumn::LastModified) {
    return;
  }
  uint64_t total_bytes = 0;
  for (const auto& r : state.searchResults) {
    if (!r.isDirectory && r.fileSize != kFileSizeNotLoaded &&
        r.fileSize != kFileSizeFailed) {
      total_bytes += r.fileSize;
    }
  }
  state.displayedTotalSizeBytes = total_bytes;
  if (state.timeFilter == TimeFilter::None) {
    state.displayedTotalSizeValid = true;
  }
}

}  // namespace

bool SearchResultsService::HandleTableSorting(GuiState& state,
                                               FileIndex& file_index,
                                               ThreadPool& thread_pool) {
  ImGuiTableSortSpecs* const sort_specs = ImGui::TableGetSortSpecs();
  if (sort_specs == nullptr) {
    return false;
  }

  if (sort_specs->SpecsDirty) {
    const ScopedTimer timer("Search - Sorting");
    if (sort_specs->SpecsCount > 0) {
      const ImGuiTableColumnSortSpecs& spec = sort_specs->Specs[0];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) NOSONAR(cpp:S3786) - ImGui API
      state.lastSortColumn = spec.ColumnIndex;
      state.lastSortDirection = spec.SortDirection;

      // For Size or Last Modified columns, use async loading
      if (spec.ColumnIndex == ResultColumn::Size || spec.ColumnIndex == ResultColumn::LastModified) {
        StartSortAttributeLoading(state, state.searchResults, spec.ColumnIndex, file_index, thread_pool);
      } else {
        // For other columns, sort immediately (no attribute loading needed)
        SortSearchResults(state.searchResults, spec.ColumnIndex, spec.SortDirection, file_index, thread_pool);
        // Invalidate and rebuild filter caches after sorting
        state.timeFilterCacheValid = false;
        state.sizeFilterCacheValid = false;
        state.InvalidateDisplayedTotalSize();
        UpdateTimeFilterCacheIfNeeded(state, file_index, &thread_pool);
        UpdateSizeFilterCacheIfNeeded(state, file_index);
      }
      sort_specs->SpecsDirty = false;
      return true;
    }
    sort_specs->SpecsDirty = false;
  } else if (state.lastSortColumn != -1 && state.resultsUpdated) {
    const ScopedTimer timer("Search - Re-sorting");

    // For Size or Last Modified columns, use async loading
    if (state.lastSortColumn == ResultColumn::Size || state.lastSortColumn == ResultColumn::LastModified) {
      StartSortAttributeLoading(state, state.searchResults, state.lastSortColumn, file_index, thread_pool);
    } else {
      // For other columns, sort immediately (no attribute loading needed)
      SortSearchResults(state.searchResults, state.lastSortColumn, state.lastSortDirection, file_index, thread_pool);
      // Invalidate and rebuild filter caches after sorting
      state.timeFilterCacheValid = false;
      state.sizeFilterCacheValid = false;
      state.InvalidateDisplayedTotalSize();
      UpdateTimeFilterCacheIfNeeded(state, file_index, &thread_pool);
      UpdateSizeFilterCacheIfNeeded(state, file_index);
    }

    // Consume the one-shot "results updated" flag
    state.resultsUpdated = false;
    return true;
  }
  
  return false;
}

bool SearchResultsService::CheckAndCompleteAsyncSort(GuiState& state,
                                                     const FileIndex& file_index,
                                                     ThreadPool& thread_pool) {
  // Check for async sorting completion
  // Also check sortDataReady flag for immediate sorting when all data is already loaded
  // NOLINTNEXTLINE(readability-simplify-boolean-expr) - Combined condition intentionally groups \"work pending\" and \"completed\" checks to mirror async sort state machine
  if ((!state.attributeLoadingFutures.empty() || state.sortDataReady) &&
      CheckSortAttributeLoadingAndSort(
          state, state.searchResults, state.lastSortColumn,
          state.lastSortDirection, state.sort_generation_)) {
    // Sort completed - invalidate and rebuild filter caches
    // Caches must be invalidated because searchResults order changed
    state.timeFilterCacheValid = false;
    state.sizeFilterCacheValid = false;
    state.InvalidateDisplayedTotalSize();

    // Piggyback: if we just sorted by Size or Last Modified, all attributes are loaded.
    // Sum immediately to avoid unnecessary progressive computation frames.
    UpdateDisplayedTotalSizeAfterSort(state);

    UpdateTimeFilterCacheIfNeeded(state, file_index, &thread_pool);
    UpdateSizeFilterCacheIfNeeded(state, file_index);
    return true;
  }
  return false;
}

void SearchResultsService::UpdateFilterCaches(GuiState& state,
                                              const FileIndex& file_index,
                                              ThreadPool& thread_pool) {
  UpdateTimeFilterCacheIfNeeded(state, file_index, &thread_pool);
  UpdateSizeFilterCacheIfNeeded(state, file_index);
  UpdateDisplayedTotalSizeIfNeeded(state, file_index);
  // Only assert count/size match when the filter is active. When filter is None,
  // UpdateFilterCacheForNoFilter sets cache_valid=true but leaves the vector empty
  // and only sets the count (display uses searchResults); count != size by design.
  if (state.timeFilterCacheValid && state.timeFilter != TimeFilter::None) {
    assert(state.filteredCount == state.filteredResults.size());
  }
  if (state.sizeFilterCacheValid && state.sizeFilter != SizeFilter::None) {
    assert(state.sizeFilteredCount == state.sizeFilteredResults.size());
  }
}

const std::vector<SearchResult>* SearchResultsService::GetDisplayResults(const GuiState& state) {
  // If we are showing partial results, return them directly
  // Filtering and sorting are minimal for partial results to keep UI responsive
  if (!state.resultsComplete && state.showingPartialResults) {
    // NOLINTNEXTLINE(readability-simplify-boolean-expr) - Assert intentionally mirrors if-condition to verify partial-results invariant
    assert(!state.resultsComplete && state.showingPartialResults);
    return &state.partialResults;
  }

  // Determine which results to display based on active filters
  // Priority: sizeFiltered (if size filter active and valid) > timeFiltered (if time filter active and valid) > raw searchResults
  // CRITICAL: Only return filter caches when valid. After searchResults is replaced we invalidate caches;
  // returning them when invalid would show stale data (wrong results until rebuild).
  // PERFORMANCE: If filter cache rebuild is deferred, show unfiltered results to avoid empty display
  if (!state.deferFilterCacheRebuild) {
    if (state.sizeFilter != SizeFilter::None && state.sizeFilterCacheValid) {
      return &state.sizeFilteredResults;
    }
    if (state.timeFilter != TimeFilter::None && state.timeFilterCacheValid) {
      return &state.filteredResults;
    }
  }
  // deferFilterCacheRebuild, or cache invalid (e.g. after replace), or no filter: show searchResults
  return &state.searchResults;
}

}  // namespace search
