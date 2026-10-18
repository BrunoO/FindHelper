#include "search/ParallelSearchEngine.h"

#include "utils/CTrackWrapper.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "index/ISearchableIndex.h"
#include "path/PathStorage.h"
#include "search/ExtensionIndex.h"
#include "search/FilenameTrigramIndex.h"
#include "search/SearchContext.h"
#include "search/SearchPatternUtils.h"
#include "search/SearchStatisticsCollector.h"
#include "search/TrigramQueryPlanner.h"
#include "utils/CpuFeatures.h"
#include "utils/Logger.h"
#include "utils/StdRegexUtils.h"
#include "utils/ThreadPool.h"
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

// Bundled inputs for EvaluateCandidate: keeps the parameter count within
// the S107 limit of 7 as filter options grow.
struct CandidateVerifyContext {
  const PathStorage::SoAView& soaView;  // NOLINT(readability-identifier-naming) - soaView matches SoA convention used across ParallelSearchEngine
  size_t storage_size = 0;
  ItemTypeFilter item_type_filter = ItemTypeFilter::All;
  bool check_extension_filter = false;
  const SearchContext& context;
  const ParallelSearchEngine::PatternMatchers& matchers;
};

inline bool EvaluateCandidate(uint32_t i, const CandidateVerifyContext& ctx, size_t& out_path_len) {
  const PathStorage::SoAView& soa_view = ctx.soaView;
  const size_t storage_size = ctx.storage_size;
  const ItemTypeFilter item_type_filter = ctx.item_type_filter;
  const bool check_extension_filter = ctx.check_extension_filter;
  const SearchContext& context = ctx.context;
  const ParallelSearchEngine::PatternMatchers& matchers = ctx.matchers;
  if (i >= soa_view.size || soa_view.is_deleted[i] != 0) {
    return false;
  }
  if (item_type_filter == ItemTypeFilter::FoldersOnly && soa_view.is_directory[i] == 0) {
    return false;
  }
  if (item_type_filter == ItemTypeFilter::FilesOnly && soa_view.is_directory[i] != 0) {
    return false;
  }

  out_path_len = parallel_search_detail::GetPathLength(soa_view, i, storage_size);
  if (check_extension_filter &&
      !parallel_search_detail::MatchesExtensionFilter(soa_view, i, out_path_len, true, context)) {
    return false;
  }

  if (!context.extension_only_mode) {
    const char* path = soa_view.path_storage + soa_view.path_offsets[i];
    if (const size_t filename_offset = soa_view.filename_start[i];
        !CheckMatchers(path, out_path_len, filename_offset, matchers.filename_matcher, matchers.path_matcher)) {
      return false;
    }
  }

  if (context.ShouldFilterSystemFiles()) {
    const char* const path = soa_view.path_storage + soa_view.path_offsets[i];
    if (const size_t filename_offset = soa_view.filename_start[i];
        parallel_search_detail::IsSystemPath(path, filename_offset)) {
      return false;
    }
  }

  return true;
}

struct CandidateQuery {
  // Gate inputs only: the bitmaps themselves are re-queried under the
  // filter's lock scope (a copy made here could go stale before filtering).
  // Cardinality 0 with known_empty=false cannot happen (see
  // IntersectionCardinality / UnionCardinality).
  std::vector<filename_trigram::TrigramKey> required_trigrams;
  // Normalized extension terms: lowercase, dot-stripped, non-empty, unique
  // (see ExtensionIndex::NormalizeIndexableExtensions, the sole producer).
  std::vector<std::string> extensions;
  uint64_t cardinality = 0;
  bool query_attempted = false;
  bool known_empty = false;
  bool use_trigram = false;
  bool use_extension = false;
};

bool ShouldUseTrigramFilter(uint64_t cardinality, size_t total_items,
                             int estimated_thread_count) noexcept {
  if (constexpr uint64_t kMaxFilterCardinality = 250000;  // ~16MB arena + hits
      cardinality > kMaxFilterCardinality) {
    return false;
  }
  return PredictedFilterCostUs(cardinality) <
         PredictedScanCostUs(total_items, estimated_thread_count);
}

// Maps each required literal suffix to the file extension it ends at (the chars
// after the suffix's last '.'); nullopt when any suffix has no usable extension
// or the extension would contain a path separator.
[[nodiscard]] std::optional<std::vector<std::string>> ExtensionsFromSuffixes(
    const std::vector<std::string>& suffixes) {
  std::vector<std::string> extensions;
  extensions.reserve(suffixes.size());
  for (const std::string& suffix : suffixes) {
    const size_t dot = suffix.rfind('.');
    if (dot == std::string::npos || dot + 1 >= suffix.size()) {
      return std::nullopt;
    }
    const std::string_view extension = std::string_view(suffix).substr(dot + 1);
    if (extension.find_first_of("/\\") != std::string_view::npos) {
      return std::nullopt;
    }
    extensions.emplace_back(extension);
  }
  return extensions;
}

// Extensions provably required by an end-anchored `rs:` pattern that matches a
// file extension, e.g. rs:.*\.(cpp|h)$ -> {cpp, h}. Every match must end with
// one of the pattern's required literal suffixes
// (std_regex_utils::detail::ExtractRequiredSuffixes), so the file extension is a
// provable necessary condition. The result is only ever used as a candidate
// pre-filter, so a superset is safe (the filename/path matcher still filters
// candidates); nullopt is returned whenever that guarantee does not hold.
[[nodiscard]] std::optional<std::vector<std::string>> DeriveRegexExtensionSet(
    const SearchContext& context) {
  for (const std::string_view query : {context.filename_query, context.path_query}) {
    if (query.empty() ||
        search_pattern_utils::DetectPatternType(query) !=
            search_pattern_utils::PatternType::StdRegex) {
      continue;
    }
    const std::vector<std::string> suffixes =
        std_regex_utils::detail::ExtractRequiredSuffixes(
            search_pattern_utils::ExtractPatternView(query));
    if (suffixes.empty()) {
      continue;
    }
    if (auto extensions = ExtensionsFromSuffixes(suffixes);
        extensions.has_value() && !extensions->empty()) {
      return extensions;
    }
  }
  return std::nullopt;
}

