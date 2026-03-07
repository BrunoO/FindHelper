# C++ Technical Debt Detection Prompt

**Purpose:** Comprehensive technical debt review tailored for this C++17 Windows file indexing codebase.

---

## Prompt

```
You are an expert C++17 refactoring engineer specializing in detecting technical debt. You have a strong ability to detect bugs, flaws and inefficiencies.
Review the code of this Cross-platform file indexing application: Windows USN Journal real time monitor with macOS and Linux support (no real time monitoring for MacOs and Linux).

## Code Standards Context
- **Naming Conventions**: Follow Google C++ Style Guide with project-specific adaptations:
  - Types/Functions/Methods: `PascalCase`
  - Local variables/parameters: `snake_case`
  - Member variables: `snake_case_` (trailing underscore)
  - Globals: `g_snake_case`
  - Constants: `kPascalCase`
- **Standard**: C++17 with Windows API interop
- **Architecture**: Header-heavy with template code, multi-threaded with atomic operations

---
### General Rules for Refactoring and Simplification

**Core Principles:**
- **Preserve existing behaviors**: Never change functionality during refactoring - only improve code structure, readability, or performance
- **Prefer small, incremental improvements**: Break large refactorings into smaller, testable steps that can be committed independently
- **Boy Scout Rule**: Leave code better than you found it - fix obvious issues, improve naming, add const correctness when touching code
- **DRY (Don't Repeat Yourself)**: Eliminate code duplication by extracting shared logic into reusable functions or utilities
- **KISS (Keep It Simple, Stupid)**: Favor simple, readable solutions over clever or complex ones - if code needs extensive comments to explain, simplify it
- **YAGNI (You Aren't Gonna Need It)**: Don't add functionality "just in case" - only implement what's actually needed
- **SOLID compliance**: Apply Single Responsibility, Open/Closed, Liskov Substitution, Interface Segregation, and Dependency Inversion principles

**Refactoring Guidelines:**
- **Test before refactoring**: Ensure existing tests pass before making changes
- **One change per commit**: Each refactoring step should be a single, focused improvement
- **Document non-obvious changes**: Add comments explaining why complex logic exists, not what it does
- **Verify cross-platform**: Ensure changes work on all target platforms (Windows, macOS, Linux)
- **Performance priority**: Favor execution speed over memory usage (this is a performance-critical application)
- **Thread safety first**: Never compromise thread safety for simplicity - always maintain proper synchronization

## Scan Categories

These 9 categories define the technical debt areas you will scan in the Review Process below.

### 1. Dead/Unused Code
- Functions, variables, includes, forward declarations never called
- Template specializations never instantiated
- `#ifdef` branches for removed platforms or features
- Orphaned `.cpp`/`.h` pairs after refactoring
- Includes that are never used
- Forward declarations that are never used
- Headers that are never included
- legacy code that is never used
- code forgotten after refactoring

### 2. C++ Technical Debt (Post-Refactoring)
- Duplicated logic (copy-paste template instantiations, repeated locking patterns)
- Overly complex conditionals (nested `if` chains that could be `std::variant`/`std::optional`)
- Broken error paths (exceptions not caught, RAII leaks, early returns bypassing cleanup)
- Inconsistent ownership semantics (`raw ptr` vs `unique_ptr` vs `shared_ptr` in same context)
- Redundant locking (holding mutex longer than needed, lock scope too wide)
- Thread-safety gaps (data races, missing `std::atomic`, unprotected shared state)

### 3. Aggressive DRY / Code Deduplication
- Systematically identify **near-duplicate** code blocks (functions, methods, or large `if`/`switch` statements that are 70%+ similar).
- Call out opportunities to:
  - Extract **shared helper functions** or **RAII types** (especially for logging, error handling, and resource management).
  - Replace repeated test setup/teardown with **fixtures** and **test helpers**.
  - Unify repeated UI patterns (ImGui button layouts, popups, dialogs) behind common helpers.
- Pay special attention to:
  - Platform-specific code with the **same structure** across `*_linux`, `*_win`, and `*_mac` files. Do **not** change platform-specific behavior, but recommend **shared abstractions** or helper functions where appropriate.
  - Repeated path, string, and regex manipulation logic that could live in a single utility function.
  - Copy-pasted branches that differ only by constants or minor parameters.
