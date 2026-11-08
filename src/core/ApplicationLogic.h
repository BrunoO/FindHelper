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
#include <cstddef>
#include <string_view>

#include "gui/ShortcutRegistry.h"  // ShortcutAction (DispatchGlobalShortcut parameter; imgui.h arrives transitively, already true via GuiState)
#include "gui/UIActions.h"         // ShortcutHost base: the actions seam already consumed by NavigateHistory / pin orchestration

// Forward declarations
class GuiState;
class SearchController;
class SearchWorker;
class FileIndex;
class FolderSizeAggregator;
struct AppSettings;

/**
 * @namespace application_logic
 * @brief Handles search controller updates, maintenance tasks, keyboard shortcuts,
 *        and index building checks
 */
namespace application_logic {

  /**
   * @brief Narrow host interface for keyboard shortcut dispatch (Pin, Navigate, Export, Persist).
   *
   * Extends ui::UIActions because shortcuts trigger actions such as
   * NavigateHistory, ExportToCsv, and PersistSettings.
   */
  class ShortcutHost : public ui::UIActions {
   public:
    ~ShortcutHost() override = default;

    /** Id of the most recently recorded manual-search history entry (empty when none). */
    [[nodiscard]] virtual std::string_view GetLastHistoryId() const noexcept = 0;

   protected:
    // Protected default constructor for this interface class, mirroring UIActions.
    ShortcutHost() = default;
  };

  /**
   * @brief Host interface for background crawl and monitoring coordination during per-frame updates.
   */
  class CrawlHost {
   public:
    virtual ~CrawlHost() = default;

    /** True exactly once per crawl-completion edge; clears the one-shot flag. */
    [[nodiscard]] virtual bool CheckAndClearCrawlCompletion() = 0;

    /** Wall-clock time of the last completed crawl (recrawl interval baseline). */
    [[nodiscard]] virtual std::chrono::steady_clock::time_point GetLastCrawlCompletionTime() const = 0;

    /** Whether periodic folder recrawl is armed for this session. */
    [[nodiscard]] virtual bool IsRecrawlEnabled() const = 0;

    /** Queue-depth backlog of the USN monitor (0 on non-Windows stubs). */
    [[nodiscard]] virtual size_t GetUsnQueueSize() const = 0;

    /** Starts a folder recrawl pass (no-op while a build is in flight). */
    virtual void TriggerRecrawl() = 0;

   protected:
    CrawlHost() = default;
  };

  struct UpdateContext {
    GuiState* state;  // NOLINT(readability-identifier-naming)
    const SearchController* search_controller;  // NOLINT(readability-identifier-naming)
    SearchWorker* search_worker;  // NOLINT(readability-identifier-naming)
    FolderSizeAggregator* folder_aggregator;  // NOLINT(readability-identifier-naming)
    FileIndex* file_index;  // NOLINT(readability-identifier-naming)
    size_t current_index_size;  // NOLINT(readability-identifier-naming)
    bool is_index_building;  // NOLINT(readability-identifier-naming)
    ShortcutHost* shortcut_host;  // NOLINT(readability-identifier-naming)
    CrawlHost* crawl_host;  // NOLINT(readability-identifier-naming)
    AppSettings* settings;  // NOLINT(readability-identifier-naming)
  };

  /**
   * @brief Main update function - handles all application logic per frame
   *
   * This function is called once per frame from the main loop. It:
   * - Processes keyboard shortcuts
   * - Updates the search controller (if index is ready)
   * - Performs periodic file index maintenance during idle time
   * - Checks for periodic folder recrawl (if enabled and not searching)
   *
   * @param update_context Update context holding dependencies for this frame
   */
  void Update(UpdateContext update_context);

