# Complexity Issues Fix Plan
**Date:** 2026-01-24  
**Last Updated:** 2026-01-24  
**Total Issues:** 12 complexity-related issues remaining (8 fixed)

## Overview
This plan addresses all remaining complexity issues from SonarQube, sorted from simplest to most complex. Each category builds on techniques learned in previous fixes.

**Progress:** Phase 1 (Nesting Depth) is complete! ✅ All 7 issues fixed.

---

## Phase 1: Nesting Depth (cpp:S134) - ✅ COMPLETE
**Difficulty:** ⭐ Simple  
**Technique:** Early returns, extract helper functions, invert conditions  
**Status:** All 7 issues fixed

### Fixed Issues:
- ✅ `src/ui/ResultsTable.cpp:621` - Fixed by extracting `RenderVisibleTableRows()` helper
- ✅ `src/search/SearchWorker.cpp:605` - Fixed by extracting `ProcessSearchFutures()` helper
- ✅ `src/usn/UsnMonitor.cpp:759` - Fixed by extracting helper functions
- ✅ `src/usn/UsnMonitor.cpp:769, 777, 781, 785` - Fixed by extracting `ProcessInterestingUsnRecord()` and `ProcessUsnRecordReasons()` helpers
- ✅ `src/usn/UsnMonitor.cpp:822` - Fixed by using early continue pattern

**Techniques Used:**
- Extracted helper functions (`static inline` for performance)
- Early returns/continues to reduce nesting
- Inverted conditions for cleaner control flow

---

## Phase 2: Nested Breaks (cpp:S924) - 2 issues
**Difficulty:** ⭐⭐ Simple-Medium  
**Technique:** Extract functions with early returns, use flags, or combine loop conditions  
**Estimated Time:** 15-20 min each

### 2.1 `src/path/PathPatternMatcher.cpp:1026` (2 breaks)
- **Status:** Ready
- **Approach:** Combine loop conditions or extract function with early return
- **Priority:** Medium

### 2.2 `src/path/PathPatternMatcher.cpp:973` (3 breaks)
- **Status:** Ready
- **Approach:** Extract function with early return or use flag pattern
- **Priority:** Medium

---

## Phase 3: Too Many Parameters (cpp:S107) - 3 issues
**Difficulty:** ⭐⭐⭐ Medium  
**Technique:** Group related parameters into structs  
**Estimated Time:** 20-30 min each

### 3.1 `src/index/InitialIndexPopulator.cpp:40` (8 parameters)
- **Status:** Ready
- **Approach:** Group MFT-related parameters into `MftProcessingParams` struct
- **Parameters to group:**
  - `MftMetadataReader* mft_reader`
  - `size_t& mft_success_count`
  - `size_t& mft_failure_count`
  - `size_t& mft_total_files`
- **Priority:** Medium

### 3.2 `src/index/InitialIndexPopulator.cpp:145` (10 parameters)
- **Status:** Ready
- **Approach:** Same as 3.1 - group MFT parameters into struct
- **Note:** Same function family, can use same struct
- **Priority:** Medium

### 3.3 `src/path/PathPatternMatcher.cpp:417` (8 parameters)
- **Status:** Ready
- **Approach:** Group related parameters into struct (analyze which parameters are related)
- **Priority:** Low (path matching code, less frequently modified)

---

## Phase 4: Cognitive Complexity (cpp:S3776) - 7 issues
**Difficulty:** ⭐⭐⭐⭐ Complex  
**Technique:** Extract helper functions, break into smaller focused functions  
**Estimated Time:** 20-40 min each (depending on complexity)

### 4.1 `src/path/PathPatternMatcher.cpp:1447` (Complexity: 34 → 25)
- **Status:** Ready
- **Approach:** Extract helper functions for complex logic blocks
- **Priority:** Medium
- **Reduction needed:** 9 points

### 4.2 `src/search/SearchWorker.cpp:507` (Complexity: 36 → 25)
- **Status:** Ready
- **Approach:** Extract helper functions for complex logic
- **Priority:** High (search code, performance-critical)
- **Reduction needed:** 11 points
- **Note:** New issue - may have been introduced by recent refactoring