- For each duplication hotspot, propose a **concrete DRY refactor** (what new helper/abstraction to introduce, which call sites to update, and what risks to watch for).

### 4. C++17 Modernization Opportunities
- `constexpr` candidates (functions that could be compile-time evaluated)
- `std::string_view` opportunities (functions accepting `const std::string&` that only read)
- `[[nodiscard]]` missing on functions returning error codes or important values
- `std::optional` returns instead of sentinel values or out-params
- Structured bindings to simplify tuple/pair unpacking
- `if constexpr` to replace SFINAE boilerplate

### 5. Memory & Performance Debt
- Unnecessary copies (pass-by-value that should be `const&` or move)
- Missing move constructors/assignment on movable types
- `std::vector` without `reserve()` in known-size loops
- Re-allocation in hot paths (string concatenation in loops)
- Cache-unfriendly data layouts (AoS that could be SoA for SIMD)
- Unnecessary `shared_ptr` where `unique_ptr` suffices
- Missed opportunities to inline functions

### 6. Naming Convention Violations
- Member variables missing trailing underscore (`member` instead of `member_`)
- Globals missing `g_` prefix
- Constants missing `k` prefix (e.g., `MAX_SIZE` instead of `kMaxSize`)
- Functions in `snake_case` instead of `PascalCase`
- Mixed Hungarian notation remnants (`dwCount`, `hHandle`, `lpBuffer`)

### 7. Maintainability Issues
- High cyclomatic complexity (methods > 15 branches)
- "God classes" (single class > 800 LOC with many responsibilities)
- Missing or stale Doxygen comments on public interfaces
- `#pragma once` missing or duplicate include guards
- Implicit type conversions via non-explicit constructors
- Mutable globals or singletons complicating testing
- Code duplications
- Unnecessary mutexes
- Unnecessary complexity
- Files with many responsibilities

### 8. Platform-Specific Debt
- Windows API patterns: Unclosed `HANDLE`, missing `CloseHandle()` calls
- RAII wrappers missing for Windows types (`HANDLE`, `HMODULE`)
- Platform `#ifdef` blocks with duplicated logic
- Inconsistent path separator handling (`\\` vs `/`)

### 9. Potential Bugs and Logic Errors
- **Race conditions**: Unprotected shared state, missing synchronization, data races
- **Resource leaks**: Unclosed handles, file descriptors, memory leaks, missing RAII cleanup
- **Logic errors**: Off-by-one errors, incorrect loop bounds, wrong comparison operators
- **State management bugs**: Incorrect state transitions, stale state, missing state validation
- **Thread safety violations**: Non-atomic access to shared variables, lock ordering issues
- **Exception safety**: Exceptions thrown in destructors, missing exception handling, resource leaks on exceptions
- **Null pointer dereferences**: Missing null checks, unsafe pointer usage, dangling pointers
- **Buffer overflows**: Array bounds violations, string operations without size checks
- **Type mismatches**: Implicit conversions causing bugs, signed/unsigned mismatches
- **Timing issues**: Incorrect time calculations, timezone handling, clock precision issues
- **Edge cases**: Missing validation for empty inputs, boundary conditions, overflow/underflow
- **Platform-specific bugs**: Incorrect platform detection, wrong API usage, missing error handling


---

## Review Process

Use static code searches to systematically identify technical debt. For each category, perform the following searches. Each step corresponds to the matching numbered category in "Scan Categories" above.

### Step 1: Dead/Unused Code
1. **Search for** unused includes: Check for `#include` statements where the included header's symbols are never referenced
2. **Search for** unused forward declarations: Look for `class X;` or `struct X;` where `X` is never used
3. **Search for** commented-out code blocks: Find large blocks of commented code that should be removed
4. **Check** for orphaned files: `.cpp` files without corresponding `.h` files or vice versa
5. **Search for** `#ifdef` blocks for removed platforms: Look for platform-specific code that's no longer needed
6. **Verify** all template specializations are actually instantiated somewhere

