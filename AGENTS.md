# AI Agent Guidelines for USN_WINDOWS Project

## this is a cross platform project
- main development platform is MacOs
- main target is Windows
- secondary target is Linux (Ubuntu)

the application and tests are expected to be built and run on the three platforms: MacOs, Windows, Linux

### Cursor rules (for agents)

Project-specific rules that apply when editing matching files live in **`.cursor/rules/`** (`.mdc` files). If you are unaware of them, read the rule file whose name matches the topic (e.g. Windows/MSVC → `windows-msvc-cpp.mdc`). Current rules:

- **Platform / preprocessor:** `platform-preprocessor-cpp.mdc` — `#endif` comments (every block), do not change code inside platform blocks for “cross-platform” fixes.
- **#define / macros:** `preprocessor-macros-define.mdc` — prefer constexpr over macros; when macros are needed, UPPER_SNAKE_CASE and comment them properly; comment every `#endif`.
- **Windows / MSVC:** `windows-msvc-cpp.mdc` — `(std::min)`/`(std::max)`, strcpy_safe, lambda captures, include order, forward decl, strlen.
- **C++17 init-statement:** `cxx17-init-statement.mdc` — init-statements in `if` when variable only used in that block.
- **C++17 string_view params:** `cxx17-string-view-params.mdc` — use `std::string_view` for read-only string parameters.
- **C++17 style (tool-friendly):** `cxx17-style-tool-friendly.mdc` — braced return, scoped_lock (not lock_guard), explicit single-arg constructors, [[nodiscard]].
- **Const correctness:** `const-correctness-cpp.mdc` — const parameters and member functions.
- **Sonar / code quality:** `sonar-cpp-avoid-new-issues.mdc` — avoid introducing new Sonar issues; see `docs/standards/SONAR_CPP_RULES_REFERENCE.md`.
- **ImGui / popups:** `imgui-ui.mdc` — immediate mode, popup context, SetNextWindowPos, CollapsingHeader, CloseCurrentPopup.
- **CMake:** `cmake-safe.mdc` — mirror patterns, PGO, test targets sharing source with main.
- **.clang-tidy:** `clang-tidy-yaml.mdc` — no inline `#` in Checks or other YAML values.
- **Production (exception/logging):** `production-exception-logging-cpp.mdc` — try-catch, LOG_*_BUILD, `(void)e` in catch.
- **Production (async/future):** `production-async-future-cpp.mdc` — std::future cleanup to avoid leaks.
- **Documentation placement:** `documentation-placement.mdc` — docs/ vs internal-docs/ vs specs/, update DOCUMENTATION_INDEX.md.
- **Results table shortcuts:** `results-table-shortcuts-ui.mdc` — one press = one action; never trigger from IsKeyDown alone; use IsKeyPressed/HandleDebouncedMarkKey.
- **Lock ordering / no I/O under lock:** `lock-ordering-no-io-under-lock.mdc` — no I/O or heavy work inside critical sections; never nest locks across FileIndex/SearchThreadPool/UsnMonitor.
- **TLA+ states and transitions:** `tla-plus-states-assertions.mdc` — when code has states and transitions, apply TLA+-style thinking (states, initial state, transitions, invariants) and implement as assertions in code.
- **DRY / extract helpers:** `dry-extract-helpers.mdc` — avoid duplication; extract helpers and shared constants even when small; one source of truth per concept.
- **Suppressions (NOSONAR/NOLINT):** `suppressions-same-line.mdc` — place on the same line as the flagged code; fix root cause first, suppress only as last resort.
- **Naming conventions:** `naming-conventions-cpp.mdc` — follow CXX17_NAMING_CONVENTIONS.md; PascalCase types, snake_case_ members, kPascalCase constants.
- **ImGui Test Engine preconditions:** `imgui-test-engine-preconditions.mdc` — in regression tests, after each Set* assert getter matches then trigger search; exact window title; ItemExists for optional UI.
- **Assertions (debug builds):** `assertions-debug-builds.mdc` — add assertions for preconditions, loop invariants, postconditions, state transitions; use assert() or NDEBUG.
- **macOS build/tests:** `macos-build-tests.mdc` — on macOS use only `scripts/build_tests_macos.sh`; state whether it was run and passed.
- **GitHub repos:** `gh-public-private-repos.mdc` — publish flow (private USN_WINDOWS → public FindHelper via `scripts/publish.sh`).

Sections below that mention “**Cursor rule:**” point to the corresponding `.mdc` file.

---

## Platform-Specific Code Blocks

When editing C++ with `#ifdef _WIN32` / `__APPLE__` / `__linux__`: do **not** change code inside platform blocks to make it “cross-platform”; refactor to a platform-agnostic abstraction instead. Always add a matching comment after every `#endif` (e.g. `#endif  // _WIN32`, two spaces before `//`). **Cursor rule:** `.cursor/rules/platform-preprocessor-cpp.mdc`.

---

## Linux VM - how to set a new environment
- see instructions in docs/guides/building/BUILDING_ON_LINUX.md

## Windows-Specific Coding Rules

