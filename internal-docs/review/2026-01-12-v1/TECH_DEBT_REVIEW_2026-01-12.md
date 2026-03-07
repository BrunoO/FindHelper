# Tech Debt Review - 2026-01-12

## Executive Summary
- **Health Score**: 6/10
- **Critical Issues**: 1
- **High Issues**: 4
- **Total Findings**: 10
- **Estimated Remediation Effort**: 12-16 hours

## Findings

### Critical
1.  **Debt Type**: Maintainability Issues / God Class
    - **Location**: `src/index/FileIndex.h`, `src/index/FileIndex.cpp`
    - **Risk Explanation**: The `FileIndex` class has grown to over 1000 lines and violates the Single Responsibility Principle. It manages data storage (`PathStorage`, `FileIndexStorage`), searching (`ParallelSearchEngine`), and index maintenance. This high coupling makes the class difficult to understand, test, and maintain. A change in one area (e.g., search logic) has a high risk of unintentionally affecting another (e.g., index construction).
    - **Suggested Fix**: Refactor `FileIndex` by delegating responsibilities to more focused classes. For example, create a `SearchOrchestrator` to handle the search logic and a `MaintenanceScheduler` for background updates, leaving `FileIndex` to act primarily as a data container and coordinator.
    - **Severity**: Critical
    - **Effort**: 8-10 hours

### High
1.  **Debt Type**: Aggressive DRY / Code Deduplication
    - **Location**: `tests/FileIndexSearchStrategyTests.cpp`, `tests/ParallelSearchEngineTests.cpp`, `tests/LazyAttributeLoaderTests.cpp`, and others.
    - **Risk Explanation**: Multiple test files contain significant boilerplate for setting up a `FileIndex` instance with mock data. This duplicated code increases maintenance overhead; a change to the `FileIndex` constructor or test setup requires updating many files, risking inconsistencies.
    - **Suggested Fix**: Create a dedicated test helper or fixture (e.g., `TestIndexBuilder` or `FileIndexTestFixture`) in the `tests/` directory. This helper would encapsulate the logic for creating and populating a `FileIndex` for testing purposes.
      ```cpp
      // In tests/TestHelpers.h
      class FileIndexTestFixture {
      public:
          FileIndexTestFixture();
          void AddFile(const std::string& path, uint64_t size, uint64_t timestamp);
          FileIndex& GetIndex();
      private:
          // ... internal state ...
      };
      ```
    - **Severity**: High
    - **Effort**: 2-3 hours

2.  **Debt Type**: C++17 Modernization / Memory & Performance Debt
    - **Location**: Multiple files, e.g., `src/path/PathPatternMatcher.cpp`
    - **Risk Explanation**: Many functions accept `const std::string&` for parameters that are only used for reading and do not require a null-terminated string. This forces unnecessary heap allocations when string literals or character buffers are passed, impacting performance in hot paths.
    - **Suggested Fix**: Replace `const std::string&` with `std::string_view` for read-only string parameters where applicable.
      ```cpp
      // Before
      bool Matches(const std::string& path) const;

      // After
      bool Matches(std::string_view path) const;
      ```
    - **Severity**: High
    - **Effort**: 1-2 hours

3.  **Debt Type**: Maintainability Issues / God Class
    - **Location**: `src/core/Application.cpp`
    - **Risk Explanation**: The `Application` class is another God Class that handles the main loop, UI rendering orchestration, and application logic. This makes it difficult to test UI components in isolation and intertwines business logic with presentation logic.
    - **Suggested Fix**: Separate the UI rendering from the application logic. The `Application` class should manage the main loop and window, while a separate `UIManager` or `UIRenderer` class handles all ImGui calls. Application state should be passed to the UI layer for rendering.
    - **Severity**: High
    - **Effort**: 3-4 hours (initial separation)

4.  **Debt Type**: Naming Convention Violations
    - **Location**: Codebase-wide.
    - **Risk Explanation**: There are inconsistencies in applying the project's naming conventions. Some member variables are missing the trailing underscore (e.g., `isOpen` instead of `is_open_`), and some local variables use `PascalCase`. This makes the code harder to read and understand at a glance.
    - **Suggested Fix**: Perform a codebase-wide search and replace to enforce the naming conventions. For example, find member variables in class definitions that do not end with `_` and correct them.
    - **Severity**: High
    - **Effort**: 1 hour

