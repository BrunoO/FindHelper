# Exception Handling Improvement Proposal - 2026-02-14

## Context

Based on the [Error Handling Review](ERROR_HANDLING_REVIEW_2026-02-14.md), the codebase has:
- **~40+ `catch (...)` blocks** with NOSONAR(cpp:S2738) or similar
- **Critical**: Swallowed exceptions in bootstrap without sufficient diagnostics
- **High**: Liberal NOSONAR masking technical debt
- **Goal**: Clean, standardized mechanism; log and exit acceptable; minimize NOSONAR

## SonarQube Rules Summary

| Rule   | Concern                          | Mitigation                                      |
|--------|-----------------------------------|-------------------------------------------------|
| S2738  | Generic `catch (...)` discouraged | Centralize in one helper; document necessity     |
| S2486  | Empty catch / catch-and-ignore   | Always log (or document why impossible)         |
| S1181  | Prefer specific exception types  | Catch specific first, `std::exception` then `...`|

## Taxonomy of Exception Contexts

| Context              | Can Log? | Can Exit? | Action                         | NOSONAR Needed? |
|----------------------|----------|------------|--------------------------------|------------------|
| **Fatal (main/bootstrap)** | Yes      | Yes        | Log + exit                     | No (use helper)  |
| **Worker threads**   | Yes      | No         | Log + set error state          | No (use helper)  |
| **Future drain**     | Maybe    | No         | Log if Logger alive, else silent| Yes (edge case)  |
| **Destructor**       | Maybe    | No         | Log if safe, else silent        | Yes (edge case)  |
| **Static destruction**| No      | No         | Silent (Logger destroyed)       | Yes (unavoidable)|
| **Logger internals** | No       | No         | Silent (logging failure)        | Yes (unavoidable)|
| **Test code**        | Yes      | No         | Catch to verify behavior        | Yes (test-only)  |

## Proposed Solution: Centralized Exception Handling Module

### 1. New Header: `src/utils/ExceptionHandling.h`

Cross-platform helpers that encapsulate the full try/catch chain. **All NOSONAR lives in this single file.**

```cpp
#pragma once

#include <exception>
#include <functional>
#include <string_view>

namespace exception_handling {

/**
 * Logs std::exception with context. Cross-platform (no Windows dependency).
 */
void LogException(std::string_view operation, std::string_view context,
                  const std::exception& e);

/**
 * Logs unknown exception with context. Cross-platform.
 */
void LogUnknownException(std::string_view operation, std::string_view context);

/**
 * Executes op(); on exception: logs (std::exception or unknown), runs cleanup, returns exit_code.
 * Use for fatal paths (bootstrap, main). Single NOSONAR in implementation.
 *
 * @return 0 on success, exit_code on exception
 */
int RunFatal(std::string_view context, std::function<int()> op,
             int exit_code = 1, std::function<void()> cleanup = nullptr);

/**
 * Executes op(); on exception: logs, calls on_error(error_message), does NOT rethrow.
 * Use for worker threads / recoverable paths. Single NOSONAR in implementation.
 *
 * @param on_error Called with error string; e.g. collector.SetError(msg)
 */
void RunRecoverable(std::string_view context, std::function<void()> op,
                    std::function<void(std::string_view)> on_error);

/**
 * Drains future.get(); logs if Logger is available. For shutdown/cleanup paths.
 * Use when draining futures during teardown. Documents "log if possible" contract.
 */
template<typename T>
void DrainFuture(std::future<T>& f, std::string_view context);

}  // namespace exception_handling
```

### 2. Implementation: `src/utils/ExceptionHandling.cpp`

- `LogException` / `LogUnknownException`: Use `LOG_ERROR_BUILD` (or `LOG_ERROR`). Move logic from `LoggingUtils` for cross-platform use, or keep `LoggingUtils` Windows-specific and have `ExceptionHandling` call it when available, with a fallback to `std::cerr` for non-Windows.
- **Simpler**: Make `LogException`/`LogUnknownException` use `LOG_ERROR_BUILD` directly. They are simple wrappers. No Windows dependency.
- `RunFatal`: Full try/catch chain (bad_alloc, system_error, runtime_error, std::exception, ...). **One NOSONAR** on the catch-all. Logs, runs cleanup, returns exit_code.
- `RunRecoverable`: Same chain, but calls `on_error(msg)` instead of returning. No rethrow.
- `DrainFuture`: try { f.get(); } catch (std::exception& e) { LogException(...); } catch (...) { LogUnknownException(...); }. If Logger is destroyed (static destruction), we cannot safely call it—document that `DrainFuture` is for normal shutdown only; for static destruction, keep the existing empty catch with NOSONAR.

### 3. Refactoring Plan

