#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <array>

#include "core/ApplicationLogic.h"
#include "core/Settings.h"
#include "gui/ShortcutRegistry.h"

TEST_CASE("ShouldEnablePeriodicRecrawl is false when USN monitor is primary with crawl folder") {
  CHECK_FALSE(application_logic::ShouldEnablePeriodicRecrawl(/*use_usn_monitor=*/true,
                                                              /*has_crawl_folder=*/true));
}

TEST_CASE("ShouldEnablePeriodicRecrawl is false when USN monitor is primary without crawl folder") {
  CHECK_FALSE(application_logic::ShouldEnablePeriodicRecrawl(/*use_usn_monitor=*/true,
                                                              /*has_crawl_folder=*/false));
}

TEST_CASE("ShouldEnablePeriodicRecrawl is true for folder crawl with crawl folder") {
  CHECK(application_logic::ShouldEnablePeriodicRecrawl(/*use_usn_monitor=*/false,
                                                       /*has_crawl_folder=*/true));
}

TEST_CASE("ShouldEnablePeriodicRecrawl is false for folder crawl without crawl folder") {
  CHECK_FALSE(application_logic::ShouldEnablePeriodicRecrawl(/*use_usn_monitor=*/false,
                                                              /*has_crawl_folder=*/false));
}

TEST_CASE("ShouldEnablePeriodicRecrawl evaluates auto_crawl_folder fallback correctly") {
  // When settings crawlFolder is empty but auto_crawl_folder is non-empty,
  // has_crawl_folder parameter passed to ShouldEnablePeriodicRecrawl is true.
  const bool has_crawl_folder_with_auto_fallback = true;
  CHECK(application_logic::ShouldEnablePeriodicRecrawl(/*use_usn_monitor=*/false,
                                                       has_crawl_folder_with_auto_fallback));
}

TEST_CASE("ShouldEnablePeriodicRecrawl is false when crawl folder is empty and no auto crawl folder") {
  CHECK_FALSE(application_logic::ShouldEnablePeriodicRecrawl(/*use_usn_monitor=*/false,
                                                              /*has_crawl_folder=*/false));
}

TEST_CASE("ShouldGateSearchOnUsnPopulation is true only for USN-primary sessions while populating") {
  CHECK(application_logic::ShouldGateSearchOnUsnPopulation(/*use_usn_monitor=*/true,
                                                           /*usn_populating=*/true));
  CHECK_FALSE(application_logic::ShouldGateSearchOnUsnPopulation(/*use_usn_monitor=*/true,
                                                                 /*usn_populating=*/false));
  CHECK_FALSE(application_logic::ShouldGateSearchOnUsnPopulation(/*use_usn_monitor=*/false,
                                                                 /*usn_populating=*/true));
  CHECK_FALSE(application_logic::ShouldGateSearchOnUsnPopulation(/*use_usn_monitor=*/false,
                                                                 /*usn_populating=*/false));
}

// ---------------------------------------------------------------------------
// P0-T6 shortcut registry robustness (spec §7.3)
// ---------------------------------------------------------------------------

namespace {

// When adding a ShortcutAction, extend this array — that is the guard the
// compiler cannot enforce for you (a registry entry, dispatch case, and Help
// line can all silently go missing; this array + static_assert catches the
// registry half, /w14062 + -Wswitch the dispatch half).
constexpr std::array<ShortcutAction, 8> kAllShortcutActions{{
  ShortcutAction::FocusSearch,
  ShortcutAction::RefreshSearch,
  ShortcutAction::ClearFilters,
  ShortcutAction::ToggleHierarchy,
  ShortcutAction::ExportCsv,
  ShortcutAction::PinSearch,
  ShortcutAction::NavigateBack,
  ShortcutAction::NavigateForward,
}};

// The shortcuts that fire while io.WantTextInput: the four original chords
// plus F5 (fire-while-typing preserved by P0-T6a's flag values) and the two
// navigation chords (US-9 requires them mid-typing; held primary modifier
// stops InputText from inserting the character). ClearFilters (bare Esc)
// stays skipped so a focused field can consume it.
constexpr std::array<ShortcutAction, 7> kFireWhileTyping{{
  ShortcutAction::FocusSearch,
  ShortcutAction::RefreshSearch,
  ShortcutAction::ToggleHierarchy,
  ShortcutAction::ExportCsv,
  ShortcutAction::PinSearch,
  ShortcutAction::NavigateBack,
  ShortcutAction::NavigateForward,
}};

}  // namespace

