# Test Strategy Review - 2026-01-31

## Executive Summary
- **Health Score**: 7/10
- **Critical Issues**: 0
- **High Issues**: 1
- **Total Findings**: 6
- **Estimated Remediation Effort**: 24 hours

## Findings

### High

#### 1. Lack of Automated UI Testing
- **Component**: `src/ui/`
- **Issue Type**: Missing Test Types
- **Gap Description**: There are no automated tests for the ImGui UI components. Changes to the UI must be verified manually, which is error-prone and slow.
- **Risk**: UI regressions (e.g., broken buttons, layout issues, data display bugs) can easily slip into releases.
- **Suggested Action**: Integrate `imgui_test_engine` or a similar framework to automate basic UI interactions and screenshot comparisons.
- **Priority**: High
- **Effort**: Large

### Medium

#### 2. Missing Tests for UsnMonitor and MftMetadataReader
- **Component**: `src/usn/UsnMonitor.cpp`, `src/index/mft/MftMetadataReader.cpp`
- **Issue Type**: Coverage Gaps by Type
- **Gap Description**: These components interact directly with the Windows kernel and are not covered by unit tests.
- **Risk**: Logic errors in USN record parsing or MFT metadata extraction could lead to index corruption.
- **Suggested Action**: Create a mockable interface for `DeviceIoControl` to allow simulating USN and MFT records in unit tests.
- **Priority**: Medium
- **Effort**: Medium

#### 3. No Thread Sanitizer (TSAN) Integration
- **Component**: Infrastructure
- **Issue Type**: Threading & Concurrency Testing
- **Gap Description**: While ASAN is enabled, TSAN is not configured in `CMakeLists.txt`.
- **Risk**: Race conditions in the multi-threaded search or index update paths may go undetected until they cause intermittent crashes in production.
- **Suggested Action**: Add a `ENABLE_TSAN` option to `CMakeLists.txt` and run tests with it in CI.
- **Priority**: Medium
- **Effort**: Small

### Low

#### 4. Magic Numbers in Tests
- **Component**: `tests/ParallelSearchEngineTests.cpp`
- **Issue Type**: Test Smell Detection
- **Gap Description**: Several tests use hardcoded item counts and delays (e.g., `1000`, `10ms`) without explanation.
- **Risk**: Makes tests harder to maintain and understand when they fail or need adjustment.
- **Suggested Action**: Replace magic numbers with named constants and add comments explaining the choice of values.
- **Priority**: Low
- **Effort**: Small

#### 5. Weak Assertions in Some Utility Tests
- **Component**: `tests/StringUtilsTests.cpp`
- **Issue Type**: Assertion Quality
- **Gap Description**: Use of `CHECK(result == expected)` instead of more descriptive `REQUIRE_EQ` (in doctest) or equivalent that provides better failure messages.
- **Risk**: Harder to diagnose test failures without looking at the code.
- **Suggested Action**: Standardize on descriptive assertions that provide both values on failure.
- **Priority**: Low
- **Effort**: Small

#### 6. Inconsistent Test Fixture Usage
- **Component**: Various
- **Issue Type**: Test Structure
- **Gap Description**: Some tests use `TEST_CASE` with local setup, while others use `TEST_CASE_FIXTURE`.
- **Risk**: Code duplication in test setup.
- **Suggested Action**: Refactor tests with common setup (e.g., `FileIndex` population) to use shared fixtures.
- **Priority**: Low
- **Effort**: Medium

## Summary Requirements

- **Test Health Score**: 7/10. The core logic is well-tested, and the use of object libraries for shared test sources is an excellent build-time optimization. However, the lack of UI and hardware-level (USN/MFT) testing is a significant gap.
- **Coverage Estimate**: 65-70% (Estimated). Core libraries are likely >80%, but UI and platform-specific modules are near 0%.
- **Critical Gaps**: UI interaction logic and low-level USN parsing.
- **Flakiness Assessment**: Low. Most tests are deterministic. Potential flakiness in `ParallelSearchEngineTests` due to reliance on thread timing.
- **Quick Wins**:
  1. Enable TSAN in `CMakeLists.txt` (15 min).
  2. Mock `DeviceIoControl` for USN parsing tests (4 hours).
- **Recommended Testing Strategy**:
  1. **Unit**: Continue high coverage for core logic.
  2. **Integration**: Add more tests that simulate multi-threaded index updates during active searches.
  3. **UI**: Start with basic automation for search triggering and result display.
- **Infrastructure Improvements**: Integrate coverage reporting (lcov) and TSAN into the CI pipeline.
