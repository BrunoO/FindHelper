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
  state.displayedTotalSizeBytes = ComputeTotalFileBytes(state.result_pool_->Results());
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
      const ImGuiTableColumnSortSpecs& spec = sort_specs->Specs[0];
      state.lastSortColumn = spec.ColumnIndex;
      state.lastSortDirection = spec.SortDirection;

      // For Size or Last Modified columns, use async loading
      if (spec.ColumnIndex == ResultColumn::Size || spec.ColumnIndex == ResultColumn::LastModified) {
        StartSortAttributeLoading(state, state.result_pool_->Results(), spec.ColumnIndex, file_index, thread_pool);
        // Selection remap happens in CheckAndCompleteAsyncSort when the sort finishes.
      } else {
        // Snapshot old display results before in-place sort for entity-based selection remap.
        const std::vector<SearchResult> old_display = *GetDisplayResults(state);
        // For other columns, sort immediately (no attribute loading needed)
        SortSearchResults(state.result_pool_->Results(), spec.ColumnIndex, spec.SortDirection, file_index, thread_pool);
        // Invalidate and rebuild filter caches after sorting
        state.InvalidateFilterCacheFlags();
        UpdateTimeFilterCacheIfNeeded(state, file_index, &thread_pool);
        UpdateSizeFilterCacheIfNeeded(state, file_index);
        // Remap selection so the same logical items remain selected after reorder.
        state.selection.RemapSelectionAfterDisplayResultsChange(old_display, *GetDisplayResults(state));
      }
      sort_specs->SpecsDirty = false;
      return true;
    }
    sort_specs->SpecsDirty = false;
  } else if (state.lastSortColumn != -1 && state.resultsUpdated) {
    const ScopedTimer timer("Search - Re-sorting");

    // For Size or Last Modified columns, use async loading
    if (state.lastSortColumn == ResultColumn::Size || state.lastSortColumn == ResultColumn::LastModified) {
      StartSortAttributeLoading(state, state.result_pool_->Results(), state.lastSortColumn, file_index, thread_pool);
      // Selection remap happens in CheckAndCompleteAsyncSort when the sort finishes.
    } else {
      // Snapshot old display results before in-place sort for entity-based selection remap.
      const std::vector<SearchResult> old_display = *GetDisplayResults(state);
      // For other columns, sort immediately (no attribute loading needed)
      SortSearchResults(state.result_pool_->Results(), state.lastSortColumn, state.lastSortDirection, file_index, thread_pool);
      // Invalidate and rebuild filter caches after sorting
      state.InvalidateFilterCacheFlags();
      UpdateTimeFilterCacheIfNeeded(state, file_index, &thread_pool);
      UpdateSizeFilterCacheIfNeeded(state, file_index);
      // Remap selection so the same logical items remain selected after reorder.
      state.selection.RemapSelectionAfterDisplayResultsChange(old_display, *GetDisplayResults(state));
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
  // Check for async sorting completion (either tasks running or pre-loaded data ready).
  if (!state.async_sort_.HasPendingSort()) {
    return false;
  }
  // Snapshot old display results before sort mutates searchResults order.
  const std::vector<SearchResult> old_display = *GetDisplayResults(state);
  if (!CheckSortAttributeLoadingAndSort(state, state.result_pool_->Results(), state.lastSortColumn,
                                        state.lastSortDirection, state.async_sort_.generation_)) {
    return false;  // Still loading
  }
  // Sort completed - invalidate and rebuild filter caches
  // Caches must be invalidated because searchResults order changed
  state.InvalidateFilterCacheFlags();

  // Piggyback: if we just sorted by Size or Last Modified, all attributes are loaded.
  // Sum immediately to avoid unnecessary progressive computation frames.
  UpdateDisplayedTotalSizeAfterSort(state);

  UpdateTimeFilterCacheIfNeeded(state, file_index, &thread_pool);
  UpdateSizeFilterCacheIfNeeded(state, file_index);
  // Remap selection so the same logical items remain selected after reorder.
  state.selection.RemapSelectionAfterDisplayResultsChange(old_display, *GetDisplayResults(state));
  return true;
}

void SearchResultsService::UpdateFilterCaches(GuiState& state,
                                              const FileIndex& file_index,
                                              ThreadPool& thread_pool) {
  // Detect if a filter cache rebuild is likely (filter setting changed or cache invalid).
  // Only snapshot old display results when needed to avoid per-frame copy overhead.
  const bool might_rebuild =
      !state.timeFilterCacheValid || state.cachedTimeFilter != state.timeFilter ||
      !state.sizeFilterCacheValid || state.cachedSizeFilter != state.sizeFilter;
  const std::vector<SearchResult> old_display =
      might_rebuild ? *GetDisplayResults(state) : std::vector<SearchResult>{};

  UpdateTimeFilterCacheIfNeeded(state, file_index, &thread_pool);
  UpdateSizeFilterCacheIfNeeded(state, file_index);
  UpdateDisplayedTotalSizeIfNeeded(state, file_index);

  // Remap selection so the same logical items remain selected after filter change.
  // RemapSelectionAfterDisplayResultsChange is a no-op when selectedRows is empty.
  if (might_rebuild && !state.selection.GetSelectedRows().empty()) {
    state.selection.RemapSelectionAfterDisplayResultsChange(old_display, *GetDisplayResults(state));
  }
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

}  // namespace search
