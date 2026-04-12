2026-03-31_Unified Search History Specification
==============================================

## 1. Overview

**Feature**: Merge "Saved Searches" and "Recent Searches" into one unified feature: **Search History**.

**Core idea**: A saved search is a recent search with additional metadata (pinned state and optional custom name).

### Goals

- Reduce UI duplication and reclaim space by replacing two separate panels with one.
- Keep fast access to automatic recents while preserving "durable favorites" behavior.
- Use a single data model and storage path for maintainability and future extension.

### Non-goals

- Cloud sync for history entries.
- Multi-device conflict resolution.
- Backward compatibility with previous saved/recent settings schemas.

---

## 2. Terminology

- **History entry**: one stored search configuration that can be re-run.
- **Recent entry**: a non-pinned history entry managed by recency rules.
- **Pinned entry**: a durable history entry (previously "saved search"), optionally renamed by user.
- **Display name**:
  - Pinned: optional user-provided name; fallback to generated label.
  - Recent: generated label only.

---

## 3. Product Requirements

### 3.1 Functional Requirements

1. **Single unified surface**
   - App shows one UI entry point: `Search History`.
   - Separate "Saved Searches" and "Recent Searches" sections are removed from navigation.

2. **Automatic recent capture**
   - A search execution creates or updates a history entry.
   - If equivalent query already exists, do not duplicate; update `last_used_at`.

3. **Pin/unpin behavior**
   - Any entry can be pinned (promoted to durable favorite).
   - Unpinning turns it back into a recent entry.

4. **Optional custom name**
   - User can assign a custom name to pinned entries.
   - Clearing custom name reverts to generated label.

5. **Retention policy**
   - Non-pinned recents are capped by `kMaxRecentSearches`.
   - Pinned entries are not removed by recent-cap trimming.

6. **Run and manage**
   - User can re-run, pin/unpin, rename (pinned), and delete any entry from same list.

7. **Ordering**
   - Pinned entries shown first.
   - Pinned entries are sorted by `pinned_at_unix_ms` descending (most recently pinned first).
   - Tie-breaker for pinned entries: `last_used_at_unix_ms` descending, then `id` ascending for deterministic order.
   - Recents shown below, sorted by `last_used_at` descending.
   - Tie-breaker for recent entries: `id` ascending for deterministic order.

8. **Keyboard behavior**
   - No dedicated saved/recent keyboard shortcut parity is required as a baseline.
   - If history-specific keyboard navigation/activation is introduced, it must target unified Search History actions only (single behavior path).

9. **Cmd+P / Ctrl+P global shortcut — pin current search**
   - Toggles pin state on the last executed search (the same params source used by `RecordHistoryEntry`).
   - Silent: no popup or name-entry dialog. The entry keeps its existing `custom_name` if already set.
   - No-op when no search has been executed in the current session.
   - Replaces `ShortcutAction::SaveSearch` (Ctrl+S / Cmd+S) which is removed in Phase 4.

### 3.2 UX Requirements

- One panel with two subsections:
  - `Pinned`
  - `Recent`
- Placement:
  - The unified Search History panel lives in the current Recent Searches surface (i.e. `EmptyState`).
  - `FilterPanel` does not host a separate second history implementation.
  - `FilterPanel` may show a lightweight shortcut/link that opens/focuses Search History in `EmptyState`, but all list rendering and actions are single-source in `EmptyState`.
  - The current Saved Searches UI line in `FilterPanel` is removed to reclaim horizontal/vertical space for primary filters and results controls.
- Empty states:
  - No entries: "No search history yet."
  - No pinned: hide or collapse pinned section.
- Optional lightweight filter chips: `All | Pinned | Recent` (same dataset, no duplication).

---

## 4. Data Model

Introduce one canonical model (name illustrative):

```cpp
struct SearchHistoryEntry {
  std::string id;
  SearchHistoryParams params;
  std::string custom_name;
  bool is_pinned = false;
  std::int64_t created_at_unix_ms = 0;
  std::int64_t last_used_at_unix_ms = 0;
  std::int64_t pinned_at_unix_ms = 0;  // 0 when not pinned
};
```

`SearchHistoryParams` is the persisted search payload. It is distinct from the existing runtime `SearchParams` struct in `search/SearchTypes.h` (which is the search-engine operational type and lacks time/size/AI fields). Do not reuse or extend the runtime struct.

