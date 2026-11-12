#pragma once

/**
 * @file SearchControllerDetail.h
 * @brief Shared helpers for SearchController in-flight search starts (§9 buffering).
 *
 * Keeps the visible result pool intact while a background search runs; replacement
 * happens in PollResults on the same UI-thread tick new data arrives.
 */

#include "gui/GuiState.h"
#include "search/SearchResultUtils.h"
#include "search/SearchTypes.h"

namespace search_controller_detail {

/**
 * @brief Preserve already-computed directory and file attributes (size, file count, mtime,
 * display strings) from front-buffer results into back-buffer results for matching
 * entries.
 *
 * Prevents double-buffering handoff from resetting displayed folder stats / file times /
 * lazy-loaded sizes back to unloaded sentinels when results refresh. Without mtime and
 * size reconciliation for regular files, AreSearchResultsEqual fails skip-swap after
 * EnsureDisplayStringsPopulated loads attributes on the front buffer.
 */
/**
 * @brief True when the entry carries at least one user-visible attribute already
 * computed on the front buffer (size, folder file count, or mtime loaded).
 *
 * Directory size semantics differ from file size semantics: for directories only
 * kFileSizeNotLoaded is a sentinel, while file sizes also treat kFileSizeFailed
 * as unloaded.
 */
[[nodiscard]] inline bool HasComputedAttributes(const SearchResult& entry) {
  if (entry.isDirectory) {
    return entry.fileSize != kFileSizeNotLoaded ||
           entry.folderFileCount != kFolderFileCountNotLoaded ||
           !IsSentinelTime(entry.lastModificationTime);
  }
  return (entry.fileSize != kFileSizeNotLoaded && entry.fileSize != kFileSizeFailed) ||
         !IsSentinelTime(entry.lastModificationTime);
}

/**
 * @brief Copy loaded front-buffer attributes onto matching back-buffer sentinel
 * fields (sizes + display strings for directories/files, mtime + display for all).
 */
inline void ApplyComputedAttributes(const SearchResult& front, SearchResult& back) {
  if (back.isDirectory) {
    if (back.fileSize == kFileSizeNotLoaded && front.fileSize != kFileSizeNotLoaded) {
      back.fileSize = front.fileSize;
      back.fileSizeDisplay = front.fileSizeDisplay;
    }
    if (back.folderFileCount == kFolderFileCountNotLoaded &&
        front.folderFileCount != kFolderFileCountNotLoaded) {
      back.folderFileCount = front.folderFileCount;
      back.folderFileCountDisplay = front.folderFileCountDisplay;
    }
  } else if ((back.fileSize == kFileSizeNotLoaded || back.fileSize == kFileSizeFailed) &&
             front.fileSize != kFileSizeNotLoaded && front.fileSize != kFileSizeFailed) {
    back.fileSize = front.fileSize;
    back.fileSizeDisplay = front.fileSizeDisplay;
  }

  if (IsSentinelTime(back.lastModificationTime) &&
      !IsSentinelTime(front.lastModificationTime)) {
    back.lastModificationTime = front.lastModificationTime;
    back.lastModificationDisplay = front.lastModificationDisplay;
  }
}

inline void ReconcileComputedDirectoryAttributes(
    std::vector<SearchResult>& back_results,
    const std::vector<SearchResult>& front_results,
    SearchResultMergeScratch& scratch) {
  if (front_results.empty() || back_results.empty()) {
    // Clear hoisted scratch up front so no stale indices (pointing into a
    // previous, possibly freed buffer) survive an early return.
    scratch.loaded_entries.clear();
    return;
  }
  // Hoisted flat scratch map: reuse the slot array across polls. Indices (not
  // pointers) into front_results remove any lifetime coupling to the buffer.
  auto& loaded_entries = scratch.loaded_entries;
  loaded_entries.clear();
  const auto n = front_results.size();
  for (size_t i = 0; i < n; ++i) {
    if (const auto& front = front_results[i]; HasComputedAttributes(front)) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - retained suppression
      loaded_entries[front.fileId] = static_cast<uint32_t>(i);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index validated by surrounding bounds check
    }
  }
  if (loaded_entries.empty()) {
    return;
  }
  for (auto& back : back_results) {
    const auto it = loaded_entries.find(back.fileId);
    if (it == loaded_entries.end()) {
      continue;
    }
    const SearchResult& front = front_results[it->second];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - retained suppression
    if (back.isDirectory != front.isDirectory) {
      continue;
    }
    ApplyComputedAttributes(front, back);
  }
}

