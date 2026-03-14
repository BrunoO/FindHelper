#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>  // NOSONAR S3806 - lowercase for portability (AGENTS.md); Windows SDK may report as Windows.h
#else
#include "utils/FileTimeTypes.h"
#endif  // _WIN32

/**
 * @file SearchTypes.h
 * @brief Shared types for search operations (result data, timing, statistics)
 *
 * These types were previously nested in FileIndex. Moving them here:
 * - Reduces FileIndex header surface and coupling
 * - Allows search/ and utils/ code to depend on search types without FileIndex
 * - Clarifies that these are "search result" types, not "index" types
 *
 * Used by: FileIndex, ParallelSearchEngine, LoadBalancingStrategy,
 * SearchWorker, SearchStatisticsCollector.
 */

/** Load balancing strategy for parallel search (used by ParallelSearchEngine and LoadBalancingStrategy factory). */
enum class LoadBalancingStrategyType : std::uint8_t {  // NOLINT(performance-enum-size) - explicit uint8_t; checker may still flag in some contexts
  Static = 0,       // Original static chunking (fixed chunks assigned upfront)
  Hybrid = 1,       // Initial large chunks + dynamic small chunks (recommended)
  Dynamic = 2,      // Pure dynamic chunking (small chunks assigned dynamically)
  Interleaved = 3,  // Threads process items in an interleaved manner
  WorkStealing = 4  // Distributed queues with stealing (FAST_LIBS_BOOST only)
};

/** Search statistics (duration, counts, thread count). */
struct SearchStats {
  double duration_milliseconds_ = 0.0;   // NOLINT(readability-identifier-naming) - snake_case_ per project convention
  size_t total_items_scanned_ = 0;       // NOLINT(readability-identifier-naming) - snake_case_ per project convention
  size_t total_matches_found_ = 0;        // NOLINT(readability-identifier-naming) - snake_case_ per project convention
  int num_threads_used_ = 0;              // NOLINT(readability-identifier-naming) - snake_case_ per project convention
};

/** Lightweight result data extracted during search (avoids FileEntry lookup).
 * fullPath is a view into PathStorage SoA; must not outlive the search/merge phase. */
struct SearchResultData {  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - members default-constructed
  uint64_t id = 0;  // NOLINT(readability-identifier-naming)
  std::string_view fullPath;  // NOLINT(readability-identifier-naming) - view into SoA path_storage
  bool isDirectory = false;  // NOLINT(readability-identifier-naming)
};

/** Column indices for the results table. */
namespace ResultColumn {  // NOLINT(readability-identifier-naming) - Public enum-like namespace used across UI/search code
constexpr int Mark = 0;          // NOLINT(readability-identifier-naming)
constexpr int Filename = 1;      // NOLINT(readability-identifier-naming)
constexpr int Size = 2;          // NOLINT(readability-identifier-naming)
constexpr int LastModified = 3;  // NOLINT(readability-identifier-naming)
constexpr int FullPath = 4;      // NOLINT(readability-identifier-naming)
constexpr int Extension = 5;     // NOLINT(readability-identifier-naming)
constexpr int FolderFileCount = 6;   // NOLINT(readability-identifier-naming) - Optional folder stats column
constexpr int FolderTotalSize = 7;   // NOLINT(readability-identifier-naming) - Optional folder stats column
}  // namespace ResultColumn

/** Per-thread timing information for load balancing analysis. */
struct ThreadTiming {
  uint64_t thread_index_ = 0;            // NOLINT(readability-identifier-naming) - Thread index (0-based)
  uint64_t elapsed_ms_ = 0;              // NOLINT(readability-identifier-naming) - Time taken by this thread in milliseconds
  size_t items_processed_ = 0;           // NOLINT(readability-identifier-naming) - Number of items processed in initial chunk (end_index_ - start_index_)
  size_t start_index_ = 0;               // NOLINT(readability-identifier-naming) - Starting index for initial chunk
  size_t end_index_ = 0;                 // NOLINT(readability-identifier-naming) - Ending index for initial chunk
  size_t results_count_ = 0;             // NOLINT(readability-identifier-naming) - Number of results found by this thread (initial + dynamic)
  size_t bytes_processed_ = 0;           // NOLINT(readability-identifier-naming) - Total byte size of paths processed in initial chunk
  size_t dynamic_chunks_processed_ = 0;  // NOLINT(readability-identifier-naming) - Number of dynamic chunks processed (0 = no dynamic, >0 = dynamic active)
  size_t total_items_processed_ = 0;     // NOLINT(readability-identifier-naming) - Total items processed (initial + dynamic chunks)
};

struct SearchResult {  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - mutable string members default-constructed; fullPath is view into GuiState::searchResultPathPool
  std::string_view fullPath;  // NOLINT(readability-identifier-naming) - points into searchResultPathPool; pool must outlive this

  /**
   * @brief Get the filename part of the full path
   * @return std::string_view pointing into fullPath
   */
  [[nodiscard]] std::string_view GetFilename() const {
    if (filename_offset < fullPath.size()) {
      size_t count = std::string_view::npos;
      if (extension_offset != std::string_view::npos &&
          extension_offset > filename_offset) {
        count = extension_offset - filename_offset - 1; // Exclude the dot
      }
      return fullPath.substr(filename_offset, count);
    }
    return {};
  }

  /**
   * @brief Get the extension part of the full path (including the dot if desired, but here it follows existing logic)
   * @return std::string_view pointing into fullPath
   */
  [[nodiscard]] std::string_view GetExtension() const {
    if (extension_offset != std::string_view::npos &&
        extension_offset < fullPath.size()) {
      return fullPath.substr(extension_offset);
    }
    return {};
  }

