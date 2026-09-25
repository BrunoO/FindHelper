# AGENTS.md — USN_WINDOWS / FindHelper

Cross-platform C++17 project: macOS (dev), Windows (primary target), Linux.

---

## Build & Test Commands

### macOS (only entrypoint — never run cmake/make/clang++ directly)
```bash
# Full test suite
./scripts/build_tests_macos.sh

# Options
./scripts/build_tests_macos.sh --no-asan          # Faster build
./scripts/build_tests_macos.sh --tsan             # Thread sanitizer
./scripts/build_tests_macos.sh --release          # Release build
./scripts/build_tests_macos.sh --run-imgui-tests  # Also run ImGui test engine

# Single test: run the test executable directly from build_tests/
cd build_tests && ./gui_state_tests

# Single doctest case / suite (doctest accepts -tc= and -sc=)
./build_tests/gui_state_tests -tc="RemapSelectionAfterDisplayResultsChange"
./build_tests/string_search_tests -sc="StringSearch*"     # Suite wildcard
```

### Adding a new test target
1. Define the `add_executable` / `add_test` block in `CMakeLists.txt` (copy an existing block).
2. Add the target name to **`scripts/test_targets.txt`** (one name per line) — this is the single source of truth consumed by CMakeLists.txt (`file(STRINGS ...)`), `build_tests_macos.sh`, and `generate_coverage_macos.sh`. No other files need editing.

### Test Infrastructure Suppressions
`tests/.clang-tidy` suppresses false-positive checks that doctest makes unavoidable across the entire test directory (`cert-err33-c`, `readability-function-cognitive-complexity`, `readability-magic-numbers`, `readability-identifier-naming`). Do **not** add per-line `// NOLINT(cert-err33-c)` in test files; the directory config already handles it.

### Windows / Linux
Build and run tests via CMake directly — see `docs/guides/building/`.

### Lint / Analysis
```bash
# clang-tidy (from build directory)
cd build_tests && cmake --build . && cmake --build . --target clang-tidy

# clang-tidy on changed files (pre-commit hook entrypoint)
scripts/pre-commit-clang-tidy.sh

# Full clang-tidy run across project
scripts/run_clang_tidy.sh

# Detect class/struct forward-declaration mismatches (MSVC C4099)
python3 scripts/find_class_struct_mismatches.py

# SonarQube open issues
./scripts/fetch_sonar_results.sh --open-only

# Sonar full scan
scripts/run_sonar_scanner.sh

# Code coverage (macOS)
scripts/run_full_coverage_macos.sh
```

---

## Naming Conventions (from `docs/standards/CXX17_NAMING_CONVENTIONS.md`)

| Kind | Style | Example |
|---|---|---|
| Types (class/struct) | PascalCase | `FileIndex`, `SearchResult` |
| Functions / methods | PascalCase | `GetEntry()`, `StartMonitoring()` |
| Local variables | snake_case | `buffer_size`, `offset` |
| Class member variables | snake_case_ (trailing _) | `file_index_`, `reader_thread_` |
| Struct/POD public fields | snake_case (no trailing _) | `show_help`, `thread_count` |
| Global variables | g_snake_case (g_ prefix) | `g_file_index` |
| Constants | kPascalCase (k prefix) | `kBufferSize`, `kMaxQueueSize` |
| Namespaces | snake_case | `find_helper`, `file_operations` |

**Rule:** Class data members (private/protected) use `snake_case_`. Public fields in plain structs, PODs, and context objects omit the trailing underscore (`snake_case`). Do not use camelCase anywhere except `GuiState` members (by design exception).

**Ubiquitous language (USN/MFT):** `docs/design/USN_MFT_UBIQUITOUS_LANGUAGE.md` is the source of truth for domain terms (`IndexedFile`, `NtfsFileReference`, `JournalCursor`, `UsnRecord`, `UsnReason`, `UnresolvedReference`, …). New USN/MFT code must use those terms; do not introduce synonyms — extend the glossary instead.

---

## Core C++17 Conventions

