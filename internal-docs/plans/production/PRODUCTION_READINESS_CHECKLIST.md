# Production Readiness Checklist

**Purpose**: Unified checklist for ensuring production-ready code quality.

- **Quick Checklist**: Use for every commit (5-10 minutes)
- **Comprehensive Checklist**: Use for major features/releases (full review)

---

## Quick Checklist (5-10 minutes) - For Every Commit

Use this section before every commit to catch common issues quickly.

### ✅ Must-Check Items (5 minutes)

- [ ] **Headers correctness**: no missing headers, order of headers follows C++ standard (system includes → project includes → forward declarations), `<windows.h>` is before other includes that require it. See AGENTS.md "C++ Standard Include Order" section for details.
- [ ] **Windows compilation**: All `std::min`/`std::max` use `(std::min)`/`(std::max)` with parentheses
- [ ] **Forward declaration consistency**: Forward declarations must match actual type (`class` vs `struct`). Check that `class X;` forward declarations match `struct X { ... }` definitions (or vice versa)
- [ ] **Exception handling**: Critical code wrapped in try-catch blocks
- [ ] **Error logging**: Exceptions/errors logged with `LOG_ERROR_BUILD` or `LOG_WARNING_BUILD`
- [ ] **Unused exception variable warnings**: In catch blocks using `LOG_*_BUILD` with `e.what()`, add `(void)e;` before the log statement to prevent Release build warnings
- [ ] **Input validation**: All inputs validated, invalid values logged and defaulted
- [ ] **Naming conventions**: All identifiers follow `docs/standards/CXX17_NAMING_CONVENTIONS.md`
- [ ] **No linter errors**: Run linter and fix all errors
- [ ] Both Makefile (for legacy build system) and CMakeLists.txt must have been updated if new files have been added
- [ ] **PGO compatibility**: If adding source files to test targets that are also in the main executable, ensure the test target has matching PGO flags (prevents LNK1269 errors). See AGENTS.md for details.
- [ ] **Documentation organization**: If creating or updating documentation, ensure it's in the correct location:
  - **Active documents**: Keep in `docs/` (current work, active plans, essential references)
  - **Historical documents**: Move completed work summaries, implementation reports, and code reviews to `docs/Historical/`
  - **Obsolete documents**: Move superseded plans, old analysis, and experimental approaches to `docs/archive/`
  - **Update index**: Update `docs/DOCUMENTATION_INDEX.md` when moving documents

### 🔍 Code Quality (10 minutes)

- [ ] **DRY violation**: No code duplication >10 lines (extract to helpers)
- [ ] **Dead code**: Remove unused code, variables, comments
- [ ] **Missing includes**: Check for missing headers (`<chrono>`, `<exception>`, etc.)
- [ ] **Const correctness**: Methods that don't modify state are `const`

### 🛡️ Error Handling (5 minutes)

- [ ] **Exception safety**: Code handles exceptions gracefully (returns safe defaults)
- [ ] **Thread safety**: Worker threads catch exceptions, don't crash
- [ ] **Shutdown handling**: Graceful handling during shutdown (log warnings)
- [ ] **Bounds checking**: Array/container access validated

### 🔍 Memory Leak Detection (Before Release) ⚠️ MANDATORY

- [ ] **Memory leak testing**: Test for memory leaks using profiling tools (Instruments on macOS, Application Verifier on Windows)
  - macOS: Use Instruments "Leaks" or "Allocations" template (see docs/MACOS_PROFILING_GUIDE.md)
  - Windows: Use Visual Studio Diagnostic Tools or Application Verifier
  - Run for 10-15 minutes with typical usage patterns
- [ ] **Container cleanup**: Verify all containers are cleared when no longer needed
  - **Critical**: Caches like `directory_path_to_id_` must be cleared in RecomputeAllPaths()
  - All vectors should be cleared before rebuilding
  - Check that shrink_to_fit() is called after rebuilds
