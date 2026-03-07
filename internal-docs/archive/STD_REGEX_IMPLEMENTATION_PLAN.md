# std::regex Implementation Plan (rs: prefix)

## Overview

This document outlines the plan to add support for `std::regex` (ECMAScript regex) using the `rs:` prefix, in addition to the existing simple regex implementation (`re:`) based on Rob Pike's algorithm.

## Current State

### Existing Regex Implementation (`re:`)

- **Location**: `SimpleRegex.h`
- **Algorithm**: Rob Pike's simple regex matcher
- **Features**: `.`, `*`, `^`, `$`
- **Performance**: Very fast (header-only, no compilation overhead)
- **Usage**: Triggered by `re:` prefix in search patterns
- **Case sensitivity**: Supports both case-sensitive (`RegExMatch`) and case-insensitive (`RegExMatchI`)

### Current Usage Points

1. **FileIndex.cpp**:
   - Line 487: `use_regex = (query.size() > 3 && query.substr(0, 3) == "re:");`
   - Line 497: `use_regex_path = (path_query.size() > 3 && path_query.substr(0, 3) == "re:");`
   - Lines 613-623: Filename regex matching (case-sensitive/insensitive)
   - Lines 650-660: Path regex matching (case-sensitive/insensitive)
   - Similar patterns at lines 823, 832, 1151, 1160

2. **SearchWorker.cpp**:
   - Line 173: `if (pattern.size() > 3 && pattern.substr(0, 3) == "re:")`
   - Line 174: `return simple_regex::RegExMatch(pattern.substr(3), text);`

3. **main_gui.cpp**:
   - Lines 1759-1762: Help text explaining `re:` prefix

## Proposed Implementation

### 1. New Regex Wrapper Module

**File**: `StdRegexUtils.h` (new file)

Create a wrapper module that:
- Provides a caching mechanism for compiled `std::regex` objects
- Handles case sensitivity via `std::regex_constants::icase`
- Provides a simple interface similar to `SimpleRegex.h`
- Handles exceptions from invalid regex patterns gracefully