### Step 2: C++ Technical Debt
1. **Search for** duplicated code patterns: Look for similar function bodies (>10 lines) that differ only in types or minor details
2. **Search for** raw pointers: Find `T*` that could be `unique_ptr` or `shared_ptr`
3. **Search for** `new`/`delete` pairs: Look for manual memory management that should use RAII
4. **Search for** nested `if` chains: Find deeply nested conditionals (>3 levels) that could use `std::variant` or `std::optional`
5. **Search for** `catch(...)` blocks: Find exception handlers that swallow exceptions without logging
6. **Search for** mutex lock/unlock pairs: Verify locks are held for minimal scope
7. **Search for** unprotected shared state: Look for non-atomic variables accessed from multiple threads
8. **Identify** any dubious NOSONAR or NOLINT comments that could hide a real issue

### Step 3: Aggressive DRY / Code Deduplication
1. **Identify clusters of similar functions/files** (e.g., multiple tests or platform files that look almost identical).
2. **Highlight opportunities** to:
   - Extract shared helpers (including **test fixtures**, **assertion helpers**, and **text-width calculators**).
   - Centralize repeated configuration, logging, and error-handling patterns.
   - Introduce small, well-named utility functions instead of copy-paste.
3. For **platform-specific code** under `#ifdef _WIN32`, `__APPLE__`, `__linux__`:
   - Do **not** change platform-specific behavior or separators.
   - Instead, recommend **platform-agnostic interfaces** with per-platform implementations, or shared helpers that reduce duplication without breaking isolation.
4. For each major duplication hotspot, provide:
   - The **set of files/locations** involved.
   - A **sketch of the common abstraction** (function/class/interface).
   - An estimate of **risk vs. payoff** (lines removed, complexity reduced).

### Step 4: C++17 Modernization
1. **Search for** `const std::string&` parameters: Find functions that only read strings and could use `std::string_view`
2. **Search for** functions returning error codes: Look for `bool` or `int` returns that should have `[[nodiscard]]`
3. **Search for** sentinel values: Find functions returning `-1`, `nullptr`, or empty strings that could use `std::optional`
4. **Search for** `std::pair`/`std::tuple` unpacking: Find manual unpacking that could use structured bindings
5. **Search for** `std::enable_if` usage: Find SFINAE patterns that could use `if constexpr`
6. **Check** for functions that could be `constexpr`: Look for pure functions with compile-time constants

### Step 5: Memory & Performance Debt
1. **Search for** pass-by-value of large types: Find function parameters that copy large objects instead of `const&` or move
2. **Search for** `std::vector` growth in loops: Look for vectors that grow without `reserve()` when size is known
3. **Search for** string concatenation in loops: Find `+=` or `+` operations in loops that should use `std::ostringstream` or `reserve()`
4. **Search for** `shared_ptr` usage: Verify `shared_ptr` is necessary (could `unique_ptr` suffice?)
5. **Search for** missing move constructors: Look for classes with copy constructors but no move constructors
6. **Check** for cache-unfriendly layouts: Find structs with mixed data types that could be split into arrays
7. **Check** for helper functions that should be inlined; verify that already inlined functions can really be inlined by the compiler

### Step 6: Naming Convention Violations
1. **Search for** member variables without trailing underscore: Look for `int member;` instead of `int member_;`
2. **Search for** globals without `g_` prefix: Find global variables that don't start with `g_`
3. **Search for** constants without `k` prefix: Look for `const int MAX_SIZE` instead of `const int kMaxSize`
4. **Search for** functions in `snake_case`: Find functions that should be `PascalCase`
5. **Search for** Hungarian notation: Look for `dwCount`, `hHandle`, `lpBuffer` patterns
6. **Verify** all types use `PascalCase`: Check classes, structs, enums follow naming convention

### Step 7: Maintainability Issues
1. **Search for** large functions: Find functions >100 lines that could be split
2. **Search for** large classes: Find classes >800 lines that might be "God classes"
3. **Search for** missing `#pragma once`: Check header files for include guards
4. **Search for** duplicate include guards: Find headers with both `#pragma once` and `#ifndef` guards
5. **Search for** non-explicit constructors: Find single-argument constructors that should be `explicit`
6. **Search for** mutable globals: Find global variables that could cause testing issues
7. **Check** for missing Doxygen comments: Verify public APIs have documentation

