# Test Strategy Review - 2026-02-09

## Executive Summary
- **Health Score**: 8/10
- **Critical Issues**: 0
- **High Issues**: 1
- **Total Findings**: 12
- **Estimated Remediation Effort**: 24 hours

## Findings

### High
- **Component**: `src/platform/windows/UsnMonitor.cpp`
- **Issue Type**: Threading & Concurrency Testing
- **Gap Description**: Lack of stress tests for USN journal overflows and race conditions during initialization. A known race condition exists but is not currently caught by the test suite.
- **Risk**: Rare but catastrophic crashes in the field that are difficult to reproduce.
- **Suggested Action**: Add a dedicated `UsnMonitorConcurrencyTests` suite that uses mocks to simulate high-frequency journal events and concurrent initialization/failure cycles.
- **Priority**: High
- **Effort**: M

### Medium
- **Component**: `src/ui/*`
- **Issue Type**: Missing Test Types (Integration Tests)
- **Gap Description**: UI components are mostly untested. While logic is separated, the integration between `GuiState`, `SearchController`, and the rendering loop is not verified automatically.
- **Risk**: UI regressions (e.g., fields disappearing, search not triggering) after refactorings.
- **Suggested Action**: Implement headless UI tests using a mock ImGui context or verify state transitions in `GuiStateTests` more thoroughly.
- **Priority**: Medium
- **Effort**: L

- **Component**: `src/api/GeminiApiHttp_linux.cpp` (and others)
- **Issue Type**: Coverage Analysis (Platform-specific)
- **Gap Description**: Platform-specific HTTP implementations are not fully tested in CI across all platforms.
- **Risk**: Network features failing on one platform while working on another.
- **Suggested Action**: Ensure `GeminiApiUtilsTests` runs on Windows, macOS, and Linux in the CI pipeline.
- **Priority**: Medium
- **Effort**: S

### Low
- **Component**: Project-wide
- **Issue Type**: Documentation (Stale References)
- **Gap Description**: `TESTING.md` and some comments still refer to GoogleTest (gtest), but the project has migrated to **doctest**.
- **Risk**: Confusion for new contributors.
- **Suggested Action**: Update all documentation to reflect the use of doctest.
- **Priority**: Low
- **Effort**: S

## Summary Requirements

- **Test Health Score**: 8/10. The project has a solid foundation of unit tests for core logic (Path matching, Indexing, Search engine).
- **Coverage Estimate**:
  - Core Logic: ~85%
  - Platform/USN: ~60%
  - UI: ~20%
- **Critical Gaps**: Concurrency testing for USN Journal monitoring and full end-to-end integration tests for the search pipeline.
- **Flakiness Assessment**: Some timing-dependent tests in `StreamingResultsCollectorTests` might be flaky on slow CI environments.
- **Quick Wins**:
  1. Fix stale GoogleTest references in docs.
  2. Add `[[nodiscard]]` to fallible methods to catch ignored returns at compile time.
  3. Enable ASan for all Linux/macOS test runs.
- **Recommended Testing Strategy**:
  - Maintain the strong unit test base for core utilities.
  - Expand integration tests for the `SearchWorker` -> `Collector` -> `UI` pipeline.
  - Invest in property-based testing for path pattern matching.
- **Infrastructure Improvements**: Integrate **ThreadSanitizer (TSan)** to catch data races in the parallel search engine.
