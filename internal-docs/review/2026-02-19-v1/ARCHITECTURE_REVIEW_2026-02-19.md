# Architecture Review - 2026-02-19

## Executive Summary
- **Health Score**: 8/10
- **Critical Issues**: 0
- **High Issues**: 2
- **Total Findings**: 12
- **Estimated Remediation Effort**: 32 hours

## Findings

### Critical
*None identified.*

### High
1. **God Class: ResultsTable.cpp (SRP Violation)**
   - **Location**: `src/ui/ResultsTable.cpp`
   - **Smell Type**: SOLID Principle Violations: SRP
   - **Impact**: Combines rendering, input handling, and business logic (e.g., file deletion confirmation). High cognitive complexity (74) makes it fragile.
   - **Refactoring Suggestion**: Extract `ResultsTableKeyboardHandler` and `ResultsTableActionHandler`.
   - **Effort**: Large

2. **Tight Coupling between FileIndex and ParallelSearchEngine**
   - **Location**: `src/index/FileIndex.h`
   - **Smell Type**: Coupling & Cohesion Smells: Tight Coupling
   - **Impact**: Circular dependencies are avoided via forward declarations and implementation in `.cpp`, but the classes are still highly interdependent, making isolated testing harder.
   - **Refactoring Suggestion**: Define a clearer interface for search execution that doesn't require full knowledge of `FileIndex`.
   - **Effort**: Large

### Medium
1. **Leaky Abstraction: Windows-specific types in headers**
   - **Location**: `src/platform/FileOperations.h` and others
   - **Smell Type**: Abstraction Failures: Leaky Abstraction
   - **Impact**: Use of `FILETIME` or `HRESULT` in supposedly platform-agnostic interfaces (even if typedef'd) complicates macOS/Linux implementations.
   - **Refactoring Suggestion**: Use `std::chrono` and custom error enums instead of Windows-native types.
   - **Effort**: Medium

2. **Lock Granularity: mutex_ in FileIndex**
   - **Location**: `src/index/FileIndex.h`
   - **Smell Type**: Threading & Concurrency Smells: Lock Granularity
   - **Impact**: A single `shared_mutex` protects most of the index. While it uses `shared_lock` for readers, write operations (Insert/Remove) block all readers, which can cause UI stutters during massive USN Journal updates.
   - **Refactoring Suggestion**: Use more granular locking or concurrent data structures for parts of the index.
   - **Effort**: Large

### Low
1. **Feature Envy: SearchResultsService**
   - **Location**: `src/search/SearchResultsService.cpp`
   - **Smell Type**: Coupling & Cohesion Smells: Feature Envy
   - **Impact**: Accesses many internal fields of `GuiState`.
   - **Refactoring Suggestion**: Move some logic into `GuiState` as methods or use a more formal state management pattern.
   - **Effort**: Medium

2. **Inconsistent use of explicit constructors**
   - **Location**: Multiple files
   - **Smell Type**: Maintainability Red Flags: Naming & Structure
   - **Impact**: Risk of accidental implicit conversions.
   - **Refactoring Suggestion**: Audit all single-argument constructors and mark as `explicit`.
   - **Effort**: Small

## Summary Requirements

- **Architecture Health Score**: 8/10. The project uses well-defined patterns (Facade, Strategy, SoA) and successfully abstracts platform differences.
- **Top 3 Systemic Issues**:
  1. Monolithic UI components (`ResultsTable`).
  2. Windows-centric type leakage in cross-platform code.
  3. Coarse-grained locking in the central index.
- **Recommended Refactorings**:
  1. Decompose `ResultsTable` (High impact).
  2. Transition from `FILETIME` to `std::chrono` (Medium impact, improves portability).
  3. Refactor search engine interface to reduce coupling (Medium impact, improves testability).
- **Testing Improvement Areas**: Mocking the file system and USN journal would allow for more comprehensive integration tests without relying on disk state.
- **Threading Model Assessment**: The SoA design combined with `shared_mutex` is highly efficient for search but could be improved for write concurrency. It is sustainable for current requirements.