// Resolves the extension candidate side (the user's extensions, else a
// regex-derived set) and records it on `result`. Safe to call while the index
// lock is held by the caller.
inline void ResolveExtensionCandidates(const ISearchableIndex& index,
                                       const SearchContext& context,
                                       const std::optional<std::vector<std::string>>& derived,
                                       CandidateQuery& result) {
  if (const extension_index::ExtensionIndex* ext_idx = index.GetExtensionIndex();
      ext_idx == nullptr || !ext_idx->IsReady()) {
    return;
  }
  const std::vector<std::string> extension_terms = derived.value_or(context.extensions);
  std::vector<std::string> normalized =
      extension_index::ExtensionIndex::NormalizeIndexableExtensions(extension_terms);
  if (normalized.empty()) {
    return;
  }
  result.use_extension = true;
  result.extensions = std::move(normalized);
}

inline CandidateQuery QueryCandidates(const ISearchableIndex& index, const SearchContext& context) {
  CandidateQuery result;

  auto trigrams = filename_trigram::TrigramQueryPlanner::Plan(context);
  const bool has_trigrams = trigrams.has_value() && !trigrams->empty();
  // An extension-shaped rs: pattern (rs:.*\.(cpp|h)$) carries a required
  // extension set the filename/path matcher enforces anyway; use it to restrict
  // candidates when the user did not set an extension filter.
  std::optional<std::vector<std::string>> derived_extensions;
  if (!context.HasExtensionFilter()) {
    derived_extensions = DeriveRegexExtensionSet(context);
  }
  const bool has_extensions = context.HasExtensionFilter() || derived_extensions.has_value();

  if (!has_trigrams && !has_extensions) {
    return result;
  }

  const std::shared_lock lock(index.GetMutex());
  const filename_trigram::FilenameTrigramIndex* trigram_idx = index.GetTrigramIndex();
  if (has_trigrams && trigram_idx != nullptr && trigram_idx->IsReady()) {
    result.use_trigram = true;
    result.required_trigrams = std::move(*trigrams);
  }

  if (has_extensions) {
    ResolveExtensionCandidates(index, context, derived_extensions, result);
  }

  if (!result.use_trigram && !result.use_extension) {
    return result;
  }

  result.query_attempted = true;
  // Combined candidates are the intersection of the used sides, so the
  // running min is a safe upper bound for the cost-model gate (and for
  // batch/arena reserve: it may over-reserve when the sides barely overlap —
  // safe, never under). A used side reporting 0 means the intersection is
  // empty. At least one side is used, so combined_card is always set below.
  std::optional<uint64_t> combined_card;
  if (result.use_trigram) {
    const auto card = trigram_idx->IntersectionCardinality(result.required_trigrams);
    if (!card.has_value()) {
      result.known_empty = true;
      return result;
    }
    combined_card = card;
  }
  if (result.use_extension) {
    const extension_index::ExtensionIndex* ext_idx = index.GetExtensionIndex();
    // result.extensions holds pre-normalized terms (see
    // NormalizeIndexableExtensions above): exactly one normalization per query.
    const auto card = ext_idx->UnionCardinalityNormalized(result.extensions);
    if (!card.has_value()) {
      result.known_empty = true;
      return result;
    }
    combined_card = combined_card.has_value() ? (std::min)(*combined_card, *card) : *card;
  }

  result.known_empty = !combined_card.has_value() || *combined_card == 0;
  if (!result.known_empty) {
    result.cardinality = *combined_card;
  }

  return result;
}

// Re-queries one candidate-filter side under the caller's lock scope.
// Sets fallback_requested when the index is missing or went non-ready
// (caller falls back to scan). Otherwise returns the side bitmap; nullopt
// or empty means definite empty at this linearization point.
template <typename IndexT, typename QueryT>
[[nodiscard]] inline std::optional<roaring::Roaring> RequeryIndexSide(
    const IndexT* idx, const QueryT& query, bool& fallback_requested) {
  if (idx == nullptr || !idx->IsReady()) {
    fallback_requested = true;
    return std::nullopt;
  }
  fallback_requested = false;
  return idx->QueryCandidates(query);
}

// Extension-side overload: cand_query.extensions holds pre-normalized terms
// (see NormalizeIndexableExtensions), so the Normalized variant avoids a
// second normalization per query. Exact match beats the template above.
[[nodiscard]] inline std::optional<roaring::Roaring> RequeryIndexSide(
    const extension_index::ExtensionIndex* idx, const std::vector<std::string>& query,
    bool& fallback_requested) {
  if (idx == nullptr || !idx->IsReady()) {
    fallback_requested = true;
    return std::nullopt;
  }
  fallback_requested = false;
  return idx->QueryCandidatesNormalized(query);
}

// Merges a side bitmap into the running intersection. Returns false when the
// intersection is empty (caller has nothing to do).
[[nodiscard]] inline bool IntersectSide(std::optional<roaring::Roaring>& combined,
                                       roaring::Roaring side) {
  if (!combined.has_value()) {
    combined = std::move(side);
    return true;
  }
  *combined &= side;
  return !combined->isEmpty();
}

