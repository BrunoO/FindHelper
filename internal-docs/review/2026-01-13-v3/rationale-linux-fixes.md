# Rationale for Linux Build Fixes (2026-01-13-v3)

This document outlines the issues found and fixes applied during the Linux build and test verification process.

## 1. Build Compilation Error in `main_common.h`

### Description of Issue

The initial build on Linux failed with a compilation error in `src/core/main_common.h`. The error was:

```
error: could not convert ‘cleanup’ from ‘const RunApplication<...>::<lambda()>’ to ‘bool’
```

This occurred within the `ExecuteCleanup` function template, which was attempting to check a lambda function as if it were a boolean.

### Root Cause Analysis

The `ExecuteCleanup` function was written with the assumption that the `cleanup` callable would always be a function pointer that could be implicitly converted to `bool` for a null check. However, in the failing case, a lambda was passed, and lambdas do not have this implicit conversion.

### Solution Implemented

The fix involved using `if constexpr` to check at compile time if the `Cleanup` type is convertible to a boolean.

- If it is (as with a function pointer), the original `if (cleanup)` check is used.
- If it is not (as with a lambda), the cleanup function is called directly without a check.

This makes the `ExecuteCleanup` function more robust and able to handle different types of callables.

### Verification Steps

1.  **Applied the fix**: The code in `src/core/main_common.h` was modified.
2.  **Re-ran the build**: The command `cmake --build build --config Release` was executed again. The build succeeded.
3.  **Ran unit tests**: `ctest --output-on-failure` was run, and all tests passed.

## Conclusion

The only issue encountered during the Linux build verification was a single compilation error, which was resolved. The Linux build is now stable and all unit tests pass.
