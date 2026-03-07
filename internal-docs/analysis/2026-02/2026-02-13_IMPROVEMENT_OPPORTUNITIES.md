# Improvement Opportunities

**Date:** 2026-02-13  
**Purpose:** Consolidated list of improvement opportunities across the project, derived from backlog, reviews, and codebase analysis.

**Review (2026-02-13):** Marked Done: #5 (DRY), #6 (exception handling), #13 (Show All parallel), #14 (d_type), #15 (CI tests), #18 (README table). Marked Obsolete: #20 (TESTING.md not found). Partial: #17 (clang-tidy present, clang-format not added).

---

## Critical / Security

| # | Item | Source | Effort | Notes |
|---|------|--------|--------|-------|
| 1 | **ReDoS vulnerability** – User-provided regex via `rs:` prefix uses backtracking engine; malicious patterns can hang search thread | Tech debt review, Security review | 8 h | Integrate RE2 or add strict complexity limits/timeouts |
| 2 | **PathStorage fixes** – Apply defensive fixes (not just assertions) for GetPath bounds and UpdatePrefix buffer overread | [2026-02-13_CODE_REVIEW_POTENTIAL_BUGS.md](2026-02-13_CODE_REVIEW_POTENTIAL_BUGS.md) | S | Assertions added; consider runtime checks for release builds |

---

## Code Quality

| # | Item | Source | Effort | Notes |
|---|------|--------|--------|-------|
| 3 | **Naming convention audit** – Systematic rename: constants to kPascalCase, members to snake_case_ (e.g. FileEntry, UsnMonitor) | Backlog P1, Tech debt | M (~4–24 h) | ~1600 NOLINT directives; improves consistency and tooling |
| 4 | **Application decomposition** – Extract SearchManager, IndexManager, WindowCoordinator from Application "God Object" | Tech debt, Architecture review | L (16 h) | 23+ fields; improves modularity and testability |
| 5 | **DRY LoadBalancingStrategy** – Replace duplicated ProcessChunkRange lambdas with shared ProcessChunkRangeForSearch | Backlog P2 | Done | Completed 2026-02-13: Static extracted to ExecuteStaticStrategyTask; Hybrid/Dynamic/Interleaved already use extracted executors |
| 6 | **Exception handling in search lambdas** – Wrap strategy lambdas in try-catch; return empty results on exception | Backlog P0 | Done | ProcessChunkWithExceptionHandling wraps all chunk processing; logs and continues on exception |
| 7 | **Remove redundant NOLINT** – Many NOLINT reference globally disabled checks | Tech debt | S (4 h) | Use `scripts/analyze_clang_tidy_warnings.py` |
| 8 | **Add [[nodiscard]]** – On fallible methods (GetEntry, GetPath, etc.) | Tech debt | S (2 h) | Prevents ignored return values |
| 9 | **GetExtensionView underflow** – Add `ext_off > path_len` guard | Code review | S | Prevents unsigned underflow and OOB string_view |

---

## Performance

| # | Item | Source | Effort | Notes |
|---|------|--------|--------|-------|
| 10 | **ExtensionMatches heterogeneous lookup** – Use transparent hasher to avoid std::string allocation per check | Backlog P1 | M | HIGH impact, LOW complexity; hot path optimization |
| 11 | **Post-processing: reduce map lookups** – Include size/time in SoA or bulk-retrieve to avoid O(N) GetEntry per result | Backlog P2 | L | High impact at 500k+ results |
| 12 | **Regex cache LRU** – Add eviction to ThreadLocalRegexCache | Backlog P2 | M | Prevents unbounded memory growth |
| 13 | **Parallelize "Show All" path** – RunShowAllPath serial; use match-everything predicate for large indices | Backlog P2 | Done | RunShowAllPath calls RunFilteredSearchPath → SearchAsyncWithData (parallel); empty matchers = match everything |
| 14 | **macOS folder crawl: use d_type** – Avoid lstat when d_type != DT_UNKNOWN | Backlog P2 | Done | Already implemented in FolderCrawler.cpp (macOS + Linux); lstat only when DT_UNKNOWN |

---

## Developer Experience

