# Specification: ImGui Test Engine Regression Tests (std-linux-filesystem Dataset)

**Feature:** Implement regression test cases using the integrated ImGui Test Engine that load the canonical dataset `tests/data/std-linux-filesystem.txt`, run predefined searches (covering different search methods), and assert against precomputed expected results. **Goal: catch regressions** in search behavior.

**Date:** 2026-02-19  
**Status:** Implemented  
**References:** `specs/SPECIFICATION_DRIVEN_DEVELOPMENT_PROMPT.md`, `specs/2026-02-20_IMGUI_TEST_ENGINE_INTEGRATION_SPEC.md`, `AGENTS.md`, `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md`

---

## Step 1 — Requirements summary and assumptions

### Scope

- **Primary goal:** Add ImGui Test Engine tests that:
  - Run the app with `--index-from-file=<path>` pointing to `tests/data/std-linux-filesystem.txt`.
  - Wait for the index to be ready (no ongoing index build).
  - Drive the UI (or equivalent code path) to set search parameters and trigger a search.
  - Wait for the search to complete and read the result count (and optionally a small set of sample paths).
  - Assert that the result count (and optionally sample paths) matches **precomputed golden values** stored in the repo.
- **Out of scope for this spec:** Testing search performance (timing), testing on Windows/Linux (deferred to later; harness is macOS-first), testing every possible filter combination.
- **Non-goals:** No external test runner (e.g. Python); no OS-level input simulation beyond what the ImGui Test Engine provides. Tests run **in-process** with the app.

### Functional requirements

| ID | Requirement |
|----|-------------|
| R1 | The app can be launched with `--index-from-file=<path>`. When the path is `tests/data/std-linux-filesystem.txt`, the index is populated from that file (one path per line; non-empty lines only) and `RecomputeAllPaths()` is called so search is valid. |
| R2 | A small set of **regression test cases** is defined, each with: (a) a unique id, (b) search parameters (filename, path, extensions, folders_only, case_sensitive), (c) **expected result count** (and optionally first/last path or checksum) computed beforehand and stored as golden data. |
| R3 | Each test case is implemented as an ImGui Test Engine test (registered in `ImGuiTestEngineTests.cpp` or a dedicated regression module). The test: ensures the index is loaded from the dataset, sets the search inputs, triggers search (e.g. manual search or instant search as per app behavior), waits for completion, then asserts result count (and optional path checks) against the golden data. |
| R4 | Golden expected results are **versioned in the repo** (e.g. a JSON or header/data file under `tests/` or `specs/`) and are **produced once** by running the app (or `search_benchmark`) against the dataset with the same parameters, then frozen. A documented procedure allows regenerating golden data if the dataset or app semantics change intentionally. |
| R5 | Test cases cover **different search methods**: e.g. (1) show-all / no filters, (2) filename substring, (3) path substring, (4) extension filter, (5) combined path + extension, (6) folders-only. Optionally: case-sensitive and/or regex/glob if supported and stable. |
| R6 | When `ENABLE_IMGUI_TEST_ENGINE` is OFF, behavior is unchanged; regression tests are only compiled and run when the option is ON (same as existing smoke test). |

### Non-functional and constraints

- **C++17 only;** no backward compatibility required.
- **No changes inside existing platform `#ifdef` blocks** to unify code; use platform-agnostic abstractions with separate implementations if needed.
- **Regression focus:** Tests are not meant to validate “correct” search semantics from first principles; they lock **current** behavior so that future changes do not silently change result counts for the same dataset and parameters.
- **ImGui:** All ImGui and test engine usage on main thread; immediate mode rules from AGENTS.md apply.
- **Quality:** No new SonarQube or clang-tidy violations; preferred style (in-class init, const ref, no `} if (` on one line). All `#endif` with matching comments.

### Assumptions

- The dataset `tests/data/std-linux-filesystem.txt` is **stable** for the purpose of regression (or changes are rare and golden data is updated when the dataset is updated).
- The app’s search behavior (substring vs. pattern, case sensitivity, extension matching) is implemented by `SearchWorker` / `FileIndex::SearchAsyncWithData` and is consistent between GUI-triggered search and `search_benchmark` when given the same `SearchParams` / config.
- The ImGui Test Engine can: set ref to the main window, locate the search input widgets (by ID or label), set input text, trigger a button click (e.g. Search or F5), and read result count from the UI (e.g. “N results” or table row count). If the UI does not expose result count in a way the test engine can read, the spec allows for an alternative (e.g. programmatic search API called from the test, or a test-only hook) to be defined in the task breakdown.
- Bootstrap with `--index-from-file` loads the index before the first frame where search is allowed; tests can wait for “index ready” by checking that search is not disabled (e.g. index build complete).

