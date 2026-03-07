# Dynamic Load Balancing Options

## Problem Analysis

Based on testing, we've discovered:
- **Byte imbalance is low** → Byte-based chunking won't help significantly
- **Time imbalance exists** → Some threads finish much earlier than others
- **Key factor: Number of results** → Threads that find more matches take longer
- **Results are unpredictable** → Cannot predict which threads will find matches ahead of time

**Root Cause:** Work per thread is proportional to:
1. **Number of matches found** (unpredictable)
2. **Match complexity** (regex vs simple substring - variable)
3. **Result processing overhead** (extracting data, creating SearchResultData objects)

## Solution: Dynamic Chunk Assignment

Since we can't predict results, we need **dynamic load balancing** where threads grab work as they finish, rather than pre-assigning chunks.

---

## Option 1: Small Chunks with Atomic Counter ⭐ RECOMMENDED (Simplest)

### Concept

Divide work into many small chunks (e.g., 500-1000 items each). Threads atomically claim the next chunk as they finish.

### Implementation

```cpp
// In FileIndex::SearchAsyncWithData()
constexpr size_t kSmallChunkSize = 500; // Small chunks for dynamic assignment
std::atomic<size_t> next_chunk_start{0};

for (int t = 0; t < thread_count; ++t) {
  futures.push_back(std::async(std::launch::async, [this, &next_chunk_start, total_items, ...]() {
    std::vector<SearchResultData> local_results;
    
    while (true) {
      // Atomically claim next chunk
      size_t start = next_chunk_start.fetch_add(kSmallChunkSize, std::memory_order_relaxed);
      if (start >= total_items) break;
      
      size_t end = (std::min)(start + kSmallChunkSize, total_items);
      
      // Process this chunk (same logic as current implementation)
      for (size_t i = start; i < end; ++i) {
        // ... existing search logic ...
        if (matches) {
          local_results.push_back(data);
        }
      }
    }
    
    return local_results;
  }));
}
```

### Benefits

- **Simple**: Just replace static chunking with atomic counter
- **Automatic load balancing**: Fast threads (few matches) grab more chunks
- **Low overhead**: Atomic operations are very fast (~10-20 nanoseconds)
- **No queue management**: No mutexes, condition variables, or complex data structures
- **Cache-friendly**: Still processes contiguous ranges (good for cache locality)

### Trade-offs

- **Slight contention**: Multiple threads may compete for same atomic (but negligible)
- **Chunk size tuning**: Need to find optimal chunk size (500-2000 items typically good)

### Performance Gain

**Expected: 20-40% improvement** when imbalance is significant (>2x)

### Chunk Size Guidelines

- **Too small** (< 100 items): Overhead from atomic operations dominates
- **Too large** (> 5000 items): Less effective load balancing
- **Optimal**: 500-2000 items (depends on total items and thread count)

**Formula**: `chunk_size = max(500, total_items / (thread_count * 4))`

---

## Option 2: Work-Stealing Queue ⭐⭐ BEST PERFORMANCE (More Complex)

### Concept

Use a thread-safe queue where threads:
1. Pop work from their own queue (fast, no contention)
2. Steal work from other threads' queues when idle (load balancing)

### Implementation

```cpp
// Simple work-stealing queue
class WorkStealingQueue {
  std::deque<std::pair<size_t, size_t>> tasks_; // [start, end) pairs
  std::mutex mutex_;
  
public:
  void Push(size_t start, size_t end) {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.push_back({start, end});
  }
  
  bool Pop(std::pair<size_t, size_t>& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tasks_.empty()) return false;
    task = tasks_.front();
    tasks_.pop_front();
    return true;
  }
  
  bool Steal(std::pair<size_t, size_t>& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tasks_.empty()) return false;
    task = tasks_.back(); // Steal from back (different end)
    tasks_.pop_back();
    return true;
  }
};

// Divide into many small chunks
WorkStealingQueue queue;
constexpr size_t kChunkSize = 500;
for (size_t i = 0; i < total_items; i += kChunkSize) {
  size_t end = (std::min)(i + kChunkSize, total_items);
  queue.Push(i, end);
}

// Launch threads
for (int t = 0; t < thread_count; ++t) {
  futures.push_back(std::async(std::launch::async, [&]() {
    std::vector<SearchResultData> local_results;
    std::pair<size_t, size_t> task;
    
    while (queue.Pop(task)) { // Try to get work from front
      ProcessChunk(task.first, task.second, local_results);
    }
    
    // If queue empty, try stealing from other threads
    while (queue.Steal(task)) {
      ProcessChunk(task.first, task.second, local_results);
    }
    
    return local_results;
  }));
}
```

### Benefits

- **Optimal load balancing**: Idle threads automatically steal work
- **High CPU utilization**: No threads sit idle
- **Handles all variability**: Adapts to match count, complexity, cache effects

### Trade-offs

- **More complex**: Requires queue implementation
- **Mutex overhead**: Lock contention (but minimal with small chunks)
- **More code**: ~100-150 lines of additional code

### Performance Gain

**Expected: 30-50% improvement** when imbalance is significant

### Note

For production, consider using existing libraries:
- **Intel TBB** `parallel_for` (work-stealing built-in)
- **OpenMP** (simpler, but less control)

---

## Option 3: Hybrid: Initial Chunks + Dynamic Assignment

