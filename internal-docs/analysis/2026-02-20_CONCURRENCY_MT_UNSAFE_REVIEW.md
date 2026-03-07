# 2026-02-20 Concurrency-mt-unsafe: review and decisions

**Context:** clang-tidy reports 6 `concurrency-mt-unsafe` issues. These flag use of APIs that are not guaranteed thread-safe (e.g. `readdir`, `getenv`).

---

## 1. FolderCrawler.cpp:570 – `readdir(dir)`

**Cause:** `readdir()` is not thread-safe when the *same* `DIR*` is used from multiple threads (POSIX allows a shared static buffer per stream).

**Usage:** `EnumerateDirectory()` uses one `DIR*` per call (`opendir` → `readdir` loop → `closedir`). It is called from worker threads, but each invocation has its own directory stream; streams are not shared.

**Decision:** **NOLINT** – Acceptable. No shared `DIR*`; concurrent calls use different streams. The checker does not prove per-stream isolation.

---

## 2. GeminiApiUtils.cpp:117 – `std::getenv("GEMINI_API_KEY")`

**Cause:** `getenv()` is not thread-safe (returns pointer to internal storage; `setenv()` in another thread can invalidate it).

**Usage:** Used to read API key for Gemini; result is copied into a `std::string` immediately. Typically called at config/startup or from UI thread, not from a hot multi-threaded path.

**Decision:** **NOLINT** – Acceptable. Result is copied at once; we do not call `setenv()` in our code. Risk is limited to external process/env changes.

---

## 3–4. FileOperations_linux.cpp:241, 246 – `getenv("XDG_DATA_HOME")`, `getenv("HOME")`

**Cause:** Same as above: `getenv()` is not thread-safe.

**Usage:** Used in the Linux “move to trash” path to resolve trash location. Typically invoked from UI/main thread in response to user action.

**Decision:** **NOLINT** – Acceptable. Configuration-style read; result used immediately to build paths. No `setenv()` in our code on this path.

---

## 5–6. Logger.h:393, 398 – `getenv("XDG_CACHE_HOME")`, `getenv("HOME")`

**Cause:** Same: `getenv()` not thread-safe.

**Usage:** `GetDefaultLogDirectory()` is only called from `TryOpenLogFile()`, which is only called while holding `mutex_` (see `Log()` at line 178: `scoped_lock` then `TryOpenLogFile()`). So only one thread at a time can call `GetDefaultLogDirectory()`.

**Decision:** **NOLINT** – Acceptable. Calls are serialized by the logger’s mutex; result is copied into `std::string` immediately. Optional improvement for clarity/future-proofing: cache the directory in a function-local static with `std::call_once`.

---

## Summary

| Location                    | API       | Decision | Reason |
|----------------------------|-----------|----------|--------|
| FolderCrawler.cpp:570      | readdir   | NOLINT   | Per-call `DIR*`, no shared stream |
| GeminiApiUtils.cpp:117    | getenv    | NOLINT   | Result copied immediately; config-style read |
| FileOperations_linux.cpp:241 | getenv  | NOLINT   | Trash path config; result used immediately |
| FileOperations_linux.cpp:246 | getenv  | NOLINT   | Same |
| Logger.h:393               | getenv    | NOLINT   | Called only under `mutex_`; result copied |
| Logger.h:398               | getenv    | NOLINT   | Same |

All six are suppressed with **NOLINT** on the same line as the flagged call, with a short comment. No functional change; risk of real concurrency bugs from these uses is low.
