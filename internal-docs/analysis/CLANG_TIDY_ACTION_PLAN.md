# Clang-Tidy Action Plan

This document outlines the action plan to address findings from the clang-tidy classification analysis. The plan is organized by priority and includes specific tasks, file locations, and implementation steps.

**Date:** January 16, 2025  
**Status:** Planning Phase

---

## Overview

The classification document (`CLANG_TIDY_CLASSIFICATION.md`) identified four categories of clang-tidy findings:
1. **Highly Recommended** (Reliability & Security) - 4 checks
2. **Recommended** (Performance & Maintainability) - 5 check categories
3. **Style** (Consistency) - 4 checks (1 needs adjustment)
4. **Noise** (Ignore or Disable) - 4 checks (already disabled ✓)

---

## Phase 1: Highly Recommended (Priority: CRITICAL)

These checks identify potential bugs, logic errors, or security risks. **Address these first.**

### 1.1 `bugprone-branch-clone` - Identical Switch Branches

**Status:** Needs Investigation  
**Priority:** HIGH  
**Files to Check:**
- `src/filters/SizeFilterUtils.cpp` - Switch has 4 consecutive identical branches (mentioned in classification)

**Action Items:**
1. [ ] Run clang-tidy on `SizeFilterUtils.cpp` to identify exact location of identical branches
2. [ ] Review switch statements in `SizeFilterToString()`, `SizeFilterDisplayLabel()`, and `MatchesSizeFilter()`
3. [ ] Refactor to eliminate duplicate code:
   - Extract common logic to helper functions
   - Use lookup tables/maps for string conversions
   - Consolidate similar case handling
4. [ ] Verify fix doesn't break functionality
5. [ ] Run tests to ensure correctness

**Expected Outcome:** Eliminate duplicate switch branches, improve maintainability

---

### 1.2 `bugprone-empty-catch` - Empty Catch Blocks

**Status:** Needs Investigation  
**Priority:** HIGH  
**Files to Check:**
- `src/search/SearchResultUtils.cpp` - Empty catch statement (mentioned in classification)

**Action Items:**
1. [ ] Search codebase for empty catch blocks: `catch (...) { }` or `catch (Type&) { }`
2. [ ] Review each empty catch block:
   - Determine if exception should be logged
   - Determine if exception should be re-thrown
   - Determine if exception should be handled differently
3. [ ] Fix each empty catch block:
   - Add logging: `LOG_ERROR("Exception: " << e.what());`
   - Re-throw if appropriate: `throw;`
   - Handle gracefully with error state/return codes
4. [ ] Document why exceptions are caught (if intentionally ignored)
5. [ ] Run tests to verify error handling

**Expected Outcome:** All exceptions are properly handled, no silent failures

---

### 1.3 `bugprone-narrowing-conversions` - Narrowing Conversions

**Status:** Needs Investigation  
**Priority:** MEDIUM-HIGH  
**Files to Check:**
- `src/ui/MetricsWindow.cpp` - Narrowing conversion from `size_t` to `double` (mentioned in classification)

**Action Items:**
1. [ ] Run clang-tidy on `MetricsWindow.cpp` to identify exact narrowing conversions
2. [ ] Review each narrowing conversion:
   - `size_t` to `double` - Usually safe but should be explicit
   - `int` to smaller types - May lose data
   - `unsigned` to `signed` - May lose data
3. [ ] Fix narrowing conversions:
   - Use `static_cast<target_type>(value)` for intentional conversions
   - Use appropriate types (e.g., `double` instead of `int` if needed)
   - Add bounds checking if conversion could overflow
4. [ ] Verify fix doesn't change behavior
5. [ ] Run tests to ensure correctness

**Expected Outcome:** All narrowing conversions are explicit and safe

---

### 1.4 `bugprone-implicit-widening-of-multiplication-result` - Multiplication Overflow

**Status:** Needs Investigation  
**Priority:** MEDIUM-HIGH  
**Files to Check:**
- `src/api/GeminiApiUtils.cpp` - Multiplication in `int` before widening to `size_t` (mentioned in classification)

**Action Items:**
1. [ ] Run clang-tidy on `GeminiApiUtils.cpp` to identify exact multiplication issues
2. [ ] Review each multiplication:
   - Check if operands are `int` but result needs `size_t`
   - Check if multiplication could overflow before widening
3. [ ] Fix multiplication issues:
   - Cast operands to `size_t` before multiplication: `static_cast<size_t>(a) * static_cast<size_t>(b)`
   - Use `std::multiplies<size_t>()` for explicit type
   - Add overflow checks if necessary
4. [ ] Verify fix doesn't change behavior
5. [ ] Run tests to ensure correctness

**Expected Outcome:** All multiplications are type-safe and won't overflow

---

## Phase 2: Recommended (Priority: HIGH)

These checks improve code efficiency or long-term maintainability.

### 2.1 `performance-*` - Performance Optimizations

