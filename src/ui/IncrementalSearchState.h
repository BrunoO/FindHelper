#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "search/SearchWorker.h"  // for SearchResult

namespace ui {

/**
 * @brief Case-insensitive substring match over a SearchResult's filename and directory path.
 *
 * Returns true if query is empty (all rows match an empty query) or if query appears
 * (case-insensitively) in either the filename part or the directory part of result.fullPath.
 *
 * Free function so it can be unit-tested independently of IncrementalSearchState.
 */
bool IncrementalSearchMatches(std::string_view query, const SearchResult& result);

/**
 * @brief Self-contained incremental search state for the results table.
 *
 * All state lives here; no new fields in GuiState.
 * No ImGui calls; pure logic only.
 * ResultsTable.cpp owns one instance as a function-static variable in Render().
 */
class IncrementalSearchState {
 public:
  // --- Queries ---

  /** True while the inline "Filter in results" prompt should be shown and keys routed here. */
  [[nodiscard]] bool IsPromptVisible() const { return prompt_visible_; }

  /** True while the filtered result vector should be used as display_results. */
  [[nodiscard]] bool IsFilterActive() const { return filter_active_; }

  /** True if the caller should consume printable chars and Backspace this frame. */
  [[nodiscard]] bool IsActive() const { return prompt_visible_; }

  /** Current query string (may be empty). */
  [[nodiscard]] std::string_view Query() const { return query_; }

  /** Number of rows in the current filtered set (0 when filter inactive or no matches). */
  [[nodiscard]] int MatchCount() const;

  /**
   * @brief Filtered display results to substitute for the base results.
   *
   * Valid to call only when IsFilterActive() is true. Caller must not store the reference
   * beyond the current frame (may be invalidated by the next UpdateQuery call).
   */
  [[nodiscard]] const std::vector<SearchResult>& FilteredResults() const;

  /**
   * @brief Consume pending deferred scroll restore.
   *
   * Returns true (and fills out_y) exactly once after Cancel(), then resets.
   * Must be called from inside BeginTable/EndTable each frame.
   */
  [[nodiscard]] bool ConsumeScrollRestorePending(float& out_y);

  // --- Commands ---

  /**
   * @brief Activate incremental search mode.
   *
   * Captures pre-search state for cancel restore.
   *
   * @param base_results    The current display results (used to build the filtered vector).
   * @param current_selection  Current value of GuiState::selectedRow.
   * @param current_scroll_y   Current table scroll Y (captured via ImGui::GetScrollY()).
   * @param results_batch_number  Current GuiState::resultsBatchNumber.
   */
  void Begin(const std::vector<SearchResult>& base_results,
             int current_selection,
             float current_scroll_y,
             uint64_t results_batch_number);

  /**
   * @brief Update the query and rebuild the filtered result set.
   *
   * Call whenever the query string changes (character appended or deleted).
   * Updates filtered_results_ and resets current_match_index_ to 0 (first match).
   *
   * @param new_query   The complete new query string.
   * @param base_results  The current base display results (not the filtered ones).
   * @param selected_row  Out: updated to the first matching row index in display_results, or -1.
   * @param scroll_to_selected  Out: set to true when selected_row is updated.
   */
  void UpdateQuery(std::string_view new_query,
                   const std::vector<SearchResult>& base_results,
                   int& selected_row,
                   bool& scroll_to_selected);

  /**
   * @brief Move to the next match (wraps at end).
   *
   * Only valid while IsActive() and MatchCount() > 0.
   *
   * @param selected_row       Out: updated to the new match row index.
   * @param scroll_to_selected Out: set to true.
   */
  void NavigateNext(int& selected_row, bool& scroll_to_selected);

  /**
   * @brief Move to the previous match (wraps at start).
   *
   * Only valid while IsActive() and MatchCount() > 0.
   */
  void NavigatePrev(int& selected_row, bool& scroll_to_selected);

  /**
   * @brief Accept current filter: hide prompt, keep filter and selection.
   *
   * If the query is empty, also clears filter_active_.
   */
  void Accept();

  /**
   * @brief Cancel: exit prompt, restore pre-search selection and schedule scroll restore.
   *
   * Sets restore_scroll_pending_ = true. The caller must call ConsumeScrollRestorePending()
   * from inside the BeginTable block on the next frame.
   *
   * @param selected_row Out: restored to original_selection_index_.
   * @param scroll_to_selected Out: set to true so the clipper includes the row.
   */
  void Cancel(int& selected_row, bool& scroll_to_selected);

  /**
   * @brief Check for batch number change and reset if the underlying results changed.
   *
   * Must be called once per frame before any other operation that reads filtered_results_.
   * Resets both flags, query, and filter if the batch number differs from the captured value.
   *
   * @param current_batch_number  Current GuiState::resultsBatchNumber.
   * @param selected_row          Out: set to -1 if reset occurs (stale selection cleared).
   */
  void CheckBatchNumber(uint64_t current_batch_number, int& selected_row);

 private:
  void RebuildFilter(const std::vector<SearchResult>& base_results);
  void Reset(int& selected_row);

  // NOLINTNEXTLINE(readability-identifier-naming) - Private members use snake_case_ per project convention
  bool prompt_visible_ = false;
  // NOLINTNEXTLINE(readability-identifier-naming) - Private members use snake_case_ per project convention
  bool filter_active_ = false;
  // NOLINTNEXTLINE(readability-identifier-naming) - Private members use snake_case_ per project convention
  std::string query_;
  // NOLINTNEXTLINE(readability-identifier-naming) - Private members use snake_case_ per project convention
  std::vector<SearchResult> filtered_results_;
  // NOLINTNEXTLINE(readability-identifier-naming) - Private members use snake_case_ per project convention
  int current_match_index_ = -1;

  // Pre-search state for cancel restore
  // NOLINTNEXTLINE(readability-identifier-naming) - Private members use snake_case_ per project convention
  int original_selection_index_ = -1;
  // NOLINTNEXTLINE(readability-identifier-naming) - Private members use snake_case_ per project convention
  float original_scroll_y_ = 0.0F;
  // NOLINTNEXTLINE(readability-identifier-naming) - Private members use snake_case_ per project convention
  bool restore_scroll_pending_ = false;

  // Invalidation anchor
  // NOLINTNEXTLINE(readability-identifier-naming) - Private members use snake_case_ per project convention
  uint64_t captured_batch_number_ = 0;
};

}  // namespace ui