```cpp
struct SearchHistoryParams {
  std::string filename;
  std::string path;
  std::string extensions;
  bool folders_only = false;
  bool case_sensitive = false;
  int time_filter = 0;
  std::uint64_t size_filter = 0;
  std::string ai_search_description;
};
```

This is intentionally aligned with the current saved-search payload semantics; the unification work changes storage shape and behavior, not the meaning of individual search filters.
Note on field typing: this spec intentionally normalizes `time_filter` and `size_filter` to numeric types (`int`, `std::uint64_t`). Existing persisted `SavedSearch` payloads may represent these as strings; serializer/deserializer logic for the new schema must use numeric encoding.

### 4.1 Invariants

- `id` is stable and unique.
- `last_used_at_unix_ms >= created_at_unix_ms`.
- `pinned_at_unix_ms > 0` iff `is_pinned == true`.
- Only one source of truth for "search history" entries in settings/state.

### 4.2 ID generation strategy

- `id` is generated as a deterministic hash of the normalized dedup key (`SearchHistoryParams` canonical form).
- Recommended representation: lowercase hex string of a 64-bit hash.
- Consequences:
  - Equivalent normalized searches resolve to the same `id`.
  - Insert path is naturally "upsert by id" (no separate UUID then lookup pass).
  - Unit tests can assert stable IDs for fixed inputs.

Collision policy:
- If hash collision is detected with different canonical payloads (extremely unlikely), append a deterministic suffix (`-1`, `-2`, ...) based on payload comparison order to preserve uniqueness.
- Collision handling is explicitly non-deduplicating: colliding canonical payloads are inserted as distinct records (no merge), even though their unsuffixed hash prefix matches.

### 4.3 Equality / Dedup Key

- Two entries are equivalent if their normalized `SearchHistoryParams` match.
- Normalization includes:
  - Stable ordering for multi-value fields.
  - Whitespace-trimmed text fields where appropriate.
  - Explicit defaults materialized so semantically identical searches dedupe.
  - `ai_search_description` included in canonicalization and equality.

Dedup rule for natural-language searches:
- Two entries with identical structured filters but different `ai_search_description` are **different history entries**.
- Rationale: prompt intent is user-visible context and must be preserved for rerun/disambiguation.

---

## 5. Persistence and Migration

### 5.1 New settings shape

- Replace split persistence (`saved_searches`, `recent_searches`) with one `search_history` array.
- Keep max-recent limit in settings defaults (`kMaxRecentSearches`) and apply to non-pinned only.

### 5.2 Compatibility policy (intentional break)

- No migration from old `saved_searches` / `recent_searches` keys.
- No backward compatibility logic in runtime code.
- On upgrade, history starts from the new `search_history` schema only.
- If legacy keys exist, they are ignored; previous saved/recent entries are not preserved.

---

## 6. Behavior Rules

### 6.1 On search execution

- Compute dedup key from normalized params.
- If existing entry found:
  - Update `last_used_at_unix_ms = now`.
  - Keep `is_pinned`, `custom_name`, and `created_at_unix_ms`.
- Else:
  - Insert new non-pinned entry with timestamps.
- Trim only non-pinned entries over cap.

### 6.2 Pin/unpin

- Pin:
  - Set `is_pinned = true`, `pinned_at_unix_ms = now`.
- Unpin:
  - Set `is_pinned = false`, `pinned_at_unix_ms = 0`.
  - Set `last_used_at_unix_ms = now` so the newly-unpinned item is treated as most-recently-used.
  - Keep entry in recent list and apply normal recent-cap trimming.
  - If recents exceed `kMaxRecentSearches`, trim from oldest `last_used_at_unix_ms` first; ties use `id` ascending.
  - Therefore, a newly-unpinned entry is preserved by default unless many entries share the same timestamp and deterministic tie-break selects it.

### 6.3 Cmd+P / Ctrl+P shortcut

- Resolve the last executed search via `application.GetLastHistoryId()`.
- If the returned id is empty (no search has been executed this session): no-op.
- If entry is not pinned: call `PinHistoryEntry(id, now_ms, settings)` + `SaveSettings`.
- If entry is already pinned: call `UnpinHistoryEntry(id, now_ms, settings)` + `SaveSettings`.
- `custom_name` is never modified by this shortcut.

### 6.4 Deletion