// Builds the combined candidate bitmap when multiple index sides are queried,
// and unpacks candidate row IDs. Returns false if an index went non-ready
// mid-flight (signals scan fallback); populates candidate_ids (empty means
// definite empty at this linearization point).
[[nodiscard]] inline bool BuildCombinedCandidateIds(
    const ISearchableIndex& index,
    const CandidateQuery& cand_query,
    std::vector<uint32_t>& candidate_ids) {
  std::optional<roaring::Roaring> combined;
  if (cand_query.use_trigram) {
    bool fallback_requested = false;
    auto tri = RequeryIndexSide(index.GetTrigramIndex(), cand_query.required_trigrams,
                                fallback_requested);
    if (fallback_requested) {
      return false;  // Index went non-ready mid-flight: caller falls back to scan.
    }
    if (!tri.has_value() || tri->isEmpty()) {
      candidate_ids.clear();
      return true;  // Definite empty at this linearization point: nothing to do.
    }
    combined = std::move(*tri);
  }
  if (cand_query.use_extension) {
    bool fallback_requested = false;
    auto ext = RequeryIndexSide(index.GetExtensionIndex(), cand_query.extensions,
                                fallback_requested);
    if (fallback_requested) {
      return false;  // Index went non-ready mid-flight: caller falls back to scan.
    }
    if (!ext.has_value() || ext->isEmpty()) {
      candidate_ids.clear();
      return true;  // Definite empty at this linearization point: nothing to do.
    }
    if (!IntersectSide(combined, std::move(*ext))) {
      candidate_ids.clear();
      return true;
    }
  }
  if (!combined.has_value() || combined->isEmpty()) {
    candidate_ids.clear();
    return true;  // No usable side (defensive): nothing to do.
  }
  candidate_ids.resize(combined->cardinality());
  combined->toUint32Array(candidate_ids.data());
  return true;
}

template <typename Action>
[[nodiscard]] inline bool FilterCandidates(    const ISearchableIndex& index,
    const CandidateQuery& cand_query,
    const SearchContext& context,
    const ParallelSearchEngine::PatternMatchers& matchers,
    const Action& action) {
  const std::shared_lock lock(index.GetMutex());
  // Re-query under the SAME lock scope as the view snapshot below. The
  // cardinalities copied by QueryCandidates (used only for the cost-model
  // gate) may already be stale: a compaction + rebuild can land between the
  // two scopes, and filtering a stale bitmap against a fresh view misses
  // relocated rows (the verifier prevents false positives, but not false
  // negatives). Re-querying here keeps bitmaps and view atomically
  // consistent. Under this shared lock no writer can MarkBuilding, so a
  // side that is ready here stays ready for the whole scope.
  std::vector<uint32_t> candidate_ids;
  if (!cand_query.use_trigram && cand_query.use_extension) {
    const extension_index::ExtensionIndex* ext_idx = index.GetExtensionIndex();
    if (ext_idx == nullptr || !ext_idx->IsReady()) {
      return false;  // Index went non-ready mid-flight: caller falls back to scan.
    }
    if (!ext_idx->ExtractCandidateIdsNormalized(cand_query.extensions, candidate_ids)) {
      return false;  // Index went non-ready mid-flight: caller falls back to scan.
    }
  } else {
    if (!BuildCombinedCandidateIds(index, cand_query, candidate_ids)) {
      return false;
    }
  }
  if (candidate_ids.empty()) {
    return true;  // Definite empty at this linearization point: nothing to do.
  }

  const PathStorage::SoAView soaView = index.GetSearchableView();
  const size_t storage_size = index.GetStorageSize();
  const bool has_cancel_flag = (context.cancel_flag != nullptr);
  const bool has_extension_filter = context.HasExtensionFilter();
  const ItemTypeFilter item_type_filter = context.GetEffectiveItemTypeFilter();
  // If the extension index was used and the query is case-insensitive, all
  // candidates in candidate_ids already match the extension filter by construction.
  const bool check_extension_filter =
      has_extension_filter && (!cand_query.use_extension || context.case_sensitive);

  const parallel_search_detail::CandidateVerifyContext verify_ctx{
      soaView, storage_size, item_type_filter, check_extension_filter, context, matchers,};

  size_t items_checked = 0;
  for (const uint32_t i : candidate_ids) {
    if (has_cancel_flag && ((items_checked & kCancellationCheckMask) == 0U) &&
        context.cancel_flag->load()) {
      break;
    }
    items_checked++;

    size_t path_len = 0;
    if (!EvaluateCandidate(i, verify_ctx, path_len)) {
      continue;
    }

    action(soaView, i, path_len);
  }
  return true;
}

// Match-rate assumption shared by the hits-vector and arena reserves: chunk
// items (or bytes) divided by this estimates the match volume per task.
constexpr size_t kMatchRateHeuristic = 20;

// Returns true when a search has no active filters (all non-deleted items match).
[[nodiscard]] inline bool IsAllPassSearch(
    const SearchContext& context,
    const ParallelSearchEngine::PatternMatchers& matchers) noexcept {
  return !matchers.filename_matcher &&
         !matchers.path_matcher &&
         !context.HasExtensionFilter() &&
         context.GetEffectiveItemTypeFilter() == ItemTypeFilter::All &&
         !context.ShouldFilterSystemFiles() &&
         !context.extension_only_mode;
}

/**
 * @brief Thread setup data structure (after lock acquisition)
 *
 * Holds setup data that can be extracted after the lock is acquired.
 * The lock itself must remain in the lambda scope for proper RAII.
 */
