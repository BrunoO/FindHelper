# Architecture Review - 2026-01-31

## Executive Summary
- **Health Score**: 6/10
- **Critical Issues**: 1
- **High Issues**: 2
- **Total Findings**: 6
- **Estimated Remediation Effort**: 20 hours

## Findings

### Critical

#### 1. Tight Coupling and Lack of Dependency Injection in Application Class
- **Location**: `src/core/Application.cpp`
- **Smell Type**: DIP Violation / Tight Coupling
- **Impact**: `Application` is responsible for instantiating every major component. This makes unit testing the `Application` class impossible without heavy integration, and makes it difficult to swap implementations for testing or platform-specific needs.
- **Refactoring Suggestion**: Use Dependency Injection. Pass pre-constructed components to the `Application` constructor or use a `ComponentRegistry` / `ServiceLocator`.
- **Severity**: Critical
- **Effort**: Large

### High

#### 2. God Object: GuiState
- **Location**: `src/gui/GuiState.h` - `class GuiState`
- **Smell Type**: SRP Violation
- **Impact**: Aggregates 60+ fields including raw input buffers, search settings, results, and UI toggles. Changes to any part of the UI state require recompiling everything that depends on `GuiState`.
- **Refactoring Suggestion**: Split `GuiState` into smaller, focused state objects (e.g., `SearchInputState`, `FilterState`, `ViewOptionsState`, `ResultListState`).
- **Severity**: High
- **Effort**: Medium

#### 3. Fat Interface: ResultsTable::Render
- **Location**: `src/ui/ResultsTable.h` - `ResultsTable::Render(...)`
- **Smell Type**: ISP Violation / Tight Coupling
- **Impact**: The method requires 5-6 disparate dependencies to function. This makes it difficult to use the table in other contexts or to test it in isolation.
- **Refactoring Suggestion**: Group related parameters into a `RenderingContext` struct or use a Facade to provide a simpler interface to the rendering logic.
- **Severity**: High
- **Effort**: Medium

### Medium

#### 4. Leaky Abstraction: NativeWindowHandle
- **Location**: Multiple UI components (e.g., `ResultsTable`, `StatusBar`)
- **Smell Type**: Abstraction Failure / Leaky Abstraction
- **Impact**: Passing a platform-specific window handle (even as a `void*` stub) through generic UI components indicates they know too much about the platform's windowing system.
- **Refactoring Suggestion**: Wrap window-specific operations (like opening a context menu or drag-and-drop) in a `PlatformWindow` interface that hides the raw handle.
- **Severity**: Medium
- **Effort**: Medium

#### 5. Manual Thread Coordination in UsnMonitor
- **Location**: `src/usn/UsnMonitor.h` - `UsnJournalQueue`
- **Smell Type**: Low Cohesion / Missing Abstraction
- **Impact**: Manual implementation of a producer-consumer queue using `std::queue`, `mutex`, and `condition_variable` is repeated or similar across different parts of the system.
- **Refactoring Suggestion**: Use a standard concurrent queue template (e.g., `boost::lockfree::spsc_queue` or a custom `ConcurrentQueue<T>`) to improve reliability and performance.
- **Severity**: Medium
- **Effort**: Small

### Low

#### 6. Feature Envy in SearchController
- **Location**: `src/search/SearchController.cpp`
- **Smell Type**: Feature Envy
- **Impact**: `SearchController` heavily manipulates `GuiState` fields directly. It acts more like a "Manager" that orchestrates other objects' data rather than having its own.
- **Refactoring Suggestion**: Move some search-triggering logic into `GuiState` (or the new smaller state objects) or `SearchWorker`.
- **Severity**: Low
- **Effort**: Small

## Summary Requirements

- **Architecture Health Score**: 6/10. The project has a clear platform abstraction strategy and uses modern C++ features well, but suffers from "God Object" and "God Class" patterns that hinder modularity and testability.
- **Top 3 Systemic Issues**:
  1. Monolithic State (`GuiState`).
  2. Direct instantiation of dependencies in high-level classes.
  3. Platform details leaking into high-level UI logic.
- **Recommended Refactorings**:
  1. **Phase 1**: Split `GuiState` into smaller components (High Impact / Medium Effort).
  2. **Phase 2**: Introduce an `IApplicationServices` interface to facilitate dependency injection in `Application` (High Impact / Large Effort).
  3. **Phase 3**: Abstract `NativeWindowHandle` into a platform-agnostic `IWindowActions` interface (Medium Impact / Medium Effort).
- **Testing Improvement Areas**: Introducing interfaces for `SearchWorker` and `FileIndex` would allow for much more robust UI unit testing without requiring a full file system scan.
- **Threading Model Assessment**: The current threading model is sound (separate reader/processor for USN, worker pool for searches). However, the manual synchronization in several places increases the risk of deadlocks as complexity grows. Moving towards more message-passing or standardized concurrent containers is recommended.
