# Byte-Based Chunking Explained

## Understanding the Problem

### Current Approach: Item-Based Chunking

**How it works:**
- Divides work by **counting items** (paths)
- Each thread gets the same **number of paths** to process
- Example: 10,000 paths, 4 threads → each thread gets 2,500 paths

**The Problem:**
Paths have **dramatically different lengths**, so the same number of paths can represent vastly different amounts of work.

### Real-World Example

Imagine you have 10,000 paths to search:

```
Thread 1 gets paths 0-2,499:
  - "C:\a\b.txt" (12 bytes)
  - "C:\x\y.txt" (12 bytes)
  - "C:\z\w.txt" (12 bytes)
  - ... (2,500 short paths, ~30KB total)

Thread 2 gets paths 2,500-4,999:
  - "C:\Users\John\Documents\Projects\MyProject\src\main\java\com\example\app\models\User.java" (95 bytes)
  - "C:\Users\John\Documents\Projects\MyProject\src\main\java\com\example\app\controllers\ApiController.java" (105 bytes)
  - ... (2,500 long paths, ~250KB total)
```

**What happens:**
- Thread 1: Processes 2,500 short paths quickly → finishes in **5 seconds**
- Thread 2: Processes 2,500 long paths slowly → finishes in **50 seconds**

**Result:** Thread 1 sits idle for 45 seconds while Thread 2 finishes. **Total time: 50 seconds** (dominated by the slowest thread).

---

## Byte-Based Chunking Solution

### Concept

Instead of dividing by **item count**, divide by **total byte size** of the data being processed.

**Key Insight:** The work required to search/match a path is proportional to:
- String length (more bytes = more characters to process)
- Memory access (more bytes = more cache lines to read)
- Pattern matching (longer strings take more time for regex/glob)

### How It Works

**Step 1: Calculate total bytes**
```cpp
size_t total_bytes = path_storage_.size();  // Total size of all paths combined
```

**Step 2: Calculate bytes per thread**
```cpp
size_t bytes_per_thread = total_bytes / thread_count;
```

**Step 3: Find index boundaries that correspond to byte boundaries**
```cpp
std::vector<size_t> byte_boundaries;
byte_boundaries.push_back(0);  // Start at index 0

size_t current_bytes = 0;
size_t current_index = 0;

for (int t = 1; t < thread_count; ++t) {
  size_t target_bytes = t * bytes_per_thread;
  
  // Find the index where cumulative bytes >= target_bytes
  while (current_index < total_items && current_bytes < target_bytes) {
    // Calculate length of current path
    size_t path_length;
    if (current_index + 1 < path_offsets_.size()) {
      path_length = path_offsets_[current_index + 1] - path_offsets_[current_index];
    } else {
      // Last path: length to end of storage
      path_length = path_storage_.size() - path_offsets_[current_index];
    }
    
    current_bytes += path_length;
    current_index++;
  }
  
  byte_boundaries.push_back(current_index);
}
byte_boundaries.push_back(total_items);  // End at last index
```

**Step 4: Assign chunks based on byte boundaries**
```cpp
for (int t = 0; t < thread_count; ++t) {
  size_t start_index = byte_boundaries[t];
  size_t end_index = byte_boundaries[t + 1];
  // Thread processes [start_index, end_index)
}
```

### Example with Same Data

**10,000 paths, 4 threads, byte-based chunking:**

```
Total bytes: ~500KB
Bytes per thread: ~125KB

Thread 1: Gets paths 0-8,000 (short paths, ~125KB) → 8,000 paths
Thread 2: Gets paths 8,001-9,200 (mixed, ~125KB) → 1,200 paths  
Thread 3: Gets paths 9,201-9,700 (long paths, ~125KB) → 500 paths
Thread 4: Gets paths 9,701-9,999 (long paths, ~125KB) → 299 paths
```

**What happens:**
- Thread 1: Processes 8,000 short paths → finishes in **16 seconds**
- Thread 2: Processes 1,200 mixed paths → finishes in **15 seconds**
- Thread 3: Processes 500 long paths → finishes in **16 seconds**
- Thread 4: Processes 299 long paths → finishes in **15 seconds**

**Result:** All threads finish around the same time! **Total time: ~16 seconds** (vs 50 seconds before).

---

## Why Byte-Based Chunking Works

### 1. **Work is Proportional to Data Size**

The time to process a path depends on:
- **String length**: Searching "abc" vs "very_long_path_name" takes different time
- **Memory access**: Longer strings require more cache line reads
- **Pattern matching**: Regex/glob operations scale with string length

**Example:**
```cpp
// Processing these takes different time:
"a.txt"           → ~0.001ms (5 bytes, simple match)
"very_long_path"  → ~0.005ms (14 bytes, more characters to check)
```

### 2. **Better Load Balancing**

By dividing bytes instead of items:
- Threads get **similar amounts of actual work**
- Fast threads (short paths) get **more items** to compensate
- Slow threads (long paths) get **fewer items** but similar work

