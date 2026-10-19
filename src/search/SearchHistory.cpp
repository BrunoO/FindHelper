/**
 * @file SearchHistory.cpp
 * @brief Implementation of in-memory search history mutations.
 *
 * Persistence (JSON load/save) lives in Settings.cpp; this module is
 * purely in-memory: normalize, hash, upsert, pin/unpin, rename, delete,
 * sort.
 */

#include "search/SearchHistory.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/Settings.h"
#include "filters/SizeFilter.h"
#include "filters/TimeFilter.h"
#include "gui/GuiState.h"
#include "gui/UIActions.h"
#include "gui/WorkflowStates.h"
#include "utils/Logger.h"
#include "utils/StringSearch.h"
#include "utils/StringUtils.h"

namespace {

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------

[[nodiscard]] std::string TrimWhitespace(std::string_view sv) {
  const auto start = sv.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos) {
    return {};
  }
  const auto end = sv.find_last_not_of(" \t\r\n");
  return std::string(sv.substr(start, end - start + 1));
}

// Split a comma-separated string, trim and lowercase each token, remove empties.
[[nodiscard]] std::vector<std::string> SplitAndNormalizeExtensions(std::string_view ext) {
  std::vector<std::string> parts;
  std::string_view remaining = ext;
  while (!remaining.empty()) {
    const auto comma = remaining.find(',');
    const std::string_view token = (comma == std::string_view::npos)
        ? remaining
        : remaining.substr(0, comma);
    std::string trimmed = TrimWhitespace(token);
    // Lowercase
    for (char& c : trimmed) {
      c = string_search::ToLowerChar(static_cast<unsigned char>(c));
    }
    if (!trimmed.empty()) {
      parts.push_back(std::move(trimmed));
    }
    if (comma == std::string_view::npos) {
      break;
    }
    remaining = remaining.substr(comma + 1);
  }
  std::sort(parts.begin(), parts.end());  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
  return parts;
}

// ---------------------------------------------------------------------------
// FNV-1a 64-bit hash (stable across runs — pure function of input bytes)
// ---------------------------------------------------------------------------

constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

[[nodiscard]] uint64_t Fnv1a64(std::string_view data) {
  uint64_t hash = kFnvOffsetBasis;
  for (const char c : data) {
    hash ^= static_cast<uint64_t>(static_cast<uint8_t>(c));
    hash *= kFnvPrime;
  }
  return hash;
}

[[nodiscard]] std::string ToHex16(uint64_t value) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0') << std::setw(16) << value;
  return oss.str();
}

// ---------------------------------------------------------------------------
// Canonical string for hashing
// ---------------------------------------------------------------------------

[[nodiscard]] std::string BuildCanonicalString(const SearchHistoryParams& p) {
  const std::vector<std::string> ext_parts = SplitAndNormalizeExtensions(p.extensions);
  std::string ext_joined;
  for (const auto& part : ext_parts) {
    if (!ext_joined.empty()) {
      ext_joined += ',';
    }
    ext_joined += part;
  }

  std::ostringstream oss;
  oss << "filename=" << p.filename
      << "|path=" << p.path
      << "|extensions=" << ext_joined
      << "|item_type=" << p.item_type
      << "|folders_only=" << (p.folders_only ? '1' : '0')
      << "|case_sensitive=" << (p.case_sensitive ? '1' : '0')
      << "|time_filter=" << p.time_filter
      << "|size_filter=" << p.size_filter
      << "|ai=" << p.ai_search_description;
  return oss.str();
}

// ---------------------------------------------------------------------------
// Cap trim: remove oldest non-pinned entries over kMaxRecentSearches
// ---------------------------------------------------------------------------

struct NonPinnedScanResult {
  size_t count = 0;
  std::vector<SearchHistoryEntry>::iterator oldest;
};

[[nodiscard]] NonPinnedScanResult ScanNonPinnedEntries(std::vector<SearchHistoryEntry>& history) {
  NonPinnedScanResult result{0, history.end()};
  for (auto it = history.begin(); it != history.end(); ++it) {
    if (it->is_pinned) {
      continue;
    }
    ++result.count;
    if (result.oldest == history.end() ||
        it->last_used_at_unix_ms < result.oldest->last_used_at_unix_ms ||
        (it->last_used_at_unix_ms == result.oldest->last_used_at_unix_ms && it->id < result.oldest->id)) {
      result.oldest = it;
    }
  }
  return result;
}

