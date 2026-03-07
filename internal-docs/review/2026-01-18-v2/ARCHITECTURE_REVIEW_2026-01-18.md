# Architecture Review - 2026-01-18

## Executive Summary
- **Health Score**: 5/10
- **Critical Issues**: 1
- **High Issues**: 3
- **Total Findings**: 11
- **Estimated Remediation Effort**: 20-30 hours

## Findings

### Critical
1.  **High-Level Modules Depend on Low-Level Implementations**
    *   **Location**: `src/core/Application.cpp`
    *   **Smell Type**: DIP Violation
    *   **Impact**: `Application` directly instantiates and depends on concrete classes like `FileIndex`, `SearchWorker`, and platform-specific `RendererInterface` implementations. This tight coupling makes it very difficult to unit test `Application` in isolation and prevents swapping out implementations (e.g., for a different search engine or UI toolkit) without modifying the core application logic.
    *   **Refactoring Suggestion**: Introduce interfaces (`IFileIndex`, `ISearchWorker`, `IRenderer`) and use dependency injection in the `Application` constructor. The `main` function or a dedicated `CompositionRoot` class should be responsible for creating the concrete instances and wiring them together.
    *   **Severity**: Critical
    *   **Effort**: L (> 4hrs)

### High
1.  **"God Class" Violations (`Application`, `FileIndex`)**
    *   **Location**: `src/core/Application.cpp`, `src/index/FileIndex.cpp`
    *   **Smell Type**: SRP Violation
    *   **Impact**: Both `Application` and `FileIndex` have too many responsibilities. `Application` handles the main loop, UI, state, and index lifecycle. `FileIndex` manages storage, searching, and maintenance. This makes them brittle, hard to understand, and difficult to test. A change in one area (e.g., UI rendering) has a high risk of breaking another (e.g., index building).
    *   **Refactoring Suggestion**:
        -   **Application**: Extract responsibilities into smaller classes like `UIManager`, `PowerManager`, and `IndexController`. `Application` should only orchestrate these components.
        -   **FileIndex**: Separate the concerns of data storage, searching, and maintenance into distinct classes (`PathStore`, `SearchEngine`, `IndexMaintainer`) that are coordinated by `FileIndex`.
    *   **Severity**: High
    *   **Effort**: L (> 4hrs)

2.  **Platform-Specific Types Leaking into Platform-Agnostic Headers**
    *   **Location**: `src/core/Application.h`, `src/gui/RendererInterface.h`
    *   **Smell Type**: Leaky Abstraction / Tight Coupling
    *   **Impact**: Headers like `Application.h` include platform-specific headers (`windows.h`, `<Metal/Metal.h>`) guarded by `#ifdefs`. This leaks platform details into the core application header, increasing compile times and making the abstraction fragile. Any file that includes `Application.h` is now indirectly coupled to platform-specific details.
    *   **Refactoring Suggestion**: Use the Pimpl (Pointer to Implementation) idiom. The `Application` header should only contain a forward declaration to an implementation class (`ApplicationImpl`). The platform-specific details would then be confined to the `Application_win.cpp`, `Application_mac.mm`, etc. files.
    *   **Severity**: High
    *   **Effort**: M (1-4hrs)

3.  **Inconsistent Threading Model and Lack of Clear Contracts**
    *   **Location**: Across the codebase, especially in `Application`, `SearchWorker`, `FileIndex`.
    *   **Smell Type**: Threading & Concurrency Smells
    *   **Impact**: The threading model is complex and lacks clear documentation. It's not immediately obvious which methods are thread-safe, which thread is expected to call them, and what the lifetime of background threads is. This makes it easy to introduce race conditions or deadlocks. For example, the interaction between the main thread, the `SearchWorker` thread, and the `ThreadPool` is not clearly defined.
    *   **Refactoring Suggestion**:
        -   Document the threading contract for all public interfaces (e.g., using Doxygen comments).
        -   Establish a clear ownership model for threads.
        -   Use thread-safe data structures or explicit synchronization where shared state is accessed.
        -   Consider using a higher-level abstraction for concurrency, like a task-based system, to simplify the logic.
    *   **Severity**: High
    *   **Effort**: M (1-4hrs)

### Medium
1.  **Hardcoded Concrete Types Instead of Abstractions**
    *   **Location**: `src/core/Application.cpp`
    *   **Smell Type**: DIP Violation
    *   **Impact**: The application directly creates a `SearchThreadPool` and injects it. While this is better than a global, it still couples `Application` to a specific thread pool implementation.
    *   **Refactoring Suggestion**: Introduce an `IThreadPool` interface and inject that instead.
    *   **Severity**: Medium
    *   **Effort**: S (< 1hr)

