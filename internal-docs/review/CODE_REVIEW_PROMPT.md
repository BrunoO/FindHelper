# AI Code Review Prompt for USN_WINDOWS Project

**Purpose:** Use this prompt with AI assistants to perform high-quality code reviews that identify subtle bugs, architectural issues, and platform-specific problems.

---

## Project Context

You are reviewing code for **USN_WINDOWS** (FindHelper), a high-performance cross-platform file search application:

- **Primary Target:** Windows (NTFS USN Journal-based indexing)
- **Secondary Targets:** macOS, Linux (Ubuntu)
- **Development Platform:** macOS
- **Technology Stack:** C++17, ImGui (immediate mode GUI), DirectX 11 (Windows), OpenGL (macOS/Linux)
- **Architecture:** Component-based with Structure of Arrays (SoA) design for cache efficiency
- **Performance Focus:** Execution speed prioritized over memory usage

**Key Architectural Patterns:**
- Component-based architecture (FileIndex, PathStorage, PathBuilder, FileIndexStorage)
- Strategy pattern for load balancing (InterleavedChunkingStrategy, etc.)
- RAII for resource management
- Lazy loading for file attributes
- Producer-consumer threading model (USN monitoring)
- Contiguous memory buffers for paths (cache locality)

---

## Review Focus Areas

### 1. Platform-Specific Issues (CRITICAL)

#### Windows-Specific Compilation Errors
- **`std::min`/`std::max` macro conflict:** All uses must be `(std::min)` and `(std::max)` with parentheses to prevent Windows.h macro expansion
  - Search for: `std::min`, `std::max` without parentheses
  - Fix: `(std::min)(a, b)`, `(std::max)(x, y)`
  
- **Missing includes:** MSVC requires explicit includes for `std::to_string`, `std::chrono`, etc.
  - Check: All standard library functions have proper `#include` statements
  - Common missing: `<string>`, `<chrono>`, `<exception>`, `<mutex>`

- **Unsafe C functions:** Replace `strncpy`, `localtime` with safe alternatives
  - `strncpy` → `memcpy` + explicit null-termination
  - `localtime` → `localtime_s` (Windows) or `localtime_r` (POSIX)

- **Forward declaration type mismatch:** Forward declarations must match actual type
  - `class X;` forward declaration must match `class X { ... }` definition
  - `struct X;` forward declaration must match `struct X { ... }` definition
  - Common mismatches: `AppSettings`, `SearchResult`, `SavedSearch`

#### Cross-Platform Compatibility
- **File paths:** Use platform-agnostic path handling (PathUtils functions)
- **Thread primitives:** Verify mutex/lock usage works on all platforms
- **Resource management:** Windows handles vs POSIX file descriptors

---

### 2. Memory Management & Leaks (CRITICAL)

#### Move Semantics Issues
- **Move constructors/assignment operators:** Must transfer ownership, not copy
  - Check: Dynamically allocated resources (pointers, arrays) are transferred, not copied
  - Verify: Source object's pointers are nullified after transfer
  - **Common bug:** Move assignment operator doesn't clean up existing resources before transferring
  - Pattern to check:
    ```cpp
    // ❌ WRONG - Copies instead of moves
    dfa_table_ = new uint16_t[...];
    memcpy(dfa_table_, other.dfa_table_, ...);
    
    // ✅ CORRECT - Transfers ownership
    dfa_table_ = other.dfa_table_;
    other.dfa_table_ = nullptr;
    ```

#### Container Memory Leaks
- **Cache invalidation:** Caches must be cleared during rebuilds/recomputations
  - Check: `directory_path_to_id_` cleared in `RecomputeAllPaths()`
  - Check: All cache maps/sets invalidated during rebuilds
  - Check: `shrink_to_fit()` called on vectors after rebuilds

- **Unbounded growth:** Containers must not grow without bounds
  - Check: Vectors, maps, sets have size limits or cleanup mechanisms
  - Check: No accumulation of data across multiple operations

#### Future/Object Cleanup
- **`std::future` cleanup:** Futures must be properly waited for and reset
  - Before starting new async operation: Wait for existing valid future to complete
  - After getting result: Explicitly reset future with `future = std::future<Type>()`
  - In cleanup functions: Clean up futures in destructors and `ClearInputs()`
  - Pattern:
    ```cpp
    if (future.valid()) {
      future.wait();  // Wait for completion
      future.get();   // Retrieve result
    }
    future = std::future<Type>();  // Reset to invalid state
    ```

#### RAII Violations
- **Manual resource management:** All resources should use RAII wrappers
  - Windows handles: Use `VolumeHandle` wrapper, not raw `HANDLE`
  - File handles: Use `std::fstream` or RAII wrappers
  - Thread objects: Ensure threads are joined in destructors

---

### 3. Thread Safety & Concurrency (CRITICAL)

#### Data Race Conditions
- **Concurrent reads/writes:** Shared data must be protected
  - Check: `std::vector<bool>` replaced with `std::vector<uint8_t>` (bit packing is unsafe)
  - Check: All shared containers have proper synchronization
  - Check: Read operations use `std::shared_lock`, write operations use `std::unique_lock`

