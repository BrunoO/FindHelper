/**
 * @file ui/SearchHistoryCard.h
 * @brief Dedicated, reusable UI component for rendering search history cards
 */

#pragma once

#include "core/Settings.h"
#include "gui/GuiState.h"
#include "gui/UIActions.h"
#include "search/SearchHistory.h"

#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace ui {

constexpr float kDefaultCardWidth = 240.0F;
constexpr float kDefaultCardHeight = 62.0F;

/**
 * @brief Configuration and display options for rendering a SearchHistoryCard.
 */
struct SearchHistoryCardConfig {
  float width = kDefaultCardWidth;
  float height = kDefaultCardHeight;
  bool is_pinned = true;
};

// Render a compact icon button with a hover tooltip.
[[nodiscard]] bool RenderIconButtonWithTooltip(const char* icon_text, std::string_view tooltip,
                                               bool* out_hovered_or_active = nullptr);

// Width of a SmallButton for the widest of given icon labels
[[nodiscard]] float MaxSmallIconButtonWidth(std::initializer_list<const char*> icons);

// Single-line text at cursor with ellipsis when wider than max_width
void TextEllipsisSingleLine(const char* text_begin, const char* text_end, float max_width);

/**
 * @brief Dedicated, reusable UI component for rendering search history cards.
 *
 * Implements a single-window Dear ImGui card widget using ImGui::Selectable with
 * ImGuiSelectableFlags_AllowOverlap and custom ImDrawList background styling.
 * Avoids nested child-window issues and ensures reliable mouse hit-testing for both
 * the card selection and action buttons (Unpin/Pin, Rename, Delete).
 */
namespace search_history_card {

/**
 * @brief Render a search history card widget.
 *
 * @param entry Search history entry to display
 * @param state GUI state (selection, rename/delete modals)
 * @param settings AppSettings (pinning, persistence)
 * @param actions UI actions (running search, saving settings)
 * @param now_ms Current timestamp in milliseconds (for relative time)
 * @param config Card dimensions and display mode
 * @return true if the card itself was clicked (search triggered)
 */
bool Render(const SearchHistoryEntry& entry,
            GuiState& state,
            AppSettings& settings,
            UIActions* actions,
            std::int64_t now_ms,
            const SearchHistoryCardConfig& config);

}  // namespace search_history_card
}  // namespace ui
