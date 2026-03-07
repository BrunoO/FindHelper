# Investigation: vs:.*\.cpp$ Pattern Not Matching

## Issue
The user reported that `vs:.*\.cpp$` returns no results while `rs:.*\.cpp$` works correctly.

## Investigation Findings

### 1. Pattern Detection
✅ The pattern prefix detection is correct:
- Patterns starting with `vs:` are correctly identified as `PatternType::VectorScan`
- The prefix is correctly extracted (removes "vs:" and passes ".*\.cpp$" to VectorScan)

### 2. Compilation Status
✅ Code compiles without errors when `USE_VECTORSCAN` is enabled
- VectorScan is enabled in the build (`-DUSE_VECTORSCAN` is defined)
- Both `RegexMatchPrecompiled()` and `GetCache()` functions are accessible

### 3. Syntax Check
The pattern `.*\.cpp$` is syntactically correct:
- `.*` - Match any character zero or more times (valid PCRE)
- `\.` - Escaped dot, literal dot character (valid PCRE)
- `cpp` - Literal text (valid)
- `$` - End-of-string anchor (valid PCRE)

### 4. Code Implementation
✅ The implementation is correct:
- Database is pre-compiled once (not on every match)
- Text is passed as `string_view` (no allocation overhead)
- The lambda captures the pre-compiled database pointer

## Possible Root Causes

### Scenario 1: VectorScan Pattern Compilation Failure
**Symptom:** Pattern fails to compile in Hyperscan
**Why:** VectorScan might use slightly different PCRE syntax than expected
- Some PCRE features might not be supported
- The `$` anchor might behave differently in BLOCK mode
- The escaped `\.` might need different escaping

**How to verify:**
- Check build logs for "VectorScan: Failed to compile pattern" messages
- Enable DEBUG logging in VectorScanUtils.cpp to see compilation errors

### Scenario 2: Pattern Match Fails
**Symptom:** Pattern compiles but `hs_scan()` returns no matches
**Why:** 
- VectorScan's `$` anchor might require special handling
- The pattern might match differently than std::regex (e.g., matching substrings vs full strings)
- Text encoding or format differences

**How to verify:**
- Enable DEBUG logging to see if "Scan result=" appears
- Compare with what rs: returns

### Scenario 3: VectorScan Runtime Not Available
**Symptom:** `IsRuntimeAvailable()` returns false
**Why:** VectorScan library not loaded at runtime
- VectorScan compiled in but library not installed
- Library installed but not found by dynamic linker

**How to verify:**
- Check if Hyperscan/VectorScan library is installed (`brew info vectorscan`)
- Verify library is in `LD_LIBRARY_PATH` or system library paths

## Recommended Debug Steps

1. **Add comprehensive logging** (Already done):
   ```cpp
   LOG_ERROR_BUILD("VectorScan: Pattern compilation failed for: " << pattern);
   LOG_DEBUG_BUILD("VectorScan: Database is nullptr");
   LOG_DEBUG_BUILD("VectorScan: Scan result=" << match_ctx.matched);
   ```

2. **Test with simpler patterns first**:
   - Try `vs:cpp$` (no .*)
   - Try `vs:.*\.cpp` (no $)
   - Try `vs:file.cpp` (no regex)

3. **Check if pattern works in MatchPattern()** (not just CreateMatcher):
   - This is the simpler code path (no lambda)
   - If this works, the issue is in the lambda
   - If this doesn't work, the issue is in the pattern/compilation

4. **Compare actual behavior**:
   - Run with `rs:.*\.cpp$` - what files match?
   - Run with `vs:.*\.cpp$` - what files match?
   - Run with `vs:cpp$` - what files match?

## Next Steps

The issue is likely related to either:
1. **VectorScan pattern dialect differences** - Need to test if simpler patterns work
2. **Anchor handling** - The `$` anchor might need `HS_FLAG_MULTILINE` or other flags
3. **Runtime availability** - VectorScan might not be loaded

The debug logging added should provide more information. Test and check the logs to identify which scenario matches the observed behavior.
