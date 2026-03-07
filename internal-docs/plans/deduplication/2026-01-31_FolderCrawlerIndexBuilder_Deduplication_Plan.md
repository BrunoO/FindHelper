# FolderCrawlerIndexBuilder.cpp – Deduplication Plan

**Date:** 2026-01-31  
**File:** `src/crawler/FolderCrawlerIndexBuilder.cpp`  
**Goal:** Remove code duplication without introducing Sonar violations, performance penalties, or new duplication. Any `// NOSONAR` must be on the exact line with the issue.

---

## 1. Duplication Identified

### 1.1 “Has crawl folder” check (DRY)

- **Locations:**  
  - `Start()` (line 30): `if (config_.crawl_folder.empty()) { return; }`  
  - `CreateFolderCrawlerIndexBuilder()` (line 136): `if (config.crawl_folder.empty()) { return nullptr; }`
- **Issue:** Same logical condition in two places; changing the rule (e.g. trim/whitespace) would require two edits.
- **Risk if not fixed:** Inconsistent behavior or missed updates when the “has folder” rule changes.

### 1.2 Failure handling: `MarkFailed()` + `SetLastErrorMessage()`

- **Locations:**  
  - `std::exception` catch (lines 91–94): `state->MarkFailed(); state->SetLastErrorMessage(e.what());`  
  - `catch (...)` (lines 95–98): `state->MarkFailed(); state->SetLastErrorMessage("Unknown error...");`
- **Note:** The `!success` branch (lines 83–90) only calls `SetLastErrorMessage` when `!state->cancel_requested`, so it stays as-is.
- **Issue:** Repeated pair of calls in two catch blocks.

### 1.3 Worker thread single exit: `running_.store(false)`

- **Locations:**  
  - Early return when `state == nullptr` (line 54): `running_.store(false, ...); return;`  
  - End of worker lambda (line 103): `running_.store(false, ...);`
- **Issue:** Two places that clear `running_`; easy to miss one when adding paths.
- **Note:** `state->MarkInactive()` is only on the normal path; early return has no state. So we want one place that always does `running_.store(false)`, and one place that does `state->MarkInactive()` when state is non-null.

---

## 2. Refactoring Plan

### 2.1 Single source for “has crawl folder”

- **Action:** Add a helper used in both `Start()` and `CreateFolderCrawlerIndexBuilder()`.
- **Placement:** Anonymous namespace in the same .cpp (no new header).
- **Signature (C++17, string_view):**
  ```cpp
  static bool HasCrawlFolder(const IndexBuilderConfig& config);
  ```
  Implementation: `return !config.crawl_folder.empty();`
- **Use:**
  - In `Start()`: `if (!HasCrawlFolder(config_)) { ... return; }`
  - In `CreateFolderCrawlerIndexBuilder()`: `if (!HasCrawlFolder(config)) { return nullptr; }`
- **Sonar:** No new issues; helper is small and pure.
- **Performance:** Negligible (inline, single predicate).
- **NOSONAR:** None needed.

### 2.2 Helper for “Mark failed with message”

- **Action:** Add a private static helper used in both exception handlers.
- **Placement:** In the same anonymous namespace, before the class or as a static member of `FolderCrawlerIndexBuilder`.
- **Signature:**
  ```cpp
  static void MarkFailedWithMessage(IndexBuildState* state, const char* message);
  ```
  Implementation:
  ```cpp
  state->MarkFailed();
  state->SetLastErrorMessage(message);
  ```
  (`SetLastErrorMessage` takes `const std::string&`; `const char*` is acceptable.)
- **Use:**
  - In `catch (const std::exception& e)`: `MarkFailedWithMessage(state, e.what());` then existing `LOG_ERROR_BUILD(...)`.
  - In `catch (...)`: `MarkFailedWithMessage(state, "Unknown error during folder crawl");` then existing `LOG_ERROR(...)`.
