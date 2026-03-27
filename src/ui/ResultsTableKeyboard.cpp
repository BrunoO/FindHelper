#include "ui/ResultsTableKeyboard.h"

#include <chrono>
#include <string>
#include <string_view>

#include "gui/GuiState.h"
#include "gui/ImGuiUtils.h"
#include "imgui.h"
#include "index/FileIndex.h"
#include "platform/FileOperations.h"
#ifdef _WIN32
#include "platform/windows/ShellContextUtils.h"
#endif  // _WIN32
#include "ui/IncrementalSearchState.h"
#include "ui/ResultsTable.h"
#include "ui/Theme.h"
#include "ui/UiStyleGuards.h"
#include "utils/ClipboardUtils.h"
#include "utils/Logger.h"

namespace ui {

namespace {

// Recursively mark or unmark all files and subfolders within a given folder path.
// Performs an O(N) scan of the index to find all descendants.
void MarkAllInFolder(GuiState& state,
                     const FileIndex& file_index,
                     std::string_view folder_path,
                     bool mark) {
  file_index.ForEachEntryWithPath(
    [&state, folder_path, mark](uint64_t id, const FileEntry& /*entry*/, std::string_view path) {
      bool match = (path == folder_path);
      if (!match && path.size() > folder_path.size() &&
          path.substr(0, folder_path.size()) == folder_path) {
        const char next_char = path[folder_path.size()];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by path.size() > folder_path.size() above
        if (next_char == '/' || next_char == '\\') {
          match = true;
        }
      }

      if (match) {
        if (mark) {
          state.markedFileIds.insert(id);
        } else {
          state.markedFileIds.erase(id);
        }
      }
      return true;
    });
}

// T key: invert all marked (shift) or toggle current row and move selection. Extracted to satisfy cpp:S134.
void MarkInvertAll(GuiState& state, const std::vector<SearchResult>& display_results) {
  for (const auto& result : display_results) {
    const uint64_t file_id = result.fileId;
    const auto it = state.markedFileIds.find(file_id);
    if (it != state.markedFileIds.end()) {
      state.markedFileIds.erase(it);
    } else {
      state.markedFileIds.insert(file_id);
    }
  }
}

// Move the single-row selection (and ImGui focus rect) by delta rows (+1 down, -1 up).
// Calls EnsureSomeSelection so N/P work even when nothing is currently selected.
// Clamps silently: no-op at the first/last row.
void MoveSelectionAndFocusInDirection(GuiState& state,
                                      const std::vector<SearchResult>& display_results,
                                      int delta) {
  state.EnsureSomeSelection(display_results);
  const int selected_row = state.GetSelectedRow();
  const int new_row      = selected_row + delta;
  if (new_row < 0 || new_row >= static_cast<int>(display_results.size())) {
    return;
  }
  state.SetSelectedRowAndFocus(new_row);
  state.scrollToSelectedRow = true;
}

// Extend the multi-selection range by delta rows (+1 down, -1 up) from the current
// anchor (GetPrimarySelectedRow). Calls SetSelectionRange to replace the current range
// without touching ImGui's RangeSrcItem, keeping Shift+Arrow anchor consistent.
// Clamps silently: no-op at the first/last row.
void ExtendSelectionInDirection(GuiState& state,
                                const std::vector<SearchResult>& display_results,
                                int delta) {
  state.EnsureSomeSelection(display_results);
  const int selected_row = state.GetSelectedRow();
  const int new_active   = selected_row + delta;
  if (new_active < 0 || new_active >= static_cast<int>(display_results.size())) {
    return;
  }
  const int anchor = state.GetPrimarySelectedRow();
  state.SetSelectionRange((std::min)(anchor, new_active), (std::max)(anchor, new_active),
                          anchor, new_active);
  state.scrollToSelectedRow = true;
}

// Apply a mark (true) or unmark (false) to the currently selected row.
// For directories, marks/unmarks all descendants via MarkAllInFolder.
void ApplyMarkToCurrentRow(GuiState& state,
                           const FileIndex& file_index,
                           const std::vector<SearchResult>& display_results,
                           bool mark) {
  const int selected_row = state.GetSelectedRow();
  if (selected_row < 0 ||
      selected_row >= static_cast<int>(display_results.size())) {
    return;
  }
  const auto& selected_res = display_results[static_cast<size_t>(selected_row)];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by range check above
  if (selected_res.isDirectory) {
    MarkAllInFolder(state, file_index, selected_res.fullPath, mark);
  } else if (mark) {
    state.markedFileIds.insert(selected_res.fileId);
  } else {
    state.markedFileIds.erase(selected_res.fileId);
  }
}

void MarkToggleCurrentAndMove(GuiState& state,
                              const FileIndex& file_index,
                              const std::vector<SearchResult>& display_results) {
  const int selected_row = state.GetSelectedRow();
  if (selected_row < 0 ||
      selected_row >= static_cast<int>(display_results.size())) {
    return;
  }
  const auto& selected_res = display_results[static_cast<size_t>(selected_row)];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by range check above
  const bool currently_marked =
    state.markedFileIds.find(selected_res.fileId) != state.markedFileIds.end();
  ApplyMarkToCurrentRow(state, file_index, display_results, !currently_marked);
  MoveSelectionAndFocusInDirection(state, display_results, +1);
}

void MarkCurrentAndMoveDown(GuiState& state,
                            const FileIndex& file_index,
                            const std::vector<SearchResult>& display_results) {
  ApplyMarkToCurrentRow(state, file_index, display_results, true);
  MoveSelectionAndFocusInDirection(state, display_results, +1);
}

void UnmarkCurrentAndMoveDown(GuiState& state,
                              const FileIndex& file_index,
                              const std::vector<SearchResult>& display_results) {
  ApplyMarkToCurrentRow(state, file_index, display_results, false);
  MoveSelectionAndFocusInDirection(state, display_results, +1);
}

// Debounce interval for mark/unmark key actions (absorbs macOS synthetic key repeats).
constexpr auto kDebounceMs = std::chrono::milliseconds(50);

// State for one-press-per-hold handling of M/T/U. See HandleDebouncedMarkKey.
struct KeyHoldState {
  bool handled = false;
  std::chrono::steady_clock::time_point release_time;
};

// --- Shortcut implementation rules (regression prevention) ---
// 1. One physical press = one action: never trigger on raw IsKeyDown(key) alone.
// 2. One-shot shortcuts: use KeyPressedOnce(key) [= ImGui::IsKeyPressed(key, false)] only.
// 3. M/T/U: use HandleDebouncedMarkKey only (trigger on IsKeyPressed(key, false) + 50ms cooldown after release).
// See docs/design/2026-02-18_RESULTS_TABLE_KEYBOARD_SHORTCUTS.md.

// Escape a single CSV field using standard rules: wrap in double quotes if it contains
// a comma, quote, or newline, and double any embedded quotes.
std::string EscapeCsvField(std::string_view value) {
  bool needs_quotes = false;
  for (const char c : value) {
    if (c == ',' || c == '"' || c == '\n' || c == '\r') {
      needs_quotes = true;
      break;
    }
  }

  if (!needs_quotes) {
    return std::string(value);
  }

  std::string escaped;
  escaped.reserve(value.size() + 2U);
  escaped.push_back('"');
  for (const char c : value) {
    if (c == '"') {
      escaped.push_back('"');
    }
    escaped.push_back(c);
  }
  escaped.push_back('"');
  return escaped;
}

// Build the list of row indices to export as CSV: all marked rows (in visual order) when
// any marks exist; otherwise, all selected rows in visual order (when none marked).
std::vector<int> GetMarkedRowIndices(const GuiState& state,
                                     const std::vector<SearchResult>& display_results) {
  std::vector<int> row_indices;
  row_indices.reserve(display_results.size());

  const auto row_count = static_cast<int>(display_results.size());
  for (int row = 0; row < row_count; ++row) {
    const auto it = state.markedFileIds.find(
      display_results[static_cast<size_t>(row)].fileId);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - row bounded by row_count
    if (it != state.markedFileIds.end()) {
      row_indices.push_back(row);
    }
  }

  return row_indices;
}

std::vector<int> GetUnmarkedSelectionRowIndices(const GuiState& state,
                                                const std::vector<SearchResult>& display_results) {
  std::vector<int> row_indices;
  const auto row_count = static_cast<int>(display_results.size());
  row_indices.reserve(state.GetSelectedRows().size());
  for (const int row : state.GetSelectedRows()) {
    if (row >= 0 && row < row_count) {
      row_indices.push_back(row);
    }
  }
  return row_indices;
}

std::vector<int> BuildCsvRowIndexList(const GuiState& state,
                                      const std::vector<SearchResult>& display_results) {
  if (!state.markedFileIds.empty()) {
    return GetMarkedRowIndices(state, display_results);
  }
  return GetUnmarkedSelectionRowIndices(state, display_results);
}

struct CsvColumnVisibility {
  bool name = true;
  bool size = true;
  bool mod_time = true;
  bool full_path = true;
  bool extension = true;
  bool folder_files = true;
};

void AppendCsvHeaderRow(std::string& csv, const CsvColumnVisibility& vis) {
  bool first = true;
  auto sep = [&csv, &first]() {
    if (!first) {
      csv.push_back(',');
    }
    first = false;
  };
  if (vis.name) {
    sep();
    csv.append(EscapeCsvField("Name"));
  }
  if (vis.size) {
    sep();
    csv.append(EscapeCsvField("Size"));
  }
  if (vis.mod_time) {
    sep();
    csv.append(EscapeCsvField("Last Modified"));
  }
  if (vis.full_path) {
    sep();
    csv.append(EscapeCsvField("Full Path"));
  }
  if (vis.extension) {
    sep();
    csv.append(EscapeCsvField("Extension"));
  }
  if (vis.folder_files) {
    sep();
    csv.append(EscapeCsvField("# Files"));
  }
  csv.push_back('\n');
}

void AppendCsvDataRow(std::string& csv,
                            const SearchResult& result,
                            const CsvColumnVisibility& vis) {
  bool first = true;
  auto sep = [&csv, &first]() {
    if (!first) {
      csv.push_back(',');
    }
    first = false;
  };
  if (vis.name) {
    sep();
    std::string name_field;
    const std::string_view base_name = result.GetFilename();
    const std::string_view ext = result.GetExtension();
    if (!base_name.empty()) {
      name_field.assign(base_name.begin(), base_name.end());
      if (!ext.empty()) {
        name_field.push_back('.');
        name_field.append(ext.begin(), ext.end());
      }
    }
    csv.append(EscapeCsvField(name_field));
  }
  if (vis.size) {
    sep();
    csv.append(EscapeCsvField(ResultsTable::GetSizeDisplayText(result)));
  }
  if (vis.mod_time) {
    sep();
    csv.append(EscapeCsvField(ResultsTable::GetModTimeDisplayText(result)));
  }
  if (vis.full_path) {
    sep();
    csv.append(EscapeCsvField(result.fullPath));
  }
  if (vis.extension) {
    sep();
    csv.append(EscapeCsvField(result.GetExtension()));
  }
  if (vis.folder_files) {
    sep();
    csv.append(EscapeCsvField(ResultsTable::GetFolderFilesDisplayText(result)));
  }
  csv.push_back('\n');
}

// Build CSV for all marked rows (if any) or all currently selected rows (if none marked).
void CopySelectedOrMarkedRowsAsCsv(const GuiState& state,
                                   const std::vector<SearchResult>& display_results,
                                   GLFWwindow* glfw_window) {
  if (glfw_window == nullptr || display_results.empty()) {
    return;
  }

  const std::vector<int> row_indices = BuildCsvRowIndexList(state, display_results);
  if (row_indices.empty()) {
    return;
  }

  const CsvColumnVisibility vis;

  std::string csv;  // NOLINT(misc-const-correctness) - built incrementally
  AppendCsvHeaderRow(csv, vis);

  const auto row_count = static_cast<int>(display_results.size());
  for (const auto row : row_indices) {
    if (row < 0 || row >= row_count) {
      continue;
    }
    const SearchResult& result =
      display_results[static_cast<size_t>(row)];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by row range check above
    AppendCsvDataRow(csv, result, vis);
  }

  if (!csv.empty()) {
    clipboard_utils::SetClipboardText(glfw_window, csv);
  }
}

// Trigger only on the key-down transition (IsKeyPressed(key, false)) so one physical
// press yields one action; 50ms cooldown after release absorbs OS key-repeat Down events.
template<typename Action>
void HandleDebouncedMarkKey(ImGuiKey key,
                            KeyHoldState& key_state,
                            std::chrono::steady_clock::time_point time_now,
                            const Action& action) {
  if (ImGui::IsKeyPressed(key, false) && !key_state.handled) {
    action();
    key_state.handled = true;
    key_state.release_time = time_now;
  }
  if (ImGui::IsKeyDown(key)) {
    key_state.release_time = time_now;
  } else if (key_state.handled && time_now - key_state.release_time > kDebounceMs) {
    key_state.handled = false;
  }
}

// Render the shared header ("Filter in results: " label + query + match count).
// empty_query_placeholder is shown when the query string is empty.
void RenderIncrementalSearchCommon(const IncrementalSearchState& incremental_search,
                                   const char* empty_query_placeholder) {
  const std::string_view query = incremental_search.Query();
  const int match_count = incremental_search.MatchCount();

  ImGui::TextUnformatted("Filter in results: ");
  ImGui::SameLine();

  if (!query.empty()) {
    const std::string query_text{query.begin(), query.end()};
    ImGui::TextUnformatted(query_text.c_str());
  } else {
    ImGui::TextUnformatted(empty_query_placeholder);
  }

  ImGui::SameLine();

  if (match_count > 0) {
    ImGui::Text("(%d matches)", match_count);
  } else {
    const detail::StyleColorGuard text_color_guard(ImGuiCol_Text, Theme::Colors::Warning);
    ImGui::TextUnformatted("[no matches]");
  }
}

}  // namespace

void RenderIncrementalSearchPrompt(const IncrementalSearchState& incremental_search) {
  RenderIncrementalSearchCommon(incremental_search, "_");
}

void RenderIncrementalSearchFilterStatus(const IncrementalSearchState& incremental_search) {
  RenderIncrementalSearchCommon(incremental_search, "<empty>");
  ImGui::SameLine();
#ifdef __APPLE__
  ImGui::TextUnformatted(
    "- Cmd+G cancels; '/' starts a new filter; Esc clears all filters.");
#else
  ImGui::TextUnformatted(
    "- Ctrl+G cancels; '/' starts a new filter; Esc clears all filters.");
#endif  // __APPLE__
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Handles a cohesive set of keyboard shortcuts; splitting would fragment closely related logic
void HandleResultsTableKeyboardShortcuts(  // NOSONAR(cpp:S3776) - Cohesive shortcut handling; splitting would fragment logic
  GuiState& state,
  const std::vector<SearchResult>& display_results,
  const FileIndex& file_index,
  GLFWwindow* glfw_window,
  bool shift,
  [[maybe_unused]] NativeWindowHandle native_window,
  IncrementalSearchState& incremental_search,
  float current_table_scroll_y,
  const std::vector<SearchResult>& base_results) {
  const ImGuiIO& io = ImGui::GetIO();
  const bool window_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  const bool results_table_keyboard_active = window_focused && !io.WantTextInput;
  if (!results_table_keyboard_active) {
    return;
  }

  // One physical press = one action: use IsKeyPressed(key, false) only (see rules above).
  const auto KeyPressedOnce = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, false); };