Apply when editing C++ built on Windows: `(std::min)`/`(std::max)`; `strcpy_safe`/`strcat_safe` from StringUtils.h; explicit lambda captures in template functions; include order and lowercase paths (`<windows.h>`); forward decl must match definition (struct/class); safe use of `strlen` (Sonar S1081). **Cursor rule:** `.cursor/rules/windows-msvc-cpp.mdc`. Full text and examples: see that rule and run `python3 scripts/find_class_struct_mismatches.py` for forward-decl checks.

---

### C++17 Init-Statement Pattern

Use `if (init; condition)` when a variable is only used inside that `if` block. **Cursor rule:** `.cursor/rules/cxx17-init-statement.mdc`. Extended examples: `docs/standards/CXX17_INIT_STATEMENT_EXAMPLES.md`.

```cpp
// ❌  int width = atoi(v); if (width >= 640) { ... }
// ✅  if (int width = atoi(v); width >= 640) { ... }
```

**Common patterns:** `if (auto it = map.find(k); it != map.end())`, `if (const char* p = getenv("X"); p)`. **When NOT to Apply:** variable used after the `if`; ImGui slider/input pattern (`&var` in condition, used after); variable needed in catch or after loop.

---

## Modern C++ Practices

For C++17 language and library features see Anthony Calandra's **modern-cpp-features – C++17**: `https://github.com/AnthonyCalandra/modern-cpp-features/blob/master/CPP17.md` (background reading only). Full Problem/Solution/When/Enforcement text and code samples: **`docs/standards/SONAR_CPP_RULES_REFERENCE.md`**.

### Memory Management: Use RAII (no malloc/free/new/delete)

Use `std::vector`, `std::unique_ptr` (with custom deleter for C APIs), or `std::make_shared` instead of manual memory management. Exception-safe; no leaks.

```cpp
// ❌  char* buf = (char*)malloc(n); ... free(buf);
// ✅  std::vector<char> buf(n);
// ✅  auto buf = std::make_unique<int[]>(n);
```

### Const Correctness — **Cursor rule:** `.cursor/rules/const-correctness-cpp.mdc`

Mark non-mutating member functions `const`; use `const T&` / `const T*` for read-only parameters; apply `const` to **all** non-mutating local variables — result IDs, path strings, bool flags, lock guards (`misc-const-correctness`).

### Prefer Braced Initializer List in Return Statements (modernize-return-braced-init-list)

```cpp
// ❌  return std::string(key);  return std::string();
// ✅  return {key};             return {};
```

Use `return { ... }` / `return {}` so the return type is not repeated. clang-tidy `modernize-return-braced-init-list`.

### Use `std::string_view` Instead of `const std::string&`

Use `std::string_view` for read-only string parameters (no allocation; works with string literals, `std::string`, `char*`). clang-tidy `modernize-use-string-view`. Sonar S5350.

- **Do not** store a `std::string_view` from a ternary where one branch is a temporary `std::string` — the view dangles immediately. Use a named `static const std::string` fallback instead.
- **Do not** write `std::string(view)` when the target accepts `std::string_view`; use `.assign(view)` for member assignment and pass `view` directly to `insert_or_assign`/`emplace`.

### Prefer `auto` When Type Is Obvious (modernize-use-auto, S5827)

```cpp
// ❌  std::unordered_map<K,V>::iterator it = map.find(k);
// ✅  auto it = map.find(k);
```

Use `auto` for iterator/find results, cast expressions that name the type, `make_unique`/`make_shared`, and range-for. Keep explicit types when numeric precision matters (`int32_t` vs `int64_t`).

### Handle Unused Parameters (cpp:S1172)

Remove the parameter if the API can change; otherwise use `[[maybe_unused]]` (preferred) or an unnamed parameter `int /*unused*/`.

### Explicit Lambda Captures — **Cursor rule:** `.cursor/rules/windows-msvc-cpp.mdc`

Always use explicit capture lists. Implicit `[&]`/`[=]` in template functions cause MSVC C2062/C2059/C2143 cascading into STL headers. Sonar cpp:S3608.

```cpp
// ❌  [&](size_t i) { ... }                         // implicit — fails MSVC in templates
// ✅  [&soaView, &context, &results](size_t i) { ... }
```

### Specific Exception Handling

Catch specific exception types; do not rely solely on `catch (...)` or `catch (std::exception&)`. Place specific catchers before the catch-all fallback.

```cpp
// ❌  catch (...) {}
// ✅  catch (const std::invalid_argument& e) { ... }
//    catch (const std::exception& e) { ... }
```

### Avoid C-Style Casts

```cpp
// ❌  (int*)void_ptr        // ✅  static_cast<int*>(void_ptr)
```

Use `static_cast`, `const_cast`, `reinterpret_cast`, or `dynamic_cast` to make conversion intent explicit.

### Use `explicit` for Conversion Operators and Single-Argument Constructors (cpp:S1709)

Mark constructors where all but one parameter have defaults as `explicit`. Same for `operator bool()` and all other conversion operators. Prevents accidental implicit conversions.

### Prefer `std::scoped_lock` Over `std::lock_guard` (cpp:S5997, cpp:S6012)

```cpp
// ❌  std::lock_guard<std::mutex> lock(mutex_);
// ✅  std::scoped_lock lock(mutex_);   // CTAD; C++17
```

