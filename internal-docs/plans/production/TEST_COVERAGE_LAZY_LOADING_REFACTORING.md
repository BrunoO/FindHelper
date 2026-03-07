# Test Coverage Analysis: Lazy-Loaded Attributes Refactoring

## Overview

This document analyzes test coverage for refactoring lazy-loaded attributes from sentinel value pattern to a `LazyValue<T>` template class, addressing the "Missing Abstraction for Lazy-Loaded Attributes" code smell.

## Current State

### Lazy-Loaded Attributes in FileEntry

```cpp
struct FileEntry {
    uint64_t fileSize;              // kFileSizeNotLoaded, kFileSizeFailed, or actual size
    FILETIME lastModificationTime;   // kFileTimeNotLoaded, kFileTimeFailed, or actual time
};
```

### Sentinel Values

- `kFileSizeNotLoaded = UINT64_MAX` - Not yet loaded
- `kFileSizeFailed = UINT64_MAX - 1` - Load attempt failed
- `kFileTimeNotLoaded` - Not yet loaded (sentinel FILETIME)
- `kFileTimeFailed` - Load attempt failed (sentinel FILETIME)

### Current Usage Pattern

The pattern is repeated throughout FileIndex:

```cpp
// Pattern 1: Check if loaded
if (it->second.fileSize != kFileSizeNotLoaded) {
    // Use cached value
}

// Pattern 2: Check if failed
if (it->second.fileSize == kFileSizeFailed) {
    // Handle failure
}

// Pattern 3: Load on demand
if (it->second.fileSize == kFileSizeNotLoaded) {
    it->second.fileSize = GetFileSize(path);
}

// Pattern 4: Check sentinel time
if (IsSentinelTime(it->second.lastModificationTime)) {
    // Load modification time
}
```

## Test Coverage Gaps

### Current Test Status
- ❌ **No tests for lazy-loading behavior**
- ❌ **No tests for GetFileSizeById() automatic loading**
- ❌ **No tests for GetFileModificationTimeById() automatic loading**
- ❌ **No tests for LoadFileSize() explicit loading**
- ❌ **No tests for LoadFileModificationTime() explicit loading**
- ❌ **No tests for sentinel value handling**
- ❌ **No tests for failure state handling**
- ❌ **No tests for concurrent loading (race conditions)**

### Required Test Coverage

#### 1. Basic Lazy-Loading Behavior
- [ ] Initial state: fileSize is kFileSizeNotLoaded
- [ ] Initial state: lastModificationTime is kFileTimeNotLoaded
- [ ] GetFileSizeById() loads value on first call
- [ ] GetFileSizeById() returns cached value on subsequent calls
- [ ] GetFileModificationTimeById() loads value on first call
- [ ] GetFileModificationTimeById() returns cached value on subsequent calls

#### 2. Explicit Loading Methods
- [ ] LoadFileSize() returns true when loading succeeds
- [ ] LoadFileSize() returns false when already loaded
- [ ] LoadFileSize() returns false for directories
- [ ] LoadFileModificationTime() returns true when loading succeeds
- [ ] LoadFileModificationTime() returns false when already loaded
- [ ] LoadFileModificationTime() returns false for directories

