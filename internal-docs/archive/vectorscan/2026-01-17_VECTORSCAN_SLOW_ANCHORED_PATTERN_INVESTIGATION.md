# VectorScan Slow Performance with Anchored Patterns - Investigation

**Date:** 2026-01-17  
**Status:** 🔍 Investigating  
**Issue:** VectorScan is slower than std::regex for pattern `vs:^.*\.cpp$`

## Problem

When using VectorScan with an anchored pattern like `vs:^.*\.cpp$`, VectorScan is slower than `rs:^.*\.cpp$`. This is unexpected because VectorScan should be much faster than std::regex.

## Potential Issues

### 1. Fallback Using Optimized Regex

**Issue:** When VectorScan falls back to std::regex, it was using `RegexMatch()` which has optimization routing (can use SimpleRegex or string search), while `rs:` uses `RegexMatchStrict()` which always uses std::regex.

**Fix Applied:** Changed all fallback paths to use `RegexMatchStrict()` for consistency.

### 2. Pattern Complexity

The pattern `^.*\.cpp$` contains:
- `^` - Start anchor
- `.*` - Match any characters (greedy)
- `\.` - Literal dot
- `cpp` - Literal text
- `$` - End anchor

The `.*` at the start might cause VectorScan to do more work than expected, especially if it's not optimized for this pattern structure.

### 3. VectorScan Anchored Pattern Handling

According to VectorScan/Hyperscan documentation:
- Start anchor (`^`) provides significant performance benefit
- End anchor (`$`) provides smaller marginal benefit
- Fully anchored patterns (`^...$`) should still be fast, but the `.*` wildcard might reduce the benefit

### 4. Verification Needed

We need to verify:
1. Is VectorScan actually being used, or is it falling back silently?
2. Is the pattern being compiled correctly?
3. Are there any compilation errors that cause silent fallback?

## Investigation Steps

1. **Add diagnostic logging** to verify VectorScan is being used
2. **Check compilation success** - ensure pattern compiles without errors
3. **Profile the actual matching** - see where time is spent
4. **Compare with simpler patterns** - test `^.*\.cpp$` vs `.*\.cpp$` vs `\.cpp$`

## Potential Solutions

### Solution 1: Verify VectorScan is Actually Used

Add logging to confirm VectorScan is being used:
```cpp
if (!IsRuntimeAvailable()) {
    LOG_DEBUG("VectorScan: Not available, falling back");
    return std_regex_utils::RegexMatchStrict(pattern, text, case_sensitive);
}
```

### Solution 2: Optimize Pattern for VectorScan

For patterns like `^.*\.cpp$`, we could:
- Remove unnecessary `.*` at start if pattern is anchored: `^.*\.cpp$` → `^.*\.cpp$` (no change, but verify if `.*` is needed)
- Use more specific patterns when possible

### Solution 3: Check Compilation Flags

Verify that VectorScan compilation flags are optimal:
- `HS_FLAG_DOTALL` - already set
- `HS_FLAG_CASELESS` - set when case-insensitive
- Consider if `HS_FLAG_SINGLEMATCH` would help (stop after first match)

### Solution 4: Pattern Simplification

For filename matching, `^.*\.cpp$` is equivalent to `.*\.cpp$` (without start anchor) if we're matching against filenames. The start anchor might not be necessary and could be causing overhead.

## Next Steps

1. Add diagnostic logging to verify VectorScan usage
2. Profile the matching to identify bottlenecks
3. Test with simpler patterns to isolate the issue
4. Check if VectorScan compilation is successful for this pattern
