#include "ui/RxHighlighter.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "search/SearchPrefixes.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <string_view>
#include <vector>

namespace ui {

namespace {

[[nodiscard]] constexpr bool IsWhitespaceChar(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

[[nodiscard]] constexpr bool IsAtomDelimiter(char c) noexcept {
  return IsWhitespaceChar(c) || c == '(' || c == ')' || c == '"';
}

[[nodiscard]] std::size_t ConsumeQuotedString(std::string_view source, std::size_t start) noexcept {
  const std::size_t close = source.find('"', start + 1);
  return (close == std::string_view::npos) ? source.size() : close + 1;
}

[[nodiscard]] ImVec4 GetTokenColor(rx::RxTokenCategory category) noexcept {
  switch (category) {
    case rx::RxTokenCategory::kPrefix:
      return Theme::Colors::RxPrefix;
    case rx::RxTokenCategory::kParenthesis:
      return Theme::Colors::RxParenthesis;
    case rx::RxTokenCategory::kOperator:
      return Theme::Colors::RxOperator;
    case rx::RxTokenCategory::kPathClause:
      return Theme::Colors::RxPathClause;
    case rx::RxTokenCategory::kNamedClass:
      return Theme::Colors::RxNamedClass;
    case rx::RxTokenCategory::kQuotedString:
      return Theme::Colors::RxQuotedString;
    case rx::RxTokenCategory::kBareLiteral:
    case rx::RxTokenCategory::kWhitespace:
    default:
      return Theme::Colors::RxBareLiteral;
  }
}

} // namespace

void TokenizeRx(std::string_view source, std::vector<RxToken>& out_tokens) {
  out_tokens.clear();
  if (source.empty()) {
    return;
  }

  std::size_t i = 0;
  if (source.rfind("rx:", 0) == 0) {
    out_tokens.push_back({0, 3, rx::RxTokenCategory::kPrefix});
    i = 3;
  }

  bool is_clause_head = false;
  while (i < source.size()) {
    if (const char c = source.at(i); IsWhitespaceChar(c)) {
      const std::size_t start = i;
      while (i < source.size() && IsWhitespaceChar(source.at(i))) {
        ++i;
      }
      out_tokens.push_back({start, i - start, rx::RxTokenCategory::kWhitespace});
    } else if (c == '(') {
      out_tokens.push_back({i, 1, rx::RxTokenCategory::kParenthesis});
      is_clause_head = true;
      ++i;
    } else if (c == ')') {
      out_tokens.push_back({i, 1, rx::RxTokenCategory::kParenthesis});
      is_clause_head = false;
      ++i;
    } else if (c == '"') {
      const std::size_t end = ConsumeQuotedString(source, i);
      out_tokens.push_back({i, end - i, rx::RxTokenCategory::kQuotedString});
      i = end;
      is_clause_head = false;
    } else {
      const std::size_t start = i;
      while (i < source.size() && !IsAtomDelimiter(source.at(i))) {
        ++i;
      }
      const std::string_view atom = source.substr(start, i - start);
      const rx::RxTokenCategory cat = rx::ClassifyRxAtom(atom, is_clause_head);
      out_tokens.push_back({start, i - start, cat});
      is_clause_head = false;
    }
  }
}

namespace {

struct OverlayTarget {
  ImDrawList* draw_list = nullptr;
  ImVec2 clip_min;
  ImVec2 clip_max;
  ImVec2 text_base_pos;
  bool valid = false;
};

[[nodiscard]] const ImGuiWindow* FindChildWindowForWidget(const ImGuiWindow* parent_window, ImGuiID widget_id) {
  if (parent_window != nullptr) {
    for (int i = 0; i < parent_window->DC.ChildWindows.Size; ++i) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - retained suppression
      if (const ImGuiWindow* const w = parent_window->DC.ChildWindows.Data[i];
          w != nullptr && w->ChildId == widget_id) {
        return w;
      }
    }
  }

