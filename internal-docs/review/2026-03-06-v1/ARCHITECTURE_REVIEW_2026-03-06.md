# Architecture Review - 2026-03-06

## Executive Summary
- **Health Score**: 8.5/10
- **Critical Issues**: 0
- **High Issues**: 1
- **Total Findings**: 6
- **Estimated Remediation Effort**: 20 hours

## Findings

### Critical
None.

### High
1. **Low Interface Segregation: ParallelSearchEngine.h** (Category 1)
   - **Location**: `src/search/ParallelSearchEngine.h` (~600 lines)
   - **Smell Type**: ISP violation - Fat interface.
   - **Impact**: Clients must depend on a massive header even if they only need basic search functionality. Increases coupling and compilation time.
   - **Refactoring Suggestion**: Extract `ISearchStrategy` and `IResultCollector` interfaces to decouple engine from specific logic.
   - **Severity**: High
   - **Effort**: Large (4+ hours)

### Medium
1. **Platform Logic Leakage: AppBootstrapCommon.h** (Category 5)
   - **Location**: `src/core/AppBootstrapCommon.h` (~445 lines)
   - **Smell Type**: Leaky abstraction.
   - **Impact**: Some platform-specific concerns leaking into common bootstrap logic.
   - **Refactoring Suggestion**: Use `Pimpl` or `Bridge` pattern to isolate platform-specific bootstrap details.
   - **Severity**: Medium
   - **Effort**: Medium (2-4 hours)

2. **Low Cohesion: FileSystemUtils.h** (Category 4)
   - **Location**: `src/utils/FileSystemUtils.h` (~436 lines)
   - **Smell Type**: Grab-bag of unrelated utilities.
   - **Impact**: Poor discoverability and high coupling when only a small portion is needed.
   - **Refactoring Suggestion**: Split into `PathValidationUtils`, `FileAccessUtils`, and `VolumeInfoUtils`.
   - **Severity**: Medium
   - **Effort**: Medium (2 hours)

3. **Cognitive Complexity: SettingsWindow.cpp** (Category 7)
   - **Location**: `src/ui/SettingsWindow.cpp::Render`
   - **Smell Type**: High cognitive complexity.
   - **Impact**: Difficult to maintain or add new settings without breaking existing ones.
   - **Refactoring Suggestion**: Extract section-specific rendering logic into private helper methods. (Partially addressed in recent refactoring but still complex).
   - **Severity**: Medium
   - **Effort**: Medium (2 hours)

### Low
1. **Design Pattern Misuse: Singleton remnants** (Category 3)
   - **Location**: `src/core/Application.cpp`
   - **Smell Type**: Global/Singleton dependency.
   - **Impact**: Harder to unit test or run multiple instances.
   - **Refactoring Suggestion**: Move towards full Dependency Injection for `Application` components.
   - **Severity**: Low
   - **Effort**: Large (6+ hours)

2. **Redundant Locks: SearchWorker.cpp** (Category 2)
   - **Location**: `src/search/SearchWorker.cpp`
   - **Smell Type**: Lock scope too wide.
   - **Impact**: Minor performance hit due to holding locks longer than necessary.
   - **Refactoring Suggestion**: Narrow lock scope around critical sections only.
   - **Severity**: Low
   - **Effort**: Small (1 hour)

## Architecture Health Score: 8.5/10
The architecture is generally robust, with clear platform separation and a well-defined threading model. The main concerns are growing complexity in UI and path-matching modules, and some "fat" interfaces that could be further decomposed.

## Top 3 Systemic Issues
1. **Header-heavy designs**: Many files include excessive headers, increasing build times and coupling.
2. **Platform isolation strategy**: While suffixes are used, some common headers still contain platform-specific logic via `#ifdef`.
3. **UI/Logic coupling**: Some UI components still own significant business logic (e.g., `ResultsTable.cpp`).

## Recommended Refactorings
- **Decompose ParallelSearchEngine**: Split into core engine and pluggable strategies.
- **Split FileSystemUtils**: Modularize utility classes for better cohesion.
- **Modularize ResultsTable**: Separate UI rendering from search/marking logic.

## Threading Model Assessment
The current threading model using `shared_mutex` for readers and `atomic` counters is highly effective for this workload. It is sustainable at scale, but care should be taken with lock granularity as the feature set grows.