struct ThreadSetupAfterLock {
  PathStorage::SoAView soa_view_; // NOLINT(readability-identifier-naming)
  size_t storage_size_ = 0; // NOLINT(readability-identifier-naming)
  ParallelSearchEngine::PatternMatchers matchers_; // NOLINT(readability-identifier-naming)
};

/**
 * @brief Initialize thread setup after lock acquisition
 *
 * Performs common initialization steps after the lock is acquired:
 * - Gets SoA view and storage size
 * - Reuses pre-built pattern matchers from search startup
 *
 * @param index The searchable index (lock must already be held)
 * @param matchers Pre-built pattern matchers (one per search, shared across workers)
 * @return ThreadSetupAfterLock struct with initialized data
 */
[[nodiscard]] inline ThreadSetupAfterLock SetupThreadWorkAfterLock(
  const ISearchableIndex& index,
  const ParallelSearchEngine::PatternMatchers& matchers) {
  ThreadSetupAfterLock setup;
  setup.soa_view_ = index.GetSearchableView();
  setup.storage_size_ = index.GetStorageSize();
  setup.matchers_ = matchers;
  return setup;
}

// Reserve hits before any index lock (avoids heap work under lock).
[[nodiscard]] inline SearchResultBatch MakeReservedWorkerResultBuffer(
  size_t chunk_start, size_t chunk_end, bool is_all_pass = false) {
  SearchResultBatch batch;
  constexpr size_t kMinInitialCapacity = 16;
  const size_t chunk_size = chunk_end - chunk_start;
  const size_t capacity = is_all_pass
                              ? (std::max)(chunk_size, kMinInitialCapacity)
                              : (std::max)(chunk_size / kMatchRateHeuristic, kMinInitialCapacity);
  batch.hits.reserve(capacity);
  return batch;
}

/**
 * RAII: worker-thread timing start, result buffer, index shared_lock, and post-lock setup.
 *
 * Member order + init list order matter: local_results must be fully reserved before index_lock
 * so we do not allocate while holding the FileIndex shared mutex.
 */
struct WorkerSearchChunkContext {
  std::chrono::high_resolution_clock::time_point start_time_{std::chrono::high_resolution_clock::now()}; // NOLINT(readability-identifier-naming)
  SearchResultBatch local_results; // NOLINT(readability-identifier-naming)
  std::shared_lock<std::shared_mutex> index_lock; // NOLINT(readability-identifier-naming)
  ThreadSetupAfterLock setup; // NOLINT(readability-identifier-naming)

  WorkerSearchChunkContext(const ISearchableIndex& index,
                           const ParallelSearchEngine::PatternMatchers& matchers,
                           size_t chunk_start, size_t chunk_end,
                           bool is_all_pass = false)
      : local_results(MakeReservedWorkerResultBuffer(chunk_start, chunk_end, is_all_pass)),
        index_lock(index.GetMutex()),
        setup(SetupThreadWorkAfterLock(index, matchers)) {}
};

/**
 * @brief Parameters for thread timing recording
 */
struct ThreadTimingParams {
  size_t start_index_ = 0; // NOLINT(readability-identifier-naming)
  size_t end_index_ = 0; // NOLINT(readability-identifier-naming)
  size_t initial_items_ = 0; // NOLINT(readability-identifier-naming)
  size_t dynamic_chunks_count_ = 0; // NOLINT(readability-identifier-naming)
  size_t total_items_processed_ = 0; // NOLINT(readability-identifier-naming)
  size_t bytes_processed_ = 0; // NOLINT(readability-identifier-naming)
  size_t results_count_ = 0; // NOLINT(readability-identifier-naming)
  const PathStorage::SoAView* soa_view_ = nullptr; // NOLINT(readability-identifier-naming)
  size_t storage_size_ = 0; // NOLINT(readability-identifier-naming)
  size_t chunk_start_ = 0; // NOLINT(readability-identifier-naming)
  size_t chunk_end_ = 0; // NOLINT(readability-identifier-naming)
};

/**
 * @brief Record thread timing information if requested
 */
inline void RecordThreadTimingIfRequested(
  std::vector<ThreadTiming>* thread_timings, int thread_idx,
  const std::chrono::high_resolution_clock::time_point& start_time,
  const ThreadTimingParams& params) {
  if (thread_timings == nullptr) {
    return;
  }

  const auto end_time = std::chrono::high_resolution_clock::now();
  const auto elapsed =
    std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

  size_t total_bytes = params.bytes_processed_;
  if (total_bytes == 0 && params.soa_view_ != nullptr) {
    total_bytes = ParallelSearchEngine::CalculateChunkBytes(*params.soa_view_, params.chunk_start_,
                                                            params.chunk_end_, params.storage_size_);
  }

  ThreadTiming timing;
  ParallelSearchEngine::RecordThreadTiming(timing, static_cast<uint64_t>(thread_idx), params.start_index_, params.end_index_,
                                           params.initial_items_, params.dynamic_chunks_count_,
                                           params.total_items_processed_, total_bytes,
                                           params.results_count_, elapsed);

  if (thread_idx < static_cast<int>(thread_timings->size())) {
    (*thread_timings)[thread_idx] = timing;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - bounds checked by enclosing if
  }
}

/**
 * @brief Chunk processing parameters
 */
struct ChunkProcessingParams {
  const ThreadSetupAfterLock& setup;
  size_t chunk_start_ = 0; // NOLINT(readability-identifier-naming)
  size_t chunk_end_ = 0; // NOLINT(readability-identifier-naming)
  int thread_idx_ = 0; // NOLINT(readability-identifier-naming)
  const char* chunk_type_ = nullptr; // NOLINT(readability-identifier-naming)
};