### 4.3 `src/search/SearchPatternUtils.h:209` (Complexity: 37 → 25)
- **Status:** Ready
- **Approach:** Extract helper functions (template function, may need careful handling)
- **Priority:** Medium
- **Reduction needed:** 12 points
- **Note:** Header file, template function - ensure inline helpers

### 4.4 `src/search/SearchThreadPool.cpp:52` (Complexity: 41 → 25)
- **Status:** Ready
- **Approach:** Extract helper functions for complex conditionals
- **Priority:** Medium
- **Reduction needed:** 16 points

### 4.5 `src/usn/UsnMonitor.cpp:389` (Complexity: 43 → 25)
- **Status:** Ready
- **Approach:** Extract helper functions for complex logic
- **Priority:** Medium
- **Reduction needed:** 18 points
- **Note:** New issue - may have been introduced by recent refactoring

### 4.6 `src/search/SearchPatternUtils.h:209` (Complexity: 43 → 25)
- **Status:** Ready
- **Approach:** Same as 4.3 (duplicate entry - same function, different measurement)
- **Priority:** Medium
- **Reduction needed:** 18 points
- **Note:** Same function as 4.3, appears twice in SonarQube (likely different analysis runs)

### 4.7 `src/path/PathPatternMatcher.cpp:928` (Complexity: 50 → 25)
- **Status:** Ready
- **Approach:** Major refactoring - extract multiple helper functions
- **Priority:** High (most complex)
- **Reduction needed:** 25 points
- **Note:** This is the most complex single function

**Removed Issues (Fixed):**
- ✅ `src/search/SearchWorker.cpp:462` (Complexity: 60) - Fixed or refactored
- ✅ `src/usn/UsnMonitor.cpp:318` (Complexity: 43) - Fixed
- ✅ `src/usn/UsnMonitor.cpp:717` (Complexity: 42) - Fixed

---

## Execution Strategy

### Recommended Order:
1. ✅ **Phase 1** (Nesting Depth) - **COMPLETE** - All 7 issues fixed
2. **Next: Phase 2** (Nested Breaks) - Similar techniques to Phase 1
3. **Then Phase 3** (Parameters) - Structural changes, test carefully
4. **Finally Phase 4** (Cognitive Complexity) - Most complex, requires careful refactoring

### Within Each Phase:
- Start with simpler issues first
- For cognitive complexity, start with lower complexity values (34, 36, 37) before tackling 50
- Commit after each successful fix to maintain working state

### Testing Strategy:
- Run `scripts/build_tests_macos.sh` after each fix
- Verify functionality is preserved
- Check that no new issues are introduced

### Performance Considerations:
- Use `static inline` for extracted helper functions in hot paths
- Ensure extracted functions don't add overhead
- Profile if needed (especially for search/path matching code)

---

## Notes

### Common Patterns:
- **Early returns:** Use `if (!condition) return;` to reduce nesting
- **Helper functions:** Extract logical blocks into focused functions
- **Struct grouping:** Group related parameters (e.g., MFT params, search params)
- **Flag pattern:** For nested breaks, use boolean flags or extract functions

### Risk Assessment:
- **Low Risk:** ✅ Nesting depth (COMPLETE), nested breaks (mostly mechanical fixes)
- **Medium Risk:** Parameter grouping (requires interface changes)
- **High Risk:** High cognitive complexity (50) - requires careful refactoring

### Special Considerations:
- `SearchPatternUtils.h` is a header file - ensure inline helpers
- `PathPatternMatcher.cpp` and `SearchWorker.cpp` are performance-critical
- `UsnMonitor.cpp` has multiple issues in same function - can fix together

---

## Progress Tracking

- [x] Phase 1: Nesting Depth (7/7) ✅ **COMPLETE**
- [ ] Phase 2: Nested Breaks (0/2)
- [ ] Phase 3: Too Many Parameters (0/3)
- [ ] Phase 4: Cognitive Complexity (0/7)

**Total Progress:** 7/19 issues fixed (37% complete)

**Remaining:** 12 issues
- Phase 2: 2 issues (nested breaks)
- Phase 3: 3 issues (too many parameters)
- Phase 4: 7 issues (cognitive complexity)