#### Thread Exception Safety
- **Exception handling in threads:** Thread functions must catch all exceptions
  - Check: All thread functions (`UsnReaderThread`, `UsnProcessorThread`, etc.) have try-catch blocks
  - Check: Exceptions are logged, not allowed to terminate threads
  - Pattern:
    ```cpp
    void ThreadFunction() {
      try {
        // thread work
      } catch (const std::exception& e) {
        LOG_ERROR_BUILD("Thread error: " << e.what());
      } catch (...) {
        LOG_ERROR_BUILD("Unknown thread error");
      }
    }
    ```

#### Reference Lifetime in Async Operations
- **Lambda captures:** References captured in async lambdas must remain valid
  - Check: Lambdas don't capture references to objects that may be destroyed
  - Check: Futures are not cleared immediately if tasks are still running
  - Common bug: Clearing futures while tasks still hold references to captured objects

#### Lock Ordering
- **Deadlock prevention:** Multiple locks must be acquired in consistent order
  - Check: All code paths acquire locks in the same order
  - Check: No circular lock dependencies

---

### 4. Performance & Optimization Issues

#### Cache Locality
- **Structure of Arrays (SoA):** Data structures should use SoA for cache efficiency
  - Check: Parallel arrays for related data (offsets, IDs, flags)
  - Check: Contiguous memory buffers for paths (`path_storage_`)

#### Unnecessary Allocations
- **Hot path allocations:** Avoid allocations in frequently called code
  - Check: No temporary string/object creations in search loops
  - Check: Pre-reserved vector capacity when size is known
  - Check: Move semantics used for large objects

#### Algorithm Efficiency
- **Time complexity:** Verify algorithms are optimal
  - Check: O(n²) operations that could be O(n log n) or O(n)
  - Check: Unnecessary nested loops
  - Check: Early exits in loops when possible

---

### 5. Exception Safety & Error Handling

#### Exception Handling Coverage
- **Critical code wrapped:** File I/O, memory allocation, system calls must be in try-catch
  - Check: All `CreateFile`, `ReadFile`, `WriteFile` calls wrapped
  - Check: All `new`/`new[]` allocations wrapped
  - Check: All container operations that may throw wrapped

#### Error Logging
- **Proper logging macros:** Use `LOG_ERROR_BUILD`, `LOG_WARNING_BUILD`, `LOG_INFO_BUILD`
  - Check: Exceptions logged with context (thread ID, values, etc.)
  - Check: Unused exception variable warnings suppressed with `(void)e;` before `LOG_*_BUILD` calls

#### Graceful Degradation
- **Safe defaults:** Errors should return safe defaults, not crash
  - Check: Functions return empty results/containers on error
  - Check: Invalid inputs are validated and defaulted
  - Check: No exceptions escape public APIs

---

### 6. Code Quality & Maintainability

#### DRY Violations
- **Code duplication:** Extract repeated code (>10 lines) into helper functions
  - Check: Similar logic in multiple places
  - Check: Template opportunities for type-agnostic code
  - Check: Lambda reuse opportunities

#### Single Responsibility
- **Class/function purpose:** Each should have one clear purpose
  - Check: Classes don't mix concerns (e.g., UI + business logic)
  - Check: Functions don't do multiple unrelated things
  - Check: Files have focused, cohesive purpose

#### Naming Conventions
- **Follow docs/standards/CXX17_NAMING_CONVENTIONS.md:**
  - Classes/Structs: `PascalCase`
  - Functions/Methods: `PascalCase`
  - Local Variables: `snake_case`
  - Member Variables: `snake_case_` (trailing underscore)
  - Global Variables: `g_snake_case`
  - Constants: `kPascalCase`
  - Namespaces: `snake_case`

#### Dead Code
- **Unused code removal:** Remove unused functions, variables, commented code
  - Check: No commented-out code blocks
  - Check: No unused includes
  - Check: No unused member variables

---

### 7. ImGui Immediate Mode Paradigm

#### Thread Safety
- **All ImGui calls on main thread:** ImGui is NOT thread-safe
  - Check: No ImGui calls from background threads
  - Check: Background work synchronizes results back to main thread
  - Check: UI state updates happen on main thread only

#### Widget Lifecycle
- **No widget storage:** Widgets don't exist as persistent objects
  - Check: No storing widget references
  - Check: UI code reads from data/state, doesn't "update" widgets
  - Check: All UI rendering is stateless (state passed as parameters)

#### Frame-Based Rendering
- **UI rebuilt every frame:** All UI code runs each frame
  - Check: No assumptions about widget state persisting between frames
  - Check: State is derived from data, not stored in widgets

---

### 8. Build System & Dependencies

#### CMake Configuration
- **PGO compatibility:** Test targets sharing source files with main target must use matching PGO flags
  - Check: Test targets with shared sources have same PGO compiler flags
  - Check: Test targets use standard `/LTCG` linker flags (not PGO-specific)
  - Pattern:
    ```cmake
    if(ENABLE_PGO)
      target_compile_options(test_target PRIVATE 
        $<$<CONFIG:Release>:/GL /Gy>  # Match main target
      )
      target_link_options(test_target PRIVATE 
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>  # Standard, not PGO
      )
    endif()
    ```

