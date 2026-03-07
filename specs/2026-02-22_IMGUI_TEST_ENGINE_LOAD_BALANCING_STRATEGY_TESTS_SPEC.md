# Specification: ImGui Test Engine – Load Balancing Strategy Tests

**Feature:** Add ImGui Test Engine tests that verify each load balancing strategy (static, hybrid, dynamic) produces the **same search result count** for the same search parameters. Tests reuse the existing regression test cases and golden expected counts; only the strategy is varied. **Goal: catch regressions** where one strategy would return different results than others for the same query.

**Date:** 2026-02-22  
**Status:** Draft  
**References:** `specs/SPECIFICATION_DRIVEN_DEVELOPMENT_PROMPT.md`, `specs/2026-02-19_IMGUI_REGRESSION_TESTS_STD_LINUX_FILESYSTEM_SPEC.md`, `src/ui/ImGuiTestEngineTests.cpp`, `tests/FileIndexSearchStrategyTests.cpp`, `AGENTS.md`, `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md`

---

## Step 1 — Requirements summary and assumptions

### Scope

- **Primary goal:** Add ImGui Test Engine tests that:
  - Use the same app setup as existing regression tests (`--index-from-file=tests/data/std-linux-filesystem.txt`).
  - For each **regression test case** (show_all, filename_tty, path_dev, ext_conf, path_etc_ext_conf, folders_only), run the **same search** once per **load balancing strategy** (static, hybrid, dynamic).
  - Before each run: set the load balancing strategy (via test hook), set search params, trigger search, wait for completion.
  - Assert that the **result count equals the golden expected count** for that case (unchanged from existing regression spec). Thus all strategies are validated to produce the same result for the same query.
- **Out of scope for this spec:** Testing interleaved or work_stealing strategies (optional build); performance or timing comparisons; changing golden expected counts; testing on Windows/Linux ImGui test runs (harness is macOS-first; design is cross-platform).
- **Non-goals:** No new doctest-only tests; no OS-level automation. Tests run **in-process** via the existing ImGui Test Engine.

### Functional requirements

| ID | Requirement |
|----|-------------|
| R1 | The regression test hook (or Application) supports **setting the load balancing strategy** before a search (e.g. `SetLoadBalancingStrategy("static")`, `"hybrid"`, `"dynamic"`) so that the next triggered search uses that strategy. |
| R2 | A **load-balancing strategy test** is defined for each pair (regression_case_id, strategy): same index, same search params as the existing regression case, strategy set to the given value, then trigger search, wait, and assert result count == expected count for that case. |
| R3 | Strategies under test: **static**, **hybrid**, **dynamic** (same set as in `FileIndexSearchStrategyTests` “All Strategies - Common Tests”; interleaved/work_stealing optional in a later phase). |
| R4 | Reuse existing golden data: `regression_expected::kShowAll`, `kFilenameTty`, etc., and `tests/data/std-linux-filesystem-expected.json`. No new golden file; same expected counts for all strategies. |
| R5 | When `ENABLE_IMGUI_TEST_ENGINE` is OFF, behavior is unchanged; new tests are only compiled and run when the option is ON. |

### Non-functional and constraints

- **C++17 only;** no backward compatibility required.
- **No changes inside existing platform `#ifdef` blocks** except where adding test hook support.
- **DRY:** Reuse `RunRegressionTestCase`-style flow (wait index ready, set params, trigger search, wait complete, assert count); add a step to set strategy before set params/trigger.
- **ImGui:** All ImGui and test engine usage on main thread; immediate mode rules from AGENTS.md apply.
- **Quality:** No new SonarQube or clang-tidy violations; preferred style (in-class init, const ref, no `} if (` on one line). All `#endif` with matching comments.

### Assumptions

- The dataset and golden expected counts remain as in `specs/2026-02-19_IMGUI_REGRESSION_TESTS_STD_LINUX_FILESYSTEM_SPEC.md`. Load balancing does not change the **set** of results, only their distribution across threads; therefore the same expected count applies to every strategy.
- The app uses `settings_->loadBalancingStrategy` (or equivalent) when building `SearchContext` for search; setting it before `TriggerManualSearch` is sufficient to run the next search with that strategy.
- Existing regression tests do not set the strategy (they use whatever default is in settings). New tests explicitly set it per run.

