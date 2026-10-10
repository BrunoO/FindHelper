/**
 * @file SearchHistoryTests.cpp
 * @brief Unit tests for the in-memory search history mutation API.
 *
 * Tests cover:
 * - GenerateHistoryId: stability and distinctness
 * - NormalizeHistoryParams: dedup equivalence, extension normalization
 * - ShouldRecordManualSearchCompletion: UI one-frame, F5 same-update, auto-refresh
 * - BuildHistoryEntryDisplayInfo / tooltip: sentence headlines, path crumbs, labels
 * - RecordHistoryEntry: insert, upsert, cap-trim, return value, last-run result count
 * - PinHistoryEntry / UnpinHistoryEntry invariants, unpin-at-cap
 * - RenameHistoryEntry / DeleteHistoryEntry
 * - GetSortedHistoryView: pinned-before-recent ordering
 * - Serialization round-trip via SaveSettings / LoadSettings (in-memory mode)
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "core/Settings.h"
#include "filters/SizeFilter.h"
#include "filters/TimeFilter.h"
#include "gui/GuiState.h"
#include "gui/WorkflowStates.h"
#include "search/SearchHistory.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static SearchHistoryParams MakeParams(std::string filename,
                                      std::string path = {},
                                      std::string extensions = {},
                                      bool folders_only = false,
                                      bool case_sensitive = false,
                                      int time_filter = 0,
                                      std::uint64_t size_filter = 0) {
  SearchHistoryParams p;
  p.filename = std::move(filename);
  p.path = std::move(path);
  p.extensions = std::move(extensions);
  p.folders_only = folders_only;
  p.case_sensitive = case_sensitive;
  p.time_filter = time_filter;
  p.size_filter = size_filter;
  return p;
}

static AppSettings MakeSettings() {
  AppSettings s;
  s.searchHistory.clear();
  return s;
}

static SearchHistoryEntry MakeDisplayEntry(SearchHistoryParams params,
                                           std::string custom_name = {},
                                           std::int64_t last_run_result_count = -1) {
  SearchHistoryEntry entry;
  entry.id = "display-test";
  entry.params = std::move(params);
  entry.custom_name = std::move(custom_name);
  entry.last_run_result_count = last_run_result_count;
  return entry;
}

// ---------------------------------------------------------------------------
// GenerateHistoryId
// ---------------------------------------------------------------------------

TEST_CASE("GenerateHistoryId with multiple extensions covers canonical comma-join") {
  // Exercises SplitAndNormalizeExtensions continuation path and BuildCanonicalString
  // comma-separator — both are only reached when there are 2+ extensions.
  const SearchHistoryParams p = MakeParams("report", {}, ".txt,.cpp,.h");
  const std::string id = GenerateHistoryId(p);
  CHECK(!id.empty());
  // Order-independent: same extensions in different order must produce the same ID
  const SearchHistoryParams p2 = MakeParams("report", {}, ".h,.cpp,.txt");
  CHECK(GenerateHistoryId(p2) == id);
}

TEST_CASE("PinHistoryEntry with unknown id is a no-op") {
  AppSettings settings = MakeSettings();
  (void)RecordHistoryEntry(MakeParams("foo.txt"), 1000, settings);
  const size_t count_before = settings.searchHistory.size();
  PinHistoryEntry("nonexistent-id", 2000, settings);
  CHECK(settings.searchHistory.size() == count_before);
  CHECK(!settings.searchHistory[0].is_pinned);
}

TEST_CASE("UnpinHistoryEntry with unknown id is a no-op") {
  AppSettings settings = MakeSettings();
  const std::string id = RecordHistoryEntry(MakeParams("foo.txt"), 1000, settings);
  PinHistoryEntry(id, 2000, settings);
  UnpinHistoryEntry("nonexistent-id", 3000, settings);
  CHECK(settings.searchHistory[0].is_pinned);  // original entry unchanged
}

TEST_CASE("GenerateHistoryId is stable across calls") {
  const SearchHistoryParams p = MakeParams("foo.txt", "C:\\Users", "txt");
  const std::string id1 = GenerateHistoryId(p);
  const std::string id2 = GenerateHistoryId(p);
  CHECK(id1 == id2);
  CHECK(id1.size() == 16);
}

TEST_CASE("GenerateHistoryId differs for distinct params") {
  const std::string id_a = GenerateHistoryId(MakeParams("foo.txt"));
  const std::string id_b = GenerateHistoryId(MakeParams("bar.txt"));
  CHECK(id_a != id_b);
}

TEST_CASE("GenerateHistoryId treats ai_search_description as part of identity") {
  SearchHistoryParams with_ai = MakeParams("foo.txt");
  with_ai.ai_search_description = "find large logs";
  const std::string id_no_ai = GenerateHistoryId(MakeParams("foo.txt"));
  const std::string id_with_ai = GenerateHistoryId(with_ai);
  CHECK(id_no_ai != id_with_ai);
}

// ---------------------------------------------------------------------------
// NormalizeHistoryParams
// ---------------------------------------------------------------------------

TEST_CASE("NormalizeHistoryParams: whitespace trimming produces dedup equivalence") {
  const SearchHistoryParams p1 = MakeParams("  foo.txt  ", " C:\\Users ", " TXT , cpp ");
  const SearchHistoryParams p2 = MakeParams("foo.txt", "C:\\Users", "cpp,txt");
  CHECK(GenerateHistoryId(p1) == GenerateHistoryId(p2));
}

TEST_CASE("NormalizeHistoryParams: extensions sorted and lowercased") {
  const SearchHistoryParams p = MakeParams("x", {}, "ZIP,  Txt,  cpp  ");
  const SearchHistoryParams norm = NormalizeHistoryParams(p);
  CHECK(norm.extensions == "cpp,txt,zip");
}

// ---------------------------------------------------------------------------
// ShouldRecordManualSearchCompletion (frame-order recording policy)
// ---------------------------------------------------------------------------

TEST_CASE("ShouldRecordManualSearchCompletion records a finished manual search") {
  // Covers both: UI Search/Enter (active at next-frame update start, then completes)
  // and F5 / crawl-complete (start+finish inside application_logic::Update).
  CHECK(ShouldRecordManualSearchCompletion(/*search_was_manual=*/true,
                                           /*search_active_after_update=*/false));
}

TEST_CASE("ShouldRecordManualSearchCompletion does not record while a search is still running") {
  CHECK_FALSE(ShouldRecordManualSearchCompletion(/*search_was_manual=*/true,
                                                 /*search_active_after_update=*/true));
}

TEST_CASE("ShouldRecordManualSearchCompletion records when auto-refresh replaces a completed manual search") {
  // PollResults completed the manual search; HandleAutoRefresh then started a
  // non-manual search (search_pipeline.search_was_manual cleared, search_pipeline.search_active true again).
  CHECK(ShouldRecordManualSearchCompletion(/*search_was_manual=*/false,
                                           /*search_active_after_update=*/true,
                                           /*pending_manual_history_record=*/true));
}

TEST_CASE("ShouldRecordManualSearchCompletion does not record auto-refresh or debounced search") {
  CHECK_FALSE(ShouldRecordManualSearchCompletion(/*search_was_manual=*/false,
                                                 /*search_active_after_update=*/false));
  CHECK_FALSE(ShouldRecordManualSearchCompletion(/*search_was_manual=*/false,
                                                 /*search_active_after_update=*/true));
}

// ---------------------------------------------------------------------------
// BuildHistoryEntryDisplayInfo / BuildHistoryEntryTooltip
// ---------------------------------------------------------------------------

