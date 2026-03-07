# Performance Optimization Implementation Guide
## Detailed Technical Deep-Dive - 2026-01-23

---

## Optimization #1: FileIndexStorage - Replace find()+insert with emplace()

### Current Implementation (3 locations)

**Location 1: InsertLocked() - Line 17**
```cpp
// BEFORE
bool isNewEntry = (index_.find(id) == index_.end());  // First lookup
// ... later ...
index_[id] = {parent_id, std::string(name), ...};     // Second lookup + insert
```

**Location 2: RenameLocked() - Line 95**
```cpp
// BEFORE
auto it = index_.find(id);
if (it != index_.end()) {
  it->second.name = std::string(new_name);
}
```

**Location 3: CacheDirectory() - Line 181**
```cpp
// BEFORE
directory_path_to_id_[std::string(path)] = id;  // Two map operations
```

### Optimized Implementation

**Location 1: Use emplace() with piecewise_construct**
```cpp
// AFTER - Single map operation instead of two
auto [it, inserted] = index_.emplace(
    std::piecewise_construct,
    std::forward_as_tuple(id),
    std::forward_as_tuple(parent_id, std::string(name), ext, is_directory, fileSize, lastModTime)
);
bool isNewEntry = inserted;
```

**Location 2: Keep as-is (find() is necessary for verification)**
```cpp
// This is already optimal - need to verify entry exists first
auto it = index_.find(id);
if (it != index_.end()) {
  it->second.name = std::string(new_name);
}
```

**Location 3: Optimize directory cache**
```cpp
// BEFORE - Creates temporary string
directory_path_to_id_[std::string(path)] = id;

// AFTER - Create once in variable (compiler may still optimize)
// OR check if unordered_map has transparent hash (requires C++20)
// For now: minimize by using same pattern consistently
std::string path_str(path);
directory_path_to_id_[path_str] = id;
```

### Why This Works

- **emplace() avoids double-lookup:** `operator[]` calls `find()` internally, then `find()` is called explicitly
- **piecewise_construct:** Avoids temporary FileEntry object
- **Performance gain:** Hash map lookup is O(1) average, but constant factor matters at scale

### Trade-offs

- ✅ No additional memory overhead
- ✅ No lifetime issues (strings own their data)
- ✅ Backward compatible (no API changes)
- ⚠️ Slightly more complex code (but well-commented)

### Testing

```cpp
// Existing tests should validate:
// 1. Entries are inserted correctly
// 2. Duplicate insert detection still works
// 3. Rename updates stored values
// No new tests needed - behavior unchanged
```

---

## Optimization #2: SearchContext - Use string_view instead of string

### Current Implementation

**File:** `src/search/SearchContext.h` (structural definition)
```cpp
struct SearchContext {
  std::string filename_query;    // ❌ Owns memory
  std::string path_query;        // ❌ Owns memory
  // ... other fields ...
};
```

**File:** `src/index/FileIndex.cpp:328-329` (initialization)
```cpp
context.filename_query = std::string(query);       // ❌ Forces allocation
context.path_query = std::string(path_query);      // ❌ Forces allocation
```

### Optimized Implementation

**Step 1: Update SearchContext struct**
```cpp
struct SearchContext {
  std::string_view filename_query;    // ✅ Non-owning view
  std::string_view path_query;        // ✅ Non-owning view
  // ... other fields ...
};
```

**Step 2: Update FileIndex initialization**
```cpp
context.filename_query = query;         // ✅ No allocation
context.path_query = path_query;        // ✅ No allocation
```

### Why This Works

- **Lifetime guarantee:** `SearchAsync` parameters have same lifetime as `SearchContext`
  - `query` and `path_query` are parameters to `SearchAsync()`
  - `SearchContext` is created on stack within that function
  - Views reference parameters which outlive the SearchContext usage
  
- **No ownership needed:** SearchContext doesn't need to own the strings
  - Context is used locally within the search function
  - Pointers to queries don't escape the search scope

### Trade-offs

- ✅ Zero allocations during initialization
- ✅ Cleaner code (no unnecessary conversions)
- ✅ searchContext can be copied efficiently (string_view is tiny)
- ⚠️ Must ensure strings don't go out of scope (already guaranteed by design)

### Verification

**Lifetime check:**
```
SearchAsync(query, path_query)  // query and path_query are parameters
  ├─ SearchContext context      // Created on stack
  │  ├─ context.filename_query = query  // View into parameter
  │  └─ context.path_query = path_query // View into parameter
  ├─ ParallelSearchEngine::SearchAsync(context, ...)  // Passed to engine
  │  └─ ProcessChunkRange(context, ...)  // Used locally
  └─ Return (context destroyed)  // View scope ends

// Lifetime guarantee: ✅ query/path_query outlive all uses
```

