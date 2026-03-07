# Architecture Review - 2026-02-06

## Executive Summary
- **Health Score**: 6/10
- **Critical Issues**: 1
- **High Issues**: 3
- **Total Findings**: 10
- **Estimated Remediation Effort**: 24 hours

## Findings

### Critical
1. **Application Class as God Object**
   - **Location**: `src/core/Application.cpp`
   - **Smell Type**: SRP Violation / God Class
   - **Impact**: extremely difficult to maintain and test. Any change to UI, indexing, or window management requires modifying this class.
   - **Refactoring Suggestion**: Extract `IndexLifecycleManager`, `SearchCoordinator`, and `UIManager` into separate classes. Use a Mediator pattern or simple dependency injection to coordinate between them.
   - **Severity**: Critical
   - **Effort**: Large (> 8 hours)

### High
1. **Tight Coupling in FileIndex**
   - **Location**: `src/index/FileIndex.h`
   - **Smell Type**: Coupling & Cohesion / Fat Interface
   - **Impact**: Changes to any sub-component (Storage, PathStorage, Operations) often require changes to the `FileIndex` facade. Compilation times are high due to extensive includes.
   - **Refactoring Suggestion**: Use PIMPL to hide implementation details and reduce header dependencies. Segregate the interface into `IIndexSearcher` and `IIndexUpdater`.
   - **Severity**: High
   - **Effort**: Large (> 6 hours)

2. **Complex State Management in SearchWorker**
   - **Location**: `src/search/SearchWorker.cpp`
   - **Smell Type**: Cognitive Complexity / Feature Envy
   - **Impact**: The logic for handling streaming results, cancellation, and metrics is intertwined. It's hard to follow the lifecycle of a search request.
   - **Refactoring Suggestion**: Use a State pattern for search lifecycle (Idle, Searching, Draining, Cancelled). Move metrics collection to a separate observer.
   - **Severity**: High
   - **Effort**: Medium (4 hours)

3. **Platform Code Leaks in Shared Headers**
   - **Location**: `src/utils/Logger.h`
   - **Smell Type**: Abstraction Failure / Leaky Abstraction
   - **Impact**: `Logger.h` contains many `#ifdef _WIN32` blocks. This breaks the "clean platform abstraction" goal and makes the header harder to read.
   - **Refactoring Suggestion**: Move platform-specific logic (directory creation, executable name retrieval) to `PlatformUtils.h` with per-platform `.cpp` implementations.
   - **Severity**: High
   - **Effort**: Medium (3 hours)

### Medium
1. **Hardcoded Search Strategies**
   - **Location**: `src/search/SearchWorker.cpp`
   - **Smell Type**: OCP Violation
   - **Impact**: Adding a new search path (e.g., "Search by Content") would require modifying the `ExecuteSearch` method and adding more `if/else` branches.
   - **Refactoring Suggestion**: Use the Strategy pattern for different search types.
   - **Severity**: Medium
   - **Effort**: Medium (3 hours)

2. **Interface Segregation Gaps**
   - **Location**: `ISearchableIndex.h`
   - **Smell Type**: ISP Violation
   - **Impact**: Some components might only need read access or path access but are forced to depend on the full `FileIndex` or a wide `ISearchableIndex`.
   - **Refactoring Suggestion**: Break `ISearchableIndex` into smaller, focused interfaces like `IPathProvider`.
   - **Severity**: Medium
   - **Effort**: Medium (2 hours)

### Low
1. **Magic Numbers in Logger**
   - **Location**: `src/utils/Logger.h` (`logger_constants`)
   - **Smell Type**: Abstraction Failure
   - **Impact**: Hardcoded default log sizes and intervals.
   - **Refactoring Suggestion**: Move these to a configuration file or settings class.
   - **Severity**: Low
   - **Effort**: Small (1 hour)

## Quick Wins
1. **Extract platform-specific logic from `Logger.h`**: Clean up the header by moving `#ifdef` blocks to utility functions.
2. **Introduce `SearchStrategy` interface**: Start refactoring `SearchWorker` by encapsulating the "Filtered Search" logic.
3. **Add `IIndexStateProvider`**: Extract simple status methods from `FileIndex` to a smaller interface for UI use.

## Recommended Actions
1. **Prioritize decoupling `Application`**: This is the most significant architectural bottleneck.
2. **PIMPL for `FileIndex`**: To improve compilation times and encapsulation.
3. **Clean up `SearchWorker`**: Refactor the complex loops into a more manageable state machine or task-based system.

## Threading Model Assessment
The current model (Worker thread + Thread pool for parallel search) is sound for this scale. However, the coordination logic in `SearchWorker` for draining futures and handling streaming results is becoming brittle. Moving towards a more robust "Task Graph" or "Pipeline" approach would be beneficial for future features.