### Edge cases

| # | Edge case | Requirement |
|---|-----------|-------------|
| E1 | Invalid strategy name | Hook or Application should treat invalid strategy as error or fallback (e.g. default to hybrid) and not crash. |
| E2 | Index not ready / search not complete | Same wait/timeout behavior as existing regression tests. |
| E3 | Strategy list differs by build (e.g. interleaved only with FAST_LIBS_BOOST) | This spec targets static, hybrid, dynamic only; optional strategies can be added later. |

---

## Step 2 — User stories (Gherkin)

| ID | Priority | Summary | Gherkin |
|----|----------|---------|---------|
| L1 | P0 | Same result count for show_all across strategies | **Given** the app is running with `--index-from-file=tests/data/std-linux-filesystem.txt`, **When** for each strategy in {static, hybrid, dynamic} the test sets that strategy, sets no filters, triggers search, and waits for completion, **Then** the result count equals the golden value for show_all (789531). **And** the test is implemented in the ImGui Test Engine and asserts against the same golden data. |
| L2 | P0 | Same result count for filename_tty across strategies | **Given** the same index is loaded, **When** for each strategy the test sets that strategy, sets filename "tty", triggers search, and waits, **Then** the result count equals the golden value for filename_tty (743). |
| L3 | P0 | Same result count for path_dev across strategies | **Given** the same index, **When** for each strategy the test sets path "/dev", triggers search, and waits, **Then** the result count equals the golden value for path_dev (23496). |
| L4 | P0 | Same result count for ext_conf across strategies | **Given** the same index, **When** for each strategy the test sets extension "conf", triggers search, and waits, **Then** the result count equals the golden value for ext_conf (683). |
| L5 | P1 | Same result count for path_etc_ext_conf across strategies | **Given** the same index, **When** for each strategy the test sets path "/etc" and extension "conf", triggers search, and waits, **Then** the result count equals the golden value for path_etc_ext_conf (229). |
| L6 | P1 | Same result count for folders_only across strategies | **Given** the same index, **When** for each strategy the test sets folders_only and triggers search, **Then** the result count equals the golden value for folders_only (170335). |
| L7 | P2 | Hook supports setting load balancing strategy | **Given** the regression test hook is implemented by Application, **When** the test engine calls SetLoadBalancingStrategy with a valid strategy name before TriggerManualSearch, **Then** the next search uses that strategy and the result count matches the expected value for the current test case. |

---

## Step 3 — Architecture overview

### High-level design

- **Data flow:** (1) App started with `--index-from-file=tests/data/std-linux-filesystem.txt` (unchanged). (2) For each regression case (e.g. show_all) and each strategy (static, hybrid, dynamic): set ref to main window; wait for index ready; **set load balancing strategy** via hook; set search params (filename, path, extensions, folders_only) as in existing regression; trigger search; wait for search complete; yield as in existing tests; read result count. (3) Assert result count == expected count for that case (from `regression_expected::*` or golden file). No new golden data; same expected counts for all strategies.
- **Hook extension:** `IRegressionTestHook` gains `void SetLoadBalancingStrategy(std::string_view strategy)`. Application’s implementation sets `settings_->loadBalancingStrategy` (or a test-only override that SearchContext reads) so the next `TriggerManualSearch` uses that strategy. Strategy names: `"static"`, `"hybrid"`, `"dynamic"` (lowercase, matching existing settings validation).
- **Test registration:** New tests live in the same test engine and file as existing regression tests (e.g. `ImGuiTestEngineTests.cpp`), under a category such as `regression_load_balancing` or `load_balancing`. Each test can be one (case_id, strategy) pair or a loop over strategies for one case_id; implementation choice in task breakdown.
- **Single responsibility:** Hook = set strategy + existing set params/trigger/count. Test = orchestrate (strategy × case), wait, assert. No duplication of golden values; reuse `regression_expected::*`.

### Components

