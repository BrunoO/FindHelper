#include "search/SearchWorker.h"

#include "ctrack.hpp"
#include "index/IIndexSearch.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <future>
#include <iomanip>
#include <new>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>

#include "search/SearchPatternUtils.h"
#include "utils/ExceptionHandling.h"
#include "utils/FileSystemUtils.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"
#include "utils/ThreadUtils.h"

// Helper function to atomically update maximum value using compare-and-swap
// This eliminates duplicate atomic CAS loops for updating max metrics
template<typename T>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - Template function, not a global variable (clang-tidy false positive), naming follows project convention
static void UpdateAtomicMax(std::atomic<T>& atomic_var, T new_value) {
  T current = atomic_var.load();
  while (new_value > current &&
         !atomic_var.compare_exchange_weak(
             current, new_value)) {
    // Loop until we update or find a larger value
  }
}

/**
 * @brief Format bytes to human-readable string (B, KB, MB)
 *
 * Converts a byte count to a human-readable string with appropriate unit.
 *
 * @param bytes Byte count to format
 * @return Formatted string (e.g., "1024 B", "512 KB", "2 MB")
 */
static std::string FormatBytes(size_t bytes) {
  constexpr size_t k_bytes_per_kb = 1024;
  constexpr size_t k_bytes_per_mb = 1024 * 1024;  // NOLINT(bugprone-implicit-widening-of-multiplication-result) - Intentional: 1024 * 1024 fits in int, then widened to size_t

  std::string result;
  if (bytes < k_bytes_per_kb) {
    result = std::to_string(bytes);
    result += " B";
  } else if (bytes < k_bytes_per_mb) {
    result = std::to_string(bytes / k_bytes_per_kb);
    result += " KB";
  } else {
    result = std::to_string(bytes / k_bytes_per_mb);
    result += " MB";
  }
  return result;
}

/**
 * @brief Log per-thread timing analysis
 *
 * Logs detailed timing information for each thread, including bytes processed,
 * dynamic chunking status, and result counts.
 *
 * @param thread_timings Vector of thread timing data
 * @param min_time Output parameter: minimum time (updated)
 * @param max_time Output parameter: maximum time (updated)
 * @param total_time Output parameter: total time (updated)
 * @param min_bytes Output parameter: minimum bytes (updated)
 * @param max_bytes Output parameter: maximum bytes (updated)
 * @param total_bytes Output parameter: total bytes (updated)
 */
static void LogPerThreadTiming(
    const std::vector<ThreadTiming> &thread_timings,
    uint64_t &min_time,
    uint64_t &max_time,
    uint64_t &total_time,
    size_t &min_bytes,
    size_t &max_bytes,
    size_t &total_bytes) {
  LOG_INFO_BUILD("=== Per-Thread Timing Analysis ===");
  for (const auto &timing : thread_timings) {
    if (timing.elapsed_ms_ > 0) { // Only log active threads
      // Format bytes in human-readable form
      const std::string bytes_str = FormatBytes(timing.bytes_processed_);  // NOLINT(bugprone-unused-local-non-trivial-variable,readability-redundant-string-init) - only used in LOG_INFO_BUILD; may appear unused when logging is compiled out

      // Show dynamic chunking status
      std::string dynamic_info;
      if (timing.dynamic_chunks_processed_ > 0) {
        dynamic_info = ", ";
        dynamic_info += std::to_string(timing.dynamic_chunks_processed_);
        dynamic_info += " dynamic chunks, ";
        dynamic_info += std::to_string(timing.total_items_processed_);
        dynamic_info += " total items";
      } else {
        dynamic_info = " (no dynamic chunks)";
      }

      LOG_INFO_BUILD(
          "Thread " << timing.thread_index_ << ": " << timing.elapsed_ms_
                    << "ms, " << timing.items_processed_
                    << " initial items" << dynamic_info << ", "
                    << timing.bytes_processed_ << " bytes (" << bytes_str
                    << "), " << timing.results_count_ << " results, "
                    << "initial range [" << timing.start_index_ << "-"
                    << timing.end_index_ << "]");

      min_time = (std::min)(min_time, timing.elapsed_ms_);
      max_time = (std::max)(max_time, timing.elapsed_ms_);
      total_time += timing.elapsed_ms_;
      min_bytes = (std::min)(min_bytes, timing.bytes_processed_);
      max_bytes = (std::max)(max_bytes, timing.bytes_processed_);
      total_bytes += timing.bytes_processed_;
    }
  }
}