### 3. **Handles Real-World Data Skew**

Real file systems have:
- Many short paths (system files, config files)
- Some very long paths (deep directory structures, project files)
- Mixed distributions (not uniform)

Byte-based chunking automatically adapts to this skew.

---

## Implementation Details

### Calculating Path Length

In your codebase, paths are stored in a contiguous buffer:

```cpp
std::vector<char> path_storage_;        // All paths concatenated
std::vector<size_t> path_offsets_;     // Offset for each path
```

**To get path length:**
```cpp
size_t GetPathLength(size_t index) const {
  if (index + 1 < path_offsets_.size()) {
    // Length = next offset - current offset - 1 (for null terminator)
    return path_offsets_[index + 1] - path_offsets_[index] - 1;
  } else {
    // Last path: length to end of storage - 1
    return path_storage_.size() - path_offsets_[index] - 1;
  }
}
```

### Cumulative Byte Calculation

```cpp
// Calculate cumulative bytes up to each index
std::vector<size_t> cumulative_bytes;
cumulative_bytes.reserve(total_items + 1);
cumulative_bytes.push_back(0);

for (size_t i = 0; i < total_items; ++i) {
  size_t path_length = GetPathLength(i);
  cumulative_bytes.push_back(cumulative_bytes.back() + path_length);
}
```

### Finding Byte Boundaries

```cpp
std::vector<size_t> byte_boundaries;
byte_boundaries.reserve(thread_count + 1);
byte_boundaries.push_back(0);

size_t bytes_per_thread = cumulative_bytes.back() / thread_count;

for (int t = 1; t < thread_count; ++t) {
  size_t target_bytes = t * bytes_per_thread;
  
  // Binary search for index where cumulative_bytes >= target_bytes
  auto it = std::lower_bound(cumulative_bytes.begin(), 
                             cumulative_bytes.end(), 
                             target_bytes);
  size_t index = std::distance(cumulative_bytes.begin(), it);
  byte_boundaries.push_back(index);
}

byte_boundaries.push_back(total_items);
```

---

## Performance Impact

### Expected Improvements

**Scenario 1: Mixed Path Lengths (Typical)**
- **Before**: 30-40% load imbalance → 30-40% performance loss
- **After**: 5-10% load imbalance → **20-35% faster**

**Scenario 2: Highly Skewed (Deep Directories)**
- **Before**: 50-70% load imbalance → 50-70% performance loss  
- **After**: 5-10% load imbalance → **40-60% faster**

**Scenario 3: Uniform Path Lengths (Rare)**
- **Before**: Already balanced
- **After**: Minimal overhead, similar performance

### Why Not 100% Perfect?

Byte-based chunking doesn't account for:
- **Match complexity**: Regex vs simple substring (regex is slower)
- **Cache effects**: Some paths may be in cache, others not
- **CPU branch prediction**: Different patterns affect performance

But it addresses the **biggest source of imbalance** (path length variation).

---

## Trade-offs

### Advantages ✅
- **Simple to implement**: ~50-100 lines of code
- **Low overhead**: One-time calculation (negligible for large searches)
- **Significant improvement**: 20-40% faster in typical scenarios
- **Maintains cache locality**: Still processes contiguous ranges

### Disadvantages ⚠️
- **One-time calculation cost**: Need to compute byte boundaries (but amortized over search)
- **Still static**: Doesn't dynamically redistribute if a thread finishes early
- **Doesn't account for match complexity**: Regex still slower than substring

### When It Helps Most
- **Large searches** (100K+ paths): Overhead is negligible
- **Mixed path lengths**: Real-world file systems
- **String-heavy operations**: Pattern matching, searching

### When It Helps Less
- **Small searches** (<10K paths): Overhead may be noticeable
- **Uniform path lengths**: Less benefit (but no harm)
- **CPU-bound operations**: If work isn't proportional to bytes

---

## Comparison: Item-Based vs Byte-Based

| Aspect | Item-Based (Current) | Byte-Based (Proposed) |
|--------|---------------------|----------------------|
| **Division Method** | Equal item count | Equal byte size |
| **Load Balance** | Poor (30-70% imbalance) | Good (5-10% imbalance) |
| **Implementation** | Simple (1 line) | Medium (~100 lines) |
| **Overhead** | None | Small (one-time calc) |
| **Performance Gain** | Baseline | +20-40% typical |
| **Cache Locality** | Good | Good (same) |
| **Adaptability** | None | None (still static) |

---

## Next Steps

Byte-based chunking is a **quick win** that addresses the main load imbalance issue. After implementing it, you can consider:

1. **Dynamic chunking**: Small chunks assigned as threads finish
2. **Work-stealing**: Idle threads steal work from busy threads
3. **Thread pool**: Reuse threads instead of creating new ones

But byte-based chunking alone should give you **significant improvement** with minimal complexity.