**Status:** Ongoing  
**Priority:** MEDIUM  
**Action Items:**
1. [ ] Run clang-tidy with `performance-*` checks enabled
2. [ ] Review performance warnings:
   - Unnecessary copies (use `const&` or move semantics)
   - Inefficient string operations
   - Unnecessary allocations
3. [ ] Fix performance issues:
   - Use `std::move()` for rvalue references
   - Use `const&` for read-only parameters
   - Use `std::string_view` where appropriate
   - Avoid unnecessary copies in loops
4. [ ] Benchmark critical paths if changes are significant
5. [ ] Verify fixes don't break functionality

**Expected Outcome:** Improved performance, reduced allocations

---

### 2.2 `misc-const-correctness` - Const Correctness

**Status:** Partially Addressed  
**Priority:** MEDIUM  
**Action Items:**
1. [ ] Run clang-tidy with `misc-const-correctness` enabled
2. [ ] Review const correctness suggestions:
   - Variables that are never modified should be `const`
   - Member functions that don't modify state should be `const`
   - Parameters that are only read should be `const&`
3. [ ] Fix const correctness issues:
   - Add `const` to variables: `const int value = ...;`
   - Add `const` to member functions: `int GetValue() const { ... }`
   - Add `const` to parameters: `void Process(const Data& data)`
4. [ ] Verify fixes compile and tests pass
5. [ ] Document any cases where const can't be added (and why)

**Expected Outcome:** Better code safety, clearer intent, compiler optimizations

---

### 2.3 `readability-braces-around-statements` - Braces Enforcement

**Status:** Needs Configuration  
**Priority:** LOW-MEDIUM  
**Action Items:**
1. [ ] Check if `readability-braces-around-statements` is enabled in `.clang-tidy`
2. [ ] Configure brace style (if not already):
   - Option: `Always` (enforce braces for all control flow)
   - Option: `MultiLine` (only enforce for multi-line statements)
3. [ ] Run clang-tidy to identify missing braces
4. [ ] Fix missing braces:
   - Add braces to all `if`, `for`, `while`, `do-while` statements
   - Ensure consistent style across codebase
5. [ ] Update `.clang-format` if needed to match style
6. [ ] Verify fixes compile and tests pass

**Expected Outcome:** Consistent brace style, prevents "dangling else" bugs

---

### 2.4 `readability-magic-numbers` - Magic Numbers

**Status:** Already Configured  
**Priority:** LOW (Already Handled)  
**Current Status:**
- ✅ Already configured in `.clang-tidy` with appropriate ignored values
- ✅ Common values (0, 1, -1, powers of 2) are ignored
- ✅ Floating point values (0.0, 0.5, 1.0) are ignored

**Action Items:**
1. [ ] Run clang-tidy to identify any remaining magic numbers
2. [ ] Review flagged magic numbers:
   - Determine if they should be named constants
   - Determine if they're self-explanatory (e.g., `strlen(str) == 0`)
3. [ ] Extract magic numbers to named constants:
   - Use `kPascalCase` naming convention
   - Place in appropriate namespace or class
   - Document purpose if not obvious
4. [ ] Verify fixes compile and tests pass

**Expected Outcome:** Better maintainability, self-documenting code

---

## Phase 3: Style (Priority: MEDIUM)

These checks ensure consistent coding style across the project.

### 3.1 `readability-identifier-naming` - Naming Conventions

**Status:** Needs Adjustment  
**Priority:** MEDIUM  
**Issue:** Current config may force `KPascalCase` while project uses `kPascalCase`

**Action Items:**
1. [ ] Review current `.clang-tidy` naming configuration
2. [ ] Verify constant naming:
   - Current: `ConstantPrefix: 'k'` with `ConstantCase: CamelCase`
   - Expected: Should produce `kPascalCase` (e.g., `kBufferSize`)
   - Check if `CamelCase` with `k` prefix produces correct result
3. [ ] Test naming configuration:
   - Create test file with constants: `const int kBufferSize = 1024;`
   - Run clang-tidy to verify it accepts `kPascalCase`
   - If not, adjust configuration
4. [ ] Fix configuration if needed:
   - Ensure `ConstantPrefix: 'k'` and `ConstantCase: CamelCase` work together
   - May need to use `PascalCase` instead of `CamelCase` for constants
5. [ ] Run clang-tidy on codebase to identify naming violations
6. [ ] Fix naming violations to match project conventions

**Expected Outcome:** Consistent naming across codebase, matches `CXX17_NAMING_CONVENTIONS.md`

---

### 3.2 `modernize-use-equals-default` - Default Functions

**Status:** Should Be Enabled  
**Priority:** LOW  
**Action Items:**
1. [ ] Verify `modernize-use-equals-default` is enabled (should be via `*`)
2. [ ] Run clang-tidy to identify functions that can use `= default`
3. [ ] Review each suggestion:
   - Default constructors: `MyClass() = default;`
   - Default destructors: `~MyClass() = default;`
   - Copy/move constructors/operators: `MyClass(const MyClass&) = default;`
4. [ ] Apply `= default` where appropriate:
   - Only for compiler-generated functions
   - Don't use if custom logic is needed