### Edge cases

| # | Edge case | Requirement |
|---|------------|-------------|
| E1 | Dataset file missing or unreadable | Test or harness should fail clearly (e.g. skip test with message, or fail assertion). |
| E2 | Index not ready when test runs | Test must wait for index ready (e.g. poll until search is enabled or a timeout). |
| E3 | Golden file missing or malformed | Test or test setup should fail clearly, not assume zero results. |
| E4 | App search semantics change intentionally | Update golden data and document in commit; regression test then locks the new behavior. |

---

## Step 2 — User stories (Gherkin)

| ID | Priority | Summary | Gherkin |
|----|----------|---------|---------|
| S1 | P0 | Regression test: show-all | **Given** the app is running with `--index-from-file` set to `tests/data/std-linux-filesystem.txt`, **When** the index is ready and no search filters are set and a search is triggered, **Then** the result count equals the precomputed golden value for “show all”. **And** the test is implemented in the ImGui Test Engine and asserts against golden data. |
| S2 | P0 | Regression test: filename substring | **Given** the app is running with the same index file, **When** the filename input is set to a known substring (e.g. `tty`) and search is triggered, **Then** the result count equals the precomputed golden value for that query. |
| S3 | P0 | Regression test: path substring | **Given** the app is running with the same index file, **When** the path input is set to a known substring (e.g. `/dev`) and search is triggered, **Then** the result count equals the precomputed golden value for that path query. |
| S4 | P0 | Regression test: extension filter | **Given** the app is running with the same index file, **When** the extension filter is set (e.g. `conf`) and search is triggered, **Then** the result count equals the precomputed golden value for that extension. |
| S5 | P1 | Regression test: combined path + extension | **Given** the app is running with the same index file, **When** both path and extension are set (e.g. path `/etc`, extension `conf`) and search is triggered, **Then** the result count equals the precomputed golden value. |
| S6 | P1 | Regression test: folders only | **Given** the app is running with the same index file, **When** “folders only” is enabled and search is triggered (no other filters), **Then** the result count equals the precomputed golden value for folders-only. |
| S7 | P2 | Golden data regeneration documented | **Given** the dataset or search semantics are intentionally changed, **When** a developer needs to update expected results, **Then** a clear procedure (e.g. run `search_benchmark` with index and JSON configs, or run the app and record counts) is documented and golden data files are updated and committed. |

---

## Step 3 — Architecture overview

### High-level design

- **Data flow:** (1) App started with `--index-from-file=tests/data/std-linux-filesystem.txt` → bootstrap loads index and calls `RecomputeAllPaths()`. (2) ImGui Test Engine test runs: sets ref to main window, waits for index ready, sets search inputs (filename/path/extension/folders-only), triggers search (e.g. F5 or Search button), yields until search complete, reads result count from UI (or from a test-visible state). (3) Test compares count (and optional path sample) to golden data and fails if mismatch.
- **Golden data:** Stored in repo (e.g. `tests/data/std-linux-filesystem-expected.json` or a header with constants). Contains one entry per test case: test_case_id, expected_count, optional first_path / last_path or checksum. Generated once by running the app or `search_benchmark` with the same dataset and configs.
- **Test registration:** Regression tests are registered in the same test engine as the existing smoke test (e.g. in `ImGuiTestEngineTests.cpp` or a new `ImGuiTestEngineRegressionTests.cpp`), under a category like `regression` or `search_std_linux`.
- **Single responsibility:** Golden data file = expected results only. Test module = drive UI, wait, read, assert. No business logic duplicated in tests.

### Components

- **Dataset:** `tests/data/std-linux-filesystem.txt` — canonical input for index population.
- **Golden data:** `tests/data/std-linux-filesystem-expected.json` (or equivalent) — expected result count (and optional path sample) per test case id.
- **Test module:** New or extended file under `src/ui/` (e.g. `ImGuiTestEngineTests.cpp` or `ImGuiTestEngineRegressionTests.cpp`) — registers regression tests, loads golden data, drives UI, asserts.
- **App bootstrap:** Already supports `--index-from-file`; no change required for loading the dataset. Test runs the app with that CLI arg (test harness launches app with args, or in-process test uses the same Application that was configured with index-from-file by the test runner / main).

### Threading

