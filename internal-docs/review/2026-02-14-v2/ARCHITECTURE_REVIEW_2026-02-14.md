# Architecture Review - 2026-02-14

## Executive Summary
- **Health Score**: 7/10
- **Critical Issues**: 0
- **High Issues**: 2
- **Total Findings**: 8
- **Estimated Remediation Effort**: 32 hours

## Findings

### High
- **Circular Dependency between `SearchTypes.h` and UI**:
  - **Location**: `src/search/SearchTypes.h` and various UI files.
  - **Smell Type**: Tight Coupling
  - **Impact**: Makes it difficult to test search logic in isolation from UI types. Circular dependencies slow down compilation and complicate the build graph.
  - **Refactoring Suggestion**: Ensure `SearchTypes.h` only contains POD (Plain Old Data) and no dependencies on ImGui or UI-specific state.
  - **Severity**: High
  - **Effort**: M (4 hours)

- **Lack of Dependency Inversion in `SearchWorker`**:
  - **Location**: `src/search/SearchWorker.cpp`
  - **Smell Type**: SOLID (DIP)
  - **Impact**: `SearchWorker` directly instantiates or tightly couples with `FileIndex` and other low-level modules. This makes mocking the index for search tests difficult.
  - **Refactoring Suggestion**: Inject an `IFileIndex` interface or use template-based policy injection.
  - **Severity**: High
  - **Effort**: L (8 hours)

### Medium
- **Platform Abstraction Leaks**:
  - **Location**: `src/usn/UsnMonitor.cpp`
  - **Smell Type**: Abstraction Failure
  - **Impact**: Windows-specific types (`HANDLE`, `PUSN_RECORD`) are prominent in logic that could be partially abstracted for future Linux/macOS real-time monitoring.
  - **Refactoring Suggestion**: Introduce a `FileSystemMonitor` interface.
  - **Severity**: Medium
  - **Effort**: L (12 hours)

- **God Method: `SearchWorker::WorkerThread`**:
  - **Location**: `src/search/SearchWorker.cpp`
  - **Smell Type**: Cognitive Complexity
  - **Impact**: The main worker loop is overly complex, handling search execution, cancellation, result materialization, and batching in one large method.
  - **Refactoring Suggestion**: Decompose into `WaitForSearchRequest`, `ExecuteSearch`, and `ProcessResults`.
  - **Severity**: Medium
  - **Effort**: M (6 hours)

## Quick Wins
- Extract `SearchResult` display string generation from `SearchWorker` to `SearchResultUtils`.
- Move constants from `SearchTypes.h` to a dedicated `Constants.h`.

## Recommended Actions
- **Prioritize** the refactoring of `SearchWorker::WorkerThread` to improve reliability of the search pipeline.
- **Implement** a cleaner separation between the core search engine and the UI state management.

---
**Architecture Health Score**: 7/10 - The project has a solid multi-threaded foundation but suffers from "God Object" and "God Method" symptoms in core components.

**Threading Model Assessment**: Sustainable but complex. The use of `std::future` and batching in `SearchWorker` is well-implemented for responsiveness, but the shared state in `GuiState` remains a risk for future scalability.
