# Per-File String Operations Analysis

## Overview

This document identifies string operations that happen **per-file** during search and post-processing that could be **batched** or **eliminated** to improve performance.

---

## Critical Issues Found

### 1. Redundant Filename/Extension Extraction in Post-Processing ⚠️

**Location**: `SearchWorker.cpp` lines 318-329 and 485-496

**Problem**: After search completes, a second pass extracts filename and extension from `fullPath` using `find_last_of()` and `substr()` operations for **every result**.

```cpp
// Second pass to populate string_views
for (auto& result : results) {
    std::string_view path_view(result.fullPath);
    size_t last_slash = path_view.find_last_of("/\\");  // ❌ Per-file string search
    result.filename = (last_slash == std::string_view::npos) ? path_view : path_view.substr(last_slash + 1);  // ❌ Per-file substring
    
    size_t last_dot = result.filename.find_last_of('.');  // ❌ Per-file string search
    if (last_dot != std::string_view::npos && last_dot + 1 < result.filename.length()) {
        result.extension = result.filename.substr(last_dot + 1);  // ❌ Per-file substring
    }
}
```

**Impact**:
- **2x `find_last_of()` calls** per result (slash + dot)
- **2x `substr()` calls** per result (filename + extension)
- For 10,000 results: **40,000 string operations**
- This is **redundant** because:
  - PATH 1: `SearchAsyncWithData` already extracts this data in `ExtractFilenameAndExtension()`
  - PATH 2: Could extract during the initial collection phase