### Use `[[nodiscard]]` for Important Return Values

Mark functions returning error codes, status, or resource handles `[[nodiscard]]` so silently discarding the return value produces a compiler warning.

### Prefer `std::array` Over C-Style Arrays

`std::array` knows its size, works with STL algorithms, and avoids pointer decay.

```cpp
// ❌  int buf[100];           // ✅  std::array<int, 100> buf;
```

### Remove Redundant Casts (cpp:S1905)

Remove `static_cast<T>(expr)` when `expr` is already of type `T`. Sonar S1905.

### Keep Lambdas Short (cpp:S1188)

Lambdas longer than ~20 lines are hard to read and test. Extract to a named function.

### Use `std::size` for Array Size (cpp:S7127)

`std::size(arr)` instead of `sizeof(arr)/sizeof(arr[0])`.

### Remove Unused Functions (cpp:S1144)

Remove functions that are never called, or call them. Verify added helpers have at least one call site before committing.

### Prefer Private Members; Avoid `protected` Data (cpp:S3656)

Prefer private; use `protected` only when intentionally exposing state to derived classes — document the contract.

### Rule of Five (cpp:S3624)

If you define or delete any of: destructor, copy constructor, copy assignment, move constructor, move assignment — consider all five. Prefer `= default` / `= delete`; use RAII types.

### Use `inline` for File-Scope Variables (cpp:S6018)

```cpp
// ❌  extern int kGlobal;     // ✅  inline constexpr int kGlobal = 42;
```

### Prefer `std::remove_pointer_t` (cpp:S6020)

`std::remove_pointer_t<T>` instead of `typename std::remove_pointer<T>::type`.

### Static Locals Before Loop Bodies (cpp:S3010)

Do not declare `static` local variables inside a loop body; declare them before the loop.

### Suppress `llvm-use-ranges` (C++17 project)

`llvm-use-ranges` suggests `std::ranges::` algorithms (C++20). This project targets C++17. Suppress with:

```cpp
// NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
```

---

## Coding Conventions

### Naming Conventions — **Cursor rule:** `.cursor/rules/naming-conventions-cpp.mdc`

**Follow the naming conventions defined in:** `docs/standards/CXX17_NAMING_CONVENTIONS.md`

**Quick Reference:**
- **Classes/Structs:** `PascalCase` (e.g., `UsnMonitor`, `FileIndex`)
- **Functions/Methods:** `PascalCase` (e.g., `StartMonitoring()`, `GetSize()`)
- **Local Variables:** `snake_case` (e.g., `buffer_size`, `offset`)
- **Member Variables:** `snake_case_` with trailing underscore (e.g., `file_index_`, `reader_thread_`)
- **Global Variables:** `g_snake_case` with `g_` prefix (e.g., `g_file_index`)
- **Constants:** `kPascalCase` with `k` prefix (e.g., `kBufferSize`, `kMaxQueueSize`)
- **Namespaces:** `snake_case` (e.g., `find_helper`, `file_operations`)

**Action:** Before submitting code, verify all identifiers follow these conventions. See `docs/standards/CXX17_NAMING_CONVENTIONS.md` for complete details and examples.

---

## Core Development Principles

### 1. Boy Scout Rule (Leave Code Better Than You Found It)

**Definition:** When modifying code, improve it slightly even if the improvement is unrelated to your immediate task.

**How to Apply:**
- Fix obvious bugs or issues you encounter
- Remove dead code you discover
- Improve variable names for clarity
- Add missing const correctness
- Fix formatting inconsistencies
- Simplify complex expressions
- Add brief comments where logic is non-obvious

**Example:**
```cpp
// Before: You're adding a new feature, but notice this:
void ProcessData(int* data, int size) {
  for (int i = 0; i < size; i++) {  // ❌ Inconsistent style, no const
    // ...
  }
}

// After: Apply Boy Scout Rule while making your change
void ProcessData(const int* data, size_t size) {  // ✅ Added const, better type
  for (size_t i = 0; i < size; ++i) {  // ✅ Consistent style, pre-increment
    // ...
  }
}
```

**Requirement:** When applying the Boy Scout Rule, **explain in your response** what improvements you made and why.

### 2. DRY (Don't Repeat Yourself)

**Principle:** Eliminate code duplication. One source of truth per concept.

**Action:** If you find yourself copying code, extract it into a reusable function, class, or utility.

#### Undesired Duplication of Constants

**Do not** define the same constant (same value or same semantic) in multiple places. That leads to drift when one copy is updated and others are forgotten.

- **Before adding a new constant:** Search the repo for the same numeric value or the same concept (e.g. "max recent searches", "window width max"). If it already exists, reuse it or move it to a shared location and use it everywhere.
- **One constant per purpose:** Storage limit and display limit for the same thing (e.g. recent searches) must use a single constant. Validation bounds (min/max) used in multiple files (e.g. Settings, UI, command-line) must use a single constant.
- **Where to define shared constants:**
  - **App/settings defaults and bounds:** `settings_defaults` in `core/Settings.h` (window size, recrawl interval, recent list size, etc.).
  - **FILETIME / time conversion:** `file_time_constants` in `utils/FileTimeTypes.h` (e.g. Windows vs Unix epoch difference).
  - **File-system / attribute helpers:** `file_system_utils_constants` in `utils/FileSystemUtils.h` where appropriate.
