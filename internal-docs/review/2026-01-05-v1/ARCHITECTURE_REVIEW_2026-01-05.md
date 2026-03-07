# Architecture Review - 2026-01-05

## Executive Summary
- **Architecture Health Score**: 5/10
- **Top 3 Systemic Issues**:
    1.  Widespread violations of the Single Responsibility Principle.
    2.  Tight coupling between UI and core logic.
    3.  Inconsistent or missing abstractions for platform-specific code.
- **Recommended Refactorings**: Prioritize decoupling the `FileIndex` class, introducing a dedicated UI model, and creating a more robust platform abstraction layer.
- **Threading Model Assessment**: The current threading model is functional but fragile. It relies on a few "god" objects to manage concurrency, which increases the risk of deadlocks and makes the system hard to reason about.

## Findings

### 1. SOLID Principle Violations

-   **Location:** `src/index/FileIndex.h`
-   **Smell Type:** Single Responsibility Principle (SRP) Violation
-   **Impact:** The `FileIndex` class is a "god class" that manages data storage, indexing, searching, thread pooling, and more. This makes it extremely difficult to modify, test, or understand. A change to the search algorithm could inadvertently break the indexing logic.
-   **Refactoring Suggestion:** Decompose `FileIndex` into smaller, more focused classes:
    -   `FileIndexStorage`: Manages the raw file data.
    -   `SearchController`: Orchestrates search operations.
    -   `IndexBuilder`: Handles the creation of the index.
-   **Severity:** Critical
-   **Effort:** L (> 4hrs)

-   **Location:** `src/core/Application.cpp`
-   **Smell Type:** Dependency Inversion Principle (DIP) Violation
-   **Impact:** The `Application` class directly instantiates concrete classes like `FileIndex` and `SearchWorker`, tightly coupling it to specific implementations. This makes it difficult to substitute different implementations for testing or to introduce new functionality without modifying the core application class.
-   **Refactoring Suggestion:** Use dependency injection. The `Application` class should receive interfaces (e.g., `ISearchableIndex`) in its constructor, rather than creating concrete types itself.
-   **Severity:** High
-   **Effort:** M (1-4hrs)

### 2. Threading & Concurrency Smells

-   **Location:** `src/search/SearchWorker.cpp`
-   **Smell Type:** Lock Granularity Issues
-   **Impact:** The main search loop in `SearchWorker` holds a lock for the entire duration of the search, including potentially expensive post-processing steps. This can block other threads from accessing the data for longer than necessary, reducing parallelism.
-   **Refactoring Suggestion:** Reduce the lock scope to only the critical sections where data is being actively read or written. Release the lock before performing expensive, non-data-related computations.
-   **Severity:** High
-   **Effort:** M (1-4hrs)

-   **Location:** `src/index/LazyAttributeLoader.cpp`
-   **Smell Type:** Missing Thread Coordination
-   **Impact:** The `LazyAttributeLoader` uses `std::async` to fetch file attributes in the background but lacks a clear mechanism to cancel or manage the lifecycle of these asynchronous tasks. If a search is cancelled, these tasks may continue running, wasting resources.
-   **Refactoring Suggestion:** Introduce a cancellation token (e.g., a shared `std::atomic_bool`) that can be checked by the async tasks to allow for early termination.
-   **Severity:** Medium
-   **Effort:** M (1-4hrs)

### 3. Coupling & Cohesion Smells

-   **Location:** `src/ui/ResultsTable.cpp`
-   **Smell Type:** Tight Coupling (Feature Envy)
-   **Impact:** The `ResultsTable` UI component directly accesses and manipulates the internal state of the `FileIndex` and `GuiState` classes. This creates a tight coupling between the UI and the core logic, making it difficult to change one without affecting the other.
-   **Refactoring Suggestion:** Introduce a ViewModel or Presenter layer to mediate between the UI and the core logic. The `ResultsTable` should only interact with the ViewModel, which would provide it with the data it needs in a display-friendly format.
-   **Severity:** High
-   **Effort:** L (> 4hrs)

-   **Location:** `src/utils/`
-   **Smell Type:** Low Cohesion
-   **Impact:** The `utils` directory contains a mix of unrelated utility classes, from string manipulation to threading. This makes it hard to find specific functionality and increases the cognitive overhead for new developers.
-   **Refactoring Suggestion:** Reorganize the `utils` directory into more cohesive subdirectories based on functionality (e.g., `utils/string`, `utils/threading`, `utils/platform`).
-   **Severity:** Low
-   **Effort:** S (< 1hr)

### 4. Abstraction Failures

-   **Location:** `src/platform/`
-   **Smell Type:** Leaky Abstractions
-   **Impact:** The platform-specific code in the `platform` directory is not well-abstracted. For example, Windows-specific types like `HANDLE` are used directly in some of the cross-platform headers, forcing other platforms to have stub definitions.
-   **Refactoring Suggestion:** Create a more robust platform abstraction layer. Define platform-agnostic interfaces for file operations, threading, and other system-level functionality, with separate implementations for each platform.
-   **Severity:** High
-   **Effort:** L (> 4hrs)

### 5. Testability Smells

-   **Location:** `src/core/Application.cpp`
-   **Smell Type:** Hard to Unit Test
-   **Impact:** The `Application` class is difficult to unit test because it instantiates its own dependencies and manages the main application loop.
-   **Refactoring Suggestion:** By applying Dependency Injection as mentioned earlier, the `Application`'s dependencies could be mocked, making it much easier to test in isolation.
-   **Severity:** Medium
-   **Effort:** M (1-4hrs)