### Testing

- ✅ Existing search tests validate correct behavior
- ✅ No functional changes, just storage method
- ⚠️ Verify with AddressSanitizer that no dangling pointers created

---

## Optimization #3: Vector Pre-allocation in Extension Parsing

### Current Implementation

**File:** `src/api/GeminiApiUtils.cpp` (assuming ParseExtensionsFromGeminiResponse exists)

```cpp
// BEFORE - No pre-allocation
std::vector<std::string> extensions;
for (auto token : tokens) {
  ProcessAndAddToken(token, extensions);  // push_back without reserve
}
```

### Optimized Implementation

**Find the parsing function and add estimate:**
```cpp
// AFTER - Estimate extensions count and pre-allocate
std::vector<std::string> extensions;
// Estimate: typical API response has ~5-20 extensions
// Conservative estimate: response.size() / 10 (each ext ~10 chars)
extensions.reserve(response.size() / 10 + 5);  // +5 for safety

for (auto token : tokens) {
  ProcessAndAddToken(token, extensions);  // push_back uses pre-allocated space
}
```

### Why This Works

- **Typical extension counts:** 5-20 extensions per Gemini API response
- **Without reserve:** Vector doubles capacity each time (0→1→2→4→8→16→32...)
  - With 15 extensions: ~4-5 reallocations
  - Each reallocation: allocate + copy + deallocate old

- **With reserve:** Single allocation upfront
  - If estimate is accurate: exactly 1 allocation
  - If estimate is conservative: minimal waste

### Trade-offs

- ✅ Eliminates reallocations during parsing
- ✅ More cache-friendly (fewer allocations = fewer cache invalidations)
- ⚠️ Slight memory overallocation if estimate is too high (usually acceptable)
- ⚠️ Need to find the parsing function first

### Locating the Function

```bash
# Search for the parsing function
grep -r "ParseExtensions\|GetExtensions\|from.*Response" src/api/
# Look for where extensions vector is populated from Gemini response
```

### Testing

- ✅ Functionality unchanged (still parses same extensions)
- ✅ Performance improved (fewer allocations)
- ⚠️ Check memory usage doesn't increase unexpectedly

---

## Optimization #4: Extension Matching - Verify SSO Behavior

### Current Implementation

```cpp
inline bool ExtensionMatches(const std::string_view& ext_view,
                              const hash_set_t<std::string>& extension_set,
                              bool case_sensitive) {
  if (case_sensitive) {
    std::string ext_key(ext_view);  // Creates temporary string
    return (extension_set.find(ext_key) != extension_set.end());
  }
  // ...
}
```

### Before Optimizing: Profile First

**Why profiling matters:**
- Modern `std::string` implementations use **Small String Optimization (SSO)**
- Strings up to ~16-24 bytes store inline (no heap allocation)
- Most file extensions (`.jpg`, `.txt`, `.cpp`) fit easily within SSO threshold
- SSO means `std::string(ext_view)` might NOT actually allocate heap memory

### Profiling Steps

```cpp
// Add to search hot path to measure allocations
#ifdef ENABLE_ALLOCATION_TRACKING
extern thread_local size_t g_string_allocations;
extern thread_local size_t g_extension_checks;

// Patch std::string constructor to track (or use valgrind/heaptrack)
#endif
```

### If Profiling Shows Allocations

Only then implement small-buffer optimization:

```cpp
inline bool ExtensionMatches(const std::string_view& ext_view,
                              const hash_set_t<std::string>& extension_set,
                              bool case_sensitive) {
  if (case_sensitive) {
    // SSO threshold is typically 16-24 bytes
    // Most extensions: .exe(4), .txt(4), .cpp(4), .json(5)
    // Safe to assume std::string handles it efficiently
    
    // BUT: If profiling shows allocations, use this approach:
    if (ext_view.length() <= 31) {  // Well within typical SSO threshold
      // Trust SSO to handle inline storage
      std::string ext_key(ext_view);
      return (extension_set.find(ext_key) != extension_set.end());
    } else {
      // Rare case: very long extension
      std::string ext_key(ext_view);
      return (extension_set.find(ext_key) != extension_set.end());
    }
  }
  // ...
}
```

### Recommendation

**Start with profiling.** If SSO is working well (typical), this optimization isn't needed. If profiling shows allocations:

```cpp
// Heterogeneous lookup approach (C++20 feature)
// Requires unordered_set with transparent hasher
using ExtensionSet = std::unordered_set<std::string, TransparentHash>;
```