  /**
   * @brief Check and trigger periodic recrawl if conditions are met
   *
   * Triggers recrawl when: recrawl is enabled, the configured interval
   * has elapsed, and no index build is in progress.
   *
   * @param crawl_host Host interface for crawl status and triggering
   * @param settings   Application settings (recrawl interval)
   * @param is_index_building True if an index build is currently in progress
   */
  void CheckPeriodicRecrawl(CrawlHost &crawl_host,
                            const AppSettings &settings,
                            bool is_index_building);

  /**
   * @brief Whether periodic folder recrawl should arm after an index build completes.
   *
   * Returns true only for folder-crawl sessions with a configured crawl folder.
   * USN-primary sessions must stay false even when settings.crawlFolder is persisted,
   * otherwise CheckPeriodicRecrawl → StartIndexBuilding would wipe the USN index.
   */
  [[nodiscard]] inline bool ShouldEnablePeriodicRecrawl(bool use_usn_monitor,
                                                        bool has_crawl_folder) noexcept {
    return !use_usn_monitor && has_crawl_folder;
  }

  /**
   * @brief Whether USN initial population should block search and instant-search debounce.
   *
   * Crawl-primary sessions (use_usn_monitor == false) must not stay gated on
   * UsnMonitor::IsPopulatingIndex() — bootstrap may have started USN before the
   * user opted into folder crawl, and StartIndexBuilding sets use_usn_monitor_ false.
   */
  [[nodiscard]] inline bool ShouldGateSearchOnUsnPopulation(bool use_usn_monitor,
                                                            bool usn_populating) noexcept {
    return use_usn_monitor && usn_populating;
  }

  /**
   * @brief Whether periodic path-buffer maintenance may run on the UI frame.
   *
   * Maintenance acquires locks on FileIndex and must not run while the index is
   * building, a search is active, or the USN journal queue has pending buffers
   * being drained by the processor thread.
   */
  [[nodiscard]] inline bool ShouldRunPeriodicMaintenance(bool is_index_building,
                                                         bool is_search_busy,
                                                         size_t usn_queue_size) noexcept {
    return !is_index_building && !is_search_busy && usn_queue_size == 0U;
  }

  /**
   * @brief Executes the action for a triggered global shortcut.
   *
   * All preconditions (UI mode, index building, search active / results
   * state) are evaluated here, not in the registry. Public in the namespace
   * so the D7 stub-host tests drive each case without an ImGui frame;
   * production reaches it only through HandleKeyboardShortcuts.
   */
  void DispatchGlobalShortcut(ShortcutAction action,  // NOSONAR(cpp:S107) - Central shortcut dispatcher intentionally aggregates 8 parameters; refactoring into a context struct would not reduce conceptual complexity
                              GuiState& state,
                              AppSettings& settings,
                              const SearchController& search_controller,
                              SearchWorker& search_worker,
                              FolderSizeAggregator* folder_aggregator,
                              bool is_index_building,
                              ShortcutHost& host);

  /**
   * @brief Handle keyboard shortcuts (called from Update)
   *
   * Shortcuts are declared in src/gui/ShortcutRegistry.h (kShortcuts). While a
   * text field has focus, only defs marked text_input_safe fire (uniform rule
   * via ShouldSkipDueToTextInput). Adding a shortcut requires one entry in
   * kShortcuts and one case in DispatchGlobalShortcut (ApplicationLogic.cpp);
   * a new shortcut that must work while typing sets text_input_safe
   * deliberately. HelpWindow renders descriptions automatically from the
   * registry.
   *
   * @param state GUI state (modified by shortcuts)
   * @param search_controller Search controller for triggering manual search
   * @param search_worker Search worker for triggering manual search
   * @param is_index_building True if index is building (disables F5)
   * @param host        Shortcut host for ExportToCsv / UIActions seam
   * @param settings Application settings (used to check UI mode; modified by toggle shortcuts)
   */
  void HandleKeyboardShortcuts(GuiState &state,
                               const SearchController &search_controller,
                               SearchWorker &search_worker,
                               FolderSizeAggregator* folder_aggregator,
                               bool is_index_building,
                               ShortcutHost &host,
                               AppSettings &settings);
}