| # | Item | Source | Effort | Notes |
|---|------|--------|--------|-------|
| 15 | **CI: run tests** – GitHub workflow builds but does not run tests (`BUILD_TESTS=OFF`) | .github/workflows/build.yml | Done | Fixed 2026-02-13: BUILD_TESTS=ON + ctest step on all platforms |
| 16 | **CI: run on push/PR** – Workflow is manual only; uncomment push/PR triggers for automated checks | .github/workflows/build.yml | S | Uses more Actions minutes |
| 17 | **Pre-commit hook** – Clang-tidy runs on commit; consider adding clang-format | scripts/pre-commit-clang-tidy.sh | Partial | clang-tidy present; clang-format not added |
| 18 | **README test table** – Add missing tests (search_context_tests, index_operations_tests, etc.) | README.md | Done | Fixed 2026-02-13: Table now lists all 25 test targets |
| 19 | **build_tests_macos.sh** – Consider equivalent for Linux/Windows | scripts/ | M | macOS has convenience script; others use raw cmake/ctest |

---

## Documentation

| # | Item | Source | Effort | Notes |
|---|------|--------|--------|-------|
| 20 | **TESTING.md** – Update to reflect doctest migration (if exists) | Comprehensive review | Obsolete | TESTING.md not found; README has test instructions; doctest is documented in CMake |
| 21 | **Docs reorganization** – Finish internal vs public split; move prompts/historical to maintainer-only location | Backlog P3 | M | Improves open-source readiness |
| 22 | **Simplified UI tooltip** – Clarify forced settings in Simplified UI mode | UX review | S | User transparency |
| 23 | **DOCUMENTATION_INDEX.md** – Ensure all new docs are indexed | Production checklist | S | Ongoing maintenance |

---

## Testing & Robustness

| # | Item | Source | Effort | Notes |
|---|------|--------|--------|-------|
| 24 | **Lazy attribute regression prevention** – Strengthen unit tests (success vs failure caching, MFT contract) | Backlog P2 | M | Reduces risk of re-introducing bugs |
| 25 | **PathStorage edge-case tests** – Invalid idx, short paths, long prefixes | Code review | S | Validates assertions and future fixes |
| 26 | **UI integration tests** – Expand coverage for state transitions and search orchestration | Comprehensive review | M | Currently limited |
| 27 | **Sonar: 0 open issues** – Verify FolderCrawler S6004 NOSONAR; re-run Sonar | Backlog P0 | S | 1 issue may remain |

---

## Platform & Features

| # | Item | Source | Effort | Notes |
|---|------|--------|--------|-------|
| 28 | **Linux parity** – Full Linux support; investigate inotify for real-time monitoring | Backlog P3, Comprehensive review | L | Partially present |
| 29 | **BasicFinder for macOS** – Fallback for users without admin rights | Backlog P3 | M | Aligns with Windows no-elevation |
| 30 | **UsnMonitor race condition** – Fix std::future_error crash during init failure | Comprehensive review | S | Stability |

---

## Quick Wins (≤ 2 hours each)

- **#8** – Add [[nodiscard]] to key methods  
- **#9** – GetExtensionView underflow guard  
- **#22** – Simplified UI tooltip  
- **#27** – Sonar verification  

---

## Suggested Execution Order

1. **P0:** Sonar verification (#27), PathStorage runtime fixes (#2)
2. **P1:** ReDoS mitigation (#1), ExtensionMatches optimization (#10), naming audit start (#3)
3. **P2:** Quick wins (#8, #9)
4. **P3:** Application decomposition (#4), docs reorganization (#21)

---

## References

- [Prioritized Remaining Work Backlog](plans/2026-02-02_PRIORITIZED_REMAINING_WORK_BACKLOG.md)
- [Production Readiness Checklist](plans/production/PRODUCTION_READINESS_CHECKLIST.md)
- [Tech Debt Review 2026-02-09](review/2026-02-09-v1/TECH_DEBT_REVIEW_2026-02-09.md)
- [Comprehensive Review Summary 2026-02-09](review/2026-02-09-v1/COMPREHENSIVE_REVIEW_SUMMARY_2026-02-09.md)
- [Code Review Potential Bugs 2026-02-13](2026-02-13_CODE_REVIEW_POTENTIAL_BUGS.md)
