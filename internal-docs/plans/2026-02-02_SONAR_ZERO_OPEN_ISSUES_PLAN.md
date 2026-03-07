# Plan: Reach 0 Open SonarQube Issues

**Date:** 2026-02-02  
**Source:** SonarQube Cloud (BrunoO_USN_WINDOWS), MCP search  
**Current open issues:** 9

**Execution (2026-02-02):** All 5 phases implemented. Build and tests passed (`scripts/build_tests_macos.sh`). Sonar will reflect 0 open issues after next analysis.

---

## Summary of open issues

| # | Component | Line | Rule | Severity | Message |
|---|-----------|------|------|----------|---------|
| 1 | tests/TestHelpers.h | 1733 | cpp:S134 | CRITICAL | Refactor to not nest more than 3 if/for/do/while/switch |
| 2 | tests/TestHelpers.h | 1736 | cpp:S134 | CRITICAL | Refactor to not nest more than 3 if/for/do/while/switch |
| 3 | tests/TestHelpers.h | 1743 | cpp:S134 | CRITICAL | Refactor to not nest more than 3 if/for/do/while/switch |
| 4 | tests/FileIndexSearchStrategyTests.cpp | 559 | cpp:S1181 | MAJOR | Catch a more specific exception instead of a generic one |
| 5 | src/ui/UIRenderer.cpp | 44 | cpp:S5350 | MINOR | Make `monitor` pointer-to-const |
| 6 | src/ui/UIRenderer.cpp | 45 | cpp:S5350 | MINOR | Make `search_worker` reference-to-const |
| 7 | tests/ParallelSearchEngineTests.cpp | 159 | cpp:S6012 | MINOR | Use CTAD instead of explicit template arguments |
| 8 | src/index/LazyAttributeLoader.cpp | 343 | cpp:S6004 | MINOR | Use init-statement to declare `attrs` inside if |
| 9 | src/index/LazyAttributeLoader.cpp | 369 | cpp:S6004 | MINOR | Use init-statement to declare `attrs` inside if |

---

## Phase 1: CRITICAL – Nesting depth (TestHelpers.h)

**Rule:** cpp:S134 – Maximum 3 levels of nesting.

**Locations:** TestHelpers.h lines 1733, 1736, 1743 (likely same macro or template block).

**Strategy:**
1. Open `tests/TestHelpers.h` around lines 1725–1760 and identify the nested block (likely a macro or template with multiple `if`/`for`/`switch`).
2. Apply AGENTS document guidance: early returns, guard clauses, extracted helper functions, or combined conditions to reduce nesting to ≤ 3.
3. Re-run tests that use TestHelpers to avoid regressions.

**Acceptance:** Sonar no longer reports S134 at 1733, 1736, 1743.

---

## Phase 2: MAJOR – Specific exception (FileIndexSearchStrategyTests.cpp)

**Rule:** cpp:S1181 – Catch a more specific exception.

**Location:** FileIndexSearchStrategyTests.cpp line 559.

**Context:** There is already a `catch (const std::future_error&)` before `catch (const std::exception&)` in one block; line 559 may be a second `catch (const std::exception&)` in the same or another try-block.

**Strategy:**
1. Read the try/catch around line 559.
2. If the code can throw `std::future_error`, `std::runtime_error`, or other known types, add a more specific `catch` before `catch (const std::exception&)`.
3. If the only reasonable type is `std::exception`, consider a focused NOSONAR with a one-line rationale (e.g. “future.get() and task can throw; std::exception is the common base”).

**Acceptance:** Either a more specific catch is added and S1181 is resolved, or a justified NOSONAR is applied and documented here.

---

## Phase 3: MINOR – UIRenderer const (src/ui/UIRenderer.cpp)

**Rule:** cpp:S5350 – Use pointer-to-const / reference-to-const when not modified.

**Locations:** Lines 44 (`monitor`), 45 (`search_worker`).