/**
 * @brief Process chunk with exception handling
 */
inline void ProcessChunkWithExceptionHandling(
  const ChunkProcessingParams& params, SearchResultBatch& local_results,
  const SearchContext& context) {
  try {
    ParallelSearchEngine::ProcessChunkRange(
      params.setup.soa_view_, params.chunk_start_, params.chunk_end_, local_results, context,
      params.setup.storage_size_, params.setup.matchers_.filename_matcher,
      params.setup.matchers_.path_matcher, params.setup.matchers_.path_first);
  } catch (const std::bad_alloc& e) {
    (void)e;
    LOG_ERROR_BUILD("Memory allocation failure in ParallelSearchEngine ("
                    << params.chunk_type_ << ", thread "
                    << params.thread_idx_ << ", range [" << params.chunk_start_ << "-"
                    << params.chunk_end_ << "])");
  } catch (const std::runtime_error& e) {  // NOSONAR(cpp:S1181) - Catch-all for runtime errors in worker thread
    (void)e;
    LOG_ERROR_BUILD("Runtime error in ParallelSearchEngine (" << params.chunk_type_
                    << ", thread " << params.thread_idx_ << ", range ["
                    << params.chunk_start_ << "-" << params.chunk_end_
                    << "]): " << e.what());
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all for standard exceptions in worker thread
    (void)e;
    LOG_ERROR_BUILD("Exception in ParallelSearchEngine (" << params.chunk_type_
                    << ", thread " << params.thread_idx_ << ", range ["
                    << params.chunk_start_ << "-" << params.chunk_end_
                    << "]): " << e.what());
  }
}

/**
 * @brief Dynamic chunks loop input parameters
 */
struct DynamicChunksLoopParams {
  const ThreadSetupAfterLock& setup;
  size_t total_items_ = 0; // NOLINT(readability-identifier-naming)
  size_t initial_chunks_end_ = 0; // NOLINT(readability-identifier-naming)
  std::atomic<size_t>* next_chunk_start_ = nullptr; // NOLINT(readability-identifier-naming)
  const SearchContext& context;
  SearchResultBatch& local_results;
  int thread_idx_ = 0; // NOLINT(readability-identifier-naming)
  int thread_count_ = 0; // NOLINT(readability-identifier-naming)
  size_t min_chunk_size_ = 0; // NOLINT(readability-identifier-naming)
  bool is_all_pass_ = false; // NOLINT(readability-identifier-naming)
};

/**
 * @brief Dynamic chunks loop output parameters
 */
struct DynamicChunksLoopOutput {
  size_t& dynamic_chunks_count;
  size_t& dynamic_items_total;
  size_t* total_bytes_processed_ = nullptr; // NOLINT(readability-identifier-naming)
  size_t* total_items_processed_ = nullptr; // NOLINT(readability-identifier-naming)
};

/**
 * @brief Process dynamic chunks in a loop to reduce nesting depth
 */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Dynamic chunks scheduling and processing loop
inline void ProcessDynamicChunksLoop(const DynamicChunksLoopParams& params,
                                     DynamicChunksLoopOutput& output) {
  if (params.initial_chunks_end_ >= params.total_items_) {
    return;  // No remaining work
  }

  bool should_continue = true;
  while (should_continue) {
    if (params.context.cancel_flag != nullptr && params.context.cancel_flag->load()) {
      should_continue = false;
      continue;
    }

    size_t current_chunk_size = params.context.dynamic_chunk_size;
    if (params.thread_count_ > 0 && params.min_chunk_size_ > 0) {
      const size_t current_processed = params.next_chunk_start_->load();
      const size_t remaining =
        (params.total_items_ > current_processed) ? (params.total_items_ - current_processed) : 0;
      const auto gsd = static_cast<size_t>(params.context.guided_scheduling_divisor);
      const size_t divisor = gsd * static_cast<size_t>(params.thread_count_);
      const size_t calculated_size = remaining / divisor;
      current_chunk_size = (std::max)(params.min_chunk_size_, calculated_size);
    }

    const size_t chunk_start =
      params.next_chunk_start_->fetch_add(current_chunk_size);

    if (chunk_start >= params.total_items_) {
      should_continue = false;
      continue;
    }

    const size_t live_size = params.setup.soa_view_.size;
    const size_t chunk_end =
        (std::min)({chunk_start + current_chunk_size, params.total_items_, live_size});

    if (chunk_end <= chunk_start || chunk_start >= live_size) {
      should_continue = false;
      continue;
    }

    if (params.is_all_pass_) {
      if (const size_t needed_hits = params.local_results.hits.size() + (chunk_end - chunk_start);
          needed_hits > params.local_results.hits.capacity()) {
        params.local_results.hits.reserve(needed_hits);
      }
      const size_t dynamic_chunk_bytes = ParallelSearchEngine::CalculateChunkBytes(
          params.setup.soa_view_, chunk_start, chunk_end, params.setup.storage_size_);
      if (const size_t needed_arena = params.local_results.arena.size() + dynamic_chunk_bytes;
          needed_arena > params.local_results.arena.capacity()) {
        params.local_results.arena.reserve(needed_arena);
      }
    }

    ProcessChunkWithExceptionHandling({params.setup, chunk_start, chunk_end, params.thread_idx_,
                                       "dynamic chunk",},
                                      params.local_results, params.context);

    output.dynamic_chunks_count++;
    output.dynamic_items_total += (chunk_end - chunk_start);

    if (output.total_bytes_processed_ != nullptr) {
      *output.total_bytes_processed_ += ParallelSearchEngine::CalculateChunkBytes(
        params.setup.soa_view_, chunk_start, chunk_end, params.setup.storage_size_);
    }
    if (output.total_items_processed_ != nullptr) {
      *output.total_items_processed_ += (chunk_end - chunk_start);
    }
  }
}