/**
 * @brief Log load balance analysis
 *
 * Logs load balance metrics including time and byte distribution, dynamic chunking
 * status, and correlation analysis between byte and time imbalances.
 */
struct TimeStats {
  uint64_t min = 0;
  uint64_t max = 0;
  uint64_t avg = 0;
};

struct ByteStats {
  size_t min = 0;
  size_t max = 0;
  size_t avg = 0;
};

static void LogLoadBalanceAnalysis(
    [[maybe_unused]] const TimeStats& time_stats,  // NOSONAR(cpp:S1172) - Used in LOG_INFO_BUILD below; may be unused when logging is compiled out
    const ByteStats& byte_stats,
    const std::vector<ThreadTiming> &thread_timings,
    double imbalance_ratio,
    double byte_imbalance_ratio) {
  // Format bytes for display
  const std::string min_bytes_str = FormatBytes(byte_stats.min);  // NOLINT(bugprone-unused-local-non-trivial-variable) - only used in LOG_INFO_BUILD; may appear unused when logging is compiled out
  const std::string max_bytes_str = FormatBytes(byte_stats.max);  // NOLINT(bugprone-unused-local-non-trivial-variable)
  const std::string avg_bytes_str = FormatBytes(byte_stats.avg);  // NOLINT(bugprone-unused-local-non-trivial-variable)

  // Count threads that used dynamic chunking (used in LOG_INFO_BUILD below; Sonar may not expand macro)
  const size_t threads_with_dynamic = std::count_if(  // NOSONAR(cpp:S1481,cpp:S1854) NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
      thread_timings.begin(), thread_timings.end(),
      [](const ThreadTiming& t) { return t.dynamic_chunks_processed_ > 0; });

  LOG_INFO_BUILD("Load Balance: min="
                 << time_stats.min << "ms, max=" << time_stats.max << "ms, avg="
                 << time_stats.avg << "ms, imbalance=" << std::fixed
                 << std::setprecision(2) << imbalance_ratio << "x");
  LOG_INFO_BUILD("Byte Distribution: min="
                 << min_bytes_str << ", max=" << max_bytes_str
                 << ", avg=" << avg_bytes_str << ", byte_imbalance="
                 << std::fixed << std::setprecision(2)
                 << byte_imbalance_ratio << "x");
  LOG_INFO_BUILD("Dynamic Chunking: "
                 << threads_with_dynamic << " of "
                 << thread_timings.size()
                 << " threads processed dynamic chunks"
                 << (threads_with_dynamic > 0
                         ? " (ACTIVE)"
                         : " (not needed - well balanced)"));

  // Check if byte imbalance correlates with time imbalance (single log to avoid branch-clone)
  constexpr double k_byte_imbalance_threshold = 1.3;
  constexpr double k_time_imbalance_threshold = 1.5;
  const bool time_imbalanced = imbalance_ratio > k_time_imbalance_threshold;
  const bool byte_imbalanced = byte_imbalance_ratio > k_byte_imbalance_threshold;
  std::ostringstream analysis_msg;
  analysis_msg << std::fixed << std::setprecision(2);
  if (byte_imbalanced && time_imbalanced) {
    analysis_msg << "ANALYSIS: Byte imbalance (" << byte_imbalance_ratio
                 << "x) correlates with time imbalance (" << imbalance_ratio
                 << "x). Byte-based chunking would help.";
  } else if (time_imbalanced && !byte_imbalanced) {
    analysis_msg << "ANALYSIS: Time imbalance (" << imbalance_ratio
                 << "x) exists but byte distribution is balanced (" << byte_imbalance_ratio
                 << "x). Other factors (regex complexity, cache misses) may be causing imbalance.";
  } else if (time_imbalanced) {
    analysis_msg << "WARNING: Significant load imbalance detected (>1.5x). "
                  << "Consider byte-based chunking for better distribution.";
  }
  const std::string msg_str = analysis_msg.str();
  if (!msg_str.empty()) {
    LOG_INFO_BUILD(msg_str);
  }
}

