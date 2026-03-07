# Quick Production Readiness Checklist

**Use this for every feature/bug fix before committing.**

---

## ✅ Must-Check Items (5 minutes)

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

---

## 🔍 Code Quality (10 minutes)

- [ ] **DRY violation**: No code duplication >10 lines (extract to helpers)
- [ ] **Dead code**: Remove unused code, variables, comments
- [ ] **Missing includes**: Check for missing headers (`<chrono>`, `<exception>`, etc.)
- [ ] **Const correctness**: Methods that don't modify state are `const`

---

## 🛡️ Error Handling (5 minutes)

- [ ] **Exception safety**: Code handles exceptions gracefully (returns safe defaults)
- [ ] **Thread safety**: Worker threads catch exceptions, don't crash
- [ ] **Shutdown handling**: Graceful handling during shutdown (log warnings)
- [ ] **Bounds checking**: Array/container access validated

## 🔍 Memory Leak Detection (Before Release) ⚠️ MANDATORY

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

**Full Checklist**: See `docs/plans/production/GENERIC_PRODUCTION_READINESS_CHECKLIST.md` for comprehensive review.
