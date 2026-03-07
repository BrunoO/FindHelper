# File/Index ID Usage Analysis

## Overview

This document reviews how File IDs (from USN Windows monitoring) are used throughout the codebase and evaluates whether they could be replaced with faster alternatives, particularly examining the use of `std::unordered_map` for ID lookups.

## Date
2025-01-XX

---

## What Are File IDs?

File IDs come from **USN (Update Sequence Number) Journal monitoring** on Windows:

- **Source**: `FileReferenceNumber` field from `USN_RECORD_V2` structures
- **Type**: `uint64_t` (64-bit unsigned integer)
- **Nature**: NTFS file reference numbers assigned by the Windows kernel
- **Stability**: **Stable across renames and moves** - a file keeps the same ID even if renamed or moved to a different directory

### Example Flow
```cpp
// From UsnMonitor.cpp - USN record processing
uint64_t file_ref_num = record->FileReferenceNumber;
uint64_t parent_ref_num = record->ParentFileReferenceNumber;

// Insert into index using the ID from USN
file_index_.Insert(file_ref_num, parent_ref_num, filename, is_directory, kFileTimeNotLoaded);
```

---

## Current ID Usage Patterns

### 1. FileIndexStorage::index_ - `hash_map_t<uint64_t, FileEntry>`

**Location**: `src/index/FileIndexStorage.h:120`

**Purpose**: Maps file ID → file metadata (FileEntry)

**Usage**:
- **Lookups**: Very frequent - used in 20+ methods
- **Insertions**: During indexing and USN event processing
- **Deletions**: When files are removed
- **Iterations**: `ForEachEntry()`, `RecomputeAllPaths()`

**Key Characteristics**:
- Keys: `uint64_t` (excellent hash distribution)
- Values: `FileEntry` struct (~80-100 bytes)
- Size: Can be millions of entries

**Access Pattern**:
```cpp
// Typical lookup
const FileEntry* entry = storage_.GetEntry(id);
if (entry != nullptr) {
  // Use entry...
}
```

**Why unordered_map is needed**:
- O(1) average-case lookup (critical for performance)
- Random access by ID (no ordering requirements)
- USN events provide IDs, not sequential indices

---

### 2. PathStorage::id_to_path_index_ - `hash_map_t<uint64_t, size_t>`

**Location**: `src/path/PathStorage.h:245`

**Purpose**: Maps file ID → array index in Structure of Arrays (SoA)

**Usage**:
- **Lookups**: Every `GetPath()`, `GetPathView()`, `RemovePath()` call
- **Insertions**: Every `InsertPath()` call
- **Critical for**: Converting ID-based lookups to index-based array access

**Key Characteristics**:
- Keys: `uint64_t` (file ID)
- Values: `size_t` (array index)
- Size: Same as number of path entries (can be millions)

**Access Pattern**:
```cpp
// From PathStorage.cpp
auto it = id_to_path_index_.find(id);
if (it != id_to_path_index_.end()) {
  size_t idx = it->second;
  // Access SoA arrays by index
  const char* path = &path_storage_[path_offsets_[idx]];
}
```

**Why unordered_map is needed**:
- O(1) lookup from ID to index
- Without this map, finding an index would require O(n) linear search through `path_ids_` array
- Memory overhead acceptable for the massive performance gain

**Note**: The SoA arrays already store IDs in `path_ids_` array, but we need the reverse mapping (ID → index) for fast lookups.

---

## Why IDs Are Necessary

### 1. USN Records Provide IDs, Not Indices

USN monitoring provides file IDs as the primary identifier:

```cpp
// From UsnMonitor.cpp
uint64_t file_ref_num = record->FileReferenceNumber;  // From USN record
file_index_.Insert(file_ref_num, ...);  // Must use the ID
```

**We cannot change this** - it's the interface Windows provides.

### 2. IDs Are Stable Across Renames/Moves

This is **critical** for tracking files:

```cpp
// File renamed: "old.txt" → "new.txt"
// - Path changes: "C:\Users\old.txt" → "C:\Users\new.txt"
// - ID stays the same: 12345 → 12345 (unchanged)

// Sequential indices would change:
// - Index might change: 1000 → 2000 (after rebuild/defrag)
// - Cannot track file across operations
```

**Implication**: We need stable identifiers to track files across renames, moves, and directory changes.

### 3. IDs Are Used Throughout the System

