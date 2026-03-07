# Test Coverage Analysis: Pattern Matcher Setup Refactoring

## Current Test Coverage

### ✅ Covered Scenarios
1. **Basic searches** - All 4 strategies tested with simple filename queries
2. **Extension filters** - Tested across strategies
3. **Path queries** - One test exists ("Search with path query")
4. **Folders-only filter** - Tested
5. **Case sensitivity** - Implicitly tested (default is case-insensitive)

### ❌ Missing Test Coverage

Before refactoring the duplicated pattern matcher setup code, we need tests for:

#### 1. Pattern Type Coverage
- [ ] **Regex patterns** (`re:pattern`) - filename query
- [ ] **Glob patterns** (`*`, `?`) - filename query  
- [ ] **Path patterns** (`pp:pattern`) - filename query
- [ ] **Path patterns** (`pp:pattern`) - path query
- [ ] **Standard regex** (`rs:pattern`) - filename query
- [ ] **Standard regex** (`rs:pattern`) - path query

#### 2. Precompiled Pattern Coverage
- [ ] **Precompiled filename pattern** - when `pp:` prefix is used
- [ ] **Precompiled path pattern** - when `pp:` prefix is used in path query
- [ ] **On-the-fly compilation** - when no `pp:` prefix (substring, regex, glob)

#### 3. Extension-Only Mode
- [ ] **Extension-only mode** - pattern matchers should NOT be created
- [ ] **Extension-only + path query** - should still create path matcher

#### 4. Case Sensitivity
- [ ] **Case-sensitive filename query** - all pattern types
- [ ] **Case-sensitive path query** - all pattern types
- [ ] **Case-insensitive filename query** - all pattern types
- [ ] **Case-insensitive path query** - all pattern types

#### 5. Strategy Coverage
- [ ] **Static strategy** - all pattern scenarios
- [ ] **Hybrid strategy** - all pattern scenarios
- [ ] **Dynamic strategy** - all pattern scenarios
- [ ] **Interleaved strategy** - all pattern scenarios

#### 6. Edge Cases
- [ ] **Empty filename query + path query** - only path matcher created
- [ ] **Filename query + empty path query** - only filename matcher created
- [ ] **Both queries empty** - no matchers created (extension-only mode)
- [ ] **Invalid patterns** - should handle gracefully

## Test Plan

### Test Structure
Create a new test suite: `Pattern Matcher Setup` that tests all combinations:

```cpp
TEST_SUITE("Pattern Matcher Setup") {
  // Test each pattern type
  TEST_CASE("Regex patterns (re:)") { ... }
  TEST_CASE("Glob patterns (*, ?)") { ... }
  TEST_CASE("Path patterns (pp:) - precompiled") { ... }
  TEST_CASE("Standard regex (rs:)") { ... }
  
  // Test precompiled vs on-the-fly
  TEST_CASE("Precompiled filename pattern") { ... }
  TEST_CASE("On-the-fly filename pattern compilation") { ... }
  
  // Test extension-only mode
  TEST_CASE("Extension-only mode skips pattern matchers") { ... }
  
  // Test case sensitivity
  TEST_CASE("Case-sensitive pattern matching") { ... }
  TEST_CASE("Case-insensitive pattern matching") { ... }
  
  // Test all strategies
  TEST_CASE("Static strategy pattern matchers") { ... }
  TEST_CASE("Hybrid strategy pattern matchers") { ... }
  TEST_CASE("Dynamic strategy pattern matchers") { ... }
  TEST_CASE("Interleaved strategy pattern matchers") { ... }
}
```

## Refactoring Safety

Once these tests are added, the refactoring will be safe because:
1. ✅ All pattern types are tested
2. ✅ Precompiled vs on-the-fly paths are tested
3. ✅ All 4 strategies are tested
4. ✅ Edge cases are covered
5. ✅ ASan will catch memory errors
6. ✅ Integrity test will catch filter bypass bugs

## Implementation Order

1. **Add missing tests** (this document)
2. **Run tests to establish baseline** (all should pass)
3. **Refactor duplicated code** (extract to helper function)
4. **Run tests again** (all should still pass)
5. **Verify ASan passes** (no memory errors)