5. [ ] Verify fixes compile and tests pass

**Expected Outcome:** Modern C++17 style, clearer intent

---

### 3.3 `readability-convert-member-functions-to-static` - Static Member Functions

**Status:** Should Be Enabled  
**Priority:** LOW  
**Action Items:**
1. [ ] Verify `readability-convert-member-functions-to-static` is enabled (should be via `*`)
2. [ ] Run clang-tidy to identify member functions that don't use `this`
3. [ ] Review each suggestion:
   - Functions that don't access member variables
   - Functions that don't call other member functions
   - Functions that could be static or free functions
4. [ ] Convert to static where appropriate:
   - Make function `static` if it doesn't need instance
   - Consider moving to free function if it doesn't need class context
5. [ ] Verify fixes compile and tests pass
6. [ ] Update call sites if function moved to free function

**Expected Outcome:** Better encapsulation, clearer dependencies

---

### 3.4 `modernize-avoid-c-arrays` - C-Style Arrays

**Status:** Should Be Enabled  
**Priority:** MEDIUM  
**Action Items:**
1. [ ] Verify `modernize-avoid-c-arrays` is enabled (should be via `*`)
2. [ ] Run clang-tidy to identify C-style arrays
3. [ ] Review each C-style array:
   - Fixed-size arrays: Replace with `std::array<T, N>`
   - Dynamic arrays: Replace with `std::vector<T>`
   - String buffers: Replace with `std::string` or `std::array<char, N>`
4. [ ] Fix C-style arrays:
   - Use `std::array` for fixed-size: `std::array<char, 256> buffer;`
   - Use `std::vector` for dynamic: `std::vector<int> data;`
   - Use `std::string` for strings: `std::string path;`
5. [ ] Update code that uses arrays:
   - Replace `arr[i]` with `arr.at(i)` or `arr[i]` (std::array supports both)
   - Replace `sizeof(arr)` with `arr.size()`
   - Replace pointer arithmetic with iterators
6. [ ] Verify fixes compile and tests pass

**Expected Outcome:** Type-safe arrays, better bounds checking, modern C++

---

## Phase 4: Noise (Priority: NONE - Already Handled)

These checks are already disabled in `.clang-tidy`:

- ✅ `modernize-use-trailing-return-type` - Disabled (line 40)
- ✅ `misc-include-cleaner` - Disabled (line 38)
- ✅ `readability-identifier-length` - Disabled (line 42)
- ✅ `bugprone-easily-swappable-parameters` - Disabled (line 30)

**No action needed** - These are already properly disabled.

---

## Implementation Strategy

### Approach

1. **Incremental Fixes:** Address issues file-by-file or check-by-check to avoid large changes
2. **Test After Each Phase:** Run tests after each phase to ensure no regressions
3. **Documentation:** Update code comments and documentation as needed
4. **Code Review:** Review changes carefully, especially for reliability fixes

### Execution Order

1. **Week 1:** Phase 1 (Highly Recommended) - Critical reliability fixes
2. **Week 2:** Phase 2 (Recommended) - Performance and maintainability
3. **Week 3:** Phase 3 (Style) - Consistency improvements
4. **Ongoing:** Monitor clang-tidy output in CI/CD pipeline

### Testing Strategy

- Run unit tests after each file/check fix
- Run integration tests after each phase
- Verify no performance regressions (especially for performance fixes)
- Check Windows, macOS, and Linux builds

### Success Criteria

- [ ] All Phase 1 (Highly Recommended) issues fixed
- [ ] All Phase 2 (Recommended) issues fixed or documented
- [ ] All Phase 3 (Style) issues fixed
- [ ] All tests pass
- [ ] No performance regressions
- [ ] Code review approved

---

## Tools and Commands

### Running Clang-Tidy

```bash
# Generate compile_commands.json
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Run on specific file
clang-tidy src/filters/SizeFilterUtils.cpp -p build

# Run on all source files
find src -name "*.cpp" -not -path "*/external/*" | xargs clang-tidy -p build

# Run specific check
clang-tidy src/filters/SizeFilterUtils.cpp -p build -checks='bugprone-branch-clone'
```

### Running Tests

```bash
# macOS (as per project requirements)
scripts/build_tests_macos.sh

# Windows (if available)
# Run tests via Visual Studio or CMake test target
```

---

## Notes

- **Naming Convention:** Follow `docs/standards/CXX17_NAMING_CONVENTIONS.md`
- **Code Style:** Follow project conventions and `.clang-format`
- **Documentation:** Update code comments when fixing issues
- **Boy Scout Rule:** Improve code quality while fixing issues

---

## References

- **Classification Document:** `docs/analysis/CLANG_TIDY_CLASSIFICATION.md`
- **Clang-Tidy Guide:** `docs/analysis/CLANG_TIDY_GUIDE.md`
- **Naming Conventions:** `docs/standards/CXX17_NAMING_CONVENTIONS.md`
- **Project Rules:** `AGENTS.md`

---

*Last Updated: January 16, 2025*