### Concept

Start with larger initial chunks (for cache locality), but allow threads to grab additional small chunks when they finish early.

### Implementation

```cpp
// Initial chunks (larger, for cache locality)
size_t initial_chunk_size = (total_items + thread_count - 1) / thread_count;

// Small chunks for dynamic assignment
constexpr size_t kSmallChunkSize = 500;
std::atomic<size_t> next_dynamic_chunk{initial_chunk_size * thread_count};

for (int t = 0; t < thread_count; ++t) {
  futures.push_back(std::async(std::launch::async, [this, t, initial_chunk_size, ...]() {
    std::vector<SearchResultData> local_results;
    
    // Process initial chunk
    size_t start = t * initial_chunk_size;
    size_t end = (std::min)(start + initial_chunk_size, total_items);
    ProcessChunk(start, end, local_results);
    
    // Grab additional small chunks if available
    while (true) {
      size_t chunk_start = next_dynamic_chunk.fetch_add(kSmallChunkSize, std::memory_order_relaxed);
      if (chunk_start >= total_items) break;
      
      size_t chunk_end = (std::min)(chunk_start + kSmallChunkSize, total_items);
      ProcessChunk(chunk_start, chunk_end, local_results);
    }
    
    return local_results;
  }));
}
```

### Benefits

- **Best of both worlds**: Cache-friendly initial chunks + dynamic balancing
- **Simple**: Similar complexity to Option 1
- **Adaptive**: Fast threads help slow threads automatically

### Trade-offs

- **Slightly more complex** than pure dynamic
- **Tuning needed**: Balance initial chunk size vs dynamic chunk size

### Performance Gain

**Expected: 25-45% improvement**

---

## Option 4: Result-Count-Based Early Help (Advanced)

### Concept

If a thread finds many results early (indicating it's in a "hot" region), it can signal other threads to help, or it can stop early and help others.

### Implementation Complexity

**High** - Requires:
- Inter-thread communication
- Result counting during processing
- Dynamic work redistribution
- More complex synchronization

### When to Consider

Only if other options don't provide enough improvement and imbalance is severe (>3x).

---

## Recommendation

### Start with Option 1: Small Chunks with Atomic Counter

**Why:**
1. **Simplest to implement** (~20-30 lines of code changes)
2. **Effective** for unpredictable work (20-40% improvement)
3. **Low risk** (minimal changes to existing code)
4. **Easy to tune** (just adjust chunk size)

**Implementation Steps:**
1. Replace static chunk calculation with atomic counter
2. Change loop to `while (true)` with atomic `fetch_add`
3. Test with different chunk sizes (500, 1000, 2000)
4. Measure improvement

**If Option 1 isn't enough**, then consider:
- **Option 2** (Work-stealing) for maximum performance
- **Option 3** (Hybrid) for better cache behavior

---

## Implementation Details for Option 1

### Code Changes Required

**FileIndex.cpp** - `SearchAsyncWithData()` method:

```cpp
// OLD: Static chunking
chunk_size = (total_items + thread_count - 1) / thread_count;
for (int t = 0; t < thread_count; ++t) {
  size_t start_index = t * chunk_size;
  size_t end_index = (std::min)(start_index + chunk_size, total_items);
  // ... launch with fixed start/end
}

// NEW: Dynamic chunking
constexpr size_t kSmallChunkSize = 500;
std::atomic<size_t> next_chunk_start{0};

for (int t = 0; t < thread_count; ++t) {
  futures.push_back(std::async(std::launch::async, [this, &next_chunk_start, total_items, ...]() {
    std::vector<SearchResultData> local_results;
    
    while (true) {
      size_t start = next_chunk_start.fetch_add(kSmallChunkSize, std::memory_order_relaxed);
      if (start >= total_items) break;
      
      size_t end = (std::min)(start + kSmallChunkSize, total_items);
      
      // Existing search logic here (same as before, just with dynamic start/end)
      // ...
    }
    
    return local_results;
  }));
}
```

### Chunk Size Tuning

Start with `kSmallChunkSize = 500`, then test:
- **500**: Good for most cases
- **1000**: If total_items is very large (>1M)
- **2000**: If overhead is noticeable

**Rule of thumb**: `chunk_size = total_items / (thread_count * 4)` to `total_items / (thread_count * 8)`

### Performance Measurement

After implementation, measure:
- **Time imbalance ratio** (should decrease)
- **Total search time** (should decrease)
- **CPU utilization** (should increase)

---

## Comparison Table

| Option | Complexity | Performance Gain | Implementation Effort | Risk |
|--------|-----------|------------------|---------------------|------|
| **Option 1: Atomic Counter** | Low | 20-40% | Low (30 lines) | Low |
| **Option 2: Work-Stealing** | Medium | 30-50% | Medium (100 lines) | Medium |
| **Option 3: Hybrid** | Low-Medium | 25-45% | Medium (50 lines) | Low |
| **Option 4: Result-Based** | High | 40-60% | High (200+ lines) | High |

---

## Next Steps

1. **Implement Option 1** (atomic counter with small chunks)
2. **Test with different chunk sizes** (500, 1000, 2000)
3. **Measure improvement** using existing timing infrastructure
4. **If needed**, upgrade to Option 2 or 3

The key insight is: **Since results are unpredictable, we need dynamic assignment, not static pre-allocation.**