  if (const ImGuiContext* const ctx = ImGui::GetCurrentContext(); ctx != nullptr) {
    for (int i = 0; i < ctx->Windows.Size; ++i) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - retained suppression
      if (const ImGuiWindow* const w = ctx->Windows.Data[i];
          w != nullptr && w->ChildId == widget_id) {
        return w;
      }
    }
  }
  return nullptr;
}

[[nodiscard]] OverlayTarget ResolveOverlayTarget(ImGuiID widget_id, bool is_multiline) {
  const ImGuiStyle& style = ImGui::GetStyle();
  if (is_multiline) {
    const ImGuiWindow* const parent_window = ImGui::GetCurrentWindow();
    const ImGuiWindow* const child_window = FindChildWindowForWidget(parent_window, widget_id);
    if (child_window == nullptr) {
      return {};
    }
    const ImVec2 text_base_pos(child_window->Pos.x + style.FramePadding.x - child_window->Scroll.x,
                               child_window->Pos.y + style.FramePadding.y - child_window->Scroll.y);
    return {child_window->DrawList, child_window->InnerClipRect.Min, child_window->InnerClipRect.Max, text_base_pos, true};
  }

  const ImGuiInputTextState* const state = ImGui::GetInputTextState(widget_id);
  const float scroll_x = (state != nullptr) ? state->Scroll.x : 0.0F;

  const ImVec2 item_min = ImGui::GetItemRectMin();
  const ImVec2 item_max = ImGui::GetItemRectMax();

  const ImVec2 clip_min(item_min.x + style.FramePadding.x, item_min.y);
  const ImVec2 clip_max(item_max.x - style.FramePadding.x, item_max.y);
  const ImVec2 text_base_pos(clip_min.x - scroll_x, item_min.y + style.FramePadding.y);
  return {ImGui::GetWindowDrawList(), clip_min, clip_max, text_base_pos, true};
}

void BuildLineStarts(const char* buffer, std::vector<std::size_t>& out_starts) {
  out_starts.clear();
  out_starts.push_back(0);
  for (std::size_t idx = 0; buffer[idx] != '\0'; ++idx) {
    if (buffer[idx] == '\n') {
      out_starts.push_back(idx + 1);
    }
  }
}

void RenderTokenSegments(const char* buffer, const RxToken& token,
                         const std::vector<std::size_t>& line_starts,
                         ImFont* font, float font_size, ImDrawList* draw_list,
                         ImVec2 text_base_pos) {
  const ImVec4 color_vec = GetTokenColor(token.category);
  const ImU32 col_u32 = ImGui::GetColorU32(color_vec);

  std::size_t tok_start = token.offset;
  const std::size_t tok_end = token.offset + token.length;

  while (tok_start < tok_end) {
    std::size_t line_idx = 0;
    std::size_t line_start_offset = 0;
    std::size_t next_line_start = tok_end;

    if (line_starts.size() > 1) {
      const auto it = std::upper_bound(line_starts.begin(), line_starts.end(), tok_start);
      line_idx = static_cast<std::size_t>(std::distance(line_starts.begin(), it) - 1);
      line_start_offset = line_starts.at(line_idx);
      next_line_start = (line_idx + 1 < line_starts.size()) ? line_starts.at(line_idx + 1) : tok_end;
    }

    const std::size_t seg_limit = (next_line_start > line_start_offset && buffer[next_line_start - 1] == '\n')
                                      ? next_line_start - 1
                                      : next_line_start;
    const std::size_t seg_end = (std::min)(tok_end, seg_limit);

    if (seg_end > tok_start) {
      const float x_offset = font->CalcTextSizeA(font_size, FLT_MAX, -1.0F, buffer + line_start_offset, buffer + tok_start).x;
      const float y_offset = static_cast<float>(line_idx) * font_size;
      const ImVec2 token_pos(text_base_pos.x + x_offset, text_base_pos.y + y_offset);
      draw_list->AddText(font, font_size, token_pos, col_u32, buffer + tok_start, buffer + seg_end);
    }

    if (seg_end == tok_end) {
      break;
    }
    tok_start = (buffer[seg_end] == '\n') ? seg_end + 1 : seg_end;
  }
}

} // namespace

void RenderRxSyntaxOverlay(const char* id, const char* buffer, bool is_multiline) {
  if (buffer == nullptr || !search_pattern_utils::HasPrefix(buffer, search_pattern_utils::prefix::kRx)) {
    return;
  }
  if (!ImGui::IsItemVisible()) {
    return;
  }

  const OverlayTarget target = ResolveOverlayTarget(ImGui::GetID(id), is_multiline);
  if (!target.valid || target.clip_min.x >= target.clip_max.x || target.clip_min.y >= target.clip_max.y) {
    return;
  }

  ImFont* const font = ImGui::GetFont();
  const float font_size = ImGui::GetFontSize();

  // Scratch containers hoisted across frames (strictly UI-thread confined)
  static std::vector<RxToken> s_scratch_tokens;
  s_scratch_tokens.clear();
  TokenizeRx(buffer, s_scratch_tokens);

  static std::vector<std::size_t> s_line_starts;
  BuildLineStarts(buffer, s_line_starts);

  target.draw_list->PushClipRect(target.clip_min, target.clip_max, true);

  for (const RxToken& token : s_scratch_tokens) {
    if (token.category == rx::RxTokenCategory::kWhitespace || token.length == 0) {
      continue;
    }
    RenderTokenSegments(buffer, token, s_line_starts, font, font_size, target.draw_list, target.text_base_pos);
  }

  target.draw_list->PopClipRect();
}

} // namespace ui