- All ImGui and test engine code runs on the main thread. Search runs in the existing worker thread; test yields until completion (e.g. poll `state.searchActive` / result count).

### Invariants

- Golden expected counts are produced by the **same** code path that the GUI uses (SearchWorker + FileIndex).
- When the dataset file content or path list changes, golden data must be regenerated and updated in the repo.

---

## Step 4 — Acceptance criteria

| Story | Criterion | Measurable check |
|-------|-----------|------------------|
| S1–S6 | Each regression test passes | With `ENABLE_IMGUI_TEST_ENGINE=ON`, run the regression test suite; each test completes and asserts result count (and optional path) against golden data. |
| S1–S6 | Golden data present and used | Golden file exists under `tests/data/` (or agreed path); tests read it and fail if missing or malformed. |
| S7 | Regeneration procedure documented | Spec or README section describes how to regenerate golden data (e.g. `search_benchmark --index tests/data/std-linux-filesystem.txt --config '<json>'` and record count). |
| — | No new Sonar/clang-tidy violations | Run clang-tidy and Sonar on changed files; fix or align style. |
| — | No regression when test engine OFF | Build with `ENABLE_IMGUI_TEST_ENGINE=OFF`; app and existing tests unchanged. |

---

## Step 5 — Task breakdown

| Phase | Task | Dependencies | Est. (h) | Notes |
|-------|------|--------------|----------|-------|
| 1 | Define the set of regression test cases (ids, search params, description) and document them in the spec and in a test-case table (e.g. in this spec or in `tests/data/std-linux-filesystem-test-cases.md`). | None | 0.5 | See “Test cases and golden data” below. |
| 2 | Compute expected results: run `search_benchmark` (or the app) with `tests/data/std-linux-filesystem.txt` and a config per test case; record result count for each. Produce golden data file (JSON or equivalent) with test_case_id → expected_count (and optional first_path/last_path). | Phase 1 | 0.5 | Requires build with BUILD_TESTS=ON; document commands. |
| 3 | Add golden data file to repo (e.g. `tests/data/std-linux-filesystem-expected.json`) and add a short doc (e.g. in specs or tests) on how to regenerate it. | Phase 2 | 0.25 | |
| 4 | Implement test harness support: ensure the app can be run (or is run by the test runner) with `--index-from-file=tests/data/std-linux-filesystem.txt`. If tests run in-process, ensure Application receives this from bootstrap. | None / existing | 0.25 | May already be satisfied by current bootstrap. |
| 5 | Implement one regression test in the ImGui Test Engine: load golden data, set ref to main window, wait for index ready, set one search (e.g. show-all), trigger search, wait for complete, read result count, assert vs golden. | Phase 3, 4 | 1 | Proves the pipeline. |
| 6 | Implement remaining regression tests (filename, path, extension, combined, folders-only) following the same pattern. | Phase 5 | 1 | Reuse golden load and “wait for search complete” logic. |
| 7 | Run `scripts/build_tests_macos.sh` with `ENABLE_IMGUI_TEST_ENGINE=ON`; run the ImGui Test Engine and execute all regression tests; all pass. | Phase 6 | 0.5 | |
| 8 | Review: no new Sonar/clang-tidy issues; all `#endif` commented; regression tests only compiled when `ENABLE_IMGUI_TEST_ENGINE` is ON. | Phase 7 | 0.25 | |

**Total (rough):** ~4.25 h.

---

## Step 6 — Risks and mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| UI does not expose result count in a way the test engine can read | Tests cannot assert | Define a test-only hook or use programmatic search API from test (e.g. call SearchWorker/FileIndex with same params and compare count); or add a stable “result count” label/ID in the UI for test engine to read. |
| Dataset path differs between dev and CI | Golden data or index path wrong | Use path relative to project root or to test data dir; document in spec; CI runs from project root. |
| Index not ready before first search | Flaky or failing test | Test waits for “index ready” (e.g. poll until search enabled or timeout) before setting inputs and triggering search. |
| Golden data and app semantics drift | False failures or false passes | Document that golden data must be regenerated when dataset or search behavior changes intentionally; add checklist in PR template or spec. |

---

## Step 7 — Validation and handoff

### Review checklist

- [ ] No new SonarQube or clang-tidy violations; preferred style applied.
- [ ] No changes inside existing platform `#ifdef` blocks except where adding test support.
- [ ] All test engine regression code guarded by `#ifdef ENABLE_IMGUI_TEST_ENGINE` with matching `#endif` comments.
- [ ] Golden data file versioned and procedure to regenerate documented.
- [ ] Default build (option OFF) unchanged; `scripts/build_tests_macos.sh` passes with option OFF.
- [ ] With option ON, all regression tests run and pass against golden data.

