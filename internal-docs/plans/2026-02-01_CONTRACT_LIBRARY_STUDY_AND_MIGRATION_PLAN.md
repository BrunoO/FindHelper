# Lib.Contract Study, Assertion Review, and Migration Plan

**Date:** 2026-02-01  
**Scope:** Study [alexeiz/contract](https://github.com/alexeiz/contract) (C++17 contract programming), review current assertions, plan migration to proper contracts, and assess whether to adopt or drop the concept.

---

## 1. Lib.Contract Library Summary

### 1.1 What It Is

- **Lib.Contract** is a **header-only** C++17 library for design-by-contract: preconditions, postconditions, and invariants.
- **License:** BSL-1.0 (Boost Software License 1.0) — compatible with the project.
- **Dependencies:** None for consumption. Boost 1.81+ is only required to *build and run the library’s own tests*.
- **Integration:** Copy headers or add as external (submodule/FetchContent); no linking.

### 1.2 Usage

Single include:

```cpp
#include <contract/contract.hpp>
```

Contract block types:

| Block           | Use case                          |
|----------------|-----------------------------------|
| `contract(fun)`  | Free function                     |
| `contract(this)` | Member function                    |
| `contract(ctor)` | Constructor                        |
| `contract(dtor)` | Destructor                         |
| `contract(class)`| Class invariant (one per class)    |
| `contract(derived)(Base...)` | Derived class invariant      |
| `contract(loop)` | Loop invariant                     |

Checks (optional message as second argument):

- `precondition(cond [, message]);` — checked on entry.
- `postcondition(cond [, message]);` — checked on normal exit (not on exception).
- `invariant(cond [, message]);` — entry/exit (or every iteration for `contract(loop)`).

Example (from library README):

```cpp
std::size_t my_strlen(char const* str) {
  std::size_t len = 0;
  contract(fun) {
    precondition(str, "invalid argument");
    postcondition(len == std::strlen(str), "incorrect return value");
  };
  for (char const* p = str; *p; ++len, ++p)
    ;
  return len;
}
```

### 1.3 Disabling Checks (Release Builds)

Compile-time macros (same idea as `NDEBUG` for `assert`):

- `CONTRACT_DISABLE_PRECONDITIONS`
- `CONTRACT_DISABLE_INVARIANTS`
- `CONTRACT_DISABLE_POSTCONDITIONS`

Can be set in CMake for Release (e.g. `add_compile_definitions`) to get zero overhead in production.

### 1.4 Violation Handling

- Default: print to `std::cerr` and call `std::terminate()`.
- Customizable via `contract::set_handler(violation_handler)`.
- Handler can throw (e.g. in tests to catch violations instead of aborting).
- `violation_context` exposes: `contract_type`, `message`, `condition`, `file`, `line`.

### 1.5 Limitations (from README)

- C++17 only; “reasonably modern” compilers.
- Overrides of virtual functions do not inherit base contracts; derived must re-state.

---

## 2. Current Assertion Inventory

### 2.1 Classification

| File | Assertion | Classification | Notes |
|------|-----------|----------------|-------|
| **Application.cpp** | `assert(file_index_ != nullptr)` etc. (5) | **Preconditions** (ctor) | After ctor body; can move into `contract(ctor)`. |
| **StreamingResultsCollector.cpp** | `AddResult`: !search_complete_ | **Precondition** | “Producer must not add after complete.” |
| **StreamingResultsCollector.cpp** | `MarkSearchComplete`: search_complete_ | **Postcondition** | After store. |
| **StreamingResultsCollector.cpp** | `SetError`: has_error_ && search_complete_ | **Postcondition** | After store. |
| **StreamingResultsCollector.cpp** | `GetAllPendingBatches`: pending_batches_.empty() | **Postcondition** | After drain. |
| **StreamingResultsCollector.cpp** | `FlushCurrentBatch`: !pending_batches_.empty() | **Postcondition** | After flush. |
| **SearchController.cpp** | PollResults: (!showingPartialResults \|\| !resultsComplete) | **Invariant** (at entry of block) | State invariant. |
| **SearchController.cpp** | Two blocks: resultsComplete && !showingPartialResults... | **Postconditions** | After state updates. |
| **SearchResultsService.cpp** | !state.resultsComplete && state.showingPartialResults | **Precondition** | Before operation. |
| **SearchWorker.cpp** | collector.IsSearchComplete() | **Postcondition** | After MarkSearchComplete. |
| **SearchResultUtils.cpp** | column_index in [Filename, Extension] | **Precondition** | Bounds (with runtime clamp + LOG_WARNING). |
| **FolderCrawler.cpp** | config_.batch_size > 0, progress_update_interval > 0 | **Preconditions** (ctor) | Config validation. |
| **AppBootstrap_win.cpp** | hwnd != nullptr | **Precondition** | Function/method entry. |
| **SizeFilterUtils.cpp** | false && "Unknown SizeFilter value" | **Unreachable** | Not a contract; keep assert or use std::unreachable. |
| **LightweightCallable.h** | invoke_ != nullptr | **Precondition** (method) | “Callable is empty” before use. |
| **ParallelSearchEngine.h** | `#ifndef NDEBUG` block | Debug-only check | Not contract macro; can stay or wrap. |

### 2.2 What We Do *Not* Change

- **static_assert** (PathPatternMatcher, TimeFilterUtils, LightweightCallable): compile-time; leave as-is.
- **#ifdef NDEBUG** in Logger.h, StatusBar.cpp: behavioral (log level, etc.); unrelated to contract lib.
- **Unreachable:** `SizeFilterUtils.cpp` `assert(false && "Unknown SizeFilter value")` — document as “unreachable”; keep or replace with `std::unreachable` (C++23 not allowed per project; so keep assert or NOLINT).

### 2.3 Counts (Runtime assert Only)

- **Preconditions:** ~12 (Application ctor 5, FolderCrawler ctor 2, StreamingResultsCollector 1, SearchResultsService 1, SearchResultUtils 1, AppBootstrap_win 1, LightweightCallable 1).
- **Postconditions:** ~7 (StreamingResultsCollector 4, SearchController 2, SearchWorker 1).
- **Invariant (state):** 1 (SearchController PollResults).
- **Unreachable:** 1 (SizeFilterUtils).

---

## 3. Migration Plan: Assertions → Contracts

### 3.1 Integration (One-Time)

1. **Add Lib.Contract**
   - Option A: Git submodule `external/contract` (mirror structure: `include/contract/`).
   - Option B: FetchContent in CMake, use `FetchContent_GetProperties(contract)` and add `include/contract` to `target_include_directories` for targets that use contracts.
   - Prefer **Option B** (FetchContent) for no submodule maintenance; pin a tag (e.g. `v0.4.7`).

2. **CMake**
   - For Release builds, add:
     - `CONTRACT_DISABLE_PRECONDITIONS`, `CONTRACT_DISABLE_POSTCONDITIONS`, `CONTRACT_DISABLE_INVARIANTS`
     so contract checks are compiled out (same policy as `assert` + NDEBUG).
   - Only targets that actually include `<contract/contract.hpp>` need the include path and definitions.

3. **AGENTS document**
   - Update “Add Assertions for Debug Builds” to “Add Assertions or Contracts for Debug Builds” and reference this plan: prefer `contract(fun)`/`contract(this)`/`contract(ctor)`/`contract(dtor)` with `precondition`/`postcondition`/`invariant` where they fit; otherwise keep `assert` for one-off checks and unreachable code.

### 3.2 Phase 1: High-Value, Low-Risk (Constructors and Clear Pre/Post)

- **Application.cpp:** Replace the five `assert(...)` in the constructor with one `contract(ctor) { precondition(file_index_ != nullptr); precondition(settings_ != nullptr); ... }`.
- **FolderCrawler.cpp:** Replace two config asserts with `contract(ctor) { precondition(config_.batch_size > 0, "batch size"); precondition(config_.progress_update_interval > 0, "progress interval"); }`.
- **StreamingResultsCollector.cpp:**  
  - `AddResult`: `contract(this) { precondition(!search_complete_.load(...), "AddResult after complete"); }`.  
  - `MarkSearchComplete`: add `contract(this) { postcondition(search_complete_.load(...), "MarkSearchComplete"); }`.  
  - Same idea for `SetError`, `GetAllPendingBatches`, `FlushCurrentBatch` (postconditions).
- **SearchWorker.cpp:** After `MarkSearchComplete()`, replace assert with `contract(this)` and `postcondition(collector.IsSearchComplete(), ...)` in the function that calls it (or the enclosing scope).

Deliverable: Same behavior as today in Debug; in Release, contracts disabled. Run `scripts/build_tests_macos.sh` and fix any compile/lint issues.

### 3.3 Phase 2: Search UI State (SearchController, SearchResultsService)

- **SearchController::PollResults:**  
  - At top of the streaming branch: express “(!state.showingPartialResults || !state.resultsComplete)” as `invariant(...)` inside a `contract(this)` at method entry, or as a precondition for that logical block (if the library is used at block level, otherwise keep as a single `assert` with a comment that it’s an invariant).  
  - Replace the two “state after update” asserts with `postcondition(...)` in the same method.
- **SearchResultsService:** Replace the assert before the operation with `precondition(!state.resultsComplete && state.showingPartialResults, ...)` in the relevant method.

Deliverable: Clearer documentation of state contracts; tests and macOS build pass.

### 3.4 Phase 3: Remaining Call Sites

- **SearchResultUtils.cpp:** `SortSearchResults`: add `contract(fun) { precondition(column_index >= ResultColumn::Filename && column_index <= ResultColumn::Extension, "column index in range"); }`. Keep the runtime clamp + LOG_WARNING for Release.
- **AppBootstrap_win.cpp:** Replace `assert(hwnd != nullptr)` with `precondition(hwnd != nullptr, "hwnd")` in the appropriate function/ctor.
- **LightweightCallable.h:** Replace “callable empty” assert with `precondition(invoke_ != nullptr, "Callable is empty")` in the method that uses it (e.g. `contract(this)`).

Do **not** convert:

- **SizeFilterUtils.cpp:** Keep `assert(false && "Unknown SizeFilter value");` (or document + NOLINT); treat as unreachable, not a contract.

### 3.5 Testing and Violation Handler (Optional)

- In tests, call `contract::set_handler(...)` to install a handler that throws. Then unit tests can expect contract violations where intended (e.g. invalid config, null pointer).
- Not required for migration; can be a follow-up.

---

## 4. Assessment: Keep or Drop

### 4.1 Reasons to **Adopt** Lib.Contract

- **Consistency:** One way to express pre/post/invariant instead of ad-hoc `assert` comments.
- **Semantics:** Postconditions are “on normal exit only”; the library encodes that. Invariants on entry/exit are explicit.
- **Disable per category:** Can turn off preconditions but keep postconditions (or vice versa) in some builds if we ever need that.
- **Testability:** Custom handler allows tests to catch violations instead of abort.
- **Documentation:** `contract(fun) { precondition(...); postcondition(...); }` is self-documenting at the call site.
- **Header-only, no extra link dependency:** Easy to try; BSL-1.0 is fine.

### 4.2 Reasons to **Drop** (or Not Adopt)

- **YAGNI:** Current `assert` usage is small (~20 runtime asserts). We already have a clear policy in AGENTS document. Adding a dependency for a small number of checks may not pay off.
- **Maintenance:** Another external (submodule or FetchContent) to update and to reason about on Windows/macOS/Linux.
- **Learning curve:** Team must learn `contract(scope)` and where pre/post/invariant run (e.g. postcondition not run on exception).
- **Virtual overrides:** If we add contracts to virtuals, derived classes must duplicate them (library limitation).
- **Release behavior:** We already compile out asserts with NDEBUG. With Lib.Contract we’d use CONTRACT_DISABLE_* to get the same “no checks in Release” behavior — equivalent, not better.
- **Risk:** Macro-heavy headers can interact badly with our includes or platform code; we’d need to validate on all three platforms.

### 4.3 Recommendation

- **Short term: Do not add Lib.Contract.**  
  - The project’s assertion count is modest and already categorized (pre/post/invariant) in this document.  
  - AGENTS document already requires assertions for preconditions, loop invariants, postconditions, and state transitions.  
  - Replacing ~20 asserts with contract macros does not, by itself, justify a new dependency and migration cost.

- **Revisit adoption if:**  
  - We introduce many more pre/post/invariants (e.g. in new modules or after a design-by-contract push).  
  - We want standardized violation handling (e.g. logging + terminate) or test-time violation catching across the codebase.  
  - We decide to document APIs with formal contracts and want a single, consistent syntax.

### 4.4 If We Revisit: Minimal Adoption

- Add Lib.Contract via FetchContent (tagged release).  
- Enable only in Debug (or only when a CMake option like `USE_CONTRACTS=ON` is set).  
- Migrate in this order: Application ctor → FolderCrawler ctor → StreamingResultsCollector → SearchController/SearchResultsService → remaining sites.  
- Leave `static_assert`, `#ifdef NDEBUG` behavior, and unreachable `assert(false)` unchanged.  
- Update AGENTS document to prefer contract macros for new code where they fit, and keep this plan as the reference.

---

## 5. Summary

| Item | Conclusion |
|------|------------|
| **Library** | Lib.Contract: header-only, C++17, BSL-1.0; pre/post/invariant; disable via CONTRACT_DISABLE_*. |
| **Current assertions** | ~12 preconditions, ~7 postconditions, ~1 invariant, 1 unreachable; rest static_assert or NDEBUG behavior. |
| **Migration plan** | Phases 1–3: add dependency → constructors and StreamingResultsCollector → Search state → remaining; leave static_assert and unreachable as-is. |
| **Keep or drop** | **Drop for now.** Keep using `assert` and this document as the classification and migration reference; revisit if we scale up contract usage or want unified violation handling. |

---

## References

- [alexeiz/contract](https://github.com/alexeiz/contract) — README and `include/contract/contract.hpp`.
- AGENTS document § “Add Assertions for Debug Builds” (preconditions, loop invariants, postconditions, state transitions).
- This plan: `docs/plans/2026-02-01_CONTRACT_LIBRARY_STUDY_AND_MIGRATION_PLAN.md`.
