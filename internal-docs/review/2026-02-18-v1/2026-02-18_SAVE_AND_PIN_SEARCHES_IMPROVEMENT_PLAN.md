# 2026-02-18 – Save & Pin Searches: Improvement Plan

## Recommendation (from FEATURE_EXPLORATION_REVIEW_2026-02-18.md)

- **Candidate: "Save & Pin Searches"**
  - *User Story*: "As a Developer, I want to pin my 'last 24h modified .cpp files' search so I can resume work instantly."
  - *Value*: Retention and efficiency.
  - *Complexity*: Low (Settings persistence).

---

## Current Implementation vs Recommendation

### What Exists Today

| Capability | Implementation | Location |
|------------|----------------|----------|
| **Save search** | User gives a name → stored in `AppSettings::savedSearches`; full persistence (path, extensions, filename, time/size filter, etc.). | `FilterPanel::RenderSavedSearches`, `Popups::SaveSearchPopup`, `CreateSavedSearchFromState`, `SaveOrUpdateSearch` |
| **Apply saved search** | Select from combo → `ApplySavedSearch` + `TriggerManualSearch`. One click *after* opening dropdown. | `FilterPanel::RenderSavedSearchCombo`, `HandleSavedSearchSelection` |
| **Recent searches** | Last 5 searches auto-recorded; shown as **one-click buttons** in **empty state only**. Apply + trigger. Persisted. | `RecordRecentSearch` (TimeFilterUtils), `EmptyState::RenderRecentSearches` |
| **Delete saved search** | Delete button + modal for selected item. | `FilterPanel`, `Popups::RenderSavedSearchPopups` |

**Data model:** `SavedSearch` in `Settings.h` (name, path, extensions, filename, foldersOnly, caseSensitive, timeFilter, sizeFilter, aiSearchDescription). No `pinned` or ordering field.

**Persistence:** `savedSearches` and `recentSearches` in JSON via `Settings.cpp` (LoadSavedSearchFromJson, SaveSettings).

### Gaps vs “Save & Pin” User Story

1. **No “pin” concept** – Saved searches are a flat list; there is no way to mark favorites or prioritize some for quick access.
2. **“Resume instantly”** – To run a saved search the user must open the combo and select. There is no always-visible one-click row for favorite (pinned) searches.
3. **Recent vs pinned** – Recent searches are one-click but (a) only on empty state, (b) auto-rotating (last 5), (c) not user-curated. Pinned would be explicit, persistent favorites with one-click access from the main UI.

---

## Improvement Plan

### 1. Data Model: Add `pinned` to SavedSearch

- **File:** `src/core/Settings.h`
- **Change:** Add `bool pinned = false` to `SavedSearch` (camelCase for JSON: `pinned`).
- **Scope:** Only meaningful for `savedSearches`. `recentSearches` can ignore it (never set, or strip when displaying).

**Rationale:** Single list of saved searches with an optional pin flag keeps the model simple and avoids a separate “pinned” list.

### 2. Persistence: Load/Save `pinned`

- **File:** `src/core/Settings.cpp`
- **Load:** In `LoadSavedSearchFromJson`, if `item.contains("pinned") && item["pinned"].is_boolean()`, set `saved.pinned`. Otherwise leave default `false` (backward compatible).
- **Save:** In the `savedSearches` serialization loop, add `search_obj["pinned"] = s.pinned;`.
- **Scope:** Only for `savedSearches`; do not persist `pinned` for `recentSearches` (they are not user-saved).

### 3. UI: Pin/Unpin Toggle

- **Option A (recommended):** In the Saved Search **combo**, for each item show a small pin icon (e.g. `ICON_FA_THUMBTACK` or similar) that toggles `pinned` on click (without closing the combo or changing selection). Requires combo items to support a small button or secondary click; if ImGui combo does not allow that, use **Option B**.
- **Option B:** Add a “Pin” / “Unpin” **button** next to the combo (e.g. after Delete): when a saved search is selected, the button toggles that item’s `pinned` and calls `SaveSettings`.
- **Option C:** In the “Save Search” popup, add a checkbox “Pin this search” so new (or updated) saves can be pinned at save time; existing saved searches get pin/unpin via Option B.