/**
 * @brief Parallel search task parameters
 */
struct ParallelSearchTaskParams {
  const ISearchableIndex& index;
  size_t start_index_ = 0; // NOLINT(readability-identifier-naming)
  size_t end_index_ = 0; // NOLINT(readability-identifier-naming)
  int thread_idx_ = 0; // NOLINT(readability-identifier-naming)
  size_t total_items_ = 0; // NOLINT(readability-identifier-naming)
  size_t initial_chunks_end_ = 0; // NOLINT(readability-identifier-naming)
  const std::shared_ptr<std::atomic<size_t>>& next_dynamic_chunk;
  const SearchContext& context;
  const std::shared_ptr<const ParallelSearchEngine::PatternMatchers>& shared_matchers;
  std::vector<ThreadTiming>* thread_timings_ = nullptr; // NOLINT(readability-identifier-naming)
  int thread_count_ = 0; // NOLINT(readability-identifier-naming)
  size_t min_chunk_size_ = 0; // NOLINT(readability-identifier-naming)
};

/**
 * @brief Execute search task for a single thread
 */
inline SearchResultBatch ExecuteSearchTask(
  const ParallelSearchTaskParams& params) {
  const bool is_all_pass = IsAllPassSearch(params.context, *params.shared_matchers);
  WorkerSearchChunkContext worker(params.index, *params.shared_matchers, params.start_index_,
                                  params.end_index_, is_all_pass);

  const size_t live_size = worker.setup.soa_view_.size;
  const size_t effective_start = (std::min)(params.start_index_, live_size);
  const size_t effective_end = (std::min)(params.end_index_, live_size);
  const size_t effective_total = (std::min)(params.total_items_, live_size);
  const size_t effective_initial_end = (std::min)(params.initial_chunks_end_, live_size);

  if (effective_start < effective_end) {
    const size_t chunk_bytes = ParallelSearchEngine::CalculateChunkBytes(
        worker.setup.soa_view_, effective_start, effective_end,
        worker.setup.storage_size_);
    const size_t arena_capacity =
        is_all_pass ? chunk_bytes : (chunk_bytes / kMatchRateHeuristic);
    worker.local_results.arena.reserve(arena_capacity);
    ProcessChunkWithExceptionHandling(
      {worker.setup, effective_start, effective_end, params.thread_idx_,
       "initial chunk",},
      worker.local_results, params.context);
  }

  const size_t initial_items = effective_end - effective_start;
  size_t dynamic_chunks_count = 0;
  size_t dynamic_items_total = 0;

  DynamicChunksLoopOutput dynamic_output{dynamic_chunks_count, dynamic_items_total};  // NOLINT(misc-const-correctness) - passed by non-const ref
  ProcessDynamicChunksLoop(
    {worker.setup, effective_total, effective_initial_end, params.next_dynamic_chunk.get(),
     params.context, worker.local_results, params.thread_idx_, params.thread_count_,
     params.min_chunk_size_, is_all_pass,},
    dynamic_output);

  RecordThreadTimingIfRequested(
    params.thread_timings_, params.thread_idx_, worker.start_time_,
    {effective_start, effective_end, initial_items, dynamic_chunks_count,
      initial_items + dynamic_items_total, 0, worker.local_results.Size(), &worker.setup.soa_view_,
     worker.setup.storage_size_, effective_start, effective_end,});

  return worker.local_results;
}

/**
 * Runs worker body with top-level exception translation.
 */
[[nodiscard]] inline SearchResultBatch ExecuteSearchTaskWithEnqueueExceptions(
    const ParallelSearchTaskParams& params) {
  try {
    return ExecuteSearchTask(params);
  } catch (const std::bad_alloc& e) {
    (void)e;
    LOG_ERROR_BUILD("ParallelSearchEngine: Memory allocation failure in worker thread "
                    << params.thread_idx_ << ", range [" << params.start_index_ << "-" << params.end_index_
                    << "]");
    return SearchResultBatch{};
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - catch-all for setup exceptions
    LOG_ERROR_BUILD("ParallelSearchEngine: Exception in worker thread "
                    << params.thread_idx_ << ", range [" << params.start_index_ << "-" << params.end_index_
                    << "]: " << e.what());
    return SearchResultBatch{};
  } catch (...) {  // NOSONAR(cpp:S2738) - unknown exceptions must not escape worker lambdas
    LOG_ERROR_BUILD("ParallelSearchEngine: Unknown exception in worker thread "
                    << params.thread_idx_ << ", range [" << params.start_index_ << "-" << params.end_index_
                    << "]");
    return SearchResultBatch{};
  }
}

} // namespace parallel_search_detail

