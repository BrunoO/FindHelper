# Dynamic/Hybrid Strategy Crash Analysis

## Problem Description

**Symptom**: Crashes occur on Windows when pressing "Search" button multiple times with the same parameters, specifically with Dynamic and Hybrid strategies.

**Root Cause**: The `shared_lock` in `SearchAsyncWithData()` is released when the function returns, but the lambdas continue accessing member variables (`path_ids_`, `path_storage_`, etc.) after the lock is released. When multiple searches run concurrently:

1. Each `SearchAsyncWithData()` call acquires its own `shared_lock`
2. The lock is released when the function returns (line 1770)
3. But the lambdas are still running in the thread pool
4. If a new search starts while old searches are running, they all access the same arrays
5. If arrays change size (e.g., during `RecomputeAllPaths()`), old searches could access out-of-bounds indices or invalid memory

## Current Code Behavior

```cpp
std::vector<std::future<...>> FileIndex::SearchAsyncWithData(...) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);  // Lock acquired
  
  // ... setup code ...
  
  // Create futures that capture 'this' and access member variables
  futures.push_back(thread_pool.Enqueue([this, ...]() {
    // Lambda accesses path_ids_, path_storage_, etc.
    // BUT: lock is already released when this executes!
  }));
  
  return futures;  // Lock released here when function returns
}
```

**The Problem**: The lock is released when `SearchAsyncWithData` returns, but the lambdas continue executing and accessing member variables without lock protection.

## Why This Causes Crashes

1. **Array Reallocation**: If `RecomputeAllPaths()` or `Insert()` is called while searches are running:
   - Arrays might be reallocated (vectors grow)
   - Old lambdas might have stale pointers/references
   - Accessing reallocated memory = crash

2. **Size Changes**: If arrays change size:
   - Bounds checking uses `actual_size` captured at start
   - But if arrays grow/shrink, bounds become invalid
   - Out-of-bounds access = crash

3. **Memory Visibility**: Without the lock:
   - No guarantee that array changes are visible to running lambdas
   - Stale data or invalid memory access = crash

## Current Mitigations (Insufficient)

The code has some defensive checks:
- Bounds checking with `safe_total_items` and `actual_size`
- Array size validation in lambdas
- Overflow protection

**But these are insufficient** because:
- They check sizes, but don't prevent accessing reallocated memory
- They don't prevent race conditions with array modifications
- They don't ensure memory visibility

## Recommended Fixes

### Option 1: Keep Lock Until Futures Complete (Not Recommended)
- Hold the lock until all futures complete
- **Problem**: Blocks all writes (Insert, Remove, RecomputeAllPaths) during entire search
- **Impact**: UI freezes, USN monitor blocked

### Option 2: Capture Array Snapshots (Recommended)
- Capture copies of array sizes and data pointers at lock time
- Pass snapshots to lambdas instead of `this` pointer
- **Problem**: Large memory overhead for large datasets
- **Impact**: Memory usage increases significantly

### Option 3: Prevent Concurrent Searches (Implemented ✅)
- **Approach**: Cancel the previous search when a new one starts (keep only the most recent)
- **Implementation**:
  - Added `std::atomic<bool> cancel_current_search_` to `SearchWorker`
  - When `StartSearch()` is called while a search is running, set the cancellation flag
  - Pass cancellation flag to `SearchAsyncWithData()` as optional parameter
  - Check cancellation flag periodically in search lambdas (Dynamic and Hybrid strategies)
  - Check cancellation flag more frequently in `SearchWorker` when waiting for futures
- **Benefits**: 
  - Prevents concurrent searches accessing arrays simultaneously
  - Fast response to user input (no queuing, just cancel old search)
  - Simple implementation
- **Impact**: Old searches are cancelled and abandoned, new search starts immediately

### Option 4: Use Atomic References/Pointers (Complex)
- Use atomic pointers for arrays
- Update atomically when arrays change
- **Problem**: Complex implementation, potential for other race conditions
- **Impact**: Significant code changes

## Solution Implemented ✅

**Option 3: Prevent Concurrent Searches** has been implemented.

### Changes Made:

1. **SearchWorker.h/cpp**:
   - Added `std::atomic<bool> cancel_current_search_` member
   - `StartSearch()` now sets cancellation flag if a search is already running
   - Cancellation flag is reset when a new search starts
   - Pass cancellation flag to `SearchAsyncWithData()`
   - Check cancellation flag more frequently when waiting for futures

2. **FileIndex.h/cpp**:
   - Added optional `const std::atomic<bool> *cancel_flag` parameter to `SearchAsyncWithData()`
   - **Dynamic Strategy**: Check cancellation flag before processing each chunk
   - **Hybrid Strategy**: Check cancellation flag before processing each dynamic chunk
   - **Static Strategy**: Cancellation flag captured (can be checked if needed in future)

### How It Works:

1. User presses "Search" button multiple times
2. First search starts normally
3. When second search is requested:
   - `StartSearch()` detects `is_searching_` is true
   - Sets `cancel_current_search_ = true`
   - Updates `next_params_` with new search parameters
   - Sets `search_requested_ = true`
4. Worker thread wakes up:
   - Sees `search_requested_` is true
   - Resets `cancel_current_search_ = false` for new search
   - Starts new search with updated parameters
5. Old search lambdas:
   - Check `cancel_flag` periodically
   - Exit early when cancellation is detected
   - Return empty results (abandoned)

### Benefits:

- ✅ Prevents concurrent searches accessing arrays simultaneously
- ✅ Fast response to user input (no queuing, just cancel old search)
- ✅ Simple implementation with minimal overhead
- ✅ Backward compatible (cancel_flag is optional, defaults to nullptr)

### Testing:

The existing concurrent search tests should now pass without crashes:
- `Dynamic strategy handles rapid successive searches`
- `Dynamic strategy handles overlapping concurrent searches`
- `Hybrid strategy handles overlapping concurrent searches`

## Test Coverage

Added tests to reproduce the crash scenario:
- `Dynamic strategy handles rapid successive searches` - Tests 5 searches in quick succession
- `Dynamic strategy handles overlapping concurrent searches` - Tests 3 concurrent searches
- `Hybrid strategy handles overlapping concurrent searches` - Tests 3 concurrent searches

These tests should help identify if the issue is:
1. Array bounds violations
2. Memory corruption
3. Race conditions
4. Invalid pointer access
