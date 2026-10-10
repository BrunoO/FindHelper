# Specification: Browser-Style Back/Forward Navigation Over Search History

**Document:** `specs/2026-10-08_SEARCH_HISTORY_BACK_FORWARD_NAVIGATION_SPEC.md`
**Date:** 2026-10-08
**Status:** Implemented (2026-10-10, branch `feat/search-history-back-forward-nav`; mandatory scope P0–P2 + P4–P5 — optional Phase 3 buttons deferred per §5; see §9 execution notes)
**Target:** USN_WINDOWS / FindHelper (C++17, ImGui)

---

## 1. Overview & Motivation

Users comparing several search expressions today must re-type them or hunt through the
Search History panel for each one. This feature adds **browser-style Back/Forward
navigation**: keyboard shortcuts that step through the sequence of searches executed in
the current session, re-running each expression as they go, so the user can flip between
"pages" of results the same way they flip between web pages. Visible arrow buttons are
specified but **deferred to an optional phase** to avoid cluttering the UI for now (§5);
the mandatory interaction surface is two global shortcuts (§7).

**Core idea**: the navigation stack is **not a second copy of search payloads**. It is a
session-scoped ordered list of **history entry ids** (`std::vector<std::string>`) with a
cursor, referencing entries that already live in `settings.searchHistory`. The existing
unified Search History infra (`RecordHistoryEntry`, `ApplyHistoryEntryToGuiState`,
`SearchHistoryParams`) is reused as-is; **no new persisted data, no settings migration**.

### Goals

- Back/Forward **shortcuts** step through the session's executed searches and re-run each
  one; the shortcut implementation is robust and non-brittle (declarative registration,
  exact modifier matching, unit-tested without a live ImGui key map — §7).
- Integrate with the existing `searchHistory` (ids reference persisted entries; no parallel
  payload store, no schema change, no migration).
- Arrow buttons next to **Search now** are a fully specified but **optional phase** (§5),
  disabled-by-default in the rollout.
- Pure, unit-testable stack operations; zero impact on search worker threading.

### Non-goals

- Persisting the navigation stack across application restarts (session-only state; history
  entries themselves remain persisted as today).
- Navigating *within* a result set or results-table history (this is search-expression
  navigation only).
- Cross-device / synced navigation.
- Native Alt+Left / Alt+Right browser-identical chords: `ShortcutDef` has no Alt field and
  Alt chords collide with platform text editing (macOS word movement, Windows menu
  accelerators). §7 uses single-modifier `Cmd/Ctrl+J` / `Cmd/Ctrl+K` chords instead;
  extending `ShortcutDef` with Alt remains out of scope.
- Visible Back/Forward **buttons in the default scope** — specified in §5 as an optional
  phase; shipping shortcuts first keeps the UI unchanged.

---

## 2. Terminology

- **History entry**: one stored search configuration (`SearchHistoryEntry` in
  `src/core/Settings.h`), id = deterministic hash of canonical params (existing concept,
  see `specs/2026-03-31_Unified_Search_History_Spec.md`).
- **Navigation stack**: session-scoped `std::vector<std::string>` of history entry ids,
  in execution order, with a cursor.
- **Cursor**: index of the entry currently displayed/applied; Back decrements,
  Forward increments.
- **Forward tail**: entries after the cursor; truncated when a *new* distinct search is
  executed (browser semantics).
- **Navigation re-run**: applying a stack entry's params to `GuiState` and triggering a
  manual search.

---

## 3. Requirements & Constraints

### 3.1 Functional Requirements

1. **Push on manual search completion**
   - When a manual search completes and `RecordHistoryEntry(...)` returns an id
     (existing hook: `Application::UpdateSearchState`, `src/core/Application.cpp`
     ~line 881), the id is pushed onto the navigation stack:
     - If `id == stack[cursor]` → **no-op** (re-run / F5 refresh of the same
       expression must not duplicate; this is also what preserves the forward tail
       after Back+re-run).
     - Else → truncate the forward tail (`stack.resize(cursor + 1)`), push, advance
       cursor to the new back element.
   - Auto-refresh and crawl-complete searches are excluded exactly as they already are
     for history recording: the push shares the
     `ShouldRecordManualSearchCompletion(...)` gate. Only manual searches advance the stack.

2. **Back**
   - Enabled iff `cursor > 0`. Decrements cursor, then navigates to `stack[cursor]`.
   - Navigation = `ApplyAndRunHistoryEntry(*entry, state, actions)` (P0-T3 helper around
     `ApplyHistoryEntryToGuiState` + `TriggerManualSearch`) — the search **re-runs
     automatically** (browser-like: user sees the old results, not just restored inputs).
     Every navigation path (shortcut, history-row click, optional buttons) shares this
     one apply+run implementation.
   - Re-running produces the same canonical hash → the same id → the completion-path
     push is a no-op (`id == stack[cursor]`) → **forward tail survives**. No extra
     "navigating" flag is required; the dedup rule is the safety mechanism.

3. **Forward**
   - Enabled iff `cursor + 1 < stack.size()`. Increments cursor, then navigates as in §3.1.2.

4. **Deleted-entry resilience**
   - If `stack[cursor]` (or a candidate) no longer resolves in `settings.searchHistory`
     (user deleted it from the history panel), the navigation action **drops that id from
     the stack** (erases it, fixing up the cursor) and attempts the next candidate in the
     same direction once; if nothing resolves, the action is a silent no-op.
     Never crash, never leave the cursor out of bounds.

5. **Clearing filters / empty search**
   - Executing a search that yields a new history entry follows the normal push rule.
     Clearing inputs without executing a search does **not** push.

