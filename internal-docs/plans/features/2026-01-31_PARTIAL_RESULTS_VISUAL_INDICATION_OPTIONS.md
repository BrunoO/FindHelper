# Partial Results – Visual Indication Options

**Date:** 2026-01-31  
**Purpose:** Investigate options for displaying a visual indication that search results are partial (streaming) until they are final.  
**Scope:** Design options only; no code changes.  
**Reference:** `docs/plans/features/2026-01-31_STREAMING_SEARCH_RESULTS_IMPLEMENTATION_PLAN.md`, streaming UI state in `GuiState` (`showingPartialResults`, `resultsComplete`, `partialResults`).

---

## Goal

When streaming is enabled, results appear incrementally (`partialResults`). Users should be able to tell at a glance that the list is **not yet complete** (e.g. more rows may appear, count may change). Once the search finishes (`resultsComplete`), the indication should disappear or change to “final.”

**Current state:**

- **Status bar:** Already shows `"Status: Searching (N)..."` when `!state.resultsComplete` and streaming (using `state.partialResults.size()`). So the *status line* indicates activity.
- **Results table area:** There is **no** visual cue in or around the table itself that the results are partial. A user focused on the table might not notice the status bar.

This document explores options to add a **table-area** (or adjacent) visual indication that results are partial until they are final.

---

## State to Use

All options can key off existing `GuiState`:

- `state.showingPartialResults` – we are displaying `partialResults` (streaming in progress).
- `state.resultsComplete` – search has finished; results are final.
- `state.partialResults.size()` – current partial count (for “N so far” style text).
- `state.searchActive` – search in progress (can disambiguate “searching” vs “idle with partials” if needed).

So **no new state** is strictly required; only where and how we render the indication.

---

## Option 1: Table Header Badge / Label

**Idea:** In the same row as the table header (e.g. “Filename”, “Size”, …), add a short label or badge that appears only when `!state.resultsComplete && state.showingPartialResults`, e.g. “Partial” or “Updating…”.

**Implementation notes:**

- After `ImGui::TableHeadersRow()` (or in a custom header row), add a small text or badge next to the table title or in the first header cell.
- Style: muted color (e.g. gray) or a small ImGui child window with a distinct background.
- Optional: use an icon (e.g. FontAwesome spinner or “stream” icon) next to the text; project already uses `IconsFontAwesome.h` (e.g. `ICON_FA_TRASH`, `ICON_FA_CIRCLE`).

**Pros:** Directly attached to the table; hard to miss when looking at results.  
**Cons:** Uses horizontal space; may need to avoid overlapping column headers or resize logic.

---

## Option 2: Footer Line Below the Table

**Idea:** Reserve a single line of text **below** the results table (in the “footer” area that already exists for layout, e.g. above the status bar). When results are partial, show e.g. “Showing N results so far — search in progress…” or “Partial results (N) — updating…” When `resultsComplete`, hide the line or show “N results” without “so far”.

**Implementation notes:**

- The table is already sized with `ImVec2(0, -footer_height)`; the footer space could contain this line when `!state.resultsComplete && state.showingPartialResults`.
- Reuse existing footer height or add one line height when the message is visible.
- Text can include `state.partialResults.size()` for the “N so far” count.

**Pros:** Clear, readable, no overlap with table content; fits existing footer concept.  
**Cons:** Slightly increases vertical footprint when visible; user might still overlook if they don’t scroll down.

---

## Option 3: Progress Bar or Indeterminate Indicator

**Idea:** A thin progress bar (or indeterminate “loading” bar) above or below the table when results are partial. Alternatively, a small spinner or animated icon in the table header or footer.

**Implementation notes:**

- ImGui has no built-in indeterminate progress; could use a thin bar with a moving segment or a periodic width change to suggest “in progress”.
- Or use a small icon (e.g. `ICON_FA_CIRCLE_DOT` or similar) that could be updated each frame for a simple animation effect.
- Place above table (below toolbar) or in the table footer.

**Pros:** Universal “loading” metaphor; very visible.  
**Cons:** Can feel heavy for a list that is already updating; animation adds a small amount of per-frame work and complexity.

---

## Option 4: Row Count with “So Far” / “Partial” Suffix

**Idea:** Where the result count is shown (e.g. in the status bar center group, or in a new “results summary” line near the table), show the count with an explicit “so far” or “(partial)” when `!state.resultsComplete && state.showingPartialResults`, e.g. “1,234 results so far” or “1,234 (partial)”. When complete, show “1,234 results” only.

**Implementation notes:**

