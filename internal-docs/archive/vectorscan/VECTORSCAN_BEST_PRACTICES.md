# VectorScan Best Practices for Filename/Path Pattern Matching

**Date:** January 17, 2026  
**Context:** VectorScan usage for file search (outside typical DPI/packet inspection use case)

## Overview

VectorScan (fork of Intel Hyperscan) is optimized for **high-throughput pattern matching** on **large data streams** and **many patterns simultaneously**. Using it for **filename/path pattern matching** is somewhat outside its typical use case, but can work well with proper configuration.

### Typical VectorScan Use Cases
- Deep Packet Inspection (DPI) - scanning network traffic
- Malware detection - scanning file content
- Log analysis - pattern matching on large logs
- **Stream processing** - continuous data matching

### Our Use Case
- File/path pattern matching (one pattern at a time)
- Filenames/paths are typically short (< 500 bytes)
- Need fast lookups across 10,000s to 100,000s of files

---

## Current Configuration Analysis

### Current VectorScan Settings (from VectorScanUtils.cpp)

```cpp
unsigned int flags = HS_FLAG_DOTALL;  // '.' matches newlines
if (!case_sensitive) {
    flags |= HS_FLAG_CASELESS;
}

hs_compile(pattern_str.c_str(), flags, HS_MODE_BLOCK, nullptr, &db, &compile_err);
```

**Current Setup:**
- **Mode:** `HS_MODE_BLOCK` ✅ (correct for non-streaming filenames)
- **Flags:** `HS_FLAG_DOTALL` (dot matches newlines)
- **Optional:** `HS_FLAG_CASELESS` (case-insensitive)

---

## Recommended Settings for Filename Matching

### 1. Pattern Flags (Compile-Time)

