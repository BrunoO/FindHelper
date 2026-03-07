# StringPool Design Documentation

## Overview

`StringPool` is a string interning mechanism designed to reduce memory usage and enable fast pointer-based comparisons for file extensions in the `FileIndex` system.

## Purpose

### Problem Statement

In a file indexing system with millions of files, many files share the same extension (e.g., `.txt`, `.pdf`, `.jpg`). Without string interning:

- **Memory Waste**: Each `FileEntry` would store its own `std::string` for the extension, duplicating memory for identical strings.
- **Comparison Overhead**: Comparing extensions would require string comparisons instead of fast pointer equality checks.

### Solution: String Interning

`StringPool` deduplicates extension strings by maintaining a single canonical instance of each unique extension string. All `FileEntry` objects that share the same extension store a pointer to the same interned string object.

## Implementation

### Class Definition

```cpp
class StringPool {
public:
  const std::string *Intern(const std::string &str) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pool_.insert(ToLower(str)).first;
    return &(*it);
  }

private:
  std::mutex mutex_;
  hash_set_stable_t<std::string> pool_;
};
```

### Key Characteristics

1. **Thread-Safe**: Uses `std::mutex` to protect concurrent access
2. **Case-Insensitive**: Automatically lowercases all strings before interning
3. **Stable Pointers**: Returns `const std::string*` that remain valid across container rehashes
4. **Deduplication**: Same string (case-insensitive) always returns the same pointer

### Critical Design Decision: `hash_set_stable_t`

**Why `hash_set_stable_t` instead of `hash_set_t`?**

The code uses `hash_set_stable_t` (node-based container) instead of `hash_set_t` (which may use contiguous containers) because:

```cpp
// StringPool stores and returns raw pointers to the underlying std::string instances:
const std::string* Intern(const std::string& str);

// For correctness, those pointers must remain stable across insertions and rehashes.
```

**Container Behavior:**

| Container Type | Memory Layout | Pointer Stability |
|----------------|---------------|-------------------|
| `std::unordered_set` | Node-based | ✅ Stable (pointers remain valid) |
| `boost::unordered_set` | Node-based | ✅ Stable (pointers remain valid) |
| `ankerl::unordered_dense::set` | Contiguous buffer | ❌ Unstable (may move on rehash) |

**Why This Matters:**

- **Node-based containers** (`std::unordered_set`, `boost::unordered_set`): Elements are stored in separate nodes. When the container rehashes, nodes are moved but the string data within each node remains at the same address. Pointers to the strings remain valid.

- **Contiguous containers** (`ankerl::unordered_dense::set`): Elements are stored in a contiguous buffer. When the container rehashes, it may allocate a new buffer and move all elements, invalidating all existing pointers.

**Consequence of Wrong Choice:**

If we used `ankerl::unordered_dense::set`:
1. `FileEntry::extension` stores a pointer to an interned string
2. Container rehashes (e.g., when growing from 100 to 200 extensions)
3. All strings are moved to a new buffer
4. All `FileEntry::extension` pointers become **dangling pointers**
5. **Crash** when dereferencing these pointers

**Current Mapping:**

- `hash_set_stable_t` maps to:
  - `boost::unordered_set` when `FAST_LIBS_BOOST` is enabled
  - `std::unordered_set` otherwise
- Both are node-based and provide stable pointers ✅

## Usage Pattern

### 1. During File Insertion

```cpp
// In FileIndex::InsertLocked()
const std::string *ext = nullptr;
if (!isDirectory) {
  std::string extension = GetExtension(name);
  if (!extension.empty()) {
    ext = stringPool_.Intern(extension);  // Returns pointer to interned string
  }
}
// Store pointer in FileEntry
entry.extension = ext;
```

**Flow:**
1. Extract extension from filename (e.g., `"document.txt"` → `"txt"`)
2. Call `stringPool_.Intern("txt")`
3. StringPool lowercases and checks if `"txt"` exists in pool
4. If exists: Returns pointer to existing string
5. If new: Inserts `"txt"` into pool, returns pointer to new string
6. Store pointer in `FileEntry::extension`

### 2. During File Rename