### Always
- `(std::min)` / `(std::max)` — never plain `min`/`max` (MSVC ambiguity)
- `[[nodiscard]]` on functions returning error codes / resource handles
- `std::scoped_lock` (CTAD) — never `std::lock_guard<std::mutex>` (S5997/S6012)
- `explicit` on single-arg constructors and `operator bool()` (S1709)
- C++17 init-statement: `if (init; cond)` when variable is block-local only
- `std::string_view` for read-only string parameters (no allocation)
- `std::string_view` member assignment: use `.assign(view)` or pass `view` directly to `insert_or_assign`/`emplace` instead of explicit `std::string(view)` when target accepts string_view
- `return { value };` / `return {};` — braced initializer in return statements
- Explicit lambda captures `[&x, &y]` — never `[&]` or `[=]` in template contexts (MSVC fails)
- Const correctness: `const` on all non-mutating params, locals, and member functions
- Comment every `#endif`: `#endif  // _WIN32`
- No `#include` in the middle of a file; all at top; lowercase paths (`<windows.h>`)
- Include ordering: sorted alphabetically by ASCII order (`'A'–'Z'` before `'a'–'z'`, e.g., `"TestHelpers.h"` sorts before `"crawler/..."` because `'T'` < `'c'`)

### Never
- `malloc`/`free`/`new`/`delete` — use `std::vector`, `std::unique_ptr`, `std::make_shared`
- C-style casts — use `static_cast`, `const_cast`, `reinterpret_cast`
- Raw `T*` returned from locked internals across threads — use `std::shared_ptr<T>` snapshot
- Implicit `[&]` / `[=]` in lambdas inside template functions
- `void` unused params — use `[[maybe_unused]]` or unnamed `int /*unused*/`
- Dangling `std::string_view` from ternary: never create a view where one branch returns a temporary `std::string`; use a named `static` / `static thread_local` fallback string
- Declare `static` local variables inside loop bodies (SonarQube S3010); declare them before the loop

### Avoid (use NOLINT with justification)
- `container[i]` in hot paths — use range-for, or `NOLINT` with invariant documented
- `std::unordered_map<K,V>::iterator` — use `auto`
- `llvm-use-ranges`: clang-tidy suggests `std::ranges` (C++20), but this project targets C++17 — suppress with `// NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20`

---

## Platform-Specific Rules

### Windows / MSVC
From `.agents/rules/windows-msvc-cpp.md`:
- `(std::min)` / `(std::max)` — required parentheses
- `strcpy_safe` / `strcat_safe` from `StringUtils.h` — required for fixed buffers
- Include order: headers first (alphabetical), then C stdlib, then system
- Forward declarations must match definition (struct/class)
- Safe `strlen`: only on guaranteed null-terminated buffers; prefer `.size()` / `string_view`

### Cross-platform `#ifdef` blocks
Do **not** change code inside platform blocks to make it "cross-platform". Refactor to a platform-agnostic abstraction. Always add a matching `#endif` comment.

---

## Concurrency & Async Safety (new rules from crash-fix work)

### UI State Write Confinement — `.agents/rules/ui-state-write-confinement-cpp.md`
- `GuiState` display/result containers are UI-thread owned; worker threads must not mutate them.
- Workers write to dedicated staging buffers only (e.g., `pending_sort_sizes_`, `pending_sort_times_`).
- Apply staged updates on the UI thread in one commit phase.
- Cancellation/reset paths must clear staged state to prevent stale-generation apply.
- `GuiState` is decomposed into single-writer substates (`SearchCriteria`,
  `ResultFilterCaches`, `SearchPipelineState`, `SelectionState`,
  `ExportWorkflowState`, `GeminiWorkflowState`, `HistoryInteractionState`,
  `AsyncSortState`, `IndexBuildProgressState`, `ResultPoolOwner`,
  `CloudFileWorkflowState`); the FIELD
  OWNERSHIP MAP at the top of `src/gui/GuiState.h` is the source of truth —
  new fields must extend a substate, never be added flat to `GuiState`.

