#pragma once

/**
 * @file ui/SearchHelpWindow.h
 * @brief Search help window rendering component
 *
 * This component handles rendering the search help window, displaying:
 * - What's new (high-level user-facing features from the last month, with dates)
 * - Item name vs Path vs Extensions (and how they combine with AND)
 * - Glob search (wildcards: *, ?)
 * - PathPattern (advanced patterns with pp: prefix)
 * - Full Regex (ECMAScript with rs: prefix)
 * - PathPattern shorthands and character classes
 */

namespace ui {

/**
 * @class SearchHelpWindow
 * @brief Static utility class for search help window rendering
 *
 * Renders a floating window displaying search syntax documentation:
 * - Item name (filename only), Path (full path), Extensions (AND combined)
 * - Glob search (wildcards: *, ?)
 * - PathPattern (advanced patterns with pp: prefix)
 * - Full Regex (ECMAScript with rs: prefix)
 * - PathPattern shorthands and character classes
 */
class SearchHelpWindow {
public:
  /**
   * @brief Renders the search help window
   *
   * Displays a floating, resizable window explaining search syntax:
   * - Item name (filename only), Path (full path), Extensions (AND combined)
   * - Glob search (wildcards: *, ?)
   * - PathPattern (advanced patterns with pp: prefix)
   * - Full Regex (ECMAScript with rs: prefix)
   * - PathPattern shorthands and character classes
   *
   * @param p_open Pointer to window visibility flag (modified when window is closed)
   */
  static void Render(bool *p_open);
};

} // namespace ui
