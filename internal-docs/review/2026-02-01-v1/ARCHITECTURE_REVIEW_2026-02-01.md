# Architecture Review - 2026-02-01

## Executive Summary
- **Health Score**: 6/10
- **Critical Issues**: 1
- **High Issues**: 2
- **Total Findings**: 5
- **Estimated Remediation Effort**: 20 hours

## Findings

### Critical
1. **Tight Coupling: Application God Class**
   - **Location**: `src/core/Application.cpp`
   - **Smell Type**: SRP Violation / God Class
   - **Impact**: `Application` class handles window management, renderer lifecycle, search coordination, index building, PGO flushing, and settings persistence. This makes it extremely hard to test in isolation and difficult to extend without modifying the core loop.
   - **Refactoring Suggestion**: Extract `WindowManager`, `SearchCoordinator`, and `SettingsManager` into separate components. Use a Mediator pattern to coordinate them.
   - **Severity**: Critical
   - **Effort**: Large

### High
2. **Hard-coded Dependencies in `FileIndex`**
   - **Location**: `src/index/FileIndex.cpp` (Constructor)
   - **Smell Type**: DIP Violation
   - **Impact**: `FileIndex` instantiates its own sub-components like `LazyAttributeLoader`, `PathRecomputer`, etc. This prevents mocking these components during unit tests.
   - **Refactoring Suggestion**: Use Constructor Injection to provide these components, possibly using a Factory or a Dependency Injection container.
   - **Severity**: High
   - **Effort**: Medium

3. **Platform Abstraction Leaks**
   - **Location**: `src/core/Application.cpp` (GetNativeWindowHandle)
   - **Smell Type**: Leaky Abstraction
   - **Impact**: `NativeWindowHandle` is defined differently per platform, but the logic for it is scattered. `Application` still contains `#ifdef _WIN32` blocks for PGO and window handles.
   - **Refactoring Suggestion**: Create a `PlatformService` interface that handles these platform-specific tasks.
   - **Severity**: High
   - **Effort**: Medium

### Medium
4. **Lock Granularity: Shared Mutex Overuse**
   - **Location**: `src/search/ParallelSearchEngine.cpp`
   - **Smell Type**: Threading Smell
   - **Impact**: While using `shared_mutex` is good for readers, the search worker threads each acquire their own `shared_lock`. If a write operation (like an index update) comes in, it must wait for all search threads to finish, which could cause UI lag if updates are frequent.
   - **Refactoring Suggestion**: Consider a Read-Copy-Update (RCU) approach or finer-grained locking for index updates.
   - **Severity**: Medium
   - **Effort**: Large

### Low
5. **Feature Envy in `SearchController`**
   - **Location**: `src/search/SearchController.cpp`
   - **Smell Type**: Feature Envy
   - **Impact**: `SearchController` heavily manipulates `GuiState` members.
   - **Refactoring Suggestion**: Move state transition logic into `GuiState` methods.
   - **Severity**: Low
   - **Effort**: Small

## Summary Requirements

### Architecture Health Score: 6/10
Justification: The project has a solid foundation for performance (SoA, parallel search), but suffers from traditional "God Class" symptoms and tight coupling that will hinder scalability and cross-platform expansion.

### Top 3 Systemic Issues
1. **God Classes**: `Application` and `FileIndex` own too many responsibilities.
2. **Direct Dependency on File I/O**: High-level search logic depends on `LoadSettings`.
3. **Incomplete Platform Isolation**: `#ifdef` blocks in core logic files.

### Recommended Refactorings
1. **Extract SearchCoordinator**: Move search lifecycle management out of `Application`.
2. **Dependency Injection in FileIndex**: Allow passing sub-components to the constructor.
3. **Settings Service**: Decouple settings loading from search execution.

### Testing Improvement Areas
- **Mocking the Index**: Interfaces for `ISearchableIndex` are already present but not fully utilized in tests.
- **UI-less Testing**: De-coupling `Application` from GLFW would allow running full integration tests in headless environments.

### Threading Model Assessment
The current model (SPSC queues for USN, parallel search with shared locks) is very efficient for Windows-based real-time indexing. However, the shared lock on the entire index for searching might become a bottleneck if multi-volume support or frequent background updates are added.