**Search Results**:
```cpp
struct SearchResult {
  uint64_t fileId;  // Used for lazy loading, UI display
  // ...
};
```

**UI Display**:
```cpp
// ResultsTable.cpp - uses fileId for lazy loading
result.fileId = data.id;
file_index.GetFileSizeById(result.fileId);  // Lazy load on demand
```

**Lazy Loading**:
```cpp
// LazyAttributeLoader uses IDs to load file size/modification time
uint64_t size = loader.GetFileSize(fileId);
FILETIME time = loader.GetModificationTime(fileId);
```

---

## Could We Replace IDs with Sequential Indices?

### Analysis: Sequential Index Approach

**Proposed Alternative**: Use sequential indices (0, 1, 2, ...) instead of USN IDs internally, only mapping at the USN boundary.

#### Problems with This Approach

**1. We'd Still Need a Map at the Boundary**

```cpp
// USN provides IDs, not indices
uint64_t usn_id = record->FileReferenceNumber;

// We'd need: usn_id → our_sequential_index
hash_map_t<uint64_t, size_t> usn_id_to_index;  // Still need unordered_map!

// Then: our_sequential_index → FileEntry
std::vector<FileEntry> entries;  // Could use vector instead of map
```

**Result**: We'd still need `unordered_map` for the USN ID → index mapping.

**2. Indices Are Not Stable**

Sequential indices change when:
- Entries are deleted (tombstones, then rebuild)
- Path storage is defragmented
- Entries are reordered

**Example**:
```cpp
// Initial state
index 0: ID 100, path "file1.txt"
index 1: ID 200, path "file2.txt"
index 2: ID 300, path "file3.txt"

// Delete file2 (ID 200)
index 0: ID 100, path "file1.txt"
index 1: ID 200, path "file2.txt" [DELETED - tombstone]
index 2: ID 300, path "file3.txt"

// After rebuild (defragmentation)
index 0: ID 100, path "file1.txt"
index 1: ID 300, path "file3.txt"  // ID 300 moved from index 2 to index 1!

// USN event comes in for ID 300
// Old index (2) is now invalid!
// Must look up ID → index again (still need unordered_map)
```

**Result**: We'd need to maintain and update the ID → index map constantly, adding complexity and overhead.

**3. Reverse Mapping Complexity**

We'd need to maintain:
- `usn_id_to_index`: USN ID → sequential index (for USN events)
- `index_to_usn_id`: Sequential index → USN ID (for reverse lookups)
- Update both maps on every insert/delete/rebuild

**Result**: More complexity, more memory, more maintenance overhead.

**4. The SoA Already Stores IDs**

The `path_ids_` array already stores the ID at each index:

```cpp
// From PathStorage.h
std::vector<uint64_t> path_ids_;  // ID for each path entry

// We could search this array, but:
for (size_t i = 0; i < path_ids_.size(); ++i) {
  if (path_ids_[i] == target_id) {
    // Found! But this is O(n) - too slow for millions of entries
  }
}
```

**Result**: Linear search is O(n) - unacceptable for millions of entries. We need O(1) lookup.

---

## Performance Analysis

### Current Approach (with unordered_map)

**Lookup Performance**:
- `FileIndexStorage::GetEntry(id)`: O(1) average case
- `PathStorage::GetIndexForId(id)`: O(1) average case
- Memory overhead: ~24-32 bytes per entry (hash table overhead)

**Memory Usage** (for 1M entries):
- `FileIndexStorage::index_`: ~120-132 MB (100 bytes FileEntry + 24-32 bytes hash overhead)
- `PathStorage::id_to_path_index_`: ~32-40 MB (8 bytes size_t + 24-32 bytes hash overhead)

### Alternative: Sequential Indices with Vector

**Lookup Performance**:
- Direct array access: O(1) - **faster than hash map**
- But: Still need `unordered_map` for USN ID → index mapping
- Net result: **No improvement** (still need hash map)

**Memory Usage** (for 1M entries):
- `std::vector<FileEntry>`: ~100 MB (no hash overhead)
- But: Still need `hash_map_t<uint64_t, size_t>` for USN ID → index: ~32-40 MB
- Net result: **Slight memory savings** (~24-32 MB), but added complexity

**Stability Problem**: Indices change on rebuild/defrag, requiring constant map updates.

---

## Optimization Opportunities

### 1. Use Faster Hash Map Implementation

