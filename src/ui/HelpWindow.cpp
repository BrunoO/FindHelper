/**
 * @file ui/HelpWindow.cpp
 * @brief Implementation of Help window (keyboard shortcuts) rendering component
 */

#include "ui/HelpWindow.h"

#include "imgui.h"

#include "gui/ShortcutRegistry.h"
#include "ui/AboutSectionHelpers.h"
#include "ui/CenteredToolWindow.h"
#include "ui/Theme.h"
#include "ui/UiStyleGuards.h"

namespace ui {

namespace {

// Renders a single shortcut as an ImGui bullet: "Ctrl+Shift+F - Toggle Matched Files / Matched Size columns"
void RenderShortcutBullet(const ShortcutDef& def) {
  const std::string text = FormatLabel(def) + " - " + std::string(def.description);
  ImGui::BulletText("%s", text.c_str());
}

// Renders a bullet point with text wrapping (for long "What's new" entries).
void BulletWrapped(const char* text) {
  ImGui::Bullet();
  ImGui::TextWrapped("%s", text);
}

// Renders a plain-text bullet shortcut entry.
void ShortcutBullet(const char* text) {
  ImGui::BulletText("%s", text);
}

// Renders a green section header preceded by spacing.
void SectionHeader(const char* title) {
  ImGui::Spacing();
  ImGui::TextColored(Theme::Colors::Success, "%s", title);
}

}  // namespace

void HelpWindow::Render(bool* p_open, const size_t* memory_bytes_from_state) {
  if (p_open == nullptr || !*p_open) {
    return;
  }

  const detail::StyleColorGuard window_bg_guard(ImGuiCol_WindowBg, Theme::Colors::WindowBg);

  const char* window_title = GetAboutHelpWindowTitle();

  constexpr float kDefaultWidth = 450.0F;
  constexpr float kDefaultHeight = 500.0F;
  detail::SetupCenteredToolWindow(kDefaultWidth, kDefaultHeight);

  // Do not use SetNextWindowClass/NoAutoMerge: keep Help in the main viewport (like Settings).
  // That avoids the font scaling issue when resizing, which only affected Help when it had its own OS window.

  const detail::WindowGuard window_guard(window_title, p_open, ImGuiWindowFlags_None);
  if (window_guard.ShowContent()) {
    // About — version, build info, and host system characteristics (same kind of info as status bar left group + system)
    if (ImGui::CollapsingHeader("About", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Text("%s", GetAboutAppDisplayName());
      ImGui::Text("Version: v-%s", GetAboutAppVersion());
      ImGui::SameLine();
      ImGui::TextDisabled("%s", GetAboutBuildTypeLabel());
      if (const char pgo_mode = GetAboutPgoMode(); pgo_mode != '\0') {
        ImGui::SameLine();
        ImGui::TextDisabled("[%c]", pgo_mode);
        if (ImGui::IsItemHovered()) {
          if (const char* tooltip = GetAboutPgoTooltip(pgo_mode); tooltip != nullptr) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(tooltip);
            ImGui::EndTooltip();
          }
        }
      }
      ImGui::Spacing();
      ImGui::TextColored(Theme::Colors::TextDim, "%s", GetAboutPlatformMonitoringLabel());
      ImGui::Spacing();
      ImGui::Text("System");
      ImGui::Separator();
      ImGui::Text("Logical processors: %zu", GetAboutLogicalProcessorCount());
      const std::string memory_str = (memory_bytes_from_state != nullptr)
                                         ? GetAboutProcessMemoryDisplayFromBytes(*memory_bytes_from_state)
                                         : GetAboutProcessMemoryDisplay();
      ImGui::Text("Process memory: %s", memory_str.c_str());
      ImGui::Spacing();
    }

    // What's new — high-level user-facing features (last calendar month; dates = git commit date; newest first)
    // Use BulletWrapped() because BulletText() does not wrap long lines
    if (ImGui::CollapsingHeader("What's new")) {
      BulletWrapped("2026-03-13: Fuzzy search for item name and path with 'fz:' prefix (subsequence matching, e.g. fz:fbr matches 'foobar', 'fiber', 'foo/bar'); Search Help updated with syntax and examples.");
      BulletWrapped("2026-03-07: Windows: in-app elevation prompt restored so UAC can run when user chooses restart as administrator.");
      BulletWrapped("2026-03-05: Faster initial index build: reduced allocations and lock contention when crawling; larger MFT buffer on Windows; bulk directory read (getattrlistbulk) on macOS.");
      BulletWrapped("2026-02-21: Results table shortcuts: Enter (open), Ctrl+Enter (reveal), Ctrl+Shift+C (copy path), Tab (focus search); Pin to Quick Access more robust.");
      BulletWrapped("2026-02-21: Settings: Auto button for font size and UI scale from monitor DPI.");
      BulletWrapped("2026-02-21: Ctrl+Shift+P shortcut under Windows to pin selected file or folder to Quick Access.");
      BulletWrapped("2026-02-21: ImGui Test Engine (optional) for regression tests and code coverage.");
      BulletWrapped("2026-02-20: Help window is a normal window (ALT+TAB); Export CSV moved; Ctrl+E / Ctrl+S shortcuts.");
      BulletWrapped("2026-02-20: Settings stored in HOME/.FindHelper with legacy path fallback.");
      BulletWrapped("2026-02-20: Empty state: example click starts search; clearer labels, 50 recent searches, scrollable panel.");
      BulletWrapped("2026-02-18: Folder statistics columns in results table; sort by folder stats; status bar 'file size' label.");
      BulletWrapped("2026-02-18: M/T/U shortcuts: one press (Shift for global)");
      BulletWrapped("2026-02-16: Extended mark-mode (dired-style) shortcuts for marked results; help and keyboard popup updated.");
      BulletWrapped("2026-02-15: Marking and bulk operations in results table; path hierarchy indentation (Phase 1).");
      BulletWrapped("2026-02-14: Export CSV (Downloads/Desktop); themes (Everforest, Dracula, Nord, One Dark, Gruvbox, Catppuccin Mocha).");
      BulletWrapped("2026-02-14: Total size in status bar for full-index searches; alternating row background; improved drag-and-drop feedback.");
      ImGui::Spacing();
    }

    ImGui::Spacing();

    ImGui::Text("Keyboard Shortcuts");
    ImGui::Separator();

    SectionHeader("Global Shortcuts");
    RenderShortcutBullet(FindShortcut(ShortcutAction::FocusSearch));
    RenderShortcutBullet(FindShortcut(ShortcutAction::RefreshSearch));
    RenderShortcutBullet(FindShortcut(ShortcutAction::ClearFilters));
    RenderShortcutBullet(FindShortcut(ShortcutAction::ToggleHierarchy));

    SectionHeader("Search & Navigation");
    ShortcutBullet("Enter - Trigger search (in any search input field)");
    ShortcutBullet("Slash (/) - Start 'Filter in results' in the results table");
#ifdef __APPLE__
    ShortcutBullet("Cmd+G - Cancel active 'Filter in results' and restore selection/scroll");
#else
    ShortcutBullet("Ctrl+G - Cancel active 'Filter in results' and restore selection/scroll");
#endif  // __APPLE__

    SectionHeader("Saved Searches");
    RenderShortcutBullet(FindShortcut(ShortcutAction::SaveSearch));

    SectionHeader("File Operations");
#ifdef __APPLE__
    ShortcutBullet("Cmd+C - Copy name (hover over name) or full path (hover over path)");
#else
    ShortcutBullet("Ctrl+C - Copy name (hover over name) or full path (hover over path)");
#endif  // __APPLE__
    RenderShortcutBullet(FindShortcut(ShortcutAction::ExportCsv));
    ShortcutBullet("Delete - Delete selected file (opens confirmation)");
    ShortcutBullet("Drag row - Drag and drop file to other applications (Windows & macOS)");
    ShortcutBullet("Double-click filename - Open file with default application");
    ShortcutBullet("Double-click full path - Open parent folder in Explorer");
    ShortcutBullet("Enter - Open selected file or folder");
#ifdef __APPLE__
    ShortcutBullet("Cmd+Enter - Reveal in Explorer (open parent folder, select file)");
    ShortcutBullet("Cmd+Shift+C - Copy full path of selected row");
#else
    ShortcutBullet("Ctrl+Enter - Reveal in Explorer (open parent folder, select file)");
    ShortcutBullet("Ctrl+Shift+C - Copy full path of selected row");
#endif  // __APPLE__
    ShortcutBullet("Tab - Focus name search (from results table)");
    RenderShortcutBullet(FindShortcut(ShortcutAction::ToggleFolderStats));
#ifdef _WIN32
    ShortcutBullet("Ctrl+Shift+P - Pin selected file or folder to Quick Access");
#endif  // _WIN32

    SectionHeader("Mark Mode (Selection & Bulk Actions)");
    ShortcutBullet("n or Down - Move to next row");
    ShortcutBullet("p or Up - Move to previous row (plain p; Ctrl+Shift+P is Pin on Windows)");
    ShortcutBullet("m - Mark file and move to next row");
    ShortcutBullet("u - Unmark file and move to next row");
    ShortcutBullet("t - Toggle mark and move to next row");
    ShortcutBullet("Shift+M - Mark all visible rows");
    ShortcutBullet("Shift+T - Invert marks for visible rows");
    ShortcutBullet("Shift+U - Unmark all files");
    ShortcutBullet("W - Copy all marked paths to clipboard");
    ShortcutBullet("Shift+W - Copy all marked names to clipboard");
    ShortcutBullet("Shift+D - Bulk delete marked files (opens confirmation)");

    ImGui::Spacing();
    detail::RenderToolWindowCloseButton(p_open);
  }
}

}  // namespace ui