  // --- Incremental search activation / deactivation ---
  constexpr int kPrintableAsciiMin = 32;
  constexpr int kPrintableAsciiMax = 126;
  if (incremental_search.IsPromptVisible()) {
    // Character input while search prompt is visible.
    std::string new_query{incremental_search.Query()};
    bool query_changed = false;

    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
      const ImWchar ch = io.InputQueueCharacters[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - bounded by loop condition i < .Size
      if (ch >= kPrintableAsciiMin && ch <= kPrintableAsciiMax) {
        new_query.push_back(static_cast<char>(ch));
        query_changed = true;
      }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true) && !new_query.empty()) {
      new_query.pop_back();
      query_changed = true;
    }

    if (query_changed) {
      int selected_row_for_incremental = state.GetPrimarySelectedRow();
      incremental_search.UpdateQuery(new_query,
                                     base_results,
                                     selected_row_for_incremental,
                                     state.scrollToSelectedRow);
      state.SetSelectedRow(selected_row_for_incremental);
      state.RequestFocusForRow(selected_row_for_incremental);
    }

    if (const int match_count = incremental_search.MatchCount(); match_count > 0) {
      if (KeyPressedOnce(ImGuiKey_DownArrow) || KeyPressedOnce(ImGuiKey_N)) {
        int selected_row_for_incremental = state.GetPrimarySelectedRow();
        incremental_search.NavigateNext(selected_row_for_incremental, state.scrollToSelectedRow);
        state.SetSelectedRow(selected_row_for_incremental);
        state.RequestFocusForRow(selected_row_for_incremental);
      } else if (KeyPressedOnce(ImGuiKey_UpArrow) ||
                 (KeyPressedOnce(ImGuiKey_P) && !IsPrimaryShortcutModifierDown(io))) {
        int selected_row_for_incremental = state.GetPrimarySelectedRow();
        incremental_search.NavigatePrev(selected_row_for_incremental, state.scrollToSelectedRow);
        state.SetSelectedRow(selected_row_for_incremental);
        state.RequestFocusForRow(selected_row_for_incremental);
      }
    }