6. **Keyboard shortcuts (mandatory — the default interaction surface)**
   - `ShortcutAction::NavigateBack` / `NavigateForward`, global scope, bound per §7.
   - At a stack boundary the chord is a **silent no-op** (never moves the cursor, never
     triggers a search).
   - Works while a search input has focus (chord must be safe during text entry, §7).
   - Help window lists both entries alongside the existing shortcuts.

7. **UI affordance (optional buttons only)**
   - If the optional button phase (§5) is enabled: controls are always rendered but
     **disabled** (ImGui disabled state, grayed) when the corresponding direction is
     unavailable, so the affordance teaches its own state.

### 3.2 Non-Functional & Architectural Constraints

- **C++17 only**: `(std::min)`/`(std::max)`, explicit lambda captures in templates,
  braced returns, `std::scoped_lock` (not used here — no new locks).
- **UI-state write confinement**: the stack lives in `GuiState` and is written only on
  the UI thread (application_logic + render paths). No worker touches it.
- **Field ownership map**: new fields **must extend a substate**
  (`SearchNavigationState`), never be added flat to `GuiState`; add the ownership-map row
  in `src/gui/GuiState.h`.
- **Single writer per substate**: writers = `Application::UpdateSearchState` (push) and
  `application_logic::DispatchGlobalShortcut` (cursor moves via the mandatory shortcuts;
  optional button handlers if §5 ships). Documented in the struct comment like
  `HistoryInteractionState`.
- **No settings schema change**: nothing is serialized; no `SaveSettings` coupling.
- **No I/O, no allocation in hot paths**: push happens once per completed manual search
  (not per frame). Render path only reads sizes. Stack capacity grows amortized; cap at a
  sane bound (see kMaxNavigationStackDepth, §4.2).
- **ImGui rules**: buttons via `ImGui::Button`/`BeginDisabled` on the main thread;
  existing popup rules untouched.

---

## 4. Architecture & Data Flow

### 4.1 Data Model

New substate in `src/gui/WorkflowStates.h` (next to `HistoryInteractionState`):

```cpp
// Browser-style back/forward navigation over search history. Session-only:
// holds ids (not payloads) referencing entries in AppSettings::searchHistory,
// so pin/rename/timestamp mutations never invalidate the stack and no payload
// is duplicated. Single writers: Application::UpdateSearchState (NavPush on
// manual-search completion); ApplicationLogic::DispatchGlobalShortcut and
// SearchInputs button handlers (cursor moves via NavBack/NavForward).
struct SearchNavigationState {
  std::vector<std::string> stack;  // history entry ids, execution order
  size_t cursor = 0;               // index of current entry; == stack.size()-1 at tip
};
```

Stack operations — pure functions in `src/search/SearchHistory.h/.cpp`
(unit-testable, no GuiState needed except the navigate step):

```cpp
// Push semantics: no-op if id == stack[cursor]; else truncate tail, push, advance.
void NavPush(SearchNavigationState& nav, std::string_view id);

[[nodiscard]] bool NavCanGoBack(const SearchNavigationState& nav);
[[nodiscard]] bool NavCanGoForward(const SearchNavigationState& nav);

// Move cursor one step; returns false (cursor unchanged) when disabled direction.
bool NavBack(SearchNavigationState& nav);
bool NavForward(SearchNavigationState& nav);

// Remove a dead id, fixing up the cursor (see §3.1.4).
void NavDrop(SearchNavigationState& nav, size_t index);
```

Navigation step (lookup + existing apply), in `src/search/SearchHistory`:

```cpp
// Resolves id in settings.searchHistory; on miss calls NavDrop and returns false.
// On hit: ApplyAndRunHistoryEntry(*entry, state, actions) (P0-T3) — applies the
// params and re-runs the search in one place, so shortcut / row-click / optional
// button paths share identical semantics.
bool NavigateToHistoryId(std::string_view id, SearchNavigationState& nav,
                         GuiState& state, const AppSettings& settings,
                         ui::UIActions* actions,
                         size_t index /* position of id in nav.stack */);
```

