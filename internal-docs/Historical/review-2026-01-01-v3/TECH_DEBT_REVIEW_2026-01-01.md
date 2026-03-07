# Tech Debt Review - 2026-01-01

## Executive Summary
- **Health Score**: 6/10
- **Critical Issues**: 0
- **High Issues**: 2
- **Total Findings**: 5
- **Estimated Remediation Effort**: 4-6 hours

## Findings

### High
1.  **Massive Class with Multiple Responsibilities (`FileIndex`)**
    *   **Code**: `FileIndex.h`
    *   **Debt Type**: Maintainability Issues (God Class)
    *   **Risk Explanation**: The `FileIndex` class has over 50 methods and coordinates storage, searching, path management, lazy loading, and thread pool management. This makes the class difficult to understand, test, and maintain. A change in one area of responsibility has a high risk of unintentionally affecting another.
    *   **Suggested Fix**: Decompose `FileIndex` into smaller, more focused components. For example, the search orchestration logic could be moved into a dedicated `SearchManager` class, leaving `FileIndex` to focus purely on data indexing and retrieval. This is a larger refactoring and should be planned carefully.
    *   **Severity**: High
    *   **Effort**: 3-5 hours (major refactoring)

2.  **Inconsistent Naming Conventions**
    *   **Code**: `FileIndex.h`, lines 121, 137, 143, etc.
        ```cpp
        void Insert(uint64_t id, uint64_t parentID, const std::string &name,
                      bool isDirectory = false,
                      FILETIME modificationTime = kFileTimeNotLoaded);
        bool Rename(uint64_t id, const std::string &newName);
        bool Move(uint64_t id, uint64_t newParentID);
        ```
    *   **Debt Type**: Naming Convention Violations
    *   **Risk Explanation**: The codebase is supposed to follow a `snake_case` convention for parameters, but `PascalCase` and `camelCase` are used in many public methods (`parentID`, `modificationTime`, `newName`, `newParentID`). This inconsistency makes the code harder to read and violates the project's established coding standards.
    *   **Suggested Fix**: Rename all parameters across the codebase to adhere to the `snake_case` convention.
        ```cpp
        void Insert(uint64_t id, uint64_t parent_id, const std::string &name,
                      bool is_directory = false,
                      FILETIME modification_time = kFileTimeNotLoaded);
        bool Rename(uint64_t id, const std::string &new_name);
        bool Move(uint64_t id, uint64_t new_parent_id);
        ```
    *   **Severity**: High
    *   **Effort**: 1-2 hours (spread across many files)

### Medium
1.  **Missing `[[nodiscard]]` Attribute**
    *   **Code**: `FileIndex.h`, lines 136, 142, 148, etc.
        ```cpp
        bool Rename(uint64_t id, const std::string &newName);
        bool Move(uint64_t id, uint64_t newParentID);
        bool Exists(uint64_t id) const;
        ```
    *   **Debt Type**: C++17 Modernization Opportunities
    *   **Risk Explanation**: Functions that return a status (like `bool` for success/failure) or a value that should not be ignored are missing the `[[nodiscard]]` attribute. This can lead to silent failures where a developer calls the function but forgets to check the return value, potentially leading to bugs.
    *   **Suggested Fix**: Add `[[nodiscard]]` to all functions whose return value should not be ignored.
        ```cpp
        [[nodiscard]] bool Rename(uint64_t id, const std::string &new_name);
        [[nodiscard]] bool Move(uint64_t id, uint64_t new_parent_id);
        [[nodiscard]] bool Exists(uint64_t id) const;
        ```
    *   **Severity**: Medium
    *   **Effort**: 30 minutes

### Low
1.  **Unused Forward Declarations**
    *   **Code**: `FileIndex.h`, lines 45-49
        ```cpp
        class SearchThreadPool;
        class StaticChunkingStrategy;
        class HybridStrategy;
        class DynamicStrategy;
        class InterleavedChunkingStrategy;
        ```
    *   **Debt Type**: Dead/Unused Code
    *   **Risk Explanation**: The forward declarations for the various load balancing strategy classes are not used within the `FileIndex.h` header file. While they may be used in the corresponding `.cpp` file, they clutter the header and can be misleading.
    *   **Suggested Fix**: Move these forward declarations to `FileIndex.cpp` where they are actually needed.
    *   **Severity**: Low
    *   **Effort**: < 15 minutes

2.  **Potentially Dangerous `GetAllEntries` Method**
    *   **Code**: `FileIndex.h`, line 280
        ```cpp
        std::vector<std::tuple<uint64_t, std::string, bool, uint64_t>>
        GetAllEntries() const;
        ```
    *   **Debt Type**: Memory & Performance Debt
    *   **Risk Explanation**: This method iterates over every single entry in the index and allocates a `std::string` for each one, returning them in a large vector. For an index with millions of files, this could lead to a massive memory allocation and potentially crash the application.
    *   **Suggested Fix**: Mark this method as deprecated with a comment explaining the risk. Provide an alternative, safer method that allows for paginated or callback-based iteration, such as the existing `ForEachEntry` method.
    *   **Severity**: Low (assuming it's not used in a critical path)
    *   **Effort**: < 15 minutes (to add a comment)

## Summary

- **Debt Ratio Estimate**: Approximately 15-20% of the codebase is affected by the issues identified, primarily the inconsistent naming conventions and the large `FileIndex` class.
- **Top 3 Quick Wins**:
    1.  Add `[[nodiscard]]` to important functions. (Effort: ~30 min)
    2.  Move unused forward declarations from `FileIndex.h` to `FileIndex.cpp`. (Effort: < 15 min)
    3.  Add a warning comment to the `GetAllEntries` method. (Effort: < 15 min)
- **Top Critical/High Items**:
    1.  **Inconsistent Naming Conventions**: This should be addressed to improve code readability and maintainability across the project.
    2.  **`FileIndex` God Class**: A plan should be made to refactor this class to better adhere to the Single Responsibility Principle.
- **Systematic Patterns**:
    *   The `PascalCase` and `camelCase` parameter naming is a systematic issue found in many of the core classes.
    *   The lack of `[[nodiscard]]` is also a widespread issue.
