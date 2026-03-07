# Architecture Review - 2026-01-13-v3

## Executive Summary
- **Health Score**: 5/10
- **Critical Issues**: 1
- **High Issues**: 3
- **Total Findings**: 4
- **Estimated Remediation Effort**: 16 hours

## Findings

### Critical
- **Violation of Single Responsibility Principle in `Application`**
  - **Location**: `src/core/Application.cpp`
  - **Smell Type**: SOLID Principle Violation (SRP)
  - **Impact**: The `Application` class is a "God Class" that handles everything from the main loop to UI rendering and application logic. This makes it extremely difficult to test, debug, and maintain. Changes in one area of functionality have a high risk of breaking others.
  - **Refactoring Suggestion**: Decompose the `Application` class into smaller, more focused classes:
    - `UIRenderer`: Responsible for all ImGui rendering.
    - `ApplicationLogic`: Handles the core application logic.
    - `MainLoop`: Manages the main application loop.
  - **Severity**: Critical
  - **Effort**: L (8 hours)

### High
- **Violation of Single Responsibility Principle in `FileIndex`**
  - **Location**: `src/index/FileIndex.h`, `src/index/FileIndex.cpp`
  - **Smell Type**: SOLID Principle Violation (SRP)
  - **Impact**: The `FileIndex` class is responsible for too many things: data storage, searching, and index maintenance. This tight coupling makes it difficult to change one aspect without affecting the others.
  - **Refactoring Suggestion**: Split the class into:
    - `FileIndexStorage`: Manages the in-memory storage of file data.
    - `SearchService`: Implements the search functionality.
    - `IndexMaintenance`: Handles adding, removing, and updating index entries.
  - **Severity**: High
  - **Effort**: M (4 hours)

- **Lack of Abstraction for Platform-Specific Code**
  - **Location**: `src/platform/win/`, `src/platform/mac/`, `src/platform/linux/`
  - **Smell Type**: Abstraction Failure
  - **Impact**: There is significant code duplication across the platform-specific implementations. This makes it difficult to maintain and introduces the risk of inconsistencies between platforms.
  - **Refactoring Suggestion**: Introduce platform-agnostic interfaces for file system operations, threading, and other platform-specific functionality. Use dependency injection to provide the correct implementation at runtime.
  - **Severity**: High
  - **Effort**: M (4 hours)

- **Inconsistent Threading Model**
  - **Location**: Throughout the codebase, especially in `SearchWorker.cpp` and `UsnMonitor.cpp`.
  - **Smell Type**: Threading & Concurrency Smell
  - **Impact**: The threading model is inconsistent and difficult to reason about. There are a mix of raw threads, `std::thread`, and a custom thread pool. This increases the risk of race conditions, deadlocks, and other concurrency-related bugs.
  - **Refactoring Suggestion**: Standardize on a single threading model. A well-managed thread pool would be a good choice for this application.
  - **Severity**: High
  - **Effort**: L (8 hours)

## Architecture Health Score: 5/10

The architecture suffers from several significant issues, primarily related to violations of the Single Responsibility Principle and a lack of clear abstraction. The "God Class" problem is prevalent, and the inconsistent threading model is a major risk.

## Top 3 Systemic Issues
1.  **God Classes**: The `Application` and `FileIndex` classes are prime examples of this anti-pattern.
2.  **Code Duplication**: There is a significant amount of duplicated code across the platform-specific implementations.
3.  **Inconsistent Concurrency**: The lack of a standardized threading model makes the code difficult to reason about and prone to bugs.

## Recommended Refactorings
1.  **Decompose `Application`**: This is the highest priority, as it will have the largest impact on maintainability.
2.  **Standardize the Threading Model**: This will improve the stability and predictability of the application.
3.  **Refactor `FileIndex`**: This will make the core indexing and search functionality easier to maintain and test.
4.  **Introduce Platform Abstractions**: This will reduce code duplication and make it easier to add support for new platforms in the future.

## Testing Improvement Areas
- **Mocking**: The lack of abstractions makes it difficult to mock dependencies for unit testing. Introducing interfaces for platform-specific code and other services would significantly improve testability.

## Threading Model Assessment
The current threading model is not sustainable at scale. The mix of different threading approaches is a major source of complexity and risk. A standardized model, such as a central thread pool, would be a significant improvement.