**Blocker:** These variables are passed to `MetricsWindow::Render(UsnMonitor* monitor, SearchWorker* search_worker, ...)`, which currently takes non-const pointers and uses them in a non-const way (e.g. `monitor->ResetMetrics()`).

**Strategy (to reach 0 issues):**
1. **Option A – API change:** Change `MetricsWindow::Render` (and any overloads) to take `const UsnMonitor*` and `const SearchWorker*`. Then refactor `MetricsWindow.cpp` so that:
   - Read-only use of `monitor`/`search_worker` stays in the const version.
   - Any call that needs `ResetMetrics()` or other non-const operations gets the pointer from the `RenderMainWindowContext` (or similar) and is documented. If `ResetMetrics()` must be called from inside `Render`, the API cannot be const without a larger design change.
2. **Option B – NOSONAR:** If the product design requires `MetricsWindow::Render` to take non-const pointers (e.g. for reset/update behavior), add `// NOSONAR(cpp:S5350)` on the same line as each variable with a short comment: “MetricsWindow::Render requires non-const; see MetricsWindow.h”.

**Recommendation:** Prefer Option A if `ResetMetrics()` can be moved or the API can be split; otherwise Option B to close the finding without changing behavior.

**Acceptance:** Either UIRenderer uses const and MetricsWindow API is updated, or NOSONAR is applied and documented.

---

## Phase 4: MINOR – CTAD (ParallelSearchEngineTests.cpp)

**Rule:** cpp:S6012 – Avoid explicit template arguments; use class template argument deduction (CTAD).

**Location:** ParallelSearchEngineTests.cpp line 159.

**Strategy:**
1. Open line 159 and find the construct with explicit template arguments (e.g. `std::scoped_lock<std::mutex>(...)` or `std::vector<...>(...)`).
2. Replace with CTAD form (e.g. `std::scoped_lock lock(mutex);` or equivalent) so the type is deduced.
3. Ensure the test still compiles and passes.

**Acceptance:** No explicit template arguments at that call site; S6012 closed.

---

## Phase 5: MINOR – Init-statement (LazyAttributeLoader.cpp)

**Rule:** cpp:S6004 – Use init-statement to declare variable inside the if statement.

**Locations:** LazyAttributeLoader.cpp lines 343 and 369 (variable `attrs`).

**Strategy:**
1. Read both sites: pattern is likely `AttrsType attrs = ...; if (condition(attrs)) { ... }` where `attrs` is only used in that if.
2. Refactor to: `if (AttrsType attrs = ...; condition(attrs)) { ... }`.
3. Confirm `attrs` is not used after the if block (AGENTS document: “When NOT to Apply” for init-statement).

**Acceptance:** Both occurrences use an if init-statement; S6004 closed for lines 343 and 369.

---

## Execution order

| Order | Phase | Severity | Effort (rough) |
|-------|--------|----------|----------------|
| 1 | Phase 1 – TestHelpers.h nesting | CRITICAL | Medium |
| 2 | Phase 2 – FileIndexSearchStrategyTests exception | MAJOR | Low |
| 3 | Phase 4 – ParallelSearchEngineTests CTAD | MINOR | Low |
| 4 | Phase 5 – LazyAttributeLoader init-statement | MINOR | Low |
| 5 | Phase 3 – UIRenderer const (API or NOSONAR) | MINOR | Medium |

---

## Verification

After each phase:
1. Run `scripts/build_tests_macos.sh` (or equivalent) and ensure all tests pass.
2. Re-run Sonar analysis (CI or local scanner) and confirm the targeted issues are gone.
3. When all 9 issues are closed, this plan is complete and the repo is at **0 open Sonar issues**.

---

## References

- AGENTS document: C++17 init-statement, const correctness, nesting depth (S134), exception handling (S1181).
- Sonar rules: S134, S1181, S5350, S6012, S6004.
