#pragma once

/**
 * @file ui/RxHighlighter.h
 * @brief Real-time syntax coloring overlay for structured regex (rx:) search inputs.
 *
 * Scans an rx: input buffer into typed tokens using the shared rx::ClassifyRxAtom
 * classifier from RxCompiler, and renders an in-place colorized overlay onto
 * Dear ImGui's draw list matching the input box coordinates, clipping, and scroll.
 */

#include "search/rx/RxCompiler.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace ui {

/**
 * @brief Token produced by TokenizeRx.
 */
struct RxToken {
  std::size_t offset = 0;
  std::size_t length = 0;
  rx::RxTokenCategory category = rx::RxTokenCategory::kBareLiteral;
};

/**
 * @brief Tokenizes an rx: search expression into contiguous tokens covering the entire source.
 *
 * @param source Full input text (including the "rx:" prefix).
 * @param out_tokens Output vector for tokens. The caller should pass a hoisted/reusable container.
 */
void TokenizeRx(std::string_view source, std::vector<RxToken>& out_tokens);

/**
 * @brief Renders the syntax-highlighted text overlay over ImGui::InputText or ImGui::InputTextMultiline.
 *
 * Must be called immediately after InputText/InputTextMultiline (and after restoring ImGuiCol_Text).
 *
 * @param id Widget ID matching the preceding InputText call (e.g. "##filename").
 * @param buffer Null-terminated text buffer currently displayed in the widget.
 * @param is_multiline True if rendering over an InputTextMultiline child window.
 */
void RenderRxSyntaxOverlay(const char* id, const char* buffer, bool is_multiline = false);

} // namespace ui
