#pragma once

/**
 * @file ui/EmptyState.h
 * @brief Empty state rendering component
 *
 * This component handles rendering the empty state UI when there are no search results
 * and no active search. It displays:
 * - Placeholder message
 * - Indexed file count
 * - Recent searches (if any)
 * - Example searches
 */

#include <cstddef> // For size_t

// Forward declarations
class GuiState;
struct AppSettings;

namespace ui {
  class UIActions;
}

namespace ui {

/**
 * @class EmptyState
 * @brief Static utility class for empty state rendering
 *
 * Renders the empty state UI when the search results table is empty:
 * - Centered placeholder message
 * - Indexed file count display
 * - Recent searches section (if any exist)
 * - Example searches section
 *
 * This component is separate from ResultsTable to follow the Single Responsibility
 * Principle - ResultsTable handles the results table, EmptyState handles the empty state.
 */
class EmptyState {
public:
  /**
   * @brief Renders the empty state UI
   *
   * Displays a centered, prominent empty state with:
   * - Placeholder message: "Enter a search term above to begin searching."
   * - Indexed file count: "📁 Indexing X,XXX,XXX files across your drives"
   * - Recent searches: Up to 5 clickable buttons (if any exist)
   * - Example searches: Clickable example search patterns
   *
   * @param state GUI state (modified when recent/example searches are clicked)
   * @param actions UI actions for applying and triggering searches
   * @param settings AppSettings containing recent searches
   * @param is_index_building When true, recent and example search buttons are disabled
   */
  static void Render(GuiState &state,
                     UIActions* actions,
                     const AppSettings &settings,
                     bool is_index_building);
};

} // namespace ui