- **Same file:** If the same value is used in more than one place in a single file, define one named constant at file or namespace scope and use it everywhere in that file (e.g. do not repeat `300` for the same debounce in two functions).

See **`docs/analysis/2026-02-20_DRY_CONSTANTS_ANALYSIS.md`** for examples of consolidations and remaining opportunities.

#### Other Types of Duplication to Avoid

- **Magic numbers:** Use named constants instead of repeating the same literal (e.g. `4096`, `60`) in multiple call sites. Search for the value; if it already has a name elsewhere, reuse it.
- **String literals:** UI labels, error messages, tooltips, and help text that say the same thing should not be duplicated with small variations. Reuse a single string or constant, or centralize in one place (e.g. a message or UI constants header) so wording stays in sync.
- **Repeated code blocks:** Identical or near-identical sequences of statements in multiple branches or files should be extracted into a function or shared helper. Same validation logic in multiple places should live in one function and be called from all call sites.
- **Documentation:** Do not repeat the same explanation in multiple files. Prefer one canonical place (e.g. a design doc or the main header) and reference it (e.g. "See AGENTS.md §…" or "See MyModule.h for the contract") instead of copying paragraphs.
- **Type aliases:** When a type is shared across headers, define one canonical alias (e.g. in a central header) and reuse it; do not introduce a second alias for the same type (see "Shared Alias Consistency Across Headers" above).

### 3. Single Responsibility Principle

**Apply at three levels:**

1. **Classes:** Each class should have one clear purpose
2. **Functions:** Each function should do one thing well
3. **Files:** Each file should have a focused, cohesive purpose

**Benefit:** Easier to understand, test, and maintain.

### 4. Performance Priority

**Guideline:** Favor execution speed over memory usage.

**Rationale:** This is a performance-critical Windows application. Optimize for speed unless memory constraints are severe.

### 5. Simplicity Over Complexity

**Guideline:** Favor simple and readable code over clever or complex solutions.

**Test:** If you need to explain complex code with extensive comments, consider simplifying it.

### 6. Add Assertions for Debug Builds — **Cursor rule:** `.cursor/rules/assertions-debug-builds.mdc`

**ALWAYS** add assertions to document and verify invariants:

1. **Preconditions**: Check inputs at function entry.
2. **Loop Invariants**: Assert at top/bottom of loops (e.g., sum parity, bounds).
3. **Postconditions**: Verify outputs/ state changes.
4. **State Transitions**: Guard data mutations.

### 7. Type Aliases for Readable, Consistent Types

Use `using` aliases (`PascalCase`: `UserId`, `ExtensionSet`, `PathOffsetVec`) for complex, nested, or frequently used types. Place shared aliases at namespace scope in the most central header — do not introduce ad-hoc aliases for the same type in other files. When renaming a shared alias, update all usages in the same change.

```cpp
using UserId        = std::uint64_t;
using ExtensionSet  = std::unordered_set<std::string>;
using PathOffsetVec = std::vector<size_t>;
```

---

## Remove Unused Backward Compatibility Code

This project has no external consumers. Remove code marked "kept for backward compatibility" that is not actually called: legacy methods replaced by newer APIs, deprecated flags never read, overloads not used. Verify by searching the codebase before removing. Do not add backward-compatibility shims unless there is an immediate, concrete need.

---

## Code Quality Checklist

When adding or modifying code, evaluate and improve the following aspects:

### Technical Debt
- [ ] Does the new code increase technical debt? If yes, refactor to reduce it.
- [ ] Are there quick wins to reduce existing technical debt in the modified area?

### Code Cleanliness
- [ ] **Dead Code:** Remove unused functions, variables, or commented-out code
- [ ] **Duplication:** Eliminate repeated code patterns (apply DRY principle)
- [ ] **Simplicity:** Can complex logic be simplified without losing functionality?

### Code Quality Dimensions
- [ ] **Readability:** Is the code easy to understand? Improve variable names, add brief comments if needed
- [ ] **Maintainability:** Will future developers be able to modify this easily?
- [ ] **Security:** Are there potential security issues (buffer overflows, unvalidated input, etc.)?
- [ ] **Efficiency:** Can algorithms or data structures be optimized?
- [ ] **Scalability:** Will this code handle larger inputs or more concurrent operations?
- [ ] **Extensibility:** Is the design flexible enough for future requirements?
- [ ] **Reliability:** Are error cases handled? Is the code robust?

### Optimization
- [ ] **Performance:** Can execution speed be improved? (Remember: favor speed over memory)
- [ ] **Memory:** If memory usage is excessive, can it be reduced without sacrificing speed?

**Note:** You don't need to address every item for every change. Focus on improvements that are:
- Relevant to the code you're modifying
- Quick wins that don't derail the main task
- Important for the overall codebase health

---

## Encapsulation

**Principle:** Encapsulate internal implementation details.

**Guidelines:**
- Use `private:` sections for implementation details
- Expose only necessary public interfaces
- Prefer passing data through function parameters over global state
- Use namespaces to organize related functionality