SearchWorker::SearchWorker(IIndexSearch& index_search) : index_search_(index_search) {
  thread_ = std::thread(&SearchWorker::WorkerThread, this);
}

SearchWorker::~SearchWorker() {
  {
    const std::scoped_lock lock(mutex_);
    thread_running_ = false;
    search_requested_ = false;
  }
  // Abort a mid-scan search so join() drains a cancellation (workers poll
  // this flag in ProcessChunkRange/ProcessSearchFutures) instead of waiting
  // out the full scan. Atomic: no lock needed.
  cancel_current_search_.store(true);
  cv_.notify_one();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void SearchWorker::StartSearch(const SearchParams &params,
                               const AppSettings *optional_settings) {
  {
    const std::scoped_lock lock(mutex_);
    // If a scan is in flight, cancel it so workers exit early. Gated on
    // is_searching_ (not thread liveness): both this read and the worker's
    // stores happen under mutex_, so the flag accurately reflects an active
    // scan; a false read means the worker is between scans with nothing to
    // cancel (WaitForSearchRequest resets the flag before every scan anyway).
    if (is_searching_.load()) {
      cancel_current_search_.store(true);
    }
    next_params_ = params;
    next_search_settings_ = optional_settings;
    search_requested_ = true;
  }
  cv_.notify_one();
}

bool SearchWorker::HasNewResults() const {
  const std::scoped_lock lock(mutex_);
  return has_new_results_;
}

std::vector<SearchResultBatch> SearchWorker::GetResultsData() {
  const std::scoped_lock lock(mutex_);
  has_new_results_ = false;
  std::vector<SearchResultBatch> res = std::move(results_data_);
  // Assign a fresh empty vector: the buffer was transferred to res, and a moved-from
  // object is only valid-but-unspecified — never call methods on it (cpp:S5272).
  results_data_ = {};
  return res;
}

bool SearchWorker::IsSearching() const {
  // This is a lock-free check for UI status.
  return is_searching_.load();
}

bool SearchWorker::IsBusy() const {
  const std::scoped_lock lock(mutex_);
  return is_searching_.load() || search_requested_;
}

bool SearchWorker::IsSearchComplete() const { return search_complete_.load(); }

void SearchWorker::DiscardResults() {
  const std::scoped_lock lock(mutex_);
  results_data_.clear();
  results_data_.shrink_to_fit();
  has_new_results_ = false;
}

void SearchWorker::CancelSearch() {
  cancel_current_search_.store(true);
}

// Helper function to collect search batch futures into task batches.
// Stage 2 arena: batches flow through to the UI-thread pool copy with no
// intermediate owned-string conversion anywhere in the pipeline.
// Extracted to reduce nesting depth in WorkerThread()
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - Static helper; function name follows project convention
static void ProcessSearchFutures(std::vector<std::future<SearchResultBatch>>& search_futures,
                                  std::vector<SearchResultBatch>& all_batches,
                                  const std::atomic<bool>& cancel_current_search,
                                  const MetricsTimer& search_timer,
                                  uint64_t& search_time_ms,
                                  size_t& total_candidates) {
  if (search_futures.empty()) {
    total_candidates = 0;
    return;
  }

  // Collect batches by move only (no string work on any thread). Cancellation
  // keeps batches collected so far, matching previous merge behavior.
  const size_t futures_count = search_futures.size();
  all_batches.reserve(futures_count);
  size_t total_hits = 0;
  for (size_t future_idx = 0; future_idx < futures_count; ++future_idx) {
    // Check cancellation periodically (check cancel flag directly for faster response)
    if (future_idx % 2 == 0 && cancel_current_search.load()) {
      LOG_INFO_BUILD("Search cancelled while waiting for futures, breaking early");
      // Drain the remaining futures before returning: pool tasks capture the
      // index by reference, so abandoning a future (destruction does not wait)
      // lets a still-running task read the index after this call's owner may
      // destroy it (ASan heap-use-after-free in ProcessChunkRange). The tasks
      // poll the same cancel flag, so get() returns promptly. Batches are
      // discarded on cancel anyway (ExecuteSearch clears them if a new search
      // was requested).
      for (size_t drain_idx = future_idx; drain_idx < futures_count; ++drain_idx) {
        (void)search_futures[drain_idx].get();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - drain_idx bounded by futures_count
      }
      break;
    }

    all_batches.push_back(search_futures[future_idx].get());  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - future_idx bounded by futures_count = search_futures.size()
    total_hits += all_batches.back().hits.size();
  }

  // Duration of the collection phase (full pass, or early cancel break). The
  // previous per-iteration "last future" branch left this at 0 whenever
  // cancellation broke the loop, silently under-reporting search metrics.
  search_time_ms = search_timer.ElapsedMs();
  total_candidates = total_hits;
}

static void LogLoadBalanceIfNeeded(const std::vector<ThreadTiming>& thread_timings) {
  if (thread_timings.empty()) {
    return;
  }

  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index 0 is safe: thread_timings.empty() checked above
  uint64_t min_time = thread_timings[0].elapsed_ms_;
  uint64_t max_time = thread_timings[0].elapsed_ms_;
  uint64_t total_time = 0;
  size_t min_bytes = thread_timings[0].bytes_processed_;
  size_t max_bytes = thread_timings[0].bytes_processed_;
  // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  size_t total_bytes = 0;

  LogPerThreadTiming(thread_timings, min_time, max_time, total_time,
                     min_bytes, max_bytes, total_bytes);

  if (max_time > 0 && min_time > 0) {
    const double imbalance_ratio =
        static_cast<double>(max_time) / static_cast<double>(min_time);
    const uint64_t avg_time = total_time / thread_timings.size();

    // Calculate byte imbalance ratio
    const double byte_imbalance_ratio =
        (min_bytes > 0) ? static_cast<double>(max_bytes) /
                              static_cast<double>(min_bytes)
                        : 1.0;
    const size_t avg_bytes = total_bytes / thread_timings.size();

    LogLoadBalanceAnalysis(
        TimeStats{min_time, max_time, avg_time},
        ByteStats{min_bytes, max_bytes, avg_bytes},
        thread_timings,
        imbalance_ratio,
        byte_imbalance_ratio);
  }
}

// Shared metrics update for both PATH 1 and PATH 2 (reduces duplication and complexity)
static void UpdateMetricsAfterSearch(SearchMetrics& metrics,
                                    uint64_t search_time_ms,
                                    uint64_t post_process_time_ms,
                                    size_t results_count) {
  metrics.total_searches_.fetch_add(1);
  metrics.total_results_found_.fetch_add(results_count);
  metrics.total_search_time_ms_.fetch_add(search_time_ms);
  metrics.total_postprocess_time_ms_.fetch_add(post_process_time_ms);
  UpdateAtomicMax(metrics.max_search_time_ms_, search_time_ms);
  UpdateAtomicMax(metrics.max_postprocess_time_ms_, post_process_time_ms);
  UpdateAtomicMax(metrics.max_results_count_, results_count);
  metrics.last_search_time_ms_.store(search_time_ms);
  metrics.last_postprocess_time_ms_.store(post_process_time_ms);
  metrics.last_results_count_.store(results_count);
}

// NOLINTNEXTLINE(readability-make-member-function-const) - called from code that mutates metrics_
size_t SearchWorker::RunFilteredSearchPath(
    const SearchParams& params,
    const std::vector<std::string>& extensions,
    std::string_view filename_search,
    std::string_view path_search,
    std::vector<SearchResultBatch>& results) {
  CTRACK_NAME("SearchWorker::RunFilteredSearchPath");  // NOLINT(misc-const-correctness)
  const ScopedTimer timer("SearchWorker - Parallel Search (Primary)");
  SearchStats stats;  // NOLINT(misc-const-correctness) - modified by SearchAsyncWithData via pointer
  const MetricsTimer search_timer;
  std::vector<ThreadTiming> thread_timings;
  std::vector<std::future<SearchResultBatch>>
      search_futures = index_search_.SearchAsyncWithData(
          filename_search, -1, &stats, path_search,
          extensions.empty() ? nullptr : &extensions, params.foldersOnly,
          params.caseSensitive, &thread_timings,
          &cancel_current_search_, current_search_settings_);

  uint64_t search_time_ms = 0;  // NOLINT(misc-const-correctness) - set by ProcessSearchFutures
  const MetricsTimer post_process_timer;

  size_t total_candidates = 0;  // NOLINT(misc-const-correctness) - set by ProcessSearchFutures
  ProcessSearchFutures(search_futures, results, cancel_current_search_,
                       search_timer, search_time_ms, total_candidates);
  LogLoadBalanceIfNeeded(thread_timings);
  // Log BEFORE the possible discard below: report what was actually collected,
  // not the post-clear state. (The old line printed total_candidates twice as
  // if it were both a candidate and a result count.)
  LOG_INFO_BUILD("Incremental search collected " << results.size()
                                                 << " task batches, "
                                                 << total_candidates << " hits");
  {
    const std::scoped_lock lock(mutex_);
    if (search_requested_) {
      results.clear();
    }
  }

  const uint64_t post_process_time_ms = post_process_timer.ElapsedMs();
  UpdateMetricsAfterSearch(metrics_, search_time_ms, post_process_time_ms, total_candidates);
  return total_candidates;
}

void SearchWorker::WorkerThread() {
  SetThreadName("Search-Worker");

  while (true) {
    try {
      SearchParams params;
      if (!WaitForSearchRequest(params)) {
        break;
      }
      ExecuteSearch(params);
    } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) NOLINT(bugprone-empty-catch) - log and continue; cannot rethrow from worker thread
      exception_handling::LogException("SearchWorker", "background search", e);
      {
        const std::scoped_lock lock(mutex_);
        is_searching_.store(false);
        search_complete_.store(true);
      }
    } catch (...) {  // NOSONAR(cpp:S2738) NOLINT(bugprone-empty-catch) - log and continue; cannot rethrow from worker thread
      exception_handling::LogUnknownException("SearchWorker", "background search");
      {
        const std::scoped_lock lock(mutex_);
        is_searching_.store(false);
        search_complete_.store(true);
      }
    }
  }
}