#### Current: `HS_FLAG_DOTALL`
- ✅ Good for filename matching
- Makes `.` match newlines (though filenames don't have newlines)

#### Recommended Additions:

**`HS_FLAG_SINGLEMATCH`** - **HIGHLY RECOMMENDED FOR FILENAMES**
```cpp
// Only report first match, stop scanning after match found
// Perfect for filename matching - we only care "does it match?" not "where?"
flags |= HS_FLAG_SINGLEMATCH;  // Significant performance improvement
```

**Rationale:**
- Filenames are usually matched once (does filename match pattern?)
- We don't need all match positions, just yes/no
- Reduces memory and CPU overhead significantly

**`HS_FLAG_SOM_LEFTMOST`** - **OPTIONAL**
```cpp
// Report start-of-match position
// Useful for debugging/analysis but adds overhead
```

**Avoid:**
- ❌ `HS_FLAG_MULTILINE` - Not needed for filenames (no newlines)
- ❌ `HS_FLAG_UTF8` - Not needed unless patterns contain UTF-8

#### Recommended Configuration:
```cpp
unsigned int GetCompileFlags(bool case_sensitive) {
    unsigned int flags = HS_FLAG_DOTALL |      // . matches newlines
                         HS_FLAG_SINGLEMATCH;   // Stop after first match
    if (!case_sensitive) {
        flags |= HS_FLAG_CASELESS;
    }
    return flags;
}
```

### 2. Compile Mode

**Current:** `HS_MODE_BLOCK` ✅ **CORRECT**
- Block mode: perfect for non-streaming data (files, paths)
- Not streaming, so use BLOCK not STREAM
- Thread-safe (each thread gets own database)

**Alternative modes (NOT recommended for filenames):**
- `HS_MODE_STREAM` - For continuous data streams (network packets)
- `HS_MODE_VECTORED` - For non-contiguous data (multiple buffers)

---

## Performance Optimization Checklist

### 1. ✅ Database Caching (Already Implemented)
```cpp
// Thread-local database cache - EXCELLENT
thread_local ThreadLocalDatabaseCache cache;

// Reuse compiled database across 10,000s of matches
db = GetCache().GetDatabase(pattern, case_sensitive);
```

**Status:** ✅ IMPLEMENTED & OPTIMIZED
- Database compiled once
- Cached per thread
- Reused for all matches

### 2. ✅ Scratch Space Allocation (Already Implemented)
```cpp
// Thread-local scratch - EXCELLENT
thread_local hs_scratch_t* g_scratch = nullptr;

// Scratch allocated once per thread, reused for all scans
hs_alloc_scratch(database, &g_scratch);
```

**Status:** ✅ IMPLEMENTED & OPTIMIZED
- Scratch allocated once per thread
- Reused across all matches
- Cleaned up automatically on thread exit

### 3. ✅ Lambda Pre-compilation (Recently Optimized)
```cpp
// Database captured in lambda (not looked up per match)
auto db = vectorscan_utils::GetCache().GetDatabase(pattern, case_sensitive);
return [db](const char* text) {
    return vectorscan_utils::RegexMatchPrecompiled(db, std::string_view(text));
};
```

**Status:** ✅ IMPLEMENTED & OPTIMIZED (from recent fixes)
- No database lookup per match
- No string conversion per match
- Pre-compiled database captured

### 4. Pattern Optimization

**Bad Patterns (Avoid):**
- ❌ `.*` (too greedy, matches everything)
- ❌ `(.*)` (capture group overhead)
- ❌ `a+b+c+` (complex backtracking)

**Good Patterns (Preferred):**
- ✅ `^exact_match$` (anchored, specific)
- ✅ `.*\.cpp$` (ends with specific extension)
- ✅ `^[a-z]+\.txt$` (specific character class)
- ✅ Literals first: `test.*\.log$` not `.*test.*\.log$`

**VectorScan-Optimized Patterns:**
- Use `*` not `+` when possible (0+ vs 1+)
- Use character classes `[abc]` not alternation `a|b|c`
- Avoid unnecessary capturing groups `(...)`

---

## Configuration Recommendations

### For Your Use Case (Filename Matching)

**Current Implementation - GOOD:**
```cpp
hs_compile(pattern_str.c_str(), 
           HS_FLAG_DOTALL | (case_sensitive ? 0 : HS_FLAG_CASELESS),
           HS_MODE_BLOCK,      // ✅ Correct for filenames
           nullptr, 
           &db, 
           &compile_err);
```

**Recommended - ADD SINGLEMATCH:**
```cpp
unsigned int flags = HS_FLAG_DOTALL |      // . matches newlines
                     HS_FLAG_SINGLEMATCH;   // Stop after first match (IMPORTANT!)
if (!case_sensitive) {
    flags |= HS_FLAG_CASELESS;
}

hs_compile(pattern_str.c_str(), 
           flags,
           HS_MODE_BLOCK,                   // ✅ Correct for filenames
           nullptr, 
           &db, 
           &compile_err);
```

**Expected Performance Impact:**
- `HS_FLAG_SINGLEMATCH` can provide **10-30% faster** matching
- Especially beneficial for patterns that match early (like `*.cpp$`)
- No downside for filename matching use case

---

## Implementation Changes Required

### File: `src/utils/VectorScanUtils.cpp`

**Before:**
```cpp
unsigned int GetCompileFlags(bool case_sensitive) {
    unsigned int flags = HS_FLAG_DOTALL; // '.' matches newlines
    if (!case_sensitive) {
        flags |= HS_FLAG_CASELESS;
    }
    return flags;
}
```

**After:**
```cpp
unsigned int GetCompileFlags(bool case_sensitive) {
    // HS_FLAG_DOTALL: . matches newlines (good for pattern matching)
    // HS_FLAG_SINGLEMATCH: Stop after first match (optimized for filename matching - we only need yes/no)
    unsigned int flags = HS_FLAG_DOTALL | HS_FLAG_SINGLEMATCH;
    
    if (!case_sensitive) {
        flags |= HS_FLAG_CASELESS;
    }
    
    return flags;
}
```

---

## Additional Recommendations

### 1. Pattern Validation & Hints

Consider providing hints to users about optimal patterns:

```
Optimal patterns for filename matching:
✅ vs:\.cpp$          - Fast (ends-with)
✅ vs:^src/.*\.cpp$   - Fast (starts-with + ends-with)
⚠️  vs:.*test.*\.cpp$ - Slower (greedy middle)
❌ vs:.*               - Slowest (matches everything)
```

### 2. Consider Hybrid Approach

For **very simple patterns** (literals, prefixes, suffixes), consider using faster string operations:

- `*.cpp` → Use `ends_with()` or `strstr()` (faster than regex)
- `src/*.h` → Use string prefix matching
- Only use VectorScan for complex patterns

### 3. Monitoring & Metrics

Track:
- Number of patterns cached
- Cache hit rate
- Average match time per pattern
- Memory usage of compiled databases

### 4. Pattern Compilation Errors

Current logging:
```
LOG_WARNING_BUILD("VectorScan: Failed to compile pattern '" << pattern 
                  << "': " << compile_err->message);
```

Consider providing fallback suggestions to users:
```
"Pattern failed to compile in VectorScan. Try using 'rs:' prefix for std::regex compatibility"
```

---

## Performance Comparison: Current vs. Recommended

### Scenario: 50,000 files, pattern `vs:.*\.cpp$`

**Current Config:**
- Per-match overhead: ~50-100ns (database lookup, string conversion)
- Pattern overhead: Each match processes all characters

**With HS_FLAG_SINGLEMATCH:**
- Per-match overhead: ~50-100ns (same, pre-compiled)
- Pattern overhead: Stops after first match ✅ Faster

**Estimated speedup:** 10-30% depending on pattern complexity

---

## Resources & References

### Official Documentation
- **Hyperscan Dev Reference:** intel.github.io/hyperscan/dev-reference/
- **VectorScan GitHub:** github.com/VectorCamp/vectorscan
- **Performance Guide:** https://intel.github.io/hyperscan/dev-reference/performance.html

### Key Sections to Review
1. **Compile Mode Flags** - Pattern compilation options
2. **Performance Considerations** - Pattern optimization
3. **Block Mode** - Non-streaming usage (our use case)
4. **Handling Matches** - How to efficiently process results

### Pattern Support Limitations
- No lookahead/lookbehind
- No backreferences (`\1`, `\2`)
- No atomic groups
- No conditional patterns

---

## Summary

Your current VectorScan integration is **well-optimized**, with:
- ✅ Thread-local database caching
- ✅ Thread-local scratch space allocation
- ✅ Pre-compiled databases captured in lambdas
- ✅ Correct BLOCK mode for non-streaming data

**Recommended Enhancement:**
Add `HS_FLAG_SINGLEMATCH` for 10-30% additional performance on filename matching.

The main thing to remember: **VectorScan is a power tool for high-throughput pattern matching**. Your use case (file search) is simpler but still benefits significantly from its performance characteristics when configured correctly.
