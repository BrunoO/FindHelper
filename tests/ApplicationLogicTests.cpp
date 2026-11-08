#define DOCTEST_CONFIG_IMPLEMENT

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "core/ApplicationLogic.h"
#include "core/Settings.h"
#include "gui/GuiState.h"
#include "gui/ShortcutRegistry.h"
#include "imgui.h"
#include "index/FileIndex.h"
#include "search/FolderSizeAggregator.h"
#include "search/SearchController.h"
#include "search/SearchHistory.h"
#include "search/SearchWorker.h"
#include "utils/ThreadUtils.h"

// GuiState members assert UI-thread ownership in debug builds; the dispatcher
// and Update() drive GuiState mutations, so mark the test thread as UI.
int main(int argc, char** argv) {
    MarkCurrentThreadAsUI();
    ImGui::CreateContext();
    doctest::Context context(argc, argv);
    const int result = context.run();
    ImGui::DestroyContext();
    return result;
}

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

TEST_CASE("ShouldRunPeriodicMaintenance requires idle index build, search worker, and USN queue") {
  CHECK(application_logic::ShouldRunPeriodicMaintenance(/*is_index_building=*/false,
                                                        /*is_search_busy=*/false,
                                                        /*usn_queue_size=*/0U));
  CHECK_FALSE(application_logic::ShouldRunPeriodicMaintenance(/*is_index_building=*/true,
                                                              /*is_search_busy=*/false,
                                                              /*usn_queue_size=*/0U));
  CHECK_FALSE(application_logic::ShouldRunPeriodicMaintenance(/*is_index_building=*/false,
                                                              /*is_search_busy=*/true,
                                                              /*usn_queue_size=*/0U));
  CHECK_FALSE(application_logic::ShouldRunPeriodicMaintenance(/*is_index_building=*/false,
                                                              /*is_search_busy=*/false,
                                                              /*usn_queue_size=*/1U));
  CHECK_FALSE(application_logic::ShouldRunPeriodicMaintenance(/*is_index_building=*/false,
                                                              /*is_search_busy=*/false,
                                                              /*usn_queue_size=*/5051U));
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

// ---------------------------------------------------------------------------
// DispatchGlobalShortcut — stub-host unit tests (tech-debt D7)
//
// The ShortcutHost seam (fab78ff) removed the composition-root dependency so
// every case's guards and side effects are drivable with a recording stub:
// no Application, no window, no real settings persistence.
// ---------------------------------------------------------------------------

namespace {

// NOLINTBEGIN(readability-identifier-naming) - stub recorder; readable field names
class StubHost final : public application_logic::ShortcutHost,
                       public application_logic::CrawlHost {
 public:
  // --- ui::UIActions ---
  void TriggerManualSearch(const GuiState& /*state*/) override { ++manual_search_calls; }
  [[nodiscard]] bool IsIndexBuilding() const override { return index_building; }
  [[nodiscard]] size_t GetIndexedFileCount() const override { return 0; }
  [[nodiscard]] bool StartIndexBuilding(std::string_view /*folder_path*/) override {
    ++start_index_building_calls;
    return true;
  }
  void StopIndexBuilding() override { ++stop_index_building_calls; }
  void ExportToCsv(GuiState& /*state*/) override { ++export_csv_calls; }
  [[nodiscard]] bool PersistSettings() override {
    ++persist_calls;
    return persist_result;
  }

  // --- application_logic::ShortcutHost ---
  [[nodiscard]] std::string_view GetLastHistoryId() const noexcept override {
    return last_history_id;
  }

  // --- application_logic::CrawlHost ---
  [[nodiscard]] bool CheckAndClearCrawlCompletion() override {
    return std::exchange(crawl_just_completed, false);
  }
  [[nodiscard]] std::chrono::steady_clock::time_point GetLastCrawlCompletionTime() const override {
    return last_crawl_time;
  }
  [[nodiscard]] bool IsRecrawlEnabled() const override { return recrawl_enabled; }
  [[nodiscard]] size_t GetUsnQueueSize() const override { return usn_queue_size; }
  void TriggerRecrawl() override { ++recrawl_calls; }

  bool persist_result = true;
  bool index_building = false;
  bool recrawl_enabled = false;
  bool crawl_just_completed = false;
  size_t usn_queue_size = 0;
  std::string last_history_id{};
  std::chrono::steady_clock::time_point last_crawl_time = std::chrono::steady_clock::now();

  int manual_search_calls = 0;
  int persist_calls = 0;
  int export_csv_calls = 0;
  int recrawl_calls = 0;
  int start_index_building_calls = 0;
  int stop_index_building_calls = 0;
};

// Full pipeline fixture: real index/controller/worker/aggregator (the
// dispatcher takes concrete types), stub host for everything app-level.
struct DispatchFixture {
  FileIndex index;
  SearchWorker worker{index};
  SearchController controller;
  FolderSizeAggregator aggregator{index};
  GuiState state;
  AppSettings settings;
  StubHost host;
};

void Dispatch(DispatchFixture& fx, ShortcutAction action, bool is_index_building = false) {
  application_logic::DispatchGlobalShortcut(action, fx.state, fx.settings, fx.controller,
                                            fx.worker, &fx.aggregator, is_index_building,
                                            fx.host);
}

std::string SeedHistory(DispatchFixture& fx, const std::string& filename, std::int64_t now_ms) {
  SearchHistoryParams params;
  params.filename = filename;
  return RecordHistoryEntry(params, now_ms, fx.settings);
}

}  // namespace

TEST_SUITE("DispatchGlobalShortcut") {

TEST_CASE("FocusSearch fires only in Full UI mode") {
  DispatchFixture fx;
  fx.settings.uiMode = AppSettings::UIMode::Full;
  Dispatch(fx, ShortcutAction::FocusSearch);
  CHECK(fx.state.input_debounce.focus_filename_input);

  DispatchFixture fx2;
  fx2.settings.uiMode = AppSettings::UIMode::Simplified;
  Dispatch(fx2, ShortcutAction::FocusSearch);
  CHECK_FALSE(fx2.state.input_debounce.focus_filename_input);
}

TEST_CASE("ClearFilters skips while inline results filter is active") {
  DispatchFixture fx;
  fx.state.searchCriteria.filename_input.SetValue("keep");
  fx.state.ui_visibility.incremental_search_active = true;

  Dispatch(fx, ShortcutAction::ClearFilters);

  CHECK(fx.state.searchCriteria.filename_input.AsString() == "keep");
}

TEST_CASE("ClearFilters clears inputs and time filter otherwise") {
  DispatchFixture fx;
  fx.state.searchCriteria.filename_input.SetValue("drop");
  fx.state.searchCriteria.time_filter = TimeFilter::Today;
  fx.state.ui_visibility.incremental_search_active = false;

  Dispatch(fx, ShortcutAction::ClearFilters);

  CHECK(fx.state.searchCriteria.filename_input.AsString().empty());
  CHECK(fx.state.searchCriteria.time_filter == TimeFilter::None);
}

TEST_CASE("PinSearch guards: no history id, then entry missing from history") {
  DispatchFixture fx;

  Dispatch(fx, ShortcutAction::PinSearch);  // last_history_id empty
  CHECK(fx.state.export_workflow.error_message == "Nothing to pin yet — run a search first");

  fx.host.last_history_id = "not-in-history";
  Dispatch(fx, ShortcutAction::PinSearch);
  CHECK(fx.state.export_workflow.error_message ==
        "Last search is no longer in history — nothing to pin");
  CHECK(fx.host.persist_calls == 0);
}

TEST_CASE("PinSearch toggles unpinned entry and persists") {
  DispatchFixture fx;
  const std::string id = SeedHistory(fx, "beta.txt", 1000);
  fx.host.last_history_id = id;
  fx.host.persist_result = true;

  Dispatch(fx, ShortcutAction::PinSearch);

  const auto* const entry = FindHistoryEntryById(fx.settings.searchHistory, id);
  REQUIRE(entry != nullptr);
  CHECK(entry->is_pinned);
  CHECK(fx.host.persist_calls == 1);
  CHECK(fx.state.export_workflow.notification.find("Pinned") != std::string::npos);
}

TEST_CASE("PinSearch toggles pinned entry back to unpinned") {
  DispatchFixture fx;
  const std::string id = SeedHistory(fx, "gamma.txt", 1000);
  PinHistoryEntry(id, 1500, fx.settings);
  fx.host.last_history_id = id;

  Dispatch(fx, ShortcutAction::PinSearch);

  const auto* const entry = FindHistoryEntryById(fx.settings.searchHistory, id);
  REQUIRE(entry != nullptr);
  CHECK_FALSE(entry->is_pinned);
  CHECK(fx.state.export_workflow.notification.find("Unpinned") != std::string::npos);
}

TEST_CASE("PinSearch keeps in-memory toggle when persist fails") {
  DispatchFixture fx;
  const std::string id = SeedHistory(fx, "delta.txt", 1000);
  fx.host.last_history_id = id;
  fx.host.persist_result = false;

  Dispatch(fx, ShortcutAction::PinSearch);

  const auto* const entry = FindHistoryEntryById(fx.settings.searchHistory, id);
  REQUIRE(entry != nullptr);
  CHECK(entry->is_pinned);  // toggle applied regardless
  CHECK(fx.state.export_workflow.error_message ==
        "Failed to save Quick Access change (see log)");
}

TEST_CASE("RefreshSearch is blocked while index building") {
  DispatchFixture fx;
  Dispatch(fx, ShortcutAction::RefreshSearch, /*is_index_building=*/true);
  CHECK(fx.host.manual_search_calls == 0);
}

TEST_CASE("ExportCsv skips when no display results") {
  DispatchFixture fx;
  Dispatch(fx, ShortcutAction::ExportCsv);
  CHECK(fx.host.export_csv_calls == 0);
}

TEST_CASE("ExportCsv forwards to host when results exist") {
  DispatchFixture fx;
  fx.state.result_pool_->Results().push_back(SearchResult{});
  fx.state.result_pool_->BumpBatchNumber();

  Dispatch(fx, ShortcutAction::ExportCsv);

  CHECK(fx.host.export_csv_calls == 1);
}

TEST_CASE("NavigateBack on empty stack is a silent no-op") {
  DispatchFixture fx;
  Dispatch(fx, ShortcutAction::NavigateBack);
  CHECK(fx.host.manual_search_calls == 0);
}

TEST_CASE("ToggleHierarchy persists flip and rolls back on persist failure") {
  DispatchFixture fx;
  const bool original = fx.settings.showPathHierarchyIndentation;

  fx.host.persist_result = true;
  Dispatch(fx, ShortcutAction::ToggleHierarchy);
  CHECK(fx.settings.showPathHierarchyIndentation == !original);
  CHECK(fx.host.persist_calls == 1);

  Dispatch(fx, ShortcutAction::ToggleHierarchy);  // second call flips again
  CHECK(fx.settings.showPathHierarchyIndentation == original);

  // Now make persist fail: the toggle must revert (flag restored).
  fx.host.persist_result = false;
  Dispatch(fx, ShortcutAction::ToggleHierarchy);
  CHECK(fx.settings.showPathHierarchyIndentation == original);  // rolled back
}

}  // TEST_SUITE

// ---------------------------------------------------------------------------
// CheckPeriodicRecrawl — host stub drives interval/arming decisions (D7).
// ---------------------------------------------------------------------------

TEST_SUITE("CheckPeriodicRecrawl") {

TEST_CASE("Disabled recrawl never triggers") {
  StubHost host;
  AppSettings settings;
  host.recrawl_enabled = false;
  host.last_crawl_time = std::chrono::steady_clock::now() - std::chrono::hours(2);

  application_logic::CheckPeriodicRecrawl(host, settings, host.index_building);
  CHECK(host.recrawl_calls == 0);
}

TEST_CASE("Building index blocks recrawl even when interval elapsed") {
  StubHost host;
  AppSettings settings;
  settings.recrawl.intervalMinutes = 1;
  host.recrawl_enabled = true;
  host.index_building = true;
  host.last_crawl_time = std::chrono::steady_clock::now() - std::chrono::hours(2);

  application_logic::CheckPeriodicRecrawl(host, settings, host.index_building);
  CHECK(host.recrawl_calls == 0);
}

TEST_CASE("Armed and elapsed triggers exactly one recrawl") {
  StubHost host;
  AppSettings settings;
  settings.recrawl.intervalMinutes = 1;
  host.recrawl_enabled = true;
  host.index_building = false;
  host.last_crawl_time = std::chrono::steady_clock::now() - std::chrono::hours(2);

  application_logic::CheckPeriodicRecrawl(host, settings, host.index_building);
  CHECK(host.recrawl_calls == 1);
}

TEST_CASE("Interval not yet elapsed does not trigger") {
  StubHost host;
  AppSettings settings;
  settings.recrawl.intervalMinutes = 60;
  host.recrawl_enabled = true;
  host.index_building = false;
  host.last_crawl_time = std::chrono::steady_clock::now();

  application_logic::CheckPeriodicRecrawl(host, settings, host.index_building);
  CHECK(host.recrawl_calls == 0);
}

}  // TEST_SUITE

// ---------------------------------------------------------------------------
// Update — frame update logic with segregated ShortcutHost and CrawlHost
// ---------------------------------------------------------------------------

TEST_SUITE("ApplicationLogicUpdate") {

TEST_CASE("Update clears crawl completion flag when crawl finishes") {
  DispatchFixture fx;
  fx.state.searchCriteria.filename_input.SetValue("test");
  fx.host.crawl_just_completed = true;

  application_logic::Update({
    &fx.state,
    &fx.controller,
    &fx.worker,
    &fx.aggregator,
    &fx.index,
    0,
    false,
    &fx.host,
    &fx.host,
    &fx.settings,
  });

  CHECK_FALSE(fx.host.crawl_just_completed);
  fx.worker.CancelSearch();
}

TEST_CASE("Update triggers periodic recrawl when conditions are met and search worker is idle") {
  DispatchFixture fx;
  fx.settings.recrawl.intervalMinutes = 1;
  fx.host.recrawl_enabled = true;
  fx.host.index_building = false;
  fx.host.last_crawl_time = std::chrono::steady_clock::now() - std::chrono::hours(2);

  application_logic::Update({
    &fx.state,
    &fx.controller,
    &fx.worker,
    &fx.aggregator,
    &fx.index,
    0,
    false,
    &fx.host,
    &fx.host,
    &fx.settings,
  });

  CHECK(fx.host.recrawl_calls == 1);
}

}  // TEST_SUITE