- Deleting a history entry removes exactly that entry by `id`.
- No hidden side-effects to current search state.

---

## 7. UI Integration Plan

### 7.1 Replace duplicated UI paths

- Remove separate rendering/control code paths for "saved searches" and "recent searches".
- Add one renderer backed by `search_history`.

### 7.2 Actions per row

- Primary: run search.
- Secondary actions:
  - Pin/Unpin
  - Rename (pinned)
  - Delete

### 7.3 Visual compactness

- Use one compact row component reused in both subsections.
- Keep iconography lightweight:
  - Pinned icon for `is_pinned`
  - Timestamp/subtitle for recency context

### 7.4 UI state ownership for unified list

- Selection/focus state for the unified Search History list must live in explicit UI/app state (for example, in `GuiState` or a dedicated history-panel state object), not in function-local `static` variables inside render functions.
- State must be reset/validated when underlying `search_history` data changes (delete, pin/unpin, sort/filter changes).
- Any temporary index-based state must be bounds-checked against current visible rows each frame.

---

## 8. Testing Requirements

### 8.1 Unit tests

New test file (`SearchHistoryTests.cpp` or appended to `TimeFilterUtilsTests.cpp`):

- `GenerateHistoryId`: same `SearchHistoryParams` input always produces the same id.
- `NormalizeHistoryParams`: whitespace trimming, default materialization, multi-value ordering all produce the same id for semantically equal inputs.
- Different `ai_search_description` with same structured filters produces distinct entries.
- `RecordHistoryEntry`: insert into empty list; upsert updates `last_used_at_unix_ms` and preserves `is_pinned`/`custom_name`; return value equals the entry id.
- `RecordHistoryEntry`: trim removes only non-pinned entries when cap is exceeded; pinned entries are never trimmed regardless of count.
- Pin/unpin transitions: `is_pinned`, `pinned_at_unix_ms`, and `last_used_at_unix_ms` invariants hold after each operation.
- Unpin at cap: newly-unpinned entry survives; oldest recent by `last_used_at_unix_ms` (then `id` ascending) is trimmed.
- `RenameHistoryEntry`: sets `custom_name`; clearing `custom_name` to empty string is allowed and stored.
- `DeleteHistoryEntry`: removes exactly the entry with the given id; other entries unchanged.
- `GetSortedHistoryView`: pinned entries precede recent entries; within pinned, `pinned_at_unix_ms` desc then `last_used_at_unix_ms` desc then `id` asc; within recent, `last_used_at_unix_ms` desc then `id` asc.
- `MergeHistoryFromDisk`: entries present on disk but absent in memory are appended; pinned state and timestamps from the in-memory version win on conflict.
- `SearchHistoryEntry` serialization round-trip: all fields (`id`, all `SearchHistoryParams` fields, `custom_name`, `is_pinned`, all three timestamps) survive a save + load cycle via `search_history.json`.
- `search_history.json` is written as a file alongside `settings.json`; the `searchHistory` key is absent from `settings.json` itself.
- Hash-collision suffix: two distinct canonical payloads that hash to the same value receive distinct ids (`-1` suffix applied to the second).

### 8.2 Regression / UI tests

- Unified panel renders with mixed entries.
- Unified panel renders in `EmptyState`; `FilterPanel` does not duplicate list rendering/actions.
- Row actions (run/pin/unpin/rename/delete) update list correctly.
- Pinned ordering follows `pinned_at_unix_ms` descending with deterministic tie-breakers.
- Unpin at cap keeps unpinned item and trims oldest recent deterministically.
- If keyboard interactions are implemented for unified Search History, they operate through one canonical action path and remain stable after list updates.
- Unified list selection/focus state remains valid after data mutations (insert/delete/pin/unpin/reorder).
- Cmd+P / Ctrl+P with no prior search is a no-op.
- Cmd+P / Ctrl+P after a search pins it silently; pin state is visible in the history panel immediately.
- Second Cmd+P / Ctrl+P unpins the same entry.
- `ShortcutAction::SaveSearch` (Ctrl+S / Cmd+S) is no longer present; verify the key binding is gone and `HelpWindow` does not mention it.

### 8.3 Existing tests to delete in Phase 4

These tests reference types and functions removed in Phase 4. They must be deleted (not commented out) as part of that phase, and their coverage must be subsumed by the new tests in §8.1.

