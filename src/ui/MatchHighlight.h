#pragma once

/**
 * @file ui/MatchHighlight.h
 * @brief Results-table match highlighting: query context, span finding, ImGui paint.
 *
 * Spans are computed on the UI thread for clipped rows only (substring, fuzzy, glob,
 * rs: regex, and pp:/PathPattern literal runs). The Name column also paints Path-field
 * matches that fall in the filename. The Path column highlights against the visible
 * full path (directory + filename), including truncated display text. The Extension
 * column paints only when the Extensions allowlist filter matches that row's extension.
 */

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gui/GuiState.h"
#include "imgui.h"
#include "path/PathPatternMatcher.h"
#include "search/SearchContext.h"
#include "search/SearchPatternUtils.h"
#include "ui/IncrementalSearchState.h"
#include "ui/Theme.h"
#include "utils/StdRegexUtils.h"
#include "utils/StringSearch.h"
#include "utils/StringUtils.h"

namespace ui {

enum class ColumnHighlightKind : std::uint8_t {
  None,
  Substring,
  Fuzzy,
  Glob,
  Regex,
  PathPattern,
};

/**
 * @brief Per-frame highlight context for one results column (Name or Path).
 *
 * @p pattern refers into GuiState input buffers or IncrementalSearchState::Query();
 * valid for the current frame only.
 */
struct ColumnHighlightQuery {
  ColumnHighlightKind kind = ColumnHighlightKind::None;
  std::string_view pattern;
  bool case_sensitive = false;

  [[nodiscard]] bool IsActive() const {
    return kind != ColumnHighlightKind::None && !pattern.empty();
  }
};

// Backward-compatible alias used by Name-column call sites.
using NameHighlightQuery = ColumnHighlightQuery;
using NameHighlightKind = ColumnHighlightKind;

namespace match_highlight_detail {

[[nodiscard]] inline ColumnHighlightQuery BuildFromRawPattern(std::string_view raw,
                                                              bool case_sensitive) {
  if (raw.empty()) {
    return {};
  }
  const search_pattern_utils::PatternType type = search_pattern_utils::DetectPatternType(raw);
  const std::string_view pattern = search_pattern_utils::ExtractPatternView(raw);
  if (pattern.empty()) {
    return {};
  }
  if (type == search_pattern_utils::PatternType::Fuzzy) {
    return ColumnHighlightQuery{ColumnHighlightKind::Fuzzy, pattern, case_sensitive};
  }
  if (type == search_pattern_utils::PatternType::Substring) {
    return ColumnHighlightQuery{ColumnHighlightKind::Substring, pattern, case_sensitive};
  }
  if (type == search_pattern_utils::PatternType::Glob) {
    return ColumnHighlightQuery{ColumnHighlightKind::Glob, pattern, case_sensitive};
  }
  if (type == search_pattern_utils::PatternType::StdRegex) {
    return ColumnHighlightQuery{ColumnHighlightKind::Regex, pattern, case_sensitive};
  }
  if (type == search_pattern_utils::PatternType::PathPattern) {
    return ColumnHighlightQuery{ColumnHighlightKind::PathPattern, pattern, case_sensitive};
  }
  return {};
}

[[nodiscard]] inline ColumnHighlightQuery BuildIncrementalSubstringQuery(
    const IncrementalSearchState& incremental_search) {
  if (!incremental_search.IsFilterActive() && !incremental_search.IsPromptVisible()) {
    return {};
  }
  const std::string_view q = incremental_search.Query();
  if (q.empty()) {
    return {};
  }
  return ColumnHighlightQuery{ColumnHighlightKind::Substring, q, false};
}

}  // namespace match_highlight_detail

/**
 * @brief Prefer active `/` filter query; else Name field pattern.
 */
[[nodiscard]] inline ColumnHighlightQuery BuildNameHighlightQuery(
    const GuiState& state, const IncrementalSearchState& incremental_search) {
  if (const ColumnHighlightQuery incremental =
          match_highlight_detail::BuildIncrementalSubstringQuery(incremental_search);
      incremental.IsActive()) {
    return incremental;
  }
  return match_highlight_detail::BuildFromRawPattern(state.searchCriteria.filename_input.AsView(),
                                                     state.searchCriteria.case_sensitive);
}

/**
 * @brief Prefer active `/` filter query; else Path field substring or fz: pattern.
 */
[[nodiscard]] inline ColumnHighlightQuery BuildPathHighlightQuery(
    const GuiState& state, const IncrementalSearchState& incremental_search) {
  if (const ColumnHighlightQuery incremental =
          match_highlight_detail::BuildIncrementalSubstringQuery(incremental_search);
      incremental.IsActive()) {
    return incremental;
  }
  return match_highlight_detail::BuildFromRawPattern(state.searchCriteria.path_input.AsView(),
                                                     state.searchCriteria.case_sensitive);
}

/**
 * @brief Per-frame Extensions allowlist for Extension-column highlighting.
 *
 * Built from @c GuiState searchCriteria.extension_input (semicolon list). Entries are lowercased
 * and sorted for O(log N) lookup via @c ExtensionMatches (case-insensitive).
 * Independent of Name/Path/`/` highlighting — only answers "why did Extensions match".
 */
struct ExtensionHighlightQuery {
  ExtensionSet extension_set;