---

## Optimization #5: String Concatenation Cleanup

### Current Implementation

**Location 1: Linux HTTP**
```cpp
// BEFORE
std::string api_key_header = "x-goog-api-key: " + std::string(api_key);
```

**Location 2: Windows bootstrap**
```cpp
// BEFORE
return R"(\\.\)" + std::string(volume);
```

**Location 3: Exception handling**
```cpp
// BEFORE
LOG_ERROR("JSON parse error: " + std::string(e.what()));
```

### Optimized Implementation

**Approach 1: Use append() method**
```cpp
std::string api_key_header = "x-goog-api-key: ";
api_key_header.append(api_key);

std::string drive_path = R"(\\.\)";
drive_path.append(volume);
```

**Approach 2: Use single constructor**
```cpp
std::string api_key_header;
api_key_header.reserve(20 + api_key.length());  // Pre-allocate
api_key_header.append("x-goog-api-key: ");
api_key_header.append(api_key);
```

**Approach 3: Use fmt library (if available)**
```cpp
#include <fmt/format.h>
std::string api_key_header = fmt::format("x-goog-api-key: {}", api_key);
```

**Approach 4: Exception path - use stream operator**
```cpp
// Already good in modern logging
LOG_ERROR("JSON parse error: " << e.what());  // Stream-based
```

### Why This Works

- **operator+ creates temporary:** `"string" + std::string` creates intermediate temp
- **append() avoids temporary:** Directly modifies target string
- **Stream operators:** Already optimized in logging library

### Trade-offs

- ✅ Eliminates temporary string creation
- ✅ More readable (explicit what's happening)
- ⚠️ Very minor impact (these are infrequent operations)
- ⚠️ Some code-style consistency needed

---

## Optimization #6: Exception Message Strings (Optional)

### Current Implementation

```cpp
catch (const std::exception& e) {
  LOG_ERROR("JSON parse error in settings file: " + std::string(e.what()));
  LOG_ERROR("JSON error in settings file: " + std::string(e.what()));
  LOG_ERROR("I/O error reading settings file: " + std::string(e.what()));
}
```

### Optimized Implementation

```cpp
catch (const std::exception& e) {
  LOG_ERROR("JSON parse error in settings file: " << e.what());
  LOG_ERROR("JSON error in settings file: " << e.what());
  LOG_ERROR("I/O error reading settings file: " << e.what());
}
```

### Why This Works

- **Stream operators** in logging are optimized
- **No temporary string** created
- **Negligible impact** (error path only, rarely executed)

### Trade-offs

- ✅ Consistent logging style
- ✅ Slightly cleaner code
- ⚠️ Pure style improvement, no functional benefit

---

## Implementation Checklist

### Before Starting

- [ ] Understand current performance baseline
- [ ] Set up profiling/testing environment
- [ ] Create feature branch

### Implementation Phase

- [ ] Optimization #1: FileIndexStorage (10 minutes)
  - [ ] Replace find()+insert with emplace()
  - [ ] Test with `file_index_search_strategy_tests`

- [ ] Optimization #2: SearchContext (15 minutes)
  - [ ] Update struct definition
  - [ ] Update initialization calls
  - [ ] Test with `parallel_search_engine_tests`

- [ ] Optimization #3: Vector pre-allocation (5 minutes)
  - [ ] Find parsing function
  - [ ] Add reserve() call
  - [ ] Test with existing API tests

- [ ] Optimization #4: Extension matching (deferred)
  - [ ] Run profiling first
  - [ ] Only optimize if allocations detected

- [ ] Optimization #5: String concatenation (10 minutes)
  - [ ] Update HTTP header building
  - [ ] Update bootstrap code
  - [ ] Update exception messages

### Validation Phase

- [ ] Run full test suite: `scripts/build_tests_macos.sh`
- [ ] All tests pass
- [ ] No new compiler warnings
- [ ] Profile shows performance improvement
- [ ] Memory usage is same or better
- [ ] Code review approval

---

## Expected Timing

| Task | Estimated | Actual |
|------|-----------|--------|
| Optimization #1 | 10 min | _ |
| Optimization #2 | 15 min | _ |
| Optimization #3 | 5 min | _ |
| Optimization #4 | 0 min (pending) | _ |
| Optimization #5 | 10 min | _ |
| Testing | 10 min | _ |
| **Total** | **~50 min** | _ |

---

## Success Criteria

- ✅ All tests pass
- ✅ No compiler warnings introduced
- ✅ Code review approved
- ✅ Performance profiling shows measurable improvement
- ✅ No memory regression
- ✅ Builds on macOS, Windows, Linux

