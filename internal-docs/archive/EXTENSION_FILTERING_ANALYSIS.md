# Analysis: Pre-Parsing Extensions for Parallel Filtering

## Current Approach

**Extension Filtering:**
- Happens sequentially in `SearchWorker` after parallel search
- Requires `FileIndex::GetEntry()` calls (lock contention risk)
- Uses `GetExtension(entry.name)` to extract extension from filename
- Compares against list of allowed extensions

**Performance:**
- Parallel search: ~10-50ms
- Sequential extension filtering: ~5-20ms (depends on match count)
- **Total: ~15-70ms**

## Proposed Approach: Pre-Parse Extension Offset

**Similar to filename_start_, store extension offset during Insert():**
- Find last dot in filename
- Store offset where extension begins (after the dot)
- During search, directly access extension without parsing

## Options Analysis

### Option 1: Pre-Parse Extension Offset (Recommended)

**Storage:**
```cpp
std::vector<size_t> extension_start_; // Offset from path start where extension begins
// Example: "C:\Users\file.txt" -> extension_start_ = offset to "txt" (after last dot)
```

**During Insert():**
```cpp
// Find last dot in filename (after filename_start_)
size_t filenameOffset = filename_start_[idx];
const char *filename = path + filenameOffset;
const char *lastDot = strrchr(filename, '.');
size_t extensionOffset = (lastDot && lastDot[1] != '\0') 
                         ? (lastDot + 1 - path)  // Offset to extension start
                         : SIZE_MAX;  // No extension (sentinel value)
extension_start_.push_back(extensionOffset);
```

**During Search():**
```cpp
// Get extension directly (no parsing needed)
if (extension_start_[i] != SIZE_MAX) {
  const char *extension = path + extension_start_[i];
  // Compare extension against filter list
}
```

**Memory Overhead:**
- ~8 bytes per entry (size_t for extension offset)
- For 1M entries: ~8MB
- Acceptable trade-off for performance gain

**Performance:**
- Zero parsing overhead in hot loop
- Direct pointer access to extension
- Fast comparison against filter set

**Complexity:** Medium (need to handle edge cases)

### Option 2: Parse Extension On-The-Fly

**Storage:** None (parse during search)

**During Search():**
```cpp
const char *filename = path + filename_start_[i];
const char *lastDot = strrchr(filename, '.');
const char *extension = (lastDot && lastDot[1] != '\0') ? (lastDot + 1) : "";
// Compare extension
```

**Memory Overhead:** Zero

**Performance:**
- ~0.0001ms per path (strrchr is fast)
- For 1M paths: ~100ms parsing overhead
- Still faster than current sequential filtering

**Complexity:** Low (simpler implementation)

### Option 3: Store Extension Separately

**Storage:**
```cpp
std::vector<const char*> extension_ptr_; // Pointer to extension in storage_
// Or: std::vector<std::string> extensions_; // Full extension strings
```

**Memory Overhead:**
- Option 3a (pointers): ~8 bytes per entry (same as Option 1)
- Option 3b (strings): ~4-10 bytes per entry (variable, depends on extension length)
- More complex to manage

**Performance:** Fastest (direct access)

**Complexity:** High (need to manage string lifetimes)

## Recommendation

### ✅ **RECOMMENDED: Option 1 (Pre-Parse Extension Offset)**

**Rationale:**
1. **Consistent with existing design:** Similar to `filename_start_` approach
2. **Zero parsing overhead:** Eliminates `strrchr()` calls in hot loop
3. **Reasonable memory cost:** ~8MB for 1M entries (acceptable)
4. **Fast access:** Direct pointer arithmetic, no string allocations
5. **Handles edge cases:** Can use `SIZE_MAX` as sentinel for "no extension"

**Implementation:**
- Add `std::vector<size_t> extension_start_;` to ContiguousStringBuffer
- Parse during `Insert()`: find last dot in filename, store offset
- Use during `Search()`: direct pointer access to extension
- Update `Rebuild()` to preserve extension offsets

**Edge Cases to Handle:**
- No extension: `"file"` → `extension_start_ = SIZE_MAX`
- Empty extension: `"file."` → `extension_start_ = SIZE_MAX` (or handle specially)
- Multiple dots: `"file.backup.txt"` → extension is `"txt"` (after last dot)
- Root paths: `"C:\"` → no filename, no extension

## Performance Comparison

### Current (Sequential Filtering)
- Parallel search: ~10-50ms
- Sequential extension filter: ~5-20ms
- **Total: ~15-70ms**

### Option 1 (Pre-Parsed + Parallel)
- Parallel search + extension filter: ~10-55ms (slight overhead from extension check)
- **Total: ~10-55ms** (faster, eliminates sequential bottleneck)

### Option 2 (On-The-Fly + Parallel)
- Parallel search + extension filter: ~12-60ms (parsing overhead)
- **Total: ~12-60ms** (faster than current, but slower than Option 1)

## Implementation Details

### Extension Offset Calculation

```cpp
// In Insert(), after computing filename_start_:
size_t filenameOffset = filename_start_[idx];
const char *filename = lowerPath.c_str() + filenameOffset;
const char *lastDot = strrchr(filename, '.');

size_t extensionOffset = SIZE_MAX; // Sentinel: no extension
if (lastDot && lastDot[1] != '\0') {
  // Extension exists and is non-empty
  extensionOffset = offset + filenameOffset + (lastDot - filename) + 1;
  // offset = start of path in storage_
  // filenameOffset = offset to filename start
  // (lastDot - filename) = offset from filename start to dot
  // +1 = skip the dot
}
extension_start_.push_back(extensionOffset);
```

### Extension Comparison in Search

```cpp
// In parallel search loop:
bool extensionMatch = true; // Default: match if no extension filter
if (!allowedExtensions.empty() && extension_start_[i] != SIZE_MAX) {
  const char *extension = path + extension_start_[i];
  size_t extLen = strlen(extension); // Or find null terminator
  std::string_view extView(extension, extLen);
  
  extensionMatch = false;
  for (const auto &allowedExt : allowedExtensions) {
    if (extView == allowedExt) {
      extensionMatch = true;
      break;
    }
  }
}
```

## Conclusion

**Pre-parsing extension offsets (Option 1) is the best choice:**
- Eliminates parsing overhead in hot loop
- Consistent with existing `filename_start_` design
- Reasonable memory cost (~8MB for 1M entries)
- Fast direct pointer access
- Enables parallel extension filtering (no lock contention)

This optimization will move extension filtering into the parallel search, eliminating the sequential bottleneck and improving overall search performance.
