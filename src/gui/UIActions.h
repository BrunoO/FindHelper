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

// Forward declarations
struct AppSettings;
class GuiState;
struct SavedSearch;

namespace ui {
  /**
   * @brief Interface for UI actions that UI components can trigger
   * 
   * This interface decouples UI components from application logic classes.
   * UI components call methods on this interface instead of directly
   * calling SearchController, SearchWorker, etc.
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
     * @brief Apply a saved search configuration to state
     * @param state GUI state to modify
     * @param saved_search Saved search configuration to apply
     */
    virtual void ApplySavedSearch(GuiState& state, const SavedSearch& saved_search) = 0;
    
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
  };
}  // namespace ui