void TrimNonPinnedEntries(AppSettings& settings) {
  auto scan = ScanNonPinnedEntries(settings.searchHistory);
  while (scan.count > settings_defaults::kMaxRecentSearches &&
         scan.oldest != settings.searchHistory.end()) {
    settings.searchHistory.erase(scan.oldest);
    --scan.count;
    if (scan.count > settings_defaults::kMaxRecentSearches) {
      scan.oldest = ScanNonPinnedEntries(settings.searchHistory).oldest;
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

SearchHistoryParams NormalizeHistoryParams(SearchHistoryParams params) {
  params.filename = TrimWhitespace(params.filename);
  params.path = TrimWhitespace(params.path);
  params.ai_search_description = TrimWhitespace(params.ai_search_description);

  // Normalize extensions: split, trim, lowercase, sort, rejoin
  const std::vector<std::string> ext_parts = SplitAndNormalizeExtensions(params.extensions);
  std::string ext_joined;
  for (const auto& part : ext_parts) {
    if (!ext_joined.empty()) {
      ext_joined += ',';
    }
    ext_joined += part;
  }
  params.extensions = std::move(ext_joined);

  // Normalize item type: clamp corrupted values to All, then derive folders_only
  // from the effective filter so canonical IDs are stable (single source of truth).
  // Note: pre-itemType entries hashed without |item_type=| will upsert to a new ID
  // once after upgrade — intentional one-time migration.
  if (!IsValidItemTypeFilterValue(params.item_type)) {
    params.item_type = static_cast<int>(ItemTypeFilter::All);
  }
  const ItemTypeFilter effective =
      EffectiveItemTypeFilter(static_cast<ItemTypeFilter>(params.item_type), params.folders_only);
  params.item_type = static_cast<int>(effective);
  params.folders_only = (effective == ItemTypeFilter::FoldersOnly);

  return params;
}

std::string GenerateHistoryId(const SearchHistoryParams& params) {
  const SearchHistoryParams normalized = NormalizeHistoryParams(params);
  const std::string canonical = BuildCanonicalString(normalized);
  return ToHex16(Fnv1a64(canonical));
}

std::string RecordHistoryEntry(SearchHistoryParams params,
                               std::int64_t now_ms,
                               AppSettings& settings,
                               std::int64_t result_count) {
  const SearchHistoryParams normalized = NormalizeHistoryParams(std::move(params));
  const std::string canonical = BuildCanonicalString(normalized);
  const std::string base_id = ToHex16(Fnv1a64(canonical));

  // Find an existing entry that matches by id, handling hash collisions via suffix scan.
  std::string effective_id = base_id;
  int suffix = 0;
  while (true) {
    const auto it = std::find_if(  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
        settings.searchHistory.begin(), settings.searchHistory.end(),
        [&effective_id](const SearchHistoryEntry& e) { return e.id == effective_id; });

    if (it == settings.searchHistory.end()) {
      break;  // ID not yet taken — this is our slot
    }

    // Check if the stored entry has the same canonical params (true dedup)
    if (BuildCanonicalString(NormalizeHistoryParams(it->params)) == canonical) {
      // Match: upsert — update timestamp, preserve pin/name/created.
      // A known count (>= 0) replaces the stored one; -1 = "this run did not
      // snapshot a count" and must never downgrade a previously known value.
      it->last_used_at_unix_ms = now_ms;
      if (result_count >= 0) {
        it->last_run_result_count = result_count;
      }
      TrimNonPinnedEntries(settings);
      return effective_id;
    }

    // Hash collision: different canonical payload shares the same base id.
    // Advance to the next suffix slot.
    ++suffix;
    effective_id = base_id + "-" + std::to_string(suffix);
  }

  // Insert new non-pinned entry
  SearchHistoryEntry entry;
  entry.id = effective_id;
  entry.params = normalized;
  entry.is_pinned = false;
  entry.created_at_unix_ms = now_ms;
  entry.last_used_at_unix_ms = now_ms;
  entry.pinned_at_unix_ms = 0;
  entry.last_run_result_count = result_count;
  settings.searchHistory.push_back(std::move(entry));

  TrimNonPinnedEntries(settings);
  return effective_id;
}

std::string RecordCompletedManualSearch(GuiState& state, const SearchParams& last_search_params,
                                        AppSettings& settings,
                                        const std::function<bool()>& persist_settings) {
  if (!ShouldRecordManualSearchCompletion(state.search_pipeline.search_was_manual,
                                          state.search_pipeline.search_active,
                                          state.search_pipeline.pending_manual_history_record)) {
    return {};
  }
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  const std::string recorded_id = RecordHistoryEntry(
      BuildHistoryParams(last_search_params, state), now_ms, settings,
      state.search_pipeline.completed_manual_result_count);
  // Single push point for the Back/Forward stack (spec §3.1.1): inside the
  // gate, so auto-refresh / non-manual completions can never advance the
  // stack, and navigated re-runs dedup at NavPush's cursor check.
  NavPush(state.navigation, recorded_id);
  if (persist_settings && !persist_settings()) {
    LOG_ERROR("Failed to persist search history after search completed");
  }
  state.search_pipeline.ConsumeManualHistoryRecord();
  return recorded_id;
}

void PinHistoryEntry(std::string_view id, std::int64_t now_ms, AppSettings& settings) {
  auto* const entry = FindHistoryEntryById(settings.searchHistory, id);
  if (entry == nullptr) {
    return;
  }
  entry->is_pinned = true;
  entry->pinned_at_unix_ms = now_ms;
}

void UnpinHistoryEntry(std::string_view id, std::int64_t now_ms, AppSettings& settings) {
  auto* const entry = FindHistoryEntryById(settings.searchHistory, id);
  if (entry == nullptr) {
    return;
  }
  entry->is_pinned = false;
  entry->pinned_at_unix_ms = 0;
  entry->last_used_at_unix_ms = now_ms;  // treat as most-recently-used after unpin
  TrimNonPinnedEntries(settings);
}

void NotifyPinAction(ExportWorkflowState& banner, std::string_view headline,
                     bool is_pinned_now, bool persist_ok) {
  if (!persist_ok) {
    banner.NotifyError("Failed to save Quick Access change (see log)");
    return;
  }
  if (is_pinned_now) {
    banner.NotifySuccess("Pinned \"" + std::string(headline) + "\" to Quick Access");
  } else {
    banner.NotifySuccess("Unpinned \"" + std::string(headline) + "\" from Quick Access");
  }
}

void ToggleHistoryPinWithFeedback(const SearchHistoryEntry& entry, bool pin,
                                  AppSettings& settings, std::int64_t now_ms,
                                  ui::UIActions* actions, GuiState& state) {
  // Headline captured BEFORE the toggle: unpin trims the vector, invalidating
  // entry references on erase/shift.
  const std::string headline = BuildHistoryEntryDisplayInfo(entry).headline;
  if (pin) {
    PinHistoryEntry(entry.id, now_ms, settings);
  } else {
    UnpinHistoryEntry(entry.id, now_ms, settings);
  }
  const bool persist_ok = (actions != nullptr) && actions->PersistSettings();
  if (!persist_ok) {
    LOG_ERROR("Failed to persist search history pin/unpin change");
  }
  NotifyPinAction(state.export_workflow, headline, pin, persist_ok);
}

void RenameHistoryEntry(std::string_view id, std::string_view name, AppSettings& settings) {
  auto* const entry = FindHistoryEntryById(settings.searchHistory, id);
  if (entry == nullptr) {
    return;
  }
  entry->custom_name.assign(name);
}

void DeleteHistoryEntry(std::string_view id, AppSettings& settings) {
  const auto* const entry = FindHistoryEntryById(settings.searchHistory, id);
  if (entry == nullptr) {
    return;
  }
  // Pointer -> iterator (FindHistoryEntryById's single-scan contract keeps the
  // predicate in one place; erase needs an iterator).
  settings.searchHistory.erase(settings.searchHistory.begin() +
                               (entry - settings.searchHistory.data()));
}

// Single source of truth for the plain find-by-id scan over a history vector.
SearchHistoryEntry* FindHistoryEntryById(std::vector<SearchHistoryEntry>& history,
                                         std::string_view id) {
  for (auto& entry : history) {
    if (entry.id == id) {
      return &entry;
    }
  }
  return {};
}

const SearchHistoryEntry* FindHistoryEntryById(const std::vector<SearchHistoryEntry>& history,
                                               std::string_view id) {
  for (const auto& entry : history) {
    if (entry.id == id) {
      return &entry;
    }
  }
  return {};
}

void SeedDefaultPinnedSearchesIfNeeded(AppSettings& settings) {
  if (!settings.searchHistory.empty()) {
    return;
  }

  struct DefaultTemplate {
    const char* custom_name;  // NOLINT(readability-identifier-naming)
    const char* filename;     // NOLINT(readability-identifier-naming)
    const char* extensions;   // NOLINT(readability-identifier-naming)
    int item_type;            // NOLINT(readability-identifier-naming)
    bool folders_only;        // NOLINT(readability-identifier-naming)
    TimeFilter time_filter;   // NOLINT(readability-identifier-naming)
    SizeFilter size_filter;   // NOLINT(readability-identifier-naming)
  };

  static constexpr std::array<DefaultTemplate, 10> kDefaults = {
    // NOSONAR(cpp:S5945) - Using std::array
    DefaultTemplate{"*.cpp", "*.cpp", "", 0, false, TimeFilter::None, SizeFilter::None},
    DefaultTemplate{"Makefile", "Makefile", "", 0, false, TimeFilter::None, SizeFilter::None},
    DefaultTemplate{"^main", "^main", "", 0, false, TimeFilter::None, SizeFilter::None},
    DefaultTemplate{"*.h", "", "h", 0, false, TimeFilter::None, SizeFilter::None},
    DefaultTemplate{"*.py", "*.py", "", 0, false, TimeFilter::None, SizeFilter::None},
    DefaultTemplate{"*.json", "*.json", "", 0, false, TimeFilter::None, SizeFilter::None},
    DefaultTemplate{"Folders only", "", "", 2, true, TimeFilter::None, SizeFilter::None},
    DefaultTemplate{"Modified today", "", "", 0, false, TimeFilter::Today, SizeFilter::None},
    DefaultTemplate{"Empty files", "", "", 0, false, TimeFilter::None, SizeFilter::Empty},
    DefaultTemplate{"Large files (>10MB)", "", "", 0, false, TimeFilter::None, SizeFilter::Large},
  };

  constexpr std::int64_t kBaseTimestamp = 1700000000000LL;
  settings.searchHistory.reserve(kDefaults.size());

  for (size_t i = 0; i < kDefaults.size(); ++i) {
    const DefaultTemplate& tmpl = kDefaults.at(i);
    SearchHistoryParams params;
    params.filename = tmpl.filename;
    params.extensions = tmpl.extensions;
    params.item_type = tmpl.item_type;
    params.folders_only = tmpl.folders_only;
    params.time_filter = static_cast<int>(tmpl.time_filter);
    params.size_filter = static_cast<std::uint64_t>(tmpl.size_filter);

    SearchHistoryEntry entry;
    entry.id = GenerateHistoryId(params);
    entry.params = params;
    entry.custom_name = tmpl.custom_name;
    entry.is_pinned = true;
    const std::int64_t ts = kBaseTimestamp - (static_cast<std::int64_t>(i) * 1000LL);
    entry.created_at_unix_ms = ts;
    entry.last_used_at_unix_ms = ts;
    entry.pinned_at_unix_ms = ts;

    settings.searchHistory.push_back(std::move(entry));
  }
}

std::vector<const SearchHistoryEntry*> GetSortedHistoryView(const AppSettings& settings) {
  std::vector<const SearchHistoryEntry*> result;
  result.reserve(settings.searchHistory.size());
  for (const auto& e : settings.searchHistory) {
    result.push_back(&e);
  }
  std::sort(  // NOLINT(llvm-use-ranges,bugprone-nondeterministic-pointer-iteration-order) - C++17; comparator is fully deterministic (id tie-break) — pointer value never used
      result.begin(), result.end(),
      [](const SearchHistoryEntry* a, const SearchHistoryEntry* b) {
        // Pinned entries before non-pinned
        if (a->is_pinned != b->is_pinned) {
          return a->is_pinned;
        }
        if (a->is_pinned) {
          // Both pinned: pinned_at DESC, last_used DESC, id ASC
          if (a->pinned_at_unix_ms != b->pinned_at_unix_ms) {
            return a->pinned_at_unix_ms > b->pinned_at_unix_ms;
          }
          if (a->last_used_at_unix_ms != b->last_used_at_unix_ms) {
            return a->last_used_at_unix_ms > b->last_used_at_unix_ms;
          }
          return a->id < b->id;
        }
        // Both non-pinned (recent): last_used DESC, id ASC
        if (a->last_used_at_unix_ms != b->last_used_at_unix_ms) {
          return a->last_used_at_unix_ms > b->last_used_at_unix_ms;
        }
        return a->id < b->id;
      });
  return result;
}

SearchHistoryParams BuildHistoryParams(const SearchParams& params, const GuiState& state) {
  SearchHistoryParams p;
  p.filename = params.filenameInput;
  p.path = params.pathInput;
  p.extensions = params.extensionInput;
  const ItemTypeFilter effective =
      EffectiveItemTypeFilter(params.itemTypeFilter, params.foldersOnly);
  p.item_type = static_cast<int>(effective);
  p.folders_only = (effective == ItemTypeFilter::FoldersOnly);
  p.case_sensitive = params.caseSensitive;
  p.time_filter = static_cast<int>(state.searchCriteria.time_filter);
  p.size_filter = static_cast<std::uint64_t>(state.searchCriteria.size_filter);
  p.ai_search_description = std::string(state.gemini.description_input.data());
  return p;
}

void ApplyHistoryEntryToGuiState(const SearchHistoryEntry& entry, GuiState& state) {
  state.searchCriteria.path_input.SetValue(entry.params.path);
  state.searchCriteria.extension_input.SetValue(entry.params.extensions);
  state.searchCriteria.filename_input.SetValue(entry.params.filename);
  // Validate persisted enum (corrupted/hand-edited JSON falls back to All via ItemTypeFilterFromInt);
  // legacy folders_only flag is honored only when effective filter is All.
  const ItemTypeFilter effective = EffectiveItemTypeFilter(
      ItemTypeFilterFromInt(entry.params.item_type), entry.params.folders_only);
  state.searchCriteria.SetItemTypeFilter(effective);
  state.searchCriteria.case_sensitive = entry.params.case_sensitive;
  state.searchCriteria.time_filter = static_cast<TimeFilter>(entry.params.time_filter);
  state.searchCriteria.size_filter = static_cast<SizeFilter>(entry.params.size_filter);
  strcpy_safe(state.gemini.description_input.data(),
              state.gemini.description_input.size(),
              entry.params.ai_search_description.c_str());
  state.MarkInputChanged();
}

void ApplyAndRunHistoryEntry(const SearchHistoryEntry& entry, GuiState& state,
                             ui::UIActions* actions) {
  ApplyHistoryEntryToGuiState(entry, state);
  if (actions != nullptr) {
    actions->TriggerManualSearch(state);
  }
}

// ---------------------------------------------------------------------------
// Browser-style back/forward navigation stack (spec §3/§4.1, §6 invariants)
// ---------------------------------------------------------------------------

void NavPush(SearchNavigationState& nav, std::string_view id) {
  assert(!id.empty() && "NavPush requires a recorded history id");
  // Dedup rule (the re-entrancy guard): a re-run of the entry the cursor
  // already points at is a COMPLETE no-op — no resize, no truncate — so a
  // navigated search completing back into this push path keeps cursor AND
  // forward tail intact (spec §3.1.2).
  if (!nav.stack.empty() && nav.stack.at(nav.cursor) == id) {
    return;
  }
  if (!nav.stack.empty()) {
    nav.stack.resize(nav.cursor + 1);  // drop the forward tail (browser semantics)
  }
  nav.stack.emplace_back(id);  // owning std::string copy — never a view into settings
  nav.cursor = nav.stack.size() - 1;
  // Overflow (§4.1): drop the oldest entry that is NOT the cursor entry so the
  // currently displayed search survives; NavDrop shifts the cursor down when an
  // earlier index is erased. Size can exceed the cap by at most one here.
  while (nav.stack.size() > kMaxNavigationStackDepth) {
    const size_t oldest_not_cursor = (nav.cursor == 0) ? 1U : 0U;
    NavDrop(nav, oldest_not_cursor);
  }
  assert(nav.stack.size() <= kMaxNavigationStackDepth);
  assert(nav.cursor < nav.stack.size());
}

bool NavCanGoBack(const SearchNavigationState& nav) {
  return nav.cursor > 0;
}

bool NavCanGoForward(const SearchNavigationState& nav) {
  return nav.cursor + 1 < nav.stack.size();
}

bool NavBack(SearchNavigationState& nav) {
  if (!NavCanGoBack(nav)) {
    return false;
  }
  --nav.cursor;
  assert(nav.cursor < nav.stack.size());
  return true;
}

bool NavForward(SearchNavigationState& nav) {
  if (!NavCanGoForward(nav)) {
    return false;
  }
  ++nav.cursor;
  assert(nav.cursor < nav.stack.size());
  return true;
}

void NavDrop(SearchNavigationState& nav, size_t index) {
  if (index >= nav.stack.size()) {
    assert(false && "NavDrop index out of bounds — callers pass cursor-range indices");
    return;
  }
  nav.stack.erase(nav.stack.begin() + static_cast<std::ptrdiff_t>(index));
  if (index < nav.cursor) {
    --nav.cursor;  // entries below the cursor shifted left
  }
  // index == cursor: cursor STAYS — it now references the entry that shifted
  // into the slot, the next candidate in the direction of travel (§3.1.4).
  // index > cursor: untouched. Clamp covers drop-last / drop-only-element.
  nav.cursor = nav.stack.empty() ? 0 : (std::min)(nav.cursor, nav.stack.size() - 1);
  assert(nav.stack.empty() || nav.cursor < nav.stack.size());
}

bool NavigateToHistoryId(SearchNavigationState& nav, size_t index, GuiState& state,
                         const AppSettings& settings, ui::UIActions* actions) {
  if (index >= nav.stack.size()) {
    assert(false && "NavigateToHistoryId index out of bounds");
    return false;
  }
  const SearchHistoryEntry* const entry =
      FindHistoryEntryById(settings.searchHistory, nav.stack.at(index));
  if (entry == nullptr) {
    // Deleted elsewhere (history panel): drop the dead id and let the caller
    // decide about retrying the neighbor now under the fixed-up cursor (§3.1.4).
    NavDrop(nav, index);
    return false;
  }
  ApplyAndRunHistoryEntry(*entry, state, actions);
  return true;
}

void NavigateHistory(int direction, SearchNavigationState& nav, GuiState& state,
                     const AppSettings& settings, ui::UIActions* actions) {
  // Boundary press (Back at cursor 0 / Forward at tip / empty stack) exits as a
  // silent no-op: no cursor move, no state touch, no search trigger (§3.1.6).
  if (const bool moved = (direction < 0) ? NavBack(nav) : NavForward(nav); !moved) {
    return;
  }
  if (NavigateToHistoryId(nav, nav.cursor, state, settings, actions)) {
    return;
  }
  // Miss path: NavigateToHistoryId already dropped the dead id and fixed up the
  // cursor (it now references the neighbor that shifted into the slot). Retry
  // that candidate exactly once; a second miss stays a silent no-op (§3.1.4).
  if (nav.cursor < nav.stack.size()) {
    (void)NavigateToHistoryId(nav, nav.cursor, state, settings, actions);
  }
}

namespace {

// Path display: keep short paths as-is; for deeper trees show the last two segments.
[[nodiscard]] std::string TruncatePathDisplay(std::string_view path) {
  if (path.empty()) {
    return {};
  }

  std::vector<std::string_view> segments;
  char join_sep = '/';
  size_t start = 0;
  while (start < path.size()) {
    const size_t slash = path.find_first_of("/\\", start);
    if (slash == std::string_view::npos) {
      segments.push_back(path.substr(start));
      break;
    }
    if (slash > start) {
      segments.push_back(path.substr(start, slash - start));
    }
    join_sep = path.substr(slash, 1).front();
    start = slash + 1;
  }

  if (segments.size() <= 2) {
    return std::string(path);
  }

  const std::string_view second_last = segments.at(segments.size() - 2);
  const std::string_view last = segments.back();
  std::string out = "...";
  out += join_sep;
  out.append(second_last.data(), second_last.size());
  out += join_sep;
  out.append(last.data(), last.size());
  return out;
}

[[nodiscard]] std::string FormatExtensionsForDisplay(std::string_view extensions) {
  if (extensions.empty()) {
    return {};
  }
  const std::vector<std::string> ext_list = ParseExtensions(extensions, ',');
  if (ext_list.empty()) {
    return {};
  }
  const auto join_extensions = [](const std::vector<std::string>& list, size_t count) {
    if (list.empty()) {
      return std::string();
    }
    std::string s = "*." + list.at(0);
    for (size_t i = 1; i < count && i < list.size(); ++i) {
      s += "," + list.at(i);
    }
    return s;
  };
  if (ext_list.size() <= 3) {
    return join_extensions(ext_list, ext_list.size());
  }
  std::string result = join_extensions(ext_list, 2);
  const size_t remaining = ext_list.size() - 2;
  result += " (+" + std::to_string(remaining) + " more)";
  return result;
}

[[nodiscard]] std::string JoinWithSeparator(const std::vector<std::string>& parts,
                                            std::string_view separator) {
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
    if (i > 0) {
      out.append(separator.data(), separator.size());
    }
    out += parts.at(i);
  }
  return out;
}

[[nodiscard]] std::string HistoryModifiedLabel(TimeFilter filter) {
  const char* const label = TimeFilterDisplayLabel(filter);
  if (label[0] == '\0') {
    return {};
  }
  return std::string("Modified: ") + label;
}

[[nodiscard]] std::string HistorySizeDisplay(SizeFilter filter) {
  if (filter == SizeFilter::None) {
    return {};
  }
  return {SizeFilterDisplayLabel(filter)};
}

// Shared last-run count phrase for the subtitle crumb and the tooltip line:
// "No items" | "1 item" | "N items". Callers must gate on count >= 0
// (-1 = unknown, rendered as nothing).
[[nodiscard]] std::string LastRunCountPhrase(std::int64_t count) {
  if (count == 0) {
    return {"No items"};
  }
  return std::to_string(count) + (count == 1 ? " item" : " items");
}

void AppendTooltipLine(std::vector<std::string>& lines, std::string_view label,
                       std::string_view value) {
  if (value.empty()) {
    return;
  }
  lines.push_back(std::string(label) + ": " + std::string(value));
}

struct StructuredHeadlineParts {
  std::string text;
  bool shows_path = false;
  bool shows_name = false;
  bool shows_types = false;
  bool shows_folders_only = false;
};

[[nodiscard]] StructuredHeadlineParts BuildStructuredHeadline(
    const SearchHistoryParams& params, std::string_view truncated_path) {
  StructuredHeadlineParts parts;
  std::string what;
  const ItemTypeFilter effective =
      EffectiveItemTypeFilter(ItemTypeFilterFromInt(params.item_type), params.folders_only);
  const bool is_folders_only = (effective == ItemTypeFilter::FoldersOnly);
  const bool is_files_only = (effective == ItemTypeFilter::FilesOnly);
  if (!params.filename.empty()) {
    what = params.filename;
    parts.shows_name = true;
  } else if (!params.extensions.empty()) {
    what = FormatExtensionsForDisplay(params.extensions);
    parts.shows_types = true;
  } else if (is_folders_only) {
    what = "Folders only";
    parts.shows_folders_only = true;
  } else if (is_files_only) {
    what = "Files only";
    parts.shows_folders_only = true;
  }

  if (!what.empty() && !truncated_path.empty()) {
    parts.text = what + " in " + std::string(truncated_path);
    parts.shows_path = true;
    return parts;
  }
  if (!what.empty()) {
    parts.text = std::move(what);
    return parts;
  }
  if (!truncated_path.empty()) {
    parts.text = std::string(truncated_path);
    parts.shows_path = true;
  }
  return parts;
}

}  // namespace

HistoryEntryDisplayInfo BuildHistoryEntryDisplayInfo(const SearchHistoryEntry& entry) {
  HistoryEntryDisplayInfo info;
  const SearchHistoryParams& params = entry.params;
  const std::string truncated_path = TruncatePathDisplay(params.path);
  const std::string time_label = HistoryModifiedLabel(static_cast<TimeFilter>(params.time_filter));
  const std::string size_display = HistorySizeDisplay(static_cast<SizeFilter>(params.size_filter));

  bool path_in_headline = false;
  bool name_in_headline = false;
  bool types_in_headline = false;
  bool folders_in_headline = false;

  if (!entry.custom_name.empty()) {
    info.headline = entry.custom_name;
  } else if (!params.ai_search_description.empty()) {
    info.headline = "\"" + params.ai_search_description + "\"";
  } else {
    const StructuredHeadlineParts structured =
        BuildStructuredHeadline(params, truncated_path);
    if (!structured.text.empty()) {
      info.headline = structured.text;
      path_in_headline = structured.shows_path;
      name_in_headline = structured.shows_name;
      types_in_headline = structured.shows_types;
      folders_in_headline = structured.shows_folders_only;
    } else if (!time_label.empty()) {
      info.headline = time_label;
    } else if (!size_display.empty()) {
      info.headline = size_display;
    } else {
      info.headline = "All files";
    }
  }

  std::vector<std::string> crumbs;
  // Point-in-time snapshot of the last completed run (engine match count) leads
  // the subtitle: pinned Quick Access cards clip this line to one ellipsized
  // row at narrow widths, so a trailing crumb would be the first thing lost.
  // Unknown (-1, e.g. legacy/seeded entries) renders nothing; 0 is meaningful.
  if (entry.last_run_result_count >= 0) {
    crumbs.push_back(LastRunCountPhrase(entry.last_run_result_count) + " last run");
  }
  if (!params.path.empty() && !path_in_headline) {
    crumbs.push_back("Folder: " + truncated_path);
  }
  if (!params.filename.empty() && !name_in_headline) {
    crumbs.push_back("Name: " + params.filename);
  }
  if (!params.extensions.empty() && !types_in_headline) {
    crumbs.push_back("Types: " + FormatExtensionsForDisplay(params.extensions));
  }
  if (const ItemTypeFilter crumb_effective =
          EffectiveItemTypeFilter(ItemTypeFilterFromInt(params.item_type), params.folders_only);
      crumb_effective == ItemTypeFilter::FoldersOnly && !folders_in_headline) {
    crumbs.emplace_back("Folders only");
  } else if (crumb_effective == ItemTypeFilter::FilesOnly && !folders_in_headline) {
    crumbs.emplace_back("Files only");
  }
  if (!time_label.empty() && info.headline != time_label) {
    crumbs.push_back(time_label);
  }
  if (!size_display.empty() && info.headline != size_display) {
    crumbs.push_back("Size: " + size_display);
  }
  if (params.case_sensitive) {
    crumbs.emplace_back("Match case");
  }
  info.subtitle = JoinWithSeparator(crumbs, " • ");
  assert(!info.headline.empty());
  return info;
}

std::string BuildHistoryEntryTooltip(const SearchHistoryEntry& entry) {
  const SearchHistoryParams& params = entry.params;
  std::vector<std::string> lines;
  if (!entry.custom_name.empty()) {
    AppendTooltipLine(lines, "Saved as", entry.custom_name);
  }
  if (!params.ai_search_description.empty()) {
    AppendTooltipLine(lines, "Asked as", params.ai_search_description);
  }
  AppendTooltipLine(lines, "Name", params.filename);
  AppendTooltipLine(lines, "Folder", params.path);
  AppendTooltipLine(lines, "Types", FormatExtensionsForDisplay(params.extensions));
  AppendTooltipLine(lines, "Modified",
                    TimeFilterDisplayLabel(static_cast<TimeFilter>(params.time_filter)));
  AppendTooltipLine(lines, "Size", HistorySizeDisplay(static_cast<SizeFilter>(params.size_filter)));
  if (const ItemTypeFilter tooltip_effective =
          EffectiveItemTypeFilter(ItemTypeFilterFromInt(params.item_type), params.folders_only);
      tooltip_effective == ItemTypeFilter::FoldersOnly) {
    lines.emplace_back("Folders only");
  } else if (tooltip_effective == ItemTypeFilter::FilesOnly) {
    lines.emplace_back("Files only");
  }
  if (params.case_sensitive) {
    lines.emplace_back("Match case");
  }
  if (entry.last_run_result_count >= 0) {
    AppendTooltipLine(lines, "Last run", LastRunCountPhrase(entry.last_run_result_count));
  }
  if (lines.empty()) {
    return {"All files"};
  }
  return JoinWithSeparator(lines, "\n");
}
