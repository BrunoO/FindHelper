# Test Strategy Review - 2026-02-14

## Executive Summary
- **Health Score**: 7/10
- **Critical Issues**: 0
- **High Issues**: 1
- **Total Findings**: 6
- **Estimated Remediation Effort**: 16 hours

## Findings

### High
- **Lack of Integration Tests for USN Journal Monitoring**:
  - **Issue**: Most tests are unit tests for the index or search engine. There are no automated integration tests that simulate USN record streams to verify the real-time update logic.
  - **Risk**: Regressions in the USN monitor are only caught during manual Windows testing.
  - **Remediation**: Create a mock USN provider that can inject records into `UsnMonitor` for cross-platform testing.
  - **Severity**: High

### Medium
- **Manual Verification of UI Components**:
  - **Issue**: UI logic (popups, tables, settings) is largely untested by automated scripts.
  - **Risk**: UI regressions are common when refactoring `GuiState`.
  - **Remediation**: Implement Playwright-based frontend tests or ImGui-specific test automation.
  - **Severity**: Medium

- **Stale References to GoogleTest**:
  - **Issue**: Project has migrated to `doctest`, but `TESTING.md` and some comments still reference `gtest`.
  - **Risk**: Confusion for new developers.
  - **Remediation**: Update all documentation to reflect `doctest` usage.
  - **Severity**: Medium

### Low
- **Inconsistent Test Coverage for Platform-specific Code**:
  - **Issue**: `_linux.cpp` and `_mac.mm` variants have less coverage than Windows counterparts.
  - **Severity**: Low

## Quick Wins
- Update `TESTING.md` to remove stale `gtest` references.
- Add basic smoke tests for `SearchController`.

## Recommended Actions
- **Implement a USN injection test harness** to allow testing the monitor logic on Linux/macOS.
- **Set up CI to run tests on all three platforms** (Windows, Linux, macOS).

---
**Health Score**: 7/10 - The core search engine is well-tested with 24 passing tests, but integration and UI testing are missing.
