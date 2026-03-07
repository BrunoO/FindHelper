# Architecture & Code Smells Review - 2026-01-21

## Executive Summary
- **Architecture Health Score**: 5/10
- **Systemic Issues**: God Classes, Tight Coupling, Missing Abstractions
- **Threading Model Assessment**: The current threading model is functional but fragile. It relies on a shared-nothing approach for the main search tasks, which is good, but the surrounding orchestration and state management show signs of potential race conditions and unclear ownership.

The application's architecture suffers from several significant design flaws that impede maintainability and scalability. The most pressing issue is the prevalence of "God Classes" (`FileIndex`, `Application`) that violate the Single Responsibility Principle, leading to high coupling and making the system difficult to reason about. While the core search algorithm is multi-threaded, the overall concurrency strategy is not well-documented, and shared state management is a concern.

## Findings

### Critical
- None

### High
1.  **"God Class" Violating SRP**
    - **Location:** `src/index/FileIndex.h`, `src/core/Application.cpp`
    - **Smell Type:** SOLID Principle Violations / Single Responsibility Principle (SRP)
    - **Impact:** `FileIndex` is responsible for data storage, indexing, searching, and maintenance. `Application` manages the main loop, UI rendering, and application logic. This makes both classes extremely large, complex, and difficult to test or modify without causing unintended side effects.
    - **Refactoring Suggestion:**
        - Decompose `FileIndex` into smaller, cohesive classes: `IndexStorage`, `IndexSearcher`, `IndexMaintainer`.
        - Refactor `Application` by extracting UI components into their own classes (e.g., `MainWindow`, `StatusBarController`) and moving business logic into a separate `AppLogic` class.
    - **Severity:** High
    - **Effort:** L

2.  **Tight Coupling Between UI and Core Logic**
    - **Location:** `src/ui/Popups.cpp`, `src/core/Application.cpp`
    - **Smell Type:** Coupling & Cohesion Smells / Tight Coupling
    - **Impact:** UI components, such as `Popups.cpp`, directly access and manipulate the state of core classes like `GuiState` and `Application`. This creates a rigid architecture where UI changes can break core logic, and core logic changes can break the UI. It also makes unit testing of UI components nearly impossible without instantiating the entire application.
    - **Refactoring Suggestion:** Introduce a clear boundary between the UI and the application logic. Use the Observer pattern or a dedicated `UIModel` to decouple the UI from the core. UI components should only interact with the `UIModel`, which in turn communicates with the application logic.
    - **Severity:** High
    - **Effort:** M

3.  **Missing Abstraction for Platform-Specific File Operations**
    - **Location:** `src/platform/windows/FileOperations_win.cpp`, `src/platform/linux/FileOperations_linux.cpp`
    - **Smell Type:** Abstraction Failures / Missing Abstractions
    - **Impact:** The lack of a common interface for file operations leads to code duplication and makes it difficult to add support for new platforms. The logic for handling file attributes, hidden files, etc., is repeated in each platform-specific implementation.
    - **Refactoring Suggestion:** Define a platform-agnostic `IFileOperations` interface and provide concrete implementations for each platform. Use a factory pattern to create the appropriate implementation at runtime.
    - **Severity:** High
    - **Effort:** M

### Medium
1.  **Inconsistent Ownership Semantics**
    - **Location:** Throughout the codebase, particularly in data handling classes.
    - **Smell Type:** C++ Technical Debt / Inconsistent ownership semantics
    - **Impact:** The codebase mixes raw pointers, `std::unique_ptr`, and `std::shared_ptr` without a clear and consistent ownership strategy. This makes it difficult to reason about object lifetimes and increases the risk of memory leaks or use-after-free bugs.
    - **Refactoring Suggestion:** Establish and document a clear ownership policy. Prefer `std::unique_ptr` for exclusive ownership and `std::shared_ptr` only when shared ownership is explicitly required. Refactor raw pointer usage to smart pointers where ownership is transferred or shared.
    - **Severity:** Medium
    - **Effort:** L

2.  **Hard to Unit Test due to Lack of DI**
    - **Location:** `src/core/Application.cpp`, `src/search/SearchController.cpp`
    - **Smell Type:** Testability Smells / Hard to Unit Test
    - **Impact:** Classes often create their own dependencies (e.g., `Application` creates `FileIndex`). This makes it impossible to substitute mocks or fakes for dependencies during unit testing, forcing tests to be integration tests.
    - **Refactoring Suggestion:** Apply Dependency Inversion. Inject dependencies through constructors. For example, the `Application` class should receive an `IFileIndex` interface in its constructor instead of creating a concrete `FileIndex` instance.
    - **Severity:** Medium
    - **Effort:** M

## Top 3 Systemic Issues
1.  **God Classes:** The `FileIndex` and `Application` classes are prime examples of the God Class anti-pattern, leading to a monolithic and inflexible design.
2.  **Tight Coupling:** There is a strong coupling between the UI, core logic, and data storage components, which makes the system resistant to change.
3.  **Missing Abstractions:** The codebase lacks clear abstractions for platform-specific functionality and cross-cutting concerns like logging and error handling.

## Recommended Refactorings
1.  **Decompose `FileIndex` and `Application` (Effort: L, Impact: High):** This is the highest priority refactoring. Breaking down these God Classes will significantly improve maintainability and testability.
2.  **Introduce `IFileOperations` Interface (Effort: M, Impact: High):** This will reduce code duplication and make the file system interactions more robust and extensible.
3.  **Apply Dependency Injection (Effort: M, Impact: Medium):** Start by injecting dependencies into the `Application` and `SearchController` classes to improve their testability.