---

## ImGui Immediate Mode Paradigm

ImGui is immediate mode: no widget storage, state from data, all ImGui on main thread. **Popup rules:** OpenPopup and BeginPopupModal in same window context; SetNextWindowPos every frame before BeginPopupModal; close button outside CollapsingHeader with SetItemDefaultFocus; CloseCurrentPopup inside BeginPopupModal block; popup IDs must match. **Cursor rule:** `.cursor/rules/imgui-ui.mdc`. Full paradigm and popup checklist: `docs/design/IMGUI_IMMEDIATE_MODE_PARADIGM.md`; working examples: `ResultsTable.cpp::HandleDeleteKeyAndPopup()`, `Popups.cpp::RenderKeyboardShortcutsPopup()`.

---

### ImGui Test Engine Regression Tests (Precondition Verification) — **Cursor rule:** `.cursor/rules/imgui-test-engine-preconditions.mdc`

Regression tests that drive search via `IRegressionTestHook` (e.g. `RunRegressionTestCase` in `ImGuiTestEngineTests.cpp`) must **verify that settings/params were applied** before triggering the search. Otherwise a test may run with different settings than intended (e.g. null `settings_`, wrong state) and pass or fail for the wrong reason.

**Rule:** After each `Set*` call that affects the next search, assert that the corresponding getter matches what was set, then trigger the search.

- **Load balancing:** After `SetLoadBalancingStrategy(strategy)`, require `GetLoadBalancingStrategy() == strategy` (IM_CHECK) before `TriggerManualSearch()`.
- **Streaming:** After `SetStreamPartialResults(stream)`, require `GetStreamPartialResults() == stream` before triggering search.
- **Search params:** After `SetSearchParams(...)`, require `GetSearchParamFilename()`, `GetSearchParamPath()`, `GetSearchParamExtensions()`, `GetSearchParamFoldersOnly()` to match the values just set before triggering search.

**UI window tests** (Help, Settings, Metrics, Search Syntax): Use exact window title comparison (`std::strcmp(win->Name, kExactTitle) == 0`) and, for optional UI (e.g. Metrics button), a pre-check (e.g. `ItemExists(ref)`) to skip gracefully when the control is not available.

**Reference:** `src/ui/ImGuiTestEngineTests.cpp` (`RunRegressionTestCase`), `src/ui/ImGuiTestEngineRegressionHook.h`.

---

## Workflow Summary

1. **Before:** Understand the existing code structure; check `docs/standards/CXX17_NAMING_CONVENTIONS.md`.
2. **While:** Apply Windows-specific rules; C++17 init-statements; RAII; const correctness; explicit lambda captures; `std::string_view`; Boy Scout Rule; Code Quality Checklist.
3. **After:** Verify naming conventions; explain Boy Scout improvements; on macOS confirm `scripts/build_tests_macos.sh` passed; confirm no build commands were run directly.

---

## macOS Test Requirement (Build-Tests Exception) — **Cursor rule:** `.cursor/rules/macos-build-tests.mdc`

On macOS, any change to C++ source, headers, or CMake **must** be validated by running the cross-platform test suite:

- **Required command (macOS only):**
  - `scripts/build_tests_macos.sh`
- **Scope:** Required when changing:
  - `*.cpp`, `*.c`, `*.h`, `*.hpp`, `*.cxx`, `*.cc`
  - `CMakeLists.txt` or files under `scripts/` that affect builds/tests
- **Exceptions:**
  - Pure documentation-only changes (for example, `*.md` files or comment-only edits) **may** skip this step.
- **Agent note:** When modifying code under macOS, explicitly state in your response whether `scripts/build_tests_macos.sh` was run and whether it passed.

This script is the **only allowed build/test entrypoint on macOS**; do not invoke `cmake`, `make`, `clang++`, or other build tools directly.

---

## Modifying `CMakeLists.txt` Safely

Mirror existing patterns; respect `if(MSVC)`/`if(ENABLE_PGO)` blocks; never change PGO flags when adding files. Test targets that share source with the main executable must use the same PGO compiler flags (see `file_index_search_strategy_tests`). **Cursor rule:** `.cursor/rules/cmake-safe.mdc`.

---

## Modifying `.clang-tidy` Safely

Never put `#` on the same line as a check name (or inside any YAML value)—YAML strips from `#` to EOL, so the check is not disabled. Put rationale in a separate comment block. **Cursor rule:** `.cursor/rules/clang-tidy-yaml.mdc`.

---

## Handling `cppcoreguidelines-pro-bounds-avoid-unchecked-container-access`

This check flags every `container[i]` (`operator[]`) as potentially unsafe because it performs no bounds checking and causes undefined behaviour on out-of-range access. The check is intentionally kept **enabled** in this project to surface real issues; it is **not** disabled globally. Each occurrence must be resolved by one of the following approaches, chosen by context:

### 1. Range-for (preferred refactor)

Use when the loop only needs the value and carries no index arithmetic. Eliminates the warning cleanly and often produces clearer code. This is the default first choice for non-DFA loop bodies.