// Cost-model mispredict detector: the gate picked the filter because
// predicted filter < predicted scan (shared estimates in the header). If the
// actual filter took longer than the predicted scan by a clear margin, the
// model is wrong for this machine/matcher shape — log loud so it is spotted
// in production logs. The noise floor suppresses timer jitter on tiny
// candidate sets; the 1.5x margin suppresses borderline calls. Rate-bound
// (first trip plus every 64th, with running count) so a systematically wrong
// model cannot flood the log on per-keystroke searches. The reverse misroute
// (scan picked, filter would win) is not observable without running both —
// it stays a benchmark exercise.
void ParallelSearchEngine::MaybeLogTrigramMispredict(uint64_t filter_us, uint64_t cardinality,
                                                      size_t total_items,
                                                      int scan_threads,
                                                      bool used_extension) const {
  constexpr uint64_t kMispredictNoiseFloorUs = 1000;
  constexpr double kMispredictMargin = 1.5;
  constexpr uint64_t kMispredictLogEvery = 64;
  const double predicted_scan_us =
      parallel_search_detail::PredictedScanCostUs(total_items, scan_threads);
  if (filter_us > kMispredictNoiseFloorUs &&
      static_cast<double>(filter_us) > predicted_scan_us * kMispredictMargin) {
    const uint64_t mispredict_n = trigram_mispredict_count_.fetch_add(1) + 1;  // NOSONAR(cpp:S6004) - declared outside if to prevent clang -Wunneeded-internal-declaration
    if (mispredict_n == 1 || mispredict_n % kMispredictLogEvery == 0) {
      LOG_IMPORTANT_BUILD("ParallelSearchEngine: trigram filter slower than predicted scan "
                          << "(filter_us=" << filter_us
                          << ", predicted_scan_us=" << predicted_scan_us
                          << ", cardinality=" << cardinality
                          << ", total_items=" << total_items
                          << ", scan_threads=" << scan_threads
                          << ", used_extension=" << (used_extension ? 1 : 0)
                          << ", extension_takes=" << extension_filter_take_count_.load()
                          << ", mispredict_n=" << mispredict_n << ")");
    }
  }
}

// Constructor - thread_pool_ initialized in initializer list
// NOLINTNEXTLINE(readability-identifier-naming,cppcoreguidelines-pro-type-member-init,hicpp-member-init) - threadPool matches API; member-init: thread_pool_ set below
ParallelSearchEngine::ParallelSearchEngine(std::shared_ptr<ThreadPool> threadPool)
    : thread_pool_(std::move(threadPool)) {
  if (!thread_pool_) {
    LOG_ERROR_BUILD("ParallelSearchEngine: thread pool is null");
  }
}

