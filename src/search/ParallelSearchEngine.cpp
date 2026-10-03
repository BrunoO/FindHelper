#include "search/ParallelSearchEngine.h"

#include "utils/CTrackWrapper.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <memory>
#include <string_view>
#include <thread>
#include <utility>

#include "index/ISearchableIndex.h"
#include "path/PathStorage.h"
#include "search/FilenameTrigramIndex.h"
#include "search/TrigramQueryPlanner.h"
#include "search/SearchContext.h"
#include "search/SearchPatternUtils.h"
#include "search/SearchStatisticsCollector.h"
#include "search/SearchThreadPool.h"
#include "utils/CpuFeatures.h"
#include "utils/HashMapAliases.h"
#include "utils/LoadBalancingStrategy.h"
#include "utils/Logger.h"
#include "utils/StringSearch.h"
#include "utils/StringUtils.h"
#include "utils/ThreadUtils.h"

namespace parallel_search_detail {

inline bool CheckMatchers(const char* path, [[maybe_unused]] size_t path_len, size_t filename_offset,
                          const search_pattern_utils::PatternMatcher& filename_matcher,
                          const search_pattern_utils::PatternMatcher& path_matcher) {
  if (filename_matcher) {
    assert(filename_offset <= path_len && "filename_start must not exceed path length");
    const std::string_view filename(path + filename_offset, path_len - filename_offset);
    if (!filename_matcher(filename)) {
      return false;
    }
  }
  if (path_matcher) {
    const std::string_view full_path(path, path_len);
    if (!path_matcher(full_path)) {
      return false;
    }
  }
  return true;
}

inline bool EvaluateTrigramCandidate(uint32_t i, const PathStorage::SoAView& soaView, size_t storage_size,
                                     ItemTypeFilter item_type_filter, bool has_extension_filter,
                                     const SearchContext& context,
                                     const ParallelSearchEngine::PatternMatchers& matchers) {
  if (i >= soaView.size || soaView.is_deleted[i] != 0) {
    return false;
  }
  if (item_type_filter == ItemTypeFilter::FoldersOnly && soaView.is_directory[i] == 0) {
    return false;
  }
  if (item_type_filter == ItemTypeFilter::FilesOnly && soaView.is_directory[i] != 0) {
    return false;
  }

  const size_t path_len = parallel_search_detail::GetPathLength(soaView, i, storage_size);
  if (!parallel_search_detail::MatchesExtensionFilter(soaView, i, path_len, has_extension_filter, context)) {
    return false;
  }

  if (!context.extension_only_mode) {
    const char* path = soaView.path_storage + soaView.path_offsets[i];
    if (const size_t filename_offset = soaView.filename_start[i];
        !CheckMatchers(path, path_len, filename_offset, matchers.filename_matcher, matchers.path_matcher)) {
      return false;
    }
  }

  return true;
}

struct TrigramCandidateQuery {
  std::optional<roaring::Roaring> candidates;
  uint64_t cardinality = 0;
  bool query_attempted = false;
};

bool ShouldUseTrigramFilter(uint64_t cardinality, size_t total_items,
                             int estimated_thread_count) noexcept {
  constexpr double kFilterUsPerCandidate = 0.9;
  constexpr double kScanUsPerRow = 1.3;
  if (constexpr uint64_t kMaxFilterCardinality = 250000;  // ~16MB arena + hits
      cardinality > kMaxFilterCardinality) {
    return false;
  }
  const int threads = estimated_thread_count > 0 ? estimated_thread_count : 1;
  return static_cast<double>(cardinality) * kFilterUsPerCandidate <
         static_cast<double>(total_items) * kScanUsPerRow / static_cast<double>(threads);
}

inline TrigramCandidateQuery QueryTrigramCandidates(const ISearchableIndex& index, const SearchContext& context) {
  TrigramCandidateQuery result;

  auto trigrams = filename_trigram::TrigramQueryPlanner::Plan(context);
  if (!trigrams) {
    return result;
  }

  {
    const std::shared_lock lock(index.GetMutex());
    const filename_trigram::FilenameTrigramIndex* trigram_idx = index.GetTrigramIndex();
    if (trigram_idx == nullptr) {
      return result;
    }
    result.query_attempted = true;
    result.candidates = trigram_idx->QueryCandidates(*trigrams);
  }

  if (result.candidates.has_value()) {
    result.cardinality = result.candidates->cardinality();
  }

  return result;
}

template <typename Action>
inline void FilterTrigramCandidates(const ISearchableIndex& index, const roaring::Roaring& candidates,
                                     const SearchContext& context,
                                     const ParallelSearchEngine::PatternMatchers& matchers,
                                     Action&& action) {
  const std::shared_lock lock(index.GetMutex());
  const PathStorage::SoAView soaView = index.GetSearchableView();
  const size_t storage_size = index.GetStorageSize();
  const bool has_cancel_flag = (context.cancel_flag != nullptr);
  const bool has_extension_filter = context.HasExtensionFilter();
  const ItemTypeFilter item_type_filter = context.GetEffectiveItemTypeFilter();

  for (auto it = candidates.begin(); it != candidates.end(); ++it) {
    uint32_t i = *it;
    if (has_cancel_flag && context.cancel_flag->load()) {
      break;
    }
    if (!EvaluateTrigramCandidate(i, soaView, storage_size, item_type_filter,
                                  has_extension_filter, context, matchers)) {
      continue;
    }

    action(soaView, i, storage_size);
  }
}

} // namespace parallel_search_detail