### Using this spec with Cursor Agent/Composer

1. Use this document as the single source of truth for ImGui Test Engine regression tests (std-linux-filesystem dataset).
2. In the task prompt, reference `AGENTS.md` and `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md` and paste the Strict Constraints block from `AGENT_STRICT_CONSTRAINTS.md`.
3. Implement in the order of the task breakdown (Phase 1 → 8).
4. On macOS, run `scripts/build_tests_macos.sh` after C++ changes. Manually run the app with `ENABLE_IMGUI_TEST_ENGINE=ON` and execute the regression tests from the test engine UI.
5. Verify no new Sonar/clang-tidy issues on changed files.

---

## Test cases and golden data (Task 2)

The following test cases are defined for regression. Expected counts are to be **computed beforehand** by running `search_benchmark` with the dataset and the given JSON config (one run per case), then stored in the golden data file.

**Dataset:** `tests/data/std-linux-filesystem.txt` (one path per line; non-empty lines only; index populated via `--index-from-file`, then `RecomputeAllPaths()`).

**How to produce expected results (run from project root after building with BUILD_TESTS=ON):**

```bash
# Build (e.g. scripts/build_tests_macos.sh); then run search_benchmark with index and config.
# Example (path to benchmark may vary, e.g. build_tests/search_benchmark):
INDEX="tests/data/std-linux-filesystem.txt"

# Case 1: Show all (empty config)
./build_tests/search_benchmark --index "$INDEX" --config '{"version":"1.0","search_config":{}}' --iterations 1 --warmup 0

# Case 2: Filename substring "tty"
./build_tests/search_benchmark --index "$INDEX" --config '{"version":"1.0","search_config":{"filename":"tty"}}' --iterations 1 --warmup 0

# Case 3: Path substring "/dev"
./build_tests/search_benchmark --index "$INDEX" --config '{"version":"1.0","search_config":{"path":"/dev"}}' --iterations 1 --warmup 0

# Case 4: Extension "conf"
./build_tests/search_benchmark --index "$INDEX" --config '{"version":"1.0","search_config":{"extensions":["conf"]}}' --iterations 1 --warmup 0

# Case 5: Path "/etc" + extension "conf"
./build_tests/search_benchmark --index "$INDEX" --config '{"version":"1.0","search_config":{"path":"/etc","extensions":["conf"]}}' --iterations 1 --warmup 0

# Case 6: Folders only
./build_tests/search_benchmark --index "$INDEX" --config '{"version":"1.0","search_config":{"folders_only":true}}' --iterations 1 --warmup 0
```

Record the **result count** printed (e.g. "Found N results" or from the benchmark output) for each case and put them into the golden file.

### Test case table

| Id | Description | Filename | Path | Extensions | Folders only | Case sensitive | Expected count |
|----|-------------|----------|------|------------|--------------|----------------|-----------------|
| show_all | No filters; show entire index | (empty) | (empty) | (none) | false | false | 789531 |
| filename_tty | Filename contains "tty" | tty | (empty) | (none) | false | false | 743 |
| path_dev | Path contains "/dev" | (empty) | /dev | (none) | false | false | 23496 |
| ext_conf | Extension .conf | (empty) | (empty) | ["conf"] | false | false | 683 |
| path_etc_ext_conf | Path /etc and extension .conf | (empty) | /etc | ["conf"] | false | false | 229 |
| folders_only | Directories only | (empty) | (empty) | (none) | true | false | 170335 |

Expected counts were computed by running `search_benchmark` with the dataset and the config per case (see commands in "How to produce expected results" above). Golden data is stored in `tests/data/std-linux-filesystem-expected.json`.

---

## Summary

| Item | Decision |
|------|----------|
| Dataset | `tests/data/std-linux-filesystem.txt`; loaded via `--index-from-file`. |
| Golden data | One expected result count per test case; stored in repo (e.g. `tests/data/std-linux-filesystem-expected.json`). |
| Test cases | show_all, filename_tty, path_dev, ext_conf, path_etc_ext_conf, folders_only. |
| Expected results | Produced by running `search_benchmark` with the dataset and JSON config per case; then frozen. |
| Test implementation | ImGui Test Engine tests in same harness as smoke test; assert result count (and optional path sample) vs golden data. |
| Goal | Catch regressions in search behavior when code or dataset changes. |