**`tests/TimeFilterUtilsTests.cpp`**:
- Delete `TEST_SUITE("RecordRecentSearch")` in its entirety (8 test cases, lines 503–620).
- Delete the `MakeSearchParams` anonymous-namespace helper above it (lines 486–500) — it builds the runtime `SearchParams` solely for `RecordRecentSearch` tests.
- Remove the `#include "filters/TimeFilterUtils.h"` entry for `RecordRecentSearch` from the include comment if no longer referenced.

**`tests/SettingsTests.cpp`**:
- Delete `VerifySavedSearchesLegacyMigration` helper function (lines 110–139).
- Delete `TEST_SUITE("Saved searches")` in its entirety (3 test cases, lines 429–518).
- In `TEST_CASE("Default settings")`: replace `CHECK(settings.savedSearches.empty())` with `CHECK(settings.searchHistory.empty())`.
- In `TEST_SUITE("Round-trip tests")`, `TEST_CASE("Save and load round-trip")`: remove the `savedSearches` setup and assertion block (lines 536–565 around `SavedSearch search`).
- Delete `TEST_CASE("LoadSettings with empty saved searches array")` (lines 595–603).
- Delete `TEST_CASE("LoadSettings with partial saved search data")` (lines 606–621).
- Delete `TEST_CASE("SaveSettings writes saved_searches.json and omits savedSearches from settings.json")` (line 640); replace with a new test asserting `search_history.json` is written and `searchHistory` is absent from `settings.json`.
- Delete `TEST_CASE("SaveSettings merges recentSearches from disk...")` (line 666).
- Delete `TEST_CASE("SaveSettings merges savedSearches from disk by name...")` (line 705).
- Replace the 4 legacy migration test cases (lines 742–814) with a single test: start with a config containing only legacy `saved_searches` / `recent_searches` keys; verify `LoadSettings` succeeds, `searchHistory` is empty, and no crash occurs.

### 8.4 Manual tests

- Start with an old config containing only legacy `saved_searches` / `recent_searches`; verify graceful empty-start and no crash when opening Search History.
- Cmd+P / Ctrl+P immediately after launch (before any search): confirm no crash and no entry created.
- Large histories (performance and responsiveness).
- Edge cases: empty names, prompt-only differences, and hash-collision fallback path.

---

## 9. Acceptance Criteria

1. Only one user-facing feature surface exists for history management.
2. Pinned entries fully cover old saved-search use cases.
3. Recent behavior remains automatic and bounded by cap.
4. No migration or backward-compatibility code is present for legacy saved/recent schemas.
5. No duplicated code paths remain for saved/recent rendering or mutation.
6. Search History list rendering and entry actions exist only in `EmptyState`.
7. The legacy Saved Searches UI line is removed from `FilterPanel` to increase available UI space.
8. Cmd+P / Ctrl+P toggles pin state on the last executed search with no popup.
9. `ShortcutAction::SaveSearch` and its Ctrl+S / Cmd+S binding are absent from the codebase.

---

## 10. Impact Analysis (YAGNI, SOLID, KISS, DRY)

- **YAGNI**: avoid introducing tags, folders, sync, or complex sort builders in this change.
- **SOLID**:
  - Single Responsibility: one history service/model handles persistence and mutation.
  - Open/Closed: future entry metadata can extend model without splitting feature again.
- **KISS**: one list + pinned flag is simpler than parallel systems.
- **DRY**: one storage schema, one dedup routine, one UI row component, one set of actions.

---

## 11. Phased Implementation Plan

The implementation is split into four sequential phases. Each phase must leave the build green and tests passing before the next begins. Dead code is not removed until Phase 4 to allow incremental validation.

### Phase 1 — New data model and persistence layer

**Goal**: introduce `SearchHistoryEntry` / `SearchHistoryParams` and their serialization alongside existing types without touching any UI or behaviour.

