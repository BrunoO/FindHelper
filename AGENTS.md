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

### Windows / Linux
Build and run tests via CMake directly — see `docs/guides/building/`.

### Lint / Analysis
```bash
# clang-tidy (from build directory)
cd build_tests && cmake --build . && cmake --build . --target clang-tidy

# SonarQube open issues
./scripts/fetch_sonar_results.sh --open-only
```

---

## Naming Conventions (from `docs/standards/CXX17_NAMING_CONVENTIONS.md`)

| Kind | Style | Example |
|---|---|---|
| Types (class/struct) | PascalCase | `FileIndex`, `SearchResult` |
| Functions / methods | PascalCase | `GetEntry()`, `StartMonitoring()` |
| Local variables | snake_case | `buffer_size`, `offset` |
| Member variables | snake_case_ (trailing _) | `file_index_`, `reader_thread_` |
| Global variables | g_snake_case (g_ prefix) | `g_file_index` |
| Constants | kPascalCase (k prefix) | `kBufferSize`, `kMaxQueueSize` |
| Namespaces | snake_case | `find_helper`, `file_operations` |

**Rule:** Do not use camelCase anywhere except `GuiState` members (by design exception).

---

## Core C++17 Conventions

### Always
- `(std::min)` / `(std::max)` — never plain `min`/`max` (MSVC ambiguity)
- `[[nodiscard]]` on functions returning error codes / resource handles
- `std::scoped_lock` (CTAD) — never `std::lock_guard<std::mutex>` (S5997/S6012)
- `explicit` on single-arg constructors and `operator bool()` (S1709)
- C++17 init-statement: `if (init; cond)` when variable is block-local only
- `std::string_view` for read-only string parameters (no allocation)
- `return { value };` / `return {};` — braced initializer in return statements
- Explicit lambda captures `[&x, &y]` — never `[&]` or `[=]` in template contexts (MSVC fails)
- Const correctness: `const` on all non-mutating params, locals, and member functions
- Comment every `#endif`: `#endif  // _WIN32`
- No `#include` in the middle of a file; all at top; lowercase paths (`<windows.h>`)

### Never
- `malloc`/`free`/`new`/`delete` — use `std::vector`, `std::unique_ptr`, `std::make_shared`
- C-style casts — use `static_cast`, `const_cast`, `reinterpret_cast`
- Raw `T*` returned from locked internals across threads — use `std::shared_ptr<T>` snapshot
- Implicit `[&]` / `[=]` in lambdas inside template functions
- `void` unused params — use `[[maybe_unused]]` or unnamed `int /*unused*/`

### Avoid (use NOLINT with justification)
- `container[i]` in hot paths — use range-for, or `NOLINT` with invariant documented
- `std::unordered_map<K,V>::iterator` — use `auto`

---

## Platform-Specific Rules

### Windows / MSVC
From `.cursor/rules/windows-msvc-cpp.mdc`:
- `(std::min)` / `(std::max)` — required parentheses
- `strcpy_safe` / `strcat_safe` from `StringUtils.h` — required for fixed buffers
- Include order: headers first (alphabetical), then C stdlib, then system
- Forward declarations must match definition (struct/class)
- Safe `strlen`: only on guaranteed null-terminated buffers; prefer `.size()` / `string_view`

### Cross-platform `#ifdef` blocks
Do **not** change code inside platform blocks to make it "cross-platform". Refactor to a platform-agnostic abstraction. Always add a matching `#endif` comment.

---

## Concurrency & Async Safety (new rules from crash-fix work)

### UI State Write Confinement — `.cursor/rules/ui-state-write-confinement-cpp.mdc`
- `GuiState` display/result containers are UI-thread owned; worker threads must not mutate them.
- Workers write to dedicated staging buffers only (e.g., `pending_sort_sizes_`, `pending_sort_times_`).
- Apply staged updates on the UI thread in one commit phase.
- Cancellation/reset paths must clear staged state to prevent stale-generation apply.

### Async Ownership & Lifetime — `.cursor/rules/async-ownership-lifetime-cpp.mdc`
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

### string_view Pool Remap — `.cursor/rules/string-view-pool-remap-safety-cpp.mdc`
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

## Crash Triage Discipline — `.cursor/rules/crash-triage-discipline.mdc`
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

See `.cursor/rules/imgui-ui.mdc` and `docs/design/IMGUI_IMMEDIATE_MODE_PARADIGM.md`.

---

## Other Cursor Rules (all in `.cursor/rules/*.mdc`)

| Rule | Topic |
|---|---|
| `const-correctness-cpp.mdc` | `const` on params / members / locals |
| `dry-extract-helpers.mdc` | One source of truth; extract small helpers |
| `assertions-debug-builds.mdc` | Preconditions, loop invariants, state transitions |
| `tla-plus-states-assertions.mdc` | States / transitions → assertions |
| `sonar-cpp-avoid-new-issues.mdc` | Avoid new Sonar issues |
| `cxx17-init-statement.mdc` | `if (init; cond)` pattern |
| `cxx17-string-view-params.mdc` | `std::string_view` read-only params |
| `cxx17-style-tool-friendly.mdc` | Braced return, `scoped_lock`, `explicit` |
| `production-exception-logging-cpp.mdc` | try-catch, `LOG_*_BUILD`, `(void)e` |
| `production-async-future-cpp.mdc` | `std::future` cleanup to avoid leaks |
| `documentation-placement.mdc` | `docs/` vs `internal-docs/` vs `specs/` |
| `results-table-shortcuts-ui.mdc` | One press = one action; `IsKeyPressed` |
| `lock-ordering-no-io-under-lock.mdc` | No I/O under lock; lock ordering |
| `clang-tidy-yaml.mdc` | No inline `#` in `.clang-tidy` YAML values |
| `cmake-safe.mdc` | Mirror patterns; PGO; test targets |
| `macos-build-tests.mdc` | Only `scripts/build_tests_macos.sh` on macOS |
| `imgui-test-engine-preconditions.mdc` | Regression test preconditions |
| `suppressions-same-line.mdc` | NOLINT on same line; fix root cause first |
| `naming-conventions-cpp.mdc` | PascalCase / snake_case / kPascalCase |
| `preprocessor-macros-define.mdc` | Prefer `constexpr` over macros |
| `platform-preprocessor-cpp.mdc` | `#endif` comments; no cross-platform hacks in blocks |
| `gh-public-private-repos.mdc` | Publish flow: USN_WINDOWS → FindHelper |

---

## Critical Anti-Patterns

| Rule | Why |
|---|---|
| Max 3-level nesting (cpp:S134) | Deep nesting is unreadable; use early returns |
| Never empty `catch` (cpp:S2486) | Log or rethrow; never swallow silently |
| No `} if (` on same line (cpp:S3972) | Put `if` on new line or use `else if` |
| `container[i]` unchecked | Use range-for, `.at()` (external data), or `NOLINT` with invariant |

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

---

## Boy Scout Rule
When modifying code, leave it slightly better: fix obvious bugs, improve names, add `const`, remove dead code. Explain improvements in your response.