| File                     | Current Pattern                    | New Pattern                                      |
|--------------------------|------------------------------------|--------------------------------------------------|
| `main_common.h`          | HandleExceptions + NOSONAR          | Use `RunFatal` (or keep HandleExceptions, have it call shared impl) |
| `AppBootstrap_win.cpp`   | catch + HandleInitializationException | Keep; ensure HandleInitializationException logs + shows message box |
| `main_win.cpp`           | try/catch in main                  | Already logs to cerr; could use RunFatal wrapper  |
| `SearchWorker.cpp`       | Long catch chain + NOSONAR         | `RunRecoverable` with `collector.SetError`        |
| `SearchController.cpp`   | catch + log                        | `RunRecoverable` or `DrainFuture`                 |
| `FolderCrawler.cpp`      | catch + LOG_ERROR_BUILD            | `RunRecoverable` with error_count / MarkFailed   |
| `SearchThreadPool.cpp`   | catch + LOG_ERROR_BUILD            | `RunRecoverable` (or inline, same pattern)        |
| `FolderBrowser.cpp`      | catch + fallback                   | Keep specific catch; use LogException/LogUnknownException |
| `AsyncUtils.h`           | ExecuteWithLogCatch                | Keep; or replace with RunRecoverable            |
| `GuiState.cpp`           | Empty catch for drain              | `DrainFuture` (logs when possible)               |
| `Application.cpp`        | Empty catch for drain              | `DrainFuture`                                    |
| `UsnMonitor.cpp`         | LogException/LogUnknownException   | Already uses logging_utils; migrate to ExceptionHandling |
| `Logger.h`               | Empty catch in logging             | Keep NOSONAR (cannot log from logger)            |
| `LazyAttributeLoader.cpp`| Static destruction                 | Keep NOSONAR (Logger may be destroyed)           |

### 4. Bootstrap / Fatal Path: Log and Exit

Per review: *"Use specific exception types and ensure at least a message box or console output is generated before exiting."*

- **Windows**: `HandleInitializationException` already returns invalid result; caller exits. Ensure it shows a message box (or logs prominently) before returning.
- **main_win.cpp**: Already logs to `std::cerr`. Consider also showing a message box on Windows for GUI builds.
- **main_common.h**: `HandleExceptions` already logs via `LOG_ERROR_BUILD`. Ensure bootstrap failures produce visible output (console or message box).

### 5. NOSONAR Reduction Strategy

1. **Centralize**: All catch-all logic in `ExceptionHandling.cpp`. One `// NOSONAR(cpp:S2738)` in `RunFatal` and one in `RunRecoverable` (and one in `DrainFuture` if we add a catch-all there).
2. **Call sites**: No NOSONAR at call sites; they use the helpers.
3. **Unavoidable**: Logger.h, LazyAttributeLoader static destruction, test helpers—keep NOSONAR with clear comments.
4. **Target**: Reduce from ~40 NOSONAR to ~5–8 (only in truly unavoidable contexts).

### 6. Alternative: Macro for Consistency

If helpers are too heavy for some call sites, a macro can wrap the pattern:

```cpp
#define TRY_LOG_EXIT(context, op, exit_code) \
  exception_handling::RunFatal((context), [&]() { (op); return 0; }, (exit_code))
```

Prefer the function-based API for clarity and testability.

### 7. Migration Order

1. Add `ExceptionHandling.h` and `ExceptionHandling.cpp` with `LogException`, `LogUnknownException`, `RunFatal`, `RunRecoverable`, `DrainFuture`.
2. Migrate `main_common.h::HandleExceptions` to use shared implementation (or replace with `RunFatal`).
3. Migrate `SearchWorker.cpp` to `RunRecoverable`.
4. Migrate `FolderCrawler.cpp`, `SearchThreadPool.cpp`, `FolderBrowser.cpp`, etc.
5. Migrate drain sites to `DrainFuture` where Logger is available.
6. Update AGENTS.md with the new pattern and when to use each helper.

### 8. LoggingUtils Consolidation

- `LogException` and `LogUnknownException` in `LoggingUtils` are Windows-only (inside `#ifdef _WIN32`).
- **Option A**: Move them to `ExceptionHandling` (cross-platform) and have `LoggingUtils` depend on it, or deprecate the exception helpers in `LoggingUtils`.
- **Option B**: Have `ExceptionHandling::LogException` call `logging_utils::LogException` on Windows and use `LOG_ERROR_BUILD` directly on other platforms.

Recommendation: **Option A**—single cross-platform implementation in `ExceptionHandling`.

## Summary

| Before                         | After                                      |
|--------------------------------|--------------------------------------------|
| ~40 NOSONAR across codebase   | ~5–8 NOSONAR (only unavoidable cases)      |
| Inconsistent handling         | `RunFatal`, `RunRecoverable`, `DrainFuture`|
| Swallowed exceptions in bootstrap | Log + message box/console + exit        |
| Ad-hoc catch blocks           | Centralized, documented helpers            |

## Estimated Effort

- **Phase 1** (ExceptionHandling module + main_common): 4 hours  
- **Phase 2** (SearchWorker, FolderCrawler, SearchThreadPool): 4 hours  
- **Phase 3** (remaining production code): 4 hours  
- **Phase 4** (drain sites, tests, AGENTS.md): 2 hours  

**Total**: ~14 hours (within the 24h remediation estimate from the review).
