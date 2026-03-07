/**
 * @file ApplicationLogic.cpp
 * @brief Implementation of application logic for per-frame updates and keyboard shortcuts
 *
 * This module implements the core application logic that runs every frame:
 * - Search controller updates (debounced auto-search, auto-refresh, polling)
 * - Keyboard shortcut handling (Ctrl+F, F5, Escape)
 * - Periodic file index maintenance during idle time
 *
 * The maintenance interval is platform-specific:
 * - Windows: Uses usn_monitor_constants::kMaintenanceCheckIntervalSeconds
 * - macOS: Uses a local constant (5 seconds) to avoid Windows dependencies
 */

#include "core/ApplicationLogic.h"

#include <cassert>
#include <chrono>

#include "imgui.h"

#include "core/Application.h"
#include "core/Settings.h"
#include "gui/GuiState.h"
#include "gui/ImGuiUtils.h"
#include "index/FileIndex.h"
#include "search/SearchController.h"
#include "search/SearchResultsService.h"
#include "search/SearchWorker.h"
#include "usn/UsnMonitor.h"  // For usn_monitor_constants on Windows
#include "utils/Logger.h"
#include "utils/SystemIdleDetector.h"

namespace application_logic {

void HandleKeyboardShortcuts(GuiState &state,
                             const SearchController &search_controller,
                             SearchWorker &search_worker,
                             bool is_index_building,
                             Application &application,
                             AppSettings &settings) {
  // Only process when no text input is active (to avoid stealing input)
  if (!ImGui::GetIO().WantTextInput) {
    const ImGuiIO& io = ImGui::GetIO();

    // Ctrl+F (Windows/Linux) or Cmd+F (macOS): Focus filename search input (disabled when not in Full UI mode)
    if (settings.uiMode == AppSettings::UIMode::Full && IsPrimaryShortcutModifierDown(io) &&
        ImGui::IsKeyPressed(ImGuiKey_F)) {
      state.focusFilenameInput = true;
    }

    // F5: Refresh/re-run current search
    if (ImGui::IsKeyPressed(ImGuiKey_F5) && !is_index_building) {
      search_controller.TriggerManualSearch(state, search_worker, settings);
    }

    // Escape: Clear all filters (skip when inline results filter is active so Esc only cancels that filter)
    if (!state.incrementalSearchActive && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      state.ClearInputs();
      state.timeFilter = TimeFilter::None;
    }

    // Ctrl+S (Windows/Linux) or Cmd+S (macOS): Save current search (open Save Search dialog)
    if (IsPrimaryShortcutModifierDown(io) && ImGui::IsKeyPressed(ImGuiKey_S)) {
      state.openSaveSearchPopup = true;
    }

    // Ctrl+E (Windows/Linux) or Cmd+E (macOS): Export current results to CSV (when results are displayed).
    // application is always valid here (HandleKeyboardShortcuts is only called from Update() with Application& from Application::RunFrame). If the signature is ever changed to Application*, add a null check before calling ExportToCsv. ExportToCsv itself guards against file_index_ being null.
    if (IsPrimaryShortcutModifierDown(io) && ImGui::IsKeyPressed(ImGuiKey_E)) {
      const auto* results = search::SearchResultsService::GetDisplayResults(state);
      if (results != nullptr && !results->empty()) {
        application.ExportToCsv(state);
      }
    }

    // Ctrl+Shift+H (Windows/Linux) or Cmd+Shift+H (macOS): Toggle path hierarchy indentation
    if (IsPrimaryShortcutModifierDown(io) && io.KeyShift &&
        ImGui::IsKeyPressed(ImGuiKey_H)) {
      settings.showPathHierarchyIndentation = !settings.showPathHierarchyIndentation;
      SaveSettings(settings);
    }
  }
}

void Update(GuiState &state,  // NOSONAR(cpp:S107) - Function has 9 parameters: Application update function requires multiple dependencies (GuiState, SearchController, SearchWorker, UsnMonitor, FileIndex, AppSettings, Application, GLFWwindow, ImGuiIO). Refactoring would require creating a context struct, increasing complexity
            const SearchController &search_controller,
            SearchWorker &search_worker,
            FileIndex &file_index,
            const UsnMonitor *monitor,
            bool is_index_building,
            Application &application,
            AppSettings &settings,
            const std::chrono::steady_clock::time_point &last_interaction_time) {
  // Handle keyboard shortcuts
  HandleKeyboardShortcuts(state, search_controller, search_worker, is_index_building, application, settings);

  // Update memory usage every 10 seconds (to avoid costly system calls every frame)
  // Also update immediately if not yet initialized (memory_bytes_ == 0)
  auto now = std::chrono::steady_clock::now();
  // NOLINTNEXTLINE(readability-identifier-naming) - Local constant follows project convention with k prefix
  if (const int kMemoryUpdateIntervalSeconds = 10; state.memory_bytes_ == 0 ||
      std::chrono::duration_cast<std::chrono::seconds>(now - state.last_memory_update_time_).count() >=
      kMemoryUpdateIntervalSeconds) {
    state.memory_bytes_ = Logger::Instance().GetPrivateMemoryBytes();
    state.last_memory_update_time_ = now;
  }

  // Check if crawl just completed and trigger search to refresh UI
  if (application.CheckAndClearCrawlCompletion() &&
      (!state.filenameInput.IsEmpty() || !state.pathInput.IsEmpty() || 
       !state.extensionInput.IsEmpty() || !state.searchResults.empty())) {
    // Crawl just completed - trigger search to refresh UI with new index
    // Only trigger if we have search parameters (user has entered something or has previous search)
    LOG_INFO_BUILD("Crawl completed - triggering search to refresh UI");
    search_controller.TriggerManualSearch(state, search_worker, settings);
  }

  // Update search controller (handles debounced auto-search, auto-refresh,
  // and polling) Pass is_index_building to prevent search during finalization
  search_controller.Update(state, search_worker, monitor, is_index_building, settings, file_index);

  // Periodic maintenance: Rebuild path buffer during idle time
  // Only perform maintenance if search is not active and index is ready
  if (!is_index_building && !search_worker.IsBusy()) {
    static auto last_maintenance_check = std::chrono::steady_clock::now();
    // Check at configured interval during idle time
#ifdef _WIN32
    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_maintenance_check).count() >=
        usn_monitor_constants::kMaintenanceCheckIntervalSeconds) {
      last_maintenance_check = now;
      (void)file_index.Maintain(); // Return value indicates if maintenance was performed
    }
#else
    // macOS: Use same interval (5 seconds) but don't depend on Windows-specific constants
    // NOLINTNEXTLINE(readability-identifier-naming) - Local constant follows project convention with k prefix
    constexpr int kMaintenanceCheckIntervalSeconds = 5;
    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_maintenance_check).count() >=
        kMaintenanceCheckIntervalSeconds) {
      last_maintenance_check = now;
      (void)file_index.Maintain(); // Return value indicates if maintenance was performed
    }