```cpp
// ❌ flagged
for (size_t i = 0; i < vec.size(); ++i) {
    process(vec[i]);
}

// ✅ range-for — no warning, no overhead
for (const auto& item : vec) {
    process(item);
}
```

### 2. `.at()` (external data boundaries only)

Use **only** where the access reads external or user-supplied data whose structure cannot be verified at compile time and where throwing `std::out_of_range` is an appropriate error response. The canonical case is JSON parsing (`GeminiApiUtils.cpp`). Do **not** use `.at()` in hot search paths — the extra branch and exception mechanism have measurable overhead on millions of iterations.

```cpp
// ✅ appropriate — external JSON data, catching bad API responses
const auto& candidates = response.at("candidates");
const auto& content = candidates.at(0).at("content");
```

### 3. `NOLINT` with justification (performance-critical / proven safe)

Use when:
- The access is inside a hot path (search workers, DFA, SIMD, string search) where the branch overhead of `.at()` is unacceptable.
- The bounds are provably established by surrounding logic (loop condition, prior size check, construction invariant) but cannot be expressed as a range-for because the index is used in arithmetic or multi-array access.

The `NOLINT` comment **must** state why the access is safe:

```cpp
// ✅ NOLINT with invariant documented
const uint16_t next = dfa_table_[state][ch];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - state < states_.size() enforced by DFA construction; ch is uint8_t (0-255)

// ✅ block-level NOLINT for a function where all accesses share the same invariant
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
// All accesses below are guarded by: i < pattern_.size(), established by the loop condition.
... indexed DFA traversal ...
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
```

### Decision summary

| Context | Fix |
|---|---|
| Simple value loop, no index needed | Range-for |
| External / user-supplied data at I/O boundary | `.at()` |
| Hot path (search, DFA, SIMD, regex) with proven bounds | `NOLINT` + justification comment |
| Multi-array indexed access (e.g. `a[i]` and `b[i]`) | `NOLINT` + justification comment |

---

## SonarQube Code Quality Rules

Full rule text and examples are in **`docs/standards/SONAR_CPP_RULES_REFERENCE.md`**. For current open issues and rule distribution run `scripts/fetch_sonar_results.sh --open-only`. The **Quick Reference** table below lists all rules; use the reference doc for Problem/Solution/When to Apply and code samples.

**Two critical patterns to avoid introducing:**

**1. Nesting depth (cpp:S134) — max 3 levels.** Use early returns and extracted functions.

```cpp
// ❌ WRONG - 4 levels
if (a) { if (b) { if (c) { if (d) { work(); } } } }

// ✅ CORRECT - early returns
if (!a) return;
if (!b) return;
if (auto x = get(); !x || !x->valid()) return;
work();
```

**2. Handle exceptions (cpp:S2486) — never empty catch.** Log or rethrow.

```cpp
// ❌ WRONG
} catch (const std::exception& e) {}

// ✅ CORRECT
} catch (const std::exception& e) {
  LOG_ERROR("Failed: " << e.what());
  throw;
}
```

---

## Clang-Tidy and SonarQube Alignment

To avoid fixing the same style issues twice (once for clang-tidy, once for SonarQube), the project states the preferred style once and configures both tools to match where possible.

**Preferred style (satisfy both tools in one fix):**

1. **In-class initializers:** Use in-class initializers for default member values; avoid redundant constructor initializer list entries. (Sonar S3230; clang-tidy: `modernize-use-default-member-init` in `.clang-tidy`.)
2. **Read-only parameters:** Use `const T&` for read-only parameters and variables. (Sonar S995/S5350; clang-tidy: `readability-non-const-parameter`.)
3. **If after brace:** Do not put `if` on the same line as a closing `}`. Put the `if` on a new line or use `else if`. (Sonar S3972; no clang-tidy equivalent — see "If/Else Formatting (cpp:S3972)" below.)

**References:**
- **Sonar vs clang-tidy:** `internal-docs/analysis/2026-02-01_CLANG_TIDY_VS_SONAR_AVOID_DOUBLE_WORK.md`
- **clang-tidy config:** `.clang-tidy` (SonarQube Rule Mapping and CheckOptions)

When cleaning clang-tidy warnings or addressing Sonar issues, apply these styles so one change satisfies both where rules overlap; for Sonar-only rules (e.g. S3972), follow the guideline in AGENTS.md and verify in Sonar UI or scanner.

---

### If/Else Formatting (cpp:S3972)

**Problem:** SonarQube rule S3972 flags `} if (` on the same line: either the `if` should be on a new line or an explicit `else` should be used. There is no clang-tidy equivalent, so this is enforced by Sonar and by this guideline.

**Solution:** Put the `if` on its own line after a closing `}`, or use `else if` when the conditions are related.

```cpp
// ❌ WRONG - } if ( on same line (S3972)
if (bytes < k_bytes_per_kb) {
  return std::to_string(bytes) + " B";
} if (bytes < k_bytes_per_mb) {
  return std::to_string(bytes / k_bytes_per_kb) + " KB";
}

// ✅ CORRECT - if on new line
if (bytes < k_bytes_per_kb) {
  return std::to_string(bytes) + " B";
}
if (bytes < k_bytes_per_mb) {
  return std::to_string(bytes / k_bytes_per_kb) + " KB";
}

// ✅ CORRECT - else if when conditions are related
if (bytes < k_bytes_per_kb) {
  return std::to_string(bytes) + " B";
} else if (bytes < k_bytes_per_mb) {
  return std::to_string(bytes / k_bytes_per_kb) + " KB";
}
```

