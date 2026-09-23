#pragma once

/**
 * @file ui/WelcomePanel.h
 * @brief Welcome panel rendering component
 *
 * This component handles rendering the welcome panel when there are no search results
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
 * @class WelcomePanel
 * @brief Static utility class for welcome panel rendering
 *
 * Renders the welcome panel when the search results table is empty:
 * - Centered placeholder message
 * - Indexed file count display
 * - Recent searches section (if any exist)
 * - Example searches section
 *
 * This component is separate from ResultsTable to follow the Single Responsibility
 * Principle - ResultsTable handles the results table, WelcomePanel handles the welcome state.
 */
class WelcomePanel {
public:
  /**
   * @brief Renders the welcome panel UI
   *
   * Displays a centered, prominent welcome panel with:
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
                     AppSettings &settings,
                     bool is_index_building);
};

} // namespace ui
