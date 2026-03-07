# Clang-Tidy Warnings Report

**Date:** 2026-01-17  
**Last Updated:** 2026-01-17  
**Original Total Warnings:** 3,184  
**Current Estimated Total:** ~2,221 warnings (after fixes and configuration changes)  
**Files Analyzed:** 182 source files (excluding external dependencies)

## Summary

This document lists all remaining clang-tidy warnings in the USN_WINDOWS project. The warnings are organized by category and severity.

**Note:** This is the original baseline report. For current warning counts after fixes, see `2026-01-17_CLANG_TIDY_WARNINGS_UPDATE.md`.

## Warning Categories

### 1. Naming Conventions (525 warnings)
**Check:** `readability-identifier-naming`

- Invalid case style for parameters, members, namespaces, etc.
- Most common: parameter naming violations (e.g., `soaView`, `searchWorker`, `oldPrefix`, `newPrefix`)
- Member variable naming issues (e.g., `mutex_`, `isDirectory`)
- Namespace naming issues (e.g., `AppBootstrapCommon` should be `app_bootstrap_common`)

### 2. Floating Point Literal Suffix (350 warnings)
**Check:** `hicpp-uppercase-literal-suffix`, `readability-uppercase-literal-suffix`

- Floating point literals should use uppercase suffix `F` instead of lowercase `f`
- Example: `0.5f` should be `0.5F`

### 3. Magic Numbers (278 warnings)
**Check:** `cppcoreguidelines-avoid-magic-numbers`, `readability-magic-numbers`

- Common magic numbers:
  - `0.5f` (46 occurrences)
  - `0.4f` (37 occurrences)
  - `0.3f` (19 occurrences)
  - `0.8f` (17 occurrences)
  - `120` (10 occurrences)
  - `5` (9 occurrences)
  - `0.25f` (9 occurrences)
  - `30` (8 occurrences)
  - `0.7f` (8 occurrences)
  - `0.20f` (8 occurrences)
  - `60` (5 occurrences)
  - `32` (5 occurrences)
  - `1024` (6 occurrences)
  - `1024.0` (6 occurrences)

### 4. Missing `[[nodiscard]]` (229 warnings)
**Check:** `modernize-use-nodiscard`

- Functions that return important values should be marked with `[[nodiscard]]`
- Common in functions like `GetName()`, `GetDescription()`, etc.

### 5. Default Arguments (262 warnings)
**Checks:** `fuchsia-default-arguments-calls` (207), `fuchsia-default-arguments-declarations` (55)

- Calling functions with default arguments (207 warnings)
- Declaring parameters with default arguments (55 warnings)
- Note: This is a Fuchsia-specific check that may not be applicable to this project

### 6. Non-Private Member Variables (182 warnings)
**Check:** `misc-non-private-member-variables-in-classes`

- Member variables that should be private are public or protected
- Example: `isDirectory` should be private

### 7. C-Style Vararg Functions (168 warnings)
**Check:** `cppcoreguidelines-pro-type-vararg`, `hicpp-vararg`

- Do not call C-style vararg functions (e.g., `printf`, `scanf`)
- Should use type-safe alternatives

### 8. Const Correctness (134 warnings)
**Check:** `misc-const-correctness`

- Variables that can be declared `const` are not
- Example: `variable 'window_right_edge' of type 'float' can be declared 'const'`

### 9. Anonymous Namespace vs Static (114 warnings)
**Check:** `llvm-prefer-static-over-anonymous-namespace`

- Functions declared in anonymous namespace should use `static` instead
- Prefer `static` for restricting visibility

### 10. Internal Linkage (89 warnings)
**Check:** `misc-use-internal-linkage`

- Functions that can be made static or moved into anonymous namespace
- Should enforce internal linkage

### 11. Static Member Functions (109 warnings)
**Check:** `readability-convert-member-functions-to-static`

- Member functions that don't use `this` should be static

### 12. Uninitialized Variables (92 warnings)
**Check:** `cppcoreguidelines-init-variables`

- Variables that are not initialized
- Examples: `written`, `entry`, `needs_fix`, `env_key`

### 13. Include Order (70 warnings)
**Check:** `llvm-include-order`

- `#includes` are not sorted properly
- Should follow standard C++ include order

### 14. Implicit Bool Conversion (53 warnings)
**Check:** `readability-implicit-bool-conversion`

- Implicit conversions from pointer/char to bool
- Examples: `const char *` -> `bool`, `char` -> `bool`, `const UsnMonitor *` -> `bool`

### 15. Compilation Errors (51 warnings)
**Check:** `clang-diagnostic-error`

- File not found errors (e.g., `'string' file not found`, `'string.h' file not found`)
- These are likely false positives due to system header path issues

### 16. Macro Usage (50 warnings)
**Check:** `cppcoreguidelines-macro-usage`

- Usage of macros that should be replaced with constexpr/const or functions

### 17. Member Initialization (46 warnings)
**Check:** `cppcoreguidelines-pro-type-member-init`, `hicpp-member-init`

- Constructors that don't initialize all member fields
- Examples: `filename`, `extensions`, `path`, `time_filter`, `size_filter`, `error_message`

### 18. Braces Around Statements (56 warnings)
**Checks:** `google-readability-braces-around-statements`, `hicpp-braces-around-statements`, `readability-braces-around-statements`

- Statements should be inside braces (e.g., if/else without braces)

### 19. Pointer Arithmetic (37 warnings)
**Check:** `cppcoreguidelines-pro-bounds-pointer-arithmetic`