### Async Ownership & Lifetime — `.agents/rules/async-ownership-lifetime-cpp.md`
- Never return raw `T*` from a reset-able object across threads.
- Use `std::shared_ptr<T>` for cross-thread handoff (captured under lock, not just the pointer).

```cpp
// ❌ Unsafe
T* Get() { std::scoped_lock l(m_); return ptr_.get(); }

// ✅ Safe
std::shared_ptr<T> Get() { std::scoped_lock l(m_); return ptr_; }
```

### Async Context Struct — lambda capture must be by value
When a struct bundles context for thread-pool task lambdas (e.g., `SortAttributeEnqueueContext`), the struct is typically a stack-local in the submitting function and is destroyed when that function returns — before the tasks execute. **Always capture the struct by value** in the lambda, never by reference.

```cpp
// ❌ Dangling: ctx is a stack-local in the caller; destroyed before task runs
thread_pool.submit([&ctx] { ctx.token->IsCancelled(); });

// ✅ Safe: lambda owns a copy; raw pointers inside point to long-lived objects
thread_pool.submit([ctx] { ctx.token->IsCancelled(); });
```

If the struct contains a `shared_ptr`, capturing by value increments the ref-count correctly. Raw pointer members are safe as long as their targets (state members, application-lifetime singletons) outlive all tasks — document this invariant in the struct definition.

### string_view Pool Remap — `.agents/rules/string-view-pool-remap-safety-cpp.md`
- Never return `std::string_view` into mutable/locked internal storage without guaranteed lifetime.
- Prefer copy-return (`std::string`) for cross-thread error/status access.
- During pool remap: validate pointer range before offset math; log and clear on failure.

---

## Key Patterns

### SearchResult path pool lifecycle
`searchResultPathPool` is a `std::vector<char>` of null-terminated paths. `SearchResult.fullPath` is a `std::string_view` into this pool. **Rule:** clear the results vector BEFORE clearing the pool. Use `ClearResultPool(state)` helper.

### Batch number protocol
`resultsBatchNumber` is the change-detection counter for `IncrementalSearchState`. It must be incremented on every result replacement so `CheckBatchNumber()` invalidates cached filter copies that hold stale `fullPath` views. All result-replacement sites must go through `ClearResultPool()` or increment explicitly.

### Sort cancellation
Before replacing results or starting a new sort, use `WaitForAllAttributeLoadingFutures(state)` — it cancels the token, spin-waits until the task counter reaches zero, resets staging buffers, and resets the counter. **Do not call `CleanupAttributeLoadingFutures(state, false)` before this**: it drops the counter shared_ptr early, making the spin-wait a no-op and allowing tasks still in I/O to write into the result buffer that is being replaced (use-after-free). For auto-refresh (which cannot block on a full drain immediately), only call `state.sort_cancellation_token_.Cancel()` — leave the counter alive so the later `WaitForAllAttributeLoadingFutures` call at finalization can drain properly. The token is reused (not replaced); generation checks in `ApplyPendingSortAttributeUpdates` guard against stale writes.

---

## Crash Triage Discipline — `.agents/rules/crash-triage-discipline.md`
- Each commit: one hypothesis, one concrete risk.
- Prefer minimal targeted fixes before broad refactors.
- Commit message: fault model + safety mechanism added.
- After each fix, record outcome (repro still fails/passes, dump change, new evidence).

---

## ImGui / Immediate Mode
ImGui is immediate mode: no widget storage, state from data, all ImGui on main thread. Popup rules:
- `OpenPopup` + `BeginPopupModal` in same window context
- `SetNextWindowPos` every frame before `BeginPopupModal`
- `CloseCurrentPopup` inside `BeginPopupModal` block; close button outside `CollapsingHeader`

See `.agents/rules/imgui-ui.md` and `docs/design/IMGUI_IMMEDIATE_MODE_PARADIGM.md`.

---

## Other Project Rules (all in `.agents/rules/*.md`)

