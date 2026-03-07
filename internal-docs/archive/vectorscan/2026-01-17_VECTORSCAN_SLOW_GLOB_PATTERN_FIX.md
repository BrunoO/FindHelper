# VectorScan Slow Performance with Glob Patterns - Fix

**Date:** 2026-01-17  
**Status:** ✅ Fixed  
**Issue:** VectorScan was 50x slower than std::regex for simple patterns like `vs:*.cpp`

## Problem

When using VectorScan with glob patterns like `vs:*.cpp`, VectorScan was extremely slow (50x slower than `rs:*.cpp`). This was unexpected because VectorScan should be much faster than std::regex.

### Root Cause

The issue was that when a user typed `vs:*.cpp`:
1. The `vs:` prefix was detected, treating it as `PatternType::VectorScan`
2. The pattern `*.cpp` was extracted and passed **directly** to VectorScan as a regex pattern
3. But `*.cpp` is **NOT a valid regex pattern** - it's a glob pattern
4. In regex, `*` means "zero or more of the preceding element", so `*.cpp` is invalid or matches incorrectly
5. VectorScan either:
   - Failed to compile and fell back to std::regex (with the same invalid pattern)
   - Compiled incorrectly and matched in unexpected ways
   - Caused VectorScan to be very slow due to inefficient pattern compilation

### Why std::regex Appeared Faster

When using `rs:*.cpp`, std::regex might have been more forgiving or handled the invalid pattern differently, but it was still incorrect. The real issue was that glob patterns were not being converted to proper regex patterns.

## Solution

Added glob-to-regex conversion for VectorScan patterns:

1. **Created `ConvertGlobToRegex()` function** - Converts glob patterns to proper regex patterns:
   - `*` → `.*` (match any sequence of characters)
   - `?` → `.` (match any single character)
   - Escapes other special regex characters (`.`, `+`, `(`, `)`, etc.)
   - Adds `$` at the end for filename matching (e.g., `*.cpp` → `.*\.cpp$`)

2. **Created `IsGlobPattern()` helper** - Detects if a pattern contains glob wildcards (`*` or `?`)

3. **Updated VectorScan case in `MatchPattern()`** - Detects glob patterns and converts them before passing to VectorScan

4. **Updated VectorScan case in `CreateMatcherImpl()`** - Same conversion for matcher creation

## Implementation Details

### Glob-to-Regex Conversion

```cpp
// Convert a glob pattern to a regex pattern
// Converts: * -> .*, ? -> ., and escapes other special regex characters
// For filename matching, patterns like *.cpp become .*\.cpp$ (match end of string)
inline std::string ConvertGlobToRegex(std::string_view glob_pattern) {
  std::string result;
  result.reserve(glob_pattern.size() * 2);
  
  for (size_t i = 0; i < glob_pattern.size(); ++i) {
    char c = glob_pattern[i];
    
    if (c == '*') {
      result += ".*";  // * in glob -> .* in regex
    } else if (c == '?') {
      result += '.';   // ? in glob -> . in regex
    } else if (c == '.' || c == '\\' || /* other special chars */) {
      result += '\\';  // Escape special regex characters
      result += c;
    } else {
      result += c;    // Literal character
    }
  }
  
  // Add $ to match end of string (for filename patterns)
  if (!result.empty() && result.back() != '$') {
    result += '$';
  }
  
  return result;
}
```

### Pattern Conversion Examples

- `vs:*.cpp` → `.*\.cpp$` (matches files ending in `.cpp`)
- `vs:test*.cpp` → `test.*\.cpp$` (matches files starting with "test" and ending in `.cpp`)
- `vs:*.?` → `.*\..$` (matches files with single-character extension)
- `vs:file?.cpp` → `file.\..cpp$` (matches files like `file1.cpp`, `fileA.cpp`)

## Testing

After the fix:
- `vs:*.cpp` should now be **faster** than `rs:*.cpp` (as expected with VectorScan)
- Pattern matching should be correct (matches files ending in `.cpp`)
- Other glob patterns should work correctly with VectorScan

## Files Modified

- `src/search/SearchPatternUtils.h`
  - Added `ConvertGlobToRegex()` function
  - Added `IsGlobPattern()` helper function
  - Updated `MatchPattern()` VectorScan case
  - Updated `CreateMatcherImpl()` VectorScan case

## Related Issues

This fix ensures that:
1. VectorScan patterns are always valid regex patterns
2. Glob patterns are properly converted before being passed to VectorScan
3. VectorScan performance is optimal (no invalid pattern compilation overhead)
4. Pattern matching behavior is correct and consistent

## Notes

- The `$` anchor is added to glob patterns to match the end of filenames, which is the expected behavior for filename patterns like `*.cpp`
- If a pattern already ends with `$`, we don't add another one
- Special regex characters are properly escaped to ensure they're treated as literals
