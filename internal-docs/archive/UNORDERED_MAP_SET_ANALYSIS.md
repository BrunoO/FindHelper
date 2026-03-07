# Unordered Map/Set Usage Analysis

## Overview
This document analyzes all usages of `std::unordered_map` and `std::unordered_set` in the codebase to determine if they remain optimal after recent refactorings.

## Analysis Date
2024-12-19

## Findings Summary
**All current usages of `unordered_map` and `unordered_set` are optimal and should remain unchanged.**

---

## Detailed Analysis

### 1. FileIndex::index_ - `std::unordered_map<uint64_t, FileEntry>`

**Location:** `FileIndex.h:755`

**Usage Pattern:**
- Random lookups by file ID (very frequent - used in almost every method)
- Insertions during indexing
- Deletions during file removal
- Iteration in `GetAllEntries()` and `RecomputeAllPaths()`
- No ordering requirements

**Access Frequency:**
- **Very High**: Used in 20+ methods for lookups
- Lookups are the primary operation (find operations dominate)

**Key Characteristics:**
- Keys: `uint64_t` (excellent hash distribution, no collisions expected)
- Values: `FileEntry` struct (~80-100 bytes)
- Size: Can be very large (millions of entries)

**Analysis:**
✅ **OPTIMAL** - `unordered_map` is the perfect choice:
- O(1) average-case lookup (critical for performance)
- No ordering needed (random iteration is acceptable)
- `uint64_t` keys provide excellent hash distribution
- Memory overhead acceptable for the performance gain
- Alternative (`std::map`) would be O(log n) - significantly slower for millions of entries

**Recommendation:** Keep as-is

---

### 2. StringPool::pool_ - `std::unordered_set<std::string>`

**Location:** `FileIndex.h:30`

**Usage Pattern:**
- String interning: Check if extension string exists, insert if not
- Used to deduplicate extension strings and reduce memory
- No ordering requirements

**Access Frequency:**
- **Low-Medium**: Called during file insertion for each file with an extension
- Small set size (typically < 100 unique extensions)

**Key Characteristics:**
- Keys: `std::string` (extension strings like "txt", "pdf", etc.)
- Size: Small (typically 20-50 unique extensions)

**Analysis:**
✅ **OPTIMAL** - `unordered_set` is appropriate:
- O(1) average-case lookup for existence checks
- Small set size means hash overhead is minimal
- Alternative (sorted `std::vector` with binary search) would be O(log n) and require sorting
- Alternative (`std::set`) would be O(log n) - unnecessary overhead for small sets

**Recommendation:** Keep as-is

---

### 3. ContiguousStringBuffer::id_to_index_ - `std::unordered_map<uint64_t, size_t>`

**Location:** `ContiguousStringBuffer.h:134`

**Usage Pattern:**
- Fast lookup of file ID -> index in parallel arrays
- Used in `Remove()`, `GetPath()`, `GetPaths()`, `InsertLocked()`
- No ordering requirements

**Access Frequency:**
- **High**: Used for every Remove/GetPath operation
- Critical for O(1) index lookup

**Key Characteristics:**
- Keys: `uint64_t` (excellent hash distribution)
- Values: `size_t` (8 bytes)
- Size: Same as number of entries (can be millions)

**Analysis:**
✅ **OPTIMAL** - `unordered_map` is essential:
- O(1) average-case lookup is critical for performance
- Without this map, finding an index would require O(n) linear search through `ids_` vector
- Memory overhead (hash table + pointers) is acceptable for the massive performance gain
- Alternative (`std::map`) would be O(log n) - significantly slower

**Recommendation:** Keep as-is

---

### 4. ContiguousStringBuffer::Search() - extensionSet - `std::unordered_set<std::string>`

**Location:** `ContiguousStringBuffer.cpp:365, 786`

**Usage Pattern:**
- Temporary set created during search for extension filtering
- Used for O(1) lookup to check if an extension is in the allowed list
- Destroyed after search completes

**Access Frequency:**
- **Medium**: Used during parallel search filtering (hot loop)
- Lookups happen millions of times during large searches

**Key Characteristics:**
- Keys: `std::string` (lowercased extensions)
- Size: Small (typically 1-20 extensions)

**Analysis:**
✅ **OPTIMAL** - `unordered_set` is appropriate:
- O(1) average-case lookup is critical in the hot loop
- Even for small sets, hash lookup is faster than linear search through vector
- Alternative (sorted `std::vector` with binary search) would be O(log n) and require sorting
- Alternative (`std::set`) would be O(log n) - unnecessary overhead

**Note:** For very small sets (< 5 extensions), a simple `std::vector` with linear search might be slightly faster due to cache locality, but the difference is negligible and `unordered_set` is more maintainable.

**Recommendation:** Keep as-is

---

### 5. SearchController::currentMetadata - `std::unordered_map<uint64_t, ResultMetadata>`

**Location:** `SearchController.cpp:95`

**Usage Pattern:**
- Temporary map created during result comparison
- Maps file ID to metadata for O(1) lookup during comparison
- Destroyed after comparison completes