**When to Apply:** Whenever you have a closing `}` immediately followed by `if` on the same line. Break the line or use `else if`.

**Enforcement:** SonarQube only (no clang-tidy check). Enforce in code review or by running Sonar scanner before push.

## Quick Reference

| Rule | Priority | Action |
|------|----------|--------|
| Don't compile/build | ⚠️ CRITICAL | Never run build commands |
| `(std::min)` / `(std::max)` | ⚠️ REQUIRED | Always use parentheses |
| C++17 init-statements | ⚠️ REQUIRED | Use init-statement when variable only used in if block |
| RAII (no malloc/free/new/delete) | ⚠️ REQUIRED | Use smart pointers and containers |
| Const correctness | ⚠️ REQUIRED | Add const to functions/parameters that don't modify |
| Explicit lambda captures | ⚠️ REQUIRED | Use explicit `[&x, y]` instead of `[&]` or `[=]` (CRITICAL in templates for MSVC) |
| Naming conventions | ⚠️ REQUIRED | Follow `docs/standards/CXX17_NAMING_CONVENTIONS.md` |
| Avoid void* pointers | ⚠️ REQUIRED | Use templates or specific types instead |
| Control nesting depth | ⚠️ REQUIRED | Maximum 3 levels, use early returns |
| Control cognitive complexity | ⚠️ REQUIRED | Maximum 25, break into smaller functions |
| Limit function parameters | ⚠️ REQUIRED | Maximum 7, group into structs |
| Use const references | ⚠️ REQUIRED | `const T&` for read-only parameters |
| Handle unused parameters | ⚠️ REQUIRED | Remove or mark with `[[maybe_unused]]` |
| Complete or remove TODOs | ⚠️ REQUIRED | Don't leave TODO comments |
| Remove commented-out code (S125) | ⚠️ REQUIRED | Remove or justify; use version control to retrieve if needed |
| Member initializer list order (S3229) | ⚠️ REQUIRED | Initializer list order must match member declaration order (C.47) |
| Handle exceptions properly | ⚠️ REQUIRED | Don't catch and ignore exceptions |
| Avoid reinterpret_cast | ⚠️ REQUIRED | Use static_cast, std::bit_cast, or proper design |
| Prefer std::array/vector | ⚠️ REQUIRED | Never use C-style arrays |
| Use in-class initializers | ⚠️ REQUIRED | Prefer in-class initializers over constructor initializer lists for defaults |
| Global variables should be const | ⚠️ REQUIRED | Mark global variables as const when not modified |
| Explicit single-argument constructors | ⚠️ REQUIRED | Mark as `explicit` to prevent implicit conversion (S1709) |
| Prefer std::scoped_lock | ⚠️ REQUIRED | Use `std::scoped_lock` (CTAD) instead of `std::lock_guard<std::mutex>` (S5997/S6012) |
| Include order: all at top | ⚠️ REQUIRED | No #include in middle of file; use lowercase for all includes e.g. `<windows.h>` (S954, S3806) |
| Safe use of strlen (S1081) | ⚠️ REQUIRED | Only on guaranteed null-terminated buffers; prefer .size()/string_view; document with NOSONAR if safe |
| Remove redundant casts (S1905) | ✅ RECOMMENDED | Remove casts that don't change the type |
| Lambda length (S1188) | ✅ RECOMMENDED | Keep lambdas ≤20 lines; extract to named function if longer |
| std::size for arrays (S7127) | ✅ RECOMMENDED | Use std::size(arr) for array/container size |
| Remove unused functions (S1144) | ✅ RECOMMENDED | Remove or use; see Dead Code in checklist |
| Prefer private over protected (S3656) | ✅ RECOMMENDED | Avoid protected data members unless intentional |
| Rule of Five (S3624) | ✅ RECOMMENDED | Custom copy/move/dtor: consider all five |
| Inline variables for globals (S6018) | ✅ RECOMMENDED | Use inline/constexpr for file-scope vars (C++17) |
| std::remove_pointer_t (S6020) | ✅ RECOMMENDED | Prefer over typename std::remove_pointer<T>::type |
| If/else formatting (S3972) | ⚠️ REQUIRED | No `} if (` on one line; put `if` on new line or use `else if` |
| Comment `#endif` directives | ⚠️ REQUIRED | Add matching comment after every `#endif` (e.g. `#endif  // _WIN32`); two spaces before `//` |
| Reduce nested breaks | ✅ RECOMMENDED | Extract functions or use flags/goto |
| Merge nested if statements | ✅ RECOMMENDED | When inner if has no else |
| Simplify boolean expressions | ✅ RECOMMENDED | Use std::any_of/all_of/none_of; extract complex conditions to named vars/functions |
| Use `std::string_view` | ✅ RECOMMENDED | Replace `const std::string&` for read-only params |
| Prefer `auto` | ✅ RECOMMENDED | Use `auto` when type is obvious from initializer (modernize-use-auto, hicpp-use-auto) |
| Return braced init list | ✅ RECOMMENDED | Use `return { ... };` / `return {};` instead of repeating return type (modernize-return-braced-init-list) |
| Specific exception handling | ✅ RECOMMENDED | Catch specific exceptions, not `catch (...)` |
| Avoid C-style casts | ✅ RECOMMENDED | Use `static_cast`, `const_cast`, etc. |
| Popup window context | ⚠️ REQUIRED | OpenPopup and BeginPopupModal must be in same window context |
| Popup SetNextWindowPos | ⚠️ REQUIRED | Call every frame before BeginPopupModal, not just when opening |
| Popup CollapsingHeader | ⚠️ REQUIRED | Close button outside header, use SetItemDefaultFocus |
| Popup CloseCurrentPopup | ⚠️ REQUIRED | Call from within BeginPopupModal block, not after EndPopup |
| Boy Scout Rule | ✅ RECOMMENDED | Improve code you touch |
| DRY principle | ✅ RECOMMENDED | Eliminate duplication; one constant per purpose; extract helpers even small (see §DRY; rule: `dry-extract-helpers.mdc`) |
| Single responsibility | ✅ RECOMMENDED | One purpose per class/function/file |
| Remove unused backward compat | ✅ RECOMMENDED | Remove code kept for compatibility that isn't used |
| **.clang-tidy YAML** | ⚠️ REQUIRED | No inline `#` in Checks (or other YAML values); put rationale in comment blocks |
| Code quality checklist | ✅ RECOMMENDED | Evaluate relevant aspects |

