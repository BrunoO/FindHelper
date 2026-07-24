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
                                   FileIndex& file_index,
                                   ThreadPool& thread_pool);

    /**
     * @brief Check if async sorting is complete and perform sort if ready
     * @param state GUI state (modified if sorting completes)
     * @param file_index File index for attribute lookups (read-only)
     * @param thread_pool Thread pool for async operations
     * @return true if sort was completed, false if still loading
     */
    static bool CheckAndCompleteAsyncSort(GuiState& state,
                                          const FileIndex& file_index,
                                          ThreadPool& thread_pool);

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
    static inline const std::vector<SearchResult>* GetDisplayResults(const GuiState& state) {
      if (!state.deferFilterCacheRebuild) {
        if (state.sizeFilter != SizeFilter::None && state.sizeFilterCacheValid) {
          return &state.sizeFilteredResults;
        }
        if (state.timeFilter != TimeFilter::None && state.timeFilterCacheValid) {
          return &state.filteredResults;
        }
      }
      return &state.result_pool_->Results();
    }
  };
}  // namespace search
