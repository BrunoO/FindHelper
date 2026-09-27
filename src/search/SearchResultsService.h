#pragma once

/**
 * @file SearchResultsService.h
 * @brief Service for managing search results (sorting, filtering, caching)
 *
 * This service handles all business logic related to search results,
 * separating it from UI rendering code. This follows the Single Responsibility
 * Principle by moving business logic out of UI components.
 */

#include <vector>

#include "gui/GuiState.h"
#include "index/FileIndex.h"
#include "utils/ThreadPool.h"

namespace search {
  /**
   * @brief Service for managing search results (sorting, filtering, caching)
   *
   * This service handles all business logic related to search results,
   * separating it from UI rendering code.
   */
  class SearchResultsService {
   public:
    /**
     * @brief Handle table sorting (handles both immediate and async sorting)
     * @param state GUI state (modified: searchResults sorted, filter caches invalidated)
     * @param file_index File index for attribute lookups
     * @param thread_pool Thread pool for async operations
     * @return true if sorting was triggered (immediate or async), false otherwise
     */
    static bool HandleTableSorting(GuiState& state,
                                   const FileIndex& file_index,
                                   ThreadPool& thread_pool,
                                   bool show_hierarchy = false);

    /**
     * @brief Check if async sorting is complete and perform sort if ready
     * @param state GUI state (modified if sorting completes)
     * @param file_index File index for attribute lookups (read-only)
     * @param thread_pool Thread pool for async operations
     * @return true if sort was completed, false if still loading
     */
    static bool CheckAndCompleteAsyncSort(GuiState& state,
                                          const FileIndex& file_index,
                                          ThreadPool& thread_pool,
                                          bool show_hierarchy = false);

    /**
     * @brief Update filter caches if needed
     * @param state GUI state (modified: filter caches updated)
     * @param file_index File index for attribute lookups (read-only)
     * @param thread_pool Thread pool for async operations
     */
    static void UpdateFilterCaches(GuiState& state,
                                   const FileIndex& file_index,
                                   ThreadPool& thread_pool);

    /**
     * @brief Get display results (applying active filters)
     * Returns filtered cache or searchResults. Inline so callers outside SearchResultsService.cpp
     * (e.g. SearchController.cpp) can use it without introducing an ImGui link dependency in test
     * binaries.
     * @param state GUI state (read-only)
     * @return Pointer to results to display (filtered or unfiltered)
     */
    static const std::vector<SearchResult>* GetDisplayResults(const GuiState& state) {
      if (!state.search_pipeline.defer_filter_cache_rebuild) {
        if (state.searchCriteria.size_filter != SizeFilter::None &&
            state.filter_caches.size.IsValidFor(state.GetResultsVersion())) {
          return &state.filter_caches.size.results;
        }
        if (state.searchCriteria.time_filter != TimeFilter::None &&
            state.filter_caches.time.IsValidFor(state.GetResultsVersion())) {
          return &state.filter_caches.time.results;
        }
      }
      return &state.result_pool_->Results();
    }
  };
}  // namespace search