- **IRegressionTestHook (extended):** Add `SetLoadBalancingStrategy(std::string_view)`. Implemented by `ApplicationRegressionTestHook` → `Application::SetLoadBalancingStrategyForRegressionTest(strategy)` which sets `settings_->loadBalancingStrategy` to the given value (validated or defaulted per existing Settings rules).
- **Application:** New method `SetLoadBalancingStrategyForRegressionTest(std::string_view strategy)` (guarded by `#ifdef ENABLE_IMGUI_TEST_ENGINE`) that writes to the same settings used when building SearchContext for manual search.
- **ImGuiTestEngineTests.cpp:** New test group (e.g. `regression_load_balancing`). For each regression case, either: (a) one test per (case, strategy) that calls the shared runner with (case_id, strategy, expected_count), or (b) one test per case that loops over strategies and asserts count each time. Reuse `WaitForIndexReady`, `WaitForSearchComplete`, and the same yield/count logic as `RunRegressionTestCase`.
- **Golden data:** No new file; reuse `regression_expected::kShowAll`, `kFilenameTty`, etc., and the existing JSON if needed.

### Threading

- Unchanged: all ImGui and test engine code on main thread; search runs in existing worker; test yields until completion.

### Invariants

- For a fixed (dataset, search params), every load balancing strategy must produce the same result count (invariant already assumed by existing doctest “All strategies return correct results”).
- Golden expected counts are produced by the same code path (SearchWorker + FileIndex) with default (or any) strategy; we only vary strategy and assert the same count.

---

## Step 4 — Acceptance criteria

| Story | Criterion | Measurable check |
|-------|-----------|-------------------|
| L1–L6 | Each load-balancing strategy test passes | With `ENABLE_IMGUI_TEST_ENGINE=ON`, run the new test group; for each (case, strategy) the test completes and asserts result count == golden expected for that case. |
| L7 | Hook sets strategy correctly | After calling `SetLoadBalancingStrategy("static")` (then params + trigger), result count matches expected; same for "hybrid" and "dynamic" for the same case. |
| — | No new Sonar/clang-tidy violations | Run clang-tidy and Sonar on changed files; fix or align style. |
| — | No regression when test engine OFF | Build with `ENABLE_IMGUI_TEST_ENGINE=OFF`; app and existing tests unchanged. |
| — | Existing regression tests unchanged | Existing regression tests (show_all, filename_tty, …) still pass and do not require setting strategy (default remains valid). |

---

## Step 5 — Task breakdown

| Phase | Task | Dependencies | Est. (h) | Notes |
|-------|------|--------------|----------|--------|
| 1 | Extend `IRegressionTestHook` with `SetLoadBalancingStrategy(std::string_view strategy)` and extend `RegressionTestHookAdapter` with a no-op default. Add `SetLoadBalancingStrategyForRegressionTest(std::string_view)` to Application (guarded by `ENABLE_IMGUI_TEST_ENGINE`); implement by setting `settings_->loadBalancingStrategy` with validation (reuse existing valid names: static, hybrid, dynamic). | None | 0.5 | Validate strategy name; invalid → log and use default (e.g. hybrid) to avoid test crash. |
| 2 | In `ImGuiTestEngineTests.cpp`, add a shared helper (e.g. `RunRegressionTestCaseWithStrategy`) that takes (ctx, case_id, strategy, filename, path, extensions_semicolon, folders_only, expected_count), or factor the existing `RunRegressionTestCase` to accept an optional strategy and call the new hook method when set. | Phase 1 | 0.5 | Reuse wait/yield/assert logic; only add “set strategy” step when strategy is not empty. |
| 3 | Register new tests: for each regression case (show_all, filename_tty, path_dev, ext_conf, path_etc_ext_conf, folders_only), register one test per strategy (static, hybrid, dynamic) that runs the case with that strategy and asserts expected count (e.g. test names `load_balancing/static_show_all`, `load_balancing/hybrid_show_all`, …). Alternatively, one test per case that loops over strategies and asserts each. | Phase 2 | 1 | Prefer one test per (case, strategy) for clear failure reporting; total 6×3 = 18 tests, or 6 tests with 3 assertions each. |
| 4 | Run `scripts/build_tests_macos.sh` with `ENABLE_IMGUI_TEST_ENGINE=ON`; run the ImGui Test Engine and execute all new load-balancing strategy tests; all pass. | Phase 3 | 0.5 | |
| 5 | Review: no new Sonar/clang-tidy issues; all `#endif` commented; new code guarded by `ENABLE_IMGUI_TEST_ENGINE`. | Phase 4 | 0.25 | |

