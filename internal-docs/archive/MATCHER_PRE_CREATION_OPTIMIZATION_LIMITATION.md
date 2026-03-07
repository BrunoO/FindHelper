# Matcher Pre-Creation Optimization Limitation

## Overview

This document explains why the optimization of pre-creating matchers (`CreateFilenameMatcher`/`CreatePathMatcher`) outside thread lambdas and capturing them is not feasible with the current architecture.

## The Proposed Optimization

**Goal**: Move `CreateFilenameMatcher` and `CreatePathMatcher` calls outside the thread lambda, create them once in `LaunchSearchTasks`, and capture the resulting `LightweightCallable` objects (which are designed for efficient copying/moving).

**Rationale**: 
- Avoids recreating matchers in each worker thread
- Reduces redundant pattern analysis and lambda creation
- `LightweightCallable` is designed for efficient copying/moving

## Why It's Not Feasible

### Technical Constraint: Lambda Size Limit

The issue is that `LightweightCallable` objects themselves are **80 bytes** in size, but when captured in a lambda, the lambda becomes too large for the thread pool's storage mechanism.

**Error Encountered**:
```
error: static assertion failed: Callable too large for lightweight wrapper
note: expression evaluates to '80 <= 48'
```

### Root Cause

1. **Thread Pool Storage**: `SearchThreadPool::Enqueue()` stores lambdas in `std::function<void()>` (see `SearchThreadPool.h:91`). While `std::function` can handle large callables via heap allocation, the issue occurs earlier.

2. **LightweightCallable Size**: Each `LightweightCallable<bool, const char*>` or `LightweightCallable<bool, std::string_view>` is approximately **80 bytes** (includes function pointers, storage buffer, and metadata).

3. **Lambda Capture**: When capturing two `LightweightCallable` objects in a lambda:
   - The lambda's closure contains ~160 bytes of `LightweightCallable` data
   - Additional captured variables (indices, flags, etc.) add more size
   - The total lambda size exceeds practical limits

4. **Compilation Failure**: Attempting to capture `LightweightCallable` objects (even by reference) causes compilation errors because:
   - The lambda itself becomes too large
   - Some code path attempts to wrap the lambda in another `LightweightCallable` (which has a 48-byte limit)

### Attempted Solutions

1. **Capture by Reference**: Tried `[&precreated_filename_matcher, &precreated_path_matcher]` - still fails because the lambda size is still too large.

2. **Copy Inside Lambda**: Tried copying from references inside the lambda - same issue, the lambda closure is still too large.

3. **Use std::function Instead**: Would require changing the thread pool architecture, which is a larger refactoring.

## Current Optimization (What We Do Instead)

We **do** optimize matcher creation, but in a way that works with the current architecture:

### PathPattern Pre-Compilation

1. **Pre-compile PathPattern patterns** once before threads start (in `FileIndex::SearchAsyncWithData`)
2. Store compiled patterns in `std::shared_ptr<path_pattern::CompiledPathPattern>`
3. Pass `shared_ptr`s to worker threads
4. Each thread creates the matcher from the pre-compiled pattern (cheap operation - just creates a lambda that captures the `shared_ptr`)

**Code Location**: `FileIndex.cpp:918-970`

```cpp
// Phase 2: Pre-compile PathPattern patterns and create matchers once before threads start
std::shared_ptr<path_pattern::CompiledPathPattern> precompiled_filename_pattern = nullptr;
// ... compile pattern once ...

// In worker thread:
if (precompiled_filename_pattern) {
    filename_matcher = search_pattern_utils::CreateFilenameMatcher(precompiled_filename_pattern);
}
```

### Benefits of Current Approach

1. **PathPattern patterns**: Expensive DFA/NFA compilation happens once, not per thread
2. **Other patterns** (StdRegex, SimpleRegex, Glob, Substring): Still created per thread, but:
   - Pattern analysis is relatively cheap
   - Lambda creation is cheap
   - The overhead is minimal compared to the actual matching work

### Performance Impact

- **PathPattern patterns**: Significant savings (DFA construction can take microseconds)
- **Other patterns**: Minimal overhead (pattern analysis is nanoseconds)
- **Overall**: The current optimization provides most of the benefit without architectural changes

## Future Considerations

If this optimization becomes critical, possible solutions:

1. **Increase LightweightCallable Storage**: Increase `kStorageSize` from 48 to 128+ bytes (trade-off: larger stack usage)

2. **Use std::function in Thread Pool**: Change `SearchThreadPool` to use `std::function` instead of `LightweightCallable` (trade-off: heap allocation overhead)

3. **Separate Matcher Storage**: Store matchers in a separate container and pass indices/IDs to worker threads (trade-off: additional indirection)

4. **Template Thread Pool**: Make thread pool storage size configurable via template parameter (trade-off: code complexity)

## Conclusion

The proposed optimization makes sense in theory, but is not feasible with the current architecture due to lambda size constraints. The current approach (pre-compiling PathPattern patterns and passing `shared_ptr`s) provides most of the performance benefit without requiring architectural changes.

**Status**: Optimization documented but not implemented due to technical constraints.

**Last Updated**: 2025-01-XX