**Deliverables**:
- Add `struct SearchHistoryParams` and `struct SearchHistoryEntry` to `Settings.h` (alongside existing `SavedSearch` for now). `SearchHistoryParams` is a new persisted type; do not modify the existing runtime `SearchParams` in `search/SearchTypes.h`.
- Add `std::vector<SearchHistoryEntry> searchHistory{}` to `AppSettings`.
- Add `kSearchHistoryFileName = "search_history.json"` constant.
- Implement load/save path for `search_history.json` in `Settings.cpp`, following the same primary-then-legacy resolution pattern used by `ResolveRecentSearchesFilePath` / `ResolveSavedSearchesFilePath`.
- Implement history mutation free functions in a new `src/search/SearchHistory.h` and `src/search/SearchHistory.cpp` (canonical home for all history-layer logic; place in `src/search/` alongside other search-layer modules; do not add to `TimeFilterUtils`):
  - `GenerateHistoryId(const SearchHistoryParams&) -> std::string` — deterministic lowercase hex hash; returns the id of the upserted entry so callers can store it.
  - `NormalizeHistoryParams(SearchHistoryParams) -> SearchHistoryParams` — canonical form for dedup.
  - `RecordHistoryEntry(SearchHistoryParams, std::int64_t now_ms, AppSettings&) -> std::string` — upsert + trim non-pinned; returns the id of the upserted entry.
  - `PinHistoryEntry(std::string_view id, std::int64_t now_ms, AppSettings&)`.
  - `UnpinHistoryEntry(std::string_view id, std::int64_t now_ms, AppSettings&)`.
  - `RenameHistoryEntry(std::string_view id, std::string_view name, AppSettings&)`.
  - `DeleteHistoryEntry(std::string_view id, AppSettings&)`.
  - `GetSortedHistoryView(const AppSettings&) -> std::vector<const SearchHistoryEntry*>` — pinned desc then recent desc with tie-breakers from §3.1.7.
- Add `MergeHistoryFromDisk(AppSettings&)` following the same merge-on-save pattern as `MergeRecentSearchesFromDisk`.
- Wire `search_history.json` into the `SaveSettings` / `LoadSettings` call sites.
- **Unit tests**: `GenerateHistoryId` stability, `NormalizeHistoryParams` dedup cases, `RecordHistoryEntry` insert/upsert/trim/return-value, pin/unpin invariants, `ai_search_description` distinctness, sort order with tie-breakers, collision suffix path, `DeleteHistoryEntry` by id.

- In `SettingsWindow.cpp`, add `searchHistory` to the preserve-restore block that guards against `settings = working_settings` overwriting live data when the Settings window has been open during a search session:
  ```cpp
  auto search_history_preserve = settings.searchHistory;   // add alongside existing preserves
  settings = working_settings;
  settings.searchHistory = std::move(search_history_preserve);   // add alongside existing restores
  ```
  This block already exists for `savedSearches` / `recentSearches`; `searchHistory` must be added to it from Phase 1. Phase 4 removes the old entries and leaves only this one.

**Files touched**: `Settings.h`, `Settings.cpp`, `SettingsWindow.cpp`, new `src/search/SearchHistory.h` and `src/search/SearchHistory.cpp`, test file.

**Nothing removed in this phase.**

---

### Phase 2 — Wire search execution to the new service

**Goal**: make real searches populate `searchHistory` so Phase 3 UI has live data to render.

**Deliverables**:
- Add `BuildHistoryParams(const SearchParams&, const GuiState&) -> SearchHistoryParams` in `src/search/SearchHistory.h/.cpp`: maps `filenameInput`→`filename`, `extensionInput`→`extensions`, `pathInput`→`path`, `foldersOnly`→`folders_only`, `caseSensitive`→`case_sensitive`, `static_cast<int>(state.timeFilter)`→`time_filter`, `static_cast<uint64_t>(state.sizeFilter)`→`size_filter`, `std::string(state.gemini_description_input_.data())`→`ai_search_description`. `SearchHistory.h` will need to include `GuiState.h`; verify no circular include (safe since `GuiState.h` does not include `SearchHistory.h`).
- In `Application.cpp` replace the `RecordRecentSearch(...)` call (line 812) with:
  ```cpp
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  last_history_id_ = RecordHistoryEntry(BuildHistoryParams(last_search_params_, state_), now_ms, *settings_);
  ```
  `now_ms` does not exist at that call site today — `RecordRecentSearch` computed timestamps internally. Add the above computation before `SaveSettings(*settings_)` on the next line. Also add `#include "search/SearchHistory.h"` to `Application.cpp` — it is not currently included (note: `ApplicationLogic.cpp` needs the same include, called out separately below).
