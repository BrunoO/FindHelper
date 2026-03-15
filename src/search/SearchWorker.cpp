#include "search/SearchWorker.h"
#include "search/StreamingResultsCollector.h"

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
  T current = atomic_var.load(std::memory_order_acquire);
  while (new_value > current &&
         !atomic_var.compare_exchange_weak(
             current, new_value,
             std::memory_order_release,
             std::memory_order_acquire)) {
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

  if (bytes < k_bytes_per_kb) {
    return std::to_string(bytes) + " B";
  }
  if (bytes < k_bytes_per_mb) {
    return std::to_string(bytes / k_bytes_per_kb) + " KB";
  }
  return std::to_string(bytes / k_bytes_per_mb) + " MB";
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
        dynamic_info = ", " +
                       std::to_string(timing.dynamic_chunks_processed_) +
                       " dynamic chunks, " +
                       std::to_string(timing.total_items_processed_) +
                       " total items";
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

SearchWorker::SearchWorker(FileIndex &file_index) : file_index_(file_index) {
  thread_ = std::thread(&SearchWorker::WorkerThread, this);
}

SearchWorker::~SearchWorker() {
  {
    const std::scoped_lock lock(mutex_);
    running_ = false;
    search_requested_ = false;
  }
  cv_.notify_one();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void SearchWorker::StartSearch(const SearchParams &params,
                               const AppSettings *optional_settings) {
  {
    const std::scoped_lock lock(mutex_);
    // If a search is already running, cancel it
    if (running_) {
      cancel_current_search_.store(true, std::memory_order_release);
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

std::vector<SearchResultData> SearchWorker::GetResultsData() {
  const std::scoped_lock lock(mutex_);
  has_new_results_ = false;
  return std::move(results_data_);
}

bool SearchWorker::IsSearching() const {
  // This is a lock-free check for UI status.
  return is_searching_.load(std::memory_order_relaxed);
}

bool SearchWorker::IsBusy() const {
  const std::scoped_lock lock(mutex_);
  return is_searching_.load(std::memory_order_relaxed) || search_requested_;
}

bool SearchWorker::IsSearchComplete() const { return search_complete_.load(); }

StreamingResultsCollector* SearchWorker::GetStreamingCollector() {
  const std::scoped_lock lock(mutex_);
  if (collector_ != nullptr && !collector_->IsSearchComplete()) {
    assert(is_searching_.load(std::memory_order_relaxed));
  }
  return collector_.get();
}

void SearchWorker::DiscardResults() {
  const std::scoped_lock lock(mutex_);
  collector_.reset();
  results_data_.clear();
  has_new_results_ = false;
}

void SearchWorker::CancelSearch() {
  cancel_current_search_.store(true, std::memory_order_release);
}

std::vector<SearchResult> MergeAndConvertToSearchResults(  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - free function, PascalCase API
    std::vector<char>& pool,
    const std::vector<SearchResultData>& data,
    const FileIndex& file_index) {
  std::vector<SearchResult> results;
  results.reserve(data.size());

  // Reserve pool space to avoid repeated reallocations (each path + null terminator)
  size_t pool_bytes_needed = 0;
  for (const SearchResultData& datum : data) {
    pool_bytes_needed += datum.fullPath.size() + 1;
  }
  pool.reserve(pool.size() + pool_bytes_needed);

  size_t pool_offset = pool.size();
  for (const SearchResultData& datum : data) {
    const std::string_view path_view = datum.fullPath;
    pool.insert(pool.end(), path_view.begin(), path_view.end());
    pool.push_back('\0');
    const size_t path_len = path_view.size();
    const char* path_start = pool.data() + pool_offset;
    pool_offset += path_len + 1;

    SearchResult result;
    result.fileId = datum.id;
    result.isDirectory = datum.isDirectory;
    result.fullPath = std::string_view(path_start, path_len);
    result.filename_offset = datum.filename_start;
    result.extension_offset =
        (datum.extension_start == SIZE_MAX) ? std::string_view::npos : datum.extension_start;

    if (datum.isDirectory) {
      result.fileSize = 0;
      result.lastModificationTime = FILETIME{0, 0};
    } else {
      const FileEntry* entry = file_index.GetEntry(datum.id);
      if (entry == nullptr) {
        result.fileSize = kFileSizeNotLoaded;
        result.lastModificationTime = kFileTimeNotLoaded;
      } else {
        result.fileSize = entry->fileSize.IsNotLoaded()
                              ? kFileSizeNotLoaded
                              : entry->fileSize.GetValue();
        result.lastModificationTime = entry->lastModificationTime.IsNotLoaded()
                                         ? kFileTimeNotLoaded
                                         : entry->lastModificationTime.GetValue();
      }
    }
    result.fileSizeDisplay = "";
    result.lastModificationDisplay = "";
    results.push_back(std::move(result));
  }
  return results;
}

// Helper function to process search futures and accumulate results
// Extracted to reduce nesting depth in WorkerThread()
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - Static helper; function name follows project convention
static void ProcessSearchFutures(std::vector<std::future<std::vector<SearchResultData>>>& search_futures,
                                  std::vector<SearchResultData>& all_search_data,
                                  const std::atomic<bool>& cancel_current_search,
                                  const MetricsTimer& search_timer,
                                  uint64_t& search_time_ms,
                                  size_t& total_candidates) {
  if (search_futures.empty()) {
    total_candidates = 0;
    return;
  }

  // Conservative estimate: assume each thread returns ~1000 results
  // This avoids expensive size queries while preventing most reallocations
  constexpr size_t k_estimated_results_per_thread = 1000;
  const size_t estimated_total = search_futures.size() * k_estimated_results_per_thread;
  all_search_data.reserve(estimated_total);

  const size_t futures_count = search_futures.size();
  for (size_t future_idx = 0; future_idx < futures_count; ++future_idx) {
    // Check cancellation periodically (check cancel flag directly for faster response)
    if (future_idx % 2 == 0 && cancel_current_search.load(std::memory_order_acquire)) {
      LOG_INFO_BUILD("Search cancelled while waiting for futures, breaking early");
      break;
    }

    std::vector<SearchResultData> chunk_data = search_futures[future_idx].get();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - future_idx bounded by futures_count = search_futures.size()

    // Reserve additional capacity if estimate was too low
    if (all_search_data.size() + chunk_data.size() > all_search_data.capacity()) {
      all_search_data.reserve(all_search_data.size() + chunk_data.size());
    }
    all_search_data.insert(all_search_data.end(),
                         std::make_move_iterator(chunk_data.begin()),
                         std::make_move_iterator(chunk_data.end()));

    // Capture search time from the last future (approximate total search time)
    if (future_idx == futures_count - 1) {
      search_time_ms = search_timer.ElapsedMs();
    }
  }

  total_candidates = all_search_data.size();
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

  if (max_time > 0) {
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
        {min_time, max_time, avg_time},
        {min_bytes, max_bytes, avg_bytes},
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
  metrics.total_searches_.fetch_add(1, std::memory_order_relaxed);
  metrics.total_results_found_.fetch_add(results_count, std::memory_order_relaxed);
  metrics.total_search_time_ms_.fetch_add(search_time_ms, std::memory_order_relaxed);
  metrics.total_postprocess_time_ms_.fetch_add(post_process_time_ms,
                                              std::memory_order_relaxed);
  UpdateAtomicMax(metrics.max_search_time_ms_, search_time_ms);
  UpdateAtomicMax(metrics.max_postprocess_time_ms_, post_process_time_ms);
  UpdateAtomicMax(metrics.max_results_count_, results_count);
  metrics.last_search_time_ms_.store(search_time_ms, std::memory_order_release);
  metrics.last_postprocess_time_ms_.store(post_process_time_ms,
                                         std::memory_order_release);
  metrics.last_results_count_.store(results_count, std::memory_order_release);
}

// Helper to process one ready future (reduces complexity/nesting in ProcessStreamingSearchFutures; S3776, S134).
// Returns true if the future at index was ready and was processed (get + add to collector or exception handled).
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - Static helper; function name follows project convention
static bool ProcessOneReadyFutureIfReady(
    std::vector<std::future<std::vector<SearchResultData>>>& search_futures,
    size_t index,
    StreamingResultsCollector& collector,
    std::atomic<bool>& cancel_flag) {
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index is caller-supplied from pending_indices, which only contains valid indices into search_futures
  if (search_futures[index].wait_for(std::chrono::milliseconds(0)) !=
      std::future_status::ready) {
    return false;
  }
  exception_handling::RunRecoverable(
      "Search future",
      [&search_futures, index, &collector]() {
        const std::vector<SearchResultData> chunk_data = search_futures[index].get();
        collector.AddResults(chunk_data);
      },
      [&collector, &cancel_flag](std::string_view msg) {
        // Set error first so consumers that see cancel_flag (acquire) will observe the error.
        collector.SetError(msg);
        cancel_flag.store(true, std::memory_order_release);
      });
  // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  return true;
}

// Bounded wait for a single future to become ready; logs if timeout. Extracted to satisfy cpp:S134.
static void WaitForFutureReadyOrLog(
    const std::future<std::vector<SearchResultData>>& fut,
    [[maybe_unused]] size_t idx) {  // Used in LOG_WARNING_BUILD (macro may hide use from clang-tidy)
  constexpr auto k_drain_poll = std::chrono::milliseconds(50);
  constexpr int k_max_drain_waits = 40;  // 2s total per future
  int waited = 0;
  for (; waited < k_max_drain_waits; ++waited) {
    if (fut.wait_for(k_drain_poll) == std::future_status::ready) {
      return;
    }
  }
  if (waited >= k_max_drain_waits) {
    LOG_WARNING_BUILD("SearchWorker drain: future " << idx
                      << " not ready after " << (k_max_drain_waits * 50)
                      << "ms, get() may block");
  }
}

// Runs on SearchWorker thread (not UI thread). Drains pending search futures.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - Static helper; function name follows project convention
static void DrainRemainingSearchFutures(
    std::vector<std::future<std::vector<SearchResultData>>>& search_futures,
    const std::vector<size_t>& pending_indices) {
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - pending_indices contains only valid indices into search_futures, built from range [0, search_futures.size())
  for (const size_t idx : pending_indices) {
    if (!search_futures[idx].valid()) {
      continue;
    }
    auto& fut = search_futures[idx];
    if (fut.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      WaitForFutureReadyOrLog(fut, idx);
    }
    exception_handling::DrainFuture(fut, "SearchWorker drain");
  }
  // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
}

void SearchWorker::ProcessStreamingSearchFutures(  // NOLINT(readability-identifier-naming) - method name follows project convention
    std::vector<std::future<std::vector<SearchResultData>>>& search_futures,
    StreamingResultsCollector& collector,
    const MetricsTimer& search_timer,
    uint64_t& search_time_ms) {
  // Process futures in completion order (first finished first) for faster
  // time-to-first-result. Controller accumulates batches for finalization.
  std::vector<size_t> pending_indices;
  pending_indices.reserve(search_futures.size());
  for (size_t i = 0; i < search_futures.size(); ++i) {
    pending_indices.push_back(i);
  }

  while (!pending_indices.empty()) {
    if (cancel_current_search_.load(std::memory_order_acquire)) {
      break;
    }

    bool found_ready = false;
    for (auto it = pending_indices.begin(); it != pending_indices.end(); ) {
      if (ProcessOneReadyFutureIfReady(search_futures, *it, collector, cancel_current_search_)) {
        it = pending_indices.erase(it);
        found_ready = true;
        break;  // re-check all from top
      }
      ++it;
    }

    if (!found_ready && !pending_indices.empty()) {
      constexpr auto k_poll_interval = std::chrono::milliseconds(5);
      // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - both accesses guarded by !pending_indices.empty(); pending_indices[0] is a valid index into search_futures
      search_futures[pending_indices[0]].wait_for(k_poll_interval);
      // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    }
  }

  DrainRemainingSearchFutures(search_futures, pending_indices);

  collector.MarkSearchComplete();
  // Invariant: collector is always marked complete on exit (normal, cancel, or error)
  assert(collector.IsSearchComplete() &&
         "ProcessStreamingSearchFutures must call MarkSearchComplete before return");
  search_time_ms = search_timer.ElapsedMs();
}

// NOLINTNEXTLINE(readability-make-member-function-const) - called from code that mutates metrics_/collector_
void SearchWorker::RunFilteredSearchPath(
    const SearchParams& params,
    const std::vector<std::string>& extensions,
    std::string_view filename_search,
    std::string_view path_search,
    std::vector<SearchResultData>& results) {
  const ScopedTimer timer("SearchWorker - Parallel Search (Primary)");
  SearchStats stats;  // NOLINT(misc-const-correctness) - modified by SearchAsyncWithData via pointer
  const MetricsTimer search_timer;
  std::vector<ThreadTiming> thread_timings;
  std::vector<std::future<std::vector<SearchResultData>>>
      search_futures = file_index_.SearchAsyncWithData(
          filename_search, -1, &stats, path_search,
          extensions.empty() ? nullptr : &extensions, params.foldersOnly,
          params.caseSensitive, &thread_timings,
          &cancel_current_search_, current_search_settings_);

  uint64_t search_time_ms = 0;  // NOLINT(misc-const-correctness) - set by ProcessStreamingSearchFutures/ProcessSearchFutures
  const MetricsTimer post_process_timer;

  // Hold shared_ptr for duration so collector stays alive even if DiscardResults
  // or StartSearch resets collector_ while we're in ProcessStreamingSearchFutures
  // (e.g. when a future throws and we call SetError in the catch handler).
  // Only collector_ref is used below; collector_ is never re-read in this block.
  // Streaming path: controller consumes batches and accumulates; no need to fill results (unused).
  if (const std::shared_ptr<StreamingResultsCollector> collector_ref = collector_;  // NOSONAR(cpp:S6004) - Init-statement used; Sonar may attach to this line
      stream_partial_results_.load(std::memory_order_acquire) && collector_ref) {
    ProcessStreamingSearchFutures(search_futures, *collector_ref, search_timer, search_time_ms);
    LogLoadBalanceIfNeeded(thread_timings);
    results.clear();
  } else {
    size_t total_candidates = 0;  // NOLINT(misc-const-correctness) - set by ProcessSearchFutures
    ProcessSearchFutures(search_futures, results, cancel_current_search_,
                         search_timer, search_time_ms, total_candidates);
    LogLoadBalanceIfNeeded(thread_timings);
    {
      const std::scoped_lock lock(mutex_);
      if (search_requested_) {
        results.clear();
      }
    }
    LOG_INFO_BUILD("Incremental Post-Processing: Processed " << total_candidates
                                                             << " candidates into "
                                                             << results.size()
                                                             << " results");
  }

  const uint64_t post_process_time_ms = post_process_timer.ElapsedMs();
  UpdateMetricsAfterSearch(metrics_, search_time_ms, post_process_time_ms, results.size());
}

void SearchWorker::RunShowAllPath(const SearchParams& params,
                                  const std::vector<std::string>& extensions,
                                  std::string_view filename_search,
                                  std::string_view path_search,
                                  std::vector<SearchResultData>& results) {
  RunFilteredSearchPath(params, extensions, filename_search, path_search, results);
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
        is_searching_.store(false, std::memory_order_release);
        search_complete_.store(true, std::memory_order_release);
      }
    } catch (...) {  // NOSONAR(cpp:S2738) NOLINT(bugprone-empty-catch) - log and continue; cannot rethrow from worker thread
      exception_handling::LogUnknownException("SearchWorker", "background search");
      {
        const std::scoped_lock lock(mutex_);
        is_searching_.store(false, std::memory_order_release);
        search_complete_.store(true, std::memory_order_release);
      }
    }
  }
}

bool SearchWorker::WaitForSearchRequest(SearchParams& params) {
  std::unique_lock lock(mutex_);
  cv_.wait(lock, [this] { return search_requested_ || !running_; });

  if (!running_) {
    return false;
  }

  params = next_params_;
  current_search_settings_ = next_search_settings_;
  next_search_settings_ = nullptr;
  search_requested_ = false;
  is_searching_.store(true, std::memory_order_release);
  search_complete_.store(false, std::memory_order_release);
  cancel_current_search_.store(false, std::memory_order_release);

  // Create collector only when streaming is enabled (Settings: "Stream search results").
  // SetStreamPartialResults(settings.streamPartialResults) is called by SearchController before StartSearch().
  const bool stream_enabled = stream_partial_results_.load(std::memory_order_acquire);
  collector_ = stream_enabled ? std::make_shared<StreamingResultsCollector>() : nullptr;
  LOG_INFO_BUILD("Stream search results: " << (stream_enabled ? "enabled" : "disabled"));
  return true;
}

void SearchWorker::ExecuteSearch(const SearchParams& params) {
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

  std::vector<SearchResultData> results;
  const bool has_filename_query = !filename_search.empty();
  const bool has_path_query = !path_search.empty();
  const bool has_extension_filter = !extensions.empty();

  if (const bool has_any_filter = has_filename_query || has_path_query || has_extension_filter;  // NOLINT(bugprone-branch-clone) - then/else call different void functions
      has_any_filter) {
    RunFilteredSearchPath(params, extensions, filename_search, path_search, results);
  } else {
    RunShowAllPath(params, extensions, filename_search, path_search, results);
  }

  {
    const std::scoped_lock lock(mutex_);
    if (!search_requested_) {
      results_data_ = std::move(results);
      has_new_results_ = true;
      is_searching_.store(false, std::memory_order_release);
      search_complete_.store(true, std::memory_order_release);
      LOG_INFO_BUILD("Background search completed - Found " << results_data_.size() << " results");
    }
  }
}
