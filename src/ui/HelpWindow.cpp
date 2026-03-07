/**
 * @file ui/HelpWindow.cpp
 * @brief Implementation of Help window (keyboard shortcuts) rendering component
 */

#include "ui/HelpWindow.h"

#include "imgui.h"

#include "core/Version.h"
#include "ui/IconsFontAwesome.h"
#include "ui/Theme.h"
#include "ui/UiStyleGuards.h"

namespace ui {

void HelpWindow::Render(bool* p_open) {
  if (p_open == nullptr || !*p_open) {
    return;
  }

  const detail::StyleColorGuard window_bg_guard(ImGuiCol_WindowBg, Theme::Colors::WindowBg);

  const char* window_title = HELP_WINDOW_TITLE_STR;

  const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  constexpr float kPivot = 0.5F;
  const ImVec2 center(main_viewport->WorkPos.x + (main_viewport->WorkSize.x * kPivot),
                      main_viewport->WorkPos.y + (main_viewport->WorkSize.y * kPivot));
  ImGui::SetNextWindowPos(center, ImGuiCond_FirstUseEver, ImVec2(kPivot, kPivot));
  constexpr float kDefaultWidth = 450.0F;
  constexpr float kDefaultHeight = 500.0F;
  ImGui::SetNextWindowSize(ImVec2(kDefaultWidth, kDefaultHeight), ImGuiCond_FirstUseEver);

  // Do not use SetNextWindowClass/NoAutoMerge: keep Help in the main viewport (like Settings).
  // That avoids the font scaling issue when resizing, which only affected Help when it had its own OS window.

  const detail::WindowGuard window_guard(window_title, p_open, ImGuiWindowFlags_None);
  if (window_guard.ShowContent()) {
    // What's new — high-level user-facing features (since 2026-01-20; dates = git commit date; newest first)
    if (ImGui::CollapsingHeader("What's new")) {
      // Use Bullet() + TextWrapped() because BulletText() does not wrap long lines
      ImGui::Bullet();
      ImGui::TextWrapped("2026-03-05: Faster initial index build: reduced allocations and lock contention when crawling; larger MFT buffer on Windows; bulk directory read (getattrlistbulk) on macOS."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-02-21: Results table shortcuts: Enter (open), Ctrl+Enter (reveal), Ctrl+Shift+C (copy path), Tab (focus search); Pin to Quick Access more robust."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-02-21: Settings: Auto button for font size and UI scale from monitor DPI."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-02-21: Ctrl+Shift+P shortcut under Windows to pin selected file or folder to Quick Access."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-02-21: ImGui Test Engine (optional) for regression tests and code coverage."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-02-20: Help window is a normal window (ALT+TAB); Export CSV moved; Ctrl+E / Ctrl+S shortcuts."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-02-20: Settings stored in HOME/.FindHelper with legacy path fallback."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-02-20: Empty state: example click starts search; clearer labels, 50 recent searches, scrollable panel."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-02-18: Folder statistics columns in results table; sort by folder stats; status bar 'file size' label."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-02-18: M/T/U shortcuts: one press (Shift for global)"); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-02-16: Extended mark-mode (dired-style) shortcuts for marked results; help and keyboard popup updated."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-02-15: Marking and bulk operations in results table; path hierarchy indentation (Phase 1)."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-02-14: Export CSV (Downloads/Desktop); themes (Everforest, Dracula, Nord, One Dark, Gruvbox, Catppuccin Mocha)."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-02-14: Total size in status bar for full-index searches; alternating row background; improved drag-and-drop feedback."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-02-13: Export Search Results (CSV) feature added."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-02-09: Multi-level UI mode toggle (Full, Simplified, Minimalistic)."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-02-02: Block search while index is building; 'Loading attributes...' for Size/Last Modified sort; Metrics hidden unless --show-metrics."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-02-02: Windows: start without administrator privileges; elevation prompt when needed."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-02-01: macOS shortcuts (Cmd+F, Cmd+C); Cmd+Enter in results table reveals in Finder; Ctrl+C copy name on all platforms; macOS drag-and-drop for results."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-02-01: Improved Gemini search-config prompt (filter logic, path+name+extensions)."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-01-31: Streaming search results; Size/Last Modified sort order fixed; filter+streaming clear and filtered count."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-01-31: Path pattern pp:**/ now matches against full path (e.g. pp:**/imgui/**)."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-01-26: Full Path column width calculation fixed after column reordering."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-01-23: OneDrive file size handling (MFT/sentinel, lazy loading)."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Bullet();
      ImGui::TextWrapped("2026-01-22: Simplified UI setting added."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Spacing();
    }

    ImGui::Spacing();

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API uses varargs intentionally
    ImGui::Text("Keyboard Shortcuts");
    ImGui::Separator();

    ImGui::Spacing();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg,readability-magic-numbers) - ImGui color values  // NOSONAR(cpp:S125)
    ImGui::TextColored(Theme::Colors::Success, "Global Shortcuts");
#ifdef __APPLE__
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Cmd+F - Focus name search input");
#else
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Ctrl+F - Focus name search input");
#endif  // __APPLE__  // NOSONAR(cpp:S125) - directive-matching comment, not commented-out code
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("F5 - Refresh search (re-run current query)");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Escape - Clear all filters");
#ifdef __APPLE__
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Cmd+Shift+H - Toggle path hierarchy indentation in results");
#else
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Ctrl+Shift+H - Toggle path hierarchy indentation in results");
#endif  // __APPLE__

  ImGui::Spacing();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg,readability-magic-numbers) - ImGui color values  // NOSONAR(cpp:S125)
  ImGui::TextColored(Theme::Colors::Success, "Search & Navigation");  // NOSONAR(cpp:S125)
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
  ImGui::BulletText("Enter - Trigger search (in any search input field)");
  // Inline "Filter in results" (results-table scoped incremental filter)
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
  ImGui::BulletText("Slash (/) - Start 'Filter in results' in the results table");
#ifdef __APPLE__
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
  ImGui::BulletText("Cmd+G - Cancel active 'Filter in results' and restore selection/scroll");
#else
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
  ImGui::BulletText("Ctrl+G - Cancel active 'Filter in results' and restore selection/scroll");
#endif  // __APPLE__

  ImGui::Spacing();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg,readability-magic-numbers) - ImGui color values  // NOSONAR(cpp:S125)
  ImGui::TextColored(Theme::Colors::Success, "Saved Searches");
#ifdef __APPLE__
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Cmd+S - Save current search (open Save Search dialog)");
#else
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Ctrl+S - Save current search (open Save Search dialog)");
#endif  // __APPLE__

    ImGui::Spacing();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg,readability-magic-numbers) - ImGui color values  // NOSONAR(cpp:S125)
    ImGui::TextColored(Theme::Colors::Success, "File Operations");
#ifdef __APPLE__
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Cmd+C - Copy name (hover over name) or full path (hover over path)");
#else
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Ctrl+C - Copy name (hover over name) or full path (hover over path)");
#endif  // __APPLE__
#ifdef __APPLE__
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Cmd+E - Export current results to CSV");
#else
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Ctrl+E - Export current results to CSV");
#endif  // __APPLE__  // NOSONAR(cpp:S125) - directive-matching comment, not commented-out code
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Delete - Delete selected file (opens confirmation)");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Drag row - Drag and drop file to other applications (Windows & macOS)");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Double-click filename - Open file with default application");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Double-click full path - Open parent folder in Explorer");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Enter - Open selected file or folder");
#ifdef __APPLE__
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Cmd+Enter - Reveal in Explorer (open parent folder, select file)");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Cmd+Shift+C - Copy full path of selected row");
#else
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Ctrl+Enter - Reveal in Explorer (open parent folder, select file)");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Ctrl+Shift+C - Copy full path of selected row");  // NOSONAR(cpp:S125)
#endif  // __APPLE__  // NOSONAR(cpp:S125) - directive-matching comment, not commented-out code
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Tab - Focus name search (from results table)");
#ifdef _WIN32
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Ctrl+Shift+P - Pin selected file or folder to Quick Access");
#endif  // _WIN32

    ImGui::Spacing();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg,readability-magic-numbers) - ImGui color values  // NOSONAR(cpp:S125)
    ImGui::TextColored(Theme::Colors::Success, "Mark Mode (Selection & Bulk Actions)");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("n or Down - Move to next row");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("p or Up - Move to previous row (plain p; Ctrl+Shift+P is Pin on Windows)");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("m - Mark file and move to next row");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("u - Unmark file and move to next row");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("t - Toggle mark and move to next row");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Shift+M - Mark all visible rows");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Shift+T - Invert marks for visible rows");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Shift+U - Unmark all files");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("W - Copy all marked paths to clipboard");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Shift+W - Copy all marked names to clipboard");  // NOSONAR(cpp:S125)
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API  // NOSONAR(cpp:S125)
    ImGui::BulletText("Shift+D - Bulk delete marked files (opens confirmation)");

    ImGui::Spacing();
    ImGui::Separator();
    if (const float kCloseButtonWidth = 120.0F;
        ImGui::Button(ICON_FA_XMARK " Close", ImVec2(kCloseButtonWidth, 0))) {
      *p_open = false;
    }
  }
}

}  // namespace ui
