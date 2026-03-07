# Optimization Analysis: Extension-Only Search Performance

## Current Performance Issues

When only extension filter is provided (no filename/path query), the code still:

1. **Unnecessary memory accesses:**
   - Accesses `path` from storage (line 332)
   - Computes `filenameOffset` and `filename` pointer (lines 336-337) - **not used**
   - Computes `dirPathLen` (line 345) - **not used**
   - These cause unnecessary cache misses

2. **String allocation overhead:**
   - Line 399: `std::string(extView)` allocates memory for every extension check
   - This happens millions of times in the hot loop

3. **Inefficient check order:**
   - Checks filename (always true when empty)
   - Checks path (always true when empty)
   - Then checks extension
   - Should check extension first when it's the only filter

## Proposed Optimizations

### Optimization 1: Skip Unnecessary Computations

**When both queries are empty, skip:**
- Path pointer access (only need extension offset)
- Filename offset computation
- Directory path length computation

**Code change:**
```cpp
// Fast path: extension-only filter
if (queryStr.empty() && !hasPathQuery && hasExtensionFilter) {
  // Skip all path/filename setup, go straight to extension check
  size_t extensionOffset = extension_start_[i];
  if (extensionOffset != SIZE_MAX) {
    const char *extension = &storage_[offsets_[i] + extensionOffset];
    // ... extension check
  }
  continue;
}
```

### Optimization 2: Avoid String Allocation in Extension Check

**Current (line 399):**
```cpp
extensionMatch = (extensionSet.find(std::string(extView)) != extensionSet.end());
```

**Problem:** Allocates a string for every extension check

**Solution:** Use a custom hash function or direct comparison
- Option A: Pre-compute hash of extension, compare hashes
- Option B: Use `std::string_view` with custom hash (C++17)
- Option C: Compare character-by-character (fastest for short extensions)

**Best option:** Option C - direct character comparison for common case
```cpp
bool extensionMatch = false;
for (const auto &allowedExt : *extensions) {
  if (strcmp(extension, allowedExt.c_str()) == 0) {
    extensionMatch = true;
    break;
  }
}
```

### Optimization 3: Reorder Checks (Extension First)

**When extension is the only filter, check it first:**
- Extension check is cheap (pre-parsed offset, O(1) lookup)
- Most entries will fail extension check (early exit)
- Skip all filename/path setup for non-matching entries

### Optimization 4: Cache-Friendly Memory Access

**Current:** Accesses multiple vectors per iteration:
- `offsets_[i]`
- `filename_start_[i]`
- `extension_start_[i]`
- `ids_[i]`
- `is_deleted_[i]`

**Optimization:** For extension-only, we only need:
- `extension_start_[i]`
- `ids_[i]`
- `is_deleted_[i]`
- `offsets_[i]` (only when extension exists)

This reduces cache misses.

## Implementation Plan

1. **Add fast path for extension-only search:**
   - Detect when `queryStr.empty() && !hasPathQuery && hasExtensionFilter`
   - Skip filename/path pointer computation
   - Go directly to extension check

2. **Optimize extension comparison:**
   - Replace `std::string(extView)` with direct `strcmp()` or character comparison
   - Use `strcmp()` for null-terminated strings (extension is part of path)

3. **Reorder extension check:**
   - When extension is the only filter, check it first
   - Early exit for non-matching extensions

## Expected Performance Improvement

**Current (extension-only):**
- Memory accesses: 5 vectors per entry
- String allocations: 1 per entry (if extension exists)
- **Time: ~50-100ms for 1M entries**

**After optimization:**
- Memory accesses: 3-4 vectors per entry (reduced)
- String allocations: 0 (direct comparison)
- **Time: ~20-40ms for 1M entries (2-2.5x faster)**

## Code Changes

### Fast Path Detection
```cpp
bool extensionOnlyMode = queryStr.empty() && !hasPathQuery && hasExtensionFilter;
```

### Fast Path Loop
```cpp
if (extensionOnlyMode) {
  // Fast path: extension-only filter
  for (size_t i = startIdx; i < endIdx; ++i) {
    if (is_deleted_[i] != 0) continue;
    
    size_t extensionOffset = extension_start_[i];
    if (extensionOffset != SIZE_MAX) {
      const char *extension = &storage_[offsets_[i] + extensionOffset];
      
      // Direct comparison (no string allocation)
      bool extensionMatch = false;
      for (const auto &allowedExt : *extensions) {
        if (strcmp(extension, allowedExt.c_str()) == 0) {
          extensionMatch = true;
          break;
        }
      }
      if (!extensionMatch) continue;
    } else {
      // No extension - check if empty extension is allowed
      bool emptyExtAllowed = false;
      for (const auto &allowedExt : *extensions) {
        if (allowedExt.empty()) {
          emptyExtAllowed = true;
          break;
        }
      }
      if (!emptyExtAllowed) continue;
    }
    
    localResults.push_back(ids_[i]);
  }
} else {
  // Normal path with queries...
}
```