  size_t filename_offset = 0; // NOLINT(readability-identifier-naming)
  size_t extension_offset = std::string_view::npos; // NOLINT(readability-identifier-naming)

  mutable uint64_t fileSize = 0; // NOLINT(readability-identifier-naming)
  mutable std::string fileSizeDisplay; // NOLINT(readability-identifier-naming, cppcoreguidelines-pro-type-member-init, hicpp-member-init) - default-constructed empty
  mutable FILETIME lastModificationTime = {0, 0}; // NOLINT(readability-identifier-naming,cppcoreguidelines-pro-type-member-init,hicpp-member-init)
  mutable std::string lastModificationDisplay; // NOLINT(readability-identifier-naming, cppcoreguidelines-pro-type-member-init, hicpp-member-init) - default-constructed empty
  // Cached truncated path for UI display (avoids expensive CalcTextSize every frame)
  mutable std::string truncatedPathDisplay; // NOLINT(readability-identifier-naming,cppcoreguidelines-pro-type-member-init,hicpp-member-init) - default-constructed empty
  mutable float truncatedPathColumnWidth = -1.0F; // NOLINT(readability-identifier-naming)
  uint64_t fileId = 0; // NOLINT(readability-identifier-naming)
  bool isDirectory = false; // NOLINT(readability-identifier-naming)
};

struct SearchParams {  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - std::string members default-constructed
  std::string filenameInput; // NOLINT(readability-identifier-naming,cppcoreguidelines-pro-type-member-init,hicpp-member-init) - default-constructed empty
  std::string extensionInput; // NOLINT(readability-identifier-naming,cppcoreguidelines-pro-type-member-init,hicpp-member-init) - default-constructed empty
  std::string pathInput; // NOLINT(readability-identifier-naming,cppcoreguidelines-pro-type-member-init,hicpp-member-init) - default-constructed empty
  bool foldersOnly = false; // NOLINT(readability-identifier-naming)
  bool caseSensitive = false; // NOLINT(readability-identifier-naming)
};

// Search metrics for performance tracking (lightweight, no overhead)
struct SearchMetrics {
  // Counters
  std::atomic<size_t> total_searches_{0};       // NOLINT(readability-identifier-naming) - public atomic counter (SearchMetrics)
  std::atomic<size_t> total_results_found_{0};  // NOLINT(readability-identifier-naming)

  // Timing (in milliseconds)
  std::atomic<uint64_t> total_search_time_ms_{0};      // NOLINT(readability-identifier-naming)
  std::atomic<uint64_t> total_postprocess_time_ms_{0}; // NOLINT(readability-identifier-naming)
  std::atomic<uint64_t> max_search_time_ms_{0};        // NOLINT(readability-identifier-naming)
  std::atomic<uint64_t> max_postprocess_time_ms_{0};   // NOLINT(readability-identifier-naming)

  // Last search stats (for quick reference)
  std::atomic<uint64_t> last_search_time_ms_{0};      // NOLINT(readability-identifier-naming) - Last search duration
  std::atomic<uint64_t> last_postprocess_time_ms_{0}; // NOLINT(readability-identifier-naming) - Last post-processing duration
  std::atomic<size_t> last_results_count_{0};         // NOLINT(readability-identifier-naming) - Last search results count
  std::atomic<size_t> max_results_count_{0};          // NOLINT(readability-identifier-naming) - Maximum results in single search

  // Reset all metrics
  void Reset() {
    total_searches_.store(0);
    total_results_found_.store(0);
    total_search_time_ms_.store(0);
    total_postprocess_time_ms_.store(0);
    max_search_time_ms_.store(0);
    max_postprocess_time_ms_.store(0);
    last_search_time_ms_.store(0);
    last_postprocess_time_ms_.store(0);
    last_results_count_.store(0);
    max_results_count_.store(0);
  }

  // Get snapshot (thread-safe)
  struct Snapshot {
    size_t total_searches_;             // NOLINT(readability-identifier-naming)
    size_t total_results_found_;        // NOLINT(readability-identifier-naming)
    uint64_t total_search_time_ms_;     // NOLINT(readability-identifier-naming)
    uint64_t total_postprocess_time_ms_;  // NOLINT(readability-identifier-naming)
    uint64_t max_search_time_ms_;       // NOLINT(readability-identifier-naming)
    uint64_t max_postprocess_time_ms_;  // NOLINT(readability-identifier-naming)
    uint64_t last_search_time_ms_;      // NOLINT(readability-identifier-naming)
    uint64_t last_postprocess_time_ms_; // NOLINT(readability-identifier-naming)
    size_t last_results_count_;         // NOLINT(readability-identifier-naming)
    size_t max_results_count_;          // NOLINT(readability-identifier-naming)
  };

  [[nodiscard]] Snapshot GetSnapshot() const {
    Snapshot s{};
    s.total_searches_ = total_searches_.load(std::memory_order_acquire);
    s.total_results_found_ = total_results_found_.load(std::memory_order_acquire);
    s.total_search_time_ms_ =
        total_search_time_ms_.load(std::memory_order_acquire);
    s.total_postprocess_time_ms_ =
        total_postprocess_time_ms_.load(std::memory_order_acquire);
    s.max_search_time_ms_ = max_search_time_ms_.load(std::memory_order_acquire);
    s.max_postprocess_time_ms_ =
        max_postprocess_time_ms_.load(std::memory_order_acquire);
    s.last_search_time_ms_ = last_search_time_ms_.load(std::memory_order_acquire);
    s.last_postprocess_time_ms_ =
        last_postprocess_time_ms_.load(std::memory_order_acquire);
    s.last_results_count_ = last_results_count_.load(std::memory_order_acquire);
    s.max_results_count_ = max_results_count_.load(std::memory_order_acquire);
    return s;
  }
};
