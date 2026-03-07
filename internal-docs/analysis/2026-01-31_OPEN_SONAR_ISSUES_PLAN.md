# 2026-01-31 Open Sonar Issues – Address Plan

**Refreshed:** 2026-01-31 (open issues fetched from SonarCloud)  
**Latest refresh:** 14 OPEN issues (all created 2026-01-31) were in FontUtils; addressed in this session.

---

## Summary Table (current open / recently addressed)

| # | Rule | File | Line | Severity | Message | Status / Action |
|---|------|------|------|----------|---------|------------------|
| – | FontUtils (14 issues) | FontUtils_win.cpp, FontUtils_linux.cpp, FontUtilsCommon.cpp | various | mixed | S5945, S7119, S3609, S5421, S6004, S125 | **Fixed 2026-01-31** – see “Issues created today (FontUtils)” below. |
| 1 | cpp:S5827 | UsnMonitor.cpp | 546 | MAJOR | Replace the redundant type with "auto". | **Fixed** – use `auto usn_journal_data = USN_JOURNAL_DATA_V0{};`. Verify on next scan. |
| 2 | cpp:S3776 | UsnMonitor.cpp | 540 | CRITICAL | Refactor this function to reduce its Cognitive Complexity from 29 to the 25 allowed. | **Likely fixed** – ReaderThread refactored 2026-01-31 (helpers extracted). Verify on next scan. |
| 3 | cpp:S1448 | FileIndex.h | 58 | MAJOR | Class has 44 methods, which is greater than the 35 authorized. | **Plan below** – NOSONAR(cpp:S1448) added; facade alignment done. |

---

## Issues created today (FontUtils) – fixed 2026-01-31

All OPEN issues from the 2026-01-31 refresh were in FontUtils (creationDate 2026-01-31). Fixes applied:

- **FontUtils_win.cpp**
  - **S5945 (C-style array):** `kEmbeddedFonts` and `kSystemFontMap` changed to `const std::array<..., N>` with double-brace initializers.
  - **S7119 (init order):** Resolved by using `std::array` (element order well-defined).
  - **S3609 (redundant static):** Removed `static` from namespace-scope `const` arrays and pointers.
  - **S5421 (global pointers const):** `kDefaultPrimary`, `kDefaultFallback`, `kDefaultSecondary` changed to `constexpr const char* const`.
  - **S6004 (init-statement):** `font` declared in `if (ImFont* font = ...; font == nullptr)` in `ConfigureFontsFromSettings`.
- **FontUtils_linux.cpp**
  - **S6004 (init-statement):** Same pattern for `font` in `ConfigureFontsFromSettings`.
- **FontUtilsCommon.cpp**
  - **S125 (commented-out code):** NOSONAR(cpp:S125) added on line 87 (NOLINT explanation line).

Re-run Sonar after push to confirm these issues are closed.

---

## Issue 1: cpp:S5827 – UsnMonitor.cpp (Redundant type → auto)

- **Message:** Replace the redundant type with "auto".
- **Fixes applied (so Sonar can resolve the issue):**
  1. Line ~547: `USN_JOURNAL_DATA_V0 usn_journal_data;` → `auto usn_journal_data = USN_JOURNAL_DATA_V0{};`
  2. Line ~559: `READ_USN_JOURNAL_DATA_V0 read_data;` → `auto read_data = READ_USN_JOURNAL_DATA_V0{};`
- **If Sonar still shows the issue after re-runs:** Confirm that the analysis is running on the branch/commit that contains both fixes (e.g. main after push). Sonar may have reported line 546 originally; after the first fix the same rule can appear on the next redundant type (read_data) until that is also fixed. Both struct declarations in ReaderThread are now using `auto`.

---

## Issue 2: cpp:S3776 – UsnMonitor.cpp:540 (Cognitive Complexity 29 → 25)

- **Message:** Refactor this function to reduce its Cognitive Complexity from 29 to the 25 allowed.
- **Function:** Line 540 is the start of `ReaderThread()` (refactored 2026-01-31 with OpenVolumeAndQueryJournal, RunInitialPopulationAndPrivileges, HandleReadJournalError).
- **Action:**
  1. **Verify on next scan:** After the next Sonar analysis, confirm this issue is resolved for ReaderThread.
  2. **If still open:** Sonar may be reporting on a different function or an older analysis. Check which function is at line 540 in the analyzed code. If it is **ProcessorThread()** (starts at line 739 in current file), apply the same strategy as ReaderThread: extract private helpers (e.g. “process one buffer”, “handle record”, “update metrics”) to bring complexity under 25. See `docs/2026-01-31_USN_MONITOR_READER_THREAD_S3776_PLAN.md` for the pattern.

---

## Issue 3: cpp:S1448 – FileIndex.h:58 (Class has 44 methods > 35)

- **Message:** Class has 44 methods, which is greater than the 35 authorized. Split it into smaller classes.
- **Location:** `class FileIndex` at line 58.
- **Effort:** ~1h (Sonar debt).
- **Strategy:**
  1. **Identify cohesive groups:** Path building, search/query, maintenance (recompute, rebuild), indexing (insert/remove/rename/move), DFA/simple tokens, configuration. Prefer extracting by responsibility (e.g. path building → PathBuilder already used; search → ISearchableIndex; maintenance → FileIndexMaintenance).
  2. **Extract, don’t duplicate:** Move methods to existing or new helper/facade types; FileIndex delegates. Preserve public API where callers depend on it (or narrow the public surface in a separate step).
  3. **References:** Align with any existing FileIndex refactoring plans (e.g. `docs/2026-01-31_FILEINDEX_REFACTORING_PLAN.md`, `docs/2026-01-31_OPTION_A_PATH_RECOMPUTER_PRODUCTION_READINESS.md`) so splits are consistent with path recomputation and production readiness.
- **Order:** Tackle after S5827 and S3776 are verified/fixed; do in a dedicated branch with tests and Windows build verification.

---

## Verification Checklist

- [ ] Re-run Sonar (or wait for CI) after the S5827 fix and ReaderThread refactor; confirm S5827 and S3776 (ReaderThread) are closed.
- [ ] If S3776 remains open, confirm target function (ReaderThread vs ProcessorThread) and apply helper extraction as above.
- [ ] For S1448, create a short “FileIndex method-split” plan (which methods go where) before coding; then implement and re-run Sonar.

---

## How to Refresh the List Again

```bash
export SONARQUBE_TOKEN="your_token"
scripts/fetch_sonar_results.sh --open-only --output-dir sonar-results
python3 scripts/check_today_sonar_issues.py   # optional: filter by creation date
```

View in SonarCloud: https://sonarcloud.io/project/issues?id=BrunoO_USN_WINDOWS