**Deliverable:** User can pin/unpin any saved search; state persists across restarts.

### 4. UI: One-Click “Pinned” Row (Resume Instantly)

- **Placement:** In `FilterPanel::RenderSavedSearches` (or directly above it in `UIRenderer`), add a row **above** the “Saved Searches:” combo: e.g. “Pinned:” followed by one-click buttons for each **pinned** saved search (same interaction as recent search buttons: click → Apply + Trigger).
- **Behavior:** Only show this row when `savedSearches` has at least one item with `pinned == true`. Buttons can use `SavedSearch::name` (or a short label) and tooltip with full criteria. Disable when `is_index_building`, same as existing Apply.
- **Layout:** Reuse the pattern from `EmptyState::RenderRecentSearches` (wrapping buttons, truncation) so pinned searches don’t overwhelm the toolbar; consider a small max width per button and “Pinned:” on its own line if needed.

**Deliverable:** Pinned searches are always visible as one-click buttons when the filter panel is visible, satisfying “resume work instantly.”

### 5. Ordering: Show Pinned First in Combo

- **Where:** When rendering the saved search combo, either:
  - **Display order:** Build a temporary list “pinned first, then unpinned” and use that for the combo (indices map back to `settings_state.savedSearches` for selection/delete), or
  - **Persisted order:** When saving, sort `savedSearches` so pinned entries come first (then save). Combo order stays in sync with file.
- **Recommendation:** Display-time sort (no change to persisted order) to avoid reordering user’s list on first run after adding the feature; optionally later allow drag-to-reorder and persist order.

### 6. Tests and Backward Compatibility

- **SettingsTests:** Add a test that a saved search with `"pinned": true` in JSON loads with `pinned == true`; one without `pinned` loads with `pinned == false`. Add round-trip test for `pinned` in SaveSettings/LoadSettings.
- **Backward compatibility:** Old settings files without `pinned` → all saved searches load with `pinned == false`; no migration script required.

### 7. Documentation and Naming

- **AGENTS.md / user docs:** No strict change required; “Save & Pin” is a small extension of existing “Saved Searches.”
- **Naming:** Use `pinned` in JSON and in code (camelCase in struct for JSON API consistency).

---

## Implementation Order (Suggested)

1. **Data + persistence:** Add `pinned` to `SavedSearch`, load/save in Settings.cpp, add/update SettingsTests.
2. **Pin/unpin UI:** Add toggle (Option B is simplest: Pin/Unpin button for selected saved search).
3. **Pinned row:** Add “Pinned:” row with one-click buttons in FilterPanel (or above it).
4. **Combo order:** Show pinned first in the saved search combo (display-time sort).
5. **Optional:** Pin icon inside combo (Option A) or “Pin when saving” (Option C) if desired.

---

## YAGNI / Scope

- **In scope:** Pin flag, persistence, one-click pinned row, pin/unpin control.
- **Out of scope for this plan:** Drag-to-reorder, separate “Pinned” list in settings file, pinning from recent searches (user can Save then Pin).

---

## Summary

| Current | After improvements |
|--------|---------------------|
| Saved searches = named list, apply via dropdown | Same + **pinned** flag and **pinned-first** ordering in combo |
| No one-click “favorites” in main UI | **Pinned row** of one-click buttons (resume instantly) |
| No way to mark favorites | **Pin/Unpin** for selected saved search (and optionally “Pin” in save popup) |

This aligns the implementation with the recommendation: low complexity (one new flag + persistence + small UI additions), retention (pinned searches persist), and efficiency (one-click resume for pinned searches).
