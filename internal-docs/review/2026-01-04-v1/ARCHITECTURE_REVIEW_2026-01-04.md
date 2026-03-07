# Architecture Review - 2026-01-04

## Executive Summary
- **Architecture Health Score**: 5/10
- **Systemic Issues**: 3
- **Recommended Refactorings**: 5
- **Testing Improvement Areas**: 3

## Findings

### SOLID Principle Violations
1.  **`FileIndex` - Single Responsibility Principle Violation**
    - **Location**: `FileIndex.h`, `FileIndex.cpp`
    - **Smell Type**: SRP Violation
    - **Impact**: The `FileIndex` class is a "God Class" that manages indexing, searching, data storage, and thread coordination. This makes it extremely difficult to understand, test, and maintain. Changes to one area of functionality have a high risk of impacting others.
    - **Refactoring Suggestion**: Decompose `FileIndex` into smaller, cohesive classes such as `Indexer`, `Searcher`, `IndexStorage`, and `SearchThreadPool`.
    - **Severity**: Critical
    - **Effort**: L

2.  **`SearchPatternUtils` - Open/Closed Principle Violation**
    - **Location**: `SearchPatternUtils.h`
    - **Smell Type**: OCP Violation
    - **Impact**: The `CreateFilenameMatcher` function uses a series of `if-else` statements to determine which search strategy to use. Adding a new search strategy requires modifying this function, which violates the OCP.
    - **Refactoring Suggestion**: Implement the Strategy pattern. Create a `SearchStrategy` interface and a factory that can register and create different search strategies at runtime.
    - **Severity**: High
    - **Effort**: M

3.  **`SearchWorker` - Dependency Inversion Principle Violation**
    - **Location**: `SearchWorker.cpp`
    - **Smell Type**: DIP Violation
    - **Impact**: `SearchWorker` creates its own instance of `FileIndex`, tightly coupling the two classes. This makes it difficult to test `SearchWorker` in isolation.
    - **Refactoring Suggestion**: Use dependency injection. Pass an interface of `FileIndex` to the `SearchWorker` constructor.
    - **Severity**: Medium
    - **Effort**: S

### Threading & Concurrency Smells
1.  **`SearchWorker` - Lock Granularity Issues**
    - **Location**: `SearchWorker.cpp`
    - **Smell Type**: Lock Granularity
    - **Impact**: The mutex in `SearchWorker` is held for the entire duration of the search, which can block other threads for a long time and reduce concurrency.
    - **Refactoring Suggestion**: Reduce the scope of the lock to only protect the shared data that is being accessed.
    - **Severity**: Medium
    - **Effort**: S

### Coupling & Cohesion Smells
1.  **UI and Application Logic - Tight Coupling**
    - **Location**: `Application.cpp`, `ApplicationLogic.h`
    - **Smell Type**: Tight Coupling
    - **Impact**: The UI code in `Application.cpp` is tightly coupled to the application logic. This makes it difficult to change the UI without affecting the application logic, and vice versa.
    - **Refactoring Suggestion**: Introduce a presentation model (e.g., MVP, MVVM) to decouple the UI from the application logic.
    - **Severity**: High
    - **Effort**: M

### Testability Smells
1.  **`ApplicationLogic` - Hard to Unit Test**
    - **Location**: `ApplicationLogic.h`, `ApplicationLogic.cpp`
    - **Smell Type**: Hard to Unit Test
    - **Impact**: `ApplicationLogic` is a large class with many dependencies, making it difficult to unit test.
    - **Refactoring Suggestion**: Decompose `ApplicationLogic` into smaller classes and use dependency injection to provide dependencies.
    - **Severity**: High
    - **Effort**: M

## Summary

-   **Architecture Health Score**: 5/10. The current architecture suffers from a few major issues, primarily the "God Class" `FileIndex` and the tight coupling between the UI and application logic. These issues make the codebase difficult to maintain and test. However, the use of platform-specific files and a clear separation of concerns in some areas are good architectural decisions.
-   **Top 3 Systemic Issues**:
    1.  **God Classes**: `FileIndex` and `ApplicationLogic` are too large and have too many responsibilities.
    2.  **Tight Coupling**: The UI and application logic are tightly coupled, and `SearchWorker` is tightly coupled to `FileIndex`.
    3.  **Lack of Dependency Injection**: Dependencies are often created within the classes that use them, making it difficult to test in isolation.
-   **Recommended Refactorings**:
    1.  Decompose `FileIndex` into smaller classes.
    2.  Introduce a presentation model to decouple the UI and application logic.
    3.  Use the Strategy pattern for search strategies.
    4.  Use dependency injection in `SearchWorker` and `ApplicationLogic`.
    5.  Reduce the lock scope in `SearchWorker`.
-   **Testing Improvement Areas**:
    1.  The `FileIndex` class needs to be refactored before it can be effectively unit tested.
    2.  The `ApplicationLogic` class needs to be refactored and its dependencies injected.
    3.  The UI needs to be decoupled from the application logic to allow for independent testing.
-   **Threading Model Assessment**: The current threading model is simple but effective for the current use case. However, as the application grows in complexity, the current model may not be sustainable. The lock granularity issue in `SearchWorker` is a sign of this. A more robust threading model, such as a task-based system, may be needed in the future.
