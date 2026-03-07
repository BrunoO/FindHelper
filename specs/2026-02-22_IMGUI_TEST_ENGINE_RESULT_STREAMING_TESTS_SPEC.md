# Specification: ImGui Test Engine – Result Streaming Option Tests

**Feature:** Add ImGui Test Engine tests that verify the **result streaming option** (stream partial results on vs off) produces the **same final search result count** for the same search parameters. Tests reuse the existing regression test cases and golden expected counts; only the streaming setting is varied. **Goal: catch regressions** where streaming vs non-streaming would return different final counts for the same query.

**Date:** 2026-02-22  
**Status:** Draft  
**References:** `specs/SPECIFICATION_DRIVEN_DEVELOPMENT_PROMPT.md`, `specs/2026-02-19_IMGUI_REGRESSION_TESTS_STD_LINUX_FILESYSTEM_SPEC.md`, `specs/2026-02-22_IMGUI_TEST_ENGINE_LOAD_BALANCING_STRATEGY_TESTS_SPEC.md`, `src/ui/ImGuiTestEngineTests.cpp`, `src/search/SearchWorker.cpp`, `src/search/SearchController.cpp`, `AGENTS.md`, `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md`

---

## Step 1 — Requirements summary and assumptions

### Scope

- **Primary goal:** Add ImGui Test Engine tests that:
  - Use the same app setup as existing regression tests (`--index-from-file=tests/data/std-linux-filesystem.txt`).
  - For each **regression test case** (show_all, filename_tty, path_dev, ext_conf, path_etc_ext_conf, folders_only), run the **same search** twice: once with **streaming enabled** (`streamPartialResults == true`) and once with **streaming disabled** (`streamPartialResults == false`).
  - Before each run: set the streaming option (via test hook), set search params, trigger search, wait for completion (including any streaming finalization so `state.searchResults` is fully populated).
  - Assert that the **result count equals the golden expected count** for that case in both runs. Thus streaming and non-streaming are validated to produce the same final result set size.
- **Out of scope for this spec:** Testing incremental UI updates during streaming; performance or timing; changing golden expected counts; testing on Windows/Linux ImGui test runs (harness is macOS-first; design is cross-platform).
- **Non-goals:** No new doctest-only tests; no OS-level automation. Tests run **in-process** via the existing ImGui Test Engine.

### Functional requirements

| ID | Requirement |
|----|-------------|
| R1 | The regression test hook (or Application) supports **setting the result streaming option** before a search (e.g. `SetStreamPartialResults(true)` / `SetStreamPartialResults(false)`) so that the next triggered search uses that setting. |
| R2 | A **streaming option test** is defined for each pair (regression_case_id, stream_enabled): same index, same search params as the existing regression case, streaming set to the given value, then trigger search, wait for completion, and assert result count == expected count for that case. |
| R3 | Two modes under test: **streaming on** (true) and **streaming off** (false), matching `AppSettings::streamPartialResults` and the "Stream search results" UI checkbox. |
| R4 | Reuse existing golden data: `regression_expected::kShowAll`, `kFilenameTty`, etc., and `tests/data/std-linux-filesystem-expected.json`. No new golden file; same expected counts for both streaming modes. |
| R5 | When `ENABLE_IMGUI_TEST_ENGINE` is OFF, behavior is unchanged; new tests are only compiled and run when the option is ON. |

### Non-functional and constraints

- **C++17 only;** no backward compatibility required.
- **No changes inside existing platform `#ifdef` blocks** except where adding test hook support.
- **DRY:** Reuse `RunRegressionTestCase`-style flow (wait index ready, set params, trigger search, wait complete, assert count); add a step to set streaming option before set params/trigger.
- **ImGui:** All ImGui and test engine usage on main thread; immediate mode rules from AGENTS.md apply.
- **Quality:** No new SonarQube or clang-tidy violations; preferred style (in-class init, const ref, no `} if (` on one line). All `#endif` with matching comments.

### Assumptions

