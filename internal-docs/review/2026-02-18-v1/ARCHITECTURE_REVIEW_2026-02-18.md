# Architecture Review - 2026-02-18

## Executive Summary
- **Health Score**: 8/10
- **Critical Issues**: 0
- **High Issues**: 1
- **Total Findings**: 10
- **Estimated Remediation Effort**: 32 hours

## Findings

### High
1. **God Class & SRP Violation: `ResultsTable.cpp`**
   - **Location**: `src/ui/ResultsTable.cpp`
   - **Impact**: High cognitive complexity. Class handles too many responsibilities: ImGui table rendering, keyboard navigation logic, bulk delete operations, file marking, path truncation, and context menus.
   - **Refactoring Suggestion**: Use the **Bridge pattern** or simply decompose into smaller classes: `ResultsRenderer`, `ResultsInteractionHandler`, `ResultsActions`.
   - **Severity**: High
   - **Effort**: Large (12+ hours)

### Medium
1. **Leaky Abstraction in `FolderCrawler.cpp`**
   - **Location**: `src/crawler/FolderCrawler.cpp`
   - **Impact**: Direct usage of Windows `HANDLE` and `WIN32_FIND_DATAW` makes the crawler hard to test and less portable.
   - **Refactoring Suggestion**: Use an **Adapter pattern** for file system enumeration. Extract a `FileSystemIterator` interface that can be implemented using `std::filesystem::directory_iterator` for Linux/macOS and optimized Win32 API for Windows.
   - **Severity**: Medium
   - **Effort**: Medium (6 hours)

2. **Tight Coupling: `SearchWorker` and `StreamingResultsCollector`**
   - **Location**: `src/search/SearchWorker.cpp`
   - **Impact**: `SearchWorker` directly manages the lifecycle and internal state of `StreamingResultsCollector`.
   - **Refactoring Suggestion**: Use **Dependency Injection**. The collector should be provided to the worker or managed via a factory.
   - **Severity**: Medium
   - **Effort**: Medium (4 hours)

3. **Inconsistent Synchronization in `SearchWorker`**
   - **Location**: `src/search/SearchWorker.cpp`
   - **Impact**: Mixes atomic operations with mutex-protected state. While currently correct, it increases the risk of race conditions during future modifications.
   - **Refactoring Suggestion**: Standardize on either full mutex protection for all related state or a more robust lock-free state machine if performance justifies it.
   - **Severity**: Medium
   - **Effort**: Medium (4 hours)

### Low
1. **Utility Grab-bag in `SearchResultUtils.cpp`**
   - **Location**: `src/search/SearchResultUtils.cpp` (~1063 lines)
   - **Impact**: Low cohesion. Functions for sorting, total size computation, and metadata/filter-cache logic are mixed.
   - **Refactoring Suggestion**: Split into `SearchResultSorter`, `TotalSizeCalculator`, and metadata/filter module (see plan).
   - **Plan**: See `2026-02-18_SEARCH_RESULT_UTILS_SPLIT_PLAN.md` — relevance (optional, do after higher-priority refactors), suggested file split, shared helpers, steps, and testing.
   - **Severity**: Low
   - **Effort**: Medium (4 hours)

## Architecture Health Score: 8/10
The architecture is generally strong, especially the search engine's SoA design and the use of interfaces like `ISearchableIndex`. The multi-threaded design is well-reasoned with clear separation between orchestration (`SearchWorker`) and execution (`ParallelSearchEngine`).

## Top 3 Systemic Issues
1. **Lack of Platform Abstraction** for file system operations in the crawler.
2. **UI Logic Bloat**: Large UI classes handling both rendering and complex state transitions.
3. **Template/Header Bloat**: Performance-critical code is heavily inlined in headers, increasing compilation times.

## Recommended Refactorings
1. **Decompose `ResultsTable`** (High Impact / Large Effort)
2. **Abstract File System Enumeration** (High Impact / Medium Effort)
3. **Formalize Search Pipeline** as a set of distinct stages (Filter -> Sort -> Decorate).

## Threading Model Assessment
The current model (Worker thread + Thread Pool) is highly sustainable and scales well with hardware concurrency. The use of `std::future` and `shared_mutex` is appropriate for the workload (heavy reads, occasional index updates).
