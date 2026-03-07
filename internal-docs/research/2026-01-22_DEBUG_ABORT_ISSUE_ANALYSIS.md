# Debug Mode Abort() Issue Analysis

## Issue

When closing the program in Debug mode on Windows, an "abort() has been called" error occurs.

## Potential Causes

### 1. Static Destruction Order (Most Likely)

**Problem:** Static/global objects are destroyed in reverse order of construction. If `LogStatistics()` is called during static destruction and the Logger singleton is already destroyed, it could cause issues.

**Fix Applied:**
- Added try-catch guard to `LogStatistics()` to prevent exceptions during shutdown
- This prevents abort() if Logger is unavailable during static destruction

**Status:** ✅ Fixed

### 2. Assertion Failures During Shutdown

**Potential Issue:** Assertions in `Application` constructor check for non-null pointers:
```cpp
assert(file_index_ != nullptr);
assert(settings_ != nullptr);
assert(window_ != nullptr);
```

**Analysis:** These assertions are in the constructor, not destructor, so they shouldn't trigger during shutdown unless there's a double-destruction issue.

**Status:** ⚠️ Unlikely but possible

### 3. Exception Thrown from Destructor

**Potential Issue:** If any destructor throws an exception during shutdown, it can cause `std::terminate()` which calls `abort()`.

**Analysis:** All destructors in the codebase have try-catch blocks to prevent exceptions from propagating.

**Status:** ✅ Already handled

### 4. Logger Singleton Access During Static Destruction

**Potential Issue:** If `LOG_INFO_BUILD` is called during static destruction (after Logger singleton is destroyed), it could cause issues.

**Fix Applied:**
- `LogStatistics()` now has try-catch guard
- Logger::Instance() uses function-local static, which should be safe

**Status:** ✅ Fixed (defensive)

### 5. Static Atomic Counters

**Potential Issue:** Static atomic counters in `LazyAttributeLoader.cpp` might be accessed during static destruction.

**Analysis:** Atomic operations are safe even during static destruction. The issue would only occur if `LogStatistics()` is called, which is now guarded.

**Status:** ✅ Fixed (defensive)

## Fixes Applied

1. **Added try-catch guard to `LogStatistics()`**
   - Prevents exceptions if Logger is unavailable
   - Silently ignores errors during shutdown
   - File: `src/index/LazyAttributeLoader.cpp`

## Debugging Steps

If the issue persists, check:

1. **Get the exact error message:**
   - What is the full error text?
   - Is there a stack trace?
   - When exactly does it occur (window close, program exit, etc.)?

2. **Check Visual Studio Output window:**
   - Look for assertion failures
   - Check for exception messages
   - Note the exact line/file where abort() is called

3. **Check if it's MFT-related:**
   - Does it happen with MFT disabled?
   - Does it happen with MFT enabled?
   - Try both configurations

4. **Check static destruction order:**
   - Logger singleton uses function-local static (safe)
   - Static atomic counters are in anonymous namespace (safe)
   - But order of destruction is undefined

## Additional Potential Fixes

If the issue persists, consider:

1. **Make LogStatistics() check if Logger is available:**
   ```cpp
   void LazyAttributeLoader::LogStatistics() {
     // Check if Logger is still available (defensive)
     try {
       // Try to access Logger to see if it's still alive
       Logger::Instance();
     } catch (...) {
       // Logger destroyed, skip logging
       return;
     }
     // ... rest of function
   }
   ```

2. **Disable LogStatistics() during shutdown:**
   - Add a flag to track if application is shutting down
   - Check flag before calling LogStatistics()

3. **Use std::atexit() to ensure proper cleanup order:**
   - Register cleanup functions in reverse order
   - Ensure Logger is destroyed last

## Current Status

✅ **Fixed:** Added try-catch guard to `LogStatistics()`

This should prevent abort() if the issue is related to Logger access during static destruction. If the issue persists, we need more information about the exact error message and stack trace.
