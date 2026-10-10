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
  ExportCsv,          // Ctrl/Cmd+E:       export results to CSV
  PinSearch,          // Ctrl/Cmd+P:       pin/unpin current search in Search History
  NavigateBack,       // Ctrl/Cmd+J:       browser-style back: re-run previous session search
  NavigateForward,    // Ctrl/Cmd+K:       browser-style forward: re-run next session search
};

// Where the shortcut fires.
enum class ShortcutScope : uint8_t {
  Global,        // fires every frame unless ShouldSkipDueToTextInput (see ShortcutDef::text_input_safe)
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
  // Declarative text-input policy: when a text field has focus (io.WantTextInput),
  // only defs with text_input_safe = true keep firing (HandleKeyboardShortcuts
  // applies ShouldSkipDueToTextInput uniformly). Chords like Cmd/Ctrl+F are safe
  // because the held primary modifier stops InputText from inserting characters;
  // bare keys that a field could consume (Esc) stay false.
  bool             text_input_safe;
  std::string_view key_label;    // display portion appended after modifier, e.g. "Shift+H"
  std::string_view description;  // shown verbatim in HelpWindow
};

// Registry — one entry per shortcut, ordered by section then display position.
// NOLINTNEXTLINE(readability-magic-numbers) - count equals number of ShortcutAction values
inline constexpr std::array<ShortcutDef, 8> kShortcuts{{
  // Global Shortcuts section
  {ShortcutAction::FocusSearch,       ImGuiKey_F,      true,  false, ShortcutScope::Global, ShortcutPlatform::All, true,  "F",       "Focus name search input"},
  {ShortcutAction::RefreshSearch,     ImGuiKey_F5,     false, false, ShortcutScope::Global, ShortcutPlatform::All, true,  "F5",      "Refresh search (re-run current query)"},
  {ShortcutAction::ClearFilters,      ImGuiKey_Escape, false, false, ShortcutScope::Global, ShortcutPlatform::All, false, "Esc",     "Clear all filters"},
  {ShortcutAction::ToggleHierarchy,   ImGuiKey_H,      true,  true,  ShortcutScope::Global, ShortcutPlatform::All, true,  "Shift+H", "Toggle path hierarchy indentation in results"},
  // Search History section
  {ShortcutAction::PinSearch,         ImGuiKey_P,      true,  false, ShortcutScope::Global, ShortcutPlatform::All, true,  "P",       "Pin/unpin current search"},
  // File Operations section
  {ShortcutAction::ExportCsv,         ImGuiKey_E,      true,  false, ShortcutScope::Global, ShortcutPlatform::All, true,  "E",       "Export current results to CSV"},
  // Search & Navigation section (browser-style back/forward over session history;
  // text_input_safe=true so the chords work while a search input has focus — US-9.
  // Mnemonic: J precedes K = earlier/later search.)
  {ShortcutAction::NavigateBack,      ImGuiKey_J,      true,  false, ShortcutScope::Global, ShortcutPlatform::All, true,  "J",       "Back: re-run previous search from this session"},
  {ShortcutAction::NavigateForward,   ImGuiKey_K,      true,  false, ShortcutScope::Global, ShortcutPlatform::All, true,  "K",       "Forward: re-run next search from this session"},
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

// Uniform text-input gate (spec §7.3 P0-T6a): while a text field has focus, a
// shortcut fires only when its def declares text_input_safe. Replaces the old
// hardcoded per-action exception so new typing-safe shortcuts are declarative.
[[nodiscard]] inline constexpr bool ShouldSkipDueToTextInput(const ShortcutDef& def,
                                                             bool want_text_input) noexcept {
  return want_text_input && !def.text_input_safe;
}

// Plain POD snapshot of the modifier keys, so shortcut matching is unit-testable
// without an ImGui context or key map. Fields are public POD members:
// snake_case without trailing underscore.
struct ShortcutModifierState {
  bool primary = false;  // Ctrl (Windows/Linux) or Super/Cmd (macOS)
  bool shift = false;
  bool alt = false;
};

// Exact modifier matching (spec §7.3 P0-T6c; deliberately tighter than the old
// partial check — extra held modifiers can no longer fire the wrong action):
//   chord (primary_modifier = true): primary down, Shift exactly def.shift, Alt up.
//   bare key (primary_modifier = false): primary up, Alt up; Shift is
//     intentionally unconstrained so Shift-driven chords keep declaring it via
//     a primary_modifier = true def.
[[nodiscard]] inline constexpr bool IsModifierStateMatch(const ShortcutDef& def,
                                                         const ShortcutModifierState& state) noexcept {
  if (def.primary_modifier) {
    return state.primary && (state.shift == def.shift) && !state.alt;
  }
  return !state.primary && !state.alt;
}

// Returns true if the shortcut's key combo was pressed this frame.
// Does NOT check scope, platform, or text-input policy — the caller is
// responsible (see HandleKeyboardShortcuts / ShouldSkipDueToTextInput).
// Modifier semantics live in IsModifierStateMatch; the ImGui-free part of that
// predicate is unit-tested directly, this wrapper only supplies io + key press.
inline bool IsTriggered(const ShortcutDef& def, const ImGuiIO& io) {
  const ShortcutModifierState modifiers{IsPrimaryShortcutModifierDown(io), io.KeyShift, io.KeyAlt};
  return IsModifierStateMatch(def, modifiers) && ImGui::IsKeyPressed(def.key, false);
}

// Builds a display label for the shortcut, e.g. "Ctrl+Shift+H", "Cmd+F", "F5".
std::string FormatLabel(const ShortcutDef& def);