    if (KeyPressedOnce(ImGuiKey_Enter)) {
      incremental_search.Accept();
    } else {
      const bool cancel_pressed =
        KeyPressedOnce(ImGuiKey_Escape) ||
        (IsPrimaryShortcutModifierDown(io) && KeyPressedOnce(ImGuiKey_G));
      if (cancel_pressed) {
        int selected_row_for_incremental = state.GetPrimarySelectedRow();
        incremental_search.Cancel(selected_row_for_incremental, state.scrollToSelectedRow);
        state.SetSelectedRow(selected_row_for_incremental);
        state.RequestFocusForRow(selected_row_for_incremental);
      }
    }
    // While the prompt is visible, do not fall through to normal shortcuts.
    return;
  }

  // Post-accept cancel: when a filter is active but the prompt is hidden,
  // Cmd/Ctrl+G cancels incremental search and restores selection/scroll.
  if (incremental_search.IsFilterActive() && IsPrimaryShortcutModifierDown(io) &&
      KeyPressedOnce(ImGuiKey_G)) {
    int selected_row_for_incremental = state.GetPrimarySelectedRow();
    incremental_search.Cancel(selected_row_for_incremental, state.scrollToSelectedRow);
    state.SetSelectedRow(selected_row_for_incremental);
    state.RequestFocusForRow(selected_row_for_incremental);
    return;
  }

  bool activate_search = KeyPressedOnce(ImGuiKey_Slash);
  if (!activate_search) {
    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
      const ImWchar ch = io.InputQueueCharacters[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - bounded by loop condition i < .Size
      if (ch == '/') {
        activate_search = true;
        break;
      }
    }
  }

  if (activate_search && !display_results.empty()) {
    incremental_search.Begin(display_results,
                             state.GetPrimarySelectedRow(),
                             current_table_scroll_y,
                             state.resultsBatchNumber);
    // Do not return here; activation is not a shortcut that needs early exit.
  }

  // time_now is used by HandleDebouncedMarkKey for the 50ms debounce (see kDebounceMs above).
  const auto time_now = std::chrono::steady_clock::now();

  // W / Shift+W - Copy marked paths or filenames
  if (KeyPressedOnce(ImGuiKey_W)) {
    if (shift) {
      CopyMarkedFilenamesToClipboard(state, glfw_window, file_index);
    } else {
      CopyMarkedPathsToClipboard(state, glfw_window, file_index);
    }
  }

  // Ctrl+Shift+X - Copy selected or marked rows as CSV (visible columns only).
  if (IsPrimaryShortcutModifierDown(io) && shift && KeyPressedOnce(ImGuiKey_X)) {
    CopySelectedOrMarkedRowsAsCsv(state, display_results, glfw_window);
  }

  // Shift+D - Bulk delete marked files (opens confirmation popup)
  if (KeyPressedOnce(ImGuiKey_D) && shift && !state.markedFileIds.empty()) {
    state.showBulkDeletePopup = true;
  }

  const int primary_row = state.GetPrimarySelectedRow(display_results);
  const bool has_selection = (primary_row >= 0);
  const std::string_view selected_path =
    has_selection ? display_results[static_cast<size_t>(primary_row)].fullPath  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by has_selection check above
                  : std::string_view{};

  // Delete - selected file(s) delete (opens confirmation popup; modal is rendered in ResultsTable)
  if (results_table_keyboard_active && KeyPressedOnce(ImGuiKey_Delete) && has_selection) {
    state.filesToDelete.clear();
    const auto result_count = static_cast<int>(display_results.size());
    for (const int sel_row : state.GetSelectedRows()) {
      if (sel_row >= 0 && sel_row < result_count) {
        state.filesToDelete.emplace_back(
          display_results[static_cast<size_t>(sel_row)].fullPath);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by range check above
      }
    }
    if (!state.filesToDelete.empty()) {
      state.showDeletePopup = true;
      LOG_INFO_BUILD("Opening delete popup for " << state.filesToDelete.size() << " item(s)");
    }
  }

  // Enter / Ctrl+Enter / Ctrl+Shift+C - Open selected, reveal in Explorer, copy path (order: specific before plain Enter).
  if (has_selection && IsPrimaryShortcutModifierDown(io) && !shift &&
      KeyPressedOnce(ImGuiKey_Enter)) {
    file_operations::OpenParentFolder(selected_path);
  } else if (has_selection && IsPrimaryShortcutModifierDown(io) && shift &&
             KeyPressedOnce(ImGuiKey_C)) {
    std::string all_paths;  // NOLINT(misc-const-correctness) - built incrementally
    std::size_t copied_count = 0;
    const auto result_count = static_cast<int>(display_results.size());
    for (const int sel_row : state.GetSelectedRows()) {
      if (sel_row < 0 || sel_row >= result_count) {
        continue;
      }
      const std::string path(
        display_results[static_cast<size_t>(sel_row)].fullPath);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - range guarded above
      if (!file_operations::internal::ValidatePath(path, "CopyPathToClipboard")) {
        continue;
      }
      all_paths.append(path);
      all_paths.push_back('\n');
      ++copied_count;
    }
    if (!all_paths.empty()) {
      if (clipboard_utils::SetClipboardText(glfw_window, all_paths)) {
        LOG_INFO_BUILD("Copied " << copied_count << " path(s) to clipboard (Ctrl/Cmd+Shift+C)");
      }
      state.exportNotification =
        (copied_count > 1U) ? "Paths copied" : "Path copied";
      state.exportErrorMessage = "";
      state.exportNotificationTime = std::chrono::steady_clock::now();
    }
  } else if (has_selection &&
             (KeyPressedOnce(ImGuiKey_Enter) || KeyPressedOnce(ImGuiKey_KeypadEnter)) &&
             !IsPrimaryShortcutModifierDown(io) && !shift) {
    file_operations::OpenFileDefault(selected_path);
  } else if (KeyPressedOnce(ImGuiKey_Tab) && !shift) {
    state.focusFilenameInput = true;
  }

