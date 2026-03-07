# Test Strategy Audit - 2026-03-06

## Executive Summary
- **Health Score**: 9/10
- **Critical Issues**: 0
- **High Issues**: 1
- **Total Findings**: 4
- **Estimated Remediation Effort**: 16 hours

## Findings

### Critical
None.

### High
1. **Low Test Coverage for USN Journal Monitoring** (Category 1)
   - **Location**: `src/usn/UsnMonitor.cpp`
   - **Smell Type**: Missing unit/integration tests for critical real-time monitoring.
   - **Impact**: Regression risk in journal reading and event processing. Changes here could break index synchronization without detection.
   - **Refactoring Suggestion**: Extract `UsnProcessor` logic to a mockable interface and test event processing with simulated USN records.
   - **Severity**: High
   - **Effort**: Large (8+ hours)

### Medium
1. **Missing Cross-platform Integration Tests** (Category 2)
   - **Location**: `tests/path_pattern_integration_test.cpp`
   - **Smell Type**: Tests that might be Windows-centric.
   - **Impact**: Potential failures or false positives on Linux/macOS.
   - **Refactoring Suggestion**: Use `GetPlatformSpecificPath()` helper more systematically across all integration tests.
   - **Severity**: Medium
   - **Effort**: Medium (4 hours)

### Low
1. **Test Fixture Duplication: ParallelSearchEngineTests.cpp** (Category 1)
   - **Location**: `tests/ParallelSearchEngineTests.cpp`
   - **Smell Type**: Repeated setup of `SearchThreadPool` and `ParallelSearchEngine`.
   - **Impact**: Boilerplate-heavy tests and harder to change setup.
   - **Refactoring Suggestion**: Use the new `TestParallelSearchEngineFixture` in `TestHelpers.h`.
   - **Severity**: Low
   - **Effort**: Small (2 hours)

2. **Incomplete Edge Case Testing: StdRegexUtilsTests.cpp** (Category 1)
   - **Location**: `tests/StdRegexUtilsTests.cpp`
   - **Smell Type**: Limited testing of complex regex patterns.
   - **Impact**: Might not catch ReDoS or parsing bugs in edge cases.
   - **Refactoring Suggestion**: Add more comprehensive test cases using the `RegexMatchTestCase` helper.
   - **Severity**: Low
   - **Effort**: Small (2 hours)

## Test Strategy Score: 9/10
The project has a very high test count (27 executables) and uses modern practices (doctest, RAII fixtures, mock objects). The search strategies and core index logic are thoroughly tested across all platforms.

## Top 3 Systemic Issues
1. **Missing USN Integration Tests**: Real-time monitoring needs a way to be tested with simulated kernel events.
2. **Platform-specific Test Isolation**: Some tests still have Windows-only blocks that could be generalized or stubbed.
3. **Complexity of Setup**: Some fixtures have complex setup (e.g., `TestFileIndexFixture`) that could be simplified with more modular factories.

## Recommended Actions
1. **Phase 1**: Refactor `ParallelSearchEngineTests` to use shared fixtures.
2. **Phase 2**: Implement a USN record simulator for `UsnMonitor` integration tests.
3. **Phase 3**: Audit all integration tests for cross-platform compatibility.

## Testing Improvement Areas
- **USN Record Processing**: High-priority area for mocking.
- **UI Interaction**: Could benefit from ImGui Test Engine integration.
- **Error Scenarios**: More testing of I/O failures and memory exhaustion.
