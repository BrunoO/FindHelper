#pragma once

/**
 * @file ui/SearchHelpWindow.h
 * @brief Search help window rendering component
 *
 * This component handles rendering the search help window, displaying:
 * - What's new (high-level user-facing features from the last month, with dates)
 * - Item name vs Path vs Extensions (and how they combine with AND)
 * - Glob search (wildcards: *, ?)
 * - PathPattern (fast glob patterns with ** and pp: prefix)
 * - Full Regex (ECMAScript with re: prefix)
 */

namespace ui::search_help_window {

/**
 * @brief Renders the search help window
 *
 * Displays a floating, resizable window explaining search syntax:
 * - Item name (filename only), Path (full path), Extensions (AND combined)
 * - Glob search (wildcards: *, ?)
 * - PathPattern (fast glob patterns with ** and pp: prefix)
 * - Full Regex (ECMAScript with re: prefix)
 *
 * @param p_open Pointer to window visibility flag (modified when window is closed)
 */
void Render(bool *p_open);

}  // namespace ui::search_help_window