- Do not use pointer arithmetic
- Should use iterators or array indexing instead

### 20. C-Style Arrays (19 warnings)
**Check:** `cppcoreguidelines-avoid-c-arrays`, `hicpp-avoid-c-arrays`, `modernize-avoid-c-arrays`

- Do not declare C-style arrays, use `std::array` instead

### 21. Cognitive Complexity (18 warnings)
**Check:** `readability-function-cognitive-complexity`

- Functions with cognitive complexity exceeding threshold (25)
- Should be broken down into smaller functions

### 22. Const/Ref Data Members (18 warnings)
**Check:** `cppcoreguidelines-avoid-const-or-ref-data-members`

- Avoid const or reference data members
- Can cause issues with assignment operators

### 23. Missing Parentheses (16 warnings)
**Check:** `readability-math-missing-parentheses`

- `'*' has higher precedence than '+'; add parentheses to explicitly specify the order of operations`

### 24. Branch Cloning (16 warnings)
**Check:** `bugprone-branch-clone`

- Repeated branch body in conditional chain
- Should extract common code

### 25. Else After Return (15 warnings)
**Check:** `llvm-else-after-return`, `readability-else-after-return`

- Do not use `else` after `return`
- Should use early returns instead

### 26. Empty Catch Statements (14 warnings)
**Check:** `bugprone-empty-catch`

- Empty catch statements hide issues
- Should handle exceptions appropriately (re-throw, handle, or avoid catch)

### 27. Use std::min/max (13 warnings)
**Check:** `readability-use-std-min-max`

- Use `std::max` instead of `<` comparison
- Use `std::min` instead of `>` comparison

### 28. Namespace Comments (13 warnings)
**Check:** `google-readability-namespace-comments`, `llvm-namespace-comment`

- Missing namespace closing comments

### 29. Unused Parameters (13 warnings)
**Check:** `[[maybe_unused]]`

- Parameters that should be marked with `[[maybe_unused]]` or removed

### 30. Redundant Inline Specifier (12 warnings)
**Check:** `readability-redundant-inline-specifier`

- Functions with redundant `inline` specifier

### 31. Use Equals Delete (12 warnings)
**Check:** `hicpp-use-equals-delete`, `modernize-use-equals-delete`

- Deleted member function should be public
- Should use `= delete` syntax

### 32. Non-Const Global Variables (28 warnings)
**Check:** `cppcoreguidelines-avoid-non-const-global-variables`

- Global variables that should be const
- Example: `ProcessAndAddToken` should be const

### 33. Signed Bitwise Operations (8 warnings)
**Check:** `hicpp-signed-bitwise`

- Use of a signed integer operand with a binary bitwise operator

### 34. Identical If Branches (6 warnings)
**Check:** `bugprone-branch-clone`

- If with identical then and else branches

### 35. C-Style Casts (6 warnings)
**Check:** `cppcoreguidelines-pro-type-cstyle-cast`

- C-style casts are discouraged
- Should use `static_cast`/`const_cast`/`reinterpret_cast`

### 36. Use Default Constructor (5 warnings)
**Check:** `modernize-use-equals-default`

- Use `= default` to define a trivial default constructor

### 37. If with Identical Branches (6 warnings)
**Check:** `bugprone-branch-clone`

- If statement with identical then and else branches

## Recommendations

### High Priority Fixes

1. **Naming Conventions (525 warnings)** - Fix parameter and member variable naming to match project conventions
2. **Floating Point Suffix (350 warnings)** - Change `f` to `F` in all floating point literals
3. **Magic Numbers (278 warnings)** - Replace magic numbers with named constants
4. **Missing `[[nodiscard]]` (229 warnings)** - Add `[[nodiscard]]` to functions that return important values
5. **Uninitialized Variables (92 warnings)** - Initialize all variables before use

### Medium Priority Fixes

1. **Const Correctness (134 warnings)** - Add `const` to variables that don't change
2. **Include Order (70 warnings)** - Sort includes according to standard C++ order
3. **Braces Around Statements (56 warnings)** - Add braces to all if/else statements
4. **Member Initialization (46 warnings)** - Initialize all members in constructors
5. **C-Style Arrays (19 warnings)** - Replace with `std::array`

### Low Priority / Style Fixes

1. **Default Arguments (262 warnings)** - Consider if Fuchsia-specific check is applicable
2. **Anonymous Namespace vs Static (114 warnings)** - Prefer `static` over anonymous namespace
3. **Internal Linkage (89 warnings)** - Make functions static when possible
4. **Namespace Comments (13 warnings)** - Add closing namespace comments

### False Positives / System Issues

1. **Compilation Errors (51 warnings)** - File not found errors are likely due to system header path issues on macOS
2. **Default Arguments (262 warnings)** - Fuchsia-specific check may not be applicable to this Windows-focused project

## Next Steps

1. Review and prioritize warnings based on project needs
2. Fix high-priority warnings first (naming, magic numbers, uninitialized variables)
3. Address medium-priority warnings (const correctness, include order)
4. Consider disabling Fuchsia-specific checks if not applicable
5. Fix system header path issues for macOS clang-tidy runs

## Notes

- **Original warning count:** 3,184 (baseline from initial scan)
- **Current estimated count:** ~2,221 warnings (after fixes and configuration changes)
- Some warnings may be false positives (e.g., system header issues, llvmlibc-* checks)
- Some checks (like Fuchsia-specific ones) may not be applicable to this project
- **See:** `2026-01-17_CLANG_TIDY_WARNINGS_UPDATE.md` for current warning counts and progress
