#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "gui/ImGuiUtils.h"
#include "imgui.h"

/**
 * @file gui/ShortcutRegistry.h
 * @brief Declarative keyboard shortcut registry.
 *
 * Adding a shortcut requires one entry in kShortcuts plus one case in
 * DispatchGlobalShortcut (ApplicationLogic.cpp). Detection and HelpWindow
 * rendering are driven automatically from the registry.
 */

// Identifies each keyboard shortcut for dispatch and HelpWindow lookup.
enum class ShortcutAction : uint8_t {
  FocusSearch,        // Ctrl/Cmd+F:       focus filename search input
  RefreshSearch,      // F5:               re-run current search
  ClearFilters,       // Escape:           clear all active filters
  ToggleHierarchy,    // Ctrl/Cmd+Shift+H: toggle path hierarchy indentation
  SaveSearch,         // Ctrl/Cmd+S:       open Save Search dialog
  ExportCsv,          // Ctrl/Cmd+E:       export results to CSV
  ToggleFolderStats,  // Ctrl/Cmd+Shift+F: toggle Matched Files/Matched Size columns
};

// Where the shortcut fires.
enum class ShortcutScope : uint8_t {
  Global,        // fires whenever !WantTextInput (no widget-focus requirement)
  TableFocused,  // fires only when results table has keyboard focus (future use)
};

// Platform availability.
enum class ShortcutPlatform : uint8_t {
  All,         // available on every platform
  WindowsOnly, // available only on Windows
};

// Immutable, constexpr-safe description of one keyboard shortcut.
struct ShortcutDef {
  ShortcutAction   action;
  ImGuiKey         key;
  bool             primary_modifier;  // true = Ctrl (Win/Linux) or Cmd (macOS)
  bool             shift;             // true = also requires Shift
  ShortcutScope    scope;
  ShortcutPlatform platform;
  std::string_view key_label;    // display portion appended after modifier, e.g. "Shift+F"
  std::string_view description;  // shown verbatim in HelpWindow
};

// Registry — one entry per shortcut, ordered by section then display position.
// NOLINTNEXTLINE(readability-magic-numbers) - count equals number of ShortcutAction values
inline constexpr std::array<ShortcutDef, 7> kShortcuts{{
  // Global Shortcuts section
  {ShortcutAction::FocusSearch,       ImGuiKey_F,      true,  false, ShortcutScope::Global, ShortcutPlatform::All, "F",       "Focus name search input"},
  {ShortcutAction::RefreshSearch,     ImGuiKey_F5,     false, false, ShortcutScope::Global, ShortcutPlatform::All, "F5",      "Refresh search (re-run current query)"},
  {ShortcutAction::ClearFilters,      ImGuiKey_Escape, false, false, ShortcutScope::Global, ShortcutPlatform::All, "Esc",     "Clear all filters"},
  {ShortcutAction::ToggleHierarchy,   ImGuiKey_H,      true,  true,  ShortcutScope::Global, ShortcutPlatform::All, "Shift+H", "Toggle path hierarchy indentation in results"},
  // Saved Searches section
  {ShortcutAction::SaveSearch,        ImGuiKey_S,      true,  false, ShortcutScope::Global, ShortcutPlatform::All, "S",       "Save current search"},
  // File Operations section
  {ShortcutAction::ExportCsv,         ImGuiKey_E,      true,  false, ShortcutScope::Global, ShortcutPlatform::All, "E",       "Export current results to CSV"},
  {ShortcutAction::ToggleFolderStats, ImGuiKey_F,      true,  true,  ShortcutScope::Global, ShortcutPlatform::All, "Shift+F", "Toggle Matched Files / Matched Size columns"},
}};

// Returns "Cmd" on macOS, "Ctrl" on Windows/Linux.
constexpr std::string_view GetPrimaryModifierName() noexcept {
#ifdef __APPLE__
  return "Cmd";
#else
  return "Ctrl";
#endif  // __APPLE__
}

// Returns true if this shortcut should be active/shown on the current compiled platform.
constexpr bool IsAvailableOnPlatform(const ShortcutDef& def) noexcept {
#ifdef _WIN32
  return def.platform == ShortcutPlatform::All || def.platform == ShortcutPlatform::WindowsOnly;
#else
  return def.platform == ShortcutPlatform::All;
#endif  // _WIN32
}

// Returns a reference to the ShortcutDef for the given action.
// Asserts in debug builds if the action is not present in kShortcuts.
const ShortcutDef& FindShortcut(ShortcutAction action) noexcept;

// Returns true if the shortcut's key combo was pressed this frame.
// Does NOT check scope or platform — the caller is responsible.
// For bare-key shortcuts (primary_modifier = false), modifier state is not checked.
// For modifier shortcuts with shift = false, also verifies Shift is NOT held to prevent
// cross-firing (e.g. Ctrl+F must not fire when Ctrl+Shift+F is pressed).
inline bool IsTriggered(const ShortcutDef& def, const ImGuiIO& io) {
  if (def.primary_modifier) {
    if (!IsPrimaryShortcutModifierDown(io)) {
      return false;
    }
    if (def.shift != io.KeyShift) {
      return false;
    }
  }
  return ImGui::IsKeyPressed(def.key);
}

// Builds a display label for the shortcut, e.g. "Ctrl+Shift+F", "Cmd+F", "F5".
std::string FormatLabel(const ShortcutDef& def);