static_assert(kAllShortcutActions.size() == kShortcuts.size(),
              "kShortcuts must carry exactly one entry per ShortcutAction value");

TEST_CASE("Registry completeness: every ShortcutAction round-trips through FindShortcut") {
  for (const ShortcutAction action : kAllShortcutActions) {
    CHECK(FindShortcut(action).action == action);
  }
}

TEST_CASE("IsModifierStateMatch requires exact modifiers for chords") {
  const ShortcutDef& focus = FindShortcut(ShortcutAction::FocusSearch);  // Ctrl/Cmd+F
  CHECK(IsModifierStateMatch(focus, {true, false, false}));    // exact chord
  CHECK_FALSE(IsModifierStateMatch(focus, {false, false, false}));  // bare key
  CHECK_FALSE(IsModifierStateMatch(focus, {true, true, false}));    // extra Shift (US-10: Ctrl+Shift+F)
  CHECK_FALSE(IsModifierStateMatch(focus, {true, false, true}));    // extra Alt (US-10: Ctrl+Alt+F)
  CHECK_FALSE(IsModifierStateMatch(focus, {false, true, false}));   // Shift alone
  CHECK_FALSE(IsModifierStateMatch(focus, {false, false, true}));   // Alt alone

  const ShortcutDef& hierarchy = FindShortcut(ShortcutAction::ToggleHierarchy);  // Ctrl/Cmd+Shift+H
  CHECK(IsModifierStateMatch(hierarchy, {true, true, false}));      // exact chord
  CHECK_FALSE(IsModifierStateMatch(hierarchy, {true, false, false}));  // Shift missing
  CHECK_FALSE(IsModifierStateMatch(hierarchy, {true, true, true}));     // extra Alt
}

TEST_CASE("IsModifierStateMatch keeps bare keys primary/Alt-free with Shift intentionally unconstrained") {
  const ShortcutDef& refresh = FindShortcut(ShortcutAction::RefreshSearch);  // bare F5
  CHECK_FALSE(refresh.primary_modifier);
  CHECK(IsModifierStateMatch(refresh, {false, false, false}));  // plain press
  CHECK(IsModifierStateMatch(refresh, {false, true, false}));   // Shift stays unconstrained for bare defs
  CHECK_FALSE(IsModifierStateMatch(refresh, {true, false, false}));   // primary must be up (tightening)
  CHECK_FALSE(IsModifierStateMatch(refresh, {false, false, true}));   // Alt must be up (tightening)

  const ShortcutDef& esc = FindShortcut(ShortcutAction::ClearFilters);  // bare Escape
  CHECK_FALSE(esc.primary_modifier);
  CHECK(IsModifierStateMatch(esc, {false, false, false}));
  CHECK_FALSE(IsModifierStateMatch(esc, {true, false, false}));
  CHECK_FALSE(IsModifierStateMatch(esc, {false, false, true}));
}

