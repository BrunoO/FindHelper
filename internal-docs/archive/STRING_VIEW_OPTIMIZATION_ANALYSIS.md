# string_view Optimization Analysis for Regex Implementation

## Current Issues

### 1. Unnecessary String Allocations in Fast Paths

**Problem**: The `string_view` and `const char*` overloads convert to `std::string` even for fast paths that support `string_view` directly.

**Current Code**:
```cpp
// string_view overload
inline bool RegexMatch(const std::string& pattern, std::string_view text, bool case_sensitive = true) {
  return RegexMatch(pattern, std::string(text), case_sensitive);  // ❌ Unnecessary allocation
}

// const char* overload  
inline bool RegexMatch(const std::string& pattern, const char* text, bool case_sensitive = true) {
  return RegexMatch(pattern, std::string(text), case_sensitive);  // ❌ Unnecessary allocation
}
```

**Impact**: 
- For literal patterns: `ContainsSubstring` already supports `string_view` - no conversion needed
- For simple patterns: `SimpleRegex::RegExMatch` already supports `string_view` - no conversion needed
- Only `std::regex` requires `std::string` (unavoidable)

### 2. Pattern Analysis Functions Use `std::string`

**Problem**: Pattern analysis functions (`IsSimplePattern`, `IsLiteralPattern`, `RequiresFullMatch`) take `const std::string&` but only read the pattern.

**Current Code**:
```cpp
inline bool IsSimplePattern(const std::string& pattern) { ... }  // ❌ Could be string_view
inline bool IsLiteralPattern(const std::string& pattern) { ... }  // ❌ Could be string_view
inline bool RequiresFullMatch(const std::string& pattern) { ... }  // ❌ Could be string_view
```

**Impact**: Forces string allocation when pattern comes from `string_view` or `const char*`.

---

## Optimization Opportunities

### ✅ Fast Paths Support string_view

1. **Literal Patterns**: `ContainsSubstring(string_view, string_view)` ✅
2. **Simple Patterns**: `SimpleRegex::RegExMatch(string_view, string_view)` ✅
3. **std::regex**: Requires `std::string` (unavoidable) ❌

### Optimization Strategy

1. **Change pattern analysis to `string_view`** - No allocations needed
2. **Use `string_view` directly in fast paths** - Avoid string conversion
3. **Only convert to `std::string` for `std::regex`** - Minimal conversion

---

## Performance Impact

### Before Optimization
- **string_view overload**: Always allocates string (even for fast paths)
- **const char* overload**: Always allocates string (even for fast paths)
- **Pattern analysis**: Forces string allocation

### After Optimization
- **Literal patterns**: Zero allocations (uses `string_view` directly)
- **Simple patterns**: Zero allocations (uses `string_view` directly)
- **Complex patterns**: One allocation (only for `std::regex`, unavoidable)

### Expected Speedup
- **Literal patterns**: 5-20% faster (no string allocation overhead)
- **Simple patterns**: 5-20% faster (no string allocation overhead)
- **Complex patterns**: No change (still need string for `std::regex`)

---

## Implementation Plan

1. Change pattern analysis functions to use `string_view`
2. Refactor `RegexMatch` to handle `string_view` in fast paths
3. Only convert to `std::string` when calling `std::regex`
