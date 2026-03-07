
# Dynamic Strategy Improvements

## Best Chunk Size Analysis

The current default `dynamic_chunk_size` is **1000 items**.

### The Math
*   **Cost of Atomic Op**: ~20-50ns (uncontended).
*   **Cost of Search per Item**: 
    *   Fastest case (simple string match): ~10ns.
    *   Typical case (substring search): ~100ns.
    *   Slow case (complex regex): >1000ns.

**At 1000 items/chunk**:
*   **Work per chunk**: $1000 \times 100\text{ns} = 100,000\text{ns}$ ($100\mu\text{s}$).
*   **Overhead per chunk**: $50\text{ns}$.
*   **Ratio**: $0.05\%$ overhead.

**Conclusion**: The default of 1000 is **extremely safe**. You could lower it to **100** without noticeable performance degradation (0.5% overhead) to improve load balancing granularity on smaller indices.

## Proposed Improvements

### 1. Adaptive Chunk Sizing (Guided Scheduling)
Instead of a fixed size, use a decaying chunk size.
*   **Start Large**: Reduces overhead when there is a lot of work.
*   **End Small**: Ensures the last few pieces of work are granular enough to balance evenly across threads.

**Algorithm:**
```cpp
// "Guided" scheduling idea
// Takes 1/Nth of remaining work, where N is thread count (or 2*N)
size_t remaining = total_items - current_index;
size_t chunk_size = std::max(min_chunk, remaining / (2 * thread_count));
```
*   **Benefit**: Best of both worlds. Low overhead at start, perfect balancing at end.
*   **Complexity**: Minimal implementation effort.

### 2. Cache Line Padding
The atomic counter `next_chunk_start` is currently a `std::shared_ptr<std::atomic<size_t>>`.
*   **Issue**: It likely sits on a heap cache line near other data.
*   **Fix**: Pad the atomic to strictly occupy its own cache line (64 bytes) to prevent "false sharing" if adjacent data is modified by other threads.

```cpp
struct alignas(64) PaddedAtomic {
    std::atomic<size_t> value;
    char padding[64 - sizeof(std::atomic<size_t>)]; // Ensure full cache line
};
```
*   **Benefit**: Reduces cache coherency traffic on high-core-count CPUs.

### 3. Batched Atomic Claiming
Currently, every thread fights for the *next* chunk.
*   **Optimization**: A thread could claim `K` chunks at once (e.g., 4 chunks) but process them one by one.
*   **Benefit**: Reduces contention on the atomic variable by factor `K`.
*   **Trade-off**: Slightly worse load balancing if `K` is too big.

## Recommendation

**Adaptive Chunk Sizing** is the highest-value improvement. It naturally solves the "what is the best chunk size" question by dynamically finding the right balance during execution.

For now, reducing the default to **250** or **500** is a safe, immediate optimization to improve responsiveness for smaller queries without complex code changes.