// Constructor - thread_pool_ initialized in initializer list
// NOLINTNEXTLINE(readability-identifier-naming,cppcoreguidelines-pro-type-member-init,hicpp-member-init) - threadPool matches API; member-init: thread_pool_ set below
ParallelSearchEngine::ParallelSearchEngine(std::shared_ptr<SearchThreadPool> threadPool)
    : thread_pool_(std::move(threadPool)) {
  if (!thread_pool_) {
    LOG_ERROR_BUILD("ParallelSearchEngine: thread pool is null");
  }
}

std::vector<std::future<SearchResultBatch>>
ParallelSearchEngine::SearchAsyncWithData(const ISearchableIndex& index,
                                          [[maybe_unused]] std::string_view query,
                                          int thread_count,
                                          const SearchContext& context,
                                          std::vector<ThreadTiming>* thread_timings,
                                          const std::atomic<bool>* cancel_flag) const {
  CTRACK;  // NOLINT(misc-const-correctness) - macro creates a non-const handler object
  // Snapshot sizes under a short-lived shared_lock, then release before enqueue.
  // Do NOT extend this lock across workers (e.g. via shared_ptr<shared_lock>): on
  // Windows SRWLOCK a waiting unique_lock (USN Insert/Remove) blocks new shared
  // locks, so a search-lifetime shared lock + per-worker shared locks deadlocks
  // and leaves Status: Searching... stuck. Maintain()/RebuildPathBuffer is gated
  // on !search_worker.IsBusy(); workers clamp chunk bounds to the live SoA size.
  size_t total_items = 0;
  size_t total_bytes = 0;
  {
    const std::shared_lock lock(index.GetMutex());
    total_items = index.GetTotalItems();
    total_bytes = index.GetStorageSize();
  }

  std::vector<std::future<SearchResultBatch>> futures;
  if (total_items == 0) {
    return futures;
  }

  // The cancel_flag parameter duplicates context.cancel_flag: FileIndex builds
  // the context from the same flag, so they usually alias. Honor both — early
  // out when either is already set, and backfill an effective context when only
  // the parameter was supplied — so a caller-passed flag is never silently dead.
  if (cancel_flag != nullptr && cancel_flag->load(std::memory_order_acquire)) {
    return futures;
  }
  SearchContext effective_context = context;
  if (effective_context.cancel_flag == nullptr) {
    effective_context.cancel_flag = cancel_flag;
  }

  // Trigram candidate filtering for SearchAsyncWithData.
  // The filter path is single-threaded; take it only while it is cheaper than
  // a parallel scan (see ShouldUseTrigramFilter for the cost model).
  const auto tq = parallel_search_detail::QueryTrigramCandidates(index, effective_context);
  int scan_threads = DetermineThreadCount(thread_count, total_bytes,
                                          effective_context.search_thread_pool_size);
  if (scan_threads < 1) {
    scan_threads = 1;
  }
  if (static_cast<size_t>(scan_threads) > total_items) {
    // scan_threads <= INT_MAX here, so total_items < INT_MAX: narrowing is safe.
    scan_threads = static_cast<int>(total_items);
  }
  if (tq.query_attempted &&
      parallel_search_detail::ShouldUseTrigramFilter(tq.cardinality, total_items, scan_threads)) {
    if (!tq.candidates.has_value()) {
      return futures;
    }

    std::promise<SearchResultBatch> promise;
    SearchResultBatch batch;
    const auto max_hits = static_cast<size_t>(tq.cardinality);
    batch.hits.reserve(max_hits);
    // Pre-reserve arena assuming average path length of ~64 bytes to eliminate reallocations
    batch.arena.reserve(max_hits * 64);

    const auto matchers = CreatePatternMatchers(effective_context);
    parallel_search_detail::FilterTrigramCandidates(
        index, *tq.candidates, effective_context, matchers,
        [&batch](const PathStorage::SoAView& soaView, uint32_t i, size_t storage_size) {
          const size_t path_len = parallel_search_detail::GetPathLength(soaView, i, storage_size);
          const char* path = soaView.path_storage + soaView.path_offsets[i];
          const size_t filename_offset = soaView.filename_start[i];
          const size_t extension_offset = soaView.extension_start[i];

          batch.AppendHit(soaView.path_ids[i], path, path_len, soaView.is_directory[i] != 0,
                          filename_offset, extension_offset);
        });

    if (thread_timings != nullptr) {
      thread_timings->clear();
      thread_timings->push_back(
          ThreadTiming{0, 0, tq.cardinality, 0, tq.cardinality, batch.hits.size(), 0, 0, tq.cardinality});
    }

    promise.set_value(std::move(batch));
    futures.push_back(promise.get_future());
    return futures;
  }

  // Determine optimal thread count
  thread_count = DetermineThreadCount(thread_count, total_bytes,
                                       effective_context.search_thread_pool_size);
  thread_count = (std::min)(thread_count, static_cast<int>(total_items));  // NOSONAR(cpp:S1905) - Required cast: std::min needs same types, convert size_t to int
  thread_count = (std::max)(thread_count, 1);

  // Prepare thread timings vector if requested
  // Pre-size to thread_count so threads can write to their index safely
  if (thread_timings != nullptr) {
    thread_timings->clear();
    thread_timings->resize(thread_count); // Pre-size for safe indexing
  }

  // Verify thread pool is ready before launching tasks
  if (!thread_pool_) {
    LOG_ERROR_BUILD("SearchAsyncWithData: Thread pool is null - cannot execute search");
    return futures;
  }

  // Check thread pool is ready (GetThreadCount() is const, so pool can be const reference)
  const SearchThreadPool& pool = *thread_pool_;
  if (pool.GetThreadCount() == 0) {  // NOSONAR(cpp:S6004) - Variable used after if block (line 235 and later)
    LOG_ERROR_BUILD("SearchAsyncWithData: Thread pool has 0 threads - cannot execute search");
    return futures;
  }

  // HybridStrategy is the sole strategy; stack-allocated (no heap allocation needed).
  // ParallelSearchEngine implements ISearchExecutor, so *this can be passed.
  const HybridStrategy strategy;
  futures = strategy.LaunchSearchTasks(
      index, *this, total_items, thread_count, effective_context, thread_timings);

  // Verify futures were created
  if (futures.empty() && thread_count > 0 && total_items > 0) {
    LOG_ERROR_BUILD(
        "SearchAsyncWithData: LaunchSearchTasks returned empty futures vector "
        << "(thread_count=" << thread_count << ", total_items=" << total_items
        << ")");
  }

  // NOTE: Each worker thread acquires its own shared_lock before calling
  // ProcessChunkRange. This ensures:
  // 1. Each worker thread holds a lock while reading arrays
  // 2. Insert/Remove operations (which require unique_lock) will block until
  //    all worker threads release their shared_locks
  // 3. This prevents race conditions where arrays could be modified while
  //    workers are still reading them

  return futures;
}