TEST_CASE("ShouldSkipDueToTextInput keeps exactly the fire-while-typing set from before the flag") {
  for (const auto& def : kShortcuts) {
    const bool expected_fire =
        std::find(kFireWhileTyping.begin(), kFireWhileTyping.end(), def.action) !=
        kFireWhileTyping.end();
    CAPTURE(static_cast<int>(def.action));
    if (expected_fire) {
      CHECK(def.text_input_safe);
      CHECK_FALSE(ShouldSkipDueToTextInput(def, /*want_text_input=*/true));
    } else {
      CHECK_FALSE(def.text_input_safe);
      CHECK(ShouldSkipDueToTextInput(def, /*want_text_input=*/true));
    }
    // No shortcut is skipped when no text field has focus.
    CHECK_FALSE(ShouldSkipDueToTextInput(def, /*want_text_input=*/false));
  }
}

TEST_CASE("ShouldSkipDueToTextInput truth table") {
  constexpr ShortcutDef bare_unsafe{ShortcutAction::ClearFilters, ImGuiKey_Escape, false, false,
                                     ShortcutScope::Global, ShortcutPlatform::All, false,
                                     "Esc", "unfocused Esc"};
  constexpr ShortcutDef chord_safe{ShortcutAction::FocusSearch, ImGuiKey_F, true, false,
                                   ShortcutScope::Global, ShortcutPlatform::All, true,
                                   "F", "unfocused F"};
  CHECK_FALSE(ShouldSkipDueToTextInput(bare_unsafe, false));
  CHECK(ShouldSkipDueToTextInput(bare_unsafe, true));
  CHECK_FALSE(ShouldSkipDueToTextInput(chord_safe, false));
  CHECK_FALSE(ShouldSkipDueToTextInput(chord_safe, true));
}

// ---------------------------------------------------------------------------
// P4-T1 mandatory Back/Forward shortcuts (spec §7)
// ---------------------------------------------------------------------------

TEST_CASE("Navigate chords registry round-trip: J steps back, K steps forward") {
  const ShortcutDef& back = FindShortcut(ShortcutAction::NavigateBack);
  CHECK(back.key == ImGuiKey_J);
  CHECK(back.primary_modifier);
  CHECK_FALSE(back.shift);
  CHECK(back.scope == ShortcutScope::Global);
  CHECK(back.platform == ShortcutPlatform::All);
  CHECK(back.text_input_safe);  // US-9: works while a search input has focus
  CHECK(back.key_label == std::string_view("J"));

  const ShortcutDef& forward = FindShortcut(ShortcutAction::NavigateForward);
  CHECK(forward.key == ImGuiKey_K);
  CHECK(forward.primary_modifier);
  CHECK_FALSE(forward.shift);
  CHECK(forward.scope == ShortcutScope::Global);
  CHECK(forward.platform == ShortcutPlatform::All);
  CHECK(forward.text_input_safe);
  CHECK(forward.key_label == std::string_view("K"));
}

TEST_CASE("Navigate Help labels are platform-correct via the registry (US-12)") {
  const std::string expected_back = std::string(GetPrimaryModifierName()) + "+J";
  const std::string expected_forward = std::string(GetPrimaryModifierName()) + "+K";
  CHECK(FormatLabel(FindShortcut(ShortcutAction::NavigateBack)) == expected_back);
  CHECK(FormatLabel(FindShortcut(ShortcutAction::NavigateForward)) == expected_forward);
}

TEST_CASE("Exact modifier matching guards the navigate chords (US-10)") {
  const ShortcutDef& back = FindShortcut(ShortcutAction::NavigateBack);
  // Cmd/Ctrl+J exactly.
  CHECK(IsModifierStateMatch(back, {true, false, false}));
  // Wrong modifier sets must NOT fire: extra Shift, extra Alt, bare key.
  CHECK_FALSE(IsModifierStateMatch(back, {true, true, false}));   // Cmd/Ctrl+Shift+J
  CHECK_FALSE(IsModifierStateMatch(back, {true, false, true}));   // Cmd/Ctrl+Alt+J (spec §7 US-10)
  CHECK_FALSE(IsModifierStateMatch(back, {false, false, false})); // bare J
  CHECK_FALSE(IsModifierStateMatch(back, {false, false, true}));  // bare Alt+J
}
