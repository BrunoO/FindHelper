# Regex Prefix Simplification - Require Valid Regex for rs: and vs:

**Date:** 2026-01-17  
**Status:** ✅ Completed  
**Change:** Simplified behavior - `rs:` and `vs:` prefixes now require valid regex patterns

## Summary

Simplified the behavior of `rs:` and `vs:` prefixes to require valid regex patterns. No automatic routing or conversion - if the user explicitly uses these prefixes, they must provide valid regex syntax.

## Changes Made

### 1. Removed Glob-to-Regex Conversion for `vs:`

**Before:**
- `vs:*.cpp` was automatically converted to `.*\.cpp$`
- This was confusing - user expected VectorScan but got automatic conversion

**After:**
- `vs:*.cpp` requires valid regex (will fail or be slow if invalid)
- User must provide valid regex: `vs:.*\.cpp$` for VectorScan

### 2. Removed SimpleRegex Routing for `rs:`

**Before:**
- `rs:*.cpp` was routed to `SimpleRegex` (optimization)
- This was confusing - user expected std::regex but got SimpleRegex

**After:**
- `rs:*.cpp` always uses `std::regex` (strict mode)
- User must provide valid regex: `rs:.*\.cpp$` for std::regex

### 3. Removed Helper Functions

- Removed `ConvertGlobToRegex()` - no longer needed
- Removed `IsGlobPattern()` - no longer needed

### 4. Added Strict Regex Function

- Added `RegexMatchStrict()` in `StdRegexUtils.h` - always uses std::regex, no routing

## Code Changes

### `src/utils/StdRegexUtils.h`

Added `RegexMatchStrict()` function:
```cpp
// Match pattern against text using std::regex (strict mode - always uses std::regex, no routing)
// Returns false on invalid pattern or no match
// Use this for rs: patterns where user explicitly wants std::regex
inline bool RegexMatchStrict(std::string_view pattern, std::string_view text, bool case_sensitive = true);
```

### `src/search/SearchPatternUtils.h`

**StdRegex case:**
- Changed from `std_regex_utils::RegexMatch()` (with routing) to `std_regex_utils::RegexMatchStrict()` (no routing)
- Removed pattern analysis and SimpleRegex routing
- Always uses std::regex directly

**VectorScan case:**
- Removed glob-to-regex conversion
- Pattern is passed directly to VectorScan (must be valid regex)

**CreateMatcherImpl:**
- Updated `StdRegex` case to always use std::regex (no routing)
- Updated `VectorScan` case to remove glob conversion

## User Impact

### Before (Confusing)
- `rs:*.cpp` → Routed to SimpleRegex (unexpected)
- `vs:*.cpp` → Converted to `.*\.cpp$` (unexpected)

### After (Clear)
- `rs:*.cpp` → Requires valid regex (fails if invalid)
- `vs:*.cpp` → Requires valid regex (fails if invalid)
- `*.cpp` → Uses glob matching (no prefix = glob)
- `pp:**/*.cpp` → Uses path pattern matching

## Benefits

1. **Clearer semantics**: If user uses `rs:` or `vs:`, they get exactly what they ask for
2. **No surprises**: No automatic routing or conversion
3. **Simpler code**: Removed conversion logic and routing complexity
4. **Better error messages**: Invalid regex patterns fail clearly instead of being silently converted

## Migration Guide

If users were relying on automatic conversion:

**Old (broken):**
- `rs:*.cpp` → Worked (but used SimpleRegex, not std::regex)
- `vs:*.cpp` → Worked (but was converted to regex)

**New (correct):**
- `rs:.*\.cpp$` → Valid regex for std::regex
- `vs:.*\.cpp$` → Valid regex for VectorScan
- `*.cpp` → Use glob pattern (no prefix)

## Related Files

- `src/utils/StdRegexUtils.h` - Added `RegexMatchStrict()`
- `src/search/SearchPatternUtils.h` - Removed conversion, updated routing
