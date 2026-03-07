# Architecture Review - 2026-01-01

## Architecture Health Score: 5/10

The application's architecture is functional but exhibits several significant design smells that increase complexity and reduce maintainability. The core issue is a violation of the Single Responsibility Principle, leading to a "God Class" in the form of `FileIndex`. While the use of platform-specific implementations and a multi-threaded search design are strengths, the tight coupling and low cohesion in core components are major weaknesses.

## Top 3 Systemic Issues

1.  **`FileIndex` God Class**: The `FileIndex` class is a classic example of a God Class, violating the Single Responsibility Principle. It is responsible for data storage, indexing, path manipulation, search orchestration, lazy attribute loading, and thread pool management. This makes the class extremely difficult to test, maintain, and reason about.
2.  **Tight Coupling Between Components**: There is tight coupling between `FileIndex`, `ParallelSearchEngine`, and the various load balancing strategies. Although an `ISearchableIndex` interface exists, the `ParallelSearchEngine` still has a strong dependency on `FileIndex`'s specific data structures, such as `FileIndex::SearchResultData` and `FileIndex::ThreadTiming`.
3.  **Inconsistent Abstraction Levels**: The codebase mixes high-level abstractions with low-level implementation details. For example, `FileIndex` exposes low-level details about its internal storage (like `GetPathViewLocked`) while also managing high-level concepts like search orchestration.

## Findings

### 1. SOLID Principle Violations

*   **Location**: `FileIndex.h`
*   **Smell Type**: Single Responsibility Principle (SRP) Violation
*   **Impact**: The `FileIndex` class is responsible for too many unrelated tasks, including data storage, search, path management, and concurrency. This makes the class brittle and difficult to modify without introducing unintended side effects.
*   **Refactoring Suggestion**: Decompose `FileIndex` into smaller, more focused classes. For example:
    *   A `SearchOrchestrator` class could handle the logic of `SearchAsync` and `SearchAsyncWithData`.
    *   A `PathManager` class could encapsulate all path-related operations.
    *   `FileIndex` itself could be slimmed down to focus solely on managing the core data structures.
*   **Severity**: High
*   **Effort**: L

### 2. Threading & Concurrency Smells

*   **Location**: `FileIndex.h`
*   **Smell Type**: Lock Granularity Issues
*   **Impact**: Several methods in `FileIndex` acquire a unique lock on the main mutex (`mutex_`) for operations that could be performed with a shared lock or a more granular lock. For example, `UpdateSize` takes a unique lock for the entire duration of the method, including the I/O operation `LazyAttributeLoader::GetFileSize`, which can be slow.
*   **Refactoring Suggestion**: Review all lock acquisitions in `FileIndex`. Use shared locks for read-only operations and narrow the scope of unique locks to the smallest possible critical section. Avoid holding locks during expensive I/O operations.
*   **Severity**: Medium
*   **Effort**: M

### 3. Coupling & Cohesion Smells

*   **Location**: `ParallelSearchEngine.h`, `FileIndex.h`
*   **Smell Type**: Tight Coupling
*   **Impact**: `ParallelSearchEngine` is tightly coupled to `FileIndex`, as evidenced by its use of `FileIndex::SearchResultData` and `FileIndex::ThreadTiming`. This makes it difficult to reuse `ParallelSearchEngine` with a different index implementation and makes both classes harder to test in isolation.
*   **Refactoring Suggestion**: Define the necessary data structures (like `SearchResultData` and `ThreadTiming`) in a neutral, shared header or within the `ISearchableIndex` interface itself. This would decouple `ParallelSearchEngine` from the concrete `FileIndex` implementation.
*   **Severity**: High
*   **Effort**: M

### 4. Abstraction Failures

*   **Location**: `FileIndex.h`
*   **Smell Type**: Leaky Abstractions
*   **Impact**: `FileIndex` exposes implementation details through its public interface. For example, methods like `GetPathViewLocked` and `GetPathComponentsViewLocked` require the caller to manage the lock, which is a leaky abstraction.
*   **Refactoring Suggestion**: Encapsulate the locking mechanism within `FileIndex`. Instead of `Get...Locked` methods, provide higher-level functions that take a callback to be executed while the lock is held.
*   **Severity**: Medium
*   **Effort**: M

## Recommended Refactorings

1.  **Decompose `FileIndex` (High Priority)**: This is the most critical refactoring. A plan should be created to break down the `FileIndex` class into smaller, more manageable components.
2.  **Decouple `ParallelSearchEngine` from `FileIndex` (High Priority)**: Abstract the data structures shared between these two classes to reduce coupling and improve testability.
3.  **Refine Locking Strategy (Medium Priority)**: Audit the use of mutexes in `FileIndex` to improve concurrency and performance.

## Threading Model Assessment

The current threading model, which uses a shared thread pool for parallel searches, is a solid foundation. However, the fine-grained locking and potential for lock contention in the `FileIndex` class could become a bottleneck as the application scales. The planned refactoring to decompose `FileIndex` should also consider a more granular locking strategy to improve concurrency.