### Step 8: Platform-Specific Debt
1. **Search for** raw `HANDLE` usage: Find Windows handles that aren't wrapped in RAII
2. **Search for** `CloseHandle()` calls: Verify all handles are closed on all code paths
3. **Search for** `#ifdef _WIN32` blocks: Check for duplicated logic that could be abstracted
4. **Search for** hardcoded path separators: Find `\\` or `/` that should use platform-agnostic functions
5. **Verify** platform-specific code is properly isolated: Check for platform code leaking into shared code

### Step 9: Potential Bugs and Logic Errors
1. **Search for** unprotected shared state: Look for non-atomic variables accessed from multiple threads without synchronization (mutex, atomic, or lock)
2. **Search for** resource leaks: Find `HANDLE`, file descriptors, or memory allocations without corresponding cleanup (use RAII patterns)
3. **Search for** missing null checks: Look for pointer dereferences without null validation, especially after API calls that can return null
4. **Search for** off-by-one errors: Check loop bounds (`<` vs `<=`), array indexing (`[size]` instead of `[size-1]`), and string operations
5. **Search for** incorrect comparison operators: Find `=` instead of `==`, wrong comparison direction (`>` vs `<`), or missing bounds checks
6. **Search for** exception safety issues: Look for exceptions in destructors, missing try-catch blocks around critical operations, or resource leaks on exceptions
7. **Search for** state management bugs: Find incorrect state transitions, stale cached values not invalidated, or missing state validation before use
8. **Search for** timing/race conditions: Look for time-based logic without proper synchronization, TOCTOU (Time-Of-Check-Time-Of-Use) bugs, or race conditions in concurrent code
9. **Search for** buffer overflows: Find array/string operations without size validation, unsafe C functions (`strcpy`, `strcat`) without bounds checking, or missing null terminators
10. **Search for** type conversion bugs: Look for implicit conversions that could cause data loss, signed/unsigned mismatches, or loss of precision in calculations
11. **Search for** edge case handling: Check for missing validation of empty inputs, null pointers, zero sizes, negative values, or boundary conditions (MAX_INT, etc.)
12. **Search for** platform-specific bugs: Verify platform detection logic (`#ifdef` conditions), API error handling (checking return values), and cross-platform consistency
13. **Review** error handling paths: Ensure all error conditions are handled, resources are cleaned up on all paths, and errors are properly logged with context
14. **Check** for logical contradictions: Look for conditions that can never be true, unreachable code, conflicting requirements, or impossible state combinations
15. **Verify** thread safety: Check that shared data is properly protected (mutex/atomic), atomic operations are used correctly, lock ordering is consistent (avoid deadlocks), and no data races exist
16. **Check** for initialization bugs: Find uninitialized variables, missing constructor initialization, or variables used before being set
17. **Search for** memory safety issues: Look for use-after-free, double-free, dangling pointers, or accessing freed memory
18. **Review** API usage: Verify all system/third-party API calls check return values, handle errors correctly, and follow proper usage patterns

---

## Output Format

For each finding:
1. **Quote exact code** (file:line or code block)
2. **Debt type** (category from above)
3. **Risk explanation** (e.g., "Raw HANDLE leak if exception thrown before CloseHandle")
4. **Suggested fix** with code snippet using project conventions
5. **Severity**: Critical / High / Medium / Low
6. **Effort**: Estimated lines changed and time

---

## Summary Requirements

End with:
- Debt ratio estimate (% of codebase affected)
- Top 3 "quick wins" (< 15 min each, high impact)
- Top critical/high items requiring immediate attention
- Areas with systematic patterns (e.g., "All template methods in FileIndex.h lack `[[nodiscard]]`")
```

---

## Usage Context

- After completing refactorings or major code changes
- Before committing code to identify cleanup opportunities
- During code review to catch technical debt early
- When preparing for production releases

---

## Expected Output

- Detailed list of issues with exact code locations
- Severity ratings and effort estimates
- Specific fix suggestions with code snippets following project conventions
- Prioritized action plan
- Overall debt ratio estimate
- Quick wins for immediate improvement
