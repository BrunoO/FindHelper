# Futures Usage in FindHelper: Why and How

## Overview

This document explains why and how `std::future` is used in the FindHelper application, specifically for asynchronous attribute loading during sorting operations. Understanding this pattern is critical for maintaining the codebase and preventing memory leaks.

---

## The Problem: Blocking I/O in the UI Thread

### Context

When users sort search results by **Size** or **Last Modified** columns, the application needs to load file metadata (file size and modification time) from the file system. This requires I/O operations that can be slow:

- **File size**: Requires reading file metadata from disk
- **Modification time**: Requires reading file timestamps from disk
- **Scale**: For 1000 search results, this means 1000-2000 I/O operations

### The Challenge

**Problem**: If we load attributes synchronously (blocking) in the UI thread:
- The UI freezes while loading attributes
- Users experience lag and unresponsiveness
- The application appears "frozen" during sorting
- Poor user experience, especially with large result sets

**Solution**: Use asynchronous loading with `std::future` to:
- Load attributes in background threads (non-blocking)
- Keep the UI responsive during loading
- Sort results once all attributes are loaded
- Provide visual feedback ("Loading attributes...")

---

## How Futures Work

### What is a `std::future`?

A `std::future` is a C++ standard library mechanism for asynchronous operations. It represents a value that will be available in the future, allowing you to:

1. **Start an async task** without waiting for it to complete
2. **Check if the task is done** without blocking
3. **Get the result** when ready (or wait for it)

### Basic Pattern

```cpp
// Start async task
std::future<void> future = thread_pool.enqueue([&result, &file_index] {
    result.fileSize = file_index.GetFileSizeById(result.fileId);
});

// Later, check if done (non-blocking)
if (future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    // Task is complete - get result
    future.get(); // Clean up and get result
}
```

---

## Our Implementation: Attribute Loading for Sorting

### Architecture

```
User clicks "Size" column
    ↓
StartSortAttributeLoading()
    ├─ Clean up any existing futures
    ├─ Enqueue async tasks to ThreadPool
    └─ Store futures in GuiState.attributeLoadingFutures
    ↓
Each frame (non-blocking):
    CheckSortAttributeLoadingAndSort()
    ├─ Check if all futures are ready
    ├─ If ready: .get() all futures (cleanup)
    ├─ Format display strings
    ├─ Sort results
    └─ Clear futures vector
```

### Step-by-Step Flow

#### 1. Starting Async Loading (`StartSortAttributeLoading`)

**Location**: `SearchResultUtils.cpp:381`

**What it does**:
- Cleans up any existing futures (prevents memory leaks)
- Enqueues I/O tasks to the thread pool
- Each task loads one attribute (size or modification time)
- Stores futures in `GuiState.attributeLoadingFutures`

**Code**:
```cpp
void StartSortAttributeLoading(GuiState &state, ...) {
    // CRITICAL: Clean up existing futures first
    for (auto& f : state.attributeLoadingFutures) {
        if (f.valid()) {
            f.wait();      // Wait for completion
            f.get();       // Clean up resources
        }
    }
    state.attributeLoadingFutures.clear();
    
    // Start new async tasks
    state.attributeLoadingFutures = StartAttributeLoadingAsync(...);
}
```

**Why clean up first?**
- Futures capture references to `SearchResult` objects
- If results are replaced while futures are running, references become invalid
- This causes memory corruption and crashes
- Cleaning up ensures all pending work completes before starting new work

#### 2. Creating Async Tasks (`StartAttributeLoadingAsync`)

**Location**: `SearchResultUtils.cpp:203`

**What it does**:
- Iterates through search results
- For each result needing attributes, enqueues a task to the thread pool
- Each task runs in a background thread (named "AttrLoader-Size" or "AttrLoader-ModTime")
- Returns a vector of futures

**Code**:
```cpp
static std::vector<std::future<void>> StartAttributeLoadingAsync(...) {
    std::vector<std::future<void>> futures;
    
    for (auto& result : results) {
        if (result.fileSize == kFileSizeNotLoaded) {
            futures.emplace_back(thread_pool.enqueue([&result, &file_index] {
                SetThreadName("AttrLoader-Size");
                result.fileSize = file_index.GetFileSizeById(result.fileId);
            }));
        }
        // Similar for modification time...
    }
    
    return futures;
}
```

**Key points**:
- Tasks run in parallel (one per thread pool worker)
- Each task modifies a `SearchResult` object (fields are `mutable`)
- Thread-safe: `GetFileSizeById()` is thread-safe
- Non-blocking: Returns immediately, doesn't wait for completion