- Add `[[nodiscard]] std::string_view GetLastHistoryId() const` to `Application` (returns `last_history_id_`; empty when no search has run this session).
- Add `ApplyHistoryEntryToGuiState(const SearchHistoryEntry&, GuiState&)` in `src/search/SearchHistory.h/.cpp` alongside (but not yet replacing) `ApplySavedSearchToGuiState`. Unlike `ApplySavedSearchToGuiState` which calls `TimeFilterFromString(string)` / `SizeFilterFromString(string)`, use `static_cast<TimeFilter>(entry.params.time_filter)` and `static_cast<SizeFilter>(entry.params.size_filter)` directly (numeric types in `SearchHistoryParams`); use `strcpy_safe(state.gemini_description_input_.data(), size, entry.params.ai_search_description.c_str())` for the AI description field.
- Add `selected_history_id_` (`std::string`) to `GuiState` to track the focused/selected entry in the unified list — use the entry's stable `id` string, not an index, so mutations (delete, pin/unpin, reorder) never silently invalidate the selection. Bounds-check each frame: if the id is no longer present in `GetSortedHistoryView`, clear the member to empty string. This is an explicit member, not a static local.
- `SaveSettings` must also persist `search_history.json`; verify this is covered by Phase 1 wiring.
- Add `ShortcutAction::PinSearch` to `ShortcutRegistry.h`: key `ImGuiKey_P`, primary modifier, no shift, `ShortcutScope::Global`, `ShortcutPlatform::All`, display key `"P"`, description `"Pin/unpin current search"`. Bump `kShortcuts` array size from 6 to 7 (update both the `std::array<ShortcutDef, N>` size and add the new entry).
- Add `case ShortcutAction::PinSearch:` to `DispatchGlobalShortcut` in `ApplicationLogic.cpp`: call `application.GetLastHistoryId()`; if empty, break (no-op); otherwise call `PinHistoryEntry` or `UnpinHistoryEntry` depending on the entry's current `is_pinned` state, then call `SaveSettings`. `DispatchGlobalShortcut` already receives `Application& application` — no signature change needed. Add `#include "search/SearchHistory.h"` to `ApplicationLogic.cpp` — it is not currently included.

**Files touched**: `Application.h`, `Application.cpp` (new include `search/SearchHistory.h`), `ApplicationLogic.cpp` (new include `search/SearchHistory.h`), `GuiState.h`, `ShortcutRegistry.h`, `src/search/SearchHistory.h/.cpp` (new helpers), `Settings.cpp` (save path already wired in Phase 1).

**Nothing removed in this phase.**

---

### Phase 3 — New unified Search History UI in EmptyState

**Goal**: build the new panel; the old UI is still present but the new panel is the primary interaction path.

**Deliverables**:
- Change `EmptyState::Render` signature in `EmptyState.h` from `const AppSettings&` to `AppSettings&` — the new panel calls mutation functions (`PinHistoryEntry`, `UnpinHistoryEntry`, `RenameHistoryEntry`, `DeleteHistoryEntry`) that all take `AppSettings&`. Update the definition in `EmptyState.cpp` and the call site in `UIRenderer.cpp` (whose `settings` member is already `AppSettings&`, so the call site change is trivial).
- In `EmptyState.cpp` add `RenderSearchHistoryPanel(GuiState&, AppSettings&, UIActions*, float, float, bool)`:
  - Reads `GetSortedHistoryView(settings)`.
  - Renders Pinned subsection followed by Recent subsection (collapse Pinned when empty).
  - Per-row actions: run (calls `ApplyHistoryEntryToGuiState` + `actions->TriggerManualSearch`), pin/unpin, rename (pinned only), delete. Note: run no longer goes through `UIActions::ApplySavedSearch`; it calls the free function `ApplyHistoryEntryToGuiState` directly then `actions->TriggerManualSearch`.
  - Pin/unpin actions: call `PinHistoryEntry` or `UnpinHistoryEntry`, then `SaveSettings(settings)` immediately — every mutation must persist or the change is lost on restart.
  - Rename action: set `state.pending_rename_id_` to the target entry id; the actual rename is completed inside `RenderHistoryRenamePopup` (called from `UIRenderer.cpp`). Introduce new `Popups::RenderHistoryRenamePopup(GuiState&, AppSettings&)` in `Popups.h/.cpp` — do **not** reuse `RenderSavedSearchPopups`. The modal calls `RenameHistoryEntry` then `SaveSettings(settings)`. Phase 4 then deletes `RenderSavedSearchPopups` cleanly.
  - Delete action: set `state.pending_delete_id_` to the target entry id. Introduce new `Popups::RenderHistoryDeletePopup(GuiState&, AppSettings&)` — called from `UIRenderer.cpp`. The modal calls `DeleteHistoryEntry` then `SaveSettings(settings)`. Phase 4 deletes the old `DeleteSavedSearchPopup` modal cleanly.
  - Optional `All | Pinned | Recent` filter chips backed by a `GuiState` chip selection member.
  - Selection/focus state uses `selected_history_id_` added in Phase 2; validated each frame against `GetSortedHistoryView`.
