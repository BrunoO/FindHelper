# SearchPatternUtils.h Duplication Analysis

**Date:** 2026-01-06  
**File:** `src/search/SearchPatternUtils.h`  
**Issue:** Significant code duplication between `CreateFilenameMatcher` and `CreatePathMatcher`

## Summary

The file contains **~270 lines of duplicated code** between two nearly identical functions:
- `CreateFilenameMatcher` (lines 118-250, ~133 lines)
- `CreatePathMatcher` (lines 255-387, ~133 lines)

**Duplication Rate:** ~90% of the logic is identical between the two functions.

## Duplication Details

### Functions Affected

1. **`CreateFilenameMatcher`** (lines 118-250)
   - Returns: `LightweightCallable<bool, const char *>`
   - Parameter type in lambdas: `const char *filename`

2. **`CreatePathMatcher`** (lines 255-387)
   - Returns: `LightweightCallable<bool, std::string_view>`
   - Parameter type in lambdas: `std::string_view dir_path`

### Duplicated Code Blocks

#### 1. StdRegex Case (~70 lines duplicated)
**Lines 127-195 vs 264-332**

Both functions contain identical logic for:
- Pattern extraction and validation
- ReDoS protection (length check)
- Pattern analysis
- Regex compilation
- Three optimization paths (literal, simple, complex)

**Only difference:** Lambda parameter type (`const char*` vs `std::string_view`)

#### 2. PathPattern Case (~15 lines duplicated)
**Lines 197-213 vs 334-350**

Identical logic for:
- Pattern extraction
- Options setup
- Pattern compilation
- Lambda creation

**Only difference:** Lambda parameter type

#### 3. Glob Case (~12 lines duplicated)
**Lines 215-227 vs 352-364**

Identical logic for:
- Pattern string capture
- Case-sensitive/insensitive branching
- Lambda creation

**Only difference:** Lambda parameter type

#### 4. Substring Case (~16 lines duplicated)
**Lines 229-245 vs 366-382**

Similar logic with minor differences:
- **CreateFilenameMatcher:** Uses `strstr()` for case-sensitive
- **CreatePathMatcher:** Uses `ContainsSubstring()` for case-sensitive
- Both use same case-insensitive logic

## Root Cause

The duplication exists because:
1. **Different return types:** `LightweightCallable<bool, const char*>` vs `LightweightCallable<bool, std::string_view>`
2. **Different lambda parameter types:** Required by the different use cases
3. **Historical evolution:** Functions were likely created separately and evolved independently

## Impact

- **Maintenance burden:** Bug fixes and improvements must be applied twice
- **Code size:** ~270 lines of duplicated code
- **Risk:** Inconsistencies can arise if one function is updated but not the other
- **Readability:** Makes the file harder to understand

## Refactoring Options

### Option 1: Template Function (Recommended)

Create a template function that works with both types:

```cpp
template<typename TextType>
inline auto CreateMatcherImpl(std::string_view pattern, bool case_sensitive,
                              std::string_view pattern_lower = "") {
  // Common implementation
  // Use type traits or SFINAE to handle type-specific differences
}
```

**Pros:**
- Eliminates all duplication
- Single source of truth
- Type-safe

**Cons:**
- More complex template code
- May need type traits for `const char*` vs `string_view` differences

### Option 2: Helper Function with Type Adapter

Extract common logic into helper functions, use adapters for type differences:

```cpp
// Common implementation
template<typename TextType>
inline auto CreateMatcherCommon(...) { ... }

// Wrappers
inline auto CreateFilenameMatcher(...) {
  return CreateMatcherCommon<const char*>(...);
}

inline auto CreatePathMatcher(...) {
  return CreateMatcherCommon<std::string_view>(...);
}
```

**Pros:**
- Cleaner separation
- Easier to understand
- Maintains existing API

**Cons:**
- Still some duplication in adapters

### Option 3: Type Traits Pattern

Use type traits to handle differences:

```cpp
template<typename TextType>
struct TextTypeTraits;

template<>
struct TextTypeTraits<const char*> {
  static bool ContainsSubstring(const char* text, std::string_view pattern) {
    return strstr(text, pattern.data()) != nullptr;
  }
};

template<>
struct TextTypeTraits<std::string_view> {
  static bool ContainsSubstring(std::string_view text, std::string_view pattern) {
    return string_search::ContainsSubstring(text, pattern);
  }
};
```

**Pros:**
- Very clean
- Type-safe
- Extensible

**Cons:**
- More boilerplate
- Requires careful design

## Recommendation

**Use Option 1 (Template Function)** with Option 3 (Type Traits) for type-specific differences.

This approach:
1. Eliminates all duplication
2. Maintains type safety
3. Keeps the code maintainable
4. Preserves existing API (wrappers can call template)

## Implementation Steps

1. Create type traits for `const char*` and `std::string_view`
2. Extract common logic into template function
3. Handle type-specific differences via traits
4. Create wrapper functions that call template
5. Test thoroughly (especially performance-critical paths)
6. Remove old duplicated code

## Testing Considerations

- **Performance:** Ensure template doesn't add overhead
- **Correctness:** Verify both filename and path matching work correctly
- **Edge cases:** Test empty patterns, invalid patterns, etc.
- **Case sensitivity:** Verify both case-sensitive and case-insensitive paths

## Estimated Effort

- **Time:** 2-4 hours
- **Risk:** Low (refactoring, not changing behavior)
- **Testing:** 1-2 hours
- **Total:** 3-6 hours

## Related Files

- `src/search/ParallelSearchEngine.cpp` - Uses `CreateFilenameMatcher`
- `src/index/FileIndex.cpp` - Uses both functions
- `src/search/SearchWorker.cpp` - Uses `MatchPattern`

## Notes

- The duplication is intentional for performance (different types, different optimizations)
- However, the logic is identical enough that templates can handle it
- Consider this refactoring as part of code quality improvements