TEST_CASE("BuildHistoryEntryDisplayInfo uses filter-bar labels instead of 'filter active'") {
  SearchHistoryParams params = MakeParams("*.cpp");
  params.time_filter = static_cast<int>(TimeFilter::Today);
  params.size_filter = static_cast<std::uint64_t>(SizeFilter::Large);
  params.case_sensitive = true;
  const HistoryEntryDisplayInfo info = BuildHistoryEntryDisplayInfo(MakeDisplayEntry(params));
  CHECK(info.headline == "*.cpp");
  CHECK(info.subtitle.find("Modified: Today") != std::string::npos);
  CHECK(info.subtitle.find("Size: Large (10-100 MB)") != std::string::npos);
  CHECK(info.subtitle.find("Match case") != std::string::npos);
  CHECK(info.subtitle.find("time filter active") == std::string::npos);
  CHECK(info.subtitle.find("size filter active") == std::string::npos);
}

TEST_CASE("BuildHistoryEntryDisplayInfo never uses the generic 'search' headline") {
  SearchHistoryParams today = MakeParams("");
  today.time_filter = static_cast<int>(TimeFilter::Today);
  CHECK(BuildHistoryEntryDisplayInfo(MakeDisplayEntry(today)).headline == "Modified: Today");

  SearchHistoryParams large = MakeParams("");
  large.size_filter = static_cast<std::uint64_t>(SizeFilter::Large);
  CHECK(BuildHistoryEntryDisplayInfo(MakeDisplayEntry(large)).headline == "Large (10-100 MB)");

  SearchHistoryParams folders = MakeParams("");
  folders.folders_only = true;
  CHECK(BuildHistoryEntryDisplayInfo(MakeDisplayEntry(folders)).headline == "Folders only");

  CHECK(BuildHistoryEntryDisplayInfo(MakeDisplayEntry(MakeParams(""))).headline == "All files");
}

TEST_CASE("BuildHistoryEntryDisplayInfo prefers custom name then AI prompt") {
  SearchHistoryParams params = MakeParams("*.log");
  params.ai_search_description = "find large logs";
  CHECK(BuildHistoryEntryDisplayInfo(MakeDisplayEntry(params)).headline == "\"find large logs\"");
  CHECK(BuildHistoryEntryDisplayInfo(MakeDisplayEntry(params, "Invoices")).headline == "Invoices");
}

TEST_CASE("BuildHistoryEntryDisplayInfo uses sentence headlines and two-segment paths") {
  SearchHistoryParams params =
      MakeParams("*.cpp", "C:\\Users\\bruno\\dev\\project\\src", "h,cpp");
  params.time_filter = static_cast<int>(TimeFilter::Today);
  const HistoryEntryDisplayInfo info = BuildHistoryEntryDisplayInfo(MakeDisplayEntry(params));
  CHECK(info.headline == "*.cpp in ...\\project\\src");
  CHECK(info.subtitle.find("Folder:") == std::string::npos);  // path already in headline
  CHECK(info.subtitle.find("Types: *.h,cpp") != std::string::npos);
  CHECK(info.subtitle.find("Modified: Today") != std::string::npos);
}

TEST_CASE("BuildHistoryEntryDisplayInfo labels folder crumb when path is not in the headline") {
  SearchHistoryParams params = MakeParams("");
  params.ai_search_description = "find invoices";
  params.path = "/Users/bruno/Documents/Finance";
  const HistoryEntryDisplayInfo info = BuildHistoryEntryDisplayInfo(MakeDisplayEntry(params));
  CHECK(info.headline == "\"find invoices\"");
  CHECK(info.subtitle.find("Folder: .../Documents/Finance") != std::string::npos);
}

TEST_CASE("BuildHistoryEntryDisplayInfo keeps short paths untruncated") {
  SearchHistoryParams params = MakeParams("Makefile", "src");
  CHECK(BuildHistoryEntryDisplayInfo(MakeDisplayEntry(params)).headline == "Makefile in src");
}

