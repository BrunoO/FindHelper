#pragma once

/**
 * @file IIndexSearch.h
 * @brief Interface for parallel search operations against an indexed file tree
 *
 * Provides the search entry point used by SearchWorker and other background
 * search callers. FileIndex implements this interface so consumers depend on
 * IIndexSearch rather than the concrete FileIndex type.
 */

#include "search/SearchTypes.h"

#include <atomic>
#include <future>
#include <string_view>
#include <vector>

struct AppSettings;

/**
 * Interface for launching parallel searches against a file index.
 *
 * SearchWorker and similar background components use this interface to run
 * SearchAsyncWithData without coupling to FileIndex implementation details.
 */
class IIndexSearch {
public:
  virtual ~IIndexSearch() = default;

protected:
  IIndexSearch() = default;

public:
  IIndexSearch(const IIndexSearch&) = delete;
  IIndexSearch& operator=(const IIndexSearch&) = delete;
  IIndexSearch(IIndexSearch&&) = delete;
  IIndexSearch& operator=(IIndexSearch&&) = delete;

  /**
   * Parallel search returning futures with per-task result batches.
   *
   * Returns futures that yield SearchResultBatch objects (arena-owned path
   * bytes plus offset hits) so post-processing avoids FileEntry lookups
   * without per-hit heap strings on worker threads. Callers convert batches
   * with AppendBatchToResultData (single-threaded).
   *
   * @param query Filename query (searches filename part of path)
   * @param thread_count Number of threads (-1 = auto from hardware concurrency)
   * @param stats Optional output for search statistics
   * @param path_query Optional path query (directory part of path)
   * @param extensions Optional extension filter (nullptr or empty = no filter)
   * @param folders_only When true, only directory entries match
   * @param case_sensitive When true, case-sensitive filename/path matching
   * @param thread_timings Optional per-thread timing for load-balance analysis
   * @param cancel_flag Optional flag checked by worker threads for early exit
   * @param optional_settings When non-null, avoids LoadSettings file I/O
   * @return Futures yielding result chunks as they complete
   */
  [[nodiscard]] virtual std::vector<std::future<SearchResultBatch>>
  SearchAsyncWithData(std::string_view query, int thread_count,
                      SearchStats* stats,
                      std::string_view path_query,
                      const std::vector<std::string>* extensions,
                      bool folders_only, bool case_sensitive,
                      std::vector<ThreadTiming>* thread_timings,
                      const std::atomic<bool>* cancel_flag,
                      const AppSettings* optional_settings) = 0;
};