```cpp
// In FileIndex::Rename()
if (!it->second.isDirectory) {
  std::string new_ext_str = GetExtension(newName);
  it->second.extension = new_ext_str.empty() 
    ? nullptr 
    : stringPool_.Intern(new_ext_str);
}
```

**Flow:**
1. Extract new extension from new filename
2. Intern the new extension (may reuse existing string if extension didn't change)
3. Update `FileEntry::extension` pointer

### 3. During Search (NOT Currently Used)

**Important Note:** Search operations do **not** currently use `FileEntry::extension` pointers. Instead:

- Search uses the **SoA (Structure of Arrays)** layout in `PathStorage`
- Extensions are extracted from the contiguous path buffer as `string_view`
- Extensions are compared using string matching, not pointer equality

**Why?**
- Search operations prioritize **cache locality** (SoA arrays are contiguous)
- Accessing `FileEntry` objects would require hash map lookups (slower)
- SoA arrays provide zero-copy `string_view` access (faster)

**Future Optimization Potential:**
- If we wanted to use pointer equality, we'd need to:
  1. Store interned extension pointers in the SoA arrays (instead of offsets)
  2. Compare pointers directly: `if (ext_ptr == target_ext_ptr)`
  3. This would be O(1) pointer comparison vs O(n) string comparison
  4. **Trade-off**: Would require hash map lookups to get `FileEntry` objects (slower than current SoA access)

## Memory Savings Analysis

### Example: 1 Million Files with `.txt` Extension

**Without StringPool:**
```
Each FileEntry stores: std::string extension
- Small String Optimization (SSO): ~24 bytes overhead
- Data: 3 bytes ("txt")
- Total per entry: ~27 bytes

1,000,000 files × 27 bytes = 27,000,000 bytes = ~27 MB
```

**With StringPool:**
```
Each FileEntry stores: const std::string* extension
- Pointer: 8 bytes (64-bit system)

Pool stores: 1 × std::string("txt")
- SSO overhead: ~24 bytes
- Data: 3 bytes
- Total: ~27 bytes

Total memory:
- Pointers: 1,000,000 × 8 bytes = 8,000,000 bytes = ~8 MB
- Pool: 27 bytes
- Total: ~8 MB
```

**Savings: 27 MB - 8 MB = 19 MB (70% reduction)**

### Real-World Impact

**Typical File System:**
- 500,000 files
- ~50 unique extensions
- Average extension length: 3-4 characters

**Memory Savings:**
- Without StringPool: 500,000 × ~27 bytes = ~13.5 MB
- With StringPool: (500,000 × 8 bytes) + (50 × ~27 bytes) = ~4 MB
- **Savings: ~9.5 MB (70% reduction)**

**For extensions with longer names:**
- Extension like `"config"` (6 chars) without SSO: ~32 bytes per entry
- With StringPool: Still 8 bytes per entry
- **Savings increase for longer extensions**

## Performance Characteristics

### Time Complexity

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| `Intern()` | O(1) average, O(n) worst | Hash set lookup/insert |
| Pointer comparison | O(1) | Direct pointer equality (if used) |
| String comparison | O(n) | Character-by-character (current search) |

### Space Complexity

- **Pool size**: O(k) where k = number of unique extensions (typically 20-100)
- **Per-entry overhead**: O(1) - single pointer (8 bytes)

### Lock Contention

- **Single mutex**: All `Intern()` calls serialize
- **Impact**: Low - `Intern()` is only called during:
  - File insertion (indexing phase, not search)
  - File rename (infrequent user operation)
- **Search operations**: Do not use StringPool (no lock contention)

## Thread Safety

### Current Implementation

```cpp
const std::string *Intern(const std::string &str) {
  std::lock_guard<std::mutex> lock(mutex_);  // Serializes all calls
  auto it = pool_.insert(ToLower(str)).first;
  return &(*it);
}
```

**Characteristics:**
- ✅ **Thread-safe**: Mutex protects all pool operations
- ⚠️ **Serialization**: All threads block on `Intern()` calls
- ✅ **Safe for concurrent reads**: Pointers remain valid after return

### Usage Context

**When is `Intern()` called?**
1. **Indexing phase**: Single-threaded (USN journal processing)
2. **File rename**: User-initiated, infrequent
3. **File move**: User-initiated, infrequent

**Conclusion:** Lock contention is minimal because:
- Indexing is single-threaded
- User operations (rename/move) are infrequent
- Search operations don't use StringPool

## Integration with FileIndexStorage

### Ownership

`StringPool` is owned by `FileIndexStorage` because:
1. It's tightly coupled to `FileEntry` storage (used during Insert/Rename)
2. It's part of the core data model (not search-specific)
3. It's used by CRUD operations (Insert, Remove, Rename, Move)

### API

```cpp
class FileIndexStorage {
public:
    // String pool for extensions
    const std::string* InternExtension(const std::string& ext);
    
private:
    StringPool stringPool_;
};
```

### Usage in FileIndexStorage Operations

```cpp
// Insert
void FileIndexStorage::Insert(...) {
    // ...
    if (!isDirectory) {
        std::string ext = GetExtension(name);
        entry.extension = ext.empty() ? nullptr : stringPool_.Intern(ext);
    }
    // ...
}

// Rename
bool FileIndexStorage::Rename(uint64_t id, const std::string& newName) {
    // ...
    if (!entry.isDirectory) {
        std::string new_ext = GetExtension(newName);
        entry.extension = new_ext.empty() ? nullptr : stringPool_.Intern(new_ext);
    }
    // ...
}
```

## Future Optimization Opportunities

### 1. Pointer-Based Extension Filtering

**Current:** Search extracts extensions from SoA arrays and compares as strings.

**Potential:** Store interned extension pointers in SoA arrays, compare pointers directly.

**Trade-offs:**
- ✅ O(1) pointer comparison vs O(n) string comparison
- ❌ Requires hash map lookup to get `FileEntry` (slower than SoA access)
- ❌ More complex SoA layout (pointers instead of offsets)

**Verdict:** Current approach (SoA with string comparison) is likely faster due to cache locality.

### 2. Lock-Free StringPool

**Current:** Single mutex serializes all `Intern()` calls.

**Potential:** Use lock-free hash table or read-copy-update (RCU) pattern.

**Trade-offs:**
- ✅ Eliminates lock contention
- ❌ More complex implementation
- ❌ Current lock contention is minimal (indexing is single-threaded)

**Verdict:** Not worth the complexity given current usage patterns.

### 3. Extension Statistics

**Potential:** Track extension frequency to optimize memory layout.

**Example:**
- Most common extensions: Store in hot cache
- Rare extensions: Store in cold storage

**Verdict:** Premature optimization - current design is sufficient.

## Testing Considerations

### Unit Tests

1. **Deduplication**: Same string returns same pointer
2. **Case-insensitivity**: `"TXT"` and `"txt"` return same pointer
3. **Thread safety**: Concurrent `Intern()` calls don't crash
4. **Pointer stability**: Pointers remain valid after container rehash
5. **Empty strings**: Handle empty extension strings correctly

### Integration Tests

1. **File insertion**: Extensions are correctly interned
2. **File rename**: Extension pointers are updated correctly
3. **Memory usage**: Verify memory savings in practice
4. **Concurrent access**: Multiple threads inserting files simultaneously

## Summary

**StringPool solves:**
- ✅ **Memory efficiency**: 70%+ reduction in extension storage
- ✅ **Potential speed optimization**: Enables pointer equality (not currently used)

**Key design decisions:**
- ✅ **Stable pointers**: Uses node-based containers (`hash_set_stable_t`)
- ✅ **Case-insensitive**: Automatically lowercases all strings
- ✅ **Thread-safe**: Mutex protects concurrent access

**Current usage:**
- ✅ **Memory savings**: Significant (70%+ reduction)
- ⚠️ **Speed optimization**: Not currently used in search (search uses SoA arrays)

**Integration:**
- ✅ **Ownership**: Belongs in `FileIndexStorage` (core data model)
- ✅ **API**: Simple `Intern()` method, returns stable pointer

---

**Last Updated:** 2025-12-29  
**Related Documents:**
- `ARCHITECTURAL_REVIEW_REMAINING_ISSUES-2025-12-29.md` - Component extraction plan
- `FileIndex.h` - Current implementation
- `HashMapAliases.h` - Container type definitions

