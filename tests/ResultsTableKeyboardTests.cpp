/**
 * @file ResultsTableKeyboardTests.cpp
 * @brief Characterization/regression tests for the results-table keyboard dispatcher.
 *
 * Drives ui::HandleResultsTableKeyboardShortcuts headlessly through a real ImGui
 * context: key/modifier/character events are queued per frame exactly like a
 * backend would, and the handler runs inside a focused window like production.
 * OS integration points (clipboard, file open, marked-paths copy, Quick Access
 * pin, display-text formatters) are stubbed in this TU to capture calls.
 *
 * These tests pin the dispatcher's load-bearing invariants (see
 * docs/design/RESULTS_TABLE_KEYBOARD_SHORTCUTS.md):
 *  - window-focus and WantTextInput guards
 *  - one physical press = one action (no OS-repeat activation)
 *  - M/T/U 50ms release cooldown
 *  - section order: incremental prompt consumes the frame; other families run
 *    after it in the original monolithic evaluation order
 */

#define DOCTEST_CONFIG_IMPLEMENT

#include <chrono>
#include <deque>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "doctest/doctest.h"
#include "gui/GuiState.h"
#include "imgui.h"
#include "index/FileIndex.h"
#include "platform/FileOperations.h"
#include "search/SearchTypes.h"
#include "ui/IncrementalSearchState.h"
#include "ui/ResultsTable.h"
#include "ui/ResultsTableKeyboard.h"
#include "ui/Theme.h"
#include "utils/ClipboardUtils.h"
#include "utils/PlatformTypes.h"
#include "utils/ThreadUtils.h"
#ifdef _WIN32
#include "platform/windows/ShellContextUtils.h"
#endif  // _WIN32

int main(int argc, char** argv) {
    MarkCurrentThreadAsUI();
    doctest::Context context(argc, argv);
    return context.run();
}

namespace {

// Captures every OS-integration call made by the handler during one test case.
// NOLINTBEGIN(readability-identifier-naming) - test stub recorder; readable field names
struct StubCalls {
  int copy_marked_paths = 0;
  int copy_marked_filenames = 0;
  std::string clipboard_text;
  int clipboard_sets = 0;
  std::string opened_default;
  std::string opened_parent;
  int open_default_calls = 0;
  int open_parent_calls = 0;
#ifdef _WIN32
  int pin_calls = 0;
  bool pin_result = true;
#endif  // _WIN32
};
// NOLINTEND(readability-identifier-naming)

StubCalls& Stubs() {
  static StubCalls stubs;
  return stubs;
}

}  // namespace

// --- Stub definitions for symbols the linked object libraries do not provide ---

namespace ui {

void CopyMarkedPathsToClipboard(GuiState& /*state*/, GLFWwindow* /*glfw_window*/,
                                const FileIndex& /*file_index*/) {
  Stubs().copy_marked_paths++;
}

void CopyMarkedFilenamesToClipboard(GuiState& /*state*/, GLFWwindow* /*glfw_window*/,
                                    const FileIndex& /*file_index*/) {
  Stubs().copy_marked_filenames++;
}

// Referenced by the CSV export helpers (AppendCsvDataRow) in ResultsTableKeyboard.cpp.
const char* ResultsTable::GetSizeDisplayText(const SearchResult& /*result*/) { return "0 B"; }
const char* ResultsTable::GetModTimeDisplayText(const SearchResult& /*result*/) { return ""; }
const char* ResultsTable::GetFolderFilesDisplayText(const SearchResult& /*result*/) { return ""; }

// Referenced by RenderIncrementalSearchCommon's [no matches] style guard.
ImVec4 Theme::Colors::Warning = ImVec4(0.85F, 0.65F, 0.20F, 1.00F);

}  // namespace ui

namespace clipboard_utils {

bool SetClipboardText(GLFWwindow* /*window*/, std::string_view text) {
  Stubs().clipboard_text.assign(text);
  Stubs().clipboard_sets++;
  return true;
}

}  // namespace clipboard_utils

namespace file_operations {

void OpenFileDefault(std::string_view full_path) {
  Stubs().opened_default.assign(full_path);
  Stubs().open_default_calls++;
}

void OpenParentFolder(std::string_view full_path) {
  Stubs().opened_parent.assign(full_path);
  Stubs().open_parent_calls++;
}

}  // namespace file_operations