- [ ] **Memory usage monitoring**: Monitor memory usage over extended period (10+ minutes) during typical usage
  - Use Activity Monitor (macOS) or Task Manager (Windows)
  - Memory should stabilize, not grow continuously
  - If memory grows >100MB over 10 minutes, investigate with profiling tools
- [ ] **Unbounded growth check**: Verify no containers grow without bounds (vectors, maps, sets)
  - Use Instruments "Allocations" with "Mark Generation" to track allocations
  - Check "Statistics" view sorted by size
  - Look for std::vector, std::unordered_map, hash_map_t growing continuously
- [ ] **Cache invalidation**: Ensure caches are cleared during rebuilds/recomputations
  - directory_path_to_id_ must be cleared in RecomputeAllPaths()
  - Any cache maps/sets must be invalidated during rebuilds
  - Verify RebuildPathBuffer() calls shrink_to_fit() on all vectors
- [ ] **Async resource cleanup**: Verify all `std::future` objects are properly cleaned up
  - **Before starting new async operation**: Wait for any existing valid future to complete
    - Check `future.valid()` before accessing
    - If not ready, wait with `wait()` or `wait_for()`
    - Call `get()` to retrieve result and clean up the future
  - **After getting result**: Explicitly reset future to invalid state
    - Use: `future = std::future<Type>()` to reset
  - **In cleanup functions**: Clean up futures in destructors, `ClearInputs()`, and state reset functions
  - **Pattern to follow**: See `UIRenderer.cpp` `StartGeminiApiCall` lambda and `GuiState.cpp::ClearInputs()` for reference

**Quick test**: 
1. Run application for 10 minutes with typical usage (load index, perform searches, interact with UI)
2. Monitor memory usage (Activity Monitor on macOS, Task Manager on Windows)
3. If memory grows continuously (>100MB over 10 minutes), investigate with profiling tools
4. Use Instruments "Allocations" template with "Mark Generation" to identify when leaks occur