#endif  // _WIN32
  }
  
  // Check for periodic recrawl (only if index is ready and not building)
  CheckPeriodicRecrawl(application,
                       settings,
                       last_interaction_time,
                       application.GetLastCrawlCompletionTime(),
                       application.IsRecrawlEnabled());
}

void CheckPeriodicRecrawl(Application &application,
                            const AppSettings &settings,
                            const std::chrono::steady_clock::time_point &last_interaction_time,
                            const std::chrono::steady_clock::time_point &last_crawl_completion_time,
                            bool recrawl_enabled) {
  // Only check if recrawl is enabled and crawl folder is set
  if (!recrawl_enabled) {
    return;
  }
  
  if (settings.crawlFolder.empty()) {
    return;
  }
  
  // Don't recrawl if crawl is already in progress
  if (application.IsIndexBuilding()) {
    return;
  }
  
  // Check if recrawl interval has elapsed (configurable via settings)
  // Clamp to valid range (should already be validated in Settings.cpp)
  int interval_minutes = settings.recrawl.intervalMinutes;
  if (interval_minutes < settings_defaults::kMinRecrawlIntervalMinutes) {
    interval_minutes = settings_defaults::kMinRecrawlIntervalMinutes;
    LOG_WARNING_BUILD("CheckPeriodicRecrawl: Recrawl interval was < "
                      << settings_defaults::kMinRecrawlIntervalMinutes << " minute, clamped to "
                      << settings_defaults::kMinRecrawlIntervalMinutes << " minute");
  } else if (interval_minutes > settings_defaults::kMaxRecrawlIntervalMinutes) {
    interval_minutes = settings_defaults::kMaxRecrawlIntervalMinutes;
    LOG_WARNING_BUILD("CheckPeriodicRecrawl: Recrawl interval was > "
                      << settings_defaults::kMaxRecrawlIntervalMinutes << " minutes, clamped to "
                      << settings_defaults::kMaxRecrawlIntervalMinutes << " minutes");
  }
  
  const std::chrono::minutes recrawl_interval(interval_minutes);
  auto now = std::chrono::steady_clock::now();
  if (auto elapsed = now - last_crawl_completion_time; elapsed < recrawl_interval) {
    // Interval not elapsed yet - silently return (log removed to avoid output pollution)
    return;  // Not enough time has passed
  }
  
  // Check idle requirement (if enabled)
  if (settings.recrawl.requireIdle) {
    int threshold_minutes = settings.recrawl.idleThresholdMinutes;
    if (threshold_minutes < settings_defaults::kMinRecrawlIdleThresholdMinutes) {
      threshold_minutes = settings_defaults::kMinRecrawlIdleThresholdMinutes;
      LOG_WARNING_BUILD("CheckPeriodicRecrawl: Idle threshold was < "
                        << settings_defaults::kMinRecrawlIdleThresholdMinutes
                        << " minute, clamped to "
                        << settings_defaults::kMinRecrawlIdleThresholdMinutes << " minute");
    } else if (threshold_minutes > settings_defaults::kMaxRecrawlIdleThresholdMinutes) {
      threshold_minutes = settings_defaults::kMaxRecrawlIdleThresholdMinutes;
      LOG_WARNING_BUILD("CheckPeriodicRecrawl: Idle threshold was > "
                        << settings_defaults::kMaxRecrawlIdleThresholdMinutes
                        << " minutes, clamped to "
                        << settings_defaults::kMaxRecrawlIdleThresholdMinutes << " minutes");
    }
    
    const double idle_threshold_seconds = static_cast<double>(threshold_minutes) * 60.0;
    
    // Check if system and process are idle
    const bool system_idle = system_idle_detector::IsSystemIdleFor(idle_threshold_seconds);
    const bool process_idle = system_idle_detector::IsProcessIdleFor(
        last_interaction_time, idle_threshold_seconds);
    
    // Log detailed idle state for debugging (only when not idle to avoid spam)
    // Silently return if idle conditions not met (removed INFO logs to reduce noise)
    // Note: system_idle_seconds < 0.0 indicates system idle detection unavailable,
    // but we still check idle state and return early if not idle
    if (!system_idle || !process_idle) {
      return;
    }
    
    // All conditions met - trigger recrawl (removed INFO log to reduce noise)
  } else {
    // Idle requirement disabled - skip idle checks (removed INFO log to reduce noise)
  }
  
  application.TriggerRecrawl();
}

} // namespace application_logic

