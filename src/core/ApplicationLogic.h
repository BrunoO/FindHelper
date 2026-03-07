#pragma once

/**
 * @file ApplicationLogic.h
 * @brief Application logic namespace for per-frame updates and keyboard shortcuts
 *
 * This module handles the core application logic that runs every frame:
 * - Search controller updates (debounced auto-search, auto-refresh, polling)
 * - Keyboard shortcut handling (Ctrl+F, F5, Escape)
 * - Periodic file index maintenance during idle time
 *
 * This logic is separated from UI rendering to follow the Single Responsibility
 * Principle and enable cross-platform reuse (Windows and macOS).
 */

#include <chrono>

// Forward declarations
class GuiState;
class SearchController;
class SearchWorker;
class FileIndex;
class UsnMonitor;
class Application;
struct AppSettings;

/**
 * @namespace application_logic
 * @brief Handles search controller updates, maintenance tasks, keyboard shortcuts,
 *        and index building checks
 */
namespace application_logic {
  /**
   * @brief Main update function - handles all application logic per frame
   *
   * This function is called once per frame from the main loop. It:
   * - Processes keyboard shortcuts
   * - Updates the search controller (if index is ready)
   * - Performs periodic file index maintenance during idle time
   * - Checks for periodic folder recrawl (if enabled)
   *
   * @param state GUI state (modified by keyboard shortcuts)
   * @param search_controller Search controller to update
   * @param search_worker Search worker for triggering manual searches
   * @param file_index File index for maintenance operations
   * @param monitor USN monitor (may be nullptr on macOS)
   * @param is_index_building True if the index is currently being built
   * @param application Application instance for triggering recrawl
   * @param settings Application settings (for crawl folder and recrawl config; may be modified by shortcuts)
   * @param last_interaction_time Time of last user interaction (for idle detection)
   */
  void Update(GuiState &state,
              const SearchController &search_controller,
              SearchWorker &search_worker,
              FileIndex &file_index,
              const UsnMonitor *monitor,
              bool is_index_building,
              Application &application,
              AppSettings &settings,
              const std::chrono::steady_clock::time_point &last_interaction_time);
  
  /**
   * @brief Check and trigger periodic recrawl if conditions are met
   *
   * Checks if periodic recrawl should be triggered based on:
   * - Recrawl is enabled
   * - Crawl folder is set
   * - Recrawl interval has elapsed (configurable, 1-60 minutes)
   * - System and process are idle (if idle requirement enabled, configurable 1-30 minutes)
   * - No crawl is currently in progress
   *
   * @param application Application instance for triggering recrawl
   * @param settings Application settings (for crawl folder, recrawl interval, and idle config)
   * @param last_interaction_time Time of last user interaction (for idle detection)
   * @param last_crawl_completion_time Time when last crawl completed
   * @param recrawl_enabled Whether periodic recrawl is enabled
   */
  void CheckPeriodicRecrawl(Application &application,
                            const AppSettings &settings,
                            const std::chrono::steady_clock::time_point &last_interaction_time,
                            const std::chrono::steady_clock::time_point &last_crawl_completion_time,
                            bool recrawl_enabled);
  
  /**
   * @brief Handle keyboard shortcuts (called from Update)
   *
   * Processes keyboard shortcuts only when no text input is active.
   * Shortcuts are declared in src/gui/ShortcutRegistry.h (kShortcuts).
   * Adding a shortcut requires one entry in kShortcuts and one case in
   * DispatchGlobalShortcut (ApplicationLogic.cpp). HelpWindow renders
   * descriptions automatically from the registry.
   *
   * @param state GUI state (modified by shortcuts)
   * @param search_controller Search controller for triggering manual search
   * @param search_worker Search worker for triggering manual search
   * @param is_index_building True if index is building (disables F5)
   * @param application Application instance for ExportToCsv
   * @param settings Application settings (used to check UI mode; modified by toggle shortcuts)
   */
  void HandleKeyboardShortcuts(GuiState &state,
                               const SearchController &search_controller,
                               SearchWorker &search_worker,
                               bool is_index_building,
                               Application &application,
                               AppSettings &settings);
}

