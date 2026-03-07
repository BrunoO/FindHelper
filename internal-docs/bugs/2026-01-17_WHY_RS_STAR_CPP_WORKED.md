# Why `rs:*.cpp` Appeared to Work

**Date:** 2026-01-17  
**Question:** Why was `rs:*.cpp` working when `vs:*.cpp` wasn't?

## The Answer

`rs:*.cpp` appeared to work because of **optimization routing** in `std_regex_utils::RegexMatch()`, but it was actually **matching incorrectly** or **silently failing**.

## How `rs:*.cpp` Was Handled

When you used `rs:*.cpp`:

1. **Pattern Detection**: The pattern `*.cpp` was extracted (removing `rs:` prefix)

2. **Pattern Analysis**: `std_regex_utils::RegexMatch()` checks if it's a "simple pattern":
   ```cpp
   inline bool IsSimplePattern(std::string_view pattern) {
     // Allow: alphanumeric, ., *, ^, $ (SimpleRegex features)
     if (std::isalnum(c) || c == '.' || c == '*' || c == '^' || c == '$') {
       continue;  // Pattern contains only these characters
     }
   }
   ```
   - `*.cpp` contains only `*`, `.`, and alphanumeric characters
   - ✅ **Passes** `IsSimplePattern()` check

3. **Routing to SimpleRegex**: Because it's a "simple pattern", it's routed to `SimpleRegex::RegExMatch()` instead of `std::regex`:
   ```cpp
   // Fast path: Route simple patterns to SimpleRegex
   if (detail::IsSimplePattern(pattern)) {
     return detail::RouteSimplePattern(pattern, text, case_sensitive);
   }
   ```

4. **SimpleRegex Behavior**: `SimpleRegex::RegExMatch()` implements `*` as a **regex quantifier** (zero or more of preceding element), NOT as a glob wildcard:
   ```cpp
   // From SimpleRegex.h line 94:
   // *    matches zero or more occurrences of previous character
   ```
   - For `*.cpp`, the `*` at the start has **no preceding element**
   - This is **invalid regex syntax**, but `SimpleRegex` might handle it in unexpected ways

## Why It "Worked"

`rs:*.cpp` likely appeared to work because:

1. **Incorrect Matching**: `SimpleRegex` might have interpreted `*.cpp` in a way that accidentally matched some files (e.g., treating `*` as a literal character or matching incorrectly)

2. **Silent Failure**: It might have been returning `false` for all matches, but you might not have noticed if you were testing with files that didn't match anyway

3. **Different Interpretation**: `SimpleRegex::RegExMatch()` uses `SearchPatternAnywhere()` which searches for the pattern anywhere in the text, so `*.cpp` might have been matching `.cpp` as a substring

## Why `vs:*.cpp` Failed

`vs:*.cpp` failed because:

1. **Direct to VectorScan**: The pattern was passed **directly** to VectorScan without any optimization routing
2. **Strict Regex Engine**: VectorScan is a strict regex engine that expects valid regex syntax
3. **Invalid Pattern**: `*.cpp` is invalid regex (no preceding element for `*`), causing:
   - Compilation failure → fallback to std::regex (with same invalid pattern)
   - Or inefficient compilation → very slow matching
   - Or incorrect matching behavior

## The Real Issue

**Both `rs:*.cpp` and `vs:*.cpp` were broken** - they were using invalid regex patterns. The difference was:

- `rs:*.cpp`: Routed to `SimpleRegex` which might have handled it incorrectly but didn't crash
- `vs:*.cpp`: Passed directly to VectorScan which strictly enforces regex syntax and failed/slowed down

## The Fix

The fix converts glob patterns to proper regex before passing to VectorScan:
- `*.cpp` → `.*\.cpp$` (proper regex: match any characters, then literal dot, then "cpp" at end)

This should also be applied to `rs:` patterns for consistency, but since `rs:*.cpp` was routed to `SimpleRegex` (which might have worked by accident), the issue was less noticeable.

## Recommendation

Consider applying the same glob-to-regex conversion to `rs:` patterns for consistency and correctness, even though `SimpleRegex` might handle some glob patterns accidentally.
