# VectorScan Optimization Opportunities

**Date:** 2026-01-17  
**Status:** 🔍 Analysis  
**Goal:** Identify and implement performance optimizations for VectorScan matching

## Current Performance Issues

VectorScan is working but still slower than expected. Analysis of the hot path reveals several optimization opportunities.

## Identified Optimizations

### 1. Remove Debug Logging from Hot Path ⚠️ HIGH IMPACT
**Location:** `RegexMatchPrecompiled()` function  
**Impact:** String allocations on every match (even in release builds if DEBUG logging enabled)  
**Complexity:** TRIVIAL

**Current Code:**
```cpp
LOG_DEBUG_BUILD("VectorScan: RegexMatchPrecompiled - calling hs_scan with text length=" << text_len << ", text ends with: '" << ... << "'");
LOG_DEBUG_BUILD("VectorScan: Scan result=" << match_ctx.matched << " (callback was " << ... << ", err=" << err << ")");
LOG_DEBUG_BUILD("VectorScan: OnMatch callback called! from=" << from << " to=" << to);
```

**Problem:**
- String concatenation and formatting happens even if logging is disabled
- Creates temporary `std::string` objects
- Called millions of times in hot path

**Fix:** Remove or make conditional (only log on actual errors)

### 2. Remove Redundant Runtime Check ⚠️ MEDIUM IMPACT
**Location:** `RegexMatchPrecompiled()` function  
**Impact:** Unnecessary function call on every match  
**Complexity:** TRIVIAL

**Current Code:**
```cpp
if (!IsRuntimeAvailable()) {
    LOG_DEBUG_BUILD("VectorScan: Not available at runtime");
    return false;
}
```

**Problem:**
- If database is compiled, VectorScan must be available
- This check is redundant in the hot path
- Called millions of times

**Fix:** Remove check (database compilation already validates availability)

### 3. Remove Unused MatchContext Field ⚠️ LOW IMPACT
**Location:** `MatchContext` struct  
**Impact:** Minor - unused field assignment  
**Complexity:** TRIVIAL

**Current Code:**
```cpp
struct MatchContext {
    bool matched = false;
    unsigned int text_length;  // ❌ Never used
};
match_ctx.text_length = text_len;  // ❌ Unnecessary assignment
```

**Fix:** Remove unused field

### 4. Optimize Callback Function ⚠️ HIGH IMPACT
**Location:** `OnMatch()` callback  
**Impact:** String formatting on every match found  
**Complexity:** TRIVIAL

**Current Code:**
```cpp
LOG_DEBUG_BUILD("VectorScan: OnMatch callback called! from=" << from << " to=" << to);
```

**Problem:**
- Called for every match found
- String formatting overhead
- In hot path

**Fix:** Remove logging from callback (or make it conditional)

### 5. Remove String Construction in Logging ⚠️ MEDIUM IMPACT
**Location:** Text end logging  
**Impact:** String allocation for logging  
**Complexity:** TRIVIAL

**Current Code:**
```cpp
std::string(text.data() + text_len - 10, 10)  // ❌ Allocates string for logging
```

**Fix:** Remove or use string_view (but better to remove entirely from hot path)

## Recommended Optimizations (Priority Order)

### Priority 1: Remove All Debug Logging from Hot Path
**Impact:** HIGH | **Effort:** 5 minutes

Remove all `LOG_DEBUG_BUILD` calls from:
- `RegexMatchPrecompiled()` function body
- `OnMatch()` callback
- Keep only error logging (actual failures)

### Priority 2: Remove Redundant Checks
**Impact:** MEDIUM | **Effort:** 2 minutes

- Remove `IsRuntimeAvailable()` check from `RegexMatchPrecompiled()`
- Remove unused `text_length` field from `MatchContext`

### Priority 3: Profile Actual VectorScan Performance
**Impact:** HIGH | **Effort:** 30 minutes

- Add performance counters to measure actual `hs_scan()` time
- Compare with std::regex performance
- Identify if VectorScan itself is slow or overhead is the issue

## Expected Performance Improvement

After optimizations:
- **Remove logging overhead:** ~50-100ns per match saved
- **Remove redundant checks:** ~10-20ns per match saved
- **Total:** ~60-120ns per match saved
- **For 10,000 matches:** ~600µs-1.2ms saved

## Additional Quickwins Identified (2026-01-23)

Beyond the hot-path micro-optimizations above, there are a few small, high-impact changes that remain:

1. **Add a maximum pattern length guard before `hs_compile()`**
   - **Impact:** Prevents obviously pathological or attacker-controlled ultra-long patterns from consuming excessive CPU and memory during compilation.
   - **Effort:** Very low; a single constant (for example, `kMaxPatternLength = 1024`) and an early-return with logging in the VectorScan compile path.
   - **Status:** Not yet implemented; tracked as a quickwin in the main VectorScan performance analysis document.

2. **Introduce a simple cache size cap for `ThreadLocalDatabaseCache`**
   - **Impact:** Places a hard upper bound on per-thread memory use from cached databases, closing most of the unbounded cache growth/DoS risk even before a full LRU implementation.
   - **Effort:** Low; add a `kMaxCacheSize` constant and clear-or-refuse insertion when the map size exceeds this threshold (a full LRU can be added later if needed).
   - **Status:** Not yet implemented; design remains as “bounded cache with LRU”, but a simple cap would already be a meaningful improvement.

3. **Add minimal metrics (cache hit/miss and compile time)**
   - **Impact:** Gives immediate visibility into cache effectiveness and expensive patterns, which is essential for future optimization and regression detection.
   - **Effort:** Low; a small metrics struct, a couple of atomic counters, and timing around `hs_compile()` are sufficient for a first version.
   - **Status:** Not yet implemented; recommended as a lightweight first step before any more advanced monitoring infrastructure.

## Implementation Plan

1. **Immediate:** Remove debug logging from hot path
2. **Immediate:** Remove redundant runtime check
3. **Immediate:** Remove unused field
4. **Next:** Profile to identify remaining bottlenecks