#ifdef _WIN32
  // Ctrl+Shift+P - Pin selected file or folder to Quick Access (Windows only).
  // NativeWindowHandle is HWND on Windows; ShellContextUtils.h and windows.h are included above.
  if (IsPrimaryShortcutModifierDown(io) && shift && KeyPressedOnce(ImGuiKey_P) && has_selection) {
    if (PinToQuickAccess(native_window, selected_path)) {
      state.exportNotification = "Pinned to Quick Access";
      state.exportErrorMessage = "";
    } else {
      state.exportNotification = "";
      state.exportErrorMessage = "Failed to pin to Quick Access (see log for details)";
    }
    state.exportNotificationTime = std::chrono::steady_clock::now();
  }
#endif  // _WIN32

  // M: Shift+M = mark all (always); m = mark current row and move down (when selection exists).
  static KeyHoldState m_state{};
  HandleDebouncedMarkKey(ImGuiKey_M, m_state, time_now,
                         [&state, &file_index, &display_results, shift, has_selection] {
                           if (shift) {
                             for (const auto& result : display_results) {
                               state.markedFileIds.insert(result.fileId);
                             }
                           } else if (has_selection) {
                             MarkCurrentAndMoveDown(state, file_index, display_results);
                           }
                         });

  // T: Shift+T = invert all (always); t = toggle current row and move down (when selection exists).
  static KeyHoldState t_state{};
  HandleDebouncedMarkKey(ImGuiKey_T, t_state, time_now,
                         [&state, &file_index, &display_results, shift, has_selection] {
                           if (shift) {
                             MarkInvertAll(state, display_results);
                           } else if (has_selection) {
                             MarkToggleCurrentAndMove(state, file_index, display_results);
                           }
                         });

  // U: Shift+U = unmark all (always); u = unmark current row and move down (when selection exists).
  static KeyHoldState u_state{};
  HandleDebouncedMarkKey(ImGuiKey_U, u_state, time_now,
                         [&state, &display_results, &file_index, shift, has_selection] {
                           if (shift) {
                             state.markedFileIds.clear();
                           } else if (has_selection) {
                             UnmarkCurrentAndMoveDown(state, file_index, display_results);
                           }
                         });

  // Plain Down/Up and N/P: handled manually because NoAutoSelect prevents ImGui from
  // generating SetRange requests on plain nav. MoveSelectionAndFocusInDirection keeps
  // the focus rect in sync via SetSelectedRowAndFocus + RequestFocusForRow.
  // P without Ctrl is "previous row"; Ctrl+Shift+P is Pin to Quick Access (Windows).
  // Shift+Arrow is intentionally NOT handled here: ImGui's EndMultiSelect generates
  // SetAll(false)+SetRange(RangeSrcItem, nav_dest) for Shift+nav even with NoAutoSelect,
  // and our ApplyMultiSelectRequests applies it correctly. Handling it manually would call
  // RequestFocusForRow → SetKeyboardFocusHere fires as plain nav next frame →
  // apply_to_range_src=true → storage->RangeSrcItem drifts → anchor corrupted.
  // Shift+N/P: ExtendSelectionInDirection calls SetSelectionRange without RequestFocusForRow,
  // so RangeSrcItem is never touched → anchor stays correct for subsequent Shift+Arrow too.
  if (!shift && (KeyPressedOnce(ImGuiKey_DownArrow) || KeyPressedOnce(ImGuiKey_N))) {
    MoveSelectionAndFocusInDirection(state, display_results, +1);
  } else if (shift && KeyPressedOnce(ImGuiKey_N)) {
    ExtendSelectionInDirection(state, display_results, +1);
  } else if (!shift && (KeyPressedOnce(ImGuiKey_UpArrow) ||
                        (KeyPressedOnce(ImGuiKey_P) && !IsPrimaryShortcutModifierDown(io)))) {
    MoveSelectionAndFocusInDirection(state, display_results, -1);
  } else if (shift && (KeyPressedOnce(ImGuiKey_P) && !IsPrimaryShortcutModifierDown(io))) {
    ExtendSelectionInDirection(state, display_results, -1);
  }
}

}  // namespace ui

