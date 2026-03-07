# Test Strategy Audit - 2026-02-18

## Executive Summary
- **Test Health Score**: 8/10
- **Coverage Estimate**: ~75% line coverage
- **Critical Gaps**: `UsnMonitor` (Real-time monitoring logic)
- **Total Findings**: 10
- **Estimated Remediation Effort**: 24 hours

## Findings

### High
1. **Missing Tests for `UsnMonitor` (Coverage Gap)**
   - **Component**: `src/usn/UsnMonitor.cpp`
   - **Issue Type**: Coverage Gaps
   - **Gap Description**: `UsnMonitor` is a critical system-level component that handles USN Journal reading, record parsing, and privilege management, but it has no dedicated unit tests.
   - **Risk**: Bugs in USN record processing or privilege dropping could lead to incorrect indexing or security vulnerabilities.
   - **Suggested Action**: Create `UsnMonitorTests.cpp`. Use a mock USN Journal source to test record processing logic.
   - **Priority**: High
   - **Effort**: Large (8 hours)

### Medium
1. **Low Coverage of Error Paths in Crawler**
   - **Component**: `src/crawler/FolderCrawler.cpp`
   - **Issue Type**: Error Path Coverage
   - **Gap Description**: While happy-path crawling is tested, there are no tests for access denied errors, disk errors, or long path handling.
   - **Risk**: Improper handling of OS errors could lead to crashes or incomplete indexes during real-world use.
   - **Suggested Action**: Add tests that simulate `ERROR_ACCESS_DENIED` and verify that the crawler continues and logs the error correctly.
   - **Priority**: Medium
   - **Effort**: Medium (4 hours)

2. **No Regression Tests for ReDoS**
   - **Component**: `src/utils/StdRegexUtils.h`
   - **Issue Type**: Missing Test Types (Performance/Security)
   - **Gap Description**: No tests for pathological regex patterns that could cause ReDoS.
   - **Risk**: Accidental introduction of complex regex patterns could lead to DoS.
   - **Suggested Action**: Add a performance test with known-bad regex patterns to ensure they don't hang the application (or are rejected).
   - **Priority**: Medium
   - **Effort**: Small (2 hours)

### Low
1. **Missing Unicode/RTL Path Tests**
   - **Component**: `src/path/PathUtils.cpp`
   - **Issue Type**: Boundary Testing
   - **Gap Description**: Most tests use simple ASCII paths.
   - **Risk**: Issues with character encoding or path normalization for non-English users.
   - **Suggested Action**: Add test cases for paths with Emoji, RTL characters (Arabic/Hebrew), and combining marks.
   - **Priority**: Low
   - **Effort**: Small (2 hours)

## Test Health Score: 8/10
The project has a strong foundation of unit tests and benchmarks. The use of **ASAN** by default is a major plus for C++ reliability. Most core algorithms (search, pattern matching, path storage) are well-covered.

## Coverage Estimate: ~75%
Good coverage for `src/search`, `src/index`, and `src/utils`. Major gap in `src/usn`.

## Flakiness Assessment
Tests appear stable. The use of `wait_for` with generous timeouts in concurrency tests reduces flakiness.

## Quick Wins
1. **Add Unicode path tests** to `PathUtilsTests.cpp`.
2. **Add negative regex tests** (invalid patterns) to `StdRegexUtilsTests.cpp`.

## Recommended Testing Strategy
1. **Critical**: Implement `UsnMonitor` unit tests using a mock source.
2. **Enhancement**: Integrate **Thread Sanitizer (TSAN)** into the build process to catch data races.
3. **Architecture**: Extract UI logic from `ResultsTable.cpp` into testable helper classes.

## Infrastructure Improvements
- **CI/CD**: Ensure all tests run on every PR for all platforms (Windows, Linux, macOS).
- **Coverage Tracking**: Integrate `lcov` reports into the CI pipeline.