**Access Frequency:**
- **Low**: Only created when comparing search results (on result updates)
- Used once per comparison

**Key Characteristics:**
- Keys: `uint64_t` (excellent hash distribution)
- Values: `ResultMetadata` struct (~100 bytes)
- Size: Same as result count (can be thousands)

**Analysis:**
✅ **OPTIMAL** - `unordered_map` is appropriate:
- O(1) average-case lookup for comparing new results with current results
- Alternative (`std::map`) would be O(log n) - unnecessary overhead
- Memory is temporary (destroyed after use)

**Recommendation:** Keep as-is

---

### 6. SearchWorker::pathMap - `std::unordered_map<uint64_t, std::string>`

**Location:** `SearchWorker.cpp:289, 343`

**Usage Pattern:**
- Temporary map created during batch path processing
- Maps file ID to path string for batch lookups
- Used to avoid repeated `GetPath()` calls
- Destroyed after processing completes

**Access Frequency:**
- **Medium**: Used during post-processing of search results
- Created per chunk in parallel processing

**Key Characteristics:**
- Keys: `uint64_t` (excellent hash distribution)
- Values: `std::string` (path strings)
- Size: Same as chunk size (hundreds to thousands)

**Analysis:**
✅ **OPTIMAL** - `unordered_map` is appropriate:
- O(1) average-case lookup for batch path retrieval
- Alternative (`std::map`) would be O(log n) - unnecessary overhead
- Memory is temporary (destroyed after use)
- Enables efficient batch processing without repeated lookups

**Recommendation:** Keep as-is

---

## Alternative Data Structures Considered

### std::map / std::set
- **Rejected**: O(log n) complexity vs O(1) for unordered variants
- Only beneficial if ordering is needed (not required in any case)
- Would significantly slow down lookups for large datasets

### std::vector with linear search
- **Rejected**: O(n) complexity vs O(1) for hash-based lookups
- Only viable for very small sets (< 5 items), but even then hash lookup is competitive
- Not suitable for large datasets (millions of entries)

### std::vector with binary search (sorted)
- **Rejected**: O(log n) complexity vs O(1) for unordered_map
- Requires sorting overhead
- Not suitable for frequent insertions/deletions

### Custom hash table
- **Rejected**: Unnecessary complexity
- Standard library implementation is well-optimized
- No performance issues identified that would justify custom implementation

---

## Performance Considerations

### Hash Function Quality
All `uint64_t` keys provide excellent hash distribution:
- No collisions expected in practice
- Standard library hash function for `uint64_t` is efficient
- No custom hash function needed

### Memory Overhead
Hash tables have memory overhead (buckets, pointers), but:
- Overhead is acceptable for the performance gain
- Memory is temporary for most use cases (SearchController, SearchWorker)
- Persistent maps (FileIndex, ContiguousStringBuffer) are essential for performance

### Cache Locality
- `unordered_map` has worse cache locality than `std::vector`
- However, O(1) lookup performance far outweighs cache locality concerns
- For large datasets, hash lookup is significantly faster than linear/ binary search

---

## Conclusion

**All current usages of `unordered_map` and `unordered_set` are optimal and should remain unchanged.**

### Key Points:
1. **FileIndex::index_**: Critical for O(1) lookups on millions of entries
2. **StringPool::pool_**: Efficient string interning for small sets
3. **ContiguousStringBuffer::id_to_index_**: Essential for O(1) index lookups
4. **Extension sets**: Efficient filtering in hot loops
5. **Temporary maps**: Appropriate for batch processing

### No Changes Recommended:
- All data structures are well-suited for their use cases
- Performance characteristics match requirements
- Memory overhead is acceptable
- No ordering requirements that would justify `std::map`/`std::set`

### Future Considerations:
- If profiling reveals specific bottlenecks, consider:
  - Custom hash functions (unlikely to help with `uint64_t` keys)
  - Memory pool allocation for temporary maps (premature optimization)
  - Lock-free hash tables (significant complexity increase, likely unnecessary)

---

## Sparsepp Alternative Analysis

