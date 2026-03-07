# Uninitialized Variables Fix Strategy

**Date:** 2026-01-17  
**Priority:** HIGH (Safety issue)  
**Total Warnings:** 92  
**Check:** `cppcoreguidelines-init-variables`

## Executive Summary

Uninitialized variables can lead to undefined behavior and bugs. This document outlines the strategy for fixing all 92 uninitialized variable warnings in the codebase.

## Problem

Variables declared without initialization can contain garbage values, leading to undefined behavior when used. Even if variables are assigned before use, clang-tidy flags them as potentially unsafe.

## Strategy

### Principle: Always Initialize Variables

Every variable should be initialized at declaration:
1. **Safe defaults:** Initialize with safe default values (0, false, nullptr, etc.)
2. **C++17 init-statements:** Use init-statements when variable is only used in if block
3. **Direct initialization:** Initialize from function calls or expressions when possible

### Fix Patterns

#### Pattern 1: Variables Assigned in Conditional Blocks

```cpp
// ❌ Before - Variable may be uninitialized
bool needs_fix;
if (condition) {
  needs_fix = true;
}

// ✅ After - Initialize with safe default
bool needs_fix = false;
if (condition) {
  needs_fix = true;
}
```

#### Pattern 2: Variables Assigned from Function Calls

```cpp
// ❌ Before - Variable may be uninitialized
size_t thread_count;
if (settings_) {
  thread_count = settings_->searchThreadPoolSize;
}

// ✅ After - Initialize with safe default
size_t thread_count = 0;
if (settings_) {
  thread_count = settings_->searchThreadPoolSize;
}
```

#### Pattern 3: Variables in Init-Statements (False Positives)

```cpp
// ❌ Before - clang-tidy flags this (false positive)
if (const size_t first_sep = label_str.find(" + "); first_sep != std::string::npos) {
  // ...
}

// ✅ After - Add NOSONAR (variable IS initialized in init-statement)
if (const size_t first_sep = label_str.find(" + "); first_sep != std::string::npos) {  // NOSONAR(cpp:S6578) - Variable initialized in C++17 init-statement
  // ...
}
```

#### Pattern 4: Variables Used in Loops

```cpp
// ❌ Before - Variable may be uninitialized
size_t chunk_end;
for (size_t i = 0; i < count; ++i) {
  chunk_end = CalculateEnd(i);
}

// ✅ After - Initialize with safe default
size_t chunk_end = 0;
for (size_t i = 0; i < count; ++i) {
  chunk_end = CalculateEnd(i);
}
```

## Files to Fix (92 warnings)

### High Priority (Most Common Patterns)

1. **SearchWorker.cpp** (15 warnings)
   - Variables: `hasExtensionFilter`, `estimated_total`, `futures_count`, `min_time`, `max_time`, `min_bytes`, `max_bytes`, `avg_time`, `avg_bytes`, `resultsCount`, `count`, `id`, `last_slash`, `extension_offset`

2. **FileIndex.cpp / FileIndex.h** (10 warnings)
   - Variables: `last_slash`, `parent_id`, `file_id`, `is_directory`, `num_threads` (3 instances), `entry` (3 instances)

3. **PathStorage.cpp** (5 warnings)
   - Variables: `path` (3 instances), `path_start` (2 instances)

4. **Application.cpp** (7 warnings)
   - Variables: `thread_count`, `idle_seconds`, `has_background_work`, `was_building`, `is_search_active_before`, `is_search_active_after`, `indexed_count`

5. **PathPatternMatcher.cpp** (5 warnings)
   - Variables: `in_class`, `has_m`, `has_n`, `reached_accept`

6. **SearchPatternUtils.h** (7 warnings)
   - Variables: `has_double_star`, `has_char_class`, `has_quantifier`, `has_d_or_w_escape`, `type` (2 instances)

7. **ResultsTable.cpp** (4 warnings)
   - Variables: `display_path_cstr`, `cache_valid`, `filename_cstr`, `ext_cstr`

8. **FileSystemUtils.h** (5 warnings)
   - Variables: `stat_result`, `written` (3 instances), `timeinfo`

9. **Utils Files** (10 warnings)
   - Logger.h: `xdg_cache`, `home`
   - CpuFeatures.cpp: `max_leaf`, `has_avx2`, `has_avx`
   - LoadBalancingStrategy.cpp: `chunk_end` (2 instances)
   - SystemIdleDetector.cpp: `idle_seconds`

10. **Other Files** (24 warnings)
    - Various files with 1-3 warnings each

## Implementation Plan

### Phase 1: High-Impact Files (SearchWorker, FileIndex, Application)
1. Fix SearchWorker.cpp (15 warnings)
2. Fix FileIndex.cpp/h (10 warnings)
3. Fix Application.cpp (7 warnings)

### Phase 2: Path-Related Files
1. Fix PathStorage.cpp (5 warnings)
2. Fix PathPatternMatcher.cpp (5 warnings)
3. Fix PathBuilder.cpp, DirectoryResolver.cpp

### Phase 3: Search-Related Files
1. Fix SearchPatternUtils.h (7 warnings)
2. Fix SearchController.cpp (2 warnings)

### Phase 4: UI Files
1. Fix ResultsTable.cpp (4 warnings)
2. Fix EmptyState.cpp (1 warning - false positive)
3. Fix other UI files

### Phase 5: Utility Files
1. Fix FileSystemUtils.h (5 warnings)
2. Fix Logger.h, CpuFeatures.cpp, etc.

### Phase 6: Remaining Files
1. Fix all remaining files with 1-3 warnings

## Testing Strategy

After fixing each file:
1. **Build verification** - Ensure code compiles
2. **Run tests** - Verify no regressions
3. **Check clang-tidy** - Verify warnings are resolved

## Success Criteria

- ✅ All 92 uninitialized variable warnings resolved
- ✅ All variables initialized with safe defaults
- ✅ No regressions in functionality
- ✅ No performance penalties
- ✅ No SonarQube violations introduced

## Notes

- **Init-statements:** Some warnings are false positives for C++17 init-statements - use NOSONAR
- **Performance:** Initializing variables has zero performance impact (compile-time safety)
- **Safety:** Always initialize with safe defaults (0, false, nullptr) when value is assigned conditionally