- Replace the `RenderRecentPanel(...)` call in `RenderSplitPanel` with `RenderSearchHistoryPanel(...)`. `RenderRecentPanel` and its helpers remain compiled (not yet deleted) but are dead after this replacement.
- In `UIRenderer.cpp`:
  - Remove the `FilterPanel::RenderSavedSearches` call (line 130). The `#include "ui/FilterPanel.h"` may be retained if other FilterPanel methods are still used; remove it only if no remaining usages exist.
  - Remove the `Popups::RenderSavedSearchPopups` call (line 138); replace with calls to the new `RenderHistoryRenamePopup` / `RenderHistoryDeletePopup`.
  - Update the footer-height calculation: replace the `kFooterHeightMultiplierWithSavedSearches` branch with an appropriate value. The saved-searches combo was rendered inside `MainContentRegion` (above the footer), so the footer itself only contains the status bar. If no other full-UI-only content appears below `MainContentRegion`, collapse both modes to `kFooterHeightMultiplierStatusOnly`. If additional full-UI footer elements exist (e.g. index-status detail row), introduce a new `kFooterHeightMultiplierFullUi` constant sized accordingly. Verify visually that the status bar is not clipped after the change.
- **Regression tests**: unified panel renders in `EmptyState`; `FilterPanel` no longer hosts list rendering; row actions update list; pinned ordering; unpin-at-cap trims oldest; selection state valid after mutations.

**Files touched**: `EmptyState.h` (signature change), `EmptyState.cpp`, `Popups.h/.cpp` (new `RenderHistoryRenamePopup`, `RenderHistoryDeletePopup`), `GuiState.h` (add `pending_rename_id_`, `pending_delete_id_`; chip state if needed), `UIRenderer.cpp`.

**Old helpers (`RenderRecentPanel`, `RenderSavedSearchPopups`, `FilterPanel::RenderSavedSearches`) remain compiled but are no longer called after this phase. Phase 4 deletes them.**

---

### Phase 4 — Dead code removal

**Goal**: delete every symbol, file, and data path made obsolete by Phases 1–3. No tombstones, no `// removed` comments — just deletion.

**Remove from `Settings.h`**:
- `struct SavedSearch`
- `AppSettings::savedSearches`
- `AppSettings::recentSearches`

**Remove from `Settings.cpp`** (all functions that exclusively serve the old model):
- `LoadSavedSearchFromJson`, `LoadSavedSearches`, `LoadRecentSearches`
- `ResolveRecentSearchesFilePath`, `ResolveSavedSearchesFilePath`
- `GetLegacyRecentSearchesPath`, `GetLegacySavedSearchesPath`
- `GetRecentSearchesPathAdjacentToSettingsFile`, `GetSavedSearchesPathAdjacentToSettingsFile`
- `ReadRecentSearchesJsonFromFile`, `ReadSavedSearchesJsonFromFile`
- `TryLoadRecentSearchesFromDedicatedFile`, `TryLoadSavedSearchesFromDedicatedFile`
- `AreSameSavedSearch`, `MergeSavedSearchesFromDisk`, `MergeRecentSearchesFromDisk`
- `FinishLoadRecentSearches`, `FinishLoadSavedSearches`
- `SerializeSavedSearch`, `WriteRecentSearchesToPath`, `WriteSavedSearchesToPath`
- Constants `kSavedSearchesFileName`, `kRecentSearchesFileName`

**Remove from `TimeFilterUtils.h/.cpp`**:
- `ApplySavedSearchToGuiState` (replaced by `ApplyHistoryEntryToGuiState`)
- `RecordRecentSearch` (replaced by `RecordHistoryEntry`)
- `AreSearchesIdentical`
- Forward declaration `struct SavedSearch`

