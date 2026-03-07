# Tech Debt Review - 2026-02-20

## Executive Summary
- **Health Score**: 7/10
- **Critical Issues**: 0
- **High Issues**: 2
- **Total Findings**: 15
- **Estimated Remediation Effort**: 40 hours

## Findings

### Critical
*None found during this review.*

### High
1. **God Class: `GuiState` (62 fields)**
   - **Location**: `src/gui/GuiState.h`
   - **Risk**: Violates Single Responsibility Principle. Managing 62 fields in a single class makes it extremely hard to maintain, test, and reason about. It also leads to many `NOLINT` suppressions.
   - **Suggested Fix**: Decompose `GuiState` into smaller, focused structs (e.g., `SearchInputState`, `ResultsDisplayState`, `IndexBuildState`, `GeminiState`).
   - **Severity**: High
   - **Effort**: 16 hours

2. **Large File: `PathPatternMatcher.cpp` (1498 LOC)**
   - **Location**: `src/path/PathPatternMatcher.cpp`
   - **Risk**: Extremely high cognitive complexity. Hard to test individual components of the matching logic.
   - **Suggested Fix**: Split into multiple files based on matching strategy (e.g., `WildcardMatcher`, `RegexMatcher`, `ExtensionMatcher`).
   - **Severity**: High
   - **Effort**: 12 hours

### Medium
1. **Raw HANDLE usage in USN Monitor**
   - **Location**: `src/usn/UsnMonitor.cpp:293, 387, 392`
   - **Risk**: Potential handle leaks if exceptions are thrown or early returns are added without proper cleanup.
   - **Suggested Fix**: Wrap raw `HANDLE` in `ScopedHandle` RAII wrapper (which already exists in the project).
   - **Severity**: Medium
   - **Effort**: 2 hours

2. **Large File: `ResultsTable.cpp` (1290 LOC)**
   - **Location**: `src/ui/ResultsTable.cpp`
   - **Risk**: Mixes UI rendering logic with complex keyboard navigation and dired-style marking logic.
   - **Suggested Fix**: Extract marking logic and keyboard navigation into separate helper classes.
   - **Severity**: Medium
   - **Effort**: 8 hours

### Low
1. **`const std::string&` parameters that could be `std::string_view`**
   - **Location**: `src/core/Settings.cpp:589` (WriteSettingsToPath)
   - **Risk**: Unnecessary overhead when passing string literals or substrings.
   - **Suggested Fix**: Change to `std::string_view`.
   - **Severity**: Low
   - **Effort**: 1 hour

2. **Naming Convention Violations in `GuiState`**
   - **Location**: `src/gui/GuiState.h`
   - **Risk**: Inconsistent naming makes the codebase feel less professional and harder for new developers to learn.
   - **Suggested Fix**: Standardize on `snake_case_` for member variables as per project guidelines, or document why `camelCase` is preferred for UI state.
   - **Severity**: Low
   - **Effort**: 4 hours

## Quick Wins
1. **Wrap raw HANDLEs in `UsnMonitor.cpp` with `ScopedHandle`** (High impact for reliability, low effort).
2. **Convert `WriteSettingsToPath` to use `std::string_view`**.
3. **Add `[[nodiscard]]` to pure query functions in `FileIndex`**.

## Recommended Actions
1. **Phase 1**: Wrap all remaining raw handles in RAII.
2. **Phase 2**: Start decomposing `GuiState` by grouping related fields into nested structs.
3. **Phase 3**: Break down `PathPatternMatcher.cpp` into smaller, strategy-based modules.

---
- **Debt ratio estimate**: ~15%
- **Systematic patterns**: `GuiState` is used as a global-like dumping ground for all UI-related state.
