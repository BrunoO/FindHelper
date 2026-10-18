#pragma once

/**
 * @file ui/RecycleRowDisplay.h
 * @brief Render-time Name-column decision for a Recycle Bin row.
 *
 * Spec: specs/2026-10-11_WINDOWS_RECYCLE_BIN_INDEXING_SPEC.md section 5 Phase 4 item 3, with the
 * AC-3 guard (restore must not keep rendering a stale badge / original name) and INV-4 / INV-6.
 *
 * A row is presented as recycled only when BOTH hold (belt-and-braces):
 *   1. the row's fileId is present in the current RecycleMetadataSnapshot, and
 *   2. the row's physical path is still under a tracked "$Recycle.Bin" subtree.
 * Condition 1 alone can be stale (eviction/enrichment ordering), and a recycled fileId reused by
 * a live file would otherwise badge a live row; condition 2 alone would badge a $R artifact whose
 * $I sidecar never parsed. Requiring both fails safe to today's rendering.
 *
 * SearchResult.fullPath always references the physical $R path (spec item 2), so the original name
 * surfaces only through this render-time lookup. Nothing here reads or writes GuiState, and nothing
 * here adds a field to SearchResult (INV-4: the struct layout asserts stay intact).
 *
 * Pure C++17: no imgui, no platform I/O, so the whole decision is unit testable on macOS, where no
 * Recycle Bin exists.
 */

#include <string>
#include <string_view>

struct SearchResult;

namespace recycle_bin {
struct RecycleMetadataSnapshot;
}  // namespace recycle_bin

namespace ui {

/**
 * @struct RecycleRowDisplay
 * @brief What the Name column shows for one row.
 */
struct RecycleRowDisplay {
  /**
   * Name to draw in the Name column.
   * Lifetime: a view into either result.fullPath's pool (physical $R name) or the snapshot's
   * original_path (original filename). The caller must keep both the result's pool and the
   * snapshot alive for as long as it uses this view.
   */
  std::string_view display_name;
  /// True only when the row passed both recycled conditions; false means "render as today".
  bool show_badge;
  /**
   * Two-line tooltip for @ref show_badge == true: original location, then deletion timestamp.
   * Format: "Original: <original_path>\nDeleted: <formatted>" (or "(unknown)" when the $I record
   * carried no timestamp). Empty - and never allocated - when @ref show_badge == false.
   */
  std::string tooltip;
};

/**
 * @brief Decide how one results row is rendered in the Name column.
 *
 * @param result Row being rendered (its fileId keys the snapshot lookup; its fullPath's filename
 *               is the fallback display name).
 * @param snapshot Immutable Recycle Bin metadata snapshot, or null when none is wired in yet.
 *                 Borrowed for the duration of the call; the caller owns it.
 * @param physical_path Row's physical path (SearchResult::fullPath), tested against
 *                      "$Recycle.Bin" for the AC-3 guard.
 * @return Physical name with no badge when the row is not confidently recycled, otherwise the
 *         original filename with show_badge set and the tooltip filled in.
 */
[[nodiscard]] RecycleRowDisplay BuildRecycleRowDisplay(
    const SearchResult& result, const recycle_bin::RecycleMetadataSnapshot* snapshot,
    std::string_view physical_path);

}  // namespace ui