### Medium
1.  **Debt Type**: Dead/Unused Code
    - **Location**: `src/utils/ClipboardUtils.cpp`
    - **Risk Explanation**: The file contains a commented-out implementation for `GetClipboardText`. This dead code clutters the codebase and can be confusing for new developers.
    - **Suggested Fix**: Remove the commented-out code block. If the implementation is experimental, it should be on a separate feature branch, not commented out on the main branch.
    - **Severity**: Medium
    - **Effort**: < 15 minutes

2.  **Debt Type**: C++ Technical Debt / Inconsistent Ownership
    - **Location**: `src/ui/FolderBrowser.h`
    - **Risk Explanation**: The `FolderBrowser` class returns a raw pointer (`const char*`) for the selected path. The ownership and lifetime of this pointer are unclear. It likely points to an internal buffer, which is a fragile design.
    - **Suggested Fix**: Change the return type to `std::string` or `std::optional<std::string>` to make ownership clear and avoid dangling pointers.
      ```cpp
      // Before
      const char* GetSelectedPath() const;

      // After
      std::optional<std::string> GetSelectedPath() const;
      ```
    - **Severity**: Medium
    - **Effort**: < 30 minutes

3.  **Debt Type**: Aggressive DRY / Code Deduplication
    - **Location**: `src/platform/windows/AppBootstrap_win.cpp`, `src/platform/macos/AppBootstrap_mac.mm`, `src/platform/linux/AppBootstrap_linux.cpp`
    - **Risk Explanation**: The platform-specific bootstrap files share common logic for parsing command-line arguments and initializing the logger. This duplication means that a change to argument parsing needs to be replicated in three places.
    - **Suggested Fix**: Extract the common logic into a shared `main_common.h` or `AppBootstrap_common.cpp` that is called by each platform's entry point.
    - **Severity**: Medium
    - **Effort**: 1 hour

### Low
1.  **Debt Type**: C++17 Modernization Opportunities
    - **Location**: `src/core/Application.cpp`
    - **Risk Explanation**: The `Initialize` method returns a boolean to indicate success or failure, but the call site does not check the return value. This could mask initialization failures.
    - **Suggested Fix**: Add the `[[nodiscard]]` attribute to the `Initialize` function declaration to ensure the compiler issues a warning if the return value is ignored.
      ```cpp
      // In Application.h
      [[nodiscard]] bool Initialize();
      ```
    - **Severity**: Low
    - **Effort**: < 15 minutes

2.  **Debt Type**: Platform-Specific Debt
    - **Location**: `src/platform/windows/DirectXManager.cpp`
    - **Risk Explanation**: The DirectX manager uses raw `ID3D11...` pointers without smart pointer wrappers. While DirectX uses COM and manages its own reference counting, using a library like `wrl::ComPtr` would provide safer, RAII-style resource management.
    - **Suggested Fix**: Wrap the raw COM pointers in `wrl::ComPtr` to automate `AddRef` and `Release` calls.
    - **Severity**: Low
    - **Effort**: 1 hour

## Summary

-   **Debt Ratio Estimate**: Approximately 20-25% of the core logic is affected by significant technical debt, primarily concentrated in the `FileIndex` and `Application` god classes and duplicated test setup code.
-   **Top 3 Quick Wins**:
    1.  **Fix Naming Conventions**: A quick, automated search-and-replace can significantly improve readability.
    2.  **Add `[[nodiscard]]` to `Application::Initialize`**: A one-line change that prevents a potential class of bugs.
    3.  **Remove commented-out code in `ClipboardUtils.cpp`**: Trivial to remove and improves code cleanliness.
-   **Top Critical/High Items**:
    1.  **Refactor `FileIndex` God Class**: This is the most critical issue and will have the largest positive impact on maintainability.
    2.  **Refactor `Application` God Class**: The second most important refactoring to separate concerns.
    3.  **Create Test Fixtures**: Will significantly improve the maintainability of the test suite.
    4.  **Modernize string parameters to `std::string_view`**: A high-impact performance improvement.
-   **Systematic Patterns**:
    -   **God Classes**: A clear pattern of concentrating too much responsibility in single classes (`FileIndex`, `Application`).
    -   **Duplicated Test Setup**: Nearly every integration-style test repeats the same boilerplate to construct a `FileIndex`.
    -   **Inconsistent Naming**: Member variables, in particular, are inconsistently named across the project.