#### 3. Failure Handling
- [ ] GetFileSizeById() handles missing files (returns 0, marks as failed)
- [ ] GetFileSizeById() handles permission errors (marks as failed)
- [ ] GetFileModificationTimeById() handles missing files (returns kFileTimeFailed)
- [ ] GetFileModificationTimeById() handles permission errors (marks as failed)
- [ ] Failed state persists (doesn't retry on subsequent calls)
- [ ] Failed state can be distinguished from not-loaded state

#### 4. Sentinel Value Checks
- [ ] IsSentinelTime() correctly identifies kFileTimeNotLoaded
- [ ] IsSentinelTime() correctly identifies kFileTimeFailed
- [ ] IsSentinelTime() returns false for valid FILETIME values
- [ ] IsFailedTime() correctly identifies kFileTimeFailed
- [ ] IsFailedTime() returns false for kFileTimeNotLoaded

#### 5. Concurrent Loading (Race Conditions)
- [ ] Multiple threads calling GetFileSizeById() concurrently (only one loads)
- [ ] Multiple threads calling GetFileModificationTimeById() concurrently
- [ ] Thread safety: cached value returned after concurrent load
- [ ] Double-check locking pattern works correctly

#### 6. Combined Loading Optimization
- [ ] GetFileSizeById() loads modification time if also needed
- [ ] GetFileModificationTimeById() loads file size if also needed
- [ ] Combined loading uses GetFileAttributes() (single system call)

#### 7. Directory Handling
- [ ] Directories have fileSize = 0 (not lazy-loaded)
- [ ] Directories have lastModificationTime = {0, 0} (not lazy-loaded)
- [ ] LoadFileSize() returns false for directories
- [ ] LoadFileModificationTime() returns false for directories

#### 8. Edge Cases
- [ ] File size = 0 (valid size, not sentinel)
- [ ] File size = UINT64_MAX - 2 (just below failed sentinel)
- [ ] File deleted between check and load
- [ ] File permissions change between check and load

## Test Plan

### Phase 1: Basic Lazy-Loading Tests

Create `tests/FileIndexLazyLoadingTests.cpp` with:

1. **Initial State Tests**
   ```cpp
   TEST_CASE("FileEntry initial state - fileSize not loaded") {
     FileIndex index;
     uint64_t id = index.Insert(1, 0, "test.txt", false, kFileTimeNotLoaded);
     // Check initial state
     // Verify fileSize == kFileSizeNotLoaded
   }
   ```

2. **Automatic Loading Tests**
   ```cpp
   TEST_CASE("GetFileSizeById - loads on first call") {
     FileIndex index;
     // Insert file
     // Call GetFileSizeById()
     // Verify value was loaded (not kFileSizeNotLoaded)
   }
   
   TEST_CASE("GetFileSizeById - returns cached value") {
     FileIndex index;
     // Insert file
     // Call GetFileSizeById() twice
     // Verify second call returns same value (no reload)
   }
   ```

3. **Explicit Loading Tests**
   ```cpp
   TEST_CASE("LoadFileSize - returns true on success") {
     FileIndex index;
     // Insert file
     // Call LoadFileSize()
     // Verify returns true
   }
   
   TEST_CASE("LoadFileSize - returns false when already loaded") {
     FileIndex index;
     // Insert file
     // Load once
     // Load again
     // Verify second call returns false
   }
   ```

### Phase 2: Failure Handling Tests

1. **Missing File Tests**
   ```cpp
   TEST_CASE("GetFileSizeById - handles missing file") {
     FileIndex index;
     // Insert file
     // Delete file from disk
     // Call GetFileSizeById()
     // Verify returns 0 and marks as failed
   }
   ```

2. **Failure State Persistence**
   ```cpp
   TEST_CASE("GetFileSizeById - failed state persists") {
     FileIndex index;
     // Insert file
     // Cause load failure
     // Call GetFileSizeById() multiple times
     // Verify always returns 0 (doesn't retry)
   }
   ```

### Phase 3: Concurrent Loading Tests

1. **Race Condition Tests**
   ```cpp
   TEST_CASE("GetFileSizeById - concurrent loading") {
     FileIndex index;
     // Insert file
     // Launch multiple threads calling GetFileSizeById()
     // Verify only one actual load occurs
     // Verify all threads get same value
   }
   ```

### Phase 4: Combined Loading Tests

1. **Optimization Tests**
   ```cpp
   TEST_CASE("GetFileSizeById - loads modification time if needed") {
     FileIndex index;
     // Insert file with both not loaded
     // Call GetFileSizeById()
     // Verify both size and time are loaded
   }
   ```

## Implementation Strategy

### Step 1: Add Comprehensive Tests
- Create `tests/FileIndexLazyLoadingTests.cpp`
- Cover all lazy-loading scenarios
- Test both fileSize and lastModificationTime
- Test explicit and automatic loading
- Test failure cases

### Step 2: Create LazyValue Template
- Create `LazyValue.h` with template class
- Implement state management
- Implement loading logic
- Add thread-safety (if needed)

### Step 3: Update FileEntry
- Replace `uint64_t fileSize` with `LazyValue<uint64_t> fileSize`
- Replace `FILETIME lastModificationTime` with `LazyValue<FILETIME> lastModificationTime`
- Update all access patterns

### Step 4: Update FileIndex Methods
- Update GetFileSizeById() to use LazyValue
- Update GetFileModificationTimeById() to use LazyValue
- Update LoadFileSize() to use LazyValue
- Update LoadFileModificationTime() to use LazyValue
- Update all sentinel value checks

### Step 5: Verify Tests Pass
- Run all tests
- Fix any issues
- Ensure no regressions

## Success Criteria

✅ All lazy-loading tests pass  
✅ All existing tests pass  
✅ No performance regression  
✅ Thread-safety maintained  
✅ Failure handling works correctly  
✅ Combined loading optimization preserved  

## Risk Assessment

### Low Risk
- Basic lazy-loading behavior
- Explicit loading methods
- Sentinel value checks

### Medium Risk
- Failure state handling
- Directory handling
- Edge cases (0 size, etc.)

### High Risk
- Concurrent loading (race conditions)
- Combined loading optimization
- Thread-safety
- Performance (must not regress)

## Notes

- LazyValue must be thread-safe for concurrent access
- Must preserve combined loading optimization (GetFileAttributes)
- Must handle mutable const methods (GetFileSizeById is const)
- Must maintain performance (no unnecessary allocations)
- Consider using std::atomic for state if needed
- May need to keep sentinel value constants for backward compatibility during transition