  [[nodiscard]] bool IsActive() const { return !extension_set.empty(); }
};

/**
 * @brief Parse Extensions field into a sorted allowlist; empty input → inactive query.
 *
 * Cached on the raw input text: ParseExtensions + sort allocate and previously ran every
 * frame. UI-thread confined (called only from ResultsTable::Render); the returned reference
 * stays valid until the next rebuild (i.e. until the Extensions input text changes).
 */
[[nodiscard]] inline const ExtensionHighlightQuery& BuildExtensionHighlightQuery(
    const GuiState& state) {
  const std::string_view input = state.searchCriteria.extension_input.AsView();
  static std::string cached_input;
  static ExtensionHighlightQuery cached_query;
  if (input == cached_input) {
    return cached_query;
  }
  cached_query.extension_set = ParseExtensions(input);
  std::sort(cached_query.extension_set.begin(),  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
            cached_query.extension_set.end());
  cached_query.extension_set.erase(
      std::unique(cached_query.extension_set.begin(),  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
                  cached_query.extension_set.end()),
      cached_query.extension_set.end());
  cached_input.assign(input);
  return cached_query;
}

/**
 * @brief True when @p extension (no leading dot) is in the Extensions allowlist.
 *
 * Matching is case-insensitive to match search / ParseExtensions behavior.
 */
[[nodiscard]] inline bool ExtensionAllowlistMatches(std::string_view extension,
                                                    const ExtensionHighlightQuery& query) {
  if (!query.IsActive() || extension.empty()) {
    return false;
  }
  return search_pattern_utils::ExtensionMatches(extension, query.extension_set, false);
}

/**
 * @brief Invisible Selectable; paints entire extension in match color when allowlisted.
 *
 * @p display_cstr must be null-terminated (thread-local buffer). Used for the Extension column.
 * Non-matching / inactive → plain Selectable text.
 */
inline bool RenderExtensionHighlightedSelectable(const char* display_cstr,
                                                 const ExtensionHighlightQuery& query,
                                                 bool is_selected, ImGuiSelectableFlags flags) {
  assert(display_cstr != nullptr);
  const std::string_view display_text{display_cstr};
  if (!ExtensionAllowlistMatches(display_text, query)) {
    return ImGui::Selectable(display_cstr, is_selected, flags);
  }

  const ImVec2 text_size =
      ImGui::CalcTextSize(display_text.data(), display_text.data() + display_text.size());
  const bool pressed = ImGui::Selectable(
      "##ext_hl", is_selected,
      flags | ImGuiSelectableFlags_AllowOverlap,  // NOLINT(hicpp-signed-bitwise,bugprone-signed-bitwise) - ImGui flags bitmask
      ImVec2(text_size.x, 0.0F));

  const ImVec2 item_min = ImGui::GetItemRectMin();
  const ImVec2 pen{item_min.x, item_min.y + ImGui::GetStyle().FramePadding.y};
  ImDrawList* const draw_list = ImGui::GetWindowDrawList();
  const ImU32 match_col = ImGui::ColorConvertFloat4ToU32(Theme::Colors::MatchHighlight);
  draw_list->AddText(pen, match_col, display_text.data(),
                     display_text.data() + static_cast<std::ptrdiff_t>(display_text.size()));
  return pressed;
}

/**
 * @brief Fill @p out_spans for @p text using @p query. Clears out_spans first.
 * @return true if there is at least one span to paint.
 */
inline bool FindColumnMatchSpans(std::string_view text, const ColumnHighlightQuery& query,
                                 std::vector<string_search::MatchSpan>& out_spans) {
  out_spans.clear();
  if (!query.IsActive() || text.empty()) {
    return false;
  }
  if (query.kind == ColumnHighlightKind::Fuzzy) {
    return string_search::FindFuzzySpans(text, query.pattern, query.case_sensitive, out_spans);
  }
  if (query.kind == ColumnHighlightKind::Glob) {
    return string_search::FindGlobLiteralSpans(text, query.pattern, query.case_sensitive,
                                               out_spans);
  }
  if (query.kind == ColumnHighlightKind::Regex) {
    return std_regex_utils::FindRegexSpans(query.pattern, text, query.case_sensitive, out_spans);
  }
  if (query.kind == ColumnHighlightKind::PathPattern) {
    return path_pattern::FindPathPatternSpans(query.pattern, text, query.case_sensitive,
                                              out_spans);
  }
  return string_search::FindSubstringSpans(text, query.pattern, query.case_sensitive, out_spans);
}

[[nodiscard]] inline bool SameHighlightQuery(const ColumnHighlightQuery& lhs,
                                             const ColumnHighlightQuery& rhs) {
  return lhs.kind == rhs.kind && lhs.case_sensitive == rhs.case_sensitive &&
         lhs.pattern == rhs.pattern;
}

/**
 * @brief Name-column spans: Name-field hits plus Path-field hits that fall in the filename.
 *
 * When `/` overrides both columns the two queries are identical; Path is not applied twice.
 */
inline bool FindNameColumnMatchSpans(std::string_view filename,
                                     const ColumnHighlightQuery& name_query,
                                     const ColumnHighlightQuery& path_query,
                                     std::vector<string_search::MatchSpan>& out_spans) {
  if (!path_query.IsActive() ||
      (name_query.IsActive() && SameHighlightQuery(name_query, path_query))) {
    return FindColumnMatchSpans(filename, name_query, out_spans);
  }
  if (!name_query.IsActive()) {
    return FindColumnMatchSpans(filename, path_query, out_spans);
  }

  FindColumnMatchSpans(filename, name_query, out_spans);
  std::vector<string_search::MatchSpan> path_spans;
  FindColumnMatchSpans(filename, path_query, path_spans);
  out_spans.insert(out_spans.end(), path_spans.begin(), path_spans.end());
  string_search::SortAndMergeSpans(out_spans);
  return !out_spans.empty();
}

/**
 * @brief Draw @p text at @p pos with matched spans in @p match_col.
 */
inline void DrawHighlightedText(ImDrawList* draw_list, ImVec2 pos, std::string_view text,
                                const std::vector<string_search::MatchSpan>& spans, ImU32 base_col,
                                ImU32 match_col) {
  assert(draw_list != nullptr);
  if (text.empty()) {
    return;
  }

  const char* const data = text.data();
  const size_t len = text.size();

  if (spans.empty()) {
    draw_list->AddText(pos, base_col, data, data + static_cast<std::ptrdiff_t>(len));
    return;
  }

  size_t cursor = 0;
  for (const string_search::MatchSpan& span : spans) {
    assert(span.start_ <= span.end_ && span.end_ <= len);
    assert(span.start_ >= cursor);
    if (span.start_ > cursor) {
      const char* const seg_begin = data + static_cast<std::ptrdiff_t>(cursor);
      const char* const seg_end = data + static_cast<std::ptrdiff_t>(span.start_);
      draw_list->AddText(pos, base_col, seg_begin, seg_end);
      pos.x += ImGui::CalcTextSize(seg_begin, seg_end).x;
    }
    {
      const char* const seg_begin = data + static_cast<std::ptrdiff_t>(span.start_);
      const char* const seg_end = data + static_cast<std::ptrdiff_t>(span.end_);
      draw_list->AddText(pos, match_col, seg_begin, seg_end);
      pos.x += ImGui::CalcTextSize(seg_begin, seg_end).x;
    }
    cursor = span.end_;
  }
  if (cursor < len) {
    const char* const seg_begin = data + static_cast<std::ptrdiff_t>(cursor);
    const char* const seg_end = data + static_cast<std::ptrdiff_t>(len);
    draw_list->AddText(pos, base_col, seg_begin, seg_end);
  }
}

/**
 * @brief Invisible Selectable plus overdrawn highlighted text (no icon prefix).
 *
 * Used for the Path column. Spans are found against @p display_text (what the user
 * sees), so truncated paths only highlight matches still visible after ellipsis.
 */
inline bool RenderHighlightedTextSelectable(std::string_view display_text,
                                            const ColumnHighlightQuery& query, bool is_selected,
                                            ImGuiSelectableFlags flags,
                                            std::vector<string_search::MatchSpan>& span_scratch) {
  const ImVec2 text_size =
      ImGui::CalcTextSize(display_text.data(), display_text.data() + display_text.size());
  const bool pressed = ImGui::Selectable(
      "##col_hl", is_selected,
      flags | ImGuiSelectableFlags_AllowOverlap,  // NOLINT(hicpp-signed-bitwise,bugprone-signed-bitwise) - ImGui flags bitmask
      ImVec2(text_size.x, 0.0F));

  const ImVec2 item_min = ImGui::GetItemRectMin();
  const ImVec2 pen{item_min.x, item_min.y + ImGui::GetStyle().FramePadding.y};
  ImDrawList* const draw_list = ImGui::GetWindowDrawList();
  const ImU32 base_col = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 match_col = ImGui::ColorConvertFloat4ToU32(Theme::Colors::MatchHighlight);

  if (FindColumnMatchSpans(display_text, query, span_scratch)) {
    DrawHighlightedText(draw_list, pen, display_text, span_scratch, base_col, match_col);
  } else if (!display_text.empty()) {
    draw_list->AddText(pen, base_col, display_text.data(),
                       display_text.data() + static_cast<std::ptrdiff_t>(display_text.size()));
  }
  return pressed;
}

/** Arguments for RenderHighlightedFilenameSelectable (avoids cpp:S107). */
struct HighlightedFilenameSelectableParams {
  const char* display_cstr = nullptr;
  size_t filename_byte_offset = 0;
  std::string_view filename;
  const ColumnHighlightQuery* name_query = nullptr;
  const ColumnHighlightQuery* path_query = nullptr;
  bool is_selected = false;
  ImGuiSelectableFlags flags = 0;
  std::vector<string_search::MatchSpan>* span_scratch = nullptr;
};

/**
 * @brief Selectable hit-target plus overdrawn icon + highlighted filename.
 *
 * @param params Bundled display string, filename offset/slice, Name/Path queries, and span scratch.
 * @return Same click result as ImGui::Selectable.
 */
inline bool RenderHighlightedFilenameSelectable(const HighlightedFilenameSelectableParams& params) {
  assert(params.display_cstr != nullptr);
  assert(params.name_query != nullptr);
  assert(params.path_query != nullptr);
  assert(params.span_scratch != nullptr);

  // Width matches display text; height 0 uses ImGui default (text + frame padding).
  const ImVec2 text_size = ImGui::CalcTextSize(params.display_cstr);
  const bool pressed = ImGui::Selectable(
      "##fn_hl", params.is_selected,
      params.flags | ImGuiSelectableFlags_AllowOverlap,  // NOLINT(hicpp-signed-bitwise,bugprone-signed-bitwise) - ImGui flags bitmask
      ImVec2(text_size.x, 0.0F));

  const ImVec2 item_min = ImGui::GetItemRectMin();
  const float text_y = item_min.y + ImGui::GetStyle().FramePadding.y;
  ImVec2 pen{item_min.x, text_y};

  ImDrawList* const draw_list = ImGui::GetWindowDrawList();
  const ImU32 base_col = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 match_col = ImGui::ColorConvertFloat4ToU32(Theme::Colors::MatchHighlight);

  const std::string_view display_view{params.display_cstr};
  const size_t prefix_len = (std::min)(params.filename_byte_offset, display_view.size());

  // Icon + space in base color.
  if (prefix_len > 0U) {
    const char* const prefix_end =
        params.display_cstr + static_cast<std::ptrdiff_t>(prefix_len);
    draw_list->AddText(pen, base_col, params.display_cstr, prefix_end);
    pen.x += ImGui::CalcTextSize(params.display_cstr, prefix_end).x;
  }

  if (FindNameColumnMatchSpans(params.filename, *params.name_query, *params.path_query,
                               *params.span_scratch)) {
    DrawHighlightedText(draw_list, pen, params.filename, *params.span_scratch, base_col,
                        match_col);
  } else if (!params.filename.empty()) {
    const char* const fn_begin =
        params.display_cstr + static_cast<std::ptrdiff_t>(prefix_len);
    const char* const fn_end =
        params.display_cstr + static_cast<std::ptrdiff_t>(display_view.size());
    draw_list->AddText(pen, base_col, fn_begin, fn_end);
  }

  return pressed;
}

}  // namespace ui