**Key Design Decisions**:
- **Caching**: Compile regex patterns once and reuse (critical for performance)
- **Thread Safety**: Use thread-local storage or mutex-protected cache
- **Error Handling**: Return `false` on invalid patterns (log error but don't crash)
- **Case Sensitivity**: Use `std::regex_constants::icase` flag when needed

**Proposed Interface**:
```cpp
namespace std_regex_utils {
  // Match pattern against text using std::regex
  // Returns false on invalid pattern or no match
  bool RegexMatch(const std::string& pattern, const std::string& text, bool case_sensitive = true);
  
  // Clear the regex cache (useful for testing or memory management)
  void ClearCache();
}
```

### 2. Pattern Detection Logic

**Modification Points**:

#### FileIndex.cpp
- Add `use_std_regex` and `use_std_regex_path` boolean flags
- Detection: `use_std_regex = (query.size() > 3 && query.substr(0, 3) == "rs:");`
- Similar for path query

#### SearchWorker.cpp
- Update `Matches` lambda to check for `rs:` prefix before `re:`
- Order: `rs:` → `re:` → glob → substring

### 3. Integration Points

#### FileIndex.cpp - Search() method

**Current pattern** (lines 613-623):
```cpp
if (use_regex) {
  std::string regex_pattern = query_str.substr(3);
  if (case_sensitive) {
    filename_matcher = [regex_pattern](const char *filename) {
      return simple_regex::RegExMatch(regex_pattern, filename);
    };
  } else {
    filename_matcher = [regex_pattern](const char *filename) {
      return simple_regex::RegExMatchI(regex_pattern, filename);
    };
  }
}
```

**New pattern**:
```cpp
if (use_std_regex) {
  std::string regex_pattern = query_str.substr(3);
  filename_matcher = [regex_pattern, case_sensitive](const char *filename) {
    return std_regex_utils::RegexMatch(regex_pattern, filename, case_sensitive);
  };
} else if (use_regex) {
  // ... existing simple_regex code ...
}
```

**Similar changes needed for**:
- Path matcher (lines 650-660)
- All other search methods in FileIndex.cpp (SearchAsync, SearchSync, etc.)

#### SearchWorker.cpp - Matches lambda

**Current** (lines 172-175):
```cpp
if (pattern.size() > 3 && pattern.substr(0, 3) == "re:") {
  return simple_regex::RegExMatch(pattern.substr(3), text);
}
```

**New**:
```cpp
if (pattern.size() > 3 && pattern.substr(0, 3) == "rs:") {
  return std_regex_utils::RegexMatch(pattern.substr(3), text, params.caseSensitive);
} else if (pattern.size() > 3 && pattern.substr(0, 3) == "re:") {
  return simple_regex::RegExMatch(pattern.substr(3), text);
}
```

### 4. UI Updates

#### main_gui.cpp

**Update help text** (around line 1759):
```cpp
ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "4. Regex Search");
ImGui::BulletText("Prefix your search with 're:' for simple regex (fast, limited features).");
ImGui::BulletText("Prefix your search with 'rs:' for full ECMAScript regex (slower, full features).");
ImGui::BulletText("Simple regex supports: . (any char), * (repeat), ^ (start), $ (end)");
ImGui::BulletText("ECMAScript regex supports: Full regex syntax (character classes, quantifiers, etc.)");
ImGui::BulletText("Example: 're:^main' matches files starting with 'main'.");
ImGui::BulletText("Example: 'rs:^main\\.(cpp|h)$' matches 'main.cpp' or 'main.h'.");
```

## Performance Analysis

### Expected Performance Characteristics

#### std::regex Advantages
- ✅ Full ECMAScript regex support (character classes, quantifiers, alternation, etc.)
- ✅ Standard library implementation (well-tested, maintained)
- ✅ More powerful than simple regex

#### std::regex Disadvantages
- ⚠️ **Compilation overhead**: Compiling a regex pattern is expensive (10-100x slower than simple regex)
- ⚠️ **Runtime overhead**: Matching is typically 2-5x slower than simple regex
- ⚠️ **Memory overhead**: Compiled regex objects consume more memory
- ⚠️ **Exception handling**: Invalid patterns throw exceptions (need try-catch)

### Performance Mitigation Strategies

1. **Regex Caching** (Critical):
   - Cache compiled `std::regex` objects by pattern string
   - Use `std::unordered_map<std::string, std::regex>` with mutex protection
   - Or use thread-local storage for thread-safe caching
   - **Impact**: Reduces compilation overhead from O(n) to O(1) per unique pattern

2. **Lazy Compilation**:
   - Only compile regex when first used
   - Cache for subsequent uses
   - **Impact**: No overhead for patterns that aren't used

3. **Early Exit on Invalid Patterns**:
   - Try-catch around compilation
   - Cache "invalid" patterns to avoid repeated attempts
   - **Impact**: Prevents repeated exception handling overhead

### Performance Comparison Estimates

| Operation | Simple Regex (`re:`) | std::regex (`rs:`) | Ratio |
|-----------|---------------------|-------------------|-------|
| **First match** (no cache) | ~100ns | ~5000ns | 50x slower |
| **Subsequent matches** (cached) | ~100ns | ~200-500ns | 2-5x slower |
| **Memory per pattern** | 0 bytes (header-only) | ~100-500 bytes | Higher |
| **Pattern compilation** | N/A (no compilation) | ~5000ns | N/A |

**Note**: These are rough estimates. Actual performance depends on:
- Pattern complexity
- Text length
- Compiler optimizations
- Cache hit rate

### Performance Impact Assessment

#### Best Case Scenario (Cached, Simple Pattern)
- **Overhead**: ~2-3x slower than simple regex
- **Acceptable**: Yes, for users who need full regex features

#### Worst Case Scenario (Uncached, Complex Pattern)
- **Overhead**: ~50-100x slower on first match, ~5-10x on subsequent
- **Acceptable**: Maybe, but caching is critical

#### Typical Scenario (Cached, Moderate Pattern)
- **Overhead**: ~3-5x slower than simple regex
- **Acceptable**: Yes, reasonable trade-off for functionality

### Recommendations

1. **Implement caching** - This is non-negotiable for acceptable performance
2. **Document performance trade-offs** - Users should know `rs:` is slower
3. **Consider compile-time optimization flags** - `std::regex_constants::optimize` may help
4. **Monitor performance** - Add metrics to track regex compilation and matching times

## Implementation Steps

### Phase 1: Core Implementation

1. **Create `StdRegexUtils.h`**
   - Implement regex caching mechanism
   - Add `RegexMatch()` function with case sensitivity support
   - Add error handling (try-catch for invalid patterns)
   - Add cache management functions

2. **Update `FileIndex.cpp`**
   - Add `use_std_regex` and `use_std_regex_path` detection
   - Integrate std::regex matching in all search methods:
     - `Search()` (line ~487, ~613, ~650)
     - `SearchAsync()` (line ~823, ~832)
     - `SearchSync()` (line ~1151, ~1160)
   - Update lambda captures to include case sensitivity

3. **Update `SearchWorker.cpp`**
   - Update `Matches` lambda to handle `rs:` prefix
   - Pass case sensitivity parameter

### Phase 2: UI and Documentation

4. **Update `main_gui.cpp`**
   - Update help text to explain both `re:` and `rs:`
   - Clarify performance implications

5. **Update documentation**
   - Add examples of `rs:` usage
   - Document performance characteristics
   - Update any existing regex documentation

### Phase 3: Testing and Optimization

6. **Add unit tests**
   - Test regex caching
   - Test case sensitivity
   - Test invalid patterns
   - Test performance (benchmark against simple regex)

7. **Performance profiling**
   - Measure actual performance impact
   - Optimize cache implementation if needed
   - Consider thread-local storage if mutex contention is high

## Code Structure

### StdRegexUtils.h (New File)

```cpp
#pragma once

#include <regex>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace std_regex_utils {

// Thread-safe regex cache
class RegexCache {
public:
  struct CachedRegex {
    std::regex regex;
    bool case_sensitive;
  };
  
  const std::regex* GetRegex(const std::string& pattern, bool case_sensitive);
  void Clear();
  
private:
  std::mutex mutex_;
  std::unordered_map<std::string, CachedRegex> cache_;
  std::string MakeKey(const std::string& pattern, bool case_sensitive);
};

// Main matching function
bool RegexMatch(const std::string& pattern, const std::string& text, bool case_sensitive = true);

// Cache management
void ClearCache();

} // namespace std_regex_utils
```

## Testing Plan

### Unit Tests

1. **Valid patterns**:
   - Simple patterns: `rs:^test$`
   - Character classes: `rs:^[a-z]+$`
   - Quantifiers: `rs:^test.*\.cpp$`
   - Alternation: `rs:^(main|test)\.cpp$`

2. **Case sensitivity**:
   - Case-sensitive: `rs:^Test$` should not match `test`
   - Case-insensitive: `rs:^Test$` should match `test` when `case_sensitive=false`

3. **Invalid patterns**:
   - Malformed: `rs:[invalid` should return false, not crash
   - Empty: `rs:` should handle gracefully

4. **Caching**:
   - Same pattern used twice should use cache
   - Different case sensitivity flags should create separate cache entries

### Integration Tests

1. **FileIndex search**:
   - Search with `rs:` prefix
   - Verify results match expected files
   - Compare performance with `re:` prefix

2. **SearchWorker**:
   - Test `rs:` in filename search
   - Test `rs:` in path search
   - Verify case sensitivity works correctly

### Performance Tests

1. **Benchmark**:
   - Compare `re:` vs `rs:` for same simple patterns
   - Measure cache hit impact
   - Measure compilation overhead

2. **Stress test**:
   - Many unique patterns (cache growth)
   - Very long patterns
   - Complex patterns with many alternations

## Risk Assessment

### Low Risk
- ✅ Adding new prefix doesn't break existing functionality
- ✅ std::regex is standard library (well-tested)
- ✅ Can be disabled/removed if issues arise

### Medium Risk
- ⚠️ Performance impact if caching not implemented correctly
- ⚠️ Exception handling overhead if many invalid patterns
- ⚠️ Memory usage if cache grows unbounded

### Mitigation
- Implement cache size limits (LRU eviction)
- Monitor cache hit rates
- Add performance metrics
- Document performance characteristics clearly

## Backward Compatibility

- ✅ Existing `re:` prefix continues to work unchanged
- ✅ No changes to existing APIs
- ✅ New feature is opt-in (users must use `rs:` prefix)
- ✅ Default behavior unchanged (substring → glob → regex detection)

## Future Enhancements

1. **Regex validation**: Pre-validate patterns in UI (show error before search)
2. **Pattern history**: Remember recently used patterns
3. **Performance metrics**: Show regex compilation time in debug mode
4. **Cache statistics**: Display cache hit rate in debug mode
5. **Alternative regex engines**: Consider PCRE2 or RE2 for better performance

## Conclusion

Adding `std::regex` support via `rs:` prefix is feasible and provides valuable functionality. The key to acceptable performance is **implementing regex caching**. Without caching, the performance penalty would be unacceptable for typical use cases.

**Recommendation**: Proceed with implementation, prioritizing regex caching as the most critical component.
