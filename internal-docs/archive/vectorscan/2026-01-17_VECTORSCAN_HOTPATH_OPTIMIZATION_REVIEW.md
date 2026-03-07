# VectorScan Hot Path Optimization Review

**Date:** 2026-01-17  
**Status:** 🔍 Analysis Complete  
**Goal:** Identify remaining optimization opportunities in VectorScan hot path

## Current Hot Path Flow

```
CreateMatcherImpl() [once per pattern]
  ↓
GetDatabase() [once - pre-compiles database]
  ↓
Lambda created with captured db (shared_ptr)
  ↓
[For each match - HOT PATH]
  ↓
RegexMatchPrecompiled(db, text)
  ├─ database.get() [shared_ptr dereference]
  ├─ GetThreadLocalScratch() [function call + hs_alloc_scratch check]
  ├─ MatchContext initialization
  ├─ Text size check
  ├─ hs_scan() [actual matching]
  └─ Return match_ctx.matched
```

## Identified Optimization Opportunities

### 1. ⚠️ **shared_ptr Overhead in Hot Path** - HIGH IMPACT
**Location:** `RegexMatchPrecompiled()` parameter  
**Impact:** Reference counting on every match call  
**Complexity:** LOW

**Current Code:**
```cpp
bool RegexMatchPrecompiled(const std::shared_ptr<hs_database_t>& database, std::string_view text)
```

**Problem:**
- `shared_ptr` reference counting on every call (atomic operations)
- Lambda already holds `shared_ptr`, so lifetime is guaranteed
- Passing raw pointer would eliminate this overhead

**Fix:** Pass raw pointer instead of shared_ptr
```cpp
bool RegexMatchPrecompiled(const hs_database_t* database, std::string_view text)
```

**Expected Savings:** ~5-10ns per match (atomic reference count operations)

### 2. ⚠️ **GetThreadLocalScratch() Function Call Overhead** - MEDIUM IMPACT
**Location:** Called on every match  
**Impact:** Function call + potential hs_alloc_scratch validation  
**Complexity:** MEDIUM

**Current Code:**
```cpp
hs_scratch_t* scratch = GetThreadLocalScratch(database.get());
```

**Problem:**
- Function call overhead on every match
- `hs_alloc_scratch()` is called every time (though it's fast if scratch already allocated)
- Could cache scratch pointer in lambda to avoid function call

**Fix Options:**
- **Option A:** Cache scratch pointer in lambda (but scratch is shared across databases)
- **Option B:** Keep current (scratch is thread-local, function call is minimal)
- **Option C:** Inline GetThreadLocalScratch() (but it's already optimized)

**Recommendation:** Keep current - scratch is thread-local and reused, function call overhead is minimal (~1-2ns)

### 3. ⚠️ **Text Size Check Branch Misprediction** - LOW IMPACT
**Location:** `RegexMatchPrecompiled()`  
**Impact:** Branch prediction penalty (rarely taken)  
**Complexity:** TRIVIAL

**Current Code:**
```cpp
if (text.size() > static_cast<size_t>(std::numeric_limits<unsigned int>::max())) {
    LOG_WARNING_BUILD("VectorScan: Text size exceeds unsigned int limit, cannot match");
    return false;
}
```

**Problem:**
- This check is almost always false for filenames (< 4GB)
- Branch misprediction penalty when compiler can't predict
- Unlikely to be hit in practice

**Fix Options:**
- **Option A:** Remove check (filenames are never > 4GB)
- **Option B:** Use `likely()` attribute to hint compiler
- **Option C:** Keep check but mark as unlikely

**Recommendation:** Keep check but add `[[unlikely]]` attribute for better branch prediction

**Expected Savings:** ~1-2ns per match (better branch prediction)

### 4. ⚠️ **string_view Construction for const char*** - LOW IMPACT
**Location:** Lambda in `SearchPatternUtils.h`  
**Impact:** Minor overhead constructing string_view  
**Complexity:** TRIVIAL

**Current Code:**
```cpp
if constexpr (std::is_same_v<TextType, const char*>) {
    return vectorscan_utils::RegexMatchPrecompiled(db, std::string_view(text));
}
```

**Problem:**
- Constructing `string_view` from `const char*` requires `strlen()` call
- This is O(n) where n is string length
- For long filenames, this could add overhead

**Fix:** Pass `const char*` directly and construct string_view inside (but we need length)
- Actually, `hs_scan()` needs length anyway, so we'd need to compute it
- Current approach is optimal

**Recommendation:** Keep current - string_view construction is necessary

### 5. ⚠️ **MatchContext Initialization** - NEGLIGIBLE
**Location:** `RegexMatchPrecompiled()`  
**Impact:** Minimal - just bool initialization  
**Status:** Already optimal

**Current Code:**
```cpp
MatchContext match_ctx;  // Just bool = false
```

**Recommendation:** Keep as-is - already optimal

## Recommended Optimizations (Priority Order)

### Priority 1: Remove shared_ptr Overhead ⚠️ HIGH IMPACT
**Impact:** ~5-10ns per match saved  
**Effort:** 10 minutes  
**Risk:** LOW (lambda holds shared_ptr, lifetime guaranteed)

**Implementation:**
1. Change `RegexMatchPrecompiled()` to accept raw pointer
2. Update lambda to pass `db.get()` instead of `db`
3. Update all call sites

### Priority 2: Add [[unlikely]] to Text Size Check ⚠️ LOW IMPACT
**Impact:** ~1-2ns per match (better branch prediction)  
**Effort:** 2 minutes  
**Risk:** NONE

**Implementation:**
```cpp
if (text.size() > static_cast<size_t>(std::numeric_limits<unsigned int>::max())) [[unlikely]] {
    LOG_WARNING_BUILD("VectorScan: Text size exceeds unsigned int limit, cannot match");
    return false;
}
```

## Expected Total Performance Improvement

After optimizations:
- **Remove shared_ptr overhead:** ~5-10ns per match
- **Better branch prediction:** ~1-2ns per match
- **Total:** ~6-12ns per match saved
- **For 10,000 matches:** ~60-120µs saved

## Comparison with std::regex

**std::regex approach:**
- Passes raw pointer (`const regex_t*`) - no shared_ptr overhead ✅
- Text size check not needed (std::string handles it)
- Function call overhead similar

**VectorScan after optimization:**
- Will pass raw pointer (matches std::regex pattern) ✅
- Text size check with [[unlikely]] (minimal overhead)
- Function call overhead similar

## Implementation Plan

1. **Immediate:** Remove shared_ptr from hot path (Priority 1)
2. **Immediate:** Add [[unlikely]] to text size check (Priority 2)
3. **Future:** Profile to verify improvements
