# Test Strategy Review - 2026-02-01

## Executive Summary
- **Health Score**: 8/10
- **Coverage Estimate**: ~75-80%
- **Critical Gaps**: 1
- **Flakiness Assessment**: Low
- **Total Findings**: 4
- **Estimated Remediation Effort**: 12 hours

## Findings

### Component: `UsnMonitor`
- **Issue Type**: Coverage Gaps by Type
- **Gap Description**: Missing unit tests for the USN reader/processor threads. Most `UsnMonitor` tests are integration tests or only cover public methods. Error conditions like journal wraps and initialization races (identified in Error Handling review) are not adequately tested.
- **Risk**: Regression in critical real-time monitoring logic or startup crashes.
- **Suggested Action**: Add mocked tests for `ReaderThread` and `ProcessorThread` that simulate various Windows API failure codes.
- **Priority**: High
- **Effort**: M

### Component: `ParallelSearchEngine`
- **Issue Type**: Concurrency Testing
- **Gap Description**: While there are tests for searching, there are few stress tests that perform searches while the index is being modified (Concurrent Read/Write).
- **Risk**: Race conditions or deadlocks during background indexing.
- **Suggested Action**: Add a stress test that spawns multiple search threads and multiple writer threads (inserting/removing/moving files) to verify lock correctness.
- **Priority**: Medium
- **Effort**: M

### Component: `StdRegexUtils`
- **Issue Type**: Error & Edge Case Testing
- **Gap Description**: Missing tests for complex or malformed regex patterns that might trigger `std::regex_error`.
- **Risk**: Application crash or unhandled exception when user enters invalid regex.
- **Suggested Action**: Add a property-based test or a fuzzer for regex search patterns.
- **Priority**: Medium
- **Effort**: S

### Component: `FileIndex`
- **Issue Type**: Missing Test Types (Performance)
- **Gap Description**: No automated regression tests for search performance. If a change slows down the AVX2 path, it might not be noticed until manual testing.
- **Risk**: Performance regressions in the most critical hot path.
- **Suggested Action**: Integrate `SearchBenchmark.cpp` into the CI pipeline with threshold checks.
- **Priority**: Low
- **Effort**: S

## Summary Requirements

### Test Health Score: 8/10
Justification: The project has a very high number of unit tests covering almost all core utility classes and data structures. The use of `doctest` provides fast execution. The main weakness is in testing the asynchronous multi-threaded interactions and platform-specific error paths.

### Coverage Estimate
Approximate line coverage: 80%
Approximate branch coverage: 65%

### Critical Gaps
- `UsnMonitor` initialization error paths.
- Concurrent read/write stress testing for `FileIndex`.

### Flakiness Assessment
The tests appear to be stable and avoid external dependencies (using stubs for Windows types). No flakiness was observed during Linux build verification.

### Quick Wins
1. Add a test case for invalid regex in `StdRegexUtilsTests.cpp`.
2. Add a basic multi-threaded search test to `ParallelSearchEngineTests.cpp`.

### Recommended Testing Strategy
Continue the current approach of heavy unit testing for logic. Shift focus toward "Failure Mode and Effects Analysis" (FMEA) testing for the USN monitor and UI-Search integration.

### Infrastructure Improvements
- Enable Thread Sanitizer (TSAN) in the Linux build to catch data races.
- Add a code coverage reporting step to the build process.
