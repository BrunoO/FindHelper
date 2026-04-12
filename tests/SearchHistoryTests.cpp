/**
 * @file SearchHistoryTests.cpp
 * @brief Unit tests for the in-memory search history mutation API.
 *
 * Tests cover:
 * - GenerateHistoryId: stability and distinctness
 * - NormalizeHistoryParams: dedup equivalence, extension normalization
 * - RecordHistoryEntry: insert, upsert, cap-trim, return value
 * - PinHistoryEntry / UnpinHistoryEntry invariants, unpin-at-cap
 * - RenameHistoryEntry / DeleteHistoryEntry
 * - GetSortedHistoryView: pinned-before-recent ordering
 * - Serialization round-trip via SaveSettings / LoadSettings (in-memory mode)
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "core/Settings.h"
#include "search/SearchHistory.h"

#include <algorithm>
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
// Serialization round-trip (in-memory settings mode)
// ---------------------------------------------------------------------------

TEST_CASE("SearchHistoryEntry survives SaveSettings / LoadSettings round-trip") {
  AppSettings settings = MakeSettings();
  SearchHistoryParams params = MakeParams("report.xlsx", "C:\\Finance", "xlsx", false, true, 2, 1024);
  params.ai_search_description = "quarterly report";
  const std::string id = RecordHistoryEntry(
      params,
      5000, settings);
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
  CHECK(e.params.filename == "report.xlsx");
  CHECK(e.params.path == "C:\\Finance");
  CHECK(e.params.extensions == "xlsx");
  CHECK(e.params.case_sensitive);
  CHECK(e.params.time_filter == 2);
  CHECK(e.params.size_filter == 1024);
  CHECK(e.params.ai_search_description == "quarterly report");
}
