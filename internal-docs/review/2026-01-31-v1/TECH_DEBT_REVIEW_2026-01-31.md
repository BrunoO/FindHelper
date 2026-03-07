# Tech Debt Review - 2026-01-31

## Executive Summary
- **Health Score**: 7/10
- **Critical Issues**: 0
- **High Issues**: 2
- **Total Findings**: 7
- **Estimated Remediation Effort**: 12 hours

## Findings

### High

#### 1. Platform-Specific Duplication in String Utilities
- **Code**: `src/platform/linux/StringUtils_linux.cpp` and `src/platform/macos/StringUtils_mac.cpp`
- **Debt Type**: Aggressive DRY / Code Deduplication
- **Risk explanation**: Identical stubs for `WideToUtf8` and `Utf8ToWide` exist in multiple files. If a fix or proper implementation is needed for non-Windows platforms, it must be applied in multiple places.
- **Suggested fix**: Move shared non-Windows stubs to a `src/platform/common/StringUtils_posix.cpp` or similar.
- **Severity**: High
- **Effort**: 1 hour

#### 2. Naming Convention Violation: snake_case Function
- **Code**: `src/index/IndexFromFilePopulator.cpp:12` - `bool populate_index_from_file(...)`
- **Debt Type**: Naming Convention Violations
- **Risk explanation**: Violates the project standard of `PascalCase` for functions. Inconsistent naming makes the codebase harder to navigate and maintain.
- **Suggested fix**: Rename to `PopulateIndexFromFile`.
- **Severity**: High
- **Effort**: 15 min

### Medium

#### 3. Large File: PathPatternMatcher.cpp
- **Code**: `src/path/PathPatternMatcher.cpp` (1527 lines)
- **Debt Type**: Maintainability Issues (God classes/files)
- **Risk explanation**: Very large files are harder to review, test, and maintain. This file contains complex regex and DFA/NFA logic.
- **Suggested fix**: Split into `PathPatternTokenizer.cpp`, `NfaBuilder.cpp`, and `DfaEngine.cpp`.
- **Severity**: Medium
- **Effort**: 4 hours

#### 4. Missed std::string_view Opportunities
- **Code**: `src/ui/ResultsTable.cpp:603` - `std::string ResultsTable::TruncatePathAtBeginning(const std::string& path, float max_width)`
- **Debt Type**: C++17 Modernization Opportunities
- **Risk explanation**: Passing `const std::string&` when only reading can cause unnecessary allocations if a `char*` or `std::string_view` is available at the call site.
- **Suggested fix**: Change parameter type to `std::string_view`.
- **Severity**: Medium
- **Effort**: 2 hours (across multiple files)

#### 5. Missing [[nodiscard]] on Critical Methods
- **Code**: `src/index/FileIndex.h` (multiple methods like `UpdateSize`, `LoadFileSize`)
- **Debt Type**: C++17 Modernization Opportunities
- **Risk explanation**: Ignoring return values of methods that indicate success/failure or return important state can lead to silent failures.
- **Suggested fix**: Add `[[nodiscard]]` to all methods that return state or status.
- **Severity**: Medium
- **Effort**: 2 hours

### Low

#### 6. Static State in Functions
- **Code**: `src/usn/UsnMonitor.cpp:560` - `static size_t push_count = 0;`
- **Debt Type**: C++ Technical Debt
- **Risk explanation**: Static variables inside functions make them non-reentrant and harder to test.
- **Suggested fix**: Move to class member or pass as parameter if possible.
- **Severity**: Low
- **Effort**: 30 min

#### 7. Raw reinterpret_cast for COM Interfaces
- **Code**: `src/platform/windows/DragDropUtils.cpp`
- **Debt Type**: Technical Debt (Windows API patterns)
- **Risk explanation**: Manual reference counting and raw casts are error-prone.
- **Suggested fix**: Use `Microsoft::WRL::ComPtr` if possible, though current implementation with `NOSONAR` is acceptable.
- **Severity**: Low
- **Effort**: 2 hours

## Quick Wins
1. Rename `populate_index_from_file` to `PopulateIndexFromFile` (15 min).
2. Add `[[nodiscard]]` to `FileIndex` methods (30 min).
3. Update `TruncatePathAtBeginning` to use `std::string_view` (15 min).

## Recommended Actions
1. **Immediate**: Fix naming convention violation in `IndexFromFilePopulator.cpp`.
2. **Short-term**: Consolidate POSIX string utility stubs to reduce duplication.
3. **Long-term**: Refactor `PathPatternMatcher.cpp` to split the complex logic into smaller, more manageable files.
