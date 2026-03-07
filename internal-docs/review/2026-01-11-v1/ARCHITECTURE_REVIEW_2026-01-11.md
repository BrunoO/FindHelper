# Architecture Review - 2026-01-11

## Executive Summary
- **Health Score**: 4/10
- **Critical Issues**: 1
- **High Issues**: 1
- **Total Findings**: 2
- **Estimated Remediation Effort**: 16 hours

## Findings

### Critical
- **God Class (`FileIndex`)**
  - **Location**: `src/index/FileIndex.h`, `src/index/FileIndex.cpp`
  - **Smell Type**: SOLID Principle Violations (Single Responsibility Principle)
  - **Impact**: The `FileIndex` class has too many responsibilities, including data storage, index manipulation (CRUD operations), path management, parallel search orchestration, lazy attribute loading, and index maintenance. This makes the class extremely difficult to understand, modify, and test. A change in any one of these areas requires modifying this monolithic class, increasing the risk of introducing bugs.
  - **Refactoring Suggestion**:
    - **Extract a `SearchService` class**: This class would be responsible for orchestrating searches, taking a `FileIndex` as a dependency. It would contain the `SearchAsync` and `SearchAsyncWithData` methods.
    - **Extract a `PathService` class**: This class would handle all path-related operations, such as building, storing, and retrieving paths.
    - **Consolidate maintenance logic**: The maintenance logic, currently split between `FileIndex` and `FileIndexMaintenance`, should be fully encapsulated within `FileIndexMaintenance`.
    - **Refactor `FileIndex` to be a pure data container**: The `FileIndex` class should be simplified to only manage the core data structures (`FileIndexStorage`) and provide basic, thread-safe access to them.
  - **Severity**: Critical
  - **Effort**: Large (> 8 hours)

### High
- **High Coupling (`FileIndex`)**
  - **Location**: `src/index/FileIndex.h`
  - **Smell Type**: Coupling & Cohesion Smells
  - **Impact**: `FileIndex` is tightly coupled to a large number of other classes, including `FileIndexStorage`, `PathStorage`, `PathOperations`, `IndexOperations`, `FileIndexMaintenance`, `DirectoryResolver`, `LazyAttributeLoader`, `ParallelSearchEngine`, `SearchThreadPool`, and `AppSettings`. This high coupling means that changes in any of these dependent classes are likely to ripple through `FileIndex`, and vice versa. It also makes the class difficult to test in isolation.
  - **Refactoring Suggestion**: Apply the Dependency Inversion Principle. Instead of `FileIndex` instantiating its dependencies directly, inject interfaces for these dependencies into its constructor. This will decouple `FileIndex` from the concrete implementations and improve testability.
  - **Severity**: High
  - **Effort**: Medium (4-8 hours)

## Architecture Health Score: 4/10
The core of the application's indexing and search functionality is concentrated in the `FileIndex` "god class," which represents a significant architectural liability. While the use of a Structure of Arrays (SoA) for path storage is a deliberate and effective performance optimization, the lack of clear separation of concerns around it makes the system brittle and difficult to maintain.

## Top 3 Systemic Issues
1.  **"God Class" Pattern**: The `FileIndex` class is the most prominent example, but other classes may also be taking on too many responsibilities.
2.  **Lack of Dependency Injection**: Classes often create their own dependencies, leading to tight coupling and poor testability.
3.  **Low Cohesion**: Functionality is not always grouped logically, leading to classes that are difficult to understand and reuse.

## Recommended Refactorings
1.  **Decompose `FileIndex`**: This is the highest priority. Breaking this class down into smaller, single-responsibility classes will have the greatest impact on the long-term health of the codebase.
2.  **Introduce Dependency Injection**: Systematically refactor classes to receive their dependencies via constructor injection. This will improve testability and reduce coupling.
