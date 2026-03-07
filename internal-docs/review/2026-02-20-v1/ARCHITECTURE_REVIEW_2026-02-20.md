# Architecture & Code Smells Review - 2026-02-20

## Executive Summary
- **Architecture Health Score**: 8/10
- **Top 3 Systemic Issues**:
  1. **Fat State Object**: `GuiState` acts as a monolithic repository for all UI state, leading to tight coupling between UI components.
  2. **Parameter-Heavy UI Rendering**: `UIRenderer` methods take up to 13 parameters, indicating a need for better encapsulation of rendering context.
  3. **Mixed Concerns in Workers**: `SearchWorker` and `UsnMonitor` mix core logic with detailed logging and metric collection.

## Findings

### 1. SOLID Principle Violations

**Single Responsibility Principle (SRP)**
- **Location**: `src/gui/GuiState.h`, `class GuiState`
- **Violation**: Manages 62 fields ranging from search inputs to export notifications and Gemini API state.
- **Impact**: Hard to maintain, test, and reason about. Any change to UI state requires modifying this central class.
- **Refactoring Suggestion**: Use **Composition**. Split `GuiState` into sub-structs (e.g., `SearchInput`, `SearchResultsView`, `BackgroundTasksStatus`).
- **Severity**: High
- **Effort**: Large

**Dependency Inversion Principle (DIP)**
- **Location**: `src/core/Application.cpp`, `Application::ProcessFrame`
- **Violation**: `Application` directly calls static methods of `ui::UIRenderer`.
- **Impact**: Tight coupling between the central application controller and the UI rendering implementation. Makes it harder to swap renderers or test in a headless mode.
- **Refactoring Suggestion**: Use an interface for the UI Renderer and inject it into the `Application`.
- **Severity**: Medium
- **Effort**: Medium

### 2. Threading & Concurrency Smells

**Lock Granularity**
- **Location**: `src/usn/UsnMonitor.cpp`
- **Violation**: Multiple methods use `std::scoped_lock guard(mutex_)` for relatively large blocks.
- **Impact**: Potential for UI thread stalls if the monitor thread is holding the lock during heavy I/O or processing.
- **Refactoring Suggestion**: Audit lock scopes and use `std::shared_mutex` where multiple threads only need read access to the index state.
- **Severity**: Medium
- **Effort**: Medium

### 3. Coupling & Cohesion Smells

**Feature Envy / Parameter List**
- **Location**: `src/ui/UIRenderer.h`, `RenderMainWindow`
- **Violation**: Takes 13 parameters.
- **Impact**: Extremely fragile interface. Adding any new dependency to the UI requires updating this signature and all call sites.
- **Refactoring Suggestion**: Introduce a `RenderContext` parameter object that encapsulates these dependencies.
- **Severity**: Medium
- **Effort**: Small

### 4. Abstraction Failures

**Leaky Platform Abstraction**
- **Location**: `src/core/Application.cpp`, `FlushPgoData` and `Application::Run`
- **Violation**: Windows-specific PGO flushing logic is embedded directly in `Application.cpp`.
- **Impact**: Pollutes the platform-agnostic core logic with platform-specific implementation details.
- **Refactoring Suggestion**: Move PGO and platform-specific lifecycle hooks into `AppBootstrap_win.cpp` or a `PlatformLifecycle` abstraction.
- **Severity**: Low
- **Effort**: Small
- **Note (2026-02-21)**: Fixed. Introduced optional `platform_exit_hook` in `AppBootstrapResultBase` (`std::function<void(const char*)>`). Application stores and invokes it on normal shutdown and in exception handlers. Windows sets the hook in `AppBootstrap_win.cpp` (FlushPgoData + OnPlatformExit); macOS/Linux leave it empty. No more `#ifdef _WIN32` or PGO code in `Application.cpp`.

## Summary

- **Architecture Health Score**: 8/10. The codebase uses modern C++17 features well and has a clear separation between the search engine (ParallelSearchEngine), the index (FileIndex), and the UI. The use of Dependency Injection for the index builder is a strong point.
- **Top 3 Systemic Issues**:
  1. Monolithic `GuiState`.
  2. Large parameter lists in UI rendering.
  3. Platform-specific logic leaking into `Application.cpp`.
- **Recommended Refactorings**:
  1. **Immediate**: Wrap `UIRenderer` parameters into a `RenderContext` struct.
  2. **Short-term**: Move Windows-specific PGO code out of `Application.cpp`.
  3. **Long-term**: Decompose `GuiState` into logical components.
- **Testing Improvement Areas**:
  - The UI logic in `UIRenderer` and `ResultsTable` is hard to unit test due to direct ImGui calls and heavy parameter lists. Extracting "Presenter" or "ViewModel" logic would help.
- **Threading Model Assessment**: The dual-thread design for USN monitoring and the thread-pool-based parallel search are well-implemented and sustainable. The use of atomic counters for progress tracking is efficient.
