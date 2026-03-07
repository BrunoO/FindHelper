# ImGui Test Engine – Test Preconditions and Guarantees

**Date:** 2026-02-23

This document lists each ImGui Test Engine test, the preconditions that must hold for the test to run in expected conditions, and how we guarantee those preconditions (either by asserting at test start or by documenting run requirements).

---

## Environment / command-line preconditions

These apply to the process that runs the app with ImGui tests:

| Precondition | Required by | How guaranteed |
|-------------|-------------|----------------|
| App run with `--index-from-file=tests/data/std-linux-filesystem.txt` | All **regression**, **load_balancing**, **streaming** tests | `RunRegressionTestCase` checks `GetIndexSize() == regression_expected::kIndexSize`; fails with clear message to use that flag if mismatch. |
| App run with `--run-imgui-tests-and-exit` (or Test Engine window open) | All tests | Test runner / CI must pass this; tests are only executed when the engine runs them. |
| (Optional) `--show-metrics` | **metrics_window_open** | Test skips gracefully with `ItemExists(kMetricsButtonRef)`; no failure if button not present. |

---

## Per-test preconditions and guarantees

### smoke

| Test | Preconditions | How guaranteed |
|------|----------------|----------------|
| **main_window_ref** | Main window titled "Find Helper" exists (assumed). | Test only does `SetRef("Find Helper")` and `Yield()` to prove engine/coroutine run. We do not call `GetWindowByRef` here because the test engine may resolve that ref differently than `SetRef`. Main window usability is validated by UI window tests (they require the toolbar button to exist via `RequireItemExists`). |

---

### ui_windows

| Test | Preconditions | How guaranteed |
|------|----------------|----------------|
| **help_window_open** | Main window exists; Help button `ICON_FA_CIRCLE_QUESTION " Help##toolbar_help"` exists. | After `SetRef("Find Helper")` and `Yield()`, `IM_CHECK(ctx->ItemExists(kHelpButtonRef))` with error message; then `ItemClick`. |
| **settings_window_open** | Main window exists; Settings button `##toolbar_settings` exists. | Same: `ItemExists(kSettingsButtonRef)` and `IM_CHECK` before `ItemClick`. |
| **search_help_window_open** | Main window exists; Search Syntax button `##SearchHelpToggle` exists. | Same: `ItemExists(kSearchHelpButtonRef)` and `IM_CHECK` before `ItemClick`. |
| **metrics_window_open** | Main window exists; Metrics button exists only when `--show-metrics`. | `ItemExists(kMetricsButtonRef)`; if false, test returns (skip) without failure. Window title checked with `std::strcmp(win->Name, kMetricsWindowTitle) == 0`. |

---

### regression (and RunRegressionTestCase)

All regression tests use `RunRegressionTestCase`, which enforces these preconditions in order:

| Precondition | How guaranteed |
|-------------|----------------|
| `IRegressionTestHook` non-null | `IM_ERRORF` and return if `hook == nullptr`. |
| Index ready for search | `WaitForIndexReady(ctx, hook, case_id)`; fails with message after `kMaxWaitFrames` if not ready. |
| Index size matches golden dataset | `GetIndexSize() == regression_expected::kIndexSize`; `IM_ERRORF` and `IM_CHECK` with hint to run with `--index-from-file=tests/data/std-linux-filesystem.txt`. |
| Load balancing strategy (when test sets one) | After `SetLoadBalancingStrategy(strategy)`, `IM_CHECK(GetLoadBalancingStrategy() == strategy)`; surfaces null `settings_` or config misapplication. |
| Streaming setting | After `SetStreamPartialResults(...)`, `IM_CHECK(GetStreamPartialResults() == expected)`; non-streaming tests set false and assert getter is false; streaming tests assert getter matches. |
| Search params applied | After `SetSearchParams(...)`, `IM_CHECK` all four getters (filename, path, extensions, folders_only) match; then `TriggerManualSearch()`. |
| Search completed | `WaitForSearchComplete(ctx, hook, case_id)`; then two `Yield()` so final count is read after streaming finalization. |
| Result count | `IM_CHECK_EQ(GetSearchResultCount(), expected_count)`. |

No additional per-test precondition is needed at test start; each test calls `RunRegressionTestCase` with the right case and options.

---

### load_balancing

Same as regression: each test calls `RunRegressionTestCase(..., strategy)` with strategy `"static"`, `"hybrid"`, or `"dynamic"`. All preconditions are enforced inside `RunRegressionTestCase` (including strategy getter check).

---

### streaming

Same as regression: each test calls `RunRegressionTestCase(..., stream_on/off)`. Streaming getter is checked inside `RunRegressionTestCase`; both stream-on and stream-off tests assert final result count matches expected.

---

## Summary

- **Smoke and UI window tests:** Preconditions are guaranteed by explicit checks at the beginning of each test (main window ref, button/item existence). Metrics test skips when the optional Metrics button is not present.
- **Regression, load_balancing, streaming:** Preconditions are guaranteed inside `RunRegressionTestCase`: hook non-null, index ready and size, strategy/streaming/search params getters verified after set, then search triggered and completion/count asserted.
- **CI/run requirement:** Regression-style tests require the app to be started with `--index-from-file=tests/data/std-linux-filesystem.txt` (e.g. `scripts/build_tests_macos.sh --run-imgui-tests`). Failure messages point to this when index size or result count is wrong.