/**
 * @brief Order-insensitive set equality over the same fields as AreSearchResultsEqual.
 *
 * O(N) pre-check for PollResults: the back buffer is unsorted at this point, so the
 * ordered compare can only run after the O(N log N) pre-sort. When the sets match and
 * the sort spec is unchanged, the front buffer already shows this set in this order
 * (pre-sorted on commit, maintained by HandleTableSorting), so the sort AND the commit
 * can both be skipped. Any real change (add/remove/rename/size/mtime/count) misses
 * here and falls through to today's sort + ordered-compare path.
 *
 * Assumes fileId is unique per entry (index invariant; same assumption as
 * ReconcileComputedDirectoryAttributes above).
 */
[[nodiscard]] inline bool AreSearchResultSetsEqualUnordered(
    const std::vector<SearchResult>& a, const std::vector<SearchResult>& b,
    SearchResultMergeScratch& scratch) {
  if (a.size() != b.size()) {
    // Clear hoisted scratch up front so no stale indices survive an early return.
    scratch.by_id.clear();
    return false;
  }
  if (a.empty()) {
    scratch.by_id.clear();
    return true;
  }
  // Hoisted flat scratch map: reuse the slot array across polls. Indices (not
  // pointers) into b remove any lifetime coupling to the buffer. clear() on a
  // flat map keeps its capacity, so no per-element free occurs.
  auto& by_id = scratch.by_id;
  by_id.clear();
  by_id.reserve(b.size());
  const auto n = b.size();
  for (size_t i = 0; i < n; ++i) {
    by_id[b[i].fileId] = static_cast<uint32_t>(i);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - retained suppression
  }
  for (const auto& entry : a) {
    const auto it = by_id.find(entry.fileId);
    if (it == by_id.end()) {
      return false;
    }
    if (const SearchResult& other = b[it->second];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - retained suppression
        entry.fullPath != other.fullPath || entry.fileSize != other.fileSize ||
        entry.lastModificationTime.dwLowDateTime != other.lastModificationTime.dwLowDateTime ||
        entry.lastModificationTime.dwHighDateTime != other.lastModificationTime.dwHighDateTime ||
        entry.folderFileCount != other.folderFileCount || entry.isDirectory != other.isDirectory) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Compare two SearchResult vectors for structural equality.
 * Returns true if both vectors have the same size and identical fileId, fullPath,
 * fileSize, lastModificationTime, folderFileCount, and isDirectory attributes.
 */
inline bool AreSearchResultsEqual(const std::vector<SearchResult>& a,
                                  const std::vector<SearchResult>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  return std::equal(a.begin(), a.end(), b.begin(), [](const SearchResult& lhs, const SearchResult& rhs) {
    return lhs.fileId == rhs.fileId &&
           lhs.fullPath == rhs.fullPath &&
           lhs.fileSize == rhs.fileSize &&
           lhs.lastModificationTime.dwLowDateTime == rhs.lastModificationTime.dwLowDateTime &&
           lhs.lastModificationTime.dwHighDateTime == rhs.lastModificationTime.dwHighDateTime &&
           lhs.folderFileCount == rhs.folderFileCount &&
           lhs.isDirectory == rhs.isDirectory;
  });
}

/**
 * @brief Arm an in-flight search without clearing visible results.
 *
 * Used by auto-refresh and debounced instant search (spec §9 / §10 Approach A).
 * Manual search still uses ClearSearchResults at start.
 */
inline void BeginInFlightSearchKeepingVisibleResults(GuiState& state,
                                                     bool reset_input_changed = true) {
  state.async_sort_.token.Cancel();
  state.async_sort_.sort_ready_state_ = SortReadyState::Idle;
  if (reset_input_changed) {
    state.input_debounce.input_changed = false;
  }
  state.search_pipeline.search_active = true;
  state.search_pipeline.results_complete = false;
  state.search_pipeline.search_was_manual = false;
}

}  // namespace search_controller_detail