#ifdef _WIN32
bool PinToQuickAccess(HWND /*hwnd*/, std::string_view /*path*/) {
  Stubs().pin_calls++;
  return Stubs().pin_result;
}
#endif  // _WIN32

namespace {

class KeyboardHarness {
 public:
  // Non-null stand-in for the GLFW window handle: the dispatcher only forwards it
  // to clipboard/file-integration stubs, which never dereference it. CSV export
  // guards on a non-null window (mirrors production), so nullptr must not be used.
  static GLFWwindow* FakeWindow() {
    static char storage = 0;
    return reinterpret_cast<GLFWwindow*>(&storage);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) - opaque handle stand-in, never dereferenced
  }

  KeyboardHarness() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800.0F, 600.0F);
    // Headless: no renderer backend exists, so build the font atlas once up
    // front — ImGui 1.92 asserts TexIsBuilt in NewFrame otherwise.
    if (io.Fonts->Fonts.empty()) {
      io.Fonts->AddFontDefault();
    }
    unsigned char* font_pixels = nullptr;
    int font_width = 0;
    int font_height = 0;
    io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
    static_cast<void>(font_pixels);
    Stubs() = StubCalls{};
    AddRow("/results/alpha.txt", 101);
    AddRow("/results/beta.txt", 102);
    AddRow("/results/gamma.txt", 103);
    state_.selection.SetSelectedRow(0);
    ResetMarkKeyDebounce();
  }

  ~KeyboardHarness() {
    ImGui::DestroyContext();
  }

  KeyboardHarness(const KeyboardHarness&) = delete;
  KeyboardHarness& operator=(const KeyboardHarness&) = delete;

  // Owns path bytes in a deque so string_views in rows stay stable.
  void AddRow(std::string path, uint64_t file_id, bool is_directory = false) {
    paths_.push_back(std::move(path));
    const std::string& stored = paths_.back();
    SearchResult row;
    row.fullPath = std::string_view(stored);
    row.fileId = file_id;
    row.isDirectory = is_directory;
    const std::size_t sep = stored.find_last_of("/\\");
    row.filename_offset = (sep == std::string::npos) ? 0U : sep + 1U;
    const std::size_t dot = stored.find_last_of('.');
    row.extension_offset =
      (dot != std::string::npos && dot > row.filename_offset) ? dot + 1U : std::string_view::npos;
    rows_.push_back(row);
  }

  // One ImGui frame: queue backend events, run the dispatcher in a window
  // context, then Render() to finish the frame and flush the event queue.
  void RunFrame(const std::vector<std::pair<ImGuiKey, bool>>& keys = {},
                const std::string& chars = "",
                bool focus_window = true) {
    ImGuiIO& io = ImGui::GetIO();
    for (const auto& [key, down] : keys) {
      io.AddKeyEvent(key, down);
    }
    for (const char c : chars) {
      io.AddInputCharacter(static_cast<unsigned char>(c));
    }
    ImGui::NewFrame();
    // io.WantTextInput is a derived per-frame flag (cleared each NewFrame, set by
    // text inputs during the frame) — emulate an active text input mid-frame.
    io.WantTextInput = want_text_input_;
    ImGui::Begin("ResultsTableTest");
    if (focus_window) {
      ImGui::SetWindowFocus();
    } else {
      ImGui::SetWindowFocus(nullptr);
    }
    // Mirrors the production call site: shift is read from io at call time.
    const bool shift = io.KeyShift;
    ui::HandleResultsTableKeyboardShortcuts(state_, rows_, file_index_, FakeWindow(), shift,
                                            NativeWindowHandle{}, incremental_search_, 0.0F, rows_);
    ImGui::End();
    ImGui::Render();
  }

  void Press(ImGuiKey key) {
    RunFrame({{key, true}});
    RunFrame({{key, false}});
  }

  // Modifiers reach io.KeyShift/io.KeyCtrl only via virtual mod-key events
  // (ImGuiMod_*) — same mechanism the GLFW backend uses (ImGui_ImplGlfw_UpdateKeyModifiers).
  void PressWithShift(ImGuiKey key) {
    RunFrame({{ImGuiKey(ImGuiMod_Shift), true}, {key, true}});
    RunFrame({{ImGuiKey(ImGuiMod_Shift), false}, {key, false}});
  }

  void PressWithCtrl(ImGuiKey key) {
    RunFrame({{ImGuiKey(ImGuiMod_Ctrl), true}, {key, true}});
    RunFrame({{ImGuiKey(ImGuiMod_Ctrl), false}, {key, false}});
  }

  void PressWithCtrlShift(ImGuiKey key) {
    RunFrame({{ImGuiKey(ImGuiMod_Ctrl), true}, {ImGuiKey(ImGuiMod_Shift), true}, {key, true}});
    RunFrame({{ImGuiKey(ImGuiMod_Ctrl), false}, {ImGuiKey(ImGuiMod_Shift), false}, {key, false}});
  }

  // Clears the file-local M/T/U debounce state: one frame without keys, a
  // sleep past the 50ms cooldown, and a second frame to flip handled=false.
  void ResetMarkKeyDebounce() {
    RunFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    RunFrame();
  }

  // NOLINTBEGIN(readability-identifier-naming) - test harness accessors
  GuiState& state() { return state_; }
  std::vector<SearchResult>& rows() { return rows_; }
  ui::IncrementalSearchState& incremental() { return incremental_search_; }
  void SetWantTextInput(bool on) { want_text_input_ = on; }
  // NOLINTEND(readability-identifier-naming)

 private:
  GuiState state_;
  std::vector<SearchResult> rows_;
  std::deque<std::string> paths_;
  FileIndex file_index_;
  ui::IncrementalSearchState incremental_search_;
  bool want_text_input_ = false;
};

}  // namespace