#### 3. Checking Completion (`CheckSortAttributeLoadingAndSort`)

**Location**: `SearchResultUtils.cpp:425`

**What it does**:
- Called every frame from the UI thread
- Non-blocking check: Uses `wait_for(0)` to see if futures are ready
- If all complete: Gets results, formats display strings, sorts, cleans up
- If not complete: Returns false, will check again next frame

**Code**:
```cpp
bool CheckSortAttributeLoadingAndSort(...) {
    if (state.attributeLoadingFutures.empty()) {
        return false;
    }
    
    // Non-blocking check: are all futures ready?
    bool allComplete = true;
    for (const auto& f : state.attributeLoadingFutures) {
        if (f.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            allComplete = false;
            break;
        }
    }
    
    if (!allComplete) {
        return false; // Still loading, check again next frame
    }
    
    // All complete - get results and clean up
    for (auto& f : state.attributeLoadingFutures) {
        f.get(); // CRITICAL: Clean up resources
    }
    
    // Format display strings, sort, clear futures...
    return true;
}
```

**Why `wait_for(0)`?**
- Non-blocking: Returns immediately
- Doesn't freeze the UI thread
- Allows checking every frame without performance impact

**Why `.get()` after checking?**
- `.get()` consumes the future and releases resources
- Must be called exactly once per future
- Prevents memory leaks from abandoned futures
- Gets any exceptions that occurred during execution

---

## Memory Leak Prevention

### The Problem

If futures are not properly cleaned up:
- Thread pool resources are not released
- Memory accumulates over time
- Application memory usage grows unbounded
- Eventually leads to out-of-memory crashes

### The Solution

**CRITICAL RULE**: Always call `.get()` on futures before they go out of scope.

**Pattern**:
```cpp
// ✅ CORRECT: Clean up futures
for (auto& f : futures) {
    if (f.valid()) {
        f.wait();      // Wait if not ready
        f.get();       // Clean up resources
    }
}
futures.clear();
```

**Where we clean up**:
1. `StartSortAttributeLoading()`: Before starting new futures
2. `CheckSortAttributeLoadingAndSort()`: After futures complete
3. `SearchController::PollResults()`: Before replacing search results
4. `GuiState::ClearInputs()`: Before clearing results

### Why `.get()` is Required

- **Resource cleanup**: Releases thread pool resources
- **Exception handling**: Propagates exceptions from async tasks
- **Memory management**: Frees internal future state
- **Thread safety**: Ensures proper synchronization

**Without `.get()`**: Futures hold resources indefinitely, causing memory leaks.

---

## Thread Safety Considerations

### Safe Operations

✅ **Reading futures** (checking status):
- `wait_for()`: Thread-safe, can be called from any thread
- `valid()`: Thread-safe, can be called from any thread

✅ **Modifying SearchResult**:
- Fields are marked `mutable` in `SearchResult` struct
- `GetFileSizeById()` is thread-safe (uses locks internally)
- Each future modifies a different `SearchResult` object

### Critical Rules

1. **Futures must be cleaned up on the UI thread**
   - All `.get()` calls happen on the main/UI thread
   - Prevents race conditions

2. **Don't access SearchResult while futures are running**
   - Futures hold references to SearchResult objects
   - If results are replaced, references become invalid
   - Always clean up futures before modifying results

3. **One `.get()` per future**
   - Calling `.get()` twice on the same future is undefined behavior
   - Check `valid()` before calling `.get()`

---

## Usage Patterns in the Codebase

### Pattern 1: Async Attribute Loading for Sorting

**Use case**: Load file attributes (size, modification time) for sorting

**Files**:
- `SearchResultUtils.cpp`: `StartSortAttributeLoading()`, `CheckSortAttributeLoadingAndSort()`
- `UIRenderer.cpp`: Calls these functions when sorting by Size/Last Modified

**Flow**:
1. User clicks column header
2. `StartSortAttributeLoading()` enqueues async tasks
3. Each frame: `CheckSortAttributeLoadingAndSort()` checks completion
4. When complete: Sort results and clean up futures

### Pattern 2: Blocking Attribute Loading

**Use case**: Load attributes immediately (blocking) when needed

**Files**:
- `SearchResultUtils.cpp`: `PrefetchAndFormatSortDataBlocking()`

**Flow**:
1. Enqueue async tasks
2. Immediately wait for all futures: `f.get()` for each
3. Format display strings
4. Continue with sorting

**When to use**: When we can't proceed without the data (e.g., initial display)

### Pattern 3: Gemini API Calls

**Use case**: Async API calls for pattern generation

