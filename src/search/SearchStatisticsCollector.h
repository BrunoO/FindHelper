#pragma once

/**
 * @file SearchStatisticsCollector.h
 * @brief Statistics collection for parallel search operations
 *
 * This class encapsulates statistics collection logic for search operations,
 * including thread timing, chunk bytes calculation, and result aggregation.
 *
 * Benefits:
 * - Single responsibility for statistics
 * - Easier to test statistics logic independently
 * - Can be reused for different search types
 */

#include "path/PathStorage.h"
#include "search/SearchTypes.h"

#include <chrono>
#include <future>
#include <vector>

/**
 * Statistics collector for parallel search operations
 *
 * This class provides methods to collect and aggregate statistics during
 * parallel search operations, including thread timing, chunk processing,
 * and result counting.
 */
class SearchStatisticsCollector {
public:
  /**
   * Calculate total bytes for a chunk range
   * 
   * Calculates the total size of paths in the specified chunk range.
   * Used by load balancing strategies to estimate work distribution.
   * 
   * @param soaView Read-only view of SoA arrays
   * @param start_index_ Start index of chunk (inclusive)
   * @param end_index_ End index of chunk (exclusive)
   * @param storage_size Total size of path storage in bytes
   * @return Total bytes in the chunk range
   */
  static size_t CalculateChunkBytes(
      const PathStorage::SoAView& soaView,  // NOLINT(readability-identifier-naming) - SoAView type name, mirror camelCase
      size_t start_index_,  // NOLINT(readability-identifier-naming) - mirrors ThreadTiming/SoAView field names
      size_t end_index_,  // NOLINT(readability-identifier-naming)
      size_t storage_size);

  /**
   * Record thread timing information
   * 
   * Fills in thread timing statistics for performance monitoring.
   * Used by load balancing strategies to record per-thread metrics.
   * 
   * @param timing ThreadTiming struct to populate
   * @param thread_idx Thread index (0-based)
   * @param start_index_ Starting index for initial chunk
   * @param end_index_ Ending index for initial chunk
   * @param initial_items Number of items in initial chunk
   * @param dynamic_chunks_count Number of dynamic chunks processed
   * @param total_items_processed_ Total items processed (initial + dynamic)
   * @param bytes_processed_ Total bytes processed in initial chunk
   * @param results_count_ Number of results found by this thread
   * @param elapsed_ms_ Time taken by this thread in milliseconds
   */
  static inline void RecordThreadTiming(  // NOSONAR(cpp:S107) - Small value helper mirroring ThreadTiming fields; explicit parameter list keeps call sites clear and avoids extra indirection
      ThreadTiming& timing,
      uint64_t thread_idx,
      size_t start_index_,  // NOLINT(readability-identifier-naming) - mirrors ThreadTiming field names
      size_t end_index_,  // NOLINT(readability-identifier-naming)
      size_t initial_items,
      size_t dynamic_chunks_count,
      size_t total_items_processed_,  // NOLINT(readability-identifier-naming)
      size_t bytes_processed_,  // NOLINT(readability-identifier-naming)
      size_t results_count_,  // NOLINT(readability-identifier-naming)
      int64_t elapsed_ms_) {  // NOLINT(readability-identifier-naming)
    timing.thread_index_ = thread_idx;
    timing.elapsed_ms_ = static_cast<uint64_t>(elapsed_ms_);
    timing.items_processed_ = initial_items;
    timing.start_index_ = start_index_;
    timing.end_index_ = end_index_;
    timing.results_count_ = results_count_;
    timing.bytes_processed_ = bytes_processed_;
    timing.dynamic_chunks_processed_ = dynamic_chunks_count;
    timing.total_items_processed_ = total_items_processed_;
  }

  /**
   * Aggregate search results into SearchStats
   * 
   * Aggregates results from futures and populates SearchStats with:
   * - total_matches_found_: Total number of matching results
   * - total_items_scanned_: Total number of items scanned (already set by caller)
   * - num_threads_used_: Number of threads used (already set by caller)
   * - duration_milliseconds_: Search duration in milliseconds
   * 
   * @param stats SearchStats to populate
   * @param futures Vector of futures containing search results
   * @param start_time Start time of the search operation
   * 
   * @tparam ResultType Type of result (uint64_t for IDs, SearchResultData for full data)
   */
  template<typename ResultType>
  static void AggregateResults(
      SearchStats& stats,
      std::vector<std::future<std::vector<ResultType>>>& futures,
      std::chrono::steady_clock::time_point start_time) {
    // Count total matches from all futures
    size_t total_matches = 0;
    for (auto& future : futures) {
      total_matches += future.get().size();
    }

    // Calculate duration
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    auto duration_ms = static_cast<double>(duration.count());

    // Update stats
    stats.total_matches_found_ = total_matches;
    stats.duration_milliseconds_ = duration_ms;
  }

  /**
   * Aggregate search results into SearchStats (overload for ID-only results)
   * 
   * Specialized version for ID-only search results (uint64_t).
   * 
   * @param stats SearchStats to populate
   * @param futures Vector of futures containing search result IDs (non-const because future.get() is not const)
   * @param start_time Start time of the search operation
   */
  static void AggregateIdResults(
      SearchStats& stats,
      std::vector<std::future<std::vector<uint64_t>>>& futures,
      std::chrono::steady_clock::time_point start_time);

  /**
   * Aggregate search results into SearchStats (overload for full data results)
   * 
   * Specialized version for full search data results (SearchResultData).
   * 
   * @param stats SearchStats to populate
   * @param futures Vector of futures containing search result data (non-const because future.get() is not const)
   * @param start_time Start time of the search operation
   */
  static void AggregateDataResults(
      SearchStats& stats,
      std::vector<std::future<std::vector<SearchResultData>>>& futures,
      std::chrono::steady_clock::time_point start_time);
};

