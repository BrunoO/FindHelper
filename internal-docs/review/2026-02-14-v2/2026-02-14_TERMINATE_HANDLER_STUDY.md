# Terminate Handler and Platform Hooks Study - 2026-02-14

## Reviewer Request

> To ensure all background tasks use unified exception handling, install terminate/unexpected handlers and any platform hooks as early as possible in main(). This aligns with best practices for reliability.

## Current State

| Platform | main() Location | Exception Handling |
|----------|-----------------|--------------------|
| Windows  | main_win.cpp    | try/catch around RunApplication; logs to std::cerr |
| macOS    | main_mac.mm     | No try/catch; exceptions would propagate to runtime |
| Linux    | main_linux.cpp  | try/catch around RunApplication; logs to std::cerr |

- **No** `std::set_terminate()` handler installed
- **No** platform-specific unhandled exception hooks (e.g. `SetUnhandledExceptionFilter` on Windows)
- **std::unexpected_handler**: Removed in C++17; not applicable

## When std::terminate() Is Called

1. **Unhandled exception in a thread** – C++11+: if an exception escapes a thread’s `std::thread` function, `std::terminate()` is invoked
2. **Exception in destructor during stack unwinding** – throwing from a destructor while another exception is active
3. **std::thread destructor** – when a joinable thread is destroyed without `join()` or `detach()`
4. **noexcept violation** – exception escapes a `noexcept` function
5. **Explicit call** – `std::terminate()` called directly

Our worker threads (SearchWorker, FolderCrawler, UsnMonitor, etc.) use `RunRecoverable` or similar patterns, so unhandled exceptions should be rare. A terminate handler still improves robustness for unexpected cases.

## std::set_terminate() Handler

### Requirements

- Handler type: `void (*)()` (no parameters, no return)
- Must not return; must call `std::abort()` or `std::_Exit()`
- Must not throw
- Cannot reliably use `std::current_exception()` for exception details (handling has already failed)

### Suggested Implementation

```cpp
#include <cstdlib>
#include <exception>
#include <iostream>

namespace {
void TerminateHandler() {
  std::cerr << "Fatal: std::terminate called (unhandled exception or destructor throw)\n";
  std::cerr.flush();
  std::abort();
}
}  // namespace
```

### Placement

Install at the very start of `RunApplication()` (or at the top of each platform `main()` before any other logic):

- Before command-line parsing
- Before Logger initialization (use `std::cerr` only; Logger may not be ready or may be in a bad state)
- Before any threads are created

## Platform-Specific Hooks

### Windows: SetUnhandledExceptionFilter

- Catches **SEH exceptions** (access violations, divide-by-zero, etc.) that are not C++ exceptions
- C++ exceptions are converted to SEH by the runtime; the filter can see them
- Handler can log, write minidump, then return `EXCEPTION_EXECUTE_HANDLER` or `EXCEPTION_CONTINUE_SEARCH`
- **Caveat**: CRT may bypass the filter in some cases (e.g. `abort()`)

### macOS / Linux: Signal Handlers

- `SIGABRT` – often from `std::abort()` or failed assertion
- `SIGSEGV` – segmentation fault
- `SIGFPE` – floating-point exception
- Handlers must be async-signal-safe; avoid complex logic, prefer logging to stderr and `_Exit()`

## Implementation Options

### Option A: Minimal (std::set_terminate only)

- Add `InstallTerminateHandler()` in `ExceptionHandling` or a small `TerminateHandler.cpp`
- Call it at the start of `RunApplication()` in `main_common.h`
- Cross-platform, low risk

### Option B: Full (terminate + platform hooks)

- `std::set_terminate()` as in Option A
- Windows: `SetUnhandledExceptionFilter` in `main_win.cpp` (or a shared init)
- macOS/Linux: `signal(SIGABRT, ...)` and optionally `SIGSEGV`/`SIGFPE` (with care for async-signal-safety)

### Option C: Centralized in RunApplication

- Add `InstallFatalHandlers()` at the top of `RunApplication()` in `main_common.h`
- That function installs terminate handler (and, if desired, platform hooks via `#ifdef`)
- Single call site, consistent across all platforms

## Recommendation

**Phase 1 (low risk):** Implement Option A or C:

1. Add `InstallTerminateHandler()` that calls `std::set_terminate(TerminateHandler)`
2. Use `std::cerr` only in the handler (no Logger)
3. Call it at the very beginning of `RunApplication()`, before any other logic

**Phase 2 (optional):** Add Windows `SetUnhandledExceptionFilter` for SEH and crash reporting; consider signal handlers on Unix only if needed for diagnostics.

## File Changes (Phase 1)

| File | Change |
|------|--------|
| `src/utils/ExceptionHandling.h` | Declare `void InstallTerminateHandler();` |
| `src/utils/ExceptionHandling.cpp` | Implement handler and `InstallTerminateHandler()` |
| `src/core/main_common.h` | Call `InstallTerminateHandler()` at start of `RunApplication()` |

## Considerations

1. **Logger**: Do not use Logger in the terminate handler; use `std::cerr` only.
2. **Order**: Install before any threads or complex initialization.
3. **macOS main**: Currently has no try/catch; adding a terminate handler helps, but consider also wrapping `RunApplication` in try/catch for consistency with Windows/Linux.
4. **Tests**: Test executables may or may not want the same handler; could be conditional or left as default for tests.