**Files**:
- `GeminiApiUtils.cpp`: Async API calls
- `GuiState.h`: `gemini_api_future_` member

**Similar pattern**: Store future, check completion each frame, clean up when done

---

## Best Practices

### ✅ DO

1. **Always call `.get()` on futures before they go out of scope**
   ```cpp
   for (auto& f : futures) {
       if (f.valid()) {
           f.wait();  // Wait if needed
           f.get();  // Clean up
       }
   }
   ```

2. **Check `valid()` before calling `.get()`**
   ```cpp
   if (f.valid()) {
       f.get();
   }
   ```

3. **Use non-blocking checks in UI thread**
   ```cpp
   if (f.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
       f.get();
   }
   ```

4. **Clean up existing futures before starting new ones**
   ```cpp
   // Clean up old futures
   for (auto& f : existing_futures) { f.wait(); f.get(); }
   existing_futures.clear();
   
   // Start new futures
   existing_futures = start_new_async_tasks();
   ```

5. **Handle exceptions gracefully**
   ```cpp
   try {
       f.get();
   } catch (...) {
       // Log error, but ensure cleanup happens
   }
   ```

### ❌ DON'T

1. **Don't call `.get()` multiple times on the same future**
   ```cpp
   // ❌ WRONG
   f.get();
   f.get(); // Undefined behavior!
   ```

2. **Don't abandon futures without calling `.get()`**
   ```cpp
   // ❌ WRONG - Memory leak!
   std::vector<std::future<void>> futures = start_tasks();
   // Forgot to call .get() - memory leak!
   ```

3. **Don't access results while futures are running**
   ```cpp
   // ❌ WRONG - Race condition!
   auto futures = start_loading(results);
   results.clear(); // Invalidates references in futures!
   ```

4. **Don't block the UI thread**
   ```cpp
   // ❌ WRONG - Freezes UI!
   for (auto& f : futures) {
       f.wait(); // Blocks until complete
   }
   
   // ✅ CORRECT - Non-blocking
   for (auto& f : futures) {
       if (f.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
           f.get();
       }
   }
   ```

---

## Performance Characteristics

### Benefits

1. **Non-blocking UI**: UI remains responsive during I/O
2. **Parallel loading**: Multiple attributes loaded simultaneously
3. **Efficient**: Only loads what's needed (lazy loading)
4. **Scalable**: Works with large result sets

### Overhead

1. **Memory**: Each future uses ~100-200 bytes
2. **Thread pool**: Uses worker threads (shared resource)
3. **Synchronization**: Minimal overhead for checking completion

### Typical Performance

- **1000 results**: ~1000-2000 futures created
- **Loading time**: 100-500ms (parallel I/O)
- **UI responsiveness**: No freezing, smooth 60 FPS

---

## Debugging Tips

### Identifying Memory Leaks

**Symptoms**:
- Memory usage grows over time
- Application becomes slower
- Eventually crashes with out-of-memory

**How to find**:
1. Check if futures are being cleaned up
2. Look for places where futures are created but `.get()` is never called
3. Use memory profiler to identify growing allocations

### Common Issues

1. **Futures not cleaned up**
   - Solution: Ensure `.get()` is called before futures go out of scope

2. **Invalid references**
   - Solution: Clean up futures before modifying/replacing results

3. **Double `.get()` calls**
   - Solution: Check `valid()` before calling `.get()`, or clear futures after getting

4. **Sorting not working**
   - Solution: Ensure `CheckSortAttributeLoadingAndSort()` is called each frame
   - Ensure futures are properly cleaned up after sorting

---

## Related Files

- **SearchResultUtils.cpp**: Main implementation of async attribute loading
- **SearchResultUtils.h**: Function declarations
- **GuiState.h/cpp**: State management and future cleanup
- **UIRenderer.cpp**: UI integration and frame-by-frame checking
- **SearchController.cpp**: Future cleanup when results change
- **ThreadPool.cpp**: Thread pool that executes async tasks

---

## Summary

**Why we use futures**:
- Keep UI responsive during slow I/O operations
- Enable parallel loading of file attributes
- Provide smooth user experience when sorting

**How we use futures**:
1. Enqueue async tasks to thread pool
2. Store futures in `GuiState.attributeLoadingFutures`
3. Check completion each frame (non-blocking)
4. When complete: Get results, format, sort, clean up

**Critical rules**:
- Always call `.get()` on futures before they go out of scope
- Clean up existing futures before starting new ones
- Use non-blocking checks in UI thread
- Handle exceptions gracefully

**Memory leak prevention**:
- `.get()` releases thread pool resources
- Must be called exactly once per future
- Clean up before modifying/replacing results

