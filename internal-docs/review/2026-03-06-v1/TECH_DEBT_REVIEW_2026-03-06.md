# Tech Debt Review - 2026-03-06

## Executive Summary
- **Health Score**: 8/10
- **Critical Issues**: 0
- **High Issues**: 2
- **Total Findings**: 8
- **Estimated Remediation Effort**: 24 hours

## Findings

### Critical
None.

### High
1. **God Class: ResultsTable.cpp** (Category 7)
   - **Location**: `src/ui/ResultsTable.cpp` (~1200 lines)
   - **Risk**: High cognitive complexity and mixed responsibilities (rendering, keyboard handling, context menus, and logic for marking/deleting).
   - **Suggested Fix**: Decompose into smaller components: `ResultsTableRenderer`, `ResultsTableContextMenu`, and `ResultsTableSelectionLogic`.
   - **Effort**: Large (8+ hours)

2. **God Class: PathPatternMatcher.cpp** (Category 7)
   - **Location**: `src/path/PathPatternMatcher.cpp` (~1500 lines)
   - **Risk**: Extremely large file handling complex regex and wildcard logic. Hard to maintain and test in isolation.
   - **Suggested Fix**: Split into `WildcardMatcher`, `RegexMatcher`, and `PathPatternParser`.
   - **Effort**: Large (8+ hours)

### Medium
1. **Missing #pragma once** (Category 7)
   - **Location**: `src/platform/windows/resource.h`
   - **Risk**: Potential for multiple inclusion issues, though less critical in resource headers.
   - **Suggested Fix**: Add `#pragma once`.
   - **Effort**: Low (5 min)

2. **Large Type Pass-by-Value** (Category 5)
   - **Location**: `src/ui/SettingsWindow.cpp` (last_save_message parameter)
   - **Risk**: Unnecessary string copy.
   - **Suggested Fix**: Change to `std::string_view`.
   - **Effort**: Low (10 min)

3. **Inconsistent Header Organization** (Category 7)
   - **Location**: `src/utils/Logger.h` (~667 lines)
   - **Risk**: Header-heavy utility makes it slow to compile and harder to read.
   - **Suggested Fix**: Move implementation details to a `.cpp` or `.inl` file.
   - **Effort**: Medium (2 hours)

### Low
1. **Redundant const std::string&** (Category 4)
   - **Location**: `src/index/FileIndexStorage.h` (Intern functions)
   - **Risk**: Missed `std::string_view` modernization.
   - **Suggested Fix**: Use `std::string_view` if interning doesn't strictly require a string object until storage.
   - **Effort**: Low (15 min)

2. **Raw Pointer Usage in MFT Parsing** (Category 2)
   - **Location**: `src/index/mft/MftMetadataReader.cpp`
   - **Risk**: Manual offset calculation with raw pointers.
   - **Suggested Fix**: Use a span-like abstraction or structured access if possible, though MFT structures are often fixed-size.
   - **Effort**: Medium (4 hours)

3. **Commented-out Code Remnants** (Category 1)
   - **Location**: Various locations in `src/index/InitialIndexPopulator.cpp`
   - **Risk**: Reduces readability.
   - **Suggested Fix**: Remove old comments or convert to formal documentation.
   - **Effort**: Low (30 min)

## Quick Wins
1. Add `#pragma once` to `src/platform/windows/resource.h`.
2. Convert `last_save_message` in `SettingsWindow.cpp` to `std::string_view`.
3. Add `[[nodiscard]]` to missing utility functions in `StringUtils.h`.

## Recommended Actions
1. **Phase 1**: Quick wins and modernization (string_view/nodiscard).
2. **Phase 2**: Refactor `ResultsTable.cpp` to separate UI rendering from logic.
3. **Phase 3**: Decompose `PathPatternMatcher.cpp` into specialized matcher classes.