**Common leak sources** (based on actual bugs found):
- Container caches not cleared (e.g., directory_path_to_id_ not cleared in RecomputeAllPaths)
- Vectors not shrunk after rebuilds (only path_storage_ was shrunk, others weren't)
- File handles not closed
- Thread objects not joined
- Callback objects not released
- **std::future objects not cleaned up**: Async futures must be properly waited for and reset
  - **Critical**: Before starting a new async operation, wait for any existing future to complete
  - **Critical**: After getting the result from a future, explicitly reset it to invalid state
  - **Critical**: Clean up futures in destructors and cleanup functions (e.g., `ClearInputs()`)
  - **Pattern**: Check `future.valid()` before accessing, wait for completion, call `get()`, then reset with default constructor

**Reference**: See docs/MACOS_PROFILING_GUIDE.md for detailed profiling instructions.

**For comprehensive review, see "Comprehensive Checklist" below**

---

## Comprehensive Checklist - For Major Features/Releases

Use this section for thorough reviews of new features, significant changes, or before releases.

## 🔍 Phase 1: Code Review & Compilation

### Windows-Specific Issues
- [ ] **Check `std::min`/`std::max` usage**: All uses must be `(std::min)` and `(std::max)` with parentheses to prevent Windows.h macro conflicts
- [ ] **Verify includes**: Check for missing headers (especially `<chrono>`, `<exception>`, `<mutex>`, etc.)
- [ ] **Include order**: Standard library headers before project headers (when possible)
- [ ] **Forward declarations**: Verify forward declarations are before class definitions that use them
- [ ] **Forward declaration type consistency**: Forward declarations must match actual type (`class` vs `struct`). If type is defined as `struct X`, forward declaration must be `struct X;` (not `class X;`), and vice versa. Some compilers (especially MSVC) warn or error on mismatches.

### Compilation Verification
- [ ] **No linter errors**: Run linter and fix all errors
- [ ] **Template placement**: Template functions/classes must be in header files
- [ ] **Const correctness**: Methods that don't modify state should be `const`
- [ ] **Missing includes**: Check for any missing `#include` statements

---

## 🧹 Phase 2: Code Quality & Technical Debt

### DRY Principle (Don't Repeat Yourself)
- [ ] **Identify duplication**: Look for repeated code blocks (>10 lines duplicated)
- [ ] **Extract helpers**: Create helper functions/methods for duplicated logic
- [ ] **Template opportunities**: Consider templates for type-agnostic duplicated code
- [ ] **Lambda reuse**: Extract repeated lambdas to helper functions

### Code Cleanliness
- [ ] **Dead code removal**: Remove unused functions, variables, commented code
- [ ] **Simplify complex logic**: Break down complex expressions into clear steps
- [ ] **Consistent style**: Ensure formatting matches project style
- [ ] **Comment clarity**: Add brief comments for non-obvious logic

### Single Responsibility
- [ ] **Class purpose**: Each class has one clear purpose
- [ ] **Function purpose**: Each function does one thing well
- [ ] **File organization**: Each file has a focused, cohesive purpose

---

## ⚡ Phase 3: Performance & Optimization

### Performance Opportunities
- [ ] **Unnecessary allocations**: Check for temporary string/object creations in hot paths
- [ ] **Cache locality**: Verify data structures support good cache behavior
- [ ] **Early exits**: Check for opportunities to exit loops/functions early
- [ ] **Reserve capacity**: Pre-reserve vector/container capacity when size is known
- [ ] **Move semantics**: Use `std::move` for large objects when appropriate

### Algorithm Efficiency
- [ ] **Time complexity**: Verify algorithms are optimal (O(n) vs O(n²), etc.)
- [ ] **Space complexity**: Check for unnecessary memory usage
- [ ] **Hot path optimization**: Profile and optimize frequently called code paths

---

## 📝 Phase 4: Naming Conventions

### Verify All Identifiers
- [ ] **Classes/Structs**: `PascalCase` (e.g., `FileIndex`, `SearchWorker`)
- [ ] **Functions/Methods**: `PascalCase` (e.g., `StartMonitoring()`, `GetSize()`)
- [ ] **Local Variables**: `snake_case` (e.g., `buffer_size`, `offset`)
- [ ] **Member Variables**: `snake_case_` with trailing underscore (e.g., `file_index_`, `mutex_`)
- [ ] **Global Variables**: `g_snake_case` with `g_` prefix (e.g., `g_file_index`)
- [ ] **Constants**: `kPascalCase` with `k` prefix (e.g., `kBufferSize`, `kMaxQueueSize`)
- [ ] **Namespaces**: `snake_case` (e.g., `find_helper`, `file_operations`)
- [ ] **Enums**: `PascalCase` (e.g., `LoadBalancingStrategy`)

**Reference**: See `docs/standards/CXX17_NAMING_CONVENTIONS.md` for complete details

---

## 🛡️ Phase 5: Exception & Error Handling

### Exception Handling
- [ ] **Try-catch blocks**: Wrap code that can throw exceptions (file I/O, memory allocation, etc.)
- [ ] **Exception types**: Catch `std::exception&` and `...` (catch-all)
- [ ] **Error logging**: Log exceptions using `LOG_ERROR_BUILD` with context
- [ ] **Graceful degradation**: Return safe defaults/empty results on error (don't crash)
- [ ] **Exception safety**: Ensure RAII and proper resource cleanup

### Error Handling
- [ ] **Input validation**: Validate all user inputs and configuration values
- [ ] **Bounds checking**: Verify array/container indices are valid
- [ ] **Null checks**: Check pointers before dereferencing (when applicable)
- [ ] **Resource checks**: Verify resources (files, handles, etc.) are valid before use

### Logging
- [ ] **Error logging**: Use `LOG_ERROR_BUILD` for errors
- [ ] **Warning logging**: Use `LOG_WARNING_BUILD` for warnings (invalid config, etc.)
- [ ] **Info logging**: Use `LOG_INFO_BUILD` for important events
- [ ] **Context in logs**: Include relevant context (thread ID, range, values, etc.)

---

## 🔒 Phase 6: Thread Safety & Concurrency

### Thread Safety
- [ ] **Shared data protection**: All shared data accessed with proper locks
- [ ] **Lock ordering**: Avoid potential deadlocks (consistent lock order)
- [ ] **Atomic operations**: Use `std::atomic` for lock-free operations when appropriate
- [ ] **Thread-local storage**: Consider thread-local storage for per-thread data

### Concurrency Patterns
- [ ] **Exception handling in threads**: Worker threads catch and log exceptions
- [ ] **Graceful shutdown**: Handle shutdown gracefully (log warnings, don't crash)
- [ ] **Resource cleanup**: Ensure threads are properly joined/cleaned up
- [ ] **Async resource cleanup**: All `std::future` objects must be properly cleaned up to prevent memory leaks
  - **Before starting new async operation**: 
    - Check if existing future is valid: `if (future.valid())`
    - Wait for completion if not ready: `future.wait()` or `future.wait_for(timeout)`
    - Retrieve result to clean up: `future.get()` (even if result is discarded)
  - **After getting result**: 
    - Explicitly reset future: `future = std::future<Type>()` to free resources
  - **In cleanup functions**: 
    - Clean up futures in destructors, state reset functions (e.g., `ClearInputs()`)
    - Wait for pending futures before clearing state
  - **When replacing futures**: 
    - Never replace a valid future without waiting for it first
    - Abandoned futures leak memory and keep threads running
  - **Example pattern**:
    ```cpp
    // Before starting new operation
    if (state.apiFuture.valid()) {
      auto status = state.apiFuture.wait_for(std::chrono::milliseconds(0));
      if (status != std::future_status::ready) {
        state.apiFuture.wait();  // Wait for completion
      }
      state.apiFuture.get();  // Clean up old future
    }
    // Start new operation
    state.apiFuture = StartAsyncOperation();
    
    // After getting result
    auto result = state.apiFuture.get();
    state.apiFuture = std::future<ResultType>();  // Reset to invalid state
    ```

---

## ✅ Phase 7: Input Validation & Edge Cases

### Input Validation
- [ ] **Settings validation**: Validate configuration values, log warnings for invalid values
- [ ] **Parameter validation**: Check function parameters for valid ranges/values
- [ ] **User input validation**: Validate all user-provided inputs
- [ ] **Default values**: Provide safe defaults for invalid inputs

### Edge Cases
- [ ] **Empty inputs**: Handle empty strings, empty containers, etc.
- [ ] **Boundary values**: Test min/max values, zero, negative numbers
- [ ] **Null/None values**: Handle null pointers, optional values
- [ ] **Overflow/Underflow**: Check for integer overflow, buffer overflows
- [ ] **Concurrent access**: Handle race conditions, concurrent modifications

---

## 📊 Phase 8: Code Review (Senior Engineer Perspective)

### Architecture
- [ ] **Design patterns**: Appropriate use of design patterns (Strategy, Factory, etc.)
- [ ] **Separation of concerns**: Clear boundaries between components
- [ ] **Dependencies**: Minimal, well-defined dependencies
- [ ] **Abstractions**: Appropriate level of abstraction

### Memory Management
- [ ] **RAII**: Proper use of RAII for resource management
- [ ] **Smart pointers**: Use `std::unique_ptr`, `std::shared_ptr` appropriately
- [ ] **Memory leaks**: No memory leaks (verify with tools if possible)
- [ ] **Move semantics**: Use move semantics to avoid unnecessary copies
- [ ] **Async resource cleanup**: Clean up `std::future` objects properly (see Phase 6: Thread Safety & Concurrency section)
- [ ] **Memory leak detection**: Test for memory leaks using profiling tools (Instruments on macOS, Application Verifier on Windows)
  - **macOS**: Use Instruments "Leaks" or "Allocations" template (see docs/MACOS_PROFILING_GUIDE.md)
    - Run for 10-15 minutes with typical usage
    - Look for red bars in Leaks template
    - Check "Persistent Bytes" in Allocations (should stabilize, not grow continuously)
    - Use "Mark Generation" to track allocations between operations
  - **Windows**: Use Visual Studio Diagnostic Tools or Application Verifier
    - Monitor memory usage over extended period (10+ minutes)
    - Use "Take Snapshot" to compare memory at different points
- [ ] **Container cleanup**: Verify all containers (vectors, maps, sets) are properly cleared when no longer needed
  - **Critical**: Caches like `directory_path_to_id_` must be cleared in RecomputeAllPaths()
  - All vectors should be cleared before rebuilding
  - Check that shrink_to_fit() is called after rebuilds (not just path_storage_, but all vectors)
- [ ] **Cache invalidation**: Ensure caches (e.g., `directory_path_to_id_`) are cleared during rebuilds/recomputations
  - Any cache maps/sets must be invalidated when data is rebuilt
  - Verify caches are cleared in RecomputeAllPaths() and RebuildPathBuffer()
- [ ] **Unbounded growth check**: Verify no containers grow without bounds
  - Use Instruments "Allocations" with "Mark Generation" to track allocations
  - Check "Statistics" view sorted by size
  - Look for std::vector, std::unordered_map, hash_map_t growing continuously
  - Memory should stabilize after initial operations, not grow continuously

### Code Quality
- [ ] **Readability**: Code is easy to understand
- [ ] **Maintainability**: Future developers can modify easily
- [ ] **Testability**: Code can be tested (consider unit tests)
- [ ] **Documentation**: Complex logic has brief comments

---

## 🧪 Phase 9: Testing Considerations

### Manual Testing
- [ ] **Happy path**: Test normal operation with valid inputs
- [ ] **Error cases**: Test error handling with invalid inputs
- [ ] **Edge cases**: Test boundary conditions, empty inputs, etc.
- [ ] **Concurrent access**: Test with multiple threads (if applicable)
- [ ] **Shutdown scenarios**: Test graceful shutdown during active operations

### Stress Testing
- [ ] **Large inputs**: Test with maximum/large data sets
- [ ] **Concurrent operations**: Test with many concurrent operations
- [ ] **Resource limits**: Test under memory/CPU constraints
- [ ] **Long-running**: Test for extended periods (memory leaks, etc.)

### Memory Leak Detection ⚠️ MANDATORY BEFORE RELEASE
- [ ] **macOS**: Use Instruments "Leaks" or "Allocations" template to detect leaks
  - Build Release version with debug symbols (enabled by default)
  - Launch Instruments: `open -a Instruments`
  - Select "Leaks" template (automatic detection) or "Allocations" template (detailed analysis)
  - Choose target: `build/FindHelper.app` or `build/find_helper`
  - Record for 10-15 minutes with typical usage:
    - Load file index (if available)
    - Perform multiple searches (various query types)
    - Interact with UI (resize, scroll, type)
    - Let application run idle for a few minutes
    - Perform operations that trigger rebuilds/recomputations
  - Analyze results:
    - **Leaks template**: Look for red bars (detected leaks), check "Leak Summary"
    - **Allocations template**: 
      - Check "Persistent Bytes" graph (should stabilize, not grow continuously)
      - Use "Mark Generation" before/after specific operations
      - Look for unbounded growth in "Statistics" view (sort by size)
      - Filter by object type (std::vector, std::unordered_map, etc.)
  - See docs/MACOS_PROFILING_GUIDE.md for detailed instructions
- [ ] **Windows**: Use Application Verifier or Visual Studio Diagnostic Tools
  - Monitor memory usage over extended period (10-15 minutes)
  - Use "Take Snapshot" feature to compare memory at different points
  - Check for containers that grow without bounds
  - Verify caches are cleared during rebuilds
- [ ] **Common leak sources to check** (based on actual bugs found):
  - Container caches not cleared (e.g., directory_path_to_id_ not cleared in RecomputeAllPaths)
  - Vectors not shrunk after rebuilds (only path_storage_ was shrunk, others weren't)
  - File handles not closed
  - Thread objects not joined
  - Callback objects not released
- [ ] **If leaks are found**:
  1. Identify the leaking container/object type from stack traces
  2. Find where it's allocated but not freed
  3. Add proper cleanup (clear(), shrink_to_fit(), etc.)
  4. Re-test to verify leak is fixed
- [ ] **Memory usage baseline**: Establish baseline memory usage and verify it doesn't grow unbounded

---

## 📋 Phase 10: Documentation & Comments

### Code Documentation
- [ ] **Function comments**: Complex functions have brief comments explaining purpose
- [ ] **Parameter documentation**: Document non-obvious parameters
- [ ] **Return value documentation**: Document return values and error conditions
- [ ] **Class documentation**: Classes have brief purpose descriptions

### Implementation Notes
- [ ] **Design decisions**: Document non-obvious design choices
- [ ] **Performance notes**: Document performance-critical sections
- [ ] **Thread safety notes**: Document thread safety guarantees
- [ ] **Known limitations**: Document any known limitations or trade-offs

### Documentation Organization
- [ ] **Document location**: Ensure documentation is in the correct directory:
  - **Active documents** (`docs/`): Current work, active development plans, essential references
    - Examples: `PRIORITIZED_DEVELOPMENT_PLAN.md`, `NEXT_STEPS.md`, `plans/production/PRODUCTION_READINESS_CHECKLIST.md`
  - **Historical documents** (`docs/Historical/`): Completed work summaries, implementation reports, code reviews
    - Examples: `DRY_REFACTORING_COMPLETE.md`, `EXCEPTION_HANDLING_COMPLETE.md`, `CODE_REVIEW_*.md`
    - Move when: Work is completed and documented
  - **Obsolete documents** (`docs/archive/`): Superseded plans, old analysis, experimental approaches
    - Examples: `SPRINT1_DETAILED.md`, `EXTENSION_ONLY_OPTIMIZATION.md`, `HYPERSCAN_FEASIBILITY_STUDY.md`
    - Move when: Plan is superseded, approach is rejected, or analysis is no longer relevant
- [ ] **Update documentation index**: When moving documents, update `docs/DOCUMENTATION_INDEX.md`:
  - Update document paths in the index
  - Update status indicators
  - Add to appropriate category (Active/Historical/Obsolete)
- [ ] **Document lifecycle**: Follow this lifecycle:
  1. **Create**: New documents start in `docs/` (active)
  2. **Complete**: When work is done, move completion summaries to `docs/Historical/`
  3. **Supersede**: When plans are replaced, move old plans to `docs/archive/`
  4. **Reference**: Keep historical documents for understanding past decisions

**Reference**: See `docs/DOCUMENTATION_INDEX.md` for complete categorization and examples.

---

## 🎯 Quick Reference: Critical Items (Must Do)

For quick reviews, focus on these critical items:

1. ✅ **Windows compilation**: `(std::min)`, `(std::max)` protection
2. ✅ **Exception handling**: Try-catch blocks in critical paths
3. ✅ **Error logging**: Log errors with context
4. ✅ **Input validation**: Validate all inputs, log warnings for invalid values
5. ✅ **Naming conventions**: All identifiers follow project conventions
6. ✅ **DRY principle**: Eliminate code duplication
7. ✅ **Thread safety**: Proper locking and exception handling in threads

---

## 📝 Quick Instructions Template

When asking AI to make code production-ready, use:

```
Review the generated code for:
1. Windows compilation issues (missing includes, std::min/std::max protection)
2. Code duplication (DRY violations) - extract to helpers
3. Exception handling - wrap critical code in try-catch, log errors
4. Input validation - validate all inputs, log warnings for invalid values
5. Naming conventions - verify all identifiers follow docs/standards/CXX17_NAMING_CONVENTIONS.md
6. Performance optimizations - check for unnecessary allocations, early exits
7. CMake/PGO compatibility - if adding files to test targets, ensure PGO flags match main target
8. Memory leak detection - test with profiling tools, verify containers are cleared, check for unbounded growth
   - Verify caches (e.g., directory_path_to_id_) are cleared during rebuilds/recomputations
   - Check that shrink_to_fit() is called on all vectors after rebuilds
   - Monitor memory usage for 10-15 minutes with typical usage patterns
   - Use Instruments "Allocations" with "Mark Generation" to track allocations
9. Documentation organization - ensure documentation is in correct location (docs/, Historical/, or archive/)
   - Update DOCUMENTATION_INDEX.md when moving documents
10. Senior C++ engineer review - check for architecture issues, memory management
```

---

## 📚 Common Issues & Solutions

### Unused Exception Variable Warnings in Release Builds

**Problem**: In Release builds, `LOG_*_BUILD` macros are disabled (they compile to no-ops). If a catch block declares an exception variable `e` but only uses it inside a `LOG_*_BUILD` macro, the compiler warns about an unused variable.

**Example of problematic code**:
```cpp
} catch (const std::exception& e) {
  LOG_ERROR_BUILD("Error: " << e.what());  // ❌ Warning: 'e' unused in Release
}
```

**Solution**: Add `(void)e;` before the log statement to explicitly mark the variable as intentionally unused:
```cpp
} catch (const std::exception& e) {
  (void)e;  // Suppress unused variable warning in Release mode
  LOG_ERROR_BUILD("Error: " << e.what());  // ✅ No warning
}
```

**When to apply**:
- ✅ Always when using `LOG_*_BUILD` with `e.what()` in catch blocks
- ❌ Not needed when using `LOG_ERROR`/`LOG_WARNING` (always active)
- ❌ Not needed when `e.what()` is used outside macros (e.g., assigned to a variable first)

**Note**: This is a Release-build-only issue. Debug builds don't have this warning because `LOG_*_BUILD` macros are active.

---

### PGO Compatibility for Test Targets (LNK1269 Errors)

**Problem**: When PGO is enabled (`ENABLE_PGO=ON`), test targets that share source files with the main executable must use the same PGO flags. If they don't, you'll get LNK1269 linker errors.

**Example of problematic situation**:
```cmake
# Main target has PGO flags
target_compile_options(find_helper PRIVATE 
    $<$<CONFIG:Release>:/GL /Gy>
)
target_link_options(find_helper PRIVATE
    $<$<CONFIG:Release>:/LTCG:PGOPTIMIZE /USEPROFILE /PGD:FindHelper.pgd /OPT:REF /OPT:ICF>
)

# Test target includes same source file but no PGO flags
add_executable(file_index_search_strategy_tests 
    LoadBalancingStrategy.cpp  # ❌ Same file as main target
    # ... other sources
)
# Missing PGO flags causes LNK1269 error
```

**Solution**: Use the same PGO compiler flags as the main target, but use standard linker flags (not PGO-specific) to avoid LNK1266 errors:
```cmake
if(ENABLE_PGO)
    if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/Release/FindHelper.pgd")
        # Phase 2: Use same compiler flags (/GL /Gy) but standard linker flags
        target_compile_options(test_target PRIVATE 
            $<$<CONFIG:Release>:/GL /Gy>
        )
        target_link_options(test_target PRIVATE 
            $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
        )
    else()
        # Phase 1: Use same compiler flags (/GL /Gy) but standard linker flags
        target_compile_options(test_target PRIVATE 
            $<$<CONFIG:Release>:/GL /Gy>
        )
        target_link_options(test_target PRIVATE 
            $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
        )
    endif()
endif()
```

**Why this works**:
- Matching compiler flags prevent LNK1269 errors (object files are compatible)
- Standard `/LTCG` linker flag prevents LNK1266 errors (no .pgd file needed)
- Tests don't need PGO optimization anyway

**When to check**:
- ✅ Always when adding source files to test targets that are also in the main executable
- ✅ When creating new test targets that include shared source files
- ✅ Before committing CMakeLists.txt changes if PGO is enabled

**Note**: This only affects builds with `ENABLE_PGO=ON`. Standard builds don't have this issue.

---

### Forward Declaration Type Mismatch (class vs struct)

**Problem**: Forward declarations must match the actual type definition. If a type is defined as `struct`, the forward declaration must also be `struct` (not `class`), and vice versa. Some compilers (especially MSVC) will warn or error on mismatches.

**Example of problematic code**:
```cpp
// In SomeHeader.h
class AppSettings;  // ❌ Forward declared as class

// In Settings.h
struct AppSettings {  // ❌ Actually defined as struct
  // ...
};
```

**Solution**: Match the forward declaration to the actual definition:
```cpp
// In SomeHeader.h
struct AppSettings;  // ✅ Matches definition

// In Settings.h
struct AppSettings {  // ✅ Consistent
  // ...
};
```

**How to check**:
1. Search for forward declarations: `grep -r "^class\|^struct" --include="*.h" .`
2. For each forward declaration, verify the actual definition matches:
   - If forward declares `class X;`, check that `X` is actually defined as `class X { ... }`
   - If forward declares `struct X;`, check that `X` is actually defined as `struct X { ... }`

**Common mismatches to check**:
- `AppSettings` - often forward declared as `class` but defined as `struct`
- `SearchResult` - often forward declared as `class` but defined as `struct`
- `SavedSearch` - should be `struct` in both places
- `CommandLineArgs` - should be `struct` in both places

**When to check**:
- ✅ Always when adding new forward declarations
- ✅ When refactoring type definitions (changing `class` to `struct` or vice versa)
- ✅ Before committing header file changes
- ✅ When seeing compiler warnings about "type name first seen as class was struct" (or vice versa)

**Note**: While `class` and `struct` are mostly interchangeable in C++, forward declarations must match exactly. This is a common source of compilation warnings on Windows/MSVC.

---

## 📝 Usage Instructions

### For New Features
1. Complete all phases in order
2. Check off items as you complete them
3. Document any deviations or trade-offs
4. Get code review before merging

### For Bug Fixes
1. Focus on Phase 1 (compilation) and Phase 5 (exception handling)
2. Check relevant items from other phases
3. Ensure fix doesn't introduce new issues

### For Refactoring
1. Focus on Phase 2 (code quality) and Phase 4 (naming)
2. Ensure refactoring maintains functionality
3. Verify no performance regressions (Phase 3)

---

## 🔄 Continuous Improvement

After each feature:
- [ ] **Review checklist**: Identify which items were most valuable
- [ ] **Update checklist**: Add new items based on issues found
- [ ] **Share learnings**: Document patterns that worked well

---

## Example: Applying Checklist to a New Feature

**Feature**: Add new search filter option

1. **Phase 1**: Check Windows compilation, includes
2. **Phase 2**: Extract filter logic to helper if duplicated
3. **Phase 3**: Optimize filter application (early exits, etc.)
4. **Phase 4**: Verify naming (e.g., `ApplyNewFilter()`)
5. **Phase 5**: Add exception handling, validate filter parameters
6. **Phase 6**: Ensure thread-safe if used in parallel search
7. **Phase 7**: Validate filter input, handle edge cases
8. **Phase 8**: Review architecture (does it fit design?)
9. **Phase 9**: Test with various inputs, edge cases
10. **Phase 10**: Document filter behavior

---

**Last Updated**: 2026-01-12 (Merged from QUICK_PRODUCTION_CHECKLIST.md and GENERIC_PRODUCTION_READINESS_CHECKLIST.md)  
**Version**: 2.0