| Rule | Topic |
|---|---|
| `const-correctness-cpp.md` | `const` on params / members / locals |
| `dry-extract-helpers.md` | One source of truth; extract small helpers |
| `assertions-debug-builds.md` | Preconditions, loop invariants, state transitions |
| `tla-plus-states-assertions.md` | States / transitions → assertions |
| `sonar-cpp-avoid-new-issues.md` | Avoid new Sonar issues |
| `cxx17-init-statement.md` | `if (init; cond)` pattern |
| `cxx17-string-view-params.md` | `std::string_view` read-only params |
| `cxx17-style-tool-friendly.md` | Braced return, `scoped_lock`, `explicit` |
| `production-exception-logging-cpp.md` | try-catch, `LOG_*_BUILD`, `(void)e` |
| `production-async-future-cpp.md` | `std::future` cleanup to avoid leaks |
| `documentation-placement.md` | `docs/` vs `internal-docs/` vs `specs/` |
| `results-table-shortcuts-ui.md` | One press = one action; `IsKeyPressed` |
| `lock-ordering-no-io-under-lock.md` | No I/O under lock; lock ordering |
| `clang-tidy-yaml.md` | No inline `#` in `.clang-tidy` YAML values |
| `cmake-safe.md` | Mirror patterns; PGO; test targets |
| `macos-build-tests.md` | Only `scripts/build_tests_macos.sh` on macOS |
| `imgui-test-engine-preconditions.md` | Regression test preconditions |
| `suppressions-same-line.md` | NOLINT on same line; fix root cause first |
| `naming-conventions-cpp.md` | PascalCase / snake_case / kPascalCase |
| `preprocessor-macros-define.md` | Prefer `constexpr` over macros |
| `platform-preprocessor-cpp.md` | `#endif` comments; no cross-platform hacks in blocks |
| `gh-public-private-repos.md` | Publish flow: USN_WINDOWS → FindHelper |

---

## Critical Anti-Patterns

| Rule | Why |
|---|---|
| Max 3-level nesting (cpp:S134) | Deep nesting is unreadable; use early returns |
| Never empty `catch` (cpp:S2486) | Log or rethrow; never swallow silently |
| No `} if (` on same line (cpp:S3972) | Put `if` on new line or use `else if` |
| `container[i]` unchecked | Use range-for, `.at()` (external data), or `NOLINT` with invariant |
| Static local in loop body (cpp:S3010) | Confusing lifetime/re-entry; declare static variable before loop |

Full SonarQube reference: `docs/standards/SONAR_CPP_RULES_REFERENCE.md`.

---

## DRY — Constants

Do not define the same constant in multiple places. One source of truth:
- App/settings defaults/bounds → `settings_defaults` in `core/Settings.h`
- FILETIME / time constants → `file_time_constants` in `utils/FileTimeTypes.h`
- File-system helpers → `file_system_utils_constants` in `utils/FileSystemUtils.h`

---

## Documentation Placement

| Folder | What goes here |
|---|---|
| `docs/` | External contributor docs: build guides, architecture, coding standards |
| `internal-docs/` | Maintainer-only: dated analyses, AI agent tools, task plans |
| `specs/` | Formal specs (pre-implementation feature descriptions) |

**Decision rule:** Would a first-time external contributor need this to build or contribute? Yes → `docs/`. No → `internal-docs/`.

### AI Prompt Templates & Workflows
- `internal-docs/prompts/TaskmasterPrompt.md` — turn a goal into a concrete task
- `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md` — quality guardrails to paste into every task
- `internal-docs/prompts/AGENT_IMPACT_ANALYSIS_REGRESSION_PREVENTION.md` — regression prevention checklist
- `specs/SPECIFICATION_DRIVEN_DEVELOPMENT_PROMPT.md` — write a spec before implementing
- `docs/DOCUMENTATION_INDEX.md` — full index of public and internal documentation

---

## Boy Scout Rule
When modifying code, leave it slightly better: fix obvious bugs, improve names, add `const`, remove dead code. Explain improvements in your response.