- The dataset and golden expected counts remain as in `specs/2026-02-19_IMGUI_REGRESSION_TESTS_STD_LINUX_FILESYSTEM_SPEC.md`. Streaming only changes **how** results are delivered (incremental batches vs. all at once); the **final set** and count are the same. Therefore the same expected count applies to both streaming on and off.
- The app uses `settings_->streamPartialResults` when starting a search; `SearchController` calls `search_worker.SetStreamPartialResults(settings.streamPartialResults)` before starting the search. Setting `settings_->streamPartialResults` before `TriggerManualSearch` is sufficient to run the next search with that mode.
- After search completion, both streaming and non-streaming paths populate `state.searchResults` (streaming via `FinalizeStreamingSearchComplete` → `UpdateSearchResults`). The existing hook’s `GetSearchResultCountForRegressionTest()` (which returns `state_.searchResults.size()`) is correct for both modes after completion.
- Existing regression tests do not set the streaming option (they use the default, which is true). New tests explicitly set it per run.

### Edge cases

| # | Edge case | Requirement |
|---|-----------|-------------|
| E1 | Index not ready / search not complete | Same wait/timeout behavior as existing regression tests. For streaming, ensure enough yields after completion so `FinalizeStreamingSearchComplete` / `PollResults` has run and `state.searchResults` is populated (same as existing regression: two yields after search complete). |
| E2 | Streaming collector not yet finalized | Test must wait until `IsSearchComplete()` is true and then yield as in existing regression (two consecutive yields) so the main loop applies streaming results to `state.searchResults` before reading count. |

---

## Step 2 — User stories (Gherkin)

| ID | Priority | Summary | Gherkin |
|----|----------|---------|---------|
| S1 | P0 | Same result count for show_all with streaming on and off | **Given** the app is running with `--index-from-file=tests/data/std-linux-filesystem.txt`, **When** the test sets streaming on, sets no filters, triggers search, waits for completion and yields, **Then** the result count equals the golden value for show_all (789531). **And when** the test sets streaming off and repeats the same search, **Then** the result count again equals 789531. **And** the test is implemented in the ImGui Test Engine and asserts against the same golden data. |
| S2 | P0 | Same result count for filename_tty with streaming on and off | **Given** the same index is loaded, **When** the test runs the filename_tty case (filename "tty") with streaming on and with streaming off, **Then** both runs yield the golden value for filename_tty (743). |
| S3 | P0 | Same result count for path_dev with streaming on and off | **Given** the same index, **When** the test runs the path_dev case with streaming on and off, **Then** both runs yield the golden value for path_dev (23496). |
| S4 | P0 | Same result count for ext_conf with streaming on and off | **Given** the same index, **When** the test runs the ext_conf case with streaming on and off, **Then** both runs yield the golden value for ext_conf (683). |
| S5 | P1 | Same result count for path_etc_ext_conf with streaming on and off | **Given** the same index, **When** the test runs the path_etc_ext_conf case with streaming on and off, **Then** both runs yield the golden value for path_etc_ext_conf (229). |
| S6 | P1 | Same result count for folders_only with streaming on and off | **Given** the same index, **When** the test runs the folders_only case with streaming on and off, **Then** both runs yield the golden value for folders_only (170335). |
| S7 | P2 | Hook supports setting stream partial results | **Given** the regression test hook is implemented by Application, **When** the test engine calls SetStreamPartialResults(true) or SetStreamPartialResults(false) before TriggerManualSearch, **Then** the next search uses that setting and the final result count matches the expected value for the current test case. |

---

## Step 3 — Architecture overview

### High-level design

- **Data flow:** (1) App started with `--index-from-file=tests/data/std-linux-filesystem.txt` (unchanged). (2) For each regression case (e.g. show_all) and each streaming value (true, false): set ref to main window; wait for index ready; **set stream partial results** via hook; set search params (filename, path, extensions, folders_only) as in existing regression; trigger search; wait for search complete; yield twice (as in existing regression) so streaming finalization is applied; read result count. (3) Assert result count == expected count for that case (from `regression_expected::*`). No new golden data; same expected counts for both streaming modes.
- **Hook extension:** `IRegressionTestHook` gains `void SetStreamPartialResults(bool stream)`. Application’s implementation sets `settings_->streamPartialResults = stream` so the next `TriggerManualSearch` uses that value (SearchController already reads settings when starting search).
- **Test registration:** New tests live in the same test engine and file as existing regression tests (e.g. `ImGuiTestEngineTests.cpp`), under a category such as `regression_streaming` or `streaming`. Each test can be one (case_id, stream_bool) pair or one test per case that runs the case twice (streaming on, then off) and asserts count both times; implementation choice in task breakdown.
- **Single responsibility:** Hook = set streaming + existing set params/trigger/count. Test = orchestrate (streaming × case), wait, assert. No duplication of golden values; reuse `regression_expected::*`.

