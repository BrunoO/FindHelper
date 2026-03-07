# Architecture Review - 2026-02-09

## Executive Summary
- **Health Score**: 7/10
- **Critical Issues**: 0
- **High Issues**: 2
- **Total Findings**: 12
- **Estimated Remediation Effort**: 40 hours

## Findings

### High
- **God Object: `Application` Class**: (src/core/Application.h)
  - **Smell Type**: SRP Violation
  - **Impact**: The `Application` class manages everything from UI rendering to USN monitoring. This makes it hard to test components in isolation and leads to high cognitive complexity (Run() method).
  - **Refactoring Suggestion**: Apply the **Facade** pattern to separate the UI management from the application state and backend services.
  - **Severity**: High
  - **Effort**: L

- **Tight Coupling between UI and Search Logic**: (src/ui/SearchInputs.cpp)
  - **Smell Type**: Coupling
  - **Impact**: The UI directly triggers searches and manages Gemini API futures. If the UI framework changes or if we want to add a CLI-only mode, this logic would need to be duplicated.
  - **Refactoring Suggestion**: Use the **Command** pattern or a dedicated **Service Layer** to handle search requests initiated from the UI.
  - **Severity**: High
  - **Effort**: M

### Medium
- **Leaky Abstraction: Windows Types in `SearchTypes.h`**: (src/search/SearchTypes.h)
  - **Smell Type**: Leaky Abstraction
  - **Impact**: `SearchResult` includes `FILETIME` directly. While aliased for other platforms, it still forces a Windows-centric data structure onto the core search results.
  - **Refactoring Suggestion**: Use a platform-neutral time representation (like `std::chrono::system_clock::time_point` or a custom `EpochTime`) in the core results, converting from `FILETIME` at the edges.
  - **Severity**: Medium
  - **Effort**: M

- **Direct Member Access in `FileEntry`**: (src/index/FileIndexStorage.h)
  - **Smell Type**: Low Cohesion / Encapsulation
  - **Impact**: `FileEntry` is a "Plain Old Data" (POD) struct with public members accessed everywhere. This makes it impossible to track where certain metadata is modified or to add validation logic later.
  - **Refactoring Suggestion**: Convert to a class with private members and controlled accessors, especially for `mutable` fields used for lazy loading.
  - **Severity**: Medium
  - **Effort**: S

### Low
- **Inconsistent Singleton Usage**: (src/search/SearchThreadPoolManager.h)
  - **Smell Type**: Design Pattern Misuse
  - **Impact**: Some services use Manager classes (which are essentially singletons), while others are passed via reference.
  - **Refactoring Suggestion**: Standardize on **Dependency Injection** for all major services.
  - **Severity**: Low
  - **Effort**: S

## Summary Requirements

- **Architecture Health Score**: 7/10. The project has a clear platform abstraction layer and good separation of indexing vs searching. However, the core application lifecycle is overly centralized.
- **Top 3 Systemic Issues**:
  1. **God Classes**: `Application` and `FileIndex` have too many responsibilities.
  2. **UI/Logic Mixing**: UI components often contain "smart" logic instead of being "dumb" views.
  3. **Platform Leakage**: Windows-specific types (`FILETIME`, `HANDLE`) are visible deeper in the stack than ideal.
- **Recommended Refactorings**:
  - **Priority 1**: Decompose `Application` into `WindowManager`, `AppLifecycle`, and `ServiceRegistry`.
  - **Priority 2**: Decouple `SearchController` from `GuiState` using an observer or message-passing pattern.
- **Testing Improvement Areas**: The lack of a clear Service Layer makes it difficult to mock the Index or Search engine for UI tests.
- **Threading Model Assessment**: The current threading model (Multiple reader threads with `shared_mutex`) is appropriate for the read-heavy search workload. The use of a specialized `SearchWorker` thread to handle search orchestration is a good design choice.
