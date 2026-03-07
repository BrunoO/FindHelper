# Architecture Review - 2025-12-31

## Executive Summary
- **Architecture Health Score**: 6/10
- **Systemic Issues**: 3
- **Recommended Refactorings**: 4
- **Threading Model Assessment**: The threading model is generally sound but has areas for improvement, particularly in lock granularity and the prevention of long-running operations on the UI thread.

## Findings

### 1. SOLID Principle Violations
-   **Location**: `FileIndex.h`
-   **Smell Type**: Single Responsibility Principle (SRP) Violation
-   **Impact**: The `FileIndex` class has too many responsibilities, including data storage, path manipulation, search orchestration, thread pool management, and lazy attribute loading. This makes the class difficult to understand, maintain, and test.
-   **Refactoring Suggestion**:
    -   Extract the thread pool management into a separate `SearchThreadPoolManager` class.
    -   Move the lazy attribute loading logic into a dedicated `AttributeLoader` class.
    -   Consider separating the search orchestration logic from the data storage, so `FileIndex` is purely a data container.
-   **Severity**: High
-   **Effort**: L

### 2. Threading & Concurrency Smells
-   **Location**: `FileIndex.h`
-   **Smell Type**: Lock Granularity Issues
-   **Impact**: The `FileIndex` class uses a single `shared_mutex` to protect all its data. While this simplifies the locking logic, it can lead to contention when different parts of the data are accessed concurrently. For example, a search operation (which needs a shared lock) can be blocked by a simple rename operation (which needs a unique lock), even if the two operations are unrelated.
-   **Refactoring Suggestion**:
    -   Use separate mutexes for different parts of the `FileIndex` data. For example, one mutex for the main index, another for the path storage, and another for the directory cache. This would allow for more granular locking and reduce contention.
-   **Severity**: Medium
-   **Effort**: M

### 3. Design Pattern Misuse
-   **Location**: `FileIndex.h`, `ParallelSearchEngine.cpp`
-   **Smell Type**: Tight Coupling
-   **Impact**: `ParallelSearchEngine` is tightly coupled to `FileIndex`. It takes an `ISearchableIndex` in its `SearchAsync` methods, but the implementation still relies on `FileIndex`-specific details, such as the `FileIndex::SearchStats` and `FileIndex::ThreadTiming` structs. This makes it difficult to test `ParallelSearchEngine` in isolation and to reuse it with other index implementations.
-   **Refactoring Suggestion**:
    -   Define the `SearchStats` and `ThreadTiming` structs in a neutral, shared location.
    -   Ensure that `ParallelSearchEngine` only relies on the `ISearchableIndex` interface and does not have any knowledge of `FileIndex`.
-   **Severity**: Medium
-   **Effort**: M

### 4. Testability Smells
-   **Location**: `ParallelSearchEngineTests.cpp`
-   **Smell Type**: Hard to Unit Test
-   **Impact**: The `ParallelSearchEngineTests` suite is failing, which suggests that the tests are either brittle or that there are genuine bugs in the code. The tight coupling between `ParallelSearchEngine` and `FileIndex` makes it difficult to write reliable unit tests.
-   **Refactoring Suggestion**:
    -   Create a mock implementation of `ISearchableIndex` that can be used to test `ParallelSearchEngine` in isolation.
    -   The tests should be reviewed to ensure that they are not dependent on the specific timing of threads or the state of the filesystem.
-   **Severity**: High
-   **Effort**: M

## Top 3 Systemic Issues
1.  **Over-reliance on `FileIndex`**: The `FileIndex` class is a "god class" that has too many responsibilities. This is a recurring theme throughout the codebase.
2.  **Tight Coupling**: Many classes are tightly coupled to `FileIndex`, which makes the code difficult to test and maintain.
3.  **Incomplete Abstractions**: The `ISearchableIndex` interface is a good start, but it's not fully utilized, as `ParallelSearchEngine` still has knowledge of `FileIndex`.

## Recommended Refactorings
1.  **Refactor `FileIndex`**: Break down the `FileIndex` class into smaller, more focused classes, each with a single responsibility.
2.  **Decouple `ParallelSearchEngine`**: Make `ParallelSearchEngine` completely independent of `FileIndex` by having it rely solely on the `ISearchableIndex` interface.
3.  **Improve Testability**: Create mock implementations of interfaces to allow for more reliable and isolated unit tests.
4.  **Refine Locking Strategy**: Use more granular locking in `FileIndex` to reduce contention and improve concurrency.