### Components

- **IRegressionTestHook (extended):** Add `SetStreamPartialResults(bool stream)`. Implemented by `ApplicationRegressionTestHook` → `Application::SetStreamPartialResultsForRegressionTest(bool stream)` which sets `settings_->streamPartialResults = stream`.
- **Application:** New method `SetStreamPartialResultsForRegressionTest(bool stream)` (guarded by `#ifdef ENABLE_IMGUI_TEST_ENGINE`) that writes to the same settings used when SearchController starts a search (`search_worker.SetStreamPartialResults(settings.streamPartialResults)`).
- **ImGuiTestEngineTests.cpp:** New test group (e.g. `regression_streaming` or `streaming`). For each regression case, either: (a) one test per (case, stream_bool) that calls the shared runner with (case_id, stream_enabled, expected_count), or (b) one test per case that runs the case with streaming true, asserts count, then runs with streaming false, asserts count. Reuse `WaitForIndexReady`, `WaitForSearchComplete`, and the same yield/count logic as `RunRegressionTestCase`.
- **Golden data:** No new file; reuse `regression_expected::kShowAll`, `kFilenameTty`, etc., and the existing JSON if needed.

### Threading

- Unchanged: all ImGui and test engine code on main thread; search runs in existing worker; test yields until completion. Streaming finalization runs on main thread in `PollResults` when the worker has marked search complete.

### Invariants

- For a fixed (dataset, search params), streaming on and streaming off must produce the same **final** result count (invariant from design: only delivery differs, not the set of results).
- Golden expected counts are produced by the same code path (SearchWorker + FileIndex); we only vary the delivery mode (streaming vs non-streaming) and assert the same count.

---

## Step 4 — Acceptance criteria

| Story | Criterion | Measurable check |
|-------|-----------|------------------|
| S1–S6 | Each streaming option test passes | With `ENABLE_IMGUI_TEST_ENGINE=ON`, run the new test group; for each (case, stream_enabled) the test completes and asserts result count == golden expected for that case. |
| S7 | Hook sets streaming correctly | After calling `SetStreamPartialResults(true)` (then params + trigger), result count matches expected; same for `SetStreamPartialResults(false)` for the same case. |
| — | No new Sonar/clang-tidy violations | Run clang-tidy and Sonar on changed files; fix or align style. |
| — | No regression when test engine OFF | Build with `ENABLE_IMGUI_TEST_ENGINE=OFF`; app and existing tests unchanged. |
| — | Existing regression tests unchanged | Existing regression tests (show_all, filename_tty, …) still pass and do not require setting the streaming option (default remains valid). |

---

## Step 5 — Task breakdown

| Phase | Task | Dependencies | Est. (h) | Notes |
|-------|------|--------------|----------|--------|
| 1 | Extend `IRegressionTestHook` with `SetStreamPartialResults(bool stream)` and extend `RegressionTestHookAdapter` with a no-op default. Add `SetStreamPartialResultsForRegressionTest(bool stream)` to Application (guarded by `ENABLE_IMGUI_TEST_ENGINE`); implement by setting `settings_->streamPartialResults = stream`. | None | 0.25 | No validation needed; boolean is always valid. |
| 2 | In `ImGuiTestEngineTests.cpp`, add a shared helper (e.g. `RunRegressionTestCaseWithStreaming`) that takes (ctx, case_id, stream_enabled, filename, path, extensions_semicolon, folders_only, expected_count), or factor the existing `RunRegressionTestCase` to accept an optional stream_enabled and call the new hook method when provided. | Phase 1 | 0.5 | Reuse wait/yield/assert logic; only add “set streaming” step before set params/trigger. |
| 3 | Register new tests: for each regression case (show_all, filename_tty, path_dev, ext_conf, path_etc_ext_conf, folders_only), register tests that run the case with streaming on and with streaming off and assert expected count each time (e.g. test names `streaming/show_all_stream_on`, `streaming/show_all_stream_off`, … or one test per case that asserts both). | Phase 2 | 1 | Prefer one test per (case, stream_bool) for clear failure reporting; total 6×2 = 12 tests, or 6 tests with 2 assertions each. |
| 4 | Run `scripts/build_tests_macos.sh` with `ENABLE_IMGUI_TEST_ENGINE=ON`; run the ImGui Test Engine and execute all new streaming option tests; all pass. | Phase 3 | 0.5 | Ensure two yields after search complete so streaming path has applied results. |
| 5 | Review: no new Sonar/clang-tidy issues; all `#endif` commented; new code guarded by `ENABLE_IMGUI_TEST_ENGINE`. | Phase 4 | 0.25 | |

