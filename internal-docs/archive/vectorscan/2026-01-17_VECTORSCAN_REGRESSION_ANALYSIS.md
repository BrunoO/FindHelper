# VectorScan Regression Analysis - Changes Made Today

**Date:** 2026-01-17  
**Status:** 🔍 Investigating  
**Issue:** VectorScan searches that were working initially are now broken

## Changes Made Today

### 1. Performance Optimization (Main Change)
**File:** `src/search/SearchPatternUtils.h`

**Before:**
```cpp
case PatternType::VectorScan: {
  std::string pattern_str(regex_pattern);
  bool case_flag = case_sensitive;
  
  return [pattern_str, case_flag](TextType text) {
    std::string text_str = Traits::ConvertToStdString(text);
    return vectorscan_utils::RegexMatch(pattern_str, text_str, case_flag);
  };
}
```

**After:**
```cpp
case PatternType::VectorScan: {
  auto db = vectorscan_utils::GetCache().GetDatabase(regex_pattern, case_sensitive);
  if (!db) {
    return [](TextType) { return false; };
  }
  
  return [db](TextType text) {
    if constexpr (std::is_same_v<TextType, const char*>) {
      return vectorscan_utils::RegexMatchPrecompiled(db, std::string_view(text));
    } else {
      return vectorscan_utils::RegexMatchPrecompiled(db, text);
    }
  };
}
```

### 2. Added HS_FLAG_MULTILINE
**File:** `src/utils/VectorScanUtils.cpp`

Added `HS_FLAG_MULTILINE` to compilation flags to fix `$` anchor issues.

### 3. Added Debug Logging
Added extensive logging throughout VectorScan matching code.

## Potential Regression Points

### Issue 1: String Conversion Difference
**Old code:** Converted `TextType` to `std::string` using `Traits::ConvertToStdString(text)`
**New code:** Passes `std::string_view` directly

**Potential problem:**
- `std::string` guarantees null-termination
- `std::string_view` from `const char*` uses `strlen()` which should work
- But if `TextType` is `std::string_view` (not `const char*`), the `string_view` might point to a substring that's not null-terminated
- However, `hs_scan()` takes `const char*` and length, so null-termination shouldn't matter

**Verdict:** ✅ Likely not the issue - `hs_scan()` uses length parameter, not null-termination

### Issue 2: Database Compilation Timing
**Old code:** Database compiled on first `RegexMatch()` call (lazy)
**New code:** Database compiled during matcher creation (eager)

**Potential problem:**
- If compilation fails during matcher creation, matcher always returns false
- Old code would try compilation on each match (though cached)
- If there's a compilation issue, old code might have worked differently

**Verdict:** ⚠️ **POSSIBLE ISSUE** - Need to check if compilation is failing silently

### Issue 3: HS_FLAG_MULTILINE Side Effects
**Change:** Added `HS_FLAG_MULTILINE` flag

**Potential problem:**
- `HS_FLAG_MULTILINE` changes how `^` and `$` work
- Might affect patterns that were working before
- Could cause different matching behavior

**Verdict:** ⚠️ **POSSIBLE ISSUE** - This flag changes anchor behavior

### Issue 4: RegexMatchPrecompiled Implementation
**New function:** `RegexMatchPrecompiled()` was added

**Potential problem:**
- Skips `RequiresFallback()` check
- Might not handle edge cases the same way
- Different error handling

**Verdict:** ⚠️ **POSSIBLE ISSUE** - New code path might have bugs

## Debugging Steps

1. **Check if compilation is failing:**
   - Look for "VectorScan: Pattern compilation failed" in logs
   - Check if `GetDatabase()` returns `nullptr`

2. **Check if callback is being called:**
   - Look for "VectorScan: OnMatch callback called!" in logs
   - If callback isn't called, pattern isn't matching

3. **Compare old vs new behavior:**
   - Test with simple patterns (no `$` anchor)
   - Test with patterns that worked before
   - Check if `HS_FLAG_MULTILINE` is causing issues

4. **Verify text being passed:**
   - Check log output for actual text being matched
   - Verify text format matches what old code received

## Recommended Fixes

### Fix 1: Revert HS_FLAG_MULTILINE (Test)
Temporarily remove `HS_FLAG_MULTILINE` to see if that's causing the issue:
```cpp
unsigned int GetCompileFlags(bool case_sensitive) {
    unsigned int flags = HS_FLAG_DOTALL;
    // flags |= HS_FLAG_MULTILINE; // TEMPORARILY REMOVED
    if (!case_sensitive) {
        flags |= HS_FLAG_CASELESS;
    }
    return flags;
}
```

### Fix 2: Add Fallback to Old Code Path
If compilation fails, fall back to old `RegexMatch()` approach:
```cpp
auto db = vectorscan_utils::GetCache().GetDatabase(regex_pattern, case_sensitive);
if (!db) {
  // Fallback to old approach
  std::string pattern_str(regex_pattern);
  bool case_flag = case_sensitive;
  return [pattern_str, case_flag](TextType text) {
    std::string text_str = Traits::ConvertToStdString(text);
    return vectorscan_utils::RegexMatch(pattern_str, text_str, case_flag);
  };
}
```

### Fix 3: Verify RegexMatchPrecompiled Matches Old Behavior
Ensure `RegexMatchPrecompiled()` behaves identically to `RegexMatch()` for the same inputs.

## Next Steps

1. **Check logs** - Look for compilation failures or callback issues
2. **Test without MULTILINE** - See if removing the flag fixes it
3. **Compare old vs new** - Test patterns that worked before
4. **Add fallback** - If compilation fails, use old code path
