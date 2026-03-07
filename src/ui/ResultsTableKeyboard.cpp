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
        const char next_char = path[folder_path.size()];
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

void MoveSelectionDownIfPossible(GuiState& state,
                                 const std::vector<SearchResult>& display_results) {
  if (state.selectedRow < static_cast<int>(display_results.size()) - 1) {
    ++state.selectedRow;
    state.scrollToSelectedRow = true;
  }
}

void MarkToggleCurrentAndMove(GuiState& state,
                              const FileIndex& file_index,
                              const std::vector<SearchResult>& display_results) {
  if (const auto& selected_res =
        display_results[static_cast<size_t>(state.selectedRow)];
      selected_res.isDirectory) {
    const bool currently_marked =
      state.markedFileIds.find(selected_res.fileId) != state.markedFileIds.end();
    MarkAllInFolder(state, file_index, selected_res.fullPath, !currently_marked);
  } else {
    const auto it = state.markedFileIds.find(selected_res.fileId);
    if (it != state.markedFileIds.end()) {
      state.markedFileIds.erase(it);
    } else {
      state.markedFileIds.insert(selected_res.fileId);
    }
  }
  MoveSelectionDownIfPossible(state, display_results);
}

void MarkCurrentAndMoveDown(GuiState& state,
                            const FileIndex& file_index,
                            const std::vector<SearchResult>& display_results) {
  if (const auto& selected_res =
        display_results[static_cast<size_t>(state.selectedRow)];
      selected_res.isDirectory) {
    MarkAllInFolder(state, file_index, selected_res.fullPath, true);
  } else {
    state.markedFileIds.insert(selected_res.fileId);
  }
  MoveSelectionDownIfPossible(state, display_results);
}

void UnmarkCurrentAndMoveDown(GuiState& state,
                              const FileIndex& file_index,
                              const std::vector<SearchResult>& display_results) {
  if (const auto& selected_res =
        display_results[static_cast<size_t>(state.selectedRow)];
      selected_res.isDirectory) {
    MarkAllInFolder(state, file_index, selected_res.fullPath, false);
  } else {
    state.markedFileIds.erase(selected_res.fileId);
  }
  MoveSelectionDownIfPossible(state, display_results);
}

}  // namespace

void RenderIncrementalSearchPrompt(const IncrementalSearchState& incremental_search) {
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  const std::string_view query = incremental_search.Query();
  const int match_count = incremental_search.MatchCount();

  ImGui::TextUnformatted("Filter in results: ");
  ImGui::SameLine();

  if (!query.empty()) {
    const std::string query_text{query.begin(), query.end()};
    ImGui::TextUnformatted(query_text.c_str());
  } else {
    ImGui::TextUnformatted("_");
  }

  ImGui::SameLine();

  if (match_count > 0) {
    ImGui::Text("(%d matches)", match_count);  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  } else {
    const detail::StyleColorGuard text_color_guard(ImGuiCol_Text, Theme::Colors::Warning);
    ImGui::TextUnformatted("[no matches]");
  }
}

