#pragma once

/**
 * @file UIActions.h
 * @brief Interface for UI actions that UI components can trigger
 *
 * This interface decouples UI components from application logic classes.
 * UI components call methods on this interface instead of directly
 * calling SearchController, SearchWorker, etc.
 *
 * This follows the Dependency Inversion Principle (DIP) by making UI
 * components depend on an abstraction rather than concrete classes.
 */

#include <string>  // NOLINT(clang-diagnostic-error) - False positive on macOS header analysis
#include <string_view>

// Forward declarations
struct AppSettings;
class GuiState;

namespace ui {
  /**
   * @brief Interface for UI-initiated actions that cross the UI → logic boundary.
   *
   * Dependency-inversion boundary: UI components (FilterPanel, UIRenderer, …) must
   * not import SearchController, SearchWorker, or other logic-layer classes directly.
   * Instead they call methods on this interface, and Application (the logic owner)
   * implements it.  Scope is deliberately narrow: only actions where the UI must
   * reach into the search/index subsystem.  Pure UI state (GuiState fields,
   * ImGui draw calls) stays in the UI layer without going through UIActions.
   */
  class UIActions {
   public:
    virtual ~UIActions() = default;

   protected:
    // Protected default constructor (interface class)
    UIActions() = default;

   public:
    // Non-copyable, non-movable (interface class)
    UIActions(const UIActions&) = delete;
    UIActions& operator=(const UIActions&) = delete;
    UIActions(UIActions&&) = delete;
    UIActions& operator=(UIActions&&) = delete;

    /**
     * @brief Trigger a manual search with current state
     * @param state Current GUI state (read-only)
     */
    virtual void TriggerManualSearch(const GuiState& state) = 0;

    /**
     * @brief Check if index is currently building
     * @return true if index is building, false otherwise
     */
    [[nodiscard]] virtual bool IsIndexBuilding() const = 0;

    /**
     * @brief Get indexed file count
     * @return Number of files in index
     */
    [[nodiscard]] virtual size_t GetIndexedFileCount() const = 0;

    /**
     * @brief Start indexing a folder (runtime index building)
     * @param folder_path Path to folder to crawl and index
     * @return true if indexing started successfully, false otherwise
     */
    [[nodiscard]] virtual bool StartIndexBuilding(std::string_view folder_path) = 0;

    /**
     * @brief Stop current index building
     *
     * Requests cancellation and waits for background threads to complete.
     */
    virtual void StopIndexBuilding() = 0;

    /**
     * @brief Export currently displayed search results to CSV
     * @param state GUI state containing search results
     */
    virtual void ExportToCsv(GuiState& state) = 0;

    /**
     * @brief Persist live AppSettings to disk after syncing runtime geometry.
     *
     * Sole production entry point for settings writes. Syncs current window size
     * into settings, then calls SaveSettings. Call after mutating AppSettings.
     *
     * @return true on successful write, false on failure
     */
    [[nodiscard]] virtual bool PersistSettings() = 0;
  };
}  // namespace ui