Constants: `kMaxNavigationStackDepth` defined once in `src/search/SearchHistory.h`
(DRY — not redefined in GUI code). On overflow, drop the oldest non-current entry
(keep the cursor's entry stable); recommended depth 100.

### 4.2 Component Flow

```
[User runs manual search]
        │
        ▼
[SearchController::TriggerManualSearch]  (existing; snapshot criteria, StartSearch)
        │  … worker completes …
        ▼
[application_logic::RecordCompletedManualSearch]   ← P0-T2 extraction (testable)
  ShouldRecordManualSearchCompletion(...)?   ← existing gate (manual only)
        │ yes
        ├─ id = RecordHistoryEntry(...)      ← existing (upsert, returns id)
        └─ NavPush(state.navigation, id)     ← NEW (same gate, no second search of state)

[User presses Cmd/Ctrl+J (Back)]            ← mandatory entry point, §7
        │
        ▼
[DispatchGlobalShortcut case NavigateBack]   (optional buttons §5 call the same path)
        ├─ NavCanGoBack?  else silent no-op (§3.1.6)
        ├─ NavBack(nav)
        └─ NavigateToHistoryId(nav.stack[nav.cursor], ..., actions)
              ├─ resolve in settings.searchHistory
              │    miss → NavDrop + retry once (§3.1.4)
              └─ hit → ApplyAndRunHistoryEntry(entry, state, actions)  ← P0-T3
                        ├─ ApplyHistoryEntryToGuiState(entry, state)   ← existing
                        └─ actions->TriggerManualSearch(state)         ← existing → re-runs
```

Re-entrancy proof (why no `navigating` flag): navigation triggers a manual search;
on completion `RecordHistoryEntry` returns the **same** id (canonical hash is
deterministic), `NavPush` sees `id == stack[cursor]` → no-op → cursor and forward tail
unchanged.

### 4.3 C4 Sketch

- **L1**: FindHelper desktop app; user + Search History panel are the actors.
- **L2**: UI thread (ImGui render + `application_logic`) owns all navigation state;
  `SearchWorker` is unchanged — navigation reuses the ordinary manual-search trigger,
  so no new worker protocol, no new shared state across threads.
- **L3 components touched**: `GuiState` (new substate), `application_logic`
  (recording/push + mandatory shortcut cases), `SearchHistory` (pure ops + navigate),
  `ShortcutRegistry` (P0-T6 robustness + 2 entries), `HelpWindow` (2 lines),
  and — only if the optional phase ships — `SearchInputs`/`UIRenderer` (buttons),
  `IconsFontAwesome`.

---

## 5. UI Specification — OPTIONAL PHASE (deferred; buttons not in default scope)

> **Rollout note**: the default interaction is the mandatory shortcut pair (§7). This
> section is kept fully specified so the optional button phase can be implemented later
> without revisiting design decisions. Nothing in §5 ships by default.

### 5.1 Button placement (primary: next to Search now)

In `SearchInputs::Render` (`src/ui/SearchInputs.cpp` ~line 607), **line 1** currently
renders `Name | Ext | [Search now]`. Add two compact icon buttons **immediately left of
the Search now button**:

- Icons: add `ICON_FA_ARROW_LEFT` / `ICON_FA_ARROW_RIGHT` to
  `src/ui/IconsFontAwesome.h` (only `ICON_FA_ARROW_UP/DOWN` exist today), same
  NOSONAR/NOLINT comment style as neighbors.
- Width budget: subtract both button widths + spacing from `kTotalInputSpan` when the
  computed Name input width would stay ≥ `kMinNameInputWidth` (the clamp constant named
  in P0-T4 — single source, don't re-declare).
- **Room threshold**: if subtracting the arrows would push Name below
  `kMinNameInputWidth` → **do not render arrows in SearchInputs** (see 5.2).

### 5.2 Fallback placement (insufficient room)

When 5.1's threshold fails (narrow window / non-Full UI mode), render the same two
buttons in `UIRenderer::RenderManualSearchHeader` (`src/ui/UIRenderer.cpp` ~line 439),
right-aligned after the section title. Behavior, disabled states, and tooltips are
identical — one shared render helper (DRY: `RenderNavigationButtons(state, actions)`
declared once, called from one of the two sites per frame based on the room check).

### 5.3 Interaction details

- Tooltips: "Back to previous search (2/7)" / "Forward (7/7)" — position/total from
  `nav.stack` so the user sees where they are; plain "Back"/"Forward" when disabled.
- Disabled via `ImGui::BeginDisabled` when `NavCanGoBack/Forward` is false.
- Clicking Back/Forward while a search is running: allowed; `TriggerManualSearch`
  already cancels/replaces the in-flight search through the existing
  `ClearSearchResults` path (same as clicking a history row today).
- Simplified/Minimalistic UI modes: buttons render identically (they live in the
  search input row, which exists in all modes).

---

## 6. User Stories & Acceptance Criteria

| ID | Title | Given | When | Then |
|---|---|---|---|---|
| US-1 | Push on execute | Session ran searches A, B, C | C completes | `stack=[A,B,C]`, cursor=2; Back available, Forward not |
| US-2 | Back re-runs | `stack=[A,B,C]`, cursor=2 | User invokes Back (shortcut) | Cursor=1; inputs show B's params; **B's search executes** and results for B appear; Forward available |
| US-3 | Forward re-runs | After US-2 | User invokes Forward (shortcut) | Cursor=2; C re-runs; Back available, Forward not |
| US-4 | New search truncates tail | `stack=[A,B,C]`, cursor=1 (after Back) | User edits filters and runs search D | `stack=[A,B,D]` — forward tail (C) truncated; cursor=2 |
| US-5 | Re-run dedup | `stack=[A,B,C]`, cursor=1 | User re-runs B (F5 or Search now) | Push is a no-op: `stack=[A,B,C]`, cursor=1, **C still reachable via Forward** |
| US-6 | Auto-refresh excluded | Search B complete, results auto-refreshing | Crawl-complete refresh finishes | Stack unchanged (shares `ShouldRecordManualSearchCompletion` gate) |
| US-7 | Deleted entry skipped | `stack=[A,B,C]`, cursor=2; user deletes B from history panel | User presses Back | Cursor targets 1 (B) → B unresolvable → dropped (stack becomes `[A,C]`, cursor fixed up), navigation retries the next candidate per §3.1.4 and lands on C or A; no crash, cursor stays in bounds |
| US-8 | Cold start (shortcut) | Fresh session, no searches yet | User presses Back and Forward chords | Both are silent no-ops; stack stays empty; no search triggered |
| US-9 | Shortcut while typing | Cursor focused in the filename search input (text entry active) | User presses Back chord | Navigation runs (chord is text-input-safe, §7); no character is inserted into the input; normal text editing (caret movement, selection) unaffected |
| US-10 | Exact chord matching | Shortcut registered as `Cmd/Ctrl+J` | User presses `Cmd/Ctrl+Alt+J` or bare `J` (wrong modifier set) | Shortcut does **not** fire (§7 exact modifier matching) |
| US-11 | History panel parity | User clicks a history row (existing action) | Row click re-runs entry | Treated as a normal manual search → id pushed with same rules (no-op if already current) |
| US-12 | Help discoverability | Help window open | User looks for the new shortcuts | Both entries listed with platform-correct labels (`Cmd+J` / `Cmd+K` on macOS, `Ctrl+J` / `Ctrl+K` elsewhere) and descriptions |
| US-13 | *Optional UI* — layout room | Button phase enabled; window wide enough | UI renders | Arrows sit left of Search now, Name input stays ≥ clamp min |
| US-14 | *Optional UI* — layout fallback | Button phase enabled; window too narrow for §5.1 | UI renders | Arrows appear in Manual Search header instead; inputs keep today's widths |
| US-15 | *Optional UI* — disabled state | Button phase enabled; `stack` empty or at tip | UI renders / user clicks | Buttons render grayed; clicking is a no-op (§3.1.7) |

**Code invariants** (enforce with debug assertions at function entry/exit):

1. `cursor <= stack.size()` always (cursor may equal `size()` only transiently during
   `NavDrop` fix-up; assert `cursor < size()` at end of NavPush/NavBack/NavForward).
2. `stack` contains no duplicate ids (enforced by NavPush dedup rule).
3. Every id in `stack` either resolves in `settings.searchHistory` or is dropped at the
   point of navigation (§3.1.4) — never dereferenced blindly (`find` + null check).
4. `NavPush` is the only function that truncates the tail.
5. UI write confinement: all stack writes on UI thread; assert-free of locks (none exist).

---

## 7. Keyboard Shortcuts (MANDATORY — default interaction surface)

Two global shortcuts, registered in `kShortcuts` (`src/gui/ShortcutRegistry.h`), handled
in `DispatchGlobalShortcut` (`src/core/ApplicationLogic.cpp`), documented in the Help
window:

| Action | Chord (macOS) | Chord (Win/Linux) | `key_label` | Description |
|---|---|---|---|---|
| `ShortcutAction::NavigateBack` | `Cmd+J` | `Ctrl+J` | `J` (with primary) | "Back to previous search" |
| `ShortcutAction::NavigateForward` | `Cmd+K` | `Ctrl+K` | `K` (with primary) | "Forward to next search" |

Mapping mnemonic: **J comes before K** in the alphabet and sits left of K on the
keyboard — J steps back to the *earlier* search, K forward to the *later* one. (Swapping
the two is a one-field change in `kShortcuts` if experience says otherwise.)

### 7.1 Chord choice and why

- **Not Alt+Left/Right.** `ShortcutDef` has no Alt field; `IsTriggered` never inspects
  `io.KeyAlt`; and Alt chords collide with platform editing (macOS Option = word
  movement, Windows menu accelerators / `Ctrl+Alt+Arrow` = display rotation on Intel).
  Adding an Alt field to `ShortcutDef` remains out of scope (§1 Non-goals).
- **Not `Cmd/Ctrl+Left/Right`.** These are bound to caret movement in text inputs, and
  `HandleKeyboardShortcuts` runs modifier chords **even while `io.WantTextInput`** —
  the chord would steal text-cursor movement from the primary typing surface.
- **Not bracket keys (`Cmd/Ctrl+Shift+[` / `]`).** Rejected for ergonomics: three keys
  stretched across the keyboard (left modifier + right Shift + top row next to `P`), and
  on several European layouts `[`/`]` are AltGr combinations, making the chord a
  four-modifier reach.
- **Chosen: `Cmd/Ctrl+J` / `Cmd/Ctrl+K`.** Rationale:
  - **One modifier, home row.** Left `Cmd`/`Ctrl` + the key under the right index/middle
    fingers — the same effort as the existing `Ctrl+F` / `Ctrl+P` / `Ctrl+E` shortcuts;
    `J` and `K` are unshifted letter keys on **every** common layout (QWERTY, QWERTZ,
    AZERTY), which retires the layout risk that brackets created.
  - **Free everywhere.** Verified unbound: registry (`F`, `F5`, `Esc`, `Shift+H`, `P`,
    `E`), results-table scoped keys (`C D G M N P T U W X`, arrows, `Enter`, `Tab`,
    `Backspace`, `Delete`, `Slash`), `Ctrl+C` copy, popup `Esc`.
  - **Text-editing safe.** Not bound to any ImGui `InputText` operation; with a primary
    modifier held, `InputText` gates character insertion anyway, so no `j`/`k` is typed
    while the chord fires.
  - **No schema change.** Same composite shape as existing entries (`primary_modifier`
    only, `shift = false`, like `FocusSearch`/`PinSearch`/`ExportCsv`);
    `FormatLabel`/`key_label` render Help entries automatically and
    `GetPrimaryModifierName` gives the platform-correct label.
  - Chords are queried via `ImGui::IsKeyPressed` (layout-mapped); residual layout risk
    is now low and tracked in §10.

### 7.2 Behavior requirements

1. **Scope/platform**: `ShortcutScope::Global`, `ShortcutPlatform::All` — same as the
   existing six shortcuts.
2. **Boundary no-op**: at `cursor == 0` (Back) or tip (Forward) the chord is a silent
   no-op — no cursor move, no search, no log spam (§3.1.6).
3. **While typing**: the chords **must work while a search input has focus**
   (`io.WantTextInput`), without inserting characters or disturbing caret/selection —
   this is what the `text_input_safe` flag (P0-T6) declares; see 7.3.
4. **While index building / search in flight**: navigation triggers the same
   `TriggerManualSearch` path as everything else — the existing index-building guard
   (`Application::TriggerManualSearch` returns early) and cancel/replace semantics apply
   unchanged; the cursor has already moved, which is acceptable (next successful
   invocation re-aligns; stack invariants hold regardless).
5. **Help window**: two `RenderShortcutBullet(FindShortcut(ShortcutAction::Navigate…))`
   lines in the existing "Search & Navigation" section (`src/ui/HelpWindow.cpp` ~136) —
   label/description come from the registry, no duplicated strings.

### 7.3 Brittleness audit of the current registry (and the P0-T6 fixes)

Adding a shortcut today touches **four places**, with uneven guards:

| # | Place | Guard today if forgotten |
|---|---|---|
| 1 | `ShortcutAction` enum value | — (compiler type error downstream) |
| 2 | `kShortcuts` entry (+ array size 6→8) | `-Wswitch`/`-Wall` warns on the dispatch switch **on Clang/GCC**; **MSVC at `/W3` compiles silently** (C4062 is off) — silent dead shortcut on the primary target |
| 3 | `DispatchGlobalShortcut` case | same as #2 |
| 4 | Help window line | `FindShortcut` debug-only `assert` (no-op in release) |

Plus two behavioral brittlenesses in `IsTriggered` / `HandleKeyboardShortcuts`:

- **Text-input gating is a hardcoded exception** — `HandleKeyboardShortcuts` special-cases
  `def.action != ShortcutAction::RefreshSearch` inline; every new typing-safe shortcut
  would require editing that condition (easy to forget → shortcut silently dead while
  typing).
- **Modifier matching is partial** — `IsTriggered` checks primary + Shift but **ignores
  Alt**, and bare-key defs (F5, Esc) don't require primary modifiers to be *up*; extra
  held modifiers can fire the wrong action.

**P0-T6 therefore lands before the navigate cases**, making shortcuts declarative and
testable:

1. Add `bool text_input_safe = false` to `ShortcutDef` and replace the inline condition
   with the uniform rule `if (io.WantTextInput && !def.text_input_safe) continue;`.
   Behavior-preserving flag values for the existing six: `true` on `FocusSearch`,
   `RefreshSearch`, `ToggleHierarchy`, `PinSearch`, `ExportCsv` (the five that fire
   today while typing — chords plus the hardcoded F5 exception), `false` on
   `ClearFilters` (bare Esc stays skipped while typing, so a field can consume it).
   Navigate entries are declared `text_input_safe = true` — the requirement of US-9 is
   now an explicit, unit-testable property instead of a side effect of being a chord.
2. Extract the modifier check into a **pure function**
   `IsModifierStateMatch(const ShortcutDef&, const ShortcutModifierState&)` where
   `ShortcutModifierState` is a plain POD `{primary, shift, alt}` — `IsTriggered` becomes
   `IsModifierStateMatch(def, ReadModifiers(io)) && ImGui::IsKeyPressed(def.key, false)`.
   The pure part is unit-testable **without an ImGui context or key map**.
3. Make matching **exact**: chord requires primary down + `shift == def.shift` + Alt up;
   bare keys require primary up + Alt up. (Small, deliberate behavior tightening of the
   existing six shortcuts — covered by the new unit tests and existing GUI tests.)
4. Enable **MSVC C4062** (`/w14062`) for the dispatch translation unit (or project) so
   a missing `DispatchGlobalShortcut` case warns on Windows too — closes guard gap #2/#3
   on the primary target.
5. Unit test: every `ShortcutAction` value has a `kShortcuts` entry (`FindShortcut`
   result's `.action` round-trips) — closes gap #2 and the Help `assert`.

The documented "one entry in `kShortcuts` + one case in `DispatchGlobalShortcut`"
contract (`ShortcutRegistry.h:15`, `ApplicationLogic.h:111`) stays; P0-T6 turns its
implicit guards into compile-time (C4062, `-Wswitch`) and test-time checks.

---

## 8. Verification Plan

1. **Unit tests — `search_history_tests`** (`tests/SearchHistoryTests.cpp`, doctest):
   - `NavPush`: fresh push, tail truncation, dedup no-op at cursor, dedup *with* tail
     preserved, overflow at `kMaxNavigationStackDepth` keeps current entry.
   - `NavBack`/`NavForward`: bounds (no underflow at 0, no overflow at tip),
     cursor arithmetic, `NavCanGoBack/Forward` matrix.
   - `NavDrop`: cursor before/at/after dropped index; stack stays duplicate-free.
   - `NavigateToHistoryId`: resolve hit (params applied to a test `GuiState`),
     resolve miss → drop + false, retry-once behavior.
   - Id-stability: recording the same params twice returns the same id → push no-op
     (guards the re-entrancy proof).
2. **`application_logic_tests`** (the mandatory-shortcut test home):
   - Phase 0 (P0-T6): `IsModifierStateMatch` matrix — exact matching for all six
     existing defs (primary/Shift combinations, extra Alt must not fire, bare keys with
     primary held must not fire); `text_input_safe` gating (skips when
     `io.WantTextInput`, runs when flagged); `kShortcuts` completeness (every
     `ShortcutAction` round-trips through `FindShortcut`).
   - Phase 0 (P0-T2): `RecordCompletedManualSearch` gate+record wiring — manual
     completion records, auto-refresh/crawl-complete does not (regression guard for the
     extraction itself).
   - Phase 2: `NavPush` called exactly once per manual completion, dedup no-op on
     re-run (US-1, US-5, US-6).
   - Phase 4: dispatch cases for `NavigateBack`/`NavigateForward` — happy path applies
     params + triggers, boundary no-op at `cursor==0`/tip (US-8), missing-entry drop path.
3. **`gui_state_tests`**: ownership-map / substate presence — `SearchNavigationState`
   reachable via `GuiState`, no flat fields added.
4. **Phase 0 regression**: each refactoring commit runs the full suite unchanged —
   Phase 0 is behavior-preserving (the deliberate exception: exact modifier matching
   tightens existing chord behavior, guarded by the new P0-T6 tests); any other test
   delta is a defect in the refactoring, not an expected feature change.
5. **Full suite**: `./scripts/build_tests_macos.sh` (macOS entrypoint only; never
   cmake/clang++ directly). Then clang-tidy via the build directory target; no new
   Sonar issues (check with `./scripts/fetch_sonar_results.sh --open-only` if a scan is
   available).
6. **Manual smoke — mandatory scope**:
   - Run 3 distinct searches; press Back chord twice, Forward chord once; confirm inputs
     *and* results change each step (US-2/US-3).
   - Press both chords at a fresh start → silent no-op (US-8).
   - Focus the filename input, type, press Back chord mid-typing → navigation runs, no
     stray `j` in the input, caret behavior intact (US-9).
   - Press chord variants (bare `J` with no modifier, `Cmd/Ctrl+Alt+J`) → no
     navigation (US-10).
   - Back to an earlier search, edit one filter, run → Forward unavailable (tail
     truncated, US-4).
   - Delete a middle history entry, navigate across it → no crash, stack skips it (US-7).
   - Run a search from the history panel row click → push rules hold (US-11).
   - Auto-refresh tick after a search → stack unchanged (US-6).
   - Help window shows both entries with platform-correct labels (US-12).
   - Windows build: temporarily remove a dispatch case in a scratch branch → C4062
     warns (verifies P0-T6 guard #4); revert.
7. **Manual smoke — optional button phase** (only if §5 ships): US-13/US-14/US-15
   layout checks (narrow-window fallback, disabled states).
8. **ImGui test engine** (optional, `--run-imgui-tests`): follow the existing
   `RegisterShortcutTests` pattern (`src/ui/ImGuiTestEngineTests.cpp:841`) to drive the
   navigate chord end-to-end via the regression hook; not a baseline requirement —
   `application_logic_tests` covers the logic headlessly.

---

## 9. Task Breakdown

Task IDs are stable (`P<phase>-T<n>`); the Deps column references them.

### Phase 0 — Preliminary refactorings (each its own commit, suite green before moving on)

Each task is a standalone, test-preserving commit: **no feature behavior** lands here and
existing suites must stay green. They remove duplication the feature would otherwise
multiply, and move the feature's riskiest wiring (recording gate, shortcut matching) into
a unit-testable layer. Sole declared exception: P0-T6(c) deliberately tightens chord
matching (§7.3) — guarded by the new P0-T6 tests landing in the same commit.

| ID | Task | Deps | Est. | Notes / Acceptance |
|---|---|---|---|---|
| P0-T1 | Extract `FindHistoryEntryById` (const + non-const) into `SearchHistory.h/.cpp`; migrate the 8 plain find-by-id sites: `SearchHistory.cpp` Pin/Unpin/Rename/Delete, `Popups.cpp` ×2, `ApplicationLogic.cpp` PinSearch, `WelcomePanel.cpp` selected-row lookup | — | 1.5 h | Behavior-preserving; `search_history_tests` + existing popup/pin tests green. **Do not** migrate `RecordHistoryEntry`'s loop — its hash-collision suffix scan is not a plain find. Gives `NavigateToHistoryId` its lookup primitive |
| P0-T2 | Extract the history-recording block from `Application::UpdateSearchState` (src/core/Application.cpp ~866–893: gate + `BuildHistoryParams` + `RecordHistoryEntry` + persist) into `application_logic::RecordCompletedManualSearch(...)` (precedent: `DispatchGlobalShortcut`/`HandleKeyboardShortcuts` extracted the same way) | — | 2 h | Application.cpp calls one line; function is directly covered by `application_logic_tests`. **This is the target for P2-T1's `NavPush` wiring** — makes the gate+record+push interaction unit-testable (the feature's re-entrancy-critical path) |
| P0-T3 | Extract `ApplyAndRunHistoryEntry(entry, state, UIActions*)` — the apply+trigger pair currently inlined in `RenderHistoryRow` (`src/ui/WelcomePanel.cpp` ~343) | — | 1 h | One shared pattern for WelcomePanel + the new nav sites; `Application` is-a `ui::UIActions`, so all call sites pass what they already hold. Existing history-row click behavior unchanged |
| P0-T4 | Name the magic `200.0F` Name-input clamp in `SearchInputs::Render` → `kMinNameInputWidth` (single source shared by the clamp and the §5.1 room threshold) | — | 0.5 h | DRY precondition for optional P3-T2; trivial cleanup that stands alone even if buttons never ship |
| P0-T5 (conditional) | Split line 1 of `SearchInputs::Render` into a helper **only if** the P3-T2 edit trips `readability-function-cognitive-complexity` (suppressed in `tests/` only, not `src/`) | — | 0–1 h | Only relevant if the optional button phase ships; do the surgical edit first, extract only on a lint failure. Not a baseline task |
| P0-T6 | **Shortcut robustness prep (prerequisite for P4-T1):** (a) add `text_input_safe` to `ShortcutDef` and replace the hardcoded `RefreshSearch` exception in `HandleKeyboardShortcuts` with the uniform rule `WantTextInput && !text_input_safe → skip` (flags: `true` on the five firing-today entries, `false` on `ClearFilters` → behavior-preserving); (b) extract pure `IsModifierStateMatch(def, ShortcutModifierState)` POD-based matcher, `IsTriggered` = match + `IsKeyPressed`; (c) exact modifier matching (chord: primary + `shift==def.shift` + Alt up; bare: primary up + Alt up); (d) enable MSVC `/w14062` for `ApplicationLogic.cpp` so a missing dispatch case warns on Windows; (e) registry completeness test (every `ShortcutAction` round-trips `FindShortcut`) | — | 2 h | (a)+(b) behavior-preserving; (c) is a deliberate tightening of the existing six shortcuts → covered by the new matcher matrix tests + existing GUI shortcut tests; (d) verified per §8.6; (e) lands in `application_logic_tests`. Closes every guard gap in §7.3 before the new shortcuts depend on them |

**Explicitly out of Phase 0 (YAGNI):** extending `ShortcutDef` with an Alt field (the
§7 chord approach makes it unnecessary), any settings/serialization prep (stack is
session-only ids), merging `SearchNavigationState` with `HistoryInteractionState`
(different concerns), refactoring `GetSortedHistoryView` / history-panel rendering (the
feature never consumes the sorted view — ids resolve by lookup), and creating a new test
target (existing `application_logic_tests` / `search_history_tests` / `gui_state_tests`
absorb everything).

### Phases 1–5 — Feature implementation

**Mandatory: Phases 1, 2, 4, 5. Optional (deferred, not in default scope): Phase 3 buttons.**

| ID | Task | Deps | Est. | Notes / Acceptance |
|---|---|---|---|---|
| P1-T1 | `SearchNavigationState` in `WorkflowStates.h` + `GuiState` member + ownership-map row | — | 0.5 h | Builds; `gui_state_tests` pass |
| P1-T2 | Pure stack ops (`NavPush/Back/Forward/Drop/Can*`) in `SearchHistory.h/.cpp` + `kMaxNavigationStackDepth` | P1-T1 | 1.5 h | Unit tests for all ops land in same commit; assertions per §6 invariants 1-4 |
| P1-T3 | `NavigateToHistoryId` (resolve via `FindHistoryEntryById`, miss→drop+retry, apply via `ApplyAndRunHistoryEntry`) | P1-T2, P0-T1, P0-T3 | 1 h | Unit tests incl. miss path |
| P2-T1 | Wire `NavPush` into `application_logic::RecordCompletedManualSearch` (P0-T2), behind the existing `ShouldRecordManualSearchCompletion` gate | P1-T3, P0-T2 | 0.5 h | `application_logic_tests` cover gate+record+push incl. dedup (US-1, US-5, US-6) |
| P2-T2 | Delete-a-middle-entry resilience drill (manual) + any fix — **mandatory**, US-7 is core behavior exercised by the shortcut path | P1-T3 | 0.5 h | Drop+retry behavior verified per §8.6 |
| P3-T1 *(optional)* | Icons (`ICON_FA_ARROW_LEFT/RIGHT`) + shared `RenderNavigationButtons` helper | — | 0.5 h | Disabled states + position tooltips (US-15) |
| P3-T2 *(optional)* | Primary placement in `SearchInputs::Render` with room threshold (`kMinNameInputWidth`) | P3-T1, P0-T4 | 1 h | US-13 manual smoke; Name ≥ clamp min |
| P3-T3 *(optional)* | Fallback placement in `RenderManualSearchHeader` | P3-T1, P3-T2 | 0.5 h | US-14 manual smoke (resize window) |
| P4-T1 | **Mandatory shortcuts:** `ShortcutAction::NavigateBack/Forward` enum values + `kShortcuts` entries (size 6→8, `text_input_safe=true`, keys `ImGuiKey_J`/`ImGuiKey_K`, `shift=false`, key labels `J`/`K`) + `DispatchGlobalShortcut` cases (boundary no-op → `NavBack/NavForward` → `NavigateToHistoryId(..., &application)`) + 2 HelpWindow lines in "Search & Navigation" | P1-T3, P0-T6, P0-T3 | 2 h | `application_logic_tests` dispatch cases (§8.2 Phase 4); manual smoke §8.6 incl. typing-safety and modifier variants; no `ShortcutDef` schema change beyond P0-T6's flag |
| P5-T1 | Docs: Help window feature bullet ("What's new", git commit date), spec status → Implemented | all | 0.5 h | `src/ui/SearchHelpWindow.cpp` bullet; keep last calendar month only |
| P5-T2 | Full verification: `build_tests_macos.sh`, clang-tidy, Sonar open-issues check | all | 1 h | No new violations; all suites green |

Totals: **mandatory scope** = Phase 0 ≈ 7–8 h + Phases 1, 2, 4, 5 ≈ 7.5 h → **≈ 14–16 h**;
optional Phase 3 (buttons) adds ≈ 2 h → **≈ 16–18 h all-in**.

### 9.1 Execution notes (2026-10-10, implementation closeout)

Two placement adaptations were applied during implementation (approved in the task
prompt `internal-docs/prompts/2026-10-09_SEARCH_HISTORY_BACK_FORWARD_NAVIGATION_IMPLEMENTATION_TASK.md`,
recorded here so this spec stays the source of truth):

1. **Composition extracted to the `search` module for test linkability.** §4.2 places
   `RecordCompletedManualSearch` in `application_logic`; `ApplicationLogic.cpp` lives
   only in `COMMON_APP_SOURCES` and transitively pulls in `Application.cpp` (GLFW /
   window), so it **cannot be linked into any test target**. The recording + navigation
   composition (`RecordCompletedManualSearch`, `ApplyAndRunHistoryEntry`,
   `NavigateToHistoryId`, `NavigateHistory`, pure `Nav*` ops, `kMaxNavigationStackDepth`)
   therefore lives in `src/search/SearchHistory.h/.cpp` — already in `test_core_obj`,
   linked by `search_history_tests`/`gui_state_tests`. Semantics are exactly §3/§4.1;
   only the home module differs. `DispatchGlobalShortcut` keeps thin composition-only
   cases delegating to `NavigateHistory` (global namespace, consistent with the rest of
   the `SearchHistory` API — the §4.2 sketch's `search::` prefix is a path label, not a
   namespace this repo's history API uses).
2. **New `test_shortcut_obj` object library.** The P0-T6(e) registry tests need
   `ShortcutRegistry.cpp` (non-inline `FindShortcut`/`FormatLabel`); a dedicated
   `add_library(test_shortcut_obj OBJECT src/gui/ShortcutRegistry.cpp)` (mirroring
   `test_core_obj`'s include/PCH wiring) is linked into `application_logic_tests`,
   which previously tested header-inline predicates only. No new test *executable*
   targets were created (`scripts/test_targets.txt` unchanged).

Deviations worth noting: P0-T1 migrated **nine** find-by-id sites, not eight — the
Settings.cpp disk-merge presence check matches the same plain find-by-id predicate and
was included for DRY. Phase 3 (§5 buttons), §5 icons, `SearchInputs` render splits and
ImGui test-engine work were NOT touched (out of default scope, per §1/§5 rollout note).

---

## 10. Risks & Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Re-entrancy: navigation triggers search → completion pushes again | Stack corruption / lost forward tail | Dedup rule `id == stack[cursor]` → no-op (deterministic canonical hash makes this exact); unit-tested (id-stability test, US-5) |
| History list order reshuffles (`last_used_at` upsert on every run) | A cursor-into-sorted-list design would point at wrong expressions | Design uses **ids**, resolved by lookup at navigation time — order-independent (this is why §1 says ids, not indices) |
| Layout crowding on narrow windows *(only if optional buttons ship)* | Inputs squeezed below usable width | Room threshold + header fallback (§5.1/5.2, US-13/US-14) — deferred with Phase 3 |
| Deleted entries dangle in stack | Crash / stuck cursor | Resolve-with-fallback: drop + retry once, silent no-op if nothing resolves (§3.1.4, US-7) |
| Auto-refresh floods the stack | Useless navigation history | Shares the existing manual-only recording gate; test US-6 |
| Missing `DispatchGlobalShortcut` case for a new enum value | Shortcut silently dead — **no warning on MSVC `/W3`** (primary target) | P0-T6(d): enable `/w14062` for the dispatch TU; Clang/GCC covered by `-Wswitch`/`-Wall`; P0-T6(e) registry completeness test |
| Navigate chord interferes with typing (stray `j`/`k`, stolen caret movement) | Broken text entry in the primary inputs | Chord keeps primary modifier held → `InputText` gates char insertion; `text_input_safe` flag (P0-T6a) makes typing-safe explicit; US-9 manual smoke + matcher unit tests |
| Chord fires with wrong modifiers (extra Alt held, wrong Shift state) | Unexpected navigation | P0-T6(c) exact modifier matching, unit-tested matrix (US-10) |
| Non-QWERTY layout: chord keys missing or repositioned | Chord awkward or undiscoverable | **Low risk** — `J`/`K` are unshifted letter keys on QWERTY/QWERTZ/AZERTY alike (that is why brackets were rejected); labels/descriptions in Help (US-12) aid discovery; residual risk: unusual layouts requiring dead-key sequences (no cross-layout remap infrastructure exists today) |
| Help/registry drift (entry added, Help line forgotten) | Feature undiscoverable | P0-T6(e) forces `FindShortcut` round-trip; Help line task explicit in P4-T1 |
| Ownership-map violation (flat GuiState field) | Breaks god-object decomposition contract | Substate + map row in Phase 1, checked in `gui_state_tests` |
| Scope creep toward "navigate results" | Divergence from spec | Non-goals §1; reviewer checklist references this document |
| Phase 0 refactoring regressions (extraction/migration) | Behavior change hidden in "prep" commits | Each refactoring is its own behavior-preserving commit; full suite must stay green before the next (§8.4); migrated sites are covered by existing tests |

---

## 11. Impact Analysis (YAGNI / SOLID / DRY / KISS)

- **YAGNI**: no persistence, no payload duplication, no UI buttons in the default scope,
  no Alt-chord `ShortcutDef` extension — only ids + cursor + two shortcuts ship; buttons
  are a fully specified optional phase (§5) and a native-Alt approach is explicitly
  rejected (§7.1) rather than left open.
- **SOLID**: stack ops are pure functions (single responsibility, no state access);
  navigation lookup isolated in one function; shortcut modifier matching extracted as a
  pure, context-free function (open/closed: `HandleKeyboardShortcuts` decides
  text-input behavior from a declarative flag instead of hard-coded exceptions); the
  optional render helper is shared across two placements (placement decided by a width
  check, not by editing both blocks).
- **DRY**: reuses `RecordHistoryEntry` (id generation), `ApplyHistoryEntryToGuiState`
  (apply), `ShouldRecordManualSearchCompletion` (manual-only gate),
  `TriggerManualSearch` (re-run), the existing 200 px width clamp (single source, P0-T4),
  the existing `kShortcuts`/`DispatchGlobalShortcut`/Help registry as the *only*
  shortcut mechanism (no parallel key-handling path), and the history panel's apply+run
  interaction pattern (US-11 parity). Phase 0 removes the duplication the feature would
  otherwise multiply: 8 find-by-id sites collapse into `FindHistoryEntryById` (P0-T1),
  the apply+run pair into `ApplyAndRunHistoryEntry` (P0-T3), the untestable
  `Application.cpp` recording block moves into `application_logic` where the
  re-entrancy-critical wiring gets unit tests (P0-T2), and the shortcut registry's
  implicit guards become compile-time/test-time checks (P0-T6).
- **KISS**: the dedup rule replaces an entire "navigating flag" state machine; ids
  replace a serialized stack; the mandatory shortcuts are one registry entry + one
  dispatch case each (no new interaction subsystem); the spec adds exactly one substate
  and zero new files in `src/`.
