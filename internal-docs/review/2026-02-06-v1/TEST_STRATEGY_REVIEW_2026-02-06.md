# Test Strategy Audit - 2026-02-06

## Executive Summary
- **Health Score**: 8/10
- **Coverage Estimate**: 75-80% for core logic, < 20% for platform-specific code.
- **Critical Gaps**: Platform-specific code (USN monitoring, Windows/macOS/Linux UI integration).
- **Flakiness Assessment**: Low. Existing tests are mostly deterministic unit tests.
- **Estimated Remediation Effort**: 15 hours

## Findings

### High
1. **Low Coverage for Platform-Specific Modules**
   - **Component**: `src/platform/`, `src/usn/`
   - **Issue Type**: Coverage Analysis
   - **Gap Description**: Files like `UsnMonitor.cpp`, `FileOperations_win.cpp`, and `FontUtils_linux.cpp` have almost no automated tests.
   - **Risk**: Regressions in platform-specific behavior (e.g., USN parsing logic, font loading) can slip through, especially since developers may not have all target platforms for testing.
   - **Suggested Action**: Introduce integration tests that use mock system calls or temporary test directories/files to exercise platform-specific paths.
   - **Priority**: High
   - **Effort**: Large

2. **Lack of Concurrency Stress Tests**
   - **Component**: `FileIndex`, `SearchWorker`
   - **Issue Type**: Threading & Concurrency Testing
   - **Gap Description**: While there are unit tests for parallel search, there are no stress tests that simulate heavy index modification (updates/removals) occurring simultaneously with multiple heavy searches.
   - **Risk**: Race conditions or deadlocks that only manifest under high load.
   - **Suggested Action**: Add a `FileIndexStressTest` that spawns many writer threads and many reader/searcher threads.
   - **Priority**: High
   - **Effort**: Medium

### Medium
1. **Discrepancy in Testing Framework Documentation**
   - **Component**: Test Infrastructure
   - **Issue Type**: Documentation Gaps
   - **Gap Description**: The internal documentation/prompts refer to GoogleTest (gtest), but the project actually uses `doctest`.
   - **Risk**: Confusion for new developers; incorrect tooling/IDE configuration.
   - **Suggested Action**: Update all documentation and prompts to correctly reference `doctest`.
   - **Priority**: Medium
   - **Effort**: Small

2. **Missing Integration Tests for Search UI**
   - **Component**: `src/ui/`
   - **Issue Type**: Missing Test Types
   - **Gap Description**: No automated tests for the UI logic (e.g., if a search input correctly triggers a `SearchWorker` request).
   - **Risk**: UI regressions where buttons or inputs stop working as expected.
   - **Suggested Action**: Use a headless ImGui test harness or extract more UI logic into testable controller classes.
   - **Priority**: Medium
   - **Effort**: Medium

### Low
1. **Weak Assertions in Some Tests**
   - **Component**: Various unit tests
   - **Issue Type**: Test Quality
   - **Gap Description**: Use of `CHECK(expr)` instead of `CHECK_EQ(val, expected)` makes failure messages less informative.
   - **Risk**: Slower debugging when tests fail.
   - **Suggested Action**: Audit and upgrade assertions to provide more context.
   - **Priority**: Low
   - **Effort**: Small

## Quick Wins
1. **Add `FileIndexStressTest`**: A simple multi-threaded loop to catch obvious race conditions.
2. **Standardize Test Naming**: Ensure all tests follow a consistent `[Component]_[Behavior]` naming pattern.
3. **Integrate ASAN/TSAN into build**: Enable AddressSanitizer and ThreadSanitizer in a dedicated test build configuration.

## Recommended Testing Strategy
1. **Maintain high Unit Test coverage** for core algorithms (search, indexing, path manipulation).
2. **Introduce "Integration Layer" tests** for platform abstractions using filesystem stubs.
3. **Automate Performance Baselines**: Run benchmarks as part of the CI and alert on significant regressions.
4. **Shift-Left Security**: Include Fuzzing for `SearchPatternUtils` (regex parsing).

## Infrastructure Improvements
- **CI Integration**: Ensure `ctest` is run on all three platforms (Windows, macOS, Linux) for every PR.
- **Coverage Reports**: Automate generation of coverage reports (LCOV/Gcov) and track over time.
- **Benchmark Tracking**: Store benchmark results to visualize performance trends.