void RenderIncrementalSearchFilterStatus(const IncrementalSearchState& incremental_search) {
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  const std::string_view query = incremental_search.Query();
  const int match_count = incremental_search.MatchCount();

  ImGui::TextUnformatted("Filter in results: ");
  ImGui::SameLine();

  if (!query.empty()) {
    const std::string query_text{query.begin(), query.end()};
    ImGui::TextUnformatted(query_text.c_str());
  } else {
    ImGui::TextUnformatted("<empty>");
  }

  ImGui::SameLine();

  if (match_count > 0) {
    ImGui::Text("(%d matches)", match_count);  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  } else {
    const detail::StyleColorGuard text_color_guard(ImGuiCol_Text, Theme::Colors::Warning);
    ImGui::TextUnformatted("[no matches]");
  }

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
  const bool window_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  const ImGuiIO& io = ImGui::GetIO();
  if (const bool want_text_input = io.WantTextInput; !window_focused || want_text_input) {
    return;
  }

  // Ignore key repeat so one physical press = one action (avoid marking multiple rows when key is held).
  const auto KeyPressedOnce = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, false); };

  // --- Incremental search activation / deactivation ---
  constexpr int kPrintableAsciiMin = 32;
  constexpr int kPrintableAsciiMax = 126;
  if (incremental_search.IsPromptVisible()) {
    // Character input while search prompt is visible.
    std::string new_query{incremental_search.Query()};
    bool query_changed = false;

    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
      const ImWchar ch = io.InputQueueCharacters[i];
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
      incremental_search.UpdateQuery(new_query,
                                     base_results,
                                     state.selectedRow,
                                     state.scrollToSelectedRow);
    }

    if (const int match_count = incremental_search.MatchCount(); match_count > 0) {
      if (KeyPressedOnce(ImGuiKey_DownArrow) || KeyPressedOnce(ImGuiKey_N)) {
        incremental_search.NavigateNext(state.selectedRow, state.scrollToSelectedRow);
      } else if (KeyPressedOnce(ImGuiKey_UpArrow) ||
                 (KeyPressedOnce(ImGuiKey_P) && !IsPrimaryShortcutModifierDown(io))) {
        incremental_search.NavigatePrev(state.selectedRow, state.scrollToSelectedRow);
      }
    }

    if (KeyPressedOnce(ImGuiKey_Enter)) {
      incremental_search.Accept();
    } else {
      const bool cancel_pressed =
        KeyPressedOnce(ImGuiKey_Escape) ||
        (IsPrimaryShortcutModifierDown(io) && KeyPressedOnce(ImGuiKey_G));
      if (cancel_pressed) {
        incremental_search.Cancel(state.selectedRow, state.scrollToSelectedRow);
      }
    }
    // While the prompt is visible, do not fall through to normal shortcuts.
    return;
  }

  // Post-accept cancel: when a filter is active but the prompt is hidden,
  // Cmd/Ctrl+G cancels incremental search and restores selection/scroll.
  if (incremental_search.IsFilterActive() && IsPrimaryShortcutModifierDown(io) &&
      KeyPressedOnce(ImGuiKey_G)) {
    incremental_search.Cancel(state.selectedRow, state.scrollToSelectedRow);
    return;
  }

  bool activate_search = KeyPressedOnce(ImGuiKey_Slash);
  if (!activate_search) {
    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
      const ImWchar ch = io.InputQueueCharacters[i];
      if (ch == '/') {
        activate_search = true;
        break;
      }
    }
  }

  if (activate_search && !display_results.empty()) {
    incremental_search.Begin(display_results,
                             state.selectedRow,
                             current_table_scroll_y,
                             state.resultsBatchNumber);
    // Do not return here; activation is not a shortcut that needs early exit.
  }

  // Helper to handle debounce for row marking actions (workaround for macOS synthetic key repeats).
  // We use a 50ms threshold after key release before we allow another action from the same physical key.
  // This absorbs synthetic KeyUp/KeyDown pairs injected by the OS during key repeat.
  struct KeyHoldState {
    bool handled = false;
    std::chrono::steady_clock::time_point release_time;
  };
  const auto time_now = std::chrono::steady_clock::now();
  constexpr auto kDebounceMs = std::chrono::milliseconds(50);

  // W / Shift+W - Copy marked paths or filenames
  if (KeyPressedOnce(ImGuiKey_W)) {
    if (shift) {
      CopyMarkedFilenamesToClipboard(state, glfw_window, file_index);
    } else {
      CopyMarkedPathsToClipboard(state, glfw_window, file_index);
    }
  }

  // Shift+D - Bulk delete marked files (opens confirmation popup)
  if (KeyPressedOnce(ImGuiKey_D) && shift && !state.markedFileIds.empty()) {
    state.showBulkDeletePopup = true;
  }

  const bool has_selection =
    state.selectedRow >= 0 &&
    state.selectedRow < static_cast<int>(display_results.size());
  const std::string_view selected_path =
    has_selection ? display_results[static_cast<size_t>(state.selectedRow)].fullPath
                  : std::string_view{};

  // Enter / Ctrl+Enter / Ctrl+Shift+C - Open selected, reveal in Explorer, copy path (order: specific before plain Enter).
  if (has_selection && IsPrimaryShortcutModifierDown(io) && !shift &&
      KeyPressedOnce(ImGuiKey_Enter)) {
    file_operations::OpenParentFolder(selected_path);
  } else if (has_selection && IsPrimaryShortcutModifierDown(io) && shift &&
             KeyPressedOnce(ImGuiKey_C)) {
    file_operations::CopyPathToClipboard(glfw_window, selected_path);
    state.exportNotification = "Path copied";
    state.exportErrorMessage = "";
    state.exportNotificationTime = std::chrono::steady_clock::now();
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
  if (static KeyHoldState m_state{}; ImGui::IsKeyDown(ImGuiKey_M) && !m_state.handled) {
    if (shift) {
      for (const auto& result : display_results) {
        state.markedFileIds.insert(result.fileId);
      }
    } else if (has_selection) {
      MarkCurrentAndMoveDown(state, file_index, display_results);
    }
    m_state.handled = true;
    m_state.release_time = time_now;
  } else if (ImGui::IsKeyDown(ImGuiKey_M)) {
    m_state.release_time = time_now;
  } else if (m_state.handled && time_now - m_state.release_time > kDebounceMs) {
    m_state.handled = false;
  }

  // T: Shift+T = invert all (always); t = toggle current row and move down (when selection exists).
  if (static KeyHoldState t_state{}; ImGui::IsKeyDown(ImGuiKey_T) && !t_state.handled) {
    if (shift) {
      MarkInvertAll(state, display_results);
    } else if (has_selection) {
      MarkToggleCurrentAndMove(state, file_index, display_results);
    }
    t_state.handled = true;
    t_state.release_time = time_now;
  } else if (ImGui::IsKeyDown(ImGuiKey_T)) {
    t_state.release_time = time_now;
  } else if (t_state.handled && time_now - t_state.release_time > kDebounceMs) {
    t_state.handled = false;
  }

  // U: Shift+U = unmark all (always); u = unmark current row and move down (when selection exists).
  if (static KeyHoldState u_state{}; ImGui::IsKeyDown(ImGuiKey_U) && !u_state.handled) {
    if (shift) {
      state.markedFileIds.clear();
    } else if (has_selection) {
      UnmarkCurrentAndMoveDown(state, file_index, display_results);
    }
    u_state.handled = true;
    u_state.release_time = time_now;
  } else if (ImGui::IsKeyDown(ImGuiKey_U)) {
    u_state.release_time = time_now;
  } else if (u_state.handled && time_now - u_state.release_time > kDebounceMs) {
    u_state.handled = false;
  }

  // Arrow key / dired navigation
  // P without Ctrl is "previous row"; Ctrl+Shift+P is Pin to Quick Access (Windows), so exclude Ctrl when treating P as navigation.
  if (KeyPressedOnce(ImGuiKey_DownArrow) || KeyPressedOnce(ImGuiKey_N)) {
    if (state.selectedRow < static_cast<int>(display_results.size()) - 1) {
      ++state.selectedRow;
      state.scrollToSelectedRow = true;
    } else if (state.selectedRow == -1 && !display_results.empty()) {
      state.selectedRow = 0;
      state.scrollToSelectedRow = true;
    }
  } else if ((KeyPressedOnce(ImGuiKey_UpArrow) ||
              (KeyPressedOnce(ImGuiKey_P) && !IsPrimaryShortcutModifierDown(io))) &&
             state.selectedRow > 0) {
    --state.selectedRow;
    state.scrollToSelectedRow = true;
  }
}

}  // namespace ui