**Total (rough):** ~2.5 h.

---

## Step 6 — Risks and mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Streaming results not yet in state.searchResults when test reads count | Wrong or flaky assertion | Use same two consecutive yields after search complete as existing regression tests; document that PollResults must run to finalize streaming. |
| Settings object not writable from test hook | Cannot change streaming | Ensure Application has non-const access to settings (it does for other regression APIs); SearchController reads settings at search start. |
| Running 12 tests increases suite time | Slower feedback | Acceptable; each run is same as one regression test. Optionally run streaming group only in CI or on demand. |

---

## Step 7 — Validation and handoff

### Review checklist

- [ ] No new SonarQube or clang-tidy violations; preferred style applied.
- [ ] No changes inside existing platform `#ifdef` blocks except for test hook implementation.
- [ ] All new code guarded by `#ifdef ENABLE_IMGUI_TEST_ENGINE` with matching `#endif` comments.
- [ ] Default build (option OFF) unchanged; `scripts/build_tests_macos.sh` passes with option OFF.
- [ ] With option ON, all new streaming option tests run and pass; existing regression tests still pass.

### Using this spec with Cursor Agent/Composer

1. Use this document as the single source of truth for ImGui Test Engine result streaming option tests.
2. In the task prompt, reference `AGENTS.md` and `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md` and paste the Strict Constraints block from `AGENT_STRICT_CONSTRAINTS.md`.
3. Implement in the order of the task breakdown (Phase 1 → 5).
4. On macOS, run `scripts/build_tests_macos.sh` after C++ changes. Manually run the app with `ENABLE_IMGUI_TEST_ENGINE=ON` and execute the new streaming tests from the test engine UI.
5. Verify no new Sonar/clang-tidy issues on changed files.

### Strict constraints reminder

When implementing, follow the "Strict Constraints / Rules to Follow" from `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md` (Sonar/clang-tidy, platform rules, verification steps). Do not compile or run build commands except `scripts/build_tests_macos.sh` on macOS as per AGENTS.md.

---

## Test matrix (reference)

| Case id | Stream partial results | Filename | Path | Ext | Folders only | Expected count |
|---------|-------------------------|----------|------|-----|--------------|----------------|
| show_all | true / false | "" | "" | "" | false | 789531 |
| filename_tty | true / false | tty | "" | "" | false | 743 |
| path_dev | true / false | "" | /dev | "" | false | 23496 |
| ext_conf | true / false | "" | "" | conf | false | 683 |
| path_etc_ext_conf | true / false | "" | /etc | conf | false | 229 |
| folders_only | true / false | "" | "" | "" | true | 170335 |

Expected counts from `regression_expected::*` and `tests/data/std-linux-filesystem-expected.json` (see `specs/2026-02-19_IMGUI_REGRESSION_TESTS_STD_LINUX_FILESYSTEM_SPEC.md`).

---

## Summary

| Item | Decision |
|------|----------|
| Goal | Verify streaming on and streaming off yield the same final result count for the same search (reuse existing regression cases and golden data). |
| Hook extension | `IRegressionTestHook::SetStreamPartialResults(bool stream)`; Application implements via `settings_->streamPartialResults = stream`. |
| Test layout | New test group (e.g. `regression_streaming` or `streaming`); one test per (case_id, stream_bool) or one test per case with two assertions. |
| Golden data | Reuse existing; no new JSON or constants. |
| Modes | streaming on (true), streaming off (false). |
