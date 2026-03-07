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
#include "imgui.h"
#include "index/FileIndex.h"
#include "utils/ThreadPool.h"

// Forward declarations
struct SearchResult;

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
     * During streaming (!resultsComplete && showingPartialResults), returns partialResults;
     * otherwise returns filtered cache or searchResults. See STREAMING_SEARCH_RESULTS_DESIGN.md.
     * @param state GUI state (read-only)
     * @return Pointer to results to display (filtered, unfiltered, or partial during streaming)
     */
    static const std::vector<SearchResult>* GetDisplayResults(const GuiState& state);
  };
}  // namespace search