### Overview
[Sparsepp](https://github.com/lemire/sparsepp) is a drop-in replacement for `std::unordered_map` and `std::unordered_set` that claims:
- **Extremely low memory usage**: ~1 byte overhead per entry (vs ~24-32 bytes for `std::unordered_map`)
- **Better performance**: Typically faster than `std::unordered_map`
- **Drop-in replacement**: Same API as standard containers
- **Single header**: Easy to integrate (`sparsepp.h`)

### Potential Benefits for Our Use Cases

#### 1. FileIndex::index_ - HIGH IMPACT POTENTIAL
**Current:** `std::unordered_map<uint64_t, FileEntry>` with millions of entries

**Potential Savings:**
- Memory overhead: ~20-30 bytes per entry → ~1 byte per entry
- For 1 million entries: **~20-30 MB saved**
- For 10 million entries: **~200-300 MB saved**

**Performance:**
- Claims to be faster than `std::unordered_map`
- Better cache locality due to lower memory overhead
- Could improve lookup performance in hot paths

**Risk Level:** Low (drop-in replacement, easy to test)

#### 2. ContiguousStringBuffer::id_to_index_ - HIGH IMPACT POTENTIAL
**Current:** `std::unordered_map<uint64_t, size_t>` with millions of entries

**Potential Savings:**
- Memory overhead: ~20-30 bytes per entry → ~1 byte per entry
- For 1 million entries: **~20-30 MB saved**
- For 10 million entries: **~200-300 MB saved**

**Performance:**
- Used in hot paths (Remove, GetPath)
- Performance improvement would directly benefit search operations

**Risk Level:** Low (drop-in replacement, easy to test)

#### 3. StringPool::pool_ - LOW IMPACT
**Current:** `std::unordered_set<std::string>` with < 100 entries

**Analysis:**
- Small set size means memory savings are negligible
- Performance difference would be minimal
- Not worth the change for this use case

**Recommendation:** Skip for this use case

#### 4. Temporary Maps (SearchController, SearchWorker) - MEDIUM IMPACT
**Current:** Temporary `unordered_map`/`unordered_set` created during processing

**Potential Benefits:**
- Lower memory overhead during batch processing
- Better performance for result comparison and path mapping
- Since these are temporary, the impact is less critical

**Risk Level:** Low (drop-in replacement)

### Implementation Considerations

#### API Compatibility
✅ **Fully compatible** - Drop-in replacement:
```cpp
// Current
std::unordered_map<uint64_t, FileEntry> index_;

// With sparsepp (just change namespace)
#include <sparsepp.h>
using spp::sparse_hash_map;
spp::sparse_hash_map<uint64_t, FileEntry> index_;
```

#### Integration Effort
- **Minimal**: Single header file, just change namespace
- **Testing**: Easy to A/B test by conditionally compiling

#### Potential Issues
1. **Iterator invalidation**: Sparsepp may have different iterator invalidation rules (documentation mentions `erase()` may invalidate iterators, but returns valid iterator)
2. **Hash function**: Should work with `uint64_t` keys (standard hash function)
3. **Thread safety**: Same as `std::unordered_map` (not thread-safe, requires external locking - which we already have)

### Recommendation: **TEST SPARSEPP**

**Priority Targets:**
1. **FileIndex::index_** - Highest priority (largest memory footprint, most frequent lookups)
2. **ContiguousStringBuffer::id_to_index_** - High priority (large footprint, hot path)

**Testing Strategy:**
1. Create a feature flag to switch between `std::unordered_map` and `spp::sparse_hash_map`
2. Benchmark memory usage before/after
3. Benchmark lookup performance (critical path)
4. Test with realistic data sizes (millions of entries)
5. Verify no regressions in functionality

**Expected Benefits:**
- **Memory**: 20-30 MB saved per million entries (significant for large indexes)
- **Performance**: Potential 10-30% improvement in lookup performance
- **Cache locality**: Better due to lower memory overhead

**Risk Assessment:**
- **Low Risk**: Drop-in replacement, easy to revert
- **High Reward**: Significant memory savings and potential performance gains
- **Easy to Test**: Can be conditionally compiled for A/B testing

### Implementation Example

```cpp
// Option 1: Conditional compilation
#ifdef USE_SPARSEPP
#include <sparsepp.h>
namespace hash_map_ns = spp;
#else
#include <unordered_map>
namespace hash_map_ns = std;
#endif

// Usage
hash_map_ns::unordered_map<uint64_t, FileEntry> index_;

// Option 2: Type alias
#ifdef USE_SPARSEPP
#include <sparsepp.h>
template<typename K, typename V>
using hash_map = spp::sparse_hash_map<K, V>;
template<typename V>
using hash_set = spp::sparse_hash_set<V>;
#else
#include <unordered_map>
#include <unordered_set>
template<typename K, typename V>
using hash_map = std::unordered_map<K, V>;
template<typename V>
using hash_set = std::unordered_set<V>;
#endif

// Usage
hash_map<uint64_t, FileEntry> index_;
```

### Conclusion on Sparsepp

**Verdict: WORTH TESTING**

Sparsepp appears to be a compelling alternative, especially for:
- Large maps with millions of entries (FileIndex, ContiguousStringBuffer)
- Memory-constrained environments
- Performance-critical lookup operations

**Next Steps:**
1. Download `sparsepp.h` and add to project
2. Create feature flag for conditional compilation
3. Test with FileIndex::index_ first (highest impact)
4. Benchmark memory and performance
5. If successful, roll out to ContiguousStringBuffer::id_to_index_
6. Skip small/temporary maps (not worth the effort)

**References:**
- [Sparsepp GitHub](https://github.com/lemire/sparsepp)
- [Original sparsepp (greg7mdp)](https://github.com/greg7mdp/sparsepp)

---

## Related Files
- `FileIndex.h` - Main index implementation
- `ContiguousStringBuffer.h/cpp` - Parallel search buffer
- `SearchController.cpp` - Search result management
- `SearchWorker.cpp` - Background search processing
