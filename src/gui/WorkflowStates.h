#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <string>
#include <vector>

#include "api/GeminiApiUtils.h"
#include "utils/AsyncUtils.h"
#include "utils/HashMapAliases.h"

// Export CSV workflow state: notification banner + export-result modal popup.
// Writers: export service (Application) + RenderExportResultPopup (Popups.cpp).
// GuiState::ClearInputs may only clear notification / errorMessage / notificationTime —
// clearing must not blank an open export modal (see ClearInputs).
struct ExportWorkflowState {
  std::string notification;
  std::string error_message;
  std::chrono::steady_clock::time_point notification_time = std::chrono::steady_clock::now();
  bool show_popup = false;
  bool success = false;
  std::string file_path;
  std::size_t result_count = 0;
};

// Gemini API-assisted search workflow state.
// Writers: AI-assisted search panel (SearchInputsGeminiHelpers) only.
// description_input stays a fixed 512-byte buffer: ImGui::InputTextMultiline requires
// char* (performance-critical, called every frame).
struct GeminiWorkflowState {
  std::array<char, 512> description_input = {};
  bool api_call_in_progress = false;
  std::future<gemini_api_utils::GeminiApiResult> api_future;  // default-constructed; assigned when API call starts
  std::string error_message;
  std::chrono::steady_clock::time_point error_display_time = std::chrono::steady_clock::now();
};

// Initial index build progress shared across all platforms.
// Writer: Application::UpdateIndexBuildState from the per-frame IsIndexBuilding()
// snapshot (IndexBuildState::active alone is insufficient — USN monitor / finalize
// phases are included). Keep in_progress in sync with UIActions::IsIndexBuilding()
// so status bar timing and search gating stay consistent.
struct IndexBuildProgressState {
  bool in_progress = false;     // True while initial index is building
  bool failed = false;          // True if initial index build failed
  size_t entries_processed = 0; // Total entries (files + dirs) processed
  size_t files_processed = 0;   // Files processed during initial build
  size_t dirs_processed = 0;    // Directories processed during initial build
  size_t error_count = 0;       // Errors encountered during build
  std::string status_text;      // Human-readable status for UI
  std::chrono::steady_clock::time_point start_time;  // Start time of current index build
  uint64_t last_duration_ms = 0;  // Duration in milliseconds of last completed index build
  bool has_timing = false;      // True once timing initialized for the current session
};

// Search history panel interaction state. Stable ids (not indices) so mutations
// never silently invalidate them; selected_id is validated each frame.
// Writers: SearchHistoryWindow rendering + rename/delete popups.
struct HistoryInteractionState {
  std::string selected_id;
  std::string pending_rename_id;  // Id of entry awaiting rename; triggers RenderHistoryRenamePopup
  std::string pending_delete_id;  // Id of entry awaiting deletion; triggers RenderHistoryDeletePopup
};

// Cloud-file attribute loading workflow (god-object Phase 6): bundles the
// deferred cloud-file set and its background-loading futures. Cloud files are
// included optimistically in time-filter results until their attributes load
// in the background; completion invalidates the time cache (see
// SearchResultUtils::CleanUpCloudFutures).
struct CloudFileWorkflowState {
  // File IDs of cloud files being loaded asynchronously; hash_set_t for FAST_LIBS_BOOST.
  hash_set_t<uint64_t> deferred_ids;
  // Futures for background cloud file attribute loading.
  std::vector<std::future<void>> loading_futures;

  // Enqueue a file ID for deferred loading; returns true if newly enqueued.
  bool TryEnqueue(uint64_t file_id) { return deferred_ids.insert(file_id).second; }
  // Clear the deferred set only — in-flight futures keep running and are
  // self-drained by CleanUpCloudFutures on subsequent frames.
  void ClearDeferred() { deferred_ids.clear(); }
  // Teardown/reset path: wait for every in-flight future, release it, and
  // clear both the futures vector and the deferred set. Blocking wait is
  // intentional and matches the pre-refactor ClearInputs/Stop cleanup.
  void WaitAndDrain() {
    for (auto& future : loading_futures) {
      async_utils::SafeWaitFuture(future);
    }
    loading_futures.clear();
    deferred_ids.clear();
  }
};

// Input-trigger and debounce state (god-object Phase 8): the search-trigger
// toggles plus the per-frame debounce bookkeeping. Single writer: input
// widgets (SearchInputs) and preset/config appliers.
struct InputDebounceState {
  // Search-as-you-type (with debounce) - default off.
  bool instant_search = false;
  // Re-run search when the index changes.
  bool auto_refresh = false;
  // Last keystroke timestamp; SearchController uses it for the debounce window.
  std::chrono::steady_clock::time_point last_input_time = std::chrono::steady_clock::now();
  // Debounce bookkeeping: true while the user is typing (consumed per frame by SearchController).
  bool input_changed = false;
  // One-shot: set to focus the filename input next frame (consumed by the widget itself).
  bool focus_filename_input = false;
};

// UI visibility/latch state (god-object Phase 8): panel expansion
// toggles, help/popup visibility and context-menu latch. Each field's sole
// writer is the widget(s) rendered from that same domain (see ownership map
// "UI visibility" row).
struct UiVisibilityState {
  // Tracks whether the Manual Search section is expanded (stored explicitly to
  // avoid relying on ImGui internal state across layout changes).
  bool manual_search_expanded = true;
  // Tracks whether the AI-Assisted Search section is expanded (collapsed by default).
  bool ai_search_expanded = false;
  // Quick Filters / Last Modified sections visibility (hidden by default).
  bool show_quick_filters = false;
  // True when the inline "Filter in results" prompt is visible; when set,
  // ApplicationLogic skips Escape "Clear all filters" so Esc cancels the inline filter.
  bool incremental_search_active = false;
  // Regex generator popup visibility (normal + filename-prefill variant).
  bool open_regex_generator_popup = false;
  bool open_regex_generator_popup_filename = false;
  // Context-menu latch + debounce timestamp (prevents multiple opens per press).
  bool context_menu_open = false;
  std::chrono::steady_clock::time_point last_context_menu_time = std::chrono::steady_clock::now();
  // Help / search-syntax window visibility flags.
  bool show_help_window = false;
  bool show_search_help_window = false;
};
