# Tech Debt Review - 2026-02-19

## Executive Summary
- **Health Score**: 7/10
- **Critical Issues**: 0
- **High Issues**: 4
- **Total Findings**: 15
- **Estimated Remediation Effort**: 40 hours

## Findings

### Critical
*None identified.*

### High
1. **God Class: ResultsTable.cpp** (src/ui/ResultsTable.cpp)
   - **Debt type**: Maintainability Issues
   - **Risk explanation**: Exceeds 1200 lines and handles rendering, navigation, keyboard shortcuts, and file actions. High cognitive complexity (74) makes it hard to modify without side effects.
   - **Suggested fix**: Decompose into `ResultsTableRenderer`, `ResultsTableActions`, and `ResultsTableInputHandler`.
   - **Effort**: 16 hours.

2. **Large Class: PathPatternMatcher.cpp** (src/path/PathPatternMatcher.cpp)
   - **Debt type**: Maintainability Issues
   - **Risk explanation**: Nearly 1500 lines. Handles complex path matching logic that is difficult to reason about in one file.
   - **Suggested fix**: Split into core matching logic and pattern parsing/optimization components.
   - **Effort**: 12 hours.

3. **Duplicated String Interning Logic** (src/index/FileIndexStorage.h)
   - **Debt type**: Aggressive DRY
   - **Risk explanation**: Custom interning logic is mixed with storage logic.
   - **Suggested fix**: Extract a `StringInterner` utility class.
   - **Effort**: 4 hours.

4. **Missing [[nodiscard]] on Critical API Functions** (Multiple files)
   - **Debt type**: C++17 Modernization
   - **Risk explanation**: Functions returning success/failure (bool/HRESULT) or resource handles often lack `[[nodiscard]]`, allowing callers to silently ignore errors.
   - **Suggested fix**: Systematically add `[[nodiscard]]` to all status-returning functions in `UsnMonitor`, `FileIndex`, and `FileOperations`.
   - **Effort**: 4 hours.

### Medium
1. **Raw Pointers in Performance-Critical Paths** (src/index/InitialIndexPopulator.cpp)
   - **Debt type**: C++ Technical Debt
   - **Risk explanation**: Use of `MftMetadataReader*` and other raw pointers complicates ownership reasoning, even if justified by performance in hot paths.
   - **Suggested fix**: Use `std::unique_ptr` where possible, or document non-owning semantics clearly.
   - **Effort**: 2 hours.

2. **Snake Case Functions** (Multiple files)
   - **Debt type**: Naming Convention Violations
   - **Risk explanation**: Inconsistent naming (e.g., `fetch_add`, `try_emplace` - though often standard library calls, some project functions use snake_case).
   - **Suggested fix**: Audit and rename non-standard snake_case functions to PascalCase.
   - **Effort**: 4 hours.

3. **C-Style Casts in Windows API Interop** (src/usn/UsnRecordUtils.cpp)
   - **Debt type**: C++ Technical Debt
   - **Risk explanation**: Frequent use of `(DWORD)` or `(char*)` instead of `static_cast` or `reinterpret_cast`.
   - **Suggested fix**: Replace with modern C++ casts.
   - **Effort**: 2 hours.

### Low
1. **TODO Comments** (Multiple files)
   - **Debt type**: Dead/Unused Code
   - **Risk explanation**: Several TODOs related to optimizations or edge cases remain in the codebase.
   - **Suggested fix**: Address or convert to tracked issues.
   - **Effort**: 4 hours.

2. **Unused Includes** (Multiple files)
   - **Debt type**: Dead/Unused Code
   - **Risk explanation**: Headers like `<cstdio>` or `<cstring>` are included in files where they might no longer be needed after refactoring.
   - **Suggested fix**: Run include-cleaner or manually prune.
   - **Effort**: 2 hours.

## Quick Wins
1. **Add `[[nodiscard]]` to `FileOperations.h`**: High impact for error safety with very low effort.
2. **Prune Commented-out Code in `UsnMonitor.cpp`**: Improves readability immediately.
3. **Fix Naming of `g_` globals**: Ensure all globals follow the `g_snake_case` convention.

## Recommended Actions
1. **Phase 1 (Immediate)**: Systematic addition of `[[nodiscard]]` and fixing naming convention violations.
2. **Phase 2 (Short-term)**: Refactor `ResultsTable.cpp` by extracting keyboard shortcut handling.
3. **Phase 3 (Medium-term)**: Decompose `PathPatternMatcher.cpp` and `ResultsTable.cpp` fully.

---
- **Debt ratio estimate**: ~5% of codebase
- **Top 3 "quick wins"**: Add `[[nodiscard]]`, Prune dead code, Standardize globals.
- **Top critical/high items**: Refactoring `ResultsTable.cpp` and `PathPatternMatcher.cpp`.
- **Areas with systematic patterns**: Consistent lack of `[[nodiscard]]` in platform-specific APIs.
