/**
 * @file SearchResultsService.cpp
 * @brief Implementation of SearchResultsService for managing search results
 */

#include <cassert>
#include <vector>

#include "ctrack.hpp"
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
  if (state.async_sort_.last_sort_column != ResultColumn::Size &&
      state.async_sort_.last_sort_column != ResultColumn::LastModified) {
    return;
  }
  state.filter_caches.total_size.bytes = ComputeTotalFileBytes(state.result_pool_->Results());
  if (state.searchCriteria.time_filter == TimeFilter::None) {
    state.filter_caches.total_size.valid = true;
  }
}

}  // namespace

bool SearchResultsService::HandleTableSorting(GuiState& state,
                                               const FileIndex& file_index,
                                               ThreadPool& thread_pool,
                                               bool show_hierarchy) {
  ImGuiTableSortSpecs* const sort_specs = ImGui::TableGetSortSpecs();
  if (sort_specs == nullptr) {
    return false;
  }

  if (sort_specs->SpecsDirty) {
    const ScopedTimer timer("Search - Sorting");
    // User changed sort column/direction; any PollResults pre-sort no longer applies.
    state.search_pipeline.results_presorted_on_commit = false;
    if (sort_specs->SpecsCount > 0) {
      const ImGuiTableColumnSortSpecs& spec = sort_specs->Specs[0];
      state.async_sort_.last_sort_column = spec.ColumnIndex;
      state.async_sort_.last_sort_direction = spec.SortDirection;

      // For Size or Last Modified columns, use async loading
      if (spec.ColumnIndex == ResultColumn::Size || spec.ColumnIndex == ResultColumn::LastModified) {
        StartSortAttributeLoading(state, state.result_pool_->Results(), spec.ColumnIndex, file_index, thread_pool);
        // Selection remap happens in CheckAndCompleteAsyncSort when the sort finishes.
      } else {
        // Snapshot old display order as path views (no deep copy) for entity-based
        // selection remap after the in-place sort. Skipped when nothing is selected.
        const std::vector<std::string_view> old_paths =
            state.selection.GetSelectedRows().empty()
                ? std::vector<std::string_view>{}
                : SnapshotDisplayPaths(*GetDisplayResults(state));
        // For other columns, sort immediately (no attribute loading needed)
        {
          CTRACK_DEV_NAME("TableSorting::SyncSort");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
          SortSearchResults(state.result_pool_->Results(), spec.ColumnIndex, spec.SortDirection, file_index, thread_pool, show_hierarchy);
        }
        // Invalidate and rebuild filter caches after sorting
        state.InvalidateFilterCacheFlags();
        {
          CTRACK_DEV_NAME("TableSorting::RebuildFilterCaches");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
          UpdateTimeFilterCacheIfNeeded(state, file_index, &thread_pool);
          UpdateSizeFilterCacheIfNeeded(state, file_index);
        }
        // Remap selection so the same logical items remain selected after reorder.
        {
          CTRACK_DEV_NAME("TableSorting::RemapSelection");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
          state.selection.RemapSelectionAfterDisplayResultsChange(old_paths, *GetDisplayResults(state));
        }
      }
      sort_specs->SpecsDirty = false;
      return true;
    }
    sort_specs->SpecsDirty = false;
  } else if (state.async_sort_.last_sort_column != -1 && state.search_pipeline.results_updated) {
    const ScopedTimer timer("Search - Re-sorting");

    // For Size or Last Modified columns, use async loading
    if (state.async_sort_.last_sort_column == ResultColumn::Size || state.async_sort_.last_sort_column == ResultColumn::LastModified) {
      StartSortAttributeLoading(state, state.result_pool_->Results(), state.async_sort_.last_sort_column, file_index, thread_pool);
      // Selection remap happens in CheckAndCompleteAsyncSort when the sort finishes.
    } else if (state.search_pipeline.results_presorted_on_commit) {
      // PollResults already sorted the committed back buffer with the same sync column /
      // direction / hierarchy. Skip clone + re-sort + re-remap; only refresh filters.
      state.InvalidateFilterCacheFlags();
      UpdateTimeFilterCacheIfNeeded(state, file_index, &thread_pool);
      UpdateSizeFilterCacheIfNeeded(state, file_index);
    } else {
      // e.g. FolderFiles after aggregator filled directory stats — order must change.
      // Snapshot old order as path views (no deep copy); skipped when nothing selected.
      const std::vector<std::string_view> old_paths =
          state.selection.GetSelectedRows().empty()
              ? std::vector<std::string_view>{}
              : SnapshotDisplayPaths(*GetDisplayResults(state));
      {
        CTRACK_DEV_NAME("TableSorting::SyncSort");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
        SortSearchResults(state.result_pool_->Results(), state.async_sort_.last_sort_column, state.async_sort_.last_sort_direction, file_index, thread_pool, show_hierarchy);
      }
      state.InvalidateFilterCacheFlags();
      {
        CTRACK_DEV_NAME("TableSorting::RebuildFilterCaches");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
        UpdateTimeFilterCacheIfNeeded(state, file_index, &thread_pool);
        UpdateSizeFilterCacheIfNeeded(state, file_index);
      }
      {
        CTRACK_DEV_NAME("TableSorting::RemapSelection");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
        state.selection.RemapSelectionAfterDisplayResultsChange(old_paths, *GetDisplayResults(state));
      }
    }

    // Consume one-shot flags from commit / aggregator sort triggers.
    state.search_pipeline.results_presorted_on_commit = false;
    state.search_pipeline.results_updated = false;
    return true;
  }

  return false;
}