TEST_SUITE("ResultsTableKeyboard Shortcuts") {

TEST_CASE("FocusGuard_ShortcutsIgnoredWhenWindowNotFocused") {
  KeyboardHarness h;
  h.RunFrame({{ImGuiKey_M, true}}, "", /*focus_window=*/false);
  h.RunFrame({{ImGuiKey_M, false}}, "", /*focus_window=*/false);
  CHECK(h.state().selection.markedFileIds.empty());
  CHECK(h.state().selection.GetSelectedRow() == 0);
}

TEST_CASE("FocusGuard_ShortcutsIgnoredWhileWantTextInput") {
  KeyboardHarness h;
  h.SetWantTextInput(true);
  h.Press(ImGuiKey_M);
  h.SetWantTextInput(false);
  CHECK(h.state().selection.markedFileIds.empty());
  CHECK(h.state().selection.GetSelectedRow() == 0);
}

TEST_CASE("MarkKey_MarksSelectedRowAndMovesDown") {
  KeyboardHarness h;
  h.Press(ImGuiKey_M);
  CHECK(h.state().selection.markedFileIds.count(101) == 1);
  CHECK(h.state().selection.GetSelectedRow() == 1);
}

TEST_CASE("MarkKey_HoldProducesExactlyOneAction") {
  KeyboardHarness h;
  h.RunFrame({{ImGuiKey_M, true}});
  h.RunFrame();  // still down, no repeat event
  h.RunFrame();
  h.RunFrame({{ImGuiKey_M, false}});
  CHECK(h.state().selection.markedFileIds.size() == 1);
  CHECK(h.state().selection.GetSelectedRow() == 1);
}

TEST_CASE("MarkKey_SecondPressInsideCooldownSwallowed_AfterCooldownFires") {
  KeyboardHarness h;
  h.Press(ImGuiKey_M);  // marks row 0, selection moves to 1
  h.Press(ImGuiKey_M);  // immediate re-press inside 50ms cooldown: ignored
  CHECK(h.state().selection.markedFileIds.size() == 1);
  CHECK(h.state().selection.GetSelectedRow() == 1);
  h.ResetMarkKeyDebounce();
  h.Press(ImGuiKey_M);  // marks row 1, selection moves to 2
  CHECK(h.state().selection.markedFileIds.size() == 2);
  CHECK(h.state().selection.markedFileIds.count(102) == 1);
  CHECK(h.state().selection.GetSelectedRow() == 2);
}

TEST_CASE("ShiftMark_MarksAllVisibleRows") {
  KeyboardHarness h;
  h.PressWithShift(ImGuiKey_M);
  CHECK(h.state().selection.markedFileIds.size() == 3);
}

TEST_CASE("ToggleKey_TogglesCurrentAndInvertsAll") {
  KeyboardHarness h;
  h.state().selection.markedFileIds.insert(101);
  h.Press(ImGuiKey_T);  // current row marked -> unmark and move
  CHECK(h.state().selection.markedFileIds.empty());
  CHECK(h.state().selection.GetSelectedRow() == 1);
  h.ResetMarkKeyDebounce();     // let the T cooldown expire before the next press
  h.PressWithShift(ImGuiKey_T);  // invert: all three become marked
  CHECK(h.state().selection.markedFileIds.size() == 3);
}

TEST_CASE("UnmarkKey_UnmarksCurrentAndClearsAll") {
  KeyboardHarness h;
  h.PressWithShift(ImGuiKey_M);  // mark all
  h.state().selection.SetSelectedRow(1);
  h.Press(ImGuiKey_U);           // unmark current row and move
  CHECK(h.state().selection.markedFileIds.count(102) == 0);
  CHECK(h.state().selection.markedFileIds.size() == 2);
  h.ResetMarkKeyDebounce();
  h.PressWithShift(ImGuiKey_U);  // unmark all
  CHECK(h.state().selection.markedFileIds.empty());
}

TEST_CASE("Navigation_ArrowAndNPKeysMoveWithClamping") {
  KeyboardHarness h;
  h.Press(ImGuiKey_UpArrow);   // clamp at top
  CHECK(h.state().selection.GetSelectedRow() == 0);
  h.Press(ImGuiKey_DownArrow);
  CHECK(h.state().selection.GetSelectedRow() == 1);
  h.Press(ImGuiKey_N);
  CHECK(h.state().selection.GetSelectedRow() == 2);
  h.Press(ImGuiKey_N);         // clamp at bottom
  CHECK(h.state().selection.GetSelectedRow() == 2);
  h.Press(ImGuiKey_P);
  CHECK(h.state().selection.GetSelectedRow() == 1);
}

TEST_CASE("Navigation_ShiftNExtendsSelectionFromAnchor") {
  KeyboardHarness h;
  h.PressWithShift(ImGuiKey_N);
  CHECK(h.state().selection.GetSelectedRows().size() == 2);
  CHECK(h.state().selection.IsRowSelected(0));
  CHECK(h.state().selection.IsRowSelected(1));
}

TEST_CASE("IncrementalSearch_SlashActivatesPromptOnlyWithResults") {
  KeyboardHarness h;
  h.Press(ImGuiKey_Slash);
  CHECK(h.incremental().IsPromptVisible());
}

TEST_CASE("IncrementalSearch_EmptyResultsSlashesDoesNotActivate") {
  KeyboardHarness h;
  h.rows().clear();
  h.Press(ImGuiKey_Slash);
  CHECK_FALSE(h.incremental().IsPromptVisible());
}

TEST_CASE("IncrementalSearch_TypingFilters_EnterAccepts_CtrlGGcancels") {
  KeyboardHarness h;
  h.Press(ImGuiKey_Slash);
  h.RunFrame({}, "be");
  REQUIRE(h.incremental().Query() == "be");
  h.Press(ImGuiKey_Enter);  // accept
  CHECK_FALSE(h.incremental().IsPromptVisible());
  CHECK(h.incremental().IsFilterActive());
  h.PressWithCtrl(ImGuiKey_G);  // post-accept cancel consumes the frame
  CHECK_FALSE(h.incremental().IsFilterActive());
}

TEST_CASE("IncrementalSearch_PromptConsumesFrame_EscCancels") {
  KeyboardHarness h;
  h.Press(ImGuiKey_Slash);
  h.RunFrame({}, "m");  // 'm' must go to the query, not the mark-key section
  CHECK(h.incremental().Query() == "m");
  CHECK(h.state().selection.markedFileIds.empty());
  h.Press(ImGuiKey_Escape);
  CHECK_FALSE(h.incremental().IsPromptVisible());
}

TEST_CASE("Copy_WAndShiftWDispatchToMarkedCopiers") {
  KeyboardHarness h;
  h.Press(ImGuiKey_W);
  CHECK(Stubs().copy_marked_paths == 1);
  CHECK(Stubs().copy_marked_filenames == 0);
  h.PressWithShift(ImGuiKey_W);
  CHECK(Stubs().copy_marked_filenames == 1);
}

TEST_CASE("Copy_CtrlShiftX_PutsCsvOfMarkedRowsOnClipboard") {
  KeyboardHarness h;
  h.state().selection.markedFileIds.insert(101);
  h.PressWithCtrlShift(ImGuiKey_X);
  REQUIRE(Stubs().clipboard_sets == 1);
  CHECK(Stubs().clipboard_text.find("Name,Size,Last Modified,Full Path,Extension,# Files") !=
        std::string::npos);
  CHECK(Stubs().clipboard_text.find("/results/alpha.txt") != std::string::npos);
  CHECK(h.state().export_workflow.notification == "Row copied as CSV");
}

TEST_CASE("BulkDelete_ShiftDOpensPopupOnlyWithMarks") {
  KeyboardHarness h;
  h.PressWithShift(ImGuiKey_D);
  CHECK_FALSE(h.state().selection.showBulkDeletePopup);
  h.state().selection.markedFileIds.insert(101);
  h.PressWithShift(ImGuiKey_D);
  CHECK(h.state().selection.showBulkDeletePopup);
}

TEST_CASE("Delete_QueuesSelectedRowsAndOpensPopup") {
  KeyboardHarness h;
  h.state().selection.SetSelectionRange(0, 2, 0, 2);
  h.Press(ImGuiKey_Delete);
  CHECK(h.state().selection.filesToDelete.size() == 3);
  CHECK(h.state().selection.showDeletePopup);
}

TEST_CASE("Delete_WithoutSelectionIsNoOp") {
  KeyboardHarness h;
  h.state().selection.ClearSelection();
  h.Press(ImGuiKey_Delete);
  CHECK(h.state().selection.filesToDelete.empty());
  CHECK_FALSE(h.state().selection.showDeletePopup);
}

TEST_CASE("Open_PlainEnterOpensFile_CtrlEnterRevealsParent") {
  KeyboardHarness h;
  h.Press(ImGuiKey_Enter);
  CHECK(Stubs().open_default_calls == 1);
  CHECK(Stubs().opened_default == "/results/alpha.txt");
  h.PressWithCtrl(ImGuiKey_Enter);
  CHECK(Stubs().open_parent_calls == 1);
  CHECK(Stubs().opened_parent == "/results/alpha.txt");
}

TEST_CASE("Open_EnterWithoutSelectionIsNoOp") {
  KeyboardHarness h;
  h.state().selection.ClearSelection();
  h.Press(ImGuiKey_Enter);
  CHECK(Stubs().open_default_calls == 0);
}

TEST_CASE("CopyPaths_CtrlShiftC_CopiesAllSelectedRowsWithNotification") {
  KeyboardHarness h;
  h.state().selection.SetSelectionRange(0, 2, 0, 2);
  h.PressWithCtrlShift(ImGuiKey_C);
  CHECK(Stubs().clipboard_sets == 1);
  CHECK(Stubs().clipboard_text.find("/results/alpha.txt") != std::string::npos);
  CHECK(Stubs().clipboard_text.find("/results/gamma.txt") != std::string::npos);
  CHECK(h.state().export_workflow.notification == "Paths copied");
}

TEST_CASE("Focus_TabFocusesFilenameInput") {
  KeyboardHarness h;
  h.Press(ImGuiKey_Tab);
  CHECK(h.state().input_debounce.focus_filename_input);
}

#ifdef _WIN32
TEST_CASE("Pin_CtrlShiftPPinsAndConsumesPrevRowShortcut") {
  KeyboardHarness h;
  h.PressWithCtrlShift(ImGuiKey_P);
  CHECK(Stubs().pin_calls == 1);
  CHECK(h.state().export_workflow.notification == "Pinned to Quick Access");
  // P with Ctrl must NOT act as "previous row" navigation.
  CHECK(h.state().selection.GetSelectedRow() == 0);
}
#endif  // _WIN32

}  // TEST_SUITE
