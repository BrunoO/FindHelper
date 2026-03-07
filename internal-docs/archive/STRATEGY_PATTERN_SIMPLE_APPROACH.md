# Simple Strategy Pattern Implementation for Load Balancing

## Quick Start: Minimal Implementation

For rapid experimentation, here's a simpler approach that requires minimal refactoring:

### Option 1: Enum-Based Strategy Selection (Simplest)

Add an enum to `FileIndex.h`:

```cpp
enum class LoadBalancingStrategy {
  Static,    // Original static chunking
  Hybrid,    // Initial + dynamic chunks (current)
  Dynamic    // Pure dynamic chunking
};
```

Add a static method to `FileIndex`:

```cpp
static LoadBalancingStrategy GetLoadBalancingStrategy() {
  // Read from settings (or use default)
  AppSettings settings;
  LoadSettings(settings);
  
  if (settings.loadBalancingStrategy == "static") return LoadBalancingStrategy::Static;
  if (settings.loadBalancingStrategy == "dynamic") return LoadBalancingStrategy::Dynamic;
  return LoadBalancingStrategy::Hybrid; // default
}
```

Modify `SearchAsyncWithData` to branch on strategy:

```cpp
LoadBalancingStrategy strategy = GetLoadBalancingStrategy();

if (strategy == LoadBalancingStrategy::Static) {
  // Original static chunking code
} else if (strategy == LoadBalancingStrategy::Hybrid) {
  // Current hybrid implementation
} else if (strategy == LoadBalancingStrategy::Dynamic) {
  // Pure dynamic implementation
}
```

**Pros:**
- ✅ Minimal code changes
- ✅ Easy to implement
- ✅ Quick to test

**Cons:**
- ⚠️ Code duplication (each strategy has similar code)
- ⚠️ Harder to add new strategies

### Option 2: Strategy Pattern with Shared Helper (Recommended)

This approach extracts common code into helper functions, reducing duplication:

```cpp
// In FileIndex.h
enum class LoadBalancingStrategy { Static, Hybrid, Dynamic };

// Helper function for processing a chunk (shared by all strategies)
void ProcessChunkRange(size_t start, size_t end, ...);

// Strategy-specific chunk assignment
void AssignChunksStatic(...);
void AssignChunksHybrid(...);
void AssignChunksDynamic(...);
```

**Pros:**
- ✅ Less code duplication
- ✅ Easier to maintain
- ✅ Still simple to implement

**Cons:**
- ⚠️ Requires extracting helper functions

---

## Recommended Implementation Path

### Phase 1: Add Settings Support (✅ Done)

- [x] Add `loadBalancingStrategy` to `AppSettings`
- [x] Add load/save support in `Settings.cpp`

### Phase 2: Add Strategy Selection (Simple)

Add to `FileIndex.h`:

```cpp
enum class LoadBalancingStrategy {
  Static = 0,
  Hybrid = 1,
  Dynamic = 2
};

static LoadBalancingStrategy GetLoadBalancingStrategy();
```

Add to `FileIndex.cpp`:

```cpp
LoadBalancingStrategy FileIndex::GetLoadBalancingStrategy() {
  AppSettings settings;
  LoadSettings(settings);
  
  if (settings.loadBalancingStrategy == "static") {
    return LoadBalancingStrategy::Static;
  } else if (settings.loadBalancingStrategy == "dynamic") {
    return LoadBalancingStrategy::Dynamic;
  }
  return LoadBalancingStrategy::Hybrid; // default
}
```

### Phase 3: Extract Helper Functions

Extract the `ProcessChunkRange` lambda into a helper function that can be reused:

```cpp
// Helper to process a chunk range (extracted from lambda)
static void ProcessChunkRangeHelper(
    const FileIndex* file_index,
    size_t chunk_start, size_t chunk_end,
    // ... all the parameters ...
    std::vector<SearchResultData>& local_results);
```

### Phase 4: Add Strategy Branching

In `SearchAsyncWithData`, add branching:

```cpp
LoadBalancingStrategy strategy = GetLoadBalancingStrategy();

if (strategy == LoadBalancingStrategy::Static) {
  // Static chunking: original code
  chunk_size = (total_items + thread_count - 1) / thread_count;
  for (int t = 0; t < thread_count; ++t) {
    // ... static assignment ...
  }
} else if (strategy == LoadBalancingStrategy::Hybrid) {
  // Hybrid: current implementation
  // ... existing hybrid code ...
} else if (strategy == LoadBalancingStrategy::Dynamic) {
  // Dynamic: pure dynamic chunking
  constexpr size_t kSmallChunkSize = 500;
  std::atomic<size_t> next_chunk{0};
  // ... dynamic assignment ...
}
```

### Phase 5: Add Logging

Log which strategy is being used:

```cpp
LOG_INFO_BUILD("Using load balancing strategy: " << 
  (strategy == LoadBalancingStrategy::Static ? "static" :
   strategy == LoadBalancingStrategy::Hybrid ? "hybrid" : "dynamic"));
```

---

## Usage for Experiments

### Experiment 1: Static Chunking

Edit `settings.json`:
```json
{
  "loadBalancingStrategy": "static"
}
```

Run search, check logs for:
- Strategy name: "static"
- Per-thread timing
- Load balance metrics

### Experiment 2: Hybrid Chunking

Edit `settings.json`:
```json
{
  "loadBalancingStrategy": "hybrid"
}
```

Run search, compare with Experiment 1.

### Experiment 3: Dynamic Chunking

Edit `settings.json`:
```json
{
  "loadBalancingStrategy": "dynamic"
}
```

Run search, compare with Experiments 1 & 2.

---

## Adding New Strategies

To add a new strategy (e.g., "byte-based"):

1. **Add to enum:**
```cpp
enum class LoadBalancingStrategy {
  Static,
  Hybrid,
  Dynamic,
  ByteBased  // New
};
```

2. **Add to GetLoadBalancingStrategy():**
```cpp
if (settings.loadBalancingStrategy == "byte-based") {
  return LoadBalancingStrategy::ByteBased;
}
```

3. **Add branch in SearchAsyncWithData:**
```cpp
else if (strategy == LoadBalancingStrategy::ByteBased) {
  // Byte-based chunking implementation
}
```

4. **Update settings validation:**
```cpp
if (strategy == "static" || strategy == "hybrid" || 
    strategy == "dynamic" || strategy == "byte-based") {
  out.loadBalancingStrategy = strategy;
}
```

---

## Comparison: Full Strategy Pattern vs Simple Approach

| Aspect | Full Strategy Pattern | Simple Enum Approach |
|--------|----------------------|---------------------|
| **Complexity** | High (requires refactoring) | Low (minimal changes) |
| **Code Duplication** | None (shared interface) | Some (similar code in branches) |
| **Extensibility** | Easy (new class) | Easy (new enum + branch) |
| **Time to Implement** | 2-3 days | 2-3 hours |
| **Maintainability** | High (isolated strategies) | Medium (branched code) |
| **Testability** | High (isolated classes) | Medium (branched code) |

**Recommendation**: Start with **Simple Enum Approach** for rapid experimentation. Refactor to full Strategy Pattern later if needed.

---

## Next Steps

1. ✅ Settings integration (done)
2. ⏳ Add enum and GetLoadBalancingStrategy() method
3. ⏳ Extract ProcessChunkRange helper function
4. ⏳ Add strategy branching in SearchAsyncWithData
5. ⏳ Add logging for strategy name
6. ⏳ Test each strategy

This approach gets you experimenting quickly while keeping the door open for a full Strategy Pattern implementation later.
