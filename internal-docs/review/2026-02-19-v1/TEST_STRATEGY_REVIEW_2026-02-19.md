# Test Strategy Audit - 2026-02-19

## Executive Summary
- **Test Health Score**: 7/10
- **Coverage Estimate**: 65-70% (High for core logic, low for UI and USN integration)
- **Critical Issues**: 1
- **Total Findings**: 10
- **Estimated Remediation Effort**: 60 hours

## Findings

### Critical
1. **Missing Tests for UsnMonitor**
   - **Component**: `src/usn/UsnMonitor.cpp`
   - **Issue Type**: Coverage Analysis: Missing Test Files
   - **Gap Description**: `UsnMonitor` is the most critical system-level component but has no corresponding unit or integration tests.
   - **Risk**: Regression in USN record parsing or threading logic could lead to index corruption or application hangs without warning.
   - **Suggested Action**: Create `UsnMonitorTests.cpp`. Use a mock `DeviceIoControl` or a simulated USN stream to verify record processing.
   - **Priority**: Critical
   - **Effort**: Large

### High
1. **Lack of Integration Tests for Platform APIs**
   - **Component**: `src/platform/`
   - **Issue Type**: Missing Test Types: Integration Tests
   - **Gap Description**: Platform-specific logic (file operations, shell context, drag-drop) is largely untested in an automated fashion.
   - **Risk**: Platform-specific bugs (e.g., in `DeleteFileToRecycleBin`) may go unnoticed.
   - **Suggested Action**: Implement integration tests that exercise these APIs on real files in a temporary directory.
   - **Priority**: High
   - **Effort**: Large

2. **No UI Regression Testing**
   - **Component**: `src/ui/`
   - **Issue Type**: Missing Test Types: UI Integration
   - **Gap Description**: Rendering logic in `ResultsTable.cpp` and `Popups.cpp` is only manually verified.
   - **Risk**: UI regressions (e.g., broken buttons or layout issues) are common in immediate-mode GUIs.
   - **Suggested Action**: Implement basic visual regression tests or use ImGui's testing engine for interaction verification.
   - **Priority**: High
   - **Effort**: Medium

### Medium
1. **Limited Concurrency Stress Tests**
   - **Component**: `ParallelSearchEngine`
   - **Issue Type**: Threading & Concurrency Testing: Race Condition Tests
   - **Gap Description**: While `ParallelSearchEngine` has unit tests, they don't explicitly stress race conditions between search and index updates.
   - **Risk**: Intermittent deadlocks or data races under high load.
   - **Suggested Action**: Add a "StressTest" that runs continuous searches while simultaneously updating the index with random data.
   - **Priority**: Medium
   - **Effort**: Medium

2. **Hard-coded Test Data in Source**
   - **Component**: Multiple test files
   - **Issue Type**: Test Quality: Magic numbers
   - **Gap Description**: Many tests use hard-coded path strings and ID values.
   - **Risk**: Brittle tests that are hard to update when logic changes.
   - **Suggested Action**: Centralize test data generation in `TestHelpers.cpp`.
   - **Priority**: Medium
   - **Effort**: Small

### Low
1. **Inconsistent Test Naming**
   - **Component**: `tests/`
   - **Issue Type**: Test Organization: Naming Conventions
   - **Gap Description**: Mix of `TestCase` and descriptive names.
   - **Risk**: Harder to locate specific failures in CI logs.
   - **Suggested Action**: Standardize on `Component_Scenario_ExpectedResult` naming pattern.
   - **Priority**: Low
   - **Effort**: Small

## Summary Requirements

- **Test Health Score**: 7/10. The project has a solid foundation of unit tests for core utilities and algorithms, but misses automated verification for the most complex integration points.
- **Coverage Estimate**:
  - `src/utils`: 90%
  - `src/index`: 80%
  - `src/search`: 75%
  - `src/usn`: < 5%
  - `src/ui`: < 5%
- **Critical Gaps**: `UsnMonitor` and UI interaction.
- **Flakiness Assessment**: Low. Existing unit tests appear deterministic.
- **Quick Wins**:
  - Add `UsnRecordUtils` unit tests (low effort, high value).
  - Add `[[nodiscard]]` to all return values used in assertions.
- **Recommended Testing Strategy**:
  - Focus next on "System Integration" tests that use a real (but controlled) filesystem.
  - Implement "Contract Tests" for the `RendererInterface` to ensure cross-platform consistency.
- **Infrastructure Improvements**:
  - Integrate Thread Sanitizer (TSAN) into the CI pipeline.
  - Automate code coverage report generation on every PR.