**Current**: `std::unordered_map` (via `hash_map_t`)

**Options**:
- **boost::unordered_map** (available via `FAST_LIBS_BOOST` flag)
  - ~2x faster lookups than std::unordered_map
  - ~60% less memory overhead than std::unordered_map
  - Stable references (important for StringPool)
  - Already integrated in `HashMapAliases.h`

**Recommendation**: Enable `FAST_LIBS_BOOST` for production builds to get better hash map performance.

### 2. Reduce Hash Map Lookups

**Current**: Multiple lookups per operation

**Example**:
```cpp
// GetPath() does:
auto it = id_to_path_index_.find(id);  // Lookup 1
size_t idx = it->second;
// Then access arrays by index

// GetEntry() does:
const FileEntry* entry = index_.find(id);  // Lookup 2
```

**Optimization**: Batch operations to reduce lookups:
- `ForEachEntryByIds()` already batches lookups
- `ForEachEntryWithPath()` combines both lookups in one lock

**Status**: Already optimized - batch operations are available and used.

### 3. Cache Frequently Accessed IDs

**Potential**: Cache ID → index mappings for hot paths

**Analysis**: 
- Search results already cache IDs in `SearchResult.fileId`
- UI lazy loading already batches operations
- Additional caching would add complexity without clear benefit

**Recommendation**: Not worth the complexity - current batching is sufficient.

---

## Conclusion

### Can We Eliminate File IDs?

**Short Answer**: **No** - File IDs are essential and cannot be fully eliminated.

**Reasons**:
1. **USN provides IDs, not indices** - we must accept IDs as the primary identifier
2. **IDs are stable** - critical for tracking files across renames/moves
3. **We'd still need unordered_map** - even with sequential indices, we'd need ID → index mapping
4. **Indices are unstable** - they change on rebuild/defrag, requiring constant map updates
5. **Current design is optimal** - O(1) lookups with acceptable memory overhead

### Can We Optimize Hash Map Usage?

**Short Answer**: **Yes** - but the optimizations are already available.

**Recommendations**:

1. **Enable FAST_LIBS_BOOST for production builds**:
   ```cmake
   # In CMakeLists.txt
   option(FAST_LIBS_BOOST "Use boost::unordered_map for faster lookups (requires Boost 1.80+)" OFF)
   ```
   - Uses `boost::unordered_map` for ~2x faster lookups
   - ~60% less memory overhead than std::unordered_map
   - Stable references (works for all use cases including StringPool)
   - **Note**: Not available on Windows (use std::unordered_map)

2. **Use batch operations** (already implemented):
   - `ForEachEntryByIds()` - batch FileEntry lookups
   - `ForEachEntryWithPath()` - batch FileEntry + path lookups
   - Reduces lock acquisitions and improves cache locality

3. **Keep current architecture**:
   - The two `unordered_map` usages are optimal
   - `FileIndexStorage::index_` - essential for metadata lookups
   - `PathStorage::id_to_path_index_` - essential for O(1) index lookups

### Summary

| Aspect | Current State | Optimization Potential |
|--------|---------------|----------------------|
| **File IDs** | Required (from USN) | Cannot eliminate |
| **unordered_map usage** | 2 maps (optimal) | Enable FAST_LIBS_BOOST flag |
| **Lookup performance** | O(1) average | ~2x faster with boost |
| **Memory overhead** | ~24-32 bytes/entry | Can reduce 60% with boost |
| **Stability** | IDs are stable | Sequential indices would be unstable |

**Final Recommendation**: 
- **Keep the current ID-based architecture** - it's optimal for the use case
- **Enable FAST_LIBS_BOOST for production** - get ~2x faster lookups and 60% less memory overhead
- **Continue using batch operations** - already implemented and working well

The use of `std::unordered_map` is **justified and necessary** - it's not a performance bottleneck, and the alternatives would add complexity without clear benefits.

---

## References

- `src/index/FileIndexStorage.h` - FileEntry storage with ID mapping
- `src/path/PathStorage.h` - SoA arrays with ID → index mapping
- `src/usn/UsnMonitor.cpp` - USN record processing (source of IDs)
- `src/utils/HashMapAliases.h` - Hash map abstraction with FAST_LIBS support
- `docs/archive/UNORDERED_MAP_SET_ANALYSIS.md` - Previous analysis (2024-12-19)

