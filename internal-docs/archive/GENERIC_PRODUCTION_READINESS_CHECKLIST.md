# Generic Production Readiness Checklist

**Purpose**: Use this checklist for every new feature or significant code change to ensure production-ready quality.

---

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
  - Check for unbounded container growth (vectors, maps, sets)
  - Verify caches are cleared during rebuilds
- [ ] **Common leak sources to check**:
  - Containers that grow without bounds (e.g., `directory_path_to_id_` not cleared)
  - File handles not closed
  - Thread objects not joined
  - Callback objects not released
  - Caches not invalidated during rebuilds
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
    - Examples: `PRIORITIZED_DEVELOPMENT_PLAN.md`, `NEXT_STEPS.md`, `plans/production/QUICK_PRODUCTION_CHECKLIST.md`
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

**Last Updated**: Based on load balancing strategy implementation
**Version**: 1.0
