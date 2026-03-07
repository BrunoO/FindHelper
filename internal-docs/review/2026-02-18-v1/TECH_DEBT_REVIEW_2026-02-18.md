# Tech Debt Review - 2026-02-18

## Executive Summary
- **Health Score**: 7/10
- **Critical Issues**: 0
- **High Issues**: 2
- **Total Findings**: 15
- **Estimated Remediation Effort**: 24 hours

## Findings

### High
1. **God Class: `ResultsTable.cpp` (Maintainability)**
   - **Code**: `src/ui/ResultsTable.cpp` is 1251 lines long.
   - **Risk**: High complexity makes it difficult to add features or fix bugs without side effects. It handles UI rendering, keyboard navigation, bulk actions, and path truncation.
   - **Suggested fix**: Split into smaller components: `ResultsTableRenderer`, `ResultsTableNavigation`, `ResultsTableActions`.
   - **Severity**: High
   - **Effort**: 12 hours

2. **Raw `HANDLE` in `FolderCrawler.cpp` (Platform Debt)** — **FIXED**
   - **Code**: `src/crawler/FolderCrawler.cpp` (Windows `EnumerateDirectory`).
   - **Risk**: If an exception was thrown or an early return occurred before `FindClose(find_handle)`, the handle would leak.
   - **Fix applied**: Introduced file-local RAII: `FindHandleDeleter` and `UniqueFindHandle` (`std::unique_ptr<void, FindHandleDeleter>`). The find handle is now held by `UniqueFindHandle`, so `FindClose` runs on scope exit or on exception. Explicit `FindClose` call removed.
   - **Severity**: High (was)
   - **Effort**: ~1 hour (done)

### Medium
1. **Unnecessary String Copies (Modernization)** — **FIXED** (targeted locations)
   - **Code**: `src/crawler/FolderCrawler.cpp` (EnumerateDirectorySafe, ProcessEntry, ProcessEntries), `src/ui/ResultsTable.cpp` (TruncatePathAtBeginning).
   - **Risk**: Performance overhead from unnecessary allocations/copies.
   - **Fix applied**: Replaced `const std::string&` with `std::string_view` for read-only parameters in the above helpers and in `ResultsTable::TruncatePathAtBeginning`. Call sites now pass string_view (or implicit conversion from string) and avoid temporary strings where possible.
   - **Severity**: Medium (was)
   - **Effort**: ~1 hour (done for these locations)

2. **`std::vector` without `reserve()` (Performance)** — **Already addressed**
   - **Code**: `src/search/SearchResultUtils.cpp` (sort path), `src/ui/EmptyState.cpp` (recent-search UI).
   - **Risk**: Multiple reallocations when growing vectors in loops.
   - **Study (2026-02-18):** At the cited locations the code already uses `reserve()`:
     - **SearchResultUtils.cpp**: In the FolderFileCount/FolderTotalSize sort block, `keys.reserve(n)` (line ~910) and `sorted.reserve(n)` (line ~929) are called before the loops; `indices` is constructed with `(n)` so no reserve needed.
     - **EmptyState.cpp**: `recent_labels.reserve(recent_count)` and `button_widths.reserve(recent_count)` (lines 375–376) are called before the loop that pushes to them.
   - **Verdict**: No change required; finding is outdated. Optional low-priority follow-up: `cloud_files_to_load` in `ApplyTimeFilter` (SearchResultUtils.cpp ~337) is grown in a loop without reserve; size is typically small (cloud files only).
   - **Severity**: Medium (was)
   - **Effort**: 0 (already done)

3. **Inconsistent Naming in `FileEntry` (Naming)**
   - **Code**: `src/index/FileIndexStorage.h:45`: `uint64_t parentID`, `bool isDirectory`.
   - **Risk**: Violates project convention (should be snake_case_ for members). Currently uses camelCase and no trailing underscore.
   - **Suggested fix**: Rename to `parent_id_`, `is_directory_`. (Note: Existing comments mention backward compatibility, but this is still debt).
   - **Severity**: Medium
   - **Effort**: 2 hours

### Low
1. **Snake_case functions in `StringSearchAVX2.cpp`**
   - **Code**: `regex_match`, `regex_search`.
   - **Risk**: Minor inconsistency with `PascalCase` function convention.
   - **Suggested fix**: Rename to `RegexMatch`, `RegexSearch`.
   - **Severity**: Low
   - **Effort**: 0.5 hours

## Quick Wins
1. ~~**RAII for `find_handle` in `FolderCrawler.cpp`**~~: Done — prevents leaks on exception/early exit.
2. ~~**string_view for FolderCrawler helpers + TruncatePathAtBeginning**~~: Done — avoids unnecessary string copies at call sites.
3. **Add `[[nodiscard]]` to search functions**: Ensures error codes are checked.
4. ~~**Reserve vector capacity in SearchResultUtils.cpp**~~: Already present at cited locations (keys/sorted in sort path).

## Recommended Actions
1. **Priority 1**: ~~Implement RAII for find_handle in FolderCrawler.cpp~~ (done). Consider RAII for any other raw Windows HANDLEs in the codebase.
2. **Priority 2**: Refactor `ResultsTable.cpp` to separate concerns.
3. **Priority 3**: ~~Targeted string_view in FolderCrawler + ResultsTable::TruncatePathAtBeginning~~ (done). ~~Broader sweep~~ (done): see `2026-02-18_STRING_VIEW_SWEEP.md` — FileIndex::UpdatePrefixLocked, ExportToCsv, UsnMonitor helpers, EmptyState label, ShellContextUtils path.
