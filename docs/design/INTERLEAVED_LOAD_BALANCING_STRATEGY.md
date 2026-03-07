# Interleaved Load Balancing Strategy

## Overview

The **Interleaved Load Balancing Strategy** is one of four load balancing strategies available in the USN_WINDOWS project for distributing parallel file search work across multiple threads. It provides a deterministic, round-robin approach to work distribution that balances load effectively without requiring atomic operations.

## How It Works

### Algorithm

1. **Divide work into sub-chunks:**
   - Uses `dynamic_chunk_size` from settings (defaults to 256 if < 100)
   - Total items are divided into `num_sub_chunks = ceil(total_items / sub_chunk_size)`

2. **Round-robin assignment:**
   - Thread `t` processes chunks: `t`, `t + thread_count`, `t + 2*thread_count`, ...
   - Each thread processes its assigned chunks sequentially

3. **Example with 3 threads and 9 chunks:**
   ```
   Thread 0: chunks 0, 3, 6
   Thread 1: chunks 1, 4, 7
   Thread 2: chunks 2, 5, 8
   ```

### Implementation

The strategy is implemented in `InterleavedChunkingStrategy::LaunchSearchTasks()` in `LoadBalancingStrategy.cpp`. Key implementation details:

- **Sub-chunk size calculation:**
  ```cpp
  size_t sub_chunk_size = (context.dynamic_chunk_size >= 100) 
      ? context.dynamic_chunk_size 
      : 256;
  ```

- **Number of sub-chunks:**
  ```cpp
  size_t num_sub_chunks = (total_items + sub_chunk_size - 1) / sub_chunk_size;
  ```

- **Interleaved pattern loop:**
  ```cpp
  for (size_t chunk_idx = t; chunk_idx < num_sub_chunks; 
       chunk_idx += thread_count) {
      // Process chunk at chunk_idx
  }
  ```

## Characteristics

### Advantages

- ✅ **Deterministic:** No atomic operations needed - chunk assignment is calculated upfront
- ✅ **Good load balance:** Distributes work evenly across threads, even with variable processing times
- ✅ **Simple:** Straightforward round-robin assignment logic
- ✅ **Handles variable work:** Effective when processing times vary between items

### Disadvantages

- ❌ **Less cache locality:** Chunks are spread across the data range (unlike static chunking)
- ❌ **More chunks:** Creates more sub-chunks than static chunking, potentially increasing overhead

## When to Use

The interleaved strategy is best suited for:

- Scenarios where you want **deterministic behavior** without atomic operations
- Work items with **moderate variance** in processing time
- Situations where you can **tune `dynamic_chunk_size`** for optimal performance
- When **load balance is more important** than cache locality

## Comparison with Other Strategies

| Strategy | Cache Locality | Load Balance | Complexity | Atomic Operations |
|----------|---------------|--------------|------------|------------------|
| **Static** | ✅ Excellent | ❌ Poor (with variable work) | ✅ Simple | ❌ None |
| **Dynamic** | ❌ Poor | ✅ Excellent | ⚠️ Moderate | ✅ Required |
| **Hybrid** | ✅ Good | ✅ Excellent | ⚠️ Moderate | ✅ Required |
| **Interleaved** | ⚠️ Moderate | ✅ Good | ✅ Simple | ❌ None |

### Detailed Comparison

- **vs. Static:** Better load balance, but less cache locality
- **vs. Dynamic:** Simpler (no atomics), but potentially less optimal load balance
- **vs. Hybrid:** Simpler and deterministic, but hybrid may perform better overall

## Configuration

The interleaved strategy uses the `dynamic_chunk_size` parameter from `SearchContext`:

- **Default:** 256 items per sub-chunk (if `dynamic_chunk_size < 100`)
- **Tunable:** Can be adjusted via settings to optimize for specific workloads
- **Recommendation:** Larger chunks (500-1000) for better cache locality, smaller chunks (256-512) for better load balance

## Code References

- **Header:** `LoadBalancingStrategy.h` (lines 100-139)
- **Implementation:** `LoadBalancingStrategy.cpp` (lines 567-713)
- **Factory:** `CreateLoadBalancingStrategy()` creates `InterleavedChunkingStrategy` when strategy name is `"interleaved"`

## Usage Example

```cpp
// Create the strategy
auto strategy = CreateLoadBalancingStrategy("interleaved");

// Launch search tasks
auto futures = strategy->LaunchSearchTasks(
    file_index,
    total_items,
    thread_count,
    context,
    &thread_timings
);

// Wait for results
for (auto& future : futures) {
    auto results = future.get();
    // Process results...
}
```

## Performance Considerations

1. **Chunk Size Tuning:** The `dynamic_chunk_size` parameter significantly affects performance:
   - Too small: High overhead from many small chunks
   - Too large: Poor load balance with variable work
   - Optimal: Depends on workload characteristics

2. **Cache Effects:** Since chunks are interleaved, cache locality is reduced compared to static chunking. This may impact performance on workloads with strong spatial locality.

3. **Thread Synchronization:** No atomic operations are needed, reducing synchronization overhead compared to dynamic strategies.

## Testing

The interleaved strategy is tested in `FileIndexSearchStrategyTests.cpp`:

- Basic functionality verification
- Result correctness across all strategies
- Pattern matching scenarios

See `tests/FileIndexSearchStrategyTests.cpp` for test implementation details.

## Related Documentation

- **Load Balancing Overview:** See `LoadBalancingStrategy.h` for strategy pattern documentation
- **Other Strategies:**
  - Static: `LoadBalancingStrategy.cpp` (lines 110-236)
  - Hybrid: `LoadBalancingStrategy.cpp` (lines 238-422)
  - Dynamic: `LoadBalancingStrategy.cpp` (lines 424-565)
- **Settings:** `Settings.h` for configuration options

