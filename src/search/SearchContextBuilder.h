#pragma once

#include <atomic>
#include <string_view>
#include <vector>

#include "search/SearchContext.h"

/**
 * @file SearchContextBuilder.h
 * @brief Builds SearchContext from search parameters (extracted from FileIndex Option C)
 *
 * Responsibilities:
 * - Build SearchContext from query, path_query, extensions, flags
 * - Pre-compile PathPattern patterns via CompilePatternIfNeeded
 *
 * Caller (FileIndex) passes built context to ParallelSearchEngine.
 * For SearchAsyncWithData, caller may set dynamic_chunk_size and hybrid_initial_percent
 * from settings and call context.ValidateAndClamp() before passing to engine.
 *
 * @see docs/2026-01-31_FILEINDEX_REFACTORING_PLAN.md Option C
 */
class SearchContextBuilder {
public:
  /**
   * Build a SearchContext from search parameters.
   * Pre-compiles PathPattern patterns when query/path_query match PathPattern type.
   */
  [[nodiscard]] static SearchContext Build(
      std::string_view query,
      std::string_view path_query,
      const std::vector<std::string>* extensions,
      bool folders_only,
      bool case_sensitive,
      const std::atomic<bool>* cancel_flag = nullptr);
};
