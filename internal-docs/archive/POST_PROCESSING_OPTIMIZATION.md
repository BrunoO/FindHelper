# Post-Processing Optimization Analysis

## Current Bottleneck

**Sequential post-processing takes 3x longer than parallel search**

**What happens:**
- For each candidate ID (could be thousands):
  1. `GetEntry(id)` - Acquires/releases `shared_lock` on FileIndex (expensive!)
  2. Copies entire `FileEntry` struct (contains strings - expensive!)
  3. Folders-only filter check
  4. `CreateSearchResult()` - Creates SearchResult object

**Problems:**
- Each `GetEntry()` call = 1 lock acquisition/release
- Each `GetEntry()` call = 1 full `FileEntry` copy (strings included)
- Sequential processing = no parallelization

## Optimization Options

### Option 1: Batch GetEntry() with ForEachEntryByIds() (Recommended)

**Use existing `ForEachEntryByIds()` method:**
- Holds **single shared_lock** for all lookups
- No per-call lock overhead
- Still copies FileEntry, but reduces lock contention

**Implementation:**
```cpp
std::vector<SearchResult> results;
results.reserve(candidateIds.size() / 2);

m_fileIndex.ForEachEntryByIds(candidateIds, [&](uint64_t id, const FileEntry &entry) {
  // Apply folders-only filter
  if (params.foldersOnly && !entry.isDirectory) {
    return true; // Skip, continue
  }
  
  // Create result
  results.push_back(CreateSearchResult(id, entry));
  return true; // Continue
});
```

**Benefits:**
- Single lock acquisition (not thousands)
- Simple to implement
- Uses existing optimized method

**Performance:** ~2-3x faster (eliminates lock overhead)

### Option 2: Parallel Post-Processing

**Split candidateIds into chunks, process in parallel:**
- Each thread processes a chunk
- Each thread calls `ForEachEntryByIds()` on its chunk
- Merge results at the end

**Implementation:**
```cpp
// Split into chunks
size_t chunkSize = (candidateIds.size() + threadCount - 1) / threadCount;
std::vector<std::future<std::vector<SearchResult>>> futures;

for (int t = 0; t < threadCount; ++t) {
  size_t start = t * chunkSize;
  size_t end = std::min(start + chunkSize, candidateIds.size());
  
  futures.push_back(std::async(std::launch::async, [&, start, end]() {
    std::vector<uint64_t> chunk(candidateIds.begin() + start, 
                                 candidateIds.begin() + end);
    std::vector<SearchResult> localResults;
    
    m_fileIndex.ForEachEntryByIds(chunk, [&](uint64_t id, const FileEntry &entry) {
      if (params.foldersOnly && !entry.isDirectory) {
        return true;
      }
      localResults.push_back(CreateSearchResult(id, entry));
      return true;
    });
    
    return localResults;
  }));
}

// Gather results
for (auto &f : futures) {
  auto part = f.get();
  results.insert(results.end(), part.begin(), part.end());
}
```

**Benefits:**
- Parallel processing (uses multiple cores)
- Each thread uses `ForEachEntryByIds()` (single lock per thread)
- Significant speedup for large result sets

**Performance:** ~3-5x faster (parallelization + batching)

**Considerations:**
- Need to handle cancellation checks
- Thread overhead for small result sets
- Memory for multiple result vectors

### Option 3: Store isDirectory in ContiguousStringBuffer

**Add `isDirectory` flag to ContiguousStringBuffer:**
- Eliminates need for `GetEntry()` for folders-only filter
- Only need `GetEntry()` for `CreateSearchResult()`
- Reduces number of lookups

**Trade-off:**
- More memory (~1 byte per entry)
- Still need `GetEntry()` for creating results

**Performance:** ~1.5-2x faster (fewer GetEntry calls, but still sequential)

### Option 4: Hybrid - Batch + Parallel

**Combine Option 1 and Option 2:**
- Use `ForEachEntryByIds()` for batching (single lock)
- Process chunks in parallel
- Best of both worlds

**Performance:** ~4-6x faster

## Recommendation

### ✅ **Implement Option 2: Parallel Post-Processing with Batching**

**Rationale:**
1. **Best performance:** Parallelization + batching = maximum speedup
2. **Uses existing infrastructure:** `ForEachEntryByIds()` already exists
3. **Scales well:** Performance improves with more cores
4. **Reasonable complexity:** Straightforward to implement

**Implementation steps:**
1. Split `candidateIds` into chunks
2. Launch parallel tasks (one per chunk)
3. Each task uses `ForEachEntryByIds()` on its chunk
4. Gather results from all threads
5. Handle cancellation checks

**Expected performance:**
- Current: ~150ms for 10K candidates (3x search time)
- After: ~30-50ms for 10K candidates (0.6-1x search time)
- **Speedup: 3-5x**
