# AppBootstrap_linux.cpp Deduplication Analysis

## Summary

Identified significant code duplication in `AppBootstrap_linux.cpp` that can be refactored using helper functions.

## Duplication Patterns Identified

### 1. Exception Handling Catch Blocks (7 occurrences)

**Before Refactoring:**
All catch blocks follow the same pattern (lines 316-354):
```cpp
} catch (const std::system_error& e) {
  LOG_ERROR_BUILD("System error during initialization: " << e.what() 
                  << " (code: " << e.code() << ")");
  cleanup_on_exception();
  // NOSONAR: RVO (Return Value Optimization) applies to plain return statements in C++17
  return result;  // NOSONAR cpp:S5274
} catch (const std::bad_alloc& e) {
  (void)e;  // Suppress unused variable warning in Release mode
  LOG_ERROR_BUILD("Memory allocation failure during initialization: " << e.what());
  cleanup_on_exception();
  // NOSONAR: RVO (Return Value Optimization) applies to plain return statements in C++17
  return result;  // NOSONAR cpp:S5274
}
// ... similar pattern for invalid_argument, logic_error, runtime_error, exception, catch-all
```

**Duplication Statistics:**
- **7 occurrences** of this pattern
- **~35 lines of repetitive code** (5 lines per catch block)
- Each catch block: log error, call cleanup, return result

**Solution:**
Create a helper function `HandleInitializationException()` that:
- Takes exception type name and message
- Logs the error
- Calls cleanup
- Returns the result

### 2. Command Line Override Pattern (4 occurrences)

**Before Refactoring:**
The `ApplyCommandLineOverrides` function has repetitive patterns (lines 87-114):
```cpp
if (cmd_args.thread_pool_size_override >= 0) {
  app_settings.searchThreadPoolSize = cmd_args.thread_pool_size_override;
  LOG_INFO_BUILD("Command line override: thread pool size = " << app_settings.searchThreadPoolSize);
}

if (!cmd_args.load_balancing_override.empty()) {
  app_settings.loadBalancingStrategy = cmd_args.load_balancing_override;
  LOG_INFO_BUILD("Command line override: load balancing strategy = " << app_settings.loadBalancingStrategy);
}

if (cmd_args.window_width_override >= 0) {
  app_settings.windowWidth = cmd_args.window_width_override;
  LOG_INFO_BUILD("Command line override: window width = " << app_settings.windowWidth);
}
// ... similar for window_height_override
```

**Duplication Statistics:**
- **4 occurrences** of this pattern
- **~12 lines of repetitive code** (3 lines per override)
- Pattern: check condition, assign value, log message

**Solution:**
Create template helper functions:
- `ApplyIntOverride()` for integer overrides
- `ApplyStringOverride()` for string overrides

## Proposed Solution

### Helper Function 1: HandleInitializationException

```cpp
static AppBootstrapResultLinux HandleInitializationException(
    const char* exception_type,
    const std::string& message,
    std::function<void()> cleanup_fn,
    AppBootstrapResultLinux& result) {
  LOG_ERROR_BUILD(exception_type << " during initialization: " << message);
  cleanup_fn();
  // NOSONAR: RVO (Return Value Optimization) applies to plain return statements in C++17
  return result;  // NOSONAR cpp:S5274
}
```

Or simpler, since all catch blocks do the same thing:
```cpp
static AppBootstrapResultLinux HandleInitializationException(
    const std::string& error_message,
    std::function<void()> cleanup_fn,
    AppBootstrapResultLinux& result) {
  LOG_ERROR_BUILD(error_message);
  cleanup_fn();
  // NOSONAR: RVO (Return Value Optimization) applies to plain return statements in C++17
  return result;  // NOSONAR cpp:S5274
}
```

### Helper Function 2: ApplyIntOverride

```cpp
template<typename T>
static void ApplyIntOverride(
    T override_value,
    T& setting_value,
    const char* setting_name,
    T invalid_value = -1) {
  if (override_value >= 0 || override_value != invalid_value) {
    setting_value = override_value;
    LOG_INFO_BUILD("Command line override: " << setting_name << " = " << setting_value);
  }
}
```

### Helper Function 3: ApplyStringOverride

```cpp
static void ApplyStringOverride(
    const std::string& override_value,
    std::string& setting_value,
    const char* setting_name) {
  if (!override_value.empty()) {
    setting_value = override_value;
    LOG_INFO_BUILD("Command line override: " << setting_name << " = " << setting_value);
  }
}
```

## Expected Results

- **Code reduction:** ~40-50 lines of duplicate code eliminated
- **Maintainability:** Changes to exception handling or override logic only need to be made in one place
- **Readability:** Clearer intent with named helper functions
- **Consistency:** Same pattern used consistently across all exception handlers and overrides

## Implementation Plan

1. Add `HandleInitializationException()` helper function
2. Replace all catch blocks with calls to the helper
3. Add `ApplyIntOverride()` and `ApplyStringOverride()` helper functions
4. Refactor `ApplyCommandLineOverrides()` to use the helpers
5. Test to ensure all functionality still works correctly
6. Verify no compilation warnings or errors