## Questions to Ask Yourself

Before submitting, walk the **Quick Reference** table above and open the linked section for any rule that might apply. For platform, popup, and tooling details see the sections referenced in **Additional Resources**.

## Additional Resources

- **Cursor rules:** Project-specific guidance lives in **`.cursor/rules/`** (`.mdc` files). Each rule applies when editing files matching its globs. See the “Cursor rules (for agents)” subsection near the top of this file for the list of rule files and topics.
- **Naming Conventions:** See `docs/standards/CXX17_NAMING_CONVENTIONS.md` for complete details
- **Project Overview:** See `README.md` for build instructions and project structure
- **Design Decisions:** See `docs/design/ARCHITECTURE_COMPONENT_BASED.md` for architectural overview
- **Production Readiness:** 
  - Production checklist: `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md`
    - Quick section: 5-10 min review for every commit
    - Comprehensive section: Full review for major features/releases

---

## Documentation Placement Rules — **Cursor rule:** `.cursor/rules/documentation-placement.mdc`

When creating new documentation, place it in the correct folder based on audience and purpose.

### `docs/` — external contributors only

Put a document in `docs/` only if an external contributor would need it to build, contribute code, or understand the architecture. All content in `docs/` must be stable enough to be publicly useful.

| Subfolder | What goes here |
|-----------|---------------|
| `docs/design/` | Stable architecture and design documents (e.g. component design, data flow, algorithm design). **Not** in-progress reviews or analysis notes. |
| `docs/guides/` | How-to guides: building on each platform, profiling, running clang-tidy, logging standards. |
| `docs/standards/` | Coding standards enforced on all contributors (naming, multiplatform coherence). |
| `docs/analysis/` | Tool guides applicable to any contributor (clang-tidy guide, check classification). Not dated analysis results. |
| `docs/security/` | Security model description. Not implementation status tracking. |
| `docs/platform/` | Platform-specific build/runtime notes useful to any contributor targeting that platform. |
| `docs/plans/production/` | Quality checklists that contributors should apply before submitting work. |

### `internal-docs/` — maintainer-only

Put a document in `internal-docs/` for anything that is not useful to external contributors: AI agent tools, dated analyses, task notes, implementation plans, and status tracking.

| Subfolder | What goes here |
|-----------|---------------|
| `internal-docs/prompts/` | AI agent prompt templates, review prompts, task prompts, workflow instructions (Taskmaster, AGENT_STRICT_CONSTRAINTS, clang-tidy/Sonar workflows, etc.) |
| `internal-docs/analysis/` | Dated code reviews, performance analyses, improvement backlogs, tooling investigations, Sonar/clang-tidy results |
| `internal-docs/design/` | In-progress design reviews, library upgrade analyses, UI/UX investigation notes |
| `internal-docs/plans/` | Maintainer checklists, open-source publish workflows, roadmap and milestone docs |
| `internal-docs/Historical/` | Completed work, resolved issue analyses |
| `internal-docs/archive/` | Superseded analyses and proposals |

### `specs/` — specification-driven development

Formal specs (using `specs/SPECIFICATION_DRIVEN_DEVELOPMENT_PROMPT.md`) that describe a feature before implementation. These are contributor-accessible but not general contributor docs.

### Decision rule

> **Ask: "Would a first-time external contributor need this to build or contribute?"**
> - Yes → `docs/`
> - No → `internal-docs/`

Dated analysis documents (with a `YYYY-MM-DD_` prefix), task prompts, Sonar/clang-tidy result files, performance opportunity trackers, and AI agent workflow files always go in `internal-docs/`.
