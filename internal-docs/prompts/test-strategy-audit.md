# Test Strategy Audit Prompt

**Purpose:** Evaluate test coverage, quality, and strategy for this cross-platform file indexing application. Identify gaps and opportunities to improve confidence in the codebase.

---

## Prompt

```
You are a Senior QA Architect and Test Engineer specializing in C++ test strategies. Audit the test suite for this cross-platform file indexing application (Windows USN Journal monitor with macOS and Linux support).

## Testing Context
- **Framework**: doctest
- **Test Location**: `tests/` directory with ~20 test files
- **Mocking**: Custom mock classes (e.g., `MockSearchableIndex.h`)
- **Coverage Tools**: gcov/lcov for coverage reports
- **Build**: CMake with separate test targets

---

## Scan Categories

### 1. Coverage Analysis

**Missing Test Files**
- Production code files with no corresponding `*Tests.cpp`
- New features added without test coverage
- Platform-specific files (`*_win.cpp`, `*_mac.mm`, `*_linux.cpp`) untested

**Coverage Gaps by Type**
- Public methods with no test cases
- Private methods with complex logic untested (via public API)
- Error handling paths never exercised
- Edge cases not covered (empty input, max values, null pointers)

**Coverage Metrics**
- Line coverage < 80% in critical components
- Branch coverage neglected (only happy paths tested)
- Function coverage missing for utility functions

---

### 2. Test Quality

**Test Smell Detection**
- Tests > 50 lines (should be split into focused cases)
- Multiple assertions testing unrelated behaviors
- Tests with no assertions (verify something!)
- Commented-out tests still in source
- Magic numbers without explanation

**Test Independence Issues**
- Tests relying on execution order
- Shared mutable state between tests
- Tests modifying global state without cleanup
- File system or network dependencies without isolation

**Assertion Quality**
- Weak assertions (`CHECK(result)` vs `CHECK_EQ(result, expected)`)
- Missing failure messages for complex assertions
- No assertion on exception messages, only exception type

---

### 3. Test Organization

**Naming Conventions**
- Test names not describing behavior (e.g., `Test1`, `TestCase`)
- Inconsistent naming patterns across test files
- Test fixture names not matching SUT class names

**Test Structure**
- Missing test fixtures for tests with common setup
- Duplicate setup code across multiple tests
- Test files with unrelated test cases grouped together

**Test Categories**
- No distinction between unit/integration/performance tests
- Missing test categories in CMake (can't run subsets)
- Slow tests not marked for optional execution

---

### 4. Mock & Stub Strategy

**Mock Coverage**
- Dependencies without mock implementations
- Mocks not verifying interaction expectations
- Over-mocking (mocking SUT internals, not just dependencies)

**Mock Quality**
- Mocks with hardcoded return values (not configurable)
- Complex mock implementations that need their own tests
- Mocks not matching interface changes (drift)

**Isolation**
- File system operations not abstracted for testing
- Time-dependent code not injectable
- Threading code difficult to test deterministically

---

### 5. Threading & Concurrency Testing

**Race Condition Tests**
- Thread-safety not tested under concurrency
- No stress tests for lock contention
- TSAN (Thread Sanitizer) not integrated

**Synchronization Testing**
- Producer-consumer queues not tested for correctness
- Shutdown sequences not verified
- Thread pool behavior under load untested

**Nondeterminism**
- Tests with timing dependencies (flaky)
- Random ordering causing intermittent failures
- Platform-specific threading behavior differences

---

### 6. Error & Edge Case Testing

**Error Path Coverage**
- Windows API failures not simulated
- Out-of-memory conditions not tested
- File permission errors not exercised
- Invalid input handling incomplete

**Boundary Testing**
- Empty collections
- Single-element collections
- Maximum size limits
- Unicode edge cases (combining characters, RTL)
- Path length limits (MAX_PATH, long paths)

**Negative Testing**
- Invalid parameters not tested
- Null pointer handling not verified
- Type boundary values (INT_MAX, SIZE_MAX)

---

### 7. Test Infrastructure

**Build Integration**
- Tests not run automatically in CI
- Test failures not blocking merges
- Coverage reports not generated

**Test Performance**
- Slow tests not optimized
- Test parallelization not enabled
- Repeated expensive setup across tests

**Maintenance Burden**
- Brittle tests failing on unrelated changes
- Tests tightly coupled to implementation details
- Hard-to-understand test failures

---

### 8. Missing Test Types

**Integration Tests**
- Component interactions not tested end-to-end
- Platform integration (Windows API, macOS APIs) not verified
- File system operations not tested with real files

**Performance Tests**
- No baseline benchmarks for search performance
- Memory usage not measured
- Regression tests for performance not automated

**Property-Based Testing**
- Invariants not tested with random input generation
- Edge cases not discovered through fuzzing
- SearchPatternUtils regex patterns not fuzz-tested

---

## Output Format

For each finding:
1. **Component**: File or module name
2. **Issue Type**: Category from above
3. **Gap Description**: What testing is missing or flawed
4. **Risk**: What bugs could slip through?
5. **Suggested Action**: Specific test to add or fix
6. **Priority**: Critical / High / Medium / Low
7. **Effort**: S/M/L

---

## Summary Requirements

End with:
- **Test Health Score**: 1-10 with justification
- **Coverage Estimate**: Approximate line/branch coverage
- **Critical Gaps**: Components with highest risk from missing tests
- **Flakiness Assessment**: Are there timing-dependent or order-dependent tests?
- **Quick Wins**: Easy tests to add with high value
- **Recommended Testing Strategy**: Unit vs Integration vs E2E balance
- **Infrastructure Improvements**: CI/CD, coverage tracking, sanitizers
```

---

## Usage Context

- Before major releases to assess confidence
- After discovering production bugs (was it testable?)
- When planning test improvement sprints
- During code reviews to suggest missing tests

---

## Test Improvement Checklist

After running this audit:

- [ ] Add tests for critical uncovered components
- [ ] Fix or remove flaky tests
- [ ] Enable TSAN/ASAN in CI
- [ ] Add coverage reporting to CI pipeline
- [ ] Create integration test suite for platform APIs
- [ ] Document testing strategy in `docs/TESTING.md`
