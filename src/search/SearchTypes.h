#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "utils/FileAttributeConstants.h"

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

/** Search statistics (duration, counts, thread count). */
struct SearchStats {
  double duration_milliseconds_ = 0.0;   // NOLINT(readability-identifier-naming) - snake_case_ per project convention
  size_t total_items_scanned_ = 0;       // NOLINT(readability-identifier-naming) - snake_case_ per project convention
  size_t total_matches_found_ = 0;        // NOLINT(readability-identifier-naming) - snake_case_ per project convention
  int num_threads_used_ = 0;              // NOLINT(readability-identifier-naming) - snake_case_ per project convention
};

/** Lightweight result data extracted during search (avoids FileEntry lookup).
 * fullPath is an owned copy taken while the index shared_lock is held, so it remains
 * valid after the lock is released (PathStorage may reallocate on Insert/Remove).
 * filename_start/extension_start are offsets from path start (from SoA); avoid re-parsing in merge. */
struct SearchResultData {  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - members default-constructed
  uint64_t id = 0;  // NOLINT(readability-identifier-naming)
  std::string fullPath;  // NOLINT(readability-identifier-naming) - owned copy; safe after shared_lock release
  bool isDirectory = false;  // NOLINT(readability-identifier-naming)
  size_t filename_start = 0;       // NOLINT(readability-identifier-naming) - offset of filename in fullPath
  size_t extension_start = SIZE_MAX;  // NOLINT(readability-identifier-naming) - offset of extension (SIZE_MAX = none)
};

/** Single hit referencing path bytes owned by its SearchResultBatch arena.
 * Stores offsets, never pointers or views, so arena reallocation during the
 * scan and batch moves through futures cannot invalidate hits. Views into the
 * arena are formed only at merge time while the batch is alive on the
 * reader's stack. */
struct SearchResultHit {  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - members default-initialized
  uint64_t id = 0;
  size_t path_offset = 0;
  size_t path_len = 0;
  bool is_directory = false;
  size_t filename_start = 0;
  size_t extension_start = SIZE_MAX;
};

/** Worker-task result courier: one arena allocation per task instead of one
 * heap std::string per hit. INVARIANT: hits hold offsets only; never store a
 * string_view or pointer into arena except as a temporary while *this is
 * alive on the reader's stack (see merge in SearchResultUtils). */
struct SearchResultBatch {
  std::vector<char> arena;
  std::vector<SearchResultHit> hits;

  void AppendHit(uint64_t id, const char* path, size_t path_len, bool is_directory,
                 size_t filename_start, size_t extension_start) {
    const size_t offset = arena.size();
    arena.insert(arena.end(), path, path + path_len);
    arena.push_back('\0');
    hits.push_back(SearchResultHit{id, offset, path_len, is_directory,
                                   filename_start, extension_start,});
  }

  [[nodiscard]] size_t Size() const noexcept { return hits.size(); }
  [[nodiscard]] bool Empty() const noexcept { return hits.empty(); }
  void Clear() {
    arena.clear();
    hits.clear();
  }
};

/**
 * Convert one worker-task batch into owned SearchResultData rows.
 *
 * Reads path bytes via arena offsets while @p batch is alive (caller-owned);
 * each row performs exactly one heap allocation, on the calling thread only.
 *
 * NOTE: production no longer calls this (batches flow straight to the pool
 * copy); it exists for test helpers that assert on owned rows
 * (test_helpers::CollectFutures overload). Do not add new production call
 * sites: that would reintroduce the per-hit std::string path the arena
 * migration removed.
 *
 * Lives in this imgui-free header (rather than SearchResultUtils.h, which
 * pulls GuiState/imgui) so test helpers compiled without imgui include
 * paths can share the exact production conversion.
 *
 * @param out Destination rows (appended; caller should reserve first)
 * @param batch Source batch, must outlive the call
 */
inline void AppendBatchToResultData(std::vector<SearchResultData>& out,
                                    const SearchResultBatch& batch) {
  // Batch is caller-owned for the whole loop; offsets produced by AppendHit.
  const char* const arena_data = batch.arena.data();
  for (const SearchResultHit& hit : batch.hits) {
    SearchResultData data;
    data.id = hit.id;
    data.isDirectory = hit.is_directory;
    data.fullPath.assign(arena_data + hit.path_offset, hit.path_len);
    data.filename_start = hit.filename_start;
    data.extension_start = hit.extension_start;
    out.push_back(std::move(data));
  }
}

/** Column indices for the results table. */
namespace ResultColumn {  // NOLINT(readability-identifier-naming) - Public enum-like namespace used across UI/search code
constexpr int Mark = 0;          // NOLINT(readability-identifier-naming)
constexpr int Filename = 1;      // NOLINT(readability-identifier-naming)
constexpr int Size = 2;          // NOLINT(readability-identifier-naming)
constexpr int LastModified = 3;  // NOLINT(readability-identifier-naming)
constexpr int FullPath = 4;      // NOLINT(readability-identifier-naming)
constexpr int Extension = 5;     // NOLINT(readability-identifier-naming)
constexpr int FolderFiles = 6;   // NOLINT(readability-identifier-naming) - recursive non-directory file count; hidden by default
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

struct SearchResult {  // NOSONAR(cpp:S8379) - UI-thread display caches; mutations confined to UI thread per ui-state-write-confinement
  std::string_view fullPath;  // NOLINT(readability-identifier-naming) - points into searchResultPathPool; pool must outlive this