bool SearchResultsService::CheckAndCompleteAsyncSort(GuiState& state,
                                                     const FileIndex& file_index,
                                                     ThreadPool& thread_pool,
                                                     bool show_hierarchy) {
  // Check for async sorting completion (either tasks running or pre-loaded data ready).
  if (!state.async_sort_.HasPendingSort()) {
    return false;
  }
  // Snapshot old display order as path views (no deep copy) before sort mutates
  // searchResults order. Skipped when nothing is selected.
  const std::vector<std::string_view> old_paths =
      state.selection.GetSelectedRows().empty()
          ? std::vector<std::string_view>{}
          : SnapshotDisplayPaths(*GetDisplayResults(state));
  {
    CTRACK_DEV_NAME("SortCompletion::Sort");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
    if (!CheckSortAttributeLoadingAndSort(state, state.result_pool_->Results(), state.async_sort_.last_sort_column,
                                          state.async_sort_.last_sort_direction, state.async_sort_.generation_,
                                          show_hierarchy)) {
      return false;  // Still loading
    }
  }
  // Sort completed - invalidate and rebuild filter caches
  // Caches must be invalidated because searchResults order changed
  state.InvalidateFilterCacheFlags();

  // Piggyback: if we just sorted by Size or Last Modified, all attributes are loaded.
  // Sum immediately to avoid unnecessary progressive computation frames.
  UpdateDisplayedTotalSizeAfterSort(state);

  {
    CTRACK_DEV_NAME("SortCompletion::RebuildFilterCaches");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
    UpdateTimeFilterCacheIfNeeded(state, file_index, &thread_pool);
    UpdateSizeFilterCacheIfNeeded(state, file_index);
  }
  // Remap selection so the same logical items remain selected after reorder.
  {
    CTRACK_DEV_NAME("SortCompletion::RemapSelection");  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
    state.selection.RemapSelectionAfterDisplayResultsChange(old_paths, *GetDisplayResults(state));
  }
  return true;
}

void SearchResultsService::UpdateFilterCaches(GuiState& state,
                                              const FileIndex& file_index,
                                              ThreadPool& thread_pool) {
  // Detect if a filter cache rebuild is likely (filter setting changed or cache invalid).
  // Only snapshot old display order when needed to avoid per-frame copy overhead.
  // Snapshot is skipped unless a rebuild is likely AND something is selected (the
  // remap below is a no-op otherwise).
  const bool might_rebuild =
      !state.filter_caches.time.valid || state.filter_caches.time.cached_filter != state.searchCriteria.time_filter ||
      !state.filter_caches.size.valid || state.filter_caches.size.cached_filter != state.searchCriteria.size_filter;
  const std::vector<std::string_view> old_paths =
      (might_rebuild && !state.selection.GetSelectedRows().empty())
          ? SnapshotDisplayPaths(*GetDisplayResults(state))
          : std::vector<std::string_view>{};

  UpdateTimeFilterCacheIfNeeded(state, file_index, &thread_pool);
  UpdateSizeFilterCacheIfNeeded(state, file_index);
  UpdateDisplayedTotalSizeIfNeeded(state, file_index);

  // Remap selection so the same logical items remain selected after filter change.
  // RemapSelectionAfterDisplayResultsChange is a no-op when selectedRows is empty.
  if (might_rebuild && !state.selection.GetSelectedRows().empty()) {
    state.selection.RemapSelectionAfterDisplayResultsChange(old_paths, *GetDisplayResults(state));
  }
  // Only assert count/size match when the filter is active. When filter is None,
  // UpdateFilterCacheForNoFilter sets cache_valid=true but leaves the vector empty
  // and only sets the count (display uses searchResults); count != size by design.
  if (state.filter_caches.time.valid && state.searchCriteria.time_filter != TimeFilter::None) {
    assert(state.filter_caches.time.count == state.filter_caches.time.results.size());
  }
  if (state.filter_caches.size.valid && state.searchCriteria.size_filter != SizeFilter::None) {
    assert(state.filter_caches.size.count == state.filter_caches.size.results.size());
  }
}

}  // namespace search