#### Resource Files
- **Windows icon:** Icon ID must be 1 (lowest ID) for Windows to recognize it
  - Check: `IDI_APP_ICON` defined as 1 in `resource.h`
  - Check: Icon file exists and is included in `APP_RESOURCES`

#### File Organization
- **New files added to build:** Both CMakeLists.txt and Makefile (if applicable) updated
  - Check: All new source files added to appropriate targets
  - Check: Platform-specific files only added to correct platforms

---

### 9. Subtle Bugs to Watch For

#### Integer Overflow/Underflow
- **Size calculations:** Check for overflow in buffer size calculations
- **Index calculations:** Verify indices don't underflow (e.g., `size_t - 1` when size is 0)

#### Off-by-One Errors
- **Loop bounds:** Verify loop conditions are correct
- **Array access:** Check bounds are inclusive/exclusive as intended

#### Null Pointer Dereferences
- **Pointer checks:** Verify pointers are checked before dereferencing
- **Optional values:** Check `std::optional`/`std::unique_ptr` before access

#### Type Conversions
- **Narrowing conversions:** Check for unsafe narrowing (e.g., `size_t` to `int`)
- **Signed/unsigned mismatches:** Verify signed/unsigned comparisons are correct

#### State Consistency
- **Invariant violations:** Check that class invariants are maintained
- **State transitions:** Verify state machines transition correctly
- **Initialization:** Check all member variables are initialized

---

### 10. Architecture & Design Patterns

#### Component Boundaries
- **Clear responsibilities:** Components should have single, clear purpose
- **Dependency direction:** Dependencies should flow in one direction (no circular dependencies)
- **Interface design:** Public APIs should be minimal and focused

#### Resource Ownership
- **Clear ownership:** Every resource should have one clear owner
- **RAII usage:** Resources should be managed with RAII, not manual cleanup
- **Move semantics:** Large objects should use move semantics when appropriate

#### Strategy Pattern
- **Load balancing strategies:** Should be interchangeable without changing client code
- **Virtual function usage:** Base class interfaces should be minimal

---

## Review Process

### Step 1: Platform-Specific Checks
1. Search for `std::min`/`std::max` without parentheses
2. Check all includes are present (especially `<string>`, `<chrono>`)
3. Verify forward declarations match actual types
4. Check unsafe C functions are replaced

### Step 2: Memory Management
1. Review all move constructors/assignment operators
2. Check container cleanup in rebuild/recompute functions
3. Verify future cleanup patterns
4. Check for RAII violations

### Step 3: Thread Safety
1. Identify all shared data structures
2. Verify proper synchronization (locks, atomics)
3. Check exception handling in thread functions
4. Verify reference lifetime in async operations

### Step 4: Performance
1. Identify hot paths (frequently called code)
2. Check for unnecessary allocations
3. Verify cache-friendly data structures
4. Check algorithm complexity

### Step 5: Code Quality
1. Check DRY violations
2. Verify naming conventions
3. Check for dead code
4. Verify single responsibility

### Step 6: Architecture
1. Verify component boundaries
2. Check dependency direction
3. Verify resource ownership
4. Check design pattern usage

---

## Output Format

For each issue found, provide:

1. **Severity:** CRITICAL / HIGH / MEDIUM / LOW
2. **Category:** Platform / Memory / Thread Safety / Performance / Code Quality / Architecture
3. **Location:** File and line number(s)
4. **Description:** Clear explanation of the issue
5. **Impact:** What could go wrong (crash, leak, incorrect behavior, etc.)
6. **Recommendation:** Specific fix or improvement
7. **Code Example:** Before/after code if helpful

---

## Example Review Output

```
### Issue 1: Memory Leak in Move Assignment Operator
- **Severity:** CRITICAL
- **Category:** Memory
- **Location:** PathPatternMatcher.cpp:145-160
- **Description:** The move assignment operator transfers ownership of `dfa_table_` from `other` but doesn't clean up the existing `dfa_table_` in `this` object before transferring.
- **Impact:** Memory leak if move assignment is called on an object that already has allocated `dfa_table_`.
- **Recommendation:** Add cleanup of existing `dfa_table_` before transferring:
  ```cpp
  if (dfa_table_ != nullptr) {
    delete[] dfa_table_;
  }
  dfa_table_ = other.dfa_table_;
  other.dfa_table_ = nullptr;
  ```
```

---

## Additional Resources

- **Naming Conventions:** `docs/standards/CXX17_NAMING_CONVENTIONS.md`
- **Production Checklist:** `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md` (Quick section for commits, Comprehensive section for major features)
- **Architecture Docs:** `docs/design/`
- **Known Issues:** `docs/ARCHITECTURAL_REVIEW_REMAINING_ISSUES-2025-12-29.md`

---

**Remember:** Focus on finding subtle bugs that automated tools miss. Look for patterns, not just individual issues. Consider the full context of how code interacts across components and threads.