TEST_CASE("BuildHistoryEntryTooltip lists the full path and labeled fields") {
  SearchHistoryParams params = MakeParams("report.xlsx", "C:\\Users\\bruno\\Documents\\Finance", "xlsx");
  params.time_filter = static_cast<int>(TimeFilter::ThisWeek);
  const std::string tooltip = BuildHistoryEntryTooltip(MakeDisplayEntry(params, "Q1 finance"));
  CHECK(tooltip.find("Saved as: Q1 finance") != std::string::npos);
  CHECK(tooltip.find("Name: report.xlsx") != std::string::npos);
  CHECK(tooltip.find("Folder: C:\\Users\\bruno\\Documents\\Finance") != std::string::npos);
  CHECK(tooltip.find("Types: *.xlsx") != std::string::npos);
  CHECK(tooltip.find("Modified: This Week") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Last-run result count display (Phase 5)
// ---------------------------------------------------------------------------

TEST_CASE("Display subtitle includes last-run count crumb when known") {
  const SearchHistoryEntry entry = MakeDisplayEntry(MakeParams("*.cpp"), {}, 142);
  const HistoryEntryDisplayInfo info = BuildHistoryEntryDisplayInfo(entry);
  CHECK(info.subtitle.find("142 items last run") != std::string::npos);
}

TEST_CASE("Display subtitle leads with the count crumb so narrow cards keep it") {
  // Pinned Quick Access cards ellipsize subtitle line 2 at ~230px width; the
  // count must survive truncation, so it precedes the labeled filter crumbs.
  SearchHistoryParams params = MakeParams("*.cpp", "C:\\Users\\dev\\project\\src", "h,cpp");
  params.time_filter = static_cast<int>(TimeFilter::Today);
  const SearchHistoryEntry entry = MakeDisplayEntry(params, {}, 142);
  const HistoryEntryDisplayInfo info = BuildHistoryEntryDisplayInfo(entry);
  CHECK(info.subtitle.rfind("142 items last run", 0) == 0);  // starts-with
  CHECK(info.subtitle.find("Folder:") > info.subtitle.find("last run"));
}

TEST_CASE("Display subtitle uses singular for a one-item last run") {
  const SearchHistoryEntry entry = MakeDisplayEntry(MakeParams("*.cpp"), {}, 1);
  CHECK(BuildHistoryEntryDisplayInfo(entry).subtitle.find("1 item last run") != std::string::npos);
}

TEST_CASE("Display subtitle renders zero count as 'No items last run'") {
  const SearchHistoryEntry entry = MakeDisplayEntry(MakeParams("*.cpp"), {}, 0);
  CHECK(BuildHistoryEntryDisplayInfo(entry).subtitle.find("No items last run") != std::string::npos);
}

TEST_CASE("Display subtitle omits count crumb when count is unknown (-1)") {
  const SearchHistoryEntry entry = MakeDisplayEntry(MakeParams("*.cpp"), {}, -1);
  const HistoryEntryDisplayInfo info = BuildHistoryEntryDisplayInfo(entry);
  CHECK(info.subtitle.find("last run") == std::string::npos);
}

TEST_CASE("Tooltip shows Last run line with count when known") {
  const std::string tooltip = BuildHistoryEntryTooltip(MakeDisplayEntry(MakeParams("*.cpp"), {}, 42));
  CHECK(tooltip.find("Last run: 42 items") != std::string::npos);

  const std::string zero = BuildHistoryEntryTooltip(MakeDisplayEntry(MakeParams("*.cpp"), {}, 0));
  CHECK(zero.find("Last run: No items") != std::string::npos);
}

TEST_CASE("Tooltip omits Last run line when count is unknown (-1)") {
  const std::string tooltip = BuildHistoryEntryTooltip(MakeDisplayEntry(MakeParams("*.cpp"), {}, -1));
  CHECK(tooltip.find("Last run") == std::string::npos);
}

// ---------------------------------------------------------------------------
// RecordHistoryEntry: insert / upsert
// ---------------------------------------------------------------------------

TEST_CASE("RecordHistoryEntry inserts a new entry and returns its id") {
  AppSettings settings = MakeSettings();
  const std::string id = RecordHistoryEntry(MakeParams("foo.txt"), 1000, settings);
  CHECK(!id.empty());
  CHECK(settings.searchHistory.size() == 1);
  CHECK(settings.searchHistory[0].id == id);
}

TEST_CASE("RecordHistoryEntry upserts on equivalent params: updates timestamp, preserves pin") {
  AppSettings settings = MakeSettings();
  const std::string id1 = RecordHistoryEntry(MakeParams("foo.txt"), 1000, settings);

  // Pin the entry, then record again with equivalent (whitespace-variant) params.
  PinHistoryEntry(id1, 1500, settings);
  const std::string id2 = RecordHistoryEntry(MakeParams("  foo.txt  "), 2000, settings);

  CHECK(id1 == id2);
  CHECK(settings.searchHistory.size() == 1);
  CHECK(settings.searchHistory[0].last_used_at_unix_ms == 2000);
  CHECK(settings.searchHistory[0].is_pinned);  // pin preserved
}

TEST_CASE("RecordHistoryEntry stores result count on insert") {
  AppSettings settings = MakeSettings();
  const std::string id = RecordHistoryEntry(MakeParams("foo.txt"), 1000, settings, 42);
  REQUIRE(settings.searchHistory.size() == 1);
  CHECK(settings.searchHistory[0].id == id);
  CHECK(settings.searchHistory[0].last_run_result_count == 42);
}

TEST_CASE("RecordHistoryEntry default result count is unknown (-1)") {
  AppSettings settings = MakeSettings();
  (void)RecordHistoryEntry(MakeParams("foo.txt"), 1000, settings);
  REQUIRE(settings.searchHistory.size() == 1);
  CHECK(settings.searchHistory[0].last_run_result_count == -1);
}

TEST_CASE("RecordHistoryEntry stores zero result count distinct from unknown") {
  AppSettings settings = MakeSettings();
  (void)RecordHistoryEntry(MakeParams("nomatch*.txt"), 1000, settings, 0);
  REQUIRE(settings.searchHistory.size() == 1);
  CHECK(settings.searchHistory[0].last_run_result_count == 0);
}

TEST_CASE("RecordHistoryEntry upsert updates result count on re-run") {
  AppSettings settings = MakeSettings();
  (void)RecordHistoryEntry(MakeParams("foo.txt"), 1000, settings, 42);
  (void)RecordHistoryEntry(MakeParams("  foo.txt  "), 2000, settings, 17);
  REQUIRE(settings.searchHistory.size() == 1);
  CHECK(settings.searchHistory[0].last_run_result_count == 17);
  CHECK(settings.searchHistory[0].last_used_at_unix_ms == 2000);
}

TEST_CASE("RecordHistoryEntry upsert with unknown count preserves stored count") {
  AppSettings settings = MakeSettings();
  (void)RecordHistoryEntry(MakeParams("foo.txt"), 1000, settings, 42);
  // Re-record without a snapshot (-1 default): must not downgrade the known count.
  (void)RecordHistoryEntry(MakeParams("foo.txt"), 2000, settings);
  REQUIRE(settings.searchHistory.size() == 1);
  CHECK(settings.searchHistory[0].last_run_result_count == 42);
  CHECK(settings.searchHistory[0].last_used_at_unix_ms == 2000);
}

TEST_CASE("RecordHistoryEntry: two distinct searches produce two entries") {
  AppSettings settings = MakeSettings();
  (void)RecordHistoryEntry(MakeParams("foo.txt"), 1000, settings);
  (void)RecordHistoryEntry(MakeParams("bar.txt"), 2000, settings);
  CHECK(settings.searchHistory.size() == 2);
}

TEST_CASE("RecordHistoryEntry cap-trims non-pinned entries at kMaxRecentSearches") {
  AppSettings settings = MakeSettings();
  for (size_t i = 0; i <= settings_defaults::kMaxRecentSearches; ++i) {
    (void)RecordHistoryEntry(MakeParams("file" + std::to_string(i) + ".txt"),
                             static_cast<std::int64_t>(i) * 1000, settings);
  }
  size_t non_pinned = 0;
  for (const auto& e : settings.searchHistory) {
    if (!e.is_pinned) {
      ++non_pinned;
    }
  }
  CHECK(non_pinned <= settings_defaults::kMaxRecentSearches);
}

// ---------------------------------------------------------------------------
// PinHistoryEntry / UnpinHistoryEntry
// ---------------------------------------------------------------------------

TEST_CASE("PinHistoryEntry sets is_pinned and pinned_at") {
  AppSettings settings = MakeSettings();
  const std::string id = RecordHistoryEntry(MakeParams("foo.txt"), 1000, settings);
  PinHistoryEntry(id, 2000, settings);
  CHECK(settings.searchHistory[0].is_pinned);
  CHECK(settings.searchHistory[0].pinned_at_unix_ms == 2000);
}

TEST_CASE("UnpinHistoryEntry clears is_pinned and updates last_used_at") {
  AppSettings settings = MakeSettings();
  const std::string id = RecordHistoryEntry(MakeParams("foo.txt"), 1000, settings);
  PinHistoryEntry(id, 2000, settings);
  UnpinHistoryEntry(id, 3000, settings);
  CHECK(!settings.searchHistory[0].is_pinned);
  CHECK(settings.searchHistory[0].pinned_at_unix_ms == 0);
  CHECK(settings.searchHistory[0].last_used_at_unix_ms == 3000);
}

TEST_CASE("PinHistoryEntry: pinned entries are not trimmed at cap") {
  AppSettings settings = MakeSettings();
  // Fill to cap with non-pinned entries.
  for (size_t i = 0; i < settings_defaults::kMaxRecentSearches; ++i) {
    (void)RecordHistoryEntry(MakeParams("file" + std::to_string(i) + ".txt"),
                             static_cast<std::int64_t>(i) * 1000, settings);
  }
  // Pin the oldest entry.
  const std::string pinned_id = settings.searchHistory[0].id;
  PinHistoryEntry(pinned_id, 999999, settings);
  // Add one more to trigger trim.
  (void)RecordHistoryEntry(MakeParams("overflow.txt"), 9999999, settings);

  const bool pinned_still_present = std::any_of(  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
      settings.searchHistory.begin(), settings.searchHistory.end(),
      [&pinned_id](const SearchHistoryEntry& e) { return e.id == pinned_id; });
  CHECK(pinned_still_present);
}

TEST_CASE("UnpinHistoryEntry at cap triggers trim") {
  AppSettings settings = MakeSettings();
  // Insert kMaxRecentSearches entries, pin one, then insert one more (now at cap for non-pinned).
  for (size_t i = 0; i < settings_defaults::kMaxRecentSearches; ++i) {
    (void)RecordHistoryEntry(MakeParams("nf" + std::to_string(i) + ".txt"),
                             static_cast<std::int64_t>(i) * 1000, settings);
  }
  const std::string pin_id = RecordHistoryEntry(MakeParams("pinned.txt"), 9999000, settings);
  PinHistoryEntry(pin_id, 9999001, settings);

  // Non-pinned count is at kMaxRecentSearches. Unpinning should trigger trim.
  UnpinHistoryEntry(pin_id, 9999002, settings);

  size_t non_pinned = 0;
  for (const auto& e : settings.searchHistory) {
    if (!e.is_pinned) {
      ++non_pinned;
    }
  }
  CHECK(non_pinned <= settings_defaults::kMaxRecentSearches);
}

// ---------------------------------------------------------------------------
// RenameHistoryEntry / DeleteHistoryEntry
// ---------------------------------------------------------------------------

TEST_CASE("RenameHistoryEntry sets custom_name") {
  AppSettings settings = MakeSettings();
  const std::string id = RecordHistoryEntry(MakeParams("foo.txt"), 1000, settings);
  RenameHistoryEntry(id, "My Search", settings);
  CHECK(settings.searchHistory[0].custom_name == "My Search");
}

TEST_CASE("RenameHistoryEntry allows empty name (reverts to generated label)") {
  AppSettings settings = MakeSettings();
  const std::string id = RecordHistoryEntry(MakeParams("foo.txt"), 1000, settings);
  RenameHistoryEntry(id, "Name", settings);
  RenameHistoryEntry(id, "", settings);
  CHECK(settings.searchHistory[0].custom_name.empty());
}

TEST_CASE("DeleteHistoryEntry removes the entry") {
  AppSettings settings = MakeSettings();
  const std::string id = RecordHistoryEntry(MakeParams("foo.txt"), 1000, settings);
  (void)RecordHistoryEntry(MakeParams("bar.txt"), 2000, settings);
  DeleteHistoryEntry(id, settings);
  CHECK(settings.searchHistory.size() == 1);
  CHECK(settings.searchHistory[0].id != id);
}

TEST_CASE("DeleteHistoryEntry is a no-op for unknown id") {
  AppSettings settings = MakeSettings();
  (void)RecordHistoryEntry(MakeParams("foo.txt"), 1000, settings);
  DeleteHistoryEntry("nonexistent", settings);
  CHECK(settings.searchHistory.size() == 1);
}

// ---------------------------------------------------------------------------
// GetSortedHistoryView
// ---------------------------------------------------------------------------

TEST_CASE("GetSortedHistoryView: pinned before non-pinned") {
  AppSettings settings = MakeSettings();
  const std::string id_recent = RecordHistoryEntry(MakeParams("recent.txt"), 5000, settings);
  const std::string id_pinned = RecordHistoryEntry(MakeParams("pinned.txt"), 1000, settings);
  PinHistoryEntry(id_pinned, 2000, settings);

  const auto view = GetSortedHistoryView(settings);
  REQUIRE(view.size() == 2);
  CHECK(view[0]->id == id_pinned);
  CHECK(view[1]->id == id_recent);
}

TEST_CASE("GetSortedHistoryView: non-pinned sorted by last_used DESC") {
  AppSettings settings = MakeSettings();
  const std::string id_old = RecordHistoryEntry(MakeParams("old.txt"), 1000, settings);
  const std::string id_new = RecordHistoryEntry(MakeParams("new.txt"), 9000, settings);

  const auto view = GetSortedHistoryView(settings);
  REQUIRE(view.size() == 2);
  CHECK(view[0]->id == id_new);
  CHECK(view[1]->id == id_old);
}

TEST_CASE("GetSortedHistoryView: pinned entries sorted by pinned_at DESC") {
  AppSettings settings = MakeSettings();
  const std::string id_a = RecordHistoryEntry(MakeParams("a.txt"), 1000, settings);
  const std::string id_b = RecordHistoryEntry(MakeParams("b.txt"), 1000, settings);
  PinHistoryEntry(id_a, 1001, settings);
  PinHistoryEntry(id_b, 9999, settings);  // pinned later → should appear first

  const auto view = GetSortedHistoryView(settings);
  REQUIRE(view.size() == 2);
  CHECK(view[0]->id == id_b);
  CHECK(view[1]->id == id_a);
}

// ---------------------------------------------------------------------------
// NotifyPinAction (status-bar toast) + ExportWorkflowState notify helpers
// ---------------------------------------------------------------------------

TEST_CASE("NotifyPinAction pin success names the headline and clears error") {
  ExportWorkflowState banner;
  banner.error_message = "stale error";
  NotifyPinAction(banner, "*.cpp", /*is_pinned_now=*/true, /*persist_ok=*/true);
  CHECK(banner.notification == "Pinned \"*.cpp\" to Quick Access");
  CHECK(banner.error_message.empty());
}

TEST_CASE("NotifyPinAction unpin success reports removal from Quick Access") {
  ExportWorkflowState banner;
  banner.notification = "stale message";
  NotifyPinAction(banner, "Invoices", /*is_pinned_now=*/false, /*persist_ok=*/true);
  CHECK(banner.notification == "Unpinned \"Invoices\" from Quick Access");
  CHECK(banner.error_message.empty());
}

TEST_CASE("NotifyPinAction persist failure wins with the error variant") {
  ExportWorkflowState banner;
  NotifyPinAction(banner, "*.cpp", /*is_pinned_now=*/true, /*persist_ok=*/false);
  CHECK(banner.notification.empty());
  CHECK(banner.error_message == "Failed to save Quick Access change (see log)");
}

TEST_CASE("NotifyPinAction refreshes the toast expiry timestamp") {
  ExportWorkflowState banner;
  const auto before = std::chrono::steady_clock::now();
  NotifyPinAction(banner, "*.cpp", /*is_pinned_now=*/true, /*persist_ok=*/true);
  CHECK(banner.notification_time >= before);
}

TEST_CASE("ExportWorkflowState NotifySuccess and NotifyError replace each other") {
  ExportWorkflowState banner;
  banner.NotifyError("boom");
  CHECK(banner.error_message == "boom");
  CHECK(banner.notification.empty());
  banner.NotifySuccess("ok");
  CHECK(banner.notification == "ok");
  CHECK(banner.error_message.empty());
}

// ---------------------------------------------------------------------------
// Serialization round-trip (in-memory settings mode)
// ---------------------------------------------------------------------------

TEST_CASE("SearchHistoryEntry survives SaveSettings / LoadSettings round-trip") {
  AppSettings settings = MakeSettings();
  SearchHistoryParams params = MakeParams("report.xlsx", "C:\\Finance", "xlsx", false, true, 2, 1024);
  params.ai_search_description = "quarterly report";
  const std::string id = RecordHistoryEntry(
      params,
      5000, settings, /*result_count=*/7);
  PinHistoryEntry(id, 6000, settings);
  RenameHistoryEntry(id, "Finance Reports", settings);

  test_settings::SetInMemorySettings(settings);
  AppSettings loaded;
  const bool ok = LoadSettings(loaded);
  test_settings::ClearInMemorySettings();

  REQUIRE(ok);
  REQUIRE(loaded.searchHistory.size() == 1);
  const SearchHistoryEntry& e = loaded.searchHistory[0];
  CHECK(e.id == id);
  CHECK(e.custom_name == "Finance Reports");
  CHECK(e.is_pinned);
  CHECK(e.pinned_at_unix_ms == 6000);
  CHECK(e.last_used_at_unix_ms == 5000);
  CHECK(e.last_run_result_count == 7);
  CHECK(e.params.filename == "report.xlsx");
  CHECK(e.params.path == "C:\\Finance");
  CHECK(e.params.extensions == "xlsx");
  CHECK(e.params.case_sensitive);
  CHECK(e.params.time_filter == 2);
  CHECK(e.params.size_filter == 1024);
  CHECK(e.params.ai_search_description == "quarterly report");
}

TEST_CASE("SearchHistoryEntry round-trip preserves FilesOnly item_type") {
  AppSettings settings = MakeSettings();
  SearchHistoryParams params = MakeParams("report.xlsx", "C:\\Finance", "xlsx", false, true, 2, 1024);
  params.item_type = 1; // FilesOnly
  const std::string id = RecordHistoryEntry(params, 5000, settings);

  test_settings::SetInMemorySettings(settings);
  AppSettings loaded;
  const bool ok = LoadSettings(loaded);
  test_settings::ClearInMemorySettings();

  REQUIRE(ok);
  REQUIRE(loaded.searchHistory.size() == 1);
  const SearchHistoryEntry& e = loaded.searchHistory[0];
  CHECK(e.id == id);
  CHECK(e.params.item_type == 1);
  CHECK_FALSE(e.params.folders_only);

  const auto info = BuildHistoryEntryDisplayInfo(e);
  CHECK(info.subtitle.find("Files only") != std::string::npos);

  const auto tooltip = BuildHistoryEntryTooltip(e);
  CHECK(tooltip.find("Files only") != std::string::npos);
}

TEST_CASE("NormalizeHistoryParams clamps out-of-range item_type and syncs folders_only") {
  SearchHistoryParams params = MakeParams("test.txt");
  params.item_type = 99;
  params.folders_only = false;

  const SearchHistoryParams normalized = NormalizeHistoryParams(params);
  CHECK(normalized.item_type == 0); // Reset to All
  CHECK_FALSE(normalized.folders_only);

  // When item_type is All (0) but folders_only is true, normalizes to FoldersOnly (2)
  SearchHistoryParams legacy = MakeParams("folder");
  legacy.item_type = 0;
  legacy.folders_only = true;

  const SearchHistoryParams normalized_legacy = NormalizeHistoryParams(legacy);
  CHECK(normalized_legacy.item_type == 2);
  CHECK(normalized_legacy.folders_only);
}

// ---------------------------------------------------------------------------
// SeedDefaultPinnedSearchesIfNeeded
// ---------------------------------------------------------------------------

TEST_CASE("SeedDefaultPinnedSearchesIfNeeded seeds 10 default pinned entries when history is empty") {
  AppSettings settings = MakeSettings();
  CHECK(settings.searchHistory.empty());

  SeedDefaultPinnedSearchesIfNeeded(settings);

  REQUIRE(settings.searchHistory.size() == 10);
  for (const auto& entry : settings.searchHistory) {
    CHECK(entry.is_pinned);
    CHECK(!entry.id.empty());
    CHECK(!entry.custom_name.empty());
  }

  // Spot-check key default template custom names
  CHECK(settings.searchHistory[0].custom_name == "*.cpp");
  CHECK(settings.searchHistory[1].custom_name == "Makefile");
  CHECK(settings.searchHistory[6].custom_name == "Folders only");
  CHECK(settings.searchHistory[7].custom_name == "Modified today");
}

TEST_CASE("SeedDefaultPinnedSearchesIfNeeded does nothing when history already contains entries") {
  AppSettings settings = MakeSettings();
  (void)RecordHistoryEntry(MakeParams("custom.txt"), 1000, settings);
  REQUIRE(settings.searchHistory.size() == 1);

  SeedDefaultPinnedSearchesIfNeeded(settings);

  CHECK(settings.searchHistory.size() == 1);
  CHECK(settings.searchHistory[0].params.filename == "custom.txt");
}

// ---------------------------------------------------------------------------
// RecordCompletedManualSearch (P0-T2 extraction: gate + record + persist seam)
// ---------------------------------------------------------------------------

namespace {

// Persist seam for RecordCompletedManualSearch tests: counts invocations.
bool CountingPersist(void* context) {
  auto* count = static_cast<int*>(context);
  ++(*count);
  return true;
}

SearchParams MakeSearchParams(std::string_view filename) {
  SearchParams params;
  params.filenameInput.assign(filename);
  return params;
}

}  // namespace

TEST_CASE("RecordCompletedManualSearch gate-false records nothing and never persists") {
  GuiState state;
  AppSettings settings = MakeSettings();
  int persist_count = 0;

  // Default pipeline state: no manual flag, no pending record, search inactive.
  const std::string id = RecordCompletedManualSearch(
      state, MakeSearchParams("*.cpp"), settings, &CountingPersist, &persist_count);

  CHECK(id.empty());
  CHECK(settings.searchHistory.empty());
  CHECK_EQ(persist_count, 0);
}

TEST_CASE("RecordCompletedManualSearch records once for a completed manual search") {
  GuiState state;
  state.search_pipeline.search_was_manual = true;
  state.search_pipeline.search_active = false;
  state.search_pipeline.completed_manual_result_count = 7;
  AppSettings settings = MakeSettings();
  int persist_count = 0;

  const std::string id = RecordCompletedManualSearch(
      state, MakeSearchParams("*.cpp"), settings, &CountingPersist, &persist_count);

  CHECK_FALSE(id.empty());
  REQUIRE_EQ(settings.searchHistory.size(), 1u);
  CHECK_EQ(settings.searchHistory.at(0).id, id);
  CHECK_EQ(settings.searchHistory.at(0).last_run_result_count, 7);
  CHECK_EQ(persist_count, 1);
}

TEST_CASE("RecordCompletedManualSearch ignores manual flag while a search is still active") {
  GuiState state;
  state.search_pipeline.search_was_manual = true;
  state.search_pipeline.search_active = true;   // still running → not a completion
  AppSettings settings = MakeSettings();
  int persist_count = 0;

  const std::string id = RecordCompletedManualSearch(
      state, MakeSearchParams("*.cpp"), settings, &CountingPersist, &persist_count);

  CHECK(id.empty());
  CHECK(settings.searchHistory.empty());
  CHECK_EQ(persist_count, 0);
}

TEST_CASE("RecordCompletedManualSearch records via pending flag even if auto-refresh replaced the search") {
  // Same-frame manual-complete + auto-refresh takeover (see ShouldRecordManualSearchCompletion):
  // search_was_manual was cleared and search is active again, but the pending
  // one-shot flag set by PollResults must still record the completed query.
  GuiState state;
  state.search_pipeline.search_was_manual = false;
  state.search_pipeline.search_active = true;
  state.search_pipeline.pending_manual_history_record = true;
  AppSettings settings = MakeSettings();
  int persist_count = 0;

  const std::string id = RecordCompletedManualSearch(
      state, MakeSearchParams("Makefile"), settings, &CountingPersist, &persist_count);

  CHECK_FALSE(id.empty());
  REQUIRE_EQ(settings.searchHistory.size(), 1u);
  CHECK_EQ(persist_count, 1);
  CHECK_FALSE(state.search_pipeline.pending_manual_history_record);  // consumed
}

TEST_CASE("RecordCompletedManualSearch consumes the one-shot record flag and count snapshot") {
  GuiState state;
  state.search_pipeline.pending_manual_history_record = true;
  state.search_pipeline.completed_manual_result_count = 42;
  AppSettings settings = MakeSettings();
  int persist_count = 0;

  (void)RecordCompletedManualSearch(
      state, MakeSearchParams("*.md"), settings, &CountingPersist, &persist_count);

  CHECK_FALSE(state.search_pipeline.pending_manual_history_record);
  CHECK_EQ(state.search_pipeline.completed_manual_result_count, -1);

  // A second call with no fresh manual completion is a no-op (flag consumed).
  const std::string second_id = RecordCompletedManualSearch(
      state, MakeSearchParams("*.md"), settings, &CountingPersist, &persist_count);
  CHECK(second_id.empty());
  CHECK_EQ(persist_count, 1);
  CHECK_EQ(settings.searchHistory.size(), 1u);
}

TEST_CASE("RecordCompletedManualSearch returns stable ids for identical params (re-entrancy proof)") {
  // Guards the navigation stack dedup rule: a navigated re-run completes through
  // this function and must return the same id so NavPush sees id == stack[cursor].
  GuiState state;
  state.search_pipeline.pending_manual_history_record = true;
  AppSettings settings = MakeSettings();
  int persist_count = 0;

  const std::string first = RecordCompletedManualSearch(
      state, MakeSearchParams("*.h"), settings, &CountingPersist, &persist_count);
  state.search_pipeline.pending_manual_history_record = true;
  const std::string second = RecordCompletedManualSearch(
      state, MakeSearchParams("*.h"), settings, &CountingPersist, &persist_count);

  CHECK_FALSE(first.empty());
  CHECK_EQ(first, second);
  CHECK_EQ(settings.searchHistory.size(), 1u);  // upsert, not duplicate insert
}

// ---------------------------------------------------------------------------
// NavPush / NavBack / NavForward / NavDrop (P1-T2, spec §3 + §6 invariants)
// ---------------------------------------------------------------------------

namespace {

SearchNavigationState MakeNav(std::initializer_list<std::string_view> ids) {
  SearchNavigationState nav;
  for (const auto& id : ids) {
    NavPush(nav, id);
  }
  return nav;
}

}  // namespace

TEST_CASE("NavPush empty then A,B,C: ordering, cursor at tip, availability (US-1)") {
  auto nav = MakeNav({"A", "B", "C"});

  REQUIRE_EQ(nav.stack.size(), 3u);
  CHECK_EQ(nav.stack.at(0), std::string("A"));
  CHECK_EQ(nav.stack.at(1), std::string("B"));
  CHECK_EQ(nav.stack.at(2), std::string("C"));
  CHECK_EQ(nav.cursor, 2u);
  CHECK(NavCanGoBack(nav));
  CHECK_FALSE(NavCanGoForward(nav));
}

TEST_CASE("NavPush dedup at cursor is a complete no-op (US-5 re-run keeps forward tail)") {
  auto nav = MakeNav({"A", "B", "C"});
  REQUIRE(NavBack(nav));  // cursor -> B
  REQUIRE_EQ(nav.cursor, 1u);

  NavPush(nav, "B");  // re-running the current entry must not truncate C

  CHECK_EQ(nav.stack.size(), 3u);
  CHECK_EQ(nav.cursor, 1u);
  CHECK_EQ(nav.stack.at(2), std::string("C"));
  CHECK(NavCanGoForward(nav));  // C still reachable
}

TEST_CASE("NavPush new distinct search truncates the forward tail (US-4)") {
  auto nav = MakeNav({"A", "B", "C"});
  REQUIRE(NavBack(nav));  // cursor -> B (C in forward tail)

  NavPush(nav, "D");

  REQUIRE_EQ(nav.stack.size(), 2u + 1u);
  CHECK_EQ(nav.stack.at(0), std::string("A"));
  CHECK_EQ(nav.stack.at(1), std::string("B"));
  CHECK_EQ(nav.stack.at(2), std::string("D"));
  CHECK_EQ(nav.cursor, 2u);
  CHECK_FALSE(NavCanGoForward(nav));  // C truncated away
}

TEST_CASE("NavPush dedup only guards the cursor position (browser push-state semantics)") {
  // Back to A, then re-run C: A != stack[cursor]==A? No: cursor is A, id is C
  // -> truncate tail (B), push C -> [A, C]. Duplicate ids away from the cursor
  // are allowed (dedup is push-position-only, spec §6 invariant 2 note).
  auto nav = MakeNav({"A", "B", "C"});
  REQUIRE(NavBack(nav));  // B
  REQUIRE(NavBack(nav));  // A

  NavPush(nav, "C");

  REQUIRE_EQ(nav.stack.size(), 2u);
  CHECK_EQ(nav.stack.at(0), std::string("A"));
  CHECK_EQ(nav.stack.at(1), std::string("C"));
  CHECK_EQ(nav.cursor, 1u);
}

TEST_CASE("NavPush overflow at cap keeps size bounded, drops oldest, keeps cursor entry") {
  SearchNavigationState nav;
  for (size_t i = 0; i <= kMaxNavigationStackDepth; ++i) {  // kMax+1 distinct ids
    NavPush(nav, "id" + std::to_string(i));
  }

  CHECK_EQ(nav.stack.size(), kMaxNavigationStackDepth);
  CHECK_EQ(nav.cursor, kMaxNavigationStackDepth - 1);
  // Oldest ("id0") was erased; the just-pushed cursor entry survived.
  CHECK_EQ(nav.stack.front(), std::string("id1"));
  CHECK_EQ(nav.stack.at(nav.cursor), std::string("id") + std::to_string(kMaxNavigationStackDepth));
}

TEST_CASE("NavBack/NavForward bounds: empty stack, single entry, and both ends") {
  SearchNavigationState empty;
  CHECK_FALSE(NavCanGoBack(empty));
  CHECK_FALSE(NavCanGoForward(empty));
  CHECK_FALSE(NavBack(empty));
  CHECK_FALSE(NavForward(empty));
  CHECK_EQ(empty.cursor, 0u);  // cursor stays 0, no underflow

  auto single = MakeNav({"A"});
  CHECK_FALSE(NavCanGoBack(single));
  CHECK_FALSE(NavCanGoForward(single));
  CHECK_FALSE(NavBack(single));
  CHECK_FALSE(NavForward(single));
  CHECK_EQ(single.cursor, 0u);

  auto nav = MakeNav({"A", "B", "C"});
  CHECK_FALSE(NavForward(nav));  // already at tip -> false, cursor unchanged
  CHECK_EQ(nav.cursor, 2u);
  REQUIRE(NavBack(nav));         // cursor 1
  CHECK(NavForward(nav));        // -> cursor 2 again
  REQUIRE(NavBack(nav));
  REQUIRE(NavBack(nav));         // cursor 0 (A)
  CHECK_FALSE(NavBack(nav));
  CHECK_EQ(nav.cursor, 0u);
  REQUIRE(NavForward(nav));
  CHECK_EQ(nav.cursor, 1u);
}

TEST_CASE("NavDrop index below cursor shifts cursor down") {
  auto nav = MakeNav({"A", "B", "C", "D"});  // cursor 3
  NavDrop(nav, 0);                            // drop A (before cursor)

  REQUIRE_EQ(nav.stack.size(), 3u);
  CHECK_EQ(nav.cursor, 2u);
  CHECK_EQ(nav.stack.at(nav.cursor), std::string("D"));  // cursor still on D
}

TEST_CASE("NavDrop at cursor keeps cursor on the neighbor shifted into the slot (US-7 rule)") {
  auto nav = MakeNav({"A", "B", "C"});
  REQUIRE(NavBack(nav));  // cursor 1 (B)

  NavDrop(nav, 1);  // drop B while cursor points at B

  REQUIRE_EQ(nav.stack.size(), 2u);
  CHECK_EQ(nav.cursor, 1u);
  CHECK_EQ(nav.stack.at(nav.cursor), std::string("C"));  // C shifted into slot 1
}

TEST_CASE("NavDrop index above cursor leaves cursor untouched") {
  auto nav = MakeNav({"A", "B", "C"});  // cursor 2... move back first
  REQUIRE(NavBack(nav));                // cursor 1

  NavDrop(nav, 2);  // drop C (after cursor)

  REQUIRE_EQ(nav.stack.size(), 2u);
  CHECK_EQ(nav.cursor, 1u);
  CHECK_EQ(nav.stack.at(nav.cursor), std::string("B"));
}

TEST_CASE("NavDrop last element and only element clamp cursor") {
  auto nav = MakeNav({"A", "B"});  // cursor 1 = B
  NavDrop(nav, 1);                 // drop the cursor element at the end
  REQUIRE_EQ(nav.stack.size(), 1u);
  CHECK_EQ(nav.cursor, 0u);        // clamped: A now under the cursor

  NavDrop(nav, 0);                 // drop the only element
  CHECK(nav.stack.empty());
  CHECK_EQ(nav.cursor, 0u);        // empty-stack convention
}

TEST_CASE("Nav stack invariants hold across a push/back/push/drop sequence") {
  auto nav = MakeNav({"A", "B", "C"});
  REQUIRE(NavBack(nav));
  NavPush(nav, "D");
  NavDrop(nav, 0);
  REQUIRE(NavBack(nav));

  CHECK(!nav.stack.empty());
  CHECK(nav.cursor < nav.stack.size());
  CHECK(nav.stack.size() <= kMaxNavigationStackDepth);
}

// ---------------------------------------------------------------------------
// NavigateToHistoryId / NavigateHistory (P1-T3, spec §3.1.2-4 + US-7)
// ---------------------------------------------------------------------------

namespace {

// Settings with three recorded entries (distinct params -> distinct ids).
struct NavFixture {
  AppSettings settings = MakeSettings();
  std::string id_a;
  std::string id_b;
  std::string id_c;
};

NavFixture MakeNavFixture() {
  NavFixture fx;
  fx.id_a = RecordHistoryEntry(MakeParams("alpha.txt"), 1000, fx.settings);
  fx.id_b = RecordHistoryEntry(MakeParams("beta.txt", "/tmp"), 1100, fx.settings);
  fx.id_c = RecordHistoryEntry(MakeParams("gamma.txt"), 1200, fx.settings);
  REQUIRE(!fx.id_a.empty());
  REQUIRE(fx.id_a != fx.id_b);
  REQUIRE(fx.id_b != fx.id_c);
  return fx;
}

}  // namespace

TEST_CASE("NavigateToHistoryId hit: applies entry params and keeps the stack") {
  const NavFixture fx = MakeNavFixture();
  auto nav = MakeNav({fx.id_a, fx.id_b, fx.id_c});
  GuiState state;

  const bool navigated = NavigateToHistoryId(nav, nav.cursor, state, fx.settings, nullptr);

  CHECK(navigated);
  CHECK_EQ(state.searchCriteria.filename_input.AsString(), std::string("gamma.txt"));
  CHECK(state.input_debounce.input_changed);  // MarkInputChanged fired
  REQUIRE_EQ(nav.stack.size(), 3u);           // read-only on the stack
  CHECK_EQ(nav.cursor, 2u);
}

TEST_CASE("NavigateToHistoryId miss: drops the dead id, fixes up cursor, returns false") {
  NavFixture fx = MakeNavFixture();
  auto nav = MakeNav({fx.id_a, fx.id_b, fx.id_c});
  DeleteHistoryEntry(fx.id_b, fx.settings);  // user deletes B from the history panel
  GuiState state;

  const bool navigated = NavigateToHistoryId(nav, 1, state, fx.settings, nullptr);

  CHECK_FALSE(navigated);
  REQUIRE_EQ(nav.stack.size(), 2u);
  CHECK_EQ(nav.stack.at(0), fx.id_a);
  CHECK_EQ(nav.stack.at(1), fx.id_c);
  CHECK_EQ(nav.cursor, 1u);  // stayed on slot 1 -> now references C (retry candidate)
  CHECK(state.searchCriteria.filename_input.AsString().empty());  // nothing applied
}

TEST_CASE("NavigateHistory back/forward re-run the adjacent entries (US-2/US-3)") {
  const NavFixture fx = MakeNavFixture();
  auto nav = MakeNav({fx.id_a, fx.id_b, fx.id_c});
  GuiState state;

  NavigateHistory(-1, nav, state, fx.settings, nullptr);
  CHECK_EQ(nav.cursor, 1u);
  CHECK_EQ(state.searchCriteria.filename_input.AsString(), std::string("beta.txt"));

  NavigateHistory(-1, nav, state, fx.settings, nullptr);
  CHECK_EQ(nav.cursor, 0u);
  CHECK_EQ(state.searchCriteria.filename_input.AsString(), std::string("alpha.txt"));
  CHECK_EQ(state.searchCriteria.path_input.AsString(), std::string(""));

  NavigateHistory(+1, nav, state, fx.settings, nullptr);
  CHECK_EQ(nav.cursor, 1u);
  CHECK_EQ(state.searchCriteria.path_input.AsString(), std::string("/tmp"));  // B's path restored
  CHECK_EQ(state.searchCriteria.filename_input.AsString(), std::string("beta.txt"));

  NavigateHistory(+1, nav, state, fx.settings, nullptr);
  CHECK_EQ(nav.cursor, 2u);
  CHECK_EQ(state.searchCriteria.filename_input.AsString(), std::string("gamma.txt"));
}

TEST_CASE("NavigateHistory at both boundaries is a silent no-op (US-8)") {
  const NavFixture fx = MakeNavFixture();
  auto nav = MakeNav({fx.id_a});  // single entry: cursor 0 is both floor and tip
  GuiState state;
  state.searchCriteria.filename_input.SetValue("SENTINEL");

  NavigateHistory(-1, nav, state, fx.settings, nullptr);  // cursor 0: Back disabled
  NavigateHistory(+1, nav, state, fx.settings, nullptr);  // tip: Forward disabled

  CHECK_EQ(nav.cursor, 0u);
  CHECK_EQ(state.searchCriteria.filename_input.AsString(), std::string("SENTINEL"));
  CHECK_FALSE(state.input_debounce.input_changed);

  // Cold start: empty stack, both directions inert.
  SearchNavigationState cold;
  NavigateHistory(-1, cold, state, fx.settings, nullptr);
  NavigateHistory(+1, cold, state, fx.settings, nullptr);
  CHECK(cold.stack.empty());
  CHECK_EQ(cold.cursor, 0u);
  CHECK_EQ(state.searchCriteria.filename_input.AsString(), std::string("SENTINEL"));
}

TEST_CASE("NavigateHistory skips a deleted middle entry via drop + single retry (US-7 drill, P2-T2)") {
  NavFixture fx = MakeNavFixture();
  auto nav = MakeNav({fx.id_a, fx.id_b, fx.id_c});
  DeleteHistoryEntry(fx.id_b, fx.settings);  // B dies while stack still references it
  GuiState state;

  // Back from the tip (C): cursor lands on B -> miss -> B dropped, cursor fix-up
  // leaves it on C (the neighbor shifted into the slot) -> retry applies C.
  NavigateHistory(-1, nav, state, fx.settings, nullptr);

  CHECK_EQ(nav.stack.size(), 2u);
  CHECK(nav.stack.at(0) == fx.id_a);
  CHECK(nav.stack.at(1) == fx.id_c);
  CHECK_EQ(nav.cursor, 1u);
  CHECK_EQ(state.searchCriteria.filename_input.AsString(), std::string("gamma.txt"));

  // Second consecutive press steps past the dead zone onto A.
  NavigateHistory(-1, nav, state, fx.settings, nullptr);
  CHECK_EQ(nav.cursor, 0u);
  CHECK_EQ(state.searchCriteria.filename_input.AsString(), std::string("alpha.txt"));

  // Further presses at the exhausted end stay silent.
  NavigateHistory(-1, nav, state, fx.settings, nullptr);
  CHECK_EQ(nav.cursor, 0u);
  CHECK_EQ(state.searchCriteria.filename_input.AsString(), std::string("alpha.txt"));
}

TEST_CASE("NavigateHistory double miss removes dead ids without applying or crashing") {
  NavFixture fx = MakeNavFixture();
  auto nav = MakeNav({fx.id_a, fx.id_b, fx.id_c});
  DeleteHistoryEntry(fx.id_b, fx.settings);
  DeleteHistoryEntry(fx.id_c, fx.settings);  // both candidates dead
  GuiState state;
  state.searchCriteria.filename_input.SetValue("SENTINEL");

  NavigateHistory(-1, nav, state, fx.settings, nullptr);

  // At most one retry (§3.1.4): B dropped (+cursor fix-up), C dropped by the
  // retry; A survives at the sole slot with a sane cursor; nothing applied.
  CHECK_EQ(nav.stack.size(), 1u);
  CHECK(nav.stack.at(0) == fx.id_a);
  CHECK(nav.cursor < nav.stack.size());
  CHECK_EQ(state.searchCriteria.filename_input.AsString(), std::string("SENTINEL"));
  CHECK_FALSE(state.input_debounce.input_changed);
}

// ---------------------------------------------------------------------------
// RecordCompletedManualSearch -> NavPush wiring (P2-T1, US-1/US-5/US-6)
// ---------------------------------------------------------------------------

namespace {

// Drive one recorded manual completion through the real push path:
// set the PollResults one-shot flag, run RecordCompletedManualSearch, return
// the recorded id ("" when the gate declined).
std::string CompleteManualSearch(GuiState& state, std::string_view filename,
                                 AppSettings& settings, int& persist_count) {
  state.search_pipeline.pending_manual_history_record = true;
  return RecordCompletedManualSearch(state, MakeSearchParams(filename), settings,
                                     &CountingPersist, &persist_count);
}

}  // namespace

TEST_CASE("Manual completion pushes the recorded id onto the navigation stack tip (US-1)") {
  GuiState state;
  AppSettings settings = MakeSettings();
  int persist_count = 0;

  const std::string first = CompleteManualSearch(state, "one.txt", settings, persist_count);
  const std::string second = CompleteManualSearch(state, "two.txt", settings, persist_count);

  REQUIRE_FALSE(first.empty());
  REQUIRE_FALSE(second.empty());
  REQUIRE_EQ(state.navigation.stack.size(), 2u);
  CHECK_EQ(state.navigation.stack.at(0), first);
  CHECK_EQ(state.navigation.stack.at(1), second);
  CHECK_EQ(state.navigation.cursor, 1u);
  CHECK(NavCanGoBack(state.navigation));
  CHECK_FALSE(NavCanGoForward(state.navigation));
}

TEST_CASE("Auto-refresh / non-manual completion never pushes (US-6)") {
  GuiState state;
  AppSettings settings = MakeSettings();
  int persist_count = 0;
  (void)CompleteManualSearch(state, "seed.txt", settings, persist_count);  // one legit push

  // Gate-false completion: search still active, no pending flag, not manual.
  state.search_pipeline.search_was_manual = true;
  state.search_pipeline.search_active = true;
  const std::string declined = RecordCompletedManualSearch(
      state, MakeSearchParams("auto.txt"), settings, &CountingPersist, &persist_count);

  CHECK(declined.empty());
  CHECK_EQ(state.navigation.stack.size(), 1u);  // stack untouched
  CHECK_EQ(persist_count, 1);
}

TEST_CASE("Re-running the cursor's search dedups: forward tail survives (US-5 end-to-end)") {
  GuiState state;
  AppSettings settings = MakeSettings();
  int persist_count = 0;
  const std::string id_a = CompleteManualSearch(state, "a.txt", settings, persist_count);
  const std::string id_b = CompleteManualSearch(state, "b.txt", settings, persist_count);
  REQUIRE(NavBack(state.navigation));  // user steps back to A (B is the forward tail)
  REQUIRE_EQ(state.navigation.cursor, 0u);

  // A's re-run (F5 / navigated re-trigger) completes through the same push path.
  const std::string id_a_again = CompleteManualSearch(state, "a.txt", settings, persist_count);

  CHECK_EQ(id_a_again, id_a);  // deterministic canonical hash -> same id
  REQUIRE_EQ(state.navigation.stack.size(), 2u);
  CHECK_EQ(state.navigation.cursor, 0u);        // no cursor jump
  CHECK_EQ(state.navigation.stack.at(1), id_b); // B NOT truncated
  CHECK(NavCanGoForward(state.navigation));     // forward tail intact
}

TEST_CASE("New distinct search after Back truncates the forward tail (US-4 end-to-end)") {
  GuiState state;
  AppSettings settings = MakeSettings();
  int persist_count = 0;
  const std::string id_a = CompleteManualSearch(state, "a.txt", settings, persist_count);
  (void)CompleteManualSearch(state, "b.txt", settings, persist_count);
  (void)CompleteManualSearch(state, "c.txt", settings, persist_count);
  REQUIRE(NavBack(state.navigation));  // cursor 1 (B), C in forward tail
  REQUIRE(NavBack(state.navigation));  // cursor 0 (A)

  const std::string id_d = CompleteManualSearch(state, "d.txt", settings, persist_count);

  REQUIRE_EQ(state.navigation.stack.size(), 2u);
  CHECK_EQ(state.navigation.stack.at(0), id_a);
  CHECK_EQ(state.navigation.stack.at(1), id_d);  // B/C tail truncated, D pushed
  CHECK_EQ(state.navigation.cursor, 1u);
  CHECK_FALSE(NavCanGoForward(state.navigation));
}

// ---------------------------------------------------------------------------
// P2-T2 deleted-entry resilience drill (spec §8.6 / US-7, driven at the API
// seam since the real UI history-panel deletion has no headless equivalent)
// ---------------------------------------------------------------------------

TEST_CASE("Resilience drill: five-entry stack with two dead ids resolves every press safely") {
  NavFixture fx = MakeNavFixture();
  const std::string id_d = RecordHistoryEntry(MakeParams("delta.txt"), 1300, fx.settings);
  const std::string id_e = RecordHistoryEntry(MakeParams("echo.txt"), 1400, fx.settings);
  auto nav = MakeNav({fx.id_a, fx.id_b, fx.id_c, id_d, id_e});  // cursor 4 (tip)

  // User deletes B and D from the history panel while the stack references them.
  DeleteHistoryEntry(fx.id_b, fx.settings);
  DeleteHistoryEntry(id_d, fx.settings);

  GuiState state;
  CHECK_EQ(nav.cursor, 4u);

  // Back #1: D (slot 3) dead -> dropped, cursor fix-up leaves slot 3 holding E
  // -> retry applies E.
  NavigateHistory(-1, nav, state, fx.settings, nullptr);
  CHECK_EQ(nav.stack.size(), 4u);
  CHECK(nav.cursor < nav.stack.size());
  CHECK_EQ(state.searchCriteria.filename_input.AsString(), std::string("echo.txt"));

  // Back #2: lands on C directly (no dead id in the way).
  NavigateHistory(-1, nav, state, fx.settings, nullptr);
  CHECK_EQ(state.searchCriteria.filename_input.AsString(), std::string("gamma.txt"));

  // Back #3: B (slot 1) dead -> dropped, cursor fix-up leaves slot 1 holding C
  // -> retry re-applies C (the neighbor shifted into the dead slot).
  NavigateHistory(-1, nav, state, fx.settings, nullptr);
  CHECK_EQ(nav.stack.size(), 3u);
  CHECK_EQ(nav.cursor, 1u);
  CHECK_EQ(state.searchCriteria.filename_input.AsString(), std::string("gamma.txt"));

  // Back #4 steps cleanly onto A; press #5 at the exhausted floor is silent.
  NavigateHistory(-1, nav, state, fx.settings, nullptr);
  CHECK_EQ(nav.cursor, 0u);
  CHECK_EQ(state.searchCriteria.filename_input.AsString(), std::string("alpha.txt"));
  NavigateHistory(-1, nav, state, fx.settings, nullptr);
  CHECK_EQ(nav.cursor, 0u);

  // All remaining ids resolve; press at the exhausted floor is silent.
  for (const auto& id : nav.stack) {
    CHECK(FindHistoryEntryById(fx.settings.searchHistory, id) != nullptr);
  }
  const size_t size_before = nav.stack.size();
  NavigateHistory(-1, nav, state, fx.settings, nullptr);
  CHECK_EQ(nav.stack.size(), size_before);  // no drop without a navigation attempt
  CHECK_EQ(nav.cursor, 0u);

  // Forward sweep across the previously dead zone lands on live neighbours.
  NavigateHistory(+1, nav, state, fx.settings, nullptr);
  CHECK_EQ(state.searchCriteria.filename_input.AsString(), std::string("gamma.txt"));
  NavigateHistory(+1, nav, state, fx.settings, nullptr);
  CHECK_EQ(state.searchCriteria.filename_input.AsString(), std::string("echo.txt"));
  CHECK_EQ(nav.cursor, nav.stack.size() - 1);
  NavigateHistory(+1, nav, state, fx.settings, nullptr);  // tip: silent
  CHECK_EQ(nav.cursor, nav.stack.size() - 1);
  CHECK(nav.stack.size() <= kMaxNavigationStackDepth);
}