2.  **Missing Abstraction for Platform-Specific Logic**
    *   **Location**: `src/platform/` directory
    *   **Smell Type**: Missing Abstraction
    *   **Impact**: There's significant code duplication in the platform-specific files (e.g., `StringUtils_win.cpp`, `StringUtils_mac.cpp`, `StringUtils_linux.cpp`). While the implementations differ, the interfaces are often similar. This violates the DRY principle and makes it harder to maintain platform consistency.
    *   **Refactoring Suggestion**: Introduce platform-agnostic interfaces (e.g., `IStringUtils`) and have each platform provide a concrete implementation.
    *   **Severity**: Medium
    *   **Effort**: M (1-4hrs)

3.  **Global State (`GuiState`)**
    *   **Location**: `src/gui/GuiState.h`
    *   **Smell Type**: Hard to Unit Test
    *   **Impact**: The `GuiState` object is passed around to many different components, effectively acting as a global state container for the UI. This makes it difficult to test UI components in isolation, as they all depend on this large, shared state object.
    *   **Refactoring Suggestion**: Break down `GuiState` into smaller, more focused state objects for different parts of the UI (e.g., `SearchState`, `FilterState`, `ResultsState`).
    *   **Severity**: Medium
    *   **Effort**: M (1-4hrs)

4.  **Header-Heavy Design with High Coupling**
    *   **Location**: Across the codebase.
    *   **Smell Type**: Low Cohesion / Tight Coupling
    *   **Impact**: Many header files include other headers, leading to long compile times and high coupling between components. A change in a low-level header can trigger a cascade of recompilations.
    *   **Refactoring Suggestion**: Use forward declarations where possible and only include headers in source files.
    *   **Severity**: Medium
    *   **Effort**: M (1-4hrs)

### Low
1.  **Utility Classes with Low Cohesion**
    *   **Location**: `src/utils/` directory
    *   **Smell Type**: Low Cohesion
    *   **Impact**: Some utility files are a mix of unrelated functions. This makes it harder to find specific functionality.
    *   **Refactoring Suggestion**: Group related functions into more specific utility files (e.g., `StringUtils.h`, `PathUtils.h`).
    *   **Severity**: Low
    *   **Effort**: S (< 1hr)

2.  **Flag Arguments in UI Components**
    *   **Location**: `src/ui/UIRenderer.cpp`
    *   **Smell Type**: Cognitive Complexity
    *   **Impact**: Functions like `UIRenderer::RenderMainWindow` take boolean flags (`show_settings_`, `show_metrics_`) that alter their behavior. This makes the code harder to understand than using separate functions or a state enum.
    *   **Refactoring Suggestion**: Use a state enum or separate functions for different rendering states.
    *   **Severity**: Low
    *   **Effort**: S (< 1hr)

3.  **Generic Names (`Manager`, `Utils`)**
    *   **Location**: `src/platform/windows/DirectXManager.cpp`, `src/utils/`
    *   **Smell Type**: Naming & Structure
    *   **Impact**: Generic names like "Manager" and "Utils" don't convey the specific purpose of the class.
    *   **Refactoring Suggestion**: Use more descriptive names (e.g., `DirectXRenderer` instead of `DirectXManager`).
    *   **Severity**: Low
    *   **Effort**: S (< 1hr)

## Summary Requirements
- **Architecture Health Score**: 5/10. The application works, but its architecture is brittle and tightly coupled. The "God Class" and DIP violations are significant issues that will hinder future development and maintenance.
- **Top 3 Systemic Issues**:
    1.  Widespread violation of the Single Responsibility Principle.
    2.  Lack of dependency inversion, leading to tight coupling and poor testability.
    3.  Leaky abstractions, especially for platform-specific code.
- **Recommended Refactorings**:
    1.  **Inject Dependencies into `Application`**: This is the highest impact change, as it will enable unit testing and decouple the core logic from its dependencies.
    2.  **Break up `Application` and `FileIndex`**: These "God Classes" are the biggest source of complexity and should be refactored into smaller, more focused classes.
    3.  **Use the Pimpl Idiom for Platform-Specific Code**: This will improve encapsulation and reduce compile times.
- **Testing Improvement Areas**: The lack of interfaces and dependency injection makes most of the core logic very difficult to unit test. The `Application` and `FileIndex` classes are effectively untestable in their current state. Introducing interfaces for their dependencies is the first step towards improving testability.
- **Threading Model Assessment**: The current threading model is ad-hoc and lacks clear contracts, which is not sustainable at scale. As the application grows, the risk of deadlocks and race conditions will increase. A more structured approach with clear documentation and ownership is needed.
