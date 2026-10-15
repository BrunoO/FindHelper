/**
 * @file ApplicationLogic.cpp
 * @brief Implementation of application logic for per-frame updates and keyboard shortcuts
 *
 * This module implements the core application logic that runs every frame:
 * - Search controller updates (debounced auto-search, auto-refresh, polling)
 * - Keyboard shortcut handling (Ctrl+F, F5, Escape)
 * - Periodic file index maintenance during idle time
 */

#include "core/ApplicationLogic.h"

#include <cassert>
#include <chrono>

#include "imgui.h"

#include "core/Settings.h"
#include "gui/GuiState.h"
#include "gui/ShortcutRegistry.h"
#include "index/FileIndex.h"
#include "search/SearchController.h"
#include "search/SearchHistory.h"
#include "search/SearchResultsService.h"
#include "search/SearchWorker.h"
#include "utils/Logger.h"

namespace application_logic {

constexpr int kMaintenanceCheckIntervalSeconds = 5; // Check every N seconds during idle time

// Executes the action for a triggered global shortcut. All preconditions
// (UI mode, index building, search active / results state) are evaluated here, not in the registry.
void DispatchGlobalShortcut(ShortcutAction action,  // NOSONAR(cpp:S107) - Central shortcut dispatcher intentionally aggregates 8 parameters; refactoring into a context struct would not reduce conceptual complexity
                            GuiState& state,
                            AppSettings& settings,
                            const SearchController& search_controller,
                            SearchWorker& search_worker,
                            FolderSizeAggregator* folder_aggregator,
                            bool is_index_building,
                            ShortcutHost& host) {
  switch (action) {
    case ShortcutAction::FocusSearch:
      if (settings.uiMode == AppSettings::UIMode::Full) {
        state.input_debounce.focus_filename_input = true;
      }
      break;
    case ShortcutAction::RefreshSearch:
      if (!is_index_building) {
        search_controller.TriggerManualSearch(state, search_worker, folder_aggregator, settings);
      }
      break;
    case ShortcutAction::ClearFilters:
      // Skip when inline results filter is active so Esc only cancels that filter.
      if (!state.ui_visibility.incremental_search_active) {
        state.ClearInputs();
        state.searchCriteria.time_filter = TimeFilter::None;
      }
      break;
    case ShortcutAction::PinSearch: {
      const std::string_view last_id = host.GetLastHistoryId();
      if (last_id.empty()) {
        state.export_workflow.NotifyError("Nothing to pin yet — run a search first");
        break;
      }
      const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count();
      const auto* const entry = FindHistoryEntryById(settings.searchHistory, last_id);
      if (entry == nullptr) {
        state.export_workflow.NotifyError("Last search is no longer in history — nothing to pin");
        break;
      }
      // Toggle semantics: pin an unpinned entry, unpin a pinned one. The
      // orchestration (headline captured before the toggle, persist, error
      // log, toast) is the shared search-layer function used by the Quick
      // Access card and welcome-panel rows.
      const bool now_pinned = !entry->is_pinned;
      ToggleHistoryPinWithFeedback(*entry, now_pinned, settings, now_ms, &host, state);
      break;
    }
    case ShortcutAction::NavigateBack:
      // Thin composition: all stack mechanics live in search::NavigateHistory
      // (testable in SearchHistory.cpp). Boundary presses are silent no-ops
      // there (§3.1.6). &host is-a ui::UIActions (implements
      // TriggerManualSearch), matching the Phase-A call sites' seam.
      NavigateHistory(-1, state.navigation, state, settings, &host);
      break;
    case ShortcutAction::NavigateForward:
      NavigateHistory(+1, state.navigation, state, settings, &host);
      break;
    case ShortcutAction::ExportCsv: {
      // host is always valid here; ExportToCsv guards against null file_index_ internally.
      if (const auto* const results = search::SearchResultsService::GetDisplayResults(state);
          results != nullptr && !results->empty()) {
        host.ExportToCsv(state);
      }
      break;
    }
    case ShortcutAction::ToggleHierarchy:
      settings.showPathHierarchyIndentation = !settings.showPathHierarchyIndentation;
      if (!host.PersistSettings()) {
        settings.showPathHierarchyIndentation = !settings.showPathHierarchyIndentation;  // Revert
        LOG_ERROR("Failed to persist hierarchy indentation toggle");
      }
      break;
  }
}

void HandleKeyboardShortcuts(GuiState &state,
                             const SearchController &search_controller,
                             SearchWorker &search_worker,
                             FolderSizeAggregator* folder_aggregator,
                             bool is_index_building,
                             ShortcutHost &host,
                             AppSettings &settings) {
  const ImGuiIO& io = ImGui::GetIO();
  for (const auto& def : kShortcuts) {
    if (def.scope != ShortcutScope::Global) {
      continue;
    }
    if (!IsAvailableOnPlatform(def)) {
      continue;
    }
    // Uniform declarative text-input gate: while a text field is focused only
    // defs with text_input_safe = true fire (the Cmd/Ctrl chords + F5 today;
    // bare Esc stays skipped so a field can consume it). Held primary modifiers
    // stop InputText from inserting characters, so the chords are typing-safe.
    if (ShouldSkipDueToTextInput(def, io.WantTextInput)) {
      continue;
    }
    if (!IsTriggered(def, io)) {
      continue;
    }
    DispatchGlobalShortcut(def.action, state, settings, search_controller,
                           search_worker, folder_aggregator, is_index_building, host);
  }
}

void Update(UpdateContext update_context) {
  assert(update_context.state != nullptr);
  assert(update_context.search_controller != nullptr);
  assert(update_context.search_worker != nullptr);
  assert(update_context.file_index != nullptr);
  assert(update_context.host != nullptr);
  assert(update_context.settings != nullptr);

  auto& state = *update_context.state;
  const auto& search_controller = *update_context.search_controller;
  auto& search_worker = *update_context.search_worker;
  auto* const folder_aggregator = update_context.folder_aggregator;
  auto& file_index = *update_context.file_index;
  const size_t current_index_size = update_context.current_index_size;
  const bool is_index_building = update_context.is_index_building;
  auto& host = *update_context.host;
  auto& settings = *update_context.settings;
  // Handle keyboard shortcuts
  HandleKeyboardShortcuts(state, search_controller, search_worker, folder_aggregator, is_index_building, host, settings);

  // Update memory usage every 10 seconds (to avoid costly system calls every frame)
  // Also update immediately if not yet initialized (memory_bytes_ == 0)
  const auto now = std::chrono::steady_clock::now();
  // NOLINTNEXTLINE(readability-identifier-naming) - Local constant follows project convention with k prefix
  if (const int kMemoryUpdateIntervalSeconds = 10; state.memory_bytes_ == 0 ||
      std::chrono::duration_cast<std::chrono::seconds>(now - state.last_memory_update_time_).count() >=
      kMemoryUpdateIntervalSeconds) {
    state.memory_bytes_ = Logger::Instance().GetPrivateMemoryBytes();
    state.last_memory_update_time_ = now;
  }

  // Check if crawl just completed and trigger search to refresh UI
  if (host.CheckAndClearCrawlCompletion() &&
      (state.searchCriteria.HasSearchText() || !state.result_pool_->Results().empty())) {
    // Crawl just completed - trigger search to refresh UI with new index
    // Only trigger if we have search parameters (user has entered something or has previous search)
    LOG_INFO_BUILD("Crawl completed - triggering search to refresh UI");
    search_controller.TriggerManualSearch(state, search_worker, folder_aggregator, settings);
  }

  // Update search controller (handles debounced auto-search, auto-refresh,
  // and polling) Pass is_index_building to prevent search during finalization
  search_controller.Update(state, search_worker, folder_aggregator,
                           {current_index_size, file_index.MutationCount(), is_index_building,
                            host.GetUsnQueueSize(),},
                           settings, file_index);

  // Periodic maintenance: Rebuild path buffer during idle time
  // Only perform maintenance if search is not active and index is ready
  if (!is_index_building && !search_worker.IsBusy()) {
    static auto last_maintenance_check = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_maintenance_check).count() >=
        kMaintenanceCheckIntervalSeconds) {
      last_maintenance_check = now;
      (void)file_index.Maintain(); // Return value indicates if maintenance was performed
    }
  }

  // Check for periodic recrawl (only if index is ready, not building, and not searching)
  if (!search_worker.IsBusy()) {
    CheckPeriodicRecrawl(host,
                         settings,
                         host.GetLastCrawlCompletionTime(),
                         host.IsRecrawlEnabled());
  }
}

void CheckPeriodicRecrawl(ShortcutHost &host,
                          const AppSettings &settings,
                          const std::chrono::steady_clock::time_point &last_crawl_completion_time,
                          bool recrawl_enabled) {
  if (!recrawl_enabled || host.IsIndexBuilding()) {
    return;
  }

  // Clamp interval to valid range (defensive; Settings.cpp enforces this on load)
  if (const int interval_minutes =
        (std::max)(settings_defaults::kMinRecrawlIntervalMinutes,
                   (std::min)(settings.recrawl.intervalMinutes,
                              settings_defaults::kMaxRecrawlIntervalMinutes));
      std::chrono::steady_clock::now() - last_crawl_completion_time <
      std::chrono::minutes(interval_minutes)) {
    return;
  }

  host.TriggerRecrawl();
}

} // namespace application_logic

