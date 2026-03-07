# Architecture Review - 2026-01-12

## Executive Summary
- **Architecture Health Score**: 5/10
- **Top 3 Systemic Issues**:
    1.  **Pervasive God Classes**: Core components like `FileIndex` and `Application` violate the Single Responsibility Principle, leading to high coupling and low cohesion.
    2.  **Inadequate Abstraction**: Platform-specific logic and UI concerns are not sufficiently abstracted from core application logic.
    3.  **Weak Testability**: The lack of dependency injection and clear interfaces makes unit testing difficult for major components.
- **Recommended Refactorings**: Focus on breaking down God Classes, introducing a clear UI/Logic separation, and using dependency injection.
- **Threading Model Assessment**: The threading model is functional but fragile. The use of a background `SearchWorker` thread is appropriate, but the `FileIndex` class directly manages a `ThreadPool` for parallel searching, mixing data management with execution logic. This could be improved by externalizing the thread pool.

## Findings

### Critical
1.  **Smell Type**: SOLID Principle Violations / Single Responsibility Principle (SRP)
    - **Location**: `src/index/FileIndex.h`
    - **Impact**: The `FileIndex` class is a "God Class" that acts as a data store, a search engine, and an index manager. This makes the class extremely difficult to reason about, test, and maintain. Changes to search logic can break indexing, and vice-versa.
    - **Refactoring Suggestion**: Decompose `FileIndex` into several smaller, more focused classes. For example:
        - `FileIndexStore`: Manages the in-memory data structures.
        - `SearchEngine`: Executes searches against the store.
        - `IndexMaintainer`: Handles updates and maintenance.
    - **Severity**: Critical
    - **Effort**: L

### High
1.  **Smell Type**: SOLID Principle Violations / Single Responsibility Principle (SRP)
    - **Location**: `src/core/Application.h`
    - **Impact**: The `Application` class orchestrates the main loop, manages the application window, directly calls UI rendering functions, and contains core application state. This mixes window management, UI rendering, and business logic, making it difficult to test any of these concerns in isolation.
    - **Refactoring Suggestion**: Apply a Model-View-Controller (MVC) or Model-View-ViewModel (MVVM) pattern. The `Application` class should be the Controller, managing the main loop. A `GuiState` or `ViewModel` should hold the application state, and a separate `UIRenderer` class should be responsible for all ImGui calls, taking the `GuiState` as input.
    - **Severity**: High
    - **Effort**: L

2.  **Smell Type**: Testability Smells / Hard to Unit Test
    - **Location**: `src/core/Application.cpp`, `src/search/SearchWorker.cpp`
    - **Impact**: Major components instantiate their own dependencies directly (e.g., `Application` creates its own `IIndexBuilder`, `SearchWorker` creates its own `ParallelSearchEngine`). This hard-coded dependency management makes it impossible to substitute mocks for unit testing, leading to a reliance on integration tests.
    - **Refactoring Suggestion**: Use Dependency Injection. Pass dependencies as interfaces to the constructor. For example, the `Application` constructor should accept an `std::unique_ptr<IIndexBuilder>`. This allows passing a mock implementation during tests.
    - **Severity**: High
    - **Effort**: M

3.  **Smell Type**: Abstraction Failures / Leaky Abstractions
    - **Location**: `src/core/ApplicationLogic.cpp` and throughout the `ui/` directory.
    - **Impact**: The core application logic is directly coupled to the ImGui library. UI components in the `ui/` directory often contain business logic (e.g., how to format data, when to disable a button). This violates the separation of concerns and means the UI cannot be easily swapped or tested without a running graphical environment.
    - **Refactoring Suggestion**: The UI components should be "dumb." All logic should reside in `ApplicationLogic` or other core components. The UI should only be responsible for rendering the state provided to it. For example, instead of the UI deciding if a button is disabled, the logic layer should provide a boolean `is_save_button_enabled` to the UI.
    - **Severity**: High
    - **Effort**: M

### Medium
1.  **Smell Type**: Coupling & Cohesion Smells / Low Cohesion
    - **Location**: `src/utils/` directory
    - **Impact**: The `utils` directory is a collection of unrelated utility classes (`ClipboardUtils`, `CpuFeatures`, `SystemIdleDetector`, `ThreadPool`). While utility classes are common, this directory lacks a clear theme and could become a dumping ground for miscellaneous code.
    - **Refactoring Suggestion**: Organize utilities into more cohesive sub-namespaces or directories. For example, `ThreadPool` could be in a `concurrency` or `threading` subdirectory, while `CpuFeatures` could be in a `system` or `platform` subdirectory.
    - **Severity**: Medium
    - **Effort**: S

2.  **Smell Type**: Abstraction Failures / Missing Abstractions
    - **Location**: `src/platform/` directory
    - **Impact**: There is duplicated logic for file operations and string utilities across the platform-specific implementations (`_win.cpp`, `_mac.mm`, `_linux.cpp`). While the implementations are platform-specific, the interfaces are nearly identical. This suggests a missing abstraction layer.
    - **Refactoring Suggestion**: Define a common, platform-agnostic interface for file operations and string utilities (e.g., `IFileOperations`, `IStringUtils`). Each platform would provide a concrete implementation of these interfaces, which would be instantiated at startup. This would reduce code duplication and make the core logic more platform-agnostic.
    - **Severity**: Medium
    - **Effort**: M

### Low
1.  **Smell Type**: Design Pattern Misuse / Singleton
    - **Location**: `src/ui/FolderBrowser.cpp`
    - **Impact**: The `FolderBrowser` is implemented as a pseudo-singleton, with a static instance being created and managed. While convenient, this creates global state and makes it difficult to have multiple folder browsers or to test the UI components that use it.
    - **Refactoring Suggestion**: The `FolderBrowser` should be owned by the class that uses it (e.g., `Application` or a UI manager). Its state should be managed as part of the application's overall UI state.
    - **Severity**: Low
    - **Effort**: S
