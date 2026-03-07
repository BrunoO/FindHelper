# Dynamic Strategy Tuning Guide

## Why Dynamic Strategy Might Be Disappointing

The pure Dynamic strategy has several potential issues:

1. **No Initial Chunks** - All threads start from 0, competing for the same atomic counter
2. **Fixed Small Chunk Size (500)** - May be suboptimal for different dataset sizes
3. **Cache Locality Loss** - No large initial chunks means less predictable memory access
4. **Atomic Contention** - All threads competing for the same counter can cause cache line bouncing
5. **No Work Stealing** - Simple atomic counter doesn't adapt to thread speed differences

---

## Tuning Parameters

### 1. **Adaptive Chunk Size** ⭐ HIGH IMPACT

**Current:** Fixed 500 items  
**Problem:** Too small for large datasets (overhead), too large for small datasets (less balancing)

**Solution:** Make chunk size adaptive based on dataset size:

```cpp
// Adaptive chunk size calculation
size_t CalculateDynamicChunkSize(size_t total_items, int thread_count) {
  // Base size: 500 items
  size_t base_chunk_size = 500;
  
  // Scale up for large datasets (fewer atomic operations)
  if (total_items > 1000000) {
    base_chunk_size = 1000;  // 1M+ items: larger chunks
  } else if (total_items > 100000) {
    base_chunk_size = 750;   // 100K-1M items: medium chunks
  }
  
  // Scale down for small datasets (better balancing)
  if (total_items < 10000) {
    base_chunk_size = 250;   // <10K items: smaller chunks
  }
  
  // Ensure chunk size is reasonable relative to total work
  // Rule: Each thread should get at least 4-8 chunks
  size_t min_chunks_per_thread = 4;
  size_t max_chunk_size = total_items / (thread_count * min_chunks_per_thread);
  if (max_chunk_size < base_chunk_size) {
    base_chunk_size = (std::max)(size_t(100), max_chunk_size);
  }
  
  return base_chunk_size;
}
```

**Expected Impact:** 5-15% performance improvement

---

### 2. **Batch Chunk Grabbing** ⭐ MEDIUM IMPACT

**Current:** One chunk at a time (500 items)  
**Problem:** More atomic operations = more contention

**Solution:** Grab multiple chunks at once:

```cpp
// Grab 2-4 chunks at once to reduce atomic contention
constexpr size_t kBatchSize = 2;  // Grab 2 chunks per atomic operation
size_t batch_chunk_size = kSmallChunkSize * kBatchSize;

size_t chunk_start = next_chunk_ref.get().fetch_add(batch_chunk_size, std::memory_order_relaxed);
if (chunk_start >= total_items) break;

// Process all chunks in the batch
for (size_t batch = 0; batch < kBatchSize && chunk_start < total_items; ++batch) {
  size_t chunk_end = (std::min)(chunk_start + kSmallChunkSize, total_items);
  // Process chunk...
  chunk_start = chunk_end;
}
```

**Expected Impact:** 3-8% performance improvement (reduces atomic contention)

---

### 3. **Hybrid-Like Initial Distribution** ⭐ HIGH IMPACT

**Current:** All threads start from 0  
**Problem:** No initial work distribution, all compete immediately

**Solution:** Give each thread an initial chunk, then go dynamic:

```cpp
// Give each thread an initial chunk (like Hybrid strategy)
size_t initial_chunk_size = safe_total_items / thread_count;
size_t initial_chunks_end = (std::min)(initial_chunk_size * thread_count, safe_total_items);

// Start dynamic chunking after initial chunks
std::atomic<size_t> next_chunk{initial_chunks_end};

for (int t = 0; t < thread_count; ++t) {
  futures.push_back(thread_pool.Enqueue([this, t, initial_chunk_size, ...]() {
    // Process initial chunk first (cache-friendly)
    size_t start = t * initial_chunk_size;
    size_t end = (std::min)(start + initial_chunk_size, initial_chunks_end);
    ProcessChunkRangeForSearch(start, end, local_results, ...);
    
    // Then grab dynamic chunks
    while (true) {
      size_t chunk_start = next_chunk.fetch_add(kSmallChunkSize, ...);
      if (chunk_start >= safe_total_items) break;
      // Process dynamic chunk...
    }
  }));
}
```