// Get thread pool
SearchThreadPool& ParallelSearchEngine::GetThreadPool() const {
  if (!thread_pool_) {
    LOG_ERROR_BUILD("ParallelSearchEngine::GetThreadPool: thread_pool_ is null");
    throw std::runtime_error("ParallelSearchEngine: thread pool is null");  // NOSONAR(cpp:S112) - std::runtime_error is appropriate for internal state validation errors
  }
  return *thread_pool_;
}

// Calculate bytes processed for a chunk range
// Implementation moved to SearchStatisticsCollector (now inline in header)

// Record thread timing information
// Implementation moved to SearchStatisticsCollector (now inline in header)

// Helper to determine optimal thread count
int ParallelSearchEngine::DetermineThreadCount(int thread_count, size_t total_bytes,
                                               int search_thread_pool_size_from_context) {
  if (thread_count <= 0) {
    if (search_thread_pool_size_from_context > 0) {
      thread_count = search_thread_pool_size_from_context;
    } else {
      // Search is memory-bandwidth-bound: HT siblings share L1/L2 and the memory
      // bus, so physical cores are the meaningful unit. Fall back to logical count
      // when physical-core detection is unavailable (e.g. macOS, Linux cgroups).
      const auto [physical, logical] = cpu_features::GetCoreCounts();
      thread_count = (physical > 0) ? static_cast<int>(physical)
                                    : static_cast<int>(GetLogicalProcessorCount());

      // Cap by data size only during auto-detection: ~8 MB per thread saturates
      // per-core memory bandwidth without wasting threads on small indexes.
      // Explicit thread counts (user setting or test fixture) are respected as-is.
      constexpr size_t kBytesPerThread = static_cast<size_t>(8) * 1024U * 1024U;
      if (const auto max_by_bytes = static_cast<int>((total_bytes + kBytesPerThread - 1) / kBytesPerThread);
          max_by_bytes > 0 && thread_count > max_by_bytes) {
        thread_count = max_by_bytes;
      }
    }
  }

  return thread_count;
}

// Create pattern matchers from search context
ParallelSearchEngine::PatternMatchers
ParallelSearchEngine::CreatePatternMatchers(const SearchContext& context) {
  PatternMatchers matchers;

  // Set up filename matcher if not in extension-only mode
  if (!context.extension_only_mode) {
    if (context.HasFilenameQuery()) {
      // Use pre-compiled pattern if available (faster), otherwise compile on-the-fly
      if (context.filename_pattern) {
        matchers.filename_matcher = search_pattern_utils::CreateFilenameMatcher(
            context.filename_pattern);
      } else {
        matchers.filename_matcher = search_pattern_utils::CreateFilenameMatcher(
            context.filename_query, context.case_sensitive, context.filename_query_lower);
      }
    }
    // Set up path matcher if path query is specified
    if (context.HasPathQuery()) {
      // Use pre-compiled pattern if available (faster), otherwise compile on-the-fly
      if (context.path_pattern) {
        matchers.path_matcher = search_pattern_utils::CreatePathMatcher(
            context.path_pattern);
      } else {
        matchers.path_matcher = search_pattern_utils::CreatePathMatcher(
            context.path_query, context.case_sensitive, context.path_query_lower);
      }
    }
  }

  return matchers;
}

// ============================================================================