- **Sonar:**  
  - No new cpp:S107 (parameter count stays within limit).  
  - No cpp:S1172 (no unused parameters).  
  - Helper does one thing; no need for NOSONAR if kept small.
- **Performance:** Same work as today; one extra call in hot path is trivial.
- **NOSONAR:** Only if Sonar flags the helper (e.g. “merge with caller”); then add `// NOSONAR(...)` on the specific line Sonar reports, not on a nearby line.

### 2.3 Single exit in worker lambda for `running_` and `MarkInactive`

- **Action:** Restructure the worker so that:
  - `running_.store(false, std::memory_order_release)` is done in exactly one place at the end of the lambda.
  - `state->MarkInactive()` is called only when `state != nullptr`, still before clearing `running_`.
- **Structure (conceptual):**
  ```text
  IndexBuildState* state = shared_state_;
  if (state != nullptr) {
    try {
      // existing crawl logic (crawler, success/failure, exception handling)
    } catch (const std::exception& e) { ... }
    catch (...) { ... }
    state->MarkInactive();
  }
  running_.store(false, std::memory_order_release);
  ```
  So the early return when `state == nullptr` is replaced by skipping the whole `if (state != nullptr) { ... }` block and then falling through to the single `running_.store(false)`.
- **Sonar:**  
  - Avoid introducing deeper nesting (cpp:S134); keep the existing try/catch and branches inside the `if (state != nullptr)` block.  
  - No new cognitive complexity (cpp:S3776); control flow is simplified (one exit for `running_`).
- **Performance:** Unchanged; same operations, reordered for single exit.
- **NOSONAR:** None unless a new finding appears; then add only on the line reported.

---

## 3. What We Are Not Changing

- **`state->MarkFailed()` + conditional `SetLastErrorMessage` in the `!success` branch:**  
  Logic is different (message only when `!cancel_requested`); no shared helper with the exception handlers.
- **State initialization in `Start()` (lines 41–44):**  
  Single block; no duplication. Not using `IndexBuildState::Reset()` here so we don’t clear progress/error message from a previous run.
- **Existing NOSONAR / NOLINT:**  
  Leave them on their current lines; do not move or broaden them. If a new NOSONAR is required, place it exactly on the line Sonar flags.

---

## 4. Implementation Order

1. Add `HasCrawlFolder(const IndexBuilderConfig&)` and switch both call sites to use it.
2. Add `MarkFailedWithMessage(IndexBuildState*, const char*)` and use it in both exception handlers.
3. Restructure the worker lambda to a single exit for `running_.store(false)` and centralize `state->MarkInactive()` under `if (state != nullptr)`.

After each step: build, run tests, and run static analysis (e.g. Sonar/clang-tidy) to ensure no new violations or regressions.

---

## 5. Verification

- **No new Sonar violations:** Run project Sonar/quality profile on `FolderCrawlerIndexBuilder.cpp` after changes.
- **No performance regression:** Same work and same memory ordering; no extra allocations in hot path (helpers are inlined).
- **No new duplication:** Only one definition of “has crawl folder”, one implementation of “mark failed with message”, and one place that sets `running_` to false.
- **NOSONAR placement:** Any new `// NOSONAR(rule)` must be on the same line as the reported issue, per project rules.

---

## 6. File Summary

| Item                         | Before                         | After                                      |
|-----------------------------|---------------------------------|--------------------------------------------|
| “Has crawl folder”          | 2 call sites (raw empty check)  | 1 helper, 2 call sites                      |
| MarkFailed + SetLastErrorMessage | 2 repeated pairs in catches | 1 helper, 2 call sites                      |
| `running_.store(false)`     | 2 code paths                    | 1 code path at end of lambda               |
| New NOSONAR                 | —                               | Only if Sonar flags new code, on that line |

This plan keeps the current behavior and API, removes the identified duplication, and avoids Sonar violations, performance penalties, and new duplication.