std::optional<std::vector<std::future<SearchResultBatch>>>
ParallelSearchEngine::TryCandidateFilterPath(
    const ISearchableIndex& index, const parallel_search_detail::CandidateQuery& query,
    const SearchContext& context, int scan_threads, size_t total_items,
    std::vector<ThreadTiming>* thread_timings) const {
  using Futures = std::vector<std::future<SearchResultBatch>>;
  if (!query.query_attempted ||
      !parallel_search_detail::ShouldUseTrigramFilter(query.cardinality, total_items,
                                                      scan_threads)) {
    return std::nullopt;
  }
  if (query.known_empty) {
    return Futures{};
  }

  std::promise<SearchResultBatch> promise;
  SearchResultBatch batch;
  const auto max_hits = static_cast<size_t>(query.cardinality);
  batch.hits.reserve(max_hits);
  constexpr size_t kMinEstimatedPathLength = 96;
  constexpr size_t kFallbackAveragePathLength = 128;
  const size_t avg_path_len =
      (total_items > 0) ? (index.GetStorageSize() / total_items) + 1 : kFallbackAveragePathLength;
  batch.arena.reserve(max_hits * (std::max)(kMinEstimatedPathLength, avg_path_len));

  const auto matchers = CreatePatternMatchers(context);
  const auto filter_start = std::chrono::steady_clock::now();
  if (query.use_extension) {
    extension_filter_take_count_.fetch_add(1);
  }
  const bool filtered = parallel_search_detail::FilterCandidates(
      index, query, context, matchers,
      [&batch](const PathStorage::SoAView& soaView, uint32_t i, size_t path_len) {  // NOLINT(readability-identifier-naming) - soaView matches SoA convention used across ParallelSearchEngine
        const char* path = soaView.path_storage + soaView.path_offsets[i];
        const size_t filename_offset = soaView.filename_start[i];
        const size_t extension_offset = soaView.extension_start[i];

        batch.AppendHit(soaView.path_ids[i], path, path_len, soaView.is_directory[i] != 0,
                        filename_offset, extension_offset);
      });
  const auto filter_us = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - filter_start)
          .count());

  // Mispredict detector (cost-model validation, e.g. on Windows where the
  // arm64-measured constants may not transfer): the gate picked the filter
  // because predicted filter < predicted scan. See MaybeLogTrigramMispredict
  // for the thresholds and rate bound.
  MaybeLogTrigramMispredict(filter_us, query.cardinality, total_items, scan_threads,
                             query.use_extension);

  if (!filtered) {
    // An index went non-ready between the cost-model gate and the filter (a
    // rebuild started mid-flight), so the re-query inside FilterCandidates
    // had nothing consistent to filter. Signal fall-through to the full
    // parallel scan rather than returning a result computed from nothing.
    return std::nullopt;
  }
  if (thread_timings != nullptr) {
    thread_timings->clear();
    thread_timings->push_back(
        ThreadTiming{0, 0, query.cardinality, 0, query.cardinality, batch.hits.size(), 0, 0,
                     query.cardinality,});
  }

  promise.set_value(std::move(batch));
  Futures futures;
  futures.push_back(promise.get_future());
  return futures;
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
  // Default seq_cst load: cancel_requested is a plain cooperative stop flag (IndexBuilderBase
  // stores true, nothing is published through it), so an acquire order would be both
  // unnecessary and a Sonar cpp:S8417 issue. See .agents/rules/cpp-atomic-memory-order-cpp.md.
  if (cancel_flag != nullptr && cancel_flag->load()) {
    return futures;
  }
  SearchContext effective_context = context;
  if (effective_context.cancel_flag == nullptr) {
    effective_context.cancel_flag = cancel_flag;
  }

  // Candidate filtering for SearchAsyncWithData (trigram and/or extension).
  // The filter path is single-threaded; take it only while it is cheaper than
  // a parallel scan (see ShouldUseTrigramFilter for the cost model).
  const auto cq = parallel_search_detail::QueryCandidates(index, effective_context);
  int scan_threads = DetermineThreadCount(thread_count, total_bytes,
                                          effective_context.search_thread_pool_size);
  scan_threads = (std::max)(scan_threads, 1);
  if (static_cast<size_t>(scan_threads) > total_items) {
    // scan_threads <= INT_MAX here, so total_items < INT_MAX: narrowing is safe.
    scan_threads = static_cast<int>(total_items);
  }
  if (auto taken = TryCandidateFilterPath(index, cq, effective_context, scan_threads,
                                           total_items, thread_timings);
      taken.has_value()) {
    return std::move(*taken);
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
  const ThreadPool& pool = *thread_pool_;
  if (pool.GetThreadCount() == 0) {  // NOSONAR(cpp:S6004) - Variable used after if block (line 235 and later)
    LOG_ERROR_BUILD("SearchAsyncWithData: Thread pool has 0 threads - cannot execute search");
    return futures;
  }

  futures = LaunchSearchTasks(
      index, total_items, thread_count, effective_context, thread_timings);

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
ThreadPool& ParallelSearchEngine::GetThreadPool() const {
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

  // Decide the per-item matcher order once: reorder only when the filename side
  // has no cheap reject and the path side does (see PreferPathMatcherFirst).
  matchers.path_first = search_pattern_utils::PreferPathMatcherFirst(matchers.filename_matcher,
                                                                    matchers.path_matcher);

  return matchers;
}

// ============================================================================
// Parallel Search Tasks Launching (Initial Chunks + Dynamic Small Chunks)
// ============================================================================

std::vector<std::future<SearchResultBatch>>
ParallelSearchEngine::LaunchSearchTasks(
    const ISearchableIndex& index,
    size_t total_items, int thread_count,
    const SearchContext& context,
    std::vector<ThreadTiming>* thread_timings) const {
  std::vector<std::future<SearchResultBatch>> futures;
  futures.reserve(static_cast<size_t>(thread_count));

  ThreadPool& thread_pool = *thread_pool_;

  // Single shared copy for all workers (keeps context/matchers alive across task lifetimes).
  const std::shared_ptr<const SearchContext> shared_context =
      std::make_shared<SearchContext>(context);
  const std::shared_ptr<const ParallelSearchEngine::PatternMatchers> shared_matchers =
      std::make_shared<ParallelSearchEngine::PatternMatchers>(
          ParallelSearchEngine::CreatePatternMatchers(context));

  const bool is_all_pass = parallel_search_detail::IsAllPassSearch(context, *shared_matchers);

  // Phase 1: Calculate initial chunk distribution
  // Convert percentage to absolute number of items.
  // For all-pass queries (100% uniform work), allocate 100% of the work in static initial chunks
  // to eliminate atomic contention, dynamic chunking, and multiple re-allocations.
  const size_t initial_work_percent =
      is_all_pass ? 100 : static_cast<size_t>(context.hybrid_initial_percent);
  const size_t initial_work_items = (total_items * initial_work_percent) / 100;
  // Divide initial work equally among threads. Floor at 1 when there is work:
  // tiny indexes (a single entry with percent < 100) otherwise round every
  // chunk to empty and launch zero tasks — silent empty results. The dynamic
  // phase only runs inside launched tasks, so it cannot recover this.
  size_t initial_chunk_size = (initial_work_items + static_cast<size_t>(thread_count) - 1) / static_cast<size_t>(thread_count);
  if (total_items > 0 && initial_chunk_size == 0) {
    initial_chunk_size = 1;
  }
  // Calculate where initial chunks end (may exceed total_items, so clamp)
  const size_t initial_chunks_end = (std::min)(initial_chunk_size * static_cast<size_t>(thread_count), total_items);

  // Phase 2: Set up dynamic chunk allocation
  // Use shared_ptr to ensure atomic counter lives as long as the lambdas need it
  // Start dynamic chunks after initial chunks are complete
  auto next_dynamic_chunk = std::make_shared<std::atomic<size_t>>(initial_chunks_end);

  // Launch one task per thread, each processing an initial chunk then competing for dynamic chunks
  for (int t = 0; t < thread_count; ++t) {
    // Calculate initial chunk boundaries for this thread
    const size_t start_index = static_cast<size_t>(t) * initial_chunk_size;
    // Last thread gets any remainder to ensure we don't exceed total_items
    const size_t end_index = (t == thread_count - 1)
                                 ? initial_chunks_end
                                 : (std::min)(start_index + initial_chunk_size, initial_chunks_end);

    // Skip threads with empty initial chunks (shouldn't happen, but be safe)
    if (start_index >= end_index) {
      continue;
    }

    futures.push_back(thread_pool.Enqueue([&index, start_index, end_index, thread_idx = t,  // NOLINT(bugprone-exception-escape) - task exceptions caught below
                                           total_items, initial_chunks_end, next_dynamic_chunk,
                                           shared_context, shared_matchers, thread_timings, thread_count] {
      const parallel_search_detail::ParallelSearchTaskParams params{
          index, start_index, end_index, thread_idx, total_items, initial_chunks_end, next_dynamic_chunk,
          *shared_context, shared_matchers, thread_timings, thread_count, shared_context->dynamic_chunk_size,};
      return parallel_search_detail::ExecuteSearchTaskWithEnqueueExceptions(params);
    }));
  }

  return futures;
}