bool SearchWorker::WaitForSearchRequest(SearchParams& params) {
  std::unique_lock lock(mutex_);
  cv_.wait(lock, [this] { return search_requested_ || !thread_running_; });

  if (!thread_running_) {
    return false;
  }

  params = next_params_;
  current_search_settings_ = next_search_settings_;
  next_search_settings_ = nullptr;
  search_requested_ = false;
  is_searching_.store(true);
  search_complete_.store(false);
  cancel_current_search_.store(false);
  return true;
}

void SearchWorker::ExecuteSearch(const SearchParams& params) {
  CTRACK_NAME("SearchWorker::ExecuteSearch");  // NOLINT(misc-const-correctness)
  LOG_INFO_BUILD("=== Starting background search ===");
  LOG_INFO_BUILD("Extension input (raw): '" << params.extensionInput << "'");
  LOG_INFO_BUILD("Filename input (raw): '" << params.filenameInput << "'");
  LOG_INFO_BUILD("Path input (raw): '" << params.pathInput << "'");
  LOG_INFO_BUILD("Case sensitive: " << (params.caseSensitive ? "true" : "false"));
  LOG_INFO_BUILD("Folders only: " << (params.foldersOnly ? "true" : "false"));

  const std::vector<std::string> extensions = ParseExtensions(params.extensionInput);
  LOG_INFO_BUILD("Parsed extensions count: " << extensions.size());

  const std::string filename_search = Trim(params.filenameInput);
  const std::string path_search = Trim(params.pathInput);

  std::vector<SearchResultBatch> results;
  const size_t total_hits =
      RunFilteredSearchPath(params, extensions, filename_search, path_search, results);

  {
    const std::scoped_lock lock(mutex_);
    if (!search_requested_) {
      results_data_ = std::move(results);
      has_new_results_ = true;
      is_searching_.store(false);
      search_complete_.store(true);
      LOG_INFO_BUILD("Background search completed - Found " << total_hits << " results");
    }
  }
}