- Status bar center group already shows a count (`state.resultsComplete ? state.searchResults.size() : state.partialResults.size()`). That text could be extended with “ (partial)” or “ so far” when `!state.resultsComplete`.
- Alternatively, add a dedicated line near the table (e.g. above or below) that shows “N results” and “N results so far” when partial.

**Pros:** Reuses or extends existing count display; minimal new UI.  
**Cons:** If only in the status bar, table-focused users might still miss it; a line near the table is more visible but adds a dedicated element.

---

## Option 5: Icon or Badge in Table Corner

**Idea:** A small icon (e.g. spinner, “stream”, or “partial”) in one corner of the table (e.g. top-right), visible only when results are partial. Could be drawn with `ImGui::SetCursorScreenPos` or in a small overlay window so it doesn’t affect table layout.

**Implementation notes:**

- After the table is laid out, get the table’s screen rect and draw the icon in a corner with a semi-transparent or muted style.
- Use FontAwesome icon; project already has `IconsFontAwesome.h`. Optionally tint with a distinct color when partial.

**Pros:** Compact; doesn’t consume table content space.  
**Cons:** Small and in a corner; some users may not notice; overlay positioning must be robust to scroll and resize.

---

## Option 6: Banner / Strip Above the Table

**Idea:** A single-line “banner” or strip **above** the results table (e.g. below the search/filter controls, above the table) that appears only when results are partial: e.g. “Partial results — more may appear as search continues.” Optionally include current count.

**Implementation notes:**

- One line of text; background color or left border to make it stand out from the rest of the content.
- Height fixed (e.g. one line); when `resultsComplete`, the banner is not rendered (or height collapses), so layout stays consistent.

**Pros:** Very visible; clear message; no overlap with table.  
**Cons:** Uses vertical space; might feel prominent for a feature that is already indicated in the status bar.

---

## Option 7: Table Border or Background Tint

**Idea:** When results are partial, give the table a subtle visual difference: e.g. a colored left border, or a very light background tint (e.g. light blue or yellow) to signal “in progress”. When `resultsComplete`, revert to normal style.

**Implementation notes:**

- ImGui table styling (e.g. `ImGui::PushStyleColor` for table background or border) before `BeginTable` / after layout; pop after the table.
- Must be subtle so it doesn’t distract or reduce readability.

**Pros:** No extra text or widgets; entire table area is the cue.  
**Cons:** Easy to make too strong or too subtle; color may not work for all themes; some users may not associate tint with “partial”.

---

## Option 8: Tooltip on Table Hover When Partial

**Idea:** When results are partial, show a tooltip when the user hovers over the table (e.g. “Results are still updating”) instead of (or in addition to) a persistent on-screen label.

**Implementation notes:**

- After `BeginTable` (or on the first cell / a dedicated invisible “info” region), call `ImGui::SetItemTooltip(...)` or equivalent when `!state.resultsComplete && state.showingPartialResults`.
- No extra visible element until hover.

**Pros:** No layout impact; no extra clutter.  
**Cons:** Discoverability is low; users who don’t hover won’t see it; not suitable as the *only* indication if we want “at a glance” clarity.

---

## Summary Table

| Option                      | Visibility | Layout impact      | Complexity | Best for                    |
|----------------------------|-----------|--------------------|------------|-----------------------------|
| 1. Header badge/label       | High      | Small (header row) | Low        | Clear, compact cue          |
| 2. Footer line              | High      | One line           | Low        | Explicit “so far” message   |
| 3. Progress / spinner      | High      | Small              | Medium     | Strong “loading” metaphor  |
| 4. Count + “so far”        | Medium    | None (status bar)  | Low        | Minimal change              |
| 5. Icon in corner          | Medium    | None               | Low        | Minimal footprint           |
| 6. Banner above table      | Very high | One line           | Low        | Maximum visibility          |
| 7. Border / tint           | Medium    | None               | Low        | Ambient cue                 |
| 8. Tooltip on hover        | Low       | None               | Low        | Supplementary only          |

---

## Recommendation (for implementation decision)

- **Primary:** **Option 2 (footer line)** or **Option 1 (header badge)** — both are clear, low complexity, and sit in or next to the table. Footer line allows a full sentence (“Showing N results so far — search in progress”); header badge keeps the cue in the header row.
- **Secondary:** **Option 4** — extend the existing status bar count with “ (partial)” or “ so far” when `!state.resultsComplete && state.showingPartialResults` for consistency with the status bar and minimal code.
- **Optional enhancement:** **Option 8 (tooltip)** as a supplement so that users who hover the table get an explicit explanation.

Combining Option 2 or 1 with Option 4 gives both a table-area indication and a consistent count wording in the status bar, without adding heavy UI (progress bar or banner) unless product preference favors maximum prominence.

---

*Document generated 2026-01-31; design options only, no code changes.*