**Expected Impact:** 10-25% performance improvement (better cache locality + load balancing)

---

### 4. **Memory Ordering Optimization** ⚠️ LOW IMPACT

**Current:** `memory_order_relaxed`  
**Problem:** May cause cache line bouncing with many threads

**Solution:** Try `memory_order_acquire` for better cache behavior:

```cpp
size_t chunk_start = next_chunk_ref.get().fetch_add(kSmallChunkSize, std::memory_order_acquire);
```

**Expected Impact:** 1-3% improvement (may help with cache coherency)

**Note:** Test both - `relaxed` is usually faster, but `acquire` might help with contention

---

### 5. **Thread-Local Chunk Buffers** ⚠️ EXPERIMENTAL

**Concept:** Each thread grabs multiple chunks into a local buffer, processes them, then grabs more

**Implementation:**
```cpp
constexpr size_t kLocalBufferSize = 4;  // Buffer 4 chunks locally
std::vector<std::pair<size_t, size_t>> local_chunks;
local_chunks.reserve(kLocalBufferSize);

while (true) {
  // Fill local buffer
  while (local_chunks.size() < kLocalBufferSize) {
    size_t chunk_start = next_chunk.fetch_add(kSmallChunkSize, ...);
    if (chunk_start >= total_items) break;
    size_t chunk_end = (std::min)(chunk_start + kSmallChunkSize, total_items);
    local_chunks.push_back({chunk_start, chunk_end});
  }
  
  if (local_chunks.empty()) break;
  
  // Process all chunks in buffer
  for (auto [start, end] : local_chunks) {
    ProcessChunkRangeForSearch(start, end, local_results, ...);
  }
  local_chunks.clear();
}
```

**Expected Impact:** 2-5% improvement (reduces atomic operations)

---

## Recommended Tuning Priority

### Priority 1: Make Dynamic More Like Hybrid ⭐⭐⭐
**Why:** Best of both worlds - cache locality + load balancing  
**Effort:** Medium  
**Impact:** High (10-25% improvement)

### Priority 2: Adaptive Chunk Size ⭐⭐
**Why:** Fixes suboptimal chunk size for different datasets  
**Effort:** Low  
**Impact:** Medium (5-15% improvement)

### Priority 3: Batch Chunk Grabbing ⭐
**Why:** Reduces atomic contention  
**Effort:** Low  
**Impact:** Medium (3-8% improvement)

---

## Quick Win: Hybrid-Like Dynamic

The easiest and most effective tuning is to make Dynamic strategy work like Hybrid:

1. **Give each thread an initial chunk** (like Hybrid)
2. **Then use dynamic chunking** for remaining work
3. **Keep adaptive chunk size** for optimal performance

This gives you:
- ✅ Better cache locality (initial chunks)
- ✅ Load balancing (dynamic chunks)
- ✅ Minimal overhead (only when needed)

---

## Testing Recommendations

After tuning, test with:

1. **Small dataset** (<10K items): Should see better balancing
2. **Medium dataset** (100K-1M items): Should see optimal chunk size
3. **Large dataset** (>1M items): Should see reduced overhead

Compare metrics:
- Load balance ratio (should improve)
- Total search time (should decrease)
- Dynamic chunks count (should be reasonable)

---

## Implementation Notes

When implementing, consider:

1. **Make chunk size configurable** via settings (for experimentation)
2. **Add logging** to show chunk size used
3. **Profile** to measure actual improvement
4. **A/B test** against current Dynamic and Hybrid strategies

---

## Expected Results

After tuning, Dynamic strategy should:
- **Match or exceed Hybrid performance** for imbalanced workloads
- **Have minimal overhead** (<0.1%) compared to Static
- **Scale better** with dataset size
- **Be more predictable** in performance
