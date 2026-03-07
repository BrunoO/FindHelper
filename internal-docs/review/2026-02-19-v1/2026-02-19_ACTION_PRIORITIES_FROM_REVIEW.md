# 2026-02-19 — Action Priorities from Latest Review

From the comprehensive review merged in commit `124e0a8` (PR #106). Focus: **what to act upon now**.

---

## 1. Critical (single item) — do first

| Item | Where | Action | Effort |
|------|--------|--------|--------|
| **Missing UsnMonitor tests** | `src/usn/UsnMonitor.cpp` | Add `UsnMonitorTests.cpp`: mock `DeviceIoControl` or simulated USN stream; verify record processing and threading. Only component with **Critical** finding; regressions can cause index corruption or hangs. | Large |

---

## 2. High impact + security — next

| Item | Where | Action | Effort |
|------|--------|--------|--------|
| **ReDoS (regex DoS)** | `StdRegexUtils.h`, `SearchWorker.cpp` | User-crafted regex (e.g. `(a+)+$`) can hang search. Use engine with linear-time guarantee (e.g. RE2) or add execution time limits for `std::regex`. | Medium |
| **Font loading command injection (Linux)** | `FontUtils_linux.cpp:32` | Sanitize `font_name` before `popen`, or use Fontconfig C API instead of CLI. | Small |

---

## 3. Quick wins (high value, low effort) — do soon

| Item | Where | Action | Effort |
|------|--------|--------|--------|
| **`[[nodiscard]]` on status-returning APIs** | `FileOperations.h`, `UsnMonitor`, `FileIndex` | Add `[[nodiscard]]` to bool/HRESULT/handle-returning functions so ignored errors are caught at compile time. Start with `FileOperations.h`. | ~4 h total |
| **Prune commented-out code** | `UsnMonitor.cpp` | Remove or justify commented-out code (Sonar S125). | Small |
| **Shortcut discoverability (UX)** | Results table / toolbar | Add small legend or tooltip for dired-style shortcuts (e.g. m, u, t, Shift+D) when a row is selected. | Small |

---

## 4. High impact, larger refactors — plan and phase

| Item | Where | Action | Effort |
|------|--------|--------|--------|
| **ResultsTable “God class”** | `ResultsTable.cpp` (1200+ lines, complexity 74) | Phase 1: extract keyboard/shortcut handling (`ResultsTableKeyboardHandler`). Phase 2: extract actions (`ResultsTableActionHandler`) and renderer. | 16 h (phase 1 ~8 h) |
| **PathPatternMatcher size** | `PathPatternMatcher.cpp` (~1500 lines) | Split into core matching vs pattern parsing/optimization. | 12 h |
| **Tight coupling FileIndex ↔ ParallelSearchEngine** | `FileIndex.h` | Introduce a clear search-execution interface that does not depend on full `FileIndex`; improves testability. | Large |

---

## 5. Medium priority — schedule when capacity allows

- **USN transient errors**: Standardize exponential backoff in `HandleReadJournalError` (UsnMonitor).
- **Folder stats allocations**: Reduce O(N) string allocations in `BuildFolderStatsIfNeeded` (e.g. `string_view` keys or interning).
- **C-style casts**: Replace with `static_cast`/`reinterpret_cast` in `UsnRecordUtils.cpp`.
- **Concurrency strategy**: Document high-level threading model for maintainers.

---

## 6. Environment / tooling

- **Clang-Tidy**: 2026-02-19 run failed (segfault with clang-tidy v18.1.3). Re-run when environment/version is fixed; consider `analyze_clang_tidy_warnings.py` or different clang-tidy version.

---

## Suggested order for “act upon now”

1. **Quick wins**: `[[nodiscard]]` (start with `FileOperations.h`), prune commented code in `UsnMonitor.cpp`, shortcut tooltips.
2. **Security**: ReDoS mitigation, then font loading sanitization on Linux.
3. **Critical**: Design and add UsnMonitor tests (mock or simulated USN).
4. **Refactor**: Phase 1 of ResultsTable (extract keyboard handler).

This order delivers fast safety and UX improvements, then addresses the single critical gap and the largest maintainability risk.
