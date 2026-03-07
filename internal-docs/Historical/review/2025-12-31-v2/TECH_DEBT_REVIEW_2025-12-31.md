# Tech Debt Review - 2025-12-31

## Executive Summary
- **Health Score**: 7/10
- **Critical Issues**: 1
- **High Issues**: 2
- **Total Findings**: 6
- **Estimated Remediation Effort**: 4-6 hours

## Findings

### Critical
1.  **Debt type**: Maintainability Issues
    **Quote**: `ParallelSearchEngineTests` test suite
    **Risk explanation**: The `ParallelSearchEngineTests` suite is failing on Linux. This indicates a potential regression or a platform-specific bug that has gone unnoticed. The failing tests relate to incorrect search result counts and thread count detection, which could point to a serious logical error in the parallel search implementation.
    **Suggested fix**:
    ```cpp
    // In /app/tests/ParallelSearchEngineTests.cpp:238
    // The expected value is 1, but the actual value is 0.
    // This suggests that the search is not finding the expected file.
    // Need to investigate why the mock index or the search function is failing.
    CHECK( stats.matchesFound == 1 ); // Fails with 0 == 1

    // In /app/tests/ParallelSearchEngineTests.cpp:311
    // The test expects a maximum of 4 threads, but the system is reporting 16.
    // This could be a misconfiguration in the test environment or a bug in thread detection logic.
    CHECK( count_large <= 4 ); // Fails with 16 <= 4
    ```
    **Severity**: Critical
    **Effort**: 2-3 hours (investigation and fix)

### High
1.  **Debt type**: Maintainability Issues
    **Quote**: `BUILDING_ON_LINUX.md`
    **Risk explanation**: The `BUILDING_ON_LINUX.md` file is missing several required dependencies for building the project on a fresh Linux environment. This leads to a frustrating and time-consuming trial-and-error process for new developers.
    **Suggested fix**: Update the `BUILDING_ON_LINUX.md` file to include the following packages: `wayland-protocols`, `libwayland-dev`, `libxkbcommon-dev`, `libxrandr-dev`, `libxinerama-dev`, `libxcursor-dev`, and `libxi-dev`.
    **Severity**: High
    **Effort**: 30 minutes

2.  **Debt type**: C++17 Modernization Opportunities
    **Quote**: `FileIndexStorage.h`, `PathUtils.cpp`, `LazyAttributeLoader.cpp`, and others.
    **Risk explanation**: The codebase consistently uses `const std::string&` for function parameters that are only read. This prevents the use of string literals and other string-like objects without creating a `std::string` object, leading to unnecessary allocations and copies.
    **Suggested fix**: Replace `const std::string&` with `std::string_view` in function parameters where the string is only read.
    ```cpp
    // In FileIndexStorage.h
    void InsertLocked(uint64_t id, uint64_t parentID, std::string_view name,
                      bool isDirectory, FILETIME modificationTime,
                      std::string_view extension);

    // In PathUtils.h
    std::string JoinPath(std::string_view base, std::string_view component);
    ```
    **Severity**: High
    **Effort**: 1-2 hours (to update across the codebase)

### Medium
1.  **Debt type**: Naming Convention Violations
    **Quote**: `PathUtils.cpp`
    **Risk explanation**: The function `GetUserHomeRelativePath` in `PathUtils.cpp` is a static helper function, but it is not marked as such and does not follow the `PascalCase` convention for functions.
    **Suggested fix**: Rename the function to `GetUserHomeRelativePath` and mark it as `static`.
    ```cpp
    // In PathUtils.cpp
    static std::string GetUserHomeRelativePath(std::string_view relative_path);
    ```
    **Severity**: Medium
    **Effort**: < 15 minutes

### Low
1.  **Debt type**: Dead/Unused Code
    **Quote**: `docs/archive/UIRENDERER_EXTRACTION_REMAINING_ISSUES.md`
    **Risk explanation**: The file `docs/archive/UIRENDERER_EXTRACTION_REMAINING_ISSUES.md` mentions that `TruncatePathAtBeginning` and `FormatPathForTooltip` are dead code. While `TruncatePathAtBeginning` is still present, `FormatPathForTooltip` appears to have been removed. This indicates that there may still be dead code in the project.
    **Suggested fix**: Investigate if `TruncatePathAtBeginning` is still in use. If not, remove it.
    **Severity**: Low
    **Effort**: 30 minutes

2.  **Debt type**: C++ Technical Debt (Post-Refactoring)
    **Quote**: `FileIndexStorage.h`
    **Risk explanation**: The `StringPool` class in `FileIndexStorage.h` uses `ToLower` when interning strings. This is a good optimization, but it's not immediately obvious from the class name or the `Intern` method that the strings are being lowercased.
    **Suggested fix**: Consider renaming the `Intern` method to `InternAndLower` to make the behavior more explicit.
    **Severity**: Low
    **Effort**: < 15 minutes

## Quick Wins
1.  **Update `BUILDING_ON_LINUX.md`**: Add the missing dependencies to the documentation. (Effort: 30 minutes)
2.  **Fix naming convention for `GetUserHomeRelativePath`**: Rename the function to `GetUserHomeRelativePath` and mark it as `static`. (Effort: < 15 minutes)
3.  **Clarify `StringPool::Intern` behavior**: Rename the `Intern` method to `InternAndLower`. (Effort: < 15 minutes)

## Recommended Actions
1.  **Investigate and fix `ParallelSearchEngineTests`**: This is the most critical issue and should be addressed immediately.
2.  **Update Linux build documentation**: A complete and accurate build guide is essential for developer productivity.
3.  **Modernize string usage**: Systematically replace `const std::string&` with `std::string_view` where appropriate to improve performance and code quality.
