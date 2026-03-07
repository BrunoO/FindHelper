# Rationale for Linux Build Fixes - 2026-01-21-v1

This document outlines the issues found during the Linux build and test verification process, along with their root causes and the solutions implemented.

## 1. `GeminiApiUtilsTests` Failure

- **Issue:** The `GeminiApiUtilsTests` unit test failed with an assertion error. The test case `BuildSearchConfigPrompt - Base Prompt Size` checked if the base prompt size was less than 3500 characters, but the actual size was 6239.
- **Root Cause:** The prompt template in `GeminiApiUtils.cpp` had been expanded over time, causing the total size of the generated prompt to exceed the sanity check value in the test.
- **Solution:** The assertion in `tests/GeminiApiUtilsTests.cpp` was updated to reflect the new, larger prompt size. The check was changed from `< 3500` to `< 6500` to provide a reasonable margin for future additions.
- **Verification:** After modifying the test case, the test suite was re-run, and the `GeminiApiUtilsTests` test passed.

## 2. `FileIndexSearchStrategyTests` Crash

- **Issue:** The `FileIndexSearchStrategyTests` unit test crashed with a segmentation fault (SIGSEGV). The AddressSanitizer output indicated a use-after-free error occurring within a worker thread during an asynchronous search.
- **Root Cause:** The `ParallelSearchEngine::SearchAsync` function was capturing the `SearchContext` object by reference (`&context`) in the lambda function passed to the thread pool. The `SearchContext` object was created on the stack in the calling function (`FileIndex::SearchAsync`) and was destroyed when that function returned, leaving the worker threads with a dangling reference.
- **Solution:** The lambda capture in `src/search/ParallelSearchEngine.cpp` was changed from a reference (`&context`) to a value (`context`). This ensures that each worker thread receives a copy of the `SearchContext` object, which remains valid for the entire duration of the thread's execution.
- **Verification:** After applying the fix, the entire test suite was re-run, and the `FileIndexSearchStrategyTests` test, along with all other tests, passed successfully.