**Fix**: 
- **PATH 1**: Use `filename` and `extension` already extracted in `SearchResultData` (don't re-extract)
- **PATH 2**: Extract during `ForEachEntry` phase using pre-parsed offsets from `FileIndex`

**Performance Gain**: Eliminates ~40,000 string operations for 10k results

---

### 2. String Allocations in Extension Matching ⚠️

**Location**: `FileIndex.cpp` `ExtensionMatches()` function (lines 27-50)

**Problem**: Creates string allocations for **every file** that passes other filters during extension checking.

```cpp
static bool ExtensionMatches(const std::string_view& ext_view,
                             const hash_set_t<std::string>& extension_set,
                             bool case_sensitive) {
  if (case_sensitive) {
    std::string ext_key(ext_view);  // ❌ String allocation per file
    return (extension_set.find(ext_key) != extension_set.end());
  } else {
    std::string ext_key;  // ❌ String allocation per file
    ext_key.reserve(ext_view.length());
    for (char c : ext_view) {
      ext_key.push_back(ToLowerChar(c));  // ❌ Character-by-character processing
    }
    return (extension_set.find(ext_key) != extension_set.end());
  }
}
```

**Impact**:
- **String allocation** for every file that passes folders-only filter
- **Character-by-character lowercasing** for case-insensitive matches
- For 100,000 files with extension filter: **100,000 string allocations**
- Extensions are typically 3-5 bytes, but allocation overhead is significant

**Fix Options**:

**Option A: Use `std::string_view` with custom hash/compare** (Recommended)
- Create a case-insensitive hash function for `string_view`
- Use `std::unordered_set` with custom hash/equal predicates
- Eliminates all string allocations

**Option B: Pre-lowercase extension_set keys, use direct comparison**
- For case-insensitive: Compare `string_view` character-by-character (no allocation)
- Only allocate if match found (rare case)

**Option C: Use interned string pool for extensions**
- Lowercase extension once, intern it
- Reuse interned strings (already have string pool infrastructure)

**Performance Gain**: Eliminates 100,000+ string allocations per search

---

### 3. String Copies in SearchAsyncWithData ⚠️

**Location**: `FileIndex.cpp` `ExtractFilenameAndExtension()` and `SearchAsyncWithData()` (lines 973-992, 1125-1128, 1200-1203)

**Problem**: Creates string copies for filename, extension, and fullPath for **every matching result**.

```cpp
// Extract data from path
SearchResultData data;
data.id = path_ids_[i];
data.isDirectory = (is_directory_[i] == 1);
data.fullPath.assign(path);  // ❌ String copy per result
ExtractFilenameAndExtension(path, filename_start_[i],
                            extension_start_[i], data.filename,  // ❌ String copy
                            data.extension);  // ❌ String copy
```

**Impact**:
- **3 string copies** per matching result (fullPath, filename, extension)
- For 10,000 results: **30,000 string copies**
- Total memory: ~10,000 × (200 + 50 + 5) bytes = **~2.5 MB** of string copies

**Fix**: 
- Store as `string_view` in `SearchResultData` (zero-copy)
- Only convert to `string` when creating final `SearchResult` (if needed)
- Or: Extract directly into `SearchResult` during search (eliminate intermediate `SearchResultData`)

**Performance Gain**: Eliminates 30,000 string copies, saves ~2.5 MB memory per search

---

### 4. Premature String Conversion in PATH 2 ⚠️

**Location**: `SearchWorker.cpp` lines 444-453

**Problem**: Converts path views to strings for **all candidates** before filtering, even though some may be filtered out.

```cpp
// PHASE 2: Batch-fetch all paths as views first
std::vector<std::string_view> path_views;
file_index_.GetPathsView(candidateIds, path_views);

// Convert views to strings only for non-empty paths
paths.clear();
paths.reserve(path_views.size());
for (const auto& view : path_views) {
  if (!view.empty()) {
    paths.emplace_back(view);  // ❌ String copy for ALL candidates
  } else {
    paths.emplace_back();  // Empty string
  }
}
```

**Impact**:
- String copies created for **all candidates** (not just final results)
- If 50,000 candidates → 10,000 results: **40,000 unnecessary string copies**
- Memory waste: ~40,000 × 200 bytes = **~8 MB** wasted allocations

**Fix**:
- Only convert to string when creating `SearchResult` (line 480)
- Pass `string_view` to `CreateSearchResult`, convert inside if needed
- Or: Extract filename/extension during `ForEachEntry` using pre-parsed offsets

**Performance Gain**: Eliminates 40,000+ unnecessary string copies in PATH 2

---

## Summary of Per-File String Operations

| Operation | Location | Frequency | Impact |
|-----------|----------|-----------|--------|
| `find_last_of()` for filename | `SearchWorker.cpp:320,487` | 2x per result | ⚠️ Medium |
| `find_last_of()` for extension | `SearchWorker.cpp:323,490` | 1x per result | ⚠️ Medium |
| `substr()` for filename | `SearchWorker.cpp:321,488` | 1x per result | ⚠️ Medium |
| `substr()` for extension | `SearchWorker.cpp:325,492` | 1x per result | ⚠️ Medium |
| String allocation in `ExtensionMatches` | `FileIndex.cpp:38,43` | 1x per file (filtered) | ⚠️ High |
| String copy for `fullPath` | `FileIndex.cpp:1125,1200` | 1x per result | ⚠️ High |
| String copy for `filename` | `FileIndex.cpp:1126-1127` | 1x per result | ⚠️ High |
| String copy for `extension` | `FileIndex.cpp:1126-1127` | 1x per result | ⚠️ Medium |
| Premature string conversion | `SearchWorker.cpp:449` | 1x per candidate | ⚠️ High |

---

## Optimization Opportunities

### High Priority ⭐⭐⭐

1. **Eliminate redundant filename/extension extraction** (Issue #1)
   - Use pre-extracted data from `SearchResultData`
   - Extract during search phase using pre-parsed offsets
   - **Impact**: Eliminates 40,000+ string operations per 10k results

2. **Eliminate string allocations in extension matching** (Issue #2)
   - Use `string_view` with custom hash/compare
   - **Impact**: Eliminates 100,000+ allocations per search

3. **Use `string_view` in SearchResultData** (Issue #3)
   - Store views instead of strings
   - Convert only when needed
   - **Impact**: Eliminates 30,000+ string copies, saves 2.5 MB memory

### Medium Priority ⭐⭐

4. **Defer string conversion in PATH 2** (Issue #4)
   - Convert only for final results
   - **Impact**: Eliminates 40,000+ unnecessary copies

---

## Recommended Implementation Order

1. **Fix Issue #1** (Redundant extraction) - Quick win, high impact
2. **Fix Issue #3** (String copies in SearchResultData) - Reduces memory pressure
3. **Fix Issue #2** (Extension matching allocations) - Requires hash function work
4. **Fix Issue #4** (Premature conversion) - Lower priority, PATH 2 is less common

---

## Estimated Performance Gains

For a typical search returning **10,000 results** from **100,000 files**:

| Optimization | Operations Eliminated | Memory Saved | Time Saved (est.) |
|--------------|----------------------|--------------|-------------------|
| Issue #1: Redundant extraction | 40,000 | - | ~5-10ms |
| Issue #2: Extension allocations | 100,000 | ~500 KB | ~10-20ms |
| Issue #3: SearchResultData copies | 30,000 | ~2.5 MB | ~5-10ms |
| Issue #4: Premature conversion | 40,000 | ~8 MB | ~5-10ms |
| **Total** | **210,000** | **~11 MB** | **~25-50ms** |

**Note**: Actual gains depend on:
- Number of results
- Number of files scanned
- Extension filter usage
- Case sensitivity settings