  /**
   * @brief Get the filename part of the full path
   * @return std::string_view pointing into fullPath
   */
  [[nodiscard]] std::string_view GetFilename() const {
    if (filename_offset < fullPath.size()) {
      size_t count = std::string_view::npos;
      // Producer invariant (PathStorage::ParsePathOffsets -> SoA
      // extension_start = last_dot + 1, last_dot > filename_start):
      // extension_offset >= filename_offset + 2 (dot + at least one char).
      // A violated pair (ext == fn+1, ext in the directory part, stale
      // offset) is treated as "no extension" (whole tail returned) rather
      // than computing a zero-length filename. No assert here: this is a UI
      // hot path and tests deliberately exercise malformed offsets.
      if (extension_offset != std::string_view::npos &&
          extension_offset >= filename_offset + 2) {
        count = extension_offset - filename_offset - 1;  // Exclude the dot
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

  mutable uint64_t fileSize = 0;  // NOSONAR(cpp:S8379) - UI-thread display cache; see struct comment NOLINT(readability-identifier-naming) - UI-facing API uses camelCase
  mutable std::string fileSizeDisplay;  // NOSONAR(cpp:S8379) - UI-thread display cache; see struct comment NOLINT(readability-identifier-naming) - UI-facing API uses camelCase
  mutable FILETIME lastModificationTime = {0, 0};  // NOSONAR(cpp:S8379) - UI-thread display cache; see struct comment NOLINT(readability-identifier-naming) - UI-facing API uses camelCase
  mutable std::string lastModificationDisplay;  // NOSONAR(cpp:S8379) - UI-thread display cache; see struct comment NOLINT(readability-identifier-naming) - UI-facing API uses camelCase
  // Cached truncated path for UI display (avoids expensive CalcTextSize every frame)
  mutable std::string truncatedPathDisplay;  // NOSONAR(cpp:S8379) - UI-thread display cache; see struct comment NOLINT(readability-identifier-naming) - UI-facing API uses camelCase
  mutable float truncatedPathColumnWidth = -1.0F;  // NOSONAR(cpp:S8379) - UI-thread display cache; see struct comment NOLINT(readability-identifier-naming) - UI-facing API uses camelCase
  // Recursive non-directory file count for directories (populated asynchronously by FolderSizeAggregator).
  // kFolderFileCountNotLoaded until computed; meaningless for non-directory entries.
  mutable uint64_t folderFileCount = kFolderFileCountNotLoaded;  // NOSONAR(cpp:S8379) - UI-thread display cache; see struct comment NOLINT(readability-identifier-naming) - UI-facing API uses camelCase
  mutable std::string folderFileCountDisplay;  // NOSONAR(cpp:S8379) - UI-thread display cache; see struct comment NOLINT(readability-identifier-naming) - UI-facing API uses camelCase
  uint64_t fileId = 0; // NOLINT(readability-identifier-naming)
  bool isDirectory = false; // NOLINT(readability-identifier-naming)
};

/**
 * Filter search results by item filesystem type (all items, files only, or folders only).
 */
enum class ItemTypeFilter : uint8_t {
  All = 0,        // Match both files and folders (default)
  FilesOnly = 1,  // Match only files (skip directories)
  FoldersOnly = 2, // Match only folders (skip non-directories)
};

/** Single source of truth for legacy folders_only fallback.
 * Effective filter is FoldersOnly when the enum is All but legacy flag is set;
 * otherwise the enum wins (explicit FilesOnly/FoldersOnly selection wins over stale flag).
 * Use everywhere instead of re-implementing the ternary. */
[[nodiscard]] inline ItemTypeFilter EffectiveItemTypeFilter(ItemTypeFilter filter,
                                                           bool folders_only) noexcept {
  if (folders_only && filter == ItemTypeFilter::All) {
    return ItemTypeFilter::FoldersOnly;
  }
  return filter;
}

/** True when raw int is a valid ItemTypeFilter value (0..2). */
[[nodiscard]] inline bool IsValidItemTypeFilterValue(int value) noexcept {
  return value >= static_cast<int>(ItemTypeFilter::All) &&
         value <= static_cast<int>(ItemTypeFilter::FoldersOnly);
}

/** Clamp raw int to a valid ItemTypeFilter; out-of-range (corrupted JSON) falls back to All. */
[[nodiscard]] inline ItemTypeFilter ItemTypeFilterFromInt(int value) noexcept {
  if (!IsValidItemTypeFilterValue(value)) {
    return ItemTypeFilter::All;
  }
  return static_cast<ItemTypeFilter>(value);
}

struct SearchParams {  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - std::string members default-constructed
  std::string filenameInput; // NOLINT(readability-identifier-naming,cppcoreguidelines-pro-type-member-init,hicpp-member-init) - default-constructed empty
  std::string extensionInput; // NOLINT(readability-identifier-naming,cppcoreguidelines-pro-type-member-init,hicpp-member-init) - default-constructed empty
  std::string pathInput; // NOLINT(readability-identifier-naming,cppcoreguidelines-pro-type-member-init,hicpp-member-init) - default-constructed empty
  ItemTypeFilter itemTypeFilter = ItemTypeFilter::All; // NOLINT(readability-identifier-naming)
  bool foldersOnly = false; // NOLINT(readability-identifier-naming) - legacy compatibility
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
    s.total_searches_ = total_searches_.load();
    s.total_results_found_ = total_results_found_.load();
    s.total_search_time_ms_ =
        total_search_time_ms_.load();
    s.total_postprocess_time_ms_ =
        total_postprocess_time_ms_.load();
    s.max_search_time_ms_ = max_search_time_ms_.load();
    s.max_postprocess_time_ms_ =
        max_postprocess_time_ms_.load();
    s.last_search_time_ms_ = last_search_time_ms_.load();
    s.last_postprocess_time_ms_ =
        last_postprocess_time_ms_.load();
    s.last_results_count_ = last_results_count_.load();
    s.max_results_count_ = max_results_count_.load();
    return s;
  }
};
