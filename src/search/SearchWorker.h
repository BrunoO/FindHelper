#pragma once

#include "index/FileIndex.h"
#include "search/SearchTypes.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Simple timer for metrics (lightweight, no logging overhead)
struct MetricsTimer {
  std::chrono::high_resolution_clock::time_point start{std::chrono::high_resolution_clock::now()};  // NOLINT(misc-non-private-member-variables-in-classes) - Simple struct for metrics, public member is intentional
  MetricsTimer() = default;
  [[nodiscard]] uint64_t ElapsedMs() const {
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
        .count();
  }
};

struct AppSettings;
class StreamingResultsCollector;

/**
 * Merge SearchResultData into a path pool and convert to SearchResult (path-pool optimization).
 * Appends each path to pool (with trailing null for .c_str() use); each SearchResult.fullPath
 * is a string_view into the pool. Caller must clear pool before calling when replacing all results.
 */
std::vector<SearchResult> MergeAndConvertToSearchResults(  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - free function, PascalCase API
    std::vector<char>& pool,
    const std::vector<SearchResultData>& data,
    const FileIndex& file_index);

/**
 * SearchWorker - Background Search Processing
 *
 * This class performs file search operations in a background thread to prevent
 * UI blocking. It processes search requests asynchronously and provides results
 * to the UI thread via polling.
 *
 * DESIGN NOTES:
 * - Takes a FileIndex reference directly rather than an abstract interface.
 *   This is acceptable because:
 *   1. FileIndex is a stable, core component that is unlikely to change
 *   2. SearchWorker needs access to multiple specific FileIndex methods
 *      (SearchAsyncWithData, Size, ForEachEntry, GetPaths, GetEntryRef)
 *   3. Creating a full interface abstraction would add complexity without
 *      significant benefit given FileIndex's stability
 * - The coupling to FileIndex is intentional and documented, not accidental
 */
class SearchWorker {
public:
  // Constructor takes FileIndex reference directly.
  // See class documentation above for design rationale.
  explicit SearchWorker(FileIndex &file_index);
  ~SearchWorker();
  
  // Non-copyable, non-movable (manages search state and threads)
  SearchWorker(const SearchWorker &) = delete;
  SearchWorker &operator=(const SearchWorker &) = delete;
  SearchWorker(SearchWorker &&) = delete;
  SearchWorker &operator=(SearchWorker &&) = delete;

  // Starts a new search with the given parameters.
  // If a search is already running, it will be cancelled and restarted.
  // optional_settings: when non-null, passed to search path to avoid file I/O (LoadSettings).
  void StartSearch(const SearchParams &params,
                   const AppSettings *optional_settings = nullptr);

  // Returns true if a new search result is available.
  [[nodiscard]] bool HasNewResults() const;

  /**
   * Returns the latest search result data (paths and ids) for path-pool conversion on the UI thread.
   * Call MergeAndConvertToSearchResults(state.searchResultPathPool, data, file_index) to get SearchResult vector.
   * Clears the "new results" flag.
   */
  std::vector<SearchResultData> GetResultsData();

  // Returns true if the worker is currently searching.
  [[nodiscard]] bool IsSearching() const;

  // Returns true if the worker is searching or has a pending request.
  [[nodiscard]] bool IsBusy() const;

  // Returns true if the current search is complete (all chunks processed).
  // Used to determine if results are final and can be sorted.
  [[nodiscard]] bool IsSearchComplete() const;

  // Returns the streaming results collector if streaming is active, nullptr otherwise.
  StreamingResultsCollector* GetStreamingCollector();

  /**
   * Discards all cached results (streaming collector and non-streaming buffer).
   * Call when the user explicitly clears results (e.g., Clear All button) so that
   * PollResults does not re-apply results from the streaming collector on the next frame.
   */
  void DiscardResults();

  /**
   * Signals the current search to cancel. The worker will exit early and finish.
   * Call when Clear All is clicked during an active search to avoid use-after-free
   * when discarding the collector before the worker has finished.
   */
  void CancelSearch();

  // Sets whether to use streaming for the next search.
  void SetStreamPartialResults(bool stream) { stream_partial_results_ = stream; }

  // Metrics access - thread-safe
  [[nodiscard]] const SearchMetrics &GetMetrics() const { return metrics_; }
  [[nodiscard]] SearchMetrics::Snapshot GetMetricsSnapshot() const {
    return metrics_.GetSnapshot();
  }
  void ResetMetrics() { metrics_.Reset(); }

private:
  void WorkerThread();
  // Helpers for WorkerThread to keep cognitive complexity under Sonar limit (cpp:S3776)
  [[nodiscard]] bool WaitForSearchRequest(SearchParams& params);
  void ExecuteSearch(const SearchParams& params);

  void RunFilteredSearchPath(const SearchParams& params,
                             const std::vector<std::string>& extensions,
                             std::string_view filename_search,
                             std::string_view path_search,
                             std::vector<SearchResultData>& results);
  void RunShowAllPath(const SearchParams& params,
                      const std::vector<std::string>& extensions,
                      std::string_view filename_search,
                      std::string_view path_search,
                      std::vector<SearchResultData>& results);
  // Completion-order loop for streaming search
  void ProcessStreamingSearchFutures(
      std::vector<std::future<std::vector<SearchResultData>>>& search_futures,
      StreamingResultsCollector& collector,
      const MetricsTimer& search_timer,
      uint64_t& search_time_ms);

  FileIndex &file_index_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  std::thread thread_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  mutable std::mutex mutex_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  std::condition_variable cv_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members

  // Synchronization notes:
  // - search_requested_: only indicates a pending request; accompanying data (next_params_, next_search_settings_) are protected by mutex_.
  //   Access and modifications that rely on those must hold mutex_.
  // - has_new_results_: guards visibility of results_data_ but reads/writes of results_data_ are under mutex_.
  // - is_searching_, search_complete_: status flags for UI; do not gate access to shared data.

  // Shared state (protected by mutex_)
  bool running_ = true;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  bool search_requested_ = false;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  SearchParams next_params_{};  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  const AppSettings* next_search_settings_ = nullptr;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  const AppSettings* current_search_settings_ = nullptr;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members

  // Result state (protected by mutex_)
  bool has_new_results_ = false;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  std::vector<SearchResultData> results_data_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members

  // Atomic flags for lock-free UI status checks
  std::atomic<bool> is_searching_ = false;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  std::atomic<bool> search_complete_ = false; // NOLINT(readability-identifier-naming) - True when all chunks are processed; project convention: snake_case_ for members
  std::atomic<bool> stream_partial_results_ = true;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  
  // shared_ptr so worker can hold a reference during ProcessStreamingSearchFutures;
  // prevents use-after-free when DiscardResults or StartSearch destroys collector
  // while worker is in exception handler calling SetError.
  std::shared_ptr<StreamingResultsCollector> collector_{nullptr};  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members

  // Cancellation flag: set to true when a new search is requested while one is running
  // This allows search lambdas to check and exit early
  std::atomic<bool> cancel_current_search_ = false;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members

  // Search metrics (lightweight, no overhead)
  SearchMetrics metrics_{};  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
};
