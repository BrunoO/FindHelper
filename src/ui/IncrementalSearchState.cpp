#include "ui/IncrementalSearchState.h"

#include <cstddef>

#include "utils/StringSearch.h"

namespace ui {

bool IncrementalSearchMatches(std::string_view query, const SearchResult& result) {
  if (query.empty()) {
    return true;
  }

  const std::string_view full_path = result.fullPath;
  const std::size_t last_separator = full_path.find_last_of("\\/");
  const std::string_view filename =
    (last_separator == std::string_view::npos) ? full_path : full_path.substr(last_separator + 1U);
  const std::string_view directory =
    (last_separator == std::string_view::npos) ? std::string_view{} : full_path.substr(0U, last_separator);

  if (string_search::ContainsSubstringI(filename, query)) {
    return true;
  }

  if (string_search::ContainsSubstringI(directory, query)) {
    return true;
  }

  return false;
}

int IncrementalSearchState::MatchCount() const {
  if (!filter_active_) {
    return 0;
  }

  return static_cast<int>(filtered_results_.size());
}

const std::vector<SearchResult>& IncrementalSearchState::FilteredResults() const {
  return filtered_results_;
}

bool IncrementalSearchState::ConsumeScrollRestorePending(float& out_y) {
  if (!restore_scroll_pending_) {
    return false;
  }

  out_y = original_scroll_y_;
  restore_scroll_pending_ = false;
  return true;
}

void IncrementalSearchState::ClearFilterState() {
  filter_active_ = false;
  query_.clear();
  filtered_results_.clear();
  current_match_index_ = -1;
}

void IncrementalSearchState::Begin(const std::vector<SearchResult>& base_results,
                                   int current_selection,
                                   float current_scroll_y,
                                   uint64_t results_batch_number) {
  (void)base_results;

  prompt_visible_ = true;
  ClearFilterState();

  original_selection_index_ = current_selection;
  original_scroll_y_ = current_scroll_y;
  restore_scroll_pending_ = false;
  captured_batch_number_ = results_batch_number;
}

void IncrementalSearchState::UpdateQuery(std::string_view new_query,
                                         const std::vector<SearchResult>& base_results,
                                         int& selected_row,
                                         bool& scroll_to_selected) {
  query_.assign(new_query.begin(), new_query.end());

  if (query_.empty()) {
    filter_active_ = false;
    filtered_results_.clear();
    current_match_index_ = -1;
    selected_row = -1;
    scroll_to_selected = false;
    return;
  }

  filter_active_ = true;
  RebuildFilter(base_results);

  if (filtered_results_.empty()) {
    current_match_index_ = -1;
    selected_row = -1;
    scroll_to_selected = false;
    return;
  }

  current_match_index_ = 0;
  selected_row = 0;
  scroll_to_selected = true;
}

void IncrementalSearchState::NavigateNext(int& selected_row, bool& scroll_to_selected) {
  const int match_count = MatchCount();
  if (!IsActive() || match_count <= 0) {
    return;
  }

  if (current_match_index_ < 0) {
    current_match_index_ = 0;
  } else {
    current_match_index_ = (current_match_index_ + 1) % match_count;
  }

  selected_row = current_match_index_;
  scroll_to_selected = true;
}

void IncrementalSearchState::NavigatePrev(int& selected_row, bool& scroll_to_selected) {
  const int match_count = MatchCount();
  if (!IsActive() || match_count <= 0) {
    return;
  }

  if (current_match_index_ < 0) {
    current_match_index_ = 0;
  } else {
    current_match_index_ = (current_match_index_ - 1 + match_count) % match_count;
  }

  selected_row = current_match_index_;
  scroll_to_selected = true;
}

void IncrementalSearchState::Accept() {
  if (query_.empty()) {
    prompt_visible_ = false;
    filter_active_ = false;
    filtered_results_.clear();
    current_match_index_ = -1;
    return;
  }

  prompt_visible_ = false;
}

void IncrementalSearchState::Cancel(int& selected_row, bool& scroll_to_selected) {
  prompt_visible_ = false;
  ClearFilterState();

  restore_scroll_pending_ = true;
  selected_row = original_selection_index_;
  scroll_to_selected = true;
}

void IncrementalSearchState::CheckBatchNumber(uint64_t current_batch_number, int& selected_row) {
  if ((prompt_visible_ || filter_active_) && current_batch_number != captured_batch_number_) {
    Reset(selected_row);
    captured_batch_number_ = current_batch_number;
  }
}

void IncrementalSearchState::RebuildFilter(const std::vector<SearchResult>& base_results) {
  filtered_results_.clear();
  filtered_results_.reserve(base_results.size());

  const std::string_view query_view = query_;
  for (const auto& result : base_results) {
    if (IncrementalSearchMatches(query_view, result)) {
      filtered_results_.push_back(result);
    }
  }
}

void IncrementalSearchState::Reset(int& selected_row) {
  prompt_visible_ = false;
  ClearFilterState();

  selected_row = -1;
  restore_scroll_pending_ = false;
  original_selection_index_ = -1;
  original_scroll_y_ = 0.0F;
}

}  // namespace ui