**Total (rough):** ~2.75 h.

---

## Step 6 — Risks and mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Settings object is read-only or not writable from test hook | Cannot change strategy | Ensure Application has non-const access to settings (it does for other regression APIs) and that SearchContext is built from current settings when TriggerManualSearch runs. |
| Running many tests (18) increases suite time | Slower feedback | Acceptable; each run is same as one regression test (wait index, set params, trigger, wait). Optionally run load_balancing group only in CI or on demand. |
| Invalid strategy name in test | Crash or wrong strategy | Validate in `SetLoadBalancingStrategyForRegressionTest`; fallback to hybrid and log. |

---

## Step 7 — Validation and handoff

### Review checklist

- [ ] No new SonarQube or clang-tidy violations; preferred style applied.
- [ ] No changes inside existing platform `#ifdef` blocks except for test hook implementation.
- [ ] All new code guarded by `#ifdef ENABLE_IMGUI_TEST_ENGINE` with matching `#endif` comments.
- [ ] Default build (option OFF) unchanged; `scripts/build_tests_macos.sh` passes with option OFF.
- [ ] With option ON, all new load-balancing strategy tests run and pass; existing regression tests still pass.

### Using this spec with Cursor Agent/Composer

1. Use this document as the single source of truth for ImGui Test Engine load balancing strategy tests.
2. In the task prompt, reference `AGENTS.md` and `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md` and paste the Strict Constraints block from `AGENT_STRICT_CONSTRAINTS.md`.
3. Implement in the order of the task breakdown (Phase 1 → 5).
4. On macOS, run `scripts/build_tests_macos.sh` after C++ changes. Manually run the app with `ENABLE_IMGUI_TEST_ENGINE=ON` and execute the new load_balancing tests from the test engine UI.
5. Verify no new Sonar/clang-tidy issues on changed files.

### Strict constraints reminder

When implementing, follow the "Strict Constraints / Rules to Follow" from `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md` (Sonar/clang-tidy, platform rules, verification steps). Do not compile or run build commands except `scripts/build_tests_macos.sh` on macOS as per AGENTS.md.

---

## Test matrix (reference)

| Case id | Strategy | Filename | Path | Ext | Folders only | Expected count |
|---------|----------|----------|------|-----|--------------|----------------|
| show_all | static / hybrid / dynamic | "" | "" | "" | false | 789531 |
| filename_tty | static / hybrid / dynamic | tty | "" | "" | false | 743 |
| path_dev | static / hybrid / dynamic | "" | /dev | "" | false | 23496 |
| ext_conf | static / hybrid / dynamic | "" | "" | conf | false | 683 |
| path_etc_ext_conf | static / hybrid / dynamic | "" | /etc | conf | false | 229 |
| folders_only | static / hybrid / dynamic | "" | "" | "" | true | 170335 |

Expected counts from `regression_expected::*` and `tests/data/std-linux-filesystem-expected.json` (see `specs/2026-02-19_IMGUI_REGRESSION_TESTS_STD_LINUX_FILESYSTEM_SPEC.md`).

---

## Summary

| Item | Decision |
|------|----------|
| Goal | Verify static, hybrid, and dynamic load balancing strategies yield the same result count for the same search (reuse existing regression cases and golden data). |
| Hook extension | `IRegressionTestHook::SetLoadBalancingStrategy(std::string_view)`; Application implements via `settings_->loadBalancingStrategy` (with validation). |
| Test layout | New test group (e.g. `regression_load_balancing` or `load_balancing`); one test per (case_id, strategy) or one test per case with strategy loop. |
| Golden data | Reuse existing; no new JSON or constants. |
| Strategies | static, hybrid, dynamic (interleaved/work_stealing optional later). |
