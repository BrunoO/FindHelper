# ImGui Test Engine – New Tests for Code Coverage

**Date:** 2026-02-22  
**Purpose:** Identify new ImGui Test Engine tests that would increase code coverage, based on `coverage/html/index.html`. Focus on UI-driven flows that the test engine can automate in-process (SetRef, ItemClick, Yield, hook-based assertions).

---

## 1. Coverage snapshot (summary)

From the latest report:

- **Totals:** ~52% function, ~40% line, ~39% region, ~31% branch.
- **Well-covered by existing tests:** SearchWorker, DirectoryResolver, PathBuilder, doctest units (string search, path_utils, etc.).
- **Gaps relevant to the test engine:** Application.cpp (58% line), UI components (Help, Settings, Metrics, Popups, FilterPanel, EmptyState, StatusBar), and ApplicationLogic (39% line).

The test engine currently runs: smoke (main window ref), regression (6 cases), load_balancing (18), streaming (12). All of these use the **regression hook** (set params, trigger search, assert count). They do not yet **open floating windows or click toolbar buttons**.

---

## 2. Recommended new tests (by priority)

### 2.1 High impact, high feasibility

| Test idea | What it does | Files / coverage improved |
|-----------|----------------|---------------------------|
| **Help window open/close** | SetRef("Find Helper"); ItemClick on the Help toolbar button (label like " Help" or " Hide Help"); yield; SetRef("Find Helper – Help"); assert window visible (e.g. ItemInfo for "What's new" or Close button); optionally ItemClick Close or toggle again. | HelpWindow.cpp (0% → non-zero), FilterPanel.cpp (RenderHelpButton), Application.cpp (floating window path) |
| **Settings window open** | ItemClick Settings button; yield; SetRef("Settings"); assert window open (e.g. presence of a known child or title). | SettingsWindow.cpp (0% → non-zero), FilterPanel.cpp (RenderSettingsButton), Application (show_settings_) |
| **Search Help (Search Syntax Guide) open** | Open the Search Syntax Guide (e.g. from menu or button if ref is known); SetRef("Search Syntax Guide"); assert content. | SearchHelpWindow.cpp (0% → non-zero), Popups.cpp (if opened via popup path) |

**Implementation notes:**

- Toolbar buttons in FilterPanel use `ImGui::SmallButton(label)` with labels like `ICON_FA_CIRCLE_QUESTION " Help"`. The visible ref may include the icon (e.g. " Help"). With SetRef("Find Helper"), use ItemClick with the ref that matches the button (may need to discover via test engine UI or wildcard).
- Window titles for SetRef: **Help** = "Find Helper – Help", **Settings** = "Settings", **Metrics** = "Find Helper Metrics", **Search Syntax** = "Search Syntax Guide".

### 2.2 Medium impact, feasible

| Test idea | What it does | Files / coverage improved |
|-----------|----------------|---------------------------|
| **Metrics window open** | Click Metrics button (if available); SetRef("Find Helper Metrics"); assert window. **Requires invoking the app with `--show-metrics`** — the test cannot force it at runtime (`metrics_available_` is set from CLI at startup and has no setter). | MetricsWindow.cpp (0% → non-zero), FilterPanel (RenderMetricsButton) |
| **Time/size quick filter + search** | After index ready, set a quick filter (e.g. "Today" or "Empty") via hook or ItemClick, trigger search, wait complete, assert count or that FilterPanel path ran. | FilterPanel.cpp (quick filter buttons), ApplicationLogic (filter application), TimeFilterUtils / SizeFilterUtils |
| **Empty state visible before index** | With app started and no index (or before index load), assert empty state or status text (e.g. "Indexing..." or "Idle"). May require a dedicated run with no `--index-from-file` or delayed index. | EmptyState.cpp, StatusBar.cpp, GuiState.cpp |

### 2.3 Lower priority / harder

| Test idea | What it does | Notes |
|-----------|----------------|--------|
| **Export CSV** | Trigger export from UI, assert file or notification. | ExportSearchResultsService.cpp (0%); needs ref for Export button and possibly file-system mocking or temp dir. |
| **Saved search apply** | Select a saved search from combo, trigger search, assert count. | Popups.cpp (saved search logic), ApplicationLogic (ApplySavedSearch) |
| **Stopping state** | Cancel a running search, assert stopping state. | StoppingState.cpp (0%); needs to start search then cancel within test. |

---

## 3. Hook extensions (if needed)

Current hook: SetSearchParams, SetLoadBalancingStrategy, GetLoadBalancingStrategy, SetStreamPartialResults, TriggerManualSearch, IsIndexReady, GetIndexSize, IsSearchComplete, GetSearchResultCount.

- For **filter-by-time/size** tests without relying on ItemClick: add **SetTimeFilter** / **SetSizeFilter** (or a single SetFilters) and optionally **GetDisplayedResultCount** (if different from GetSearchResultCount when filters apply). Then run a regression-style test with filters set and assert displayed count vs. expected.
- For **empty state**: no new hook needed if we only assert visibility via ItemClick/SetRef; if we need to assert “empty state active” from code, a small **IsEmptyStateVisible** or **GetStatusText** could be added.

---

## 4. Test layout and naming

- **Category:** e.g. `ui_windows` or `windows` for Help/Settings/Metrics/SearchHelp.
- **Names:** `help_window_open`, `settings_window_open`, `metrics_window_open`, `search_help_window_open`; optional `filter_today_then_search`, `empty_state_before_index`.
- Reuse existing pattern: SetRef("Find Helper"), yield if needed, ItemClick(button_ref), yield, SetRef(window_title), IM_CHECK or ItemInfo to verify window/content.

---

## 5. Constraints and risks

- **Ref stability:** Button refs can change if labels or layout change; prefer stable labels (or document the refs in this doc). Hidden IDs (e.g. `##id`) are stable but must be discovered.
- **Metrics window:** Only available when the app is **invoked** with `--show-metrics`. The test cannot force it at runtime (e.g. by toggling state): `metrics_available_` is set from `CommandLineArgs::show_metrics` in `Application` construction and is immutable. Run the test process with `--show-metrics` to cover Metrics; otherwise skip or disable the metrics_window_open test.
- **Ordering:** Open-window tests should not depend on a prior search; they can run right after smoke with index loading in background (or after index ready).
- **Coverage build:** New tests will only improve coverage when the app is built and run with coverage (e.g. `scripts/build_tests_macos.sh --coverage`) and the test engine runs these tests; ensure the coverage report is generated from a run that includes the new tests.

---

## 6. Suggested implementation order

1. **Help window open** – single button click, one window title, high impact (HelpWindow 0%).
2. **Settings window open** – same pattern, very high impact (SettingsWindow 0%).
3. **Search Syntax Guide open** – depends on how it’s opened (menu/item); then assert window.
4. **Metrics window open** – same as Help/Settings; gate on `--show-metrics` if needed.
5. **Time or size filter + search** – extend hook if necessary; then regression-style test with filter and expected count.

This order maximizes coverage gain (0% → exercised) for major UI files while keeping tests simple and stable.
