# Unit Test Improvements Rationale

## Overview
The unit test suite has been refactored to transition from imperative, repetitive test cases to a **data-driven testing** approach. This change primarily affects `StringUtilsTests`, `SimpleRegexTests`, and `StringSearchTests`. Additionally, the integration tests in `FileIndexSearchStrategyTests` have been expanded to cover more complex real-world scenarios.

## Key Improvements

### 1. Data-Driven Testing Pattern
Instead of writing individual `REQUIRE` or `CHECK` statements for every input/output pair, we now use a consistent structure:
```cpp
struct TestCase {
    std::string input;
    std::string expected;
};
std::vector<TestCase> test_cases = { /* ... */ };
for (const auto& tc : test_cases) {
    DOCTEST_SUBCASE(tc.input.c_str()) {
        CHECK(FunctionUnderTest(tc.input) == tc.expected);
    }
}
```

**Rationale:**
- **Reduced Boilerplate**: Removes hundreds of lines of identical-looking code.
- **Easier Expansion**: Adding a new test case is now a single line in a vector, rather than a new `TEST_CASE` block.
- **Better Failure Isolation**: `DOCTEST_SUBCASE` ensures that if one data point fails, the rest of the cases in that block still run, and the name of the failing case is clearly reported.

### 2. Enhanced Integration Scenarios
The `FileIndexSearchStrategyTests` now include specialized scenarios that were previously untested:

- **Deep Hierarchy Population**: A new helper allows generating deeply nested directory structures (e.g., 20+ levels).
- **Search on Deep Paths**: Verifies that the recursive path recomputation and search logic remain correct when paths exceed typical nesting depths.
- **Path-Based Filtering**: Explicitly tests the `path_query` parameter, ensuring that "Folder in path" filtering works independently of "Filename" filtering.

**Rationale:**
- **Real-world Robustness**: Users often have very deep directory structures. Ensuring the index handles these correctly prevents "missing results" bugs.
- **Feature Verification**: Path filtering is a core capability of the optimized search engine; dedicated tests ensure no regression in its logic.

### 3. Case Sensitivity & Edge Cases
Expanded the test vectors to include:
- Mixed-case strings across all search algorithms.
- Edge cases for path separators (`/` vs `\`).
- Leading/trailing whitespace in `Trim` and search queries.

**Rationale:**
- **Cross-Platform Consistency**: Since the project is moving towards better macOS/Linux support (via CMake), hardening path separator logic is critical.

## Benefits
- **Maintainability**: The test suite is now much smaller in terms of line count but higher in effective coverage.
- **Documentation**: The `test_cases` vectors serve as clear documentation for the expected behavior of each utility function.
- **Confidence**: Stronger integration tests verify the performance and correctness of the load-balancing strategies (`Static`, `Hybrid`, `Dynamic`) under more stress.