**Remove from `FilterPanel.h` and `FilterPanel.cpp`**:
- Public declarations and definitions of `RenderSavedSearches`, `RenderSavedSearchCombo`
- Anonymous-namespace helpers: `HandleSavedSearchSelection`, `ShowSavedSearchNaturalLanguageTooltipIfHovered`
- Forward declaration `struct SavedSearch` (in header and cpp)
- If no other FilterPanel methods remain after these removals, remove the entire include of `FilterPanel.h` from `UIRenderer.cpp`

**Remove from `LayoutConstants.h`**:
- `kFooterHeightMultiplierWithSavedSearches` (the footer formula was already updated in Phase 3; this removes the now-dead constant)

**Remove from `UIRenderer.cpp`**:
- Any remaining dead include of `"ui/FilterPanel.h"` if not already cleaned up in Phase 3

**Remove from `Popups.h/.cpp`**:
- `RenderSavedSearchPopups`, `CreateSavedSearchFromState`, `SaveOrUpdateSearch`
- The `openSaveSearchPopup`-gated save-search popup modal and `DeleteSavedSearchPopup` modal (replaced by `RenderHistoryRenamePopup` / `RenderHistoryDeletePopup` added in Phase 3)

**Remove from `EmptyState.cpp`** (do this before removing `UIActions::ApplySavedSearch` below, since `RenderRecentPanel` calls it):
- `BuildRecentSearchTooltip`, `BuildRecentSearchLabel`, `BuildRecentSearchLabelAndWidth`, `BuildFiltersSummary`
- `RenderRecentPanel` (contains the last call to `actions->ApplySavedSearch`; must be deleted first)

**Remove from `GuiState.h`**:
- `openSaveSearchPopup`
- `deleteSavedSearchIndex`

**Remove from `UIActions.h`** (only after `RenderRecentPanel` is deleted — see EmptyState step above):
- `ApplySavedSearch` pure-virtual method
- Forward declaration `struct SavedSearch`

**Remove from `Application.h/.cpp`**:
- `ApplySavedSearch` override and implementation
- Forward declaration `struct SavedSearch`

**Remove from `SettingsWindow.cpp`**:
- `saved_searches_preserve` / `recent_searches_preserve` preserve-and-restore block

**Remove from `ShortcutRegistry.h`**:
- `ShortcutAction::SaveSearch` enum value (replaced by `ShortcutAction::PinSearch` added in Phase 2).
- Corresponding entry in `kShortcuts` (net change: rename key from `S` to `P`, update description).

**Remove from `ApplicationLogic.cpp`**:
- `case ShortcutAction::SaveSearch:` and its `state.openSaveSearchPopup = true` body (replaced by `PinSearch` case added in Phase 2).

**Remove from `HelpWindow.cpp`**:
- Any reference to "Save current search" / Ctrl+S / Cmd+S shortcut description.

**Verification checklist for Phase 4**:
- `grep -r SavedSearch src/ tests/` returns zero hits.
- `grep -r recentSearches src/ tests/` returns zero hits.
- `grep -r savedSearches src/ tests/` returns zero hits.
- `grep -r RecordRecentSearch src/ tests/` returns zero hits.
- `grep -r ApplySavedSearch src/ tests/` returns zero hits.
- `grep -r recent_searches src/ tests/` returns zero hits.
- `grep -r saved_searches src/ tests/` returns zero hits.
- `grep -r openSaveSearchPopup src/ tests/` returns zero hits.
- `grep -r deleteSavedSearchIndex src/ tests/` returns zero hits.
- `grep -r ShortcutAction::SaveSearch src/ tests/` returns zero hits.
- `grep -r kSavedSearchesFileName src/ tests/` returns zero hits.
- `grep -r kRecentSearchesFileName src/ tests/` returns zero hits.
- Build succeeds. All tests pass. Clang-tidy clean.

---

### Phase 5 — Polish and rollout

**Goal**: update user-visible text and remove any short-lived feature flag.

**Deliverables**:
- Update help/shortcuts text: replace all "Saved Searches" / "Recent Searches" references with "Search History".
- If a feature flag was used in Phase 3, remove the flag and its branch.
- Confirm all Acceptance Criteria in §9 are met.
- Run `scripts/run_clang_tidy.sh` and `scripts/build_tests_macos.sh` to completion.

---

## 12. Rollout Notes

