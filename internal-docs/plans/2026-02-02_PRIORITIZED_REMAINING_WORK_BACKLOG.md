# Prioritized Remaining Work Backlog

**Date:** 2026-02-02  
**Last status update:** 2026-02-02 (codebase verified)  
**Purpose:** Single prioritized backlog of remaining work derived from existing docs (public `docs/` and maintainer plans, reviews, and prompts).  
**Sources:** Production checklist, Sonar plan, tech debt review, performance review, multiplatform coherence review, security status, feature ideas, NEXT_STEPS, PRIORITIZED_DEVELOPMENT_PLAN, hot path analysis, lazy attribute loading, folder crawl research, task prompts.

---

## Status summary (as of 2026-02-02)

**Verified done (removed from active backlog):**

- **P0 #2 – Remove LoadSettings from search hot path:** Implemented (verified 2026-02-06). Application passes `&settings` via SearchController → SearchWorker → FileIndex; FileIndex uses `optional_settings` and does not call LoadSettings when provided; ParallelSearchEngine uses `context.load_balancing_strategy` and `context.search_thread_pool_size` only. No file I/O in search hot path when settings are passed.
- **P1 #7 – Hide Metrics unless --show-metrics:** Implemented. `CommandLineArgs::show_metrics`, `Application::metrics_available_` from `cmd_args.show_metrics`; Metrics button and window gated by `metrics_available` in FilterPanel and UIRenderer.
- **P1 #9 – [[nodiscard]] on FileIndexStorage:** Implemented. `RenameLocked` and `MoveLocked` in `FileIndexStorage.h` are marked `[[nodiscard]]`.
- **P1 #5 – macOS Step 2 (full search):** Implemented. `main_mac.mm` delegates to `RunApplication`; shared `Application` / `ApplicationLogic` / `UIRenderer` provide full search; macOS build includes `UIRenderer.cpp` and `SearchResultUtils.cpp` in CMakeLists.txt.
- **P1 #6 – Windows run without elevation:** Implemented. `resources/windows/app.manifest` uses `level="asInvoker"`; SECURITY_MODEL.md documents standard-user default and folder crawl.
- **P0 #4 – Thread pool error handling:** Implemented. `SearchThreadPool::Enqueue()` logs `LOG_WARNING_BUILD` when `shutdown_` and returns invalid future; worker loop in `SearchThreadPool.cpp` catches and logs exceptions (bad_alloc, runtime_error, exception, ...).
- **P2 #14 – Hot path: cache HasExtensionFilter():** Implemented. `ParallelSearchEngine.h` and `.cpp` use `const bool has_extension_filter = context.HasExtensionFilter()` outside the loop and pass it into `MatchesExtensionFilter`.
- **P2 #15 – Hot path: cancellation check:** Implemented. `ParallelSearchEngine.h` uses `kCancellationCheckMask = kCancellationCheckInterval - 1` and `(items_checked & kCancellationCheckMask) == 0U` (bitwise, no modulo).
- **Sonar Phase 3 (UIRenderer const):** Addressed with `// NOSONAR(cpp:S5350)` on lines 44–45 in UIRenderer.cpp (MetricsWindow::Render requires non-const).
- **Sonar Phase 5 (LazyAttributeLoader init-statement):** Implemented. `LazyAttributeLoader.cpp` lines 353 and 378 use `if (FileAttributes attrs = LoadAttributes(path); attrs.success)`.

**Still to verify with Sonar/CI:** Sonar Phases 1 (TestHelpers.h nesting S134), 2 (FileIndexSearchStrategyTests exception S1181), 4 (ParallelSearchEngineTests CTAD S6012). Re-run Sonar analysis to confirm 0 open issues.

---

## P0 – Critical / Quality gate & production blockers

Work that blocks “production ready” or a clean quality gate (Sonar 0, tests green, no known critical debt).

| # | Item | Source | Effort | Notes |
|---|------|--------|--------|-------|
| 1 | **Sonar: 0 open issues** – Verify remaining Sonar fixes; Phases 3 & 5 done (UIRenderer NOSONAR, LazyAttributeLoader init-statement). Phases 1, 2, 4: TestHelpers.h nesting (S134), FileIndexSearchStrategyTests exception (S1181), ParallelSearchEngineTests CTAD (S6012) | [2026-02-02_SONAR_ZERO_OPEN_ISSUES_PLAN.md](2026-02-02_SONAR_ZERO_OPEN_ISSUES_PLAN.md) | M | Re-run Sonar and `scripts/build_tests_macos.sh` to confirm 0 open issues. |
| 2 | ~~**Remove LoadSettings from search hot path**~~ | — | — | **DONE (2026-02-06).** Settings passed via SearchController → SearchWorker → FileIndex; no LoadSettings in hot path when settings provided. |
| 3 | **Exception handling in search lambdas** – Add try-catch in all three strategy lambdas in FileIndex::SearchAsyncWithData; log with LOG_ERROR_BUILD, return empty results on exception | PRIORITIZED_DEVELOPMENT_PLAN #2, Production checklist | S (~1 h) | Thread pool worker catches exceptions; futures may still hold exception. Wrapping inside strategy lambdas would return empty vector and avoid exception propagation to callers. |

---

## P1 – High priority (feature parity, major improvements)

High impact, clear scope; enables workflows or removes significant debt.

| # | Item | Source | Effort | Notes |
|---|------|--------|--------|-------|
| 4 | **Naming convention audit** – Systematic rename: constants to kPascalCase, member variables to snake_case_ (e.g. UsnMonitor.h, Logger.h and elsewhere) | Tech debt review | M (~4 h) | Improves consistency and tooling. |
| 5 | **Hot path: ExtensionMatches allocations** – Use heterogeneous lookup for extension_set (e.g. boost::unordered_set or transparent hasher) to avoid std::string allocation per check in tight loop | Hot path optimization review (§2.1), Search performance optimizations | M | HIGH impact, LOW complexity; reduces allocations in inner loop. |

---

## P2 – Medium priority (performance, robustness, polish)

Valuable improvements; can be scheduled after P0/P1.

| # | Item | Source | Effort | Notes |
|---|------|--------|--------|-------|
| 6 | **Post-processing: reduce map lookups** – Include basic metadata (size, time) in SoA or bulk-retrieve in ConvertSearchResultData to avoid O(N) GetEntry(id) per result | Performance review | L | High impact at scale (e.g. 500k results); may require index layout change. |
| 7 | **Regex cache LRU** – Add LRU eviction to ThreadLocalRegexCache to bound memory when many unique regexes are used | Performance review | M | Prevents unbounded regex cache growth. |
| 8 | **Lazy attribute loading: regression prevention** – Implement Option 1 (strengthen unit tests: success vs failure caching, MFT contract, zero-byte file); Option 2 (document semantics in LazyAttributeLoader, InitialIndexPopulator, FileSystemUtils); Option 3 (pre-merge checklist for touched files) | [2026-02-02_LAZY_ATTRIBUTE_LOADING_REGRESSION_PREVENTION.md](../analysis/2026-02-02_LAZY_ATTRIBUTE_LOADING_REGRESSION_PREVENTION.md) | M | Reduces risk of re-introducing past bugs (wrong 0 caching, success vs failure). |
| 9 | **Parallelize “Show All” path** – RunShowAllPath currently serial (ForEachEntry); use ParallelSearchEngine with “match everything” predicate for large indices | Hot path review (§2.5) | M | HIGH impact for empty query on 10M+ files. |
| 10 | **Multi-platform coherence (optional)** – (DONE) Add #endif comments for platform blocks in shared files (Application.cpp, Logger.h, PathUtils.cpp); (DONE) add docs/platform/macos/; (DONE) move FileOperations.h to platform/FileOperations.h and update includes/docs | [2026-02-02_MULTIPLATFORM_COHERENCE_REVIEW.md](../review/2026-02-02_MULTIPLATFORM_COHERENCE_REVIEW.md) | S–M | Completed: improves readability, parity, and clarifies shared FileOperations contract location. |
| 11 | **macOS folder crawl: use d_type** – In FolderCrawler macOS branch, use readdir d_type when != DT_UNKNOWN to avoid lstat per entry; call lstat only when d_type == DT_UNKNOWN | [2026-02-02_FOLDER_CRAWL_REVIEW_AND_OPTIMIZATION_RESEARCH.md](../optimization/2026-02-02_FOLDER_CRAWL_REVIEW_AND_OPTIMIZATION_RESEARCH.md) | M | Reduces syscalls on macOS crawl. |
| 12 | **DRY: LoadBalancingStrategy** – Replace ProcessChunkRange lambdas in Static/Hybrid/Dynamic with ProcessChunkRangeForSearch; use RecordThreadTiming and CalculateChunkBytes; reduce ~400 lines duplication | PRIORITIZED_DEVELOPMENT_PLAN #1 | M (2–3 h) | Maintenance and bug-fix consistency. |

---

## P3 – Lower priority / backlog (long-term, optional)

Nice-to-have, future enhancements, or lower ROI.

| # | Item | Source | Notes |
|---|------|--------|-------|
| 13 | **Privilege dropping Option 2** – Two-process model (unprivileged main process + privileged service for USN) for full privilege separation | [PRIVILEGE_DROPPING_STATUS.md](../security/PRIVILEGE_DROPPING_STATUS.md) | 1–2 weeks; major architecture. Option 1 (disable specific privileges) already implemented. |
| 14 | **SIMD is_deleted / is_directory scan** – Use AVX2 to scan 32 bytes at once in hot path for early skip | Hot path review (§3.1) | Moderate complexity; high impact in theory. |
| 15 | **PathPatternMatcher** – Continue new implementation; benchmark vs current; keep faster/cleaner design | FEATURE_IDEAS.md, feature ideas | Medium priority in feature ideas. |
| 16 | **Refactor UIRenderer monolith** – Decompose for maintainability; see UIRENDERER_DECOMPOSITION_PLAN | Feature ideas, refactoring plans | Improves structure; can follow macOS parity work. |
| 17 | **Saved searches + AI description** – Save AI description with search; new window or UI for saved searches; “classic” vs “AI” panel | FEATURE_IDEAS.md | Product UX. |
| 18 | **Linux cross-platform** – Full Linux support (build, test, behavior) | FEATURE_IDEAS.md, platform docs | Already partially present; bring to parity. |
| 19 | **BasicFinder for macOS / no-admin** – Fallback for users without admin rights | FEATURE_IDEAS.md | Aligns with Windows no-elevation story. |
| 20 | **Logger streaming interface** – Modern streaming-style logger (e.g. cppstories inspiration) | FEATURE_IDEAS.md | Low priority. |
| 21 | **Docs reorganization** – Finish internal vs public split; move prompts/historical/archive into maintainer-only location where appropriate | Docs reorganization plan | Improves open-source readiness. |
| 22 | **Clang-tidy: test files and remaining sources** – Zero clang-tidy warnings in test files (TestHelpers, MockSearchableIndex, etc.) and any remaining app sources if not already done | Prompts (CLEAN_CLANG_TIDY_*), analysis docs | **Current (2026-02-02):** 610 warnings, 112 clean, 86 with warnings in src/ (198 files). See docs/analysis/2026-02-02_CLANG_TIDY_WARNINGS_ASSESSMENT.md. Clean remaining files. |

---

## Execution order suggestion (P0 → P1)

1. **P0:** Verify Sonar 0 issues (Phases 1, 2, 4 if still open); then LoadSettings hot path (#2); then exception handling in search lambdas (#3).
2. **P1:** Naming convention audit (#4); ExtensionMatches heterogeneous lookup (#5).
3. **P2:** Pick by impact vs effort (e.g. lazy attribute tests + checklist (#8), Show All parallelization (#9), macOS d_type (#11), DRY LoadBalancingStrategy (#12)).

---

## Verification

- After Sonar-related changes: run `scripts/build_tests_macos.sh` and re-run Sonar analysis.
- After performance-related changes: use production checklist “Memory Leak Detection” and hot path benchmarks where applicable.
- After platform changes: build and test on all three platforms (Windows, macOS, Linux) per AGENTS document.

---

## References

- [PRODUCTION_READINESS_CHECKLIST.md](production/PRODUCTION_READINESS_CHECKLIST.md) – Quick and comprehensive checklists
- [2026-02-02_SONAR_ZERO_OPEN_ISSUES_PLAN.md](2026-02-02_SONAR_ZERO_OPEN_ISSUES_PLAN.md) – Sonar phases and locations
- [2026-02-02_MULTIPLATFORM_COHERENCE_REVIEW.md](../review/2026-02-02_MULTIPLATFORM_COHERENCE_REVIEW.md) – Platform coherence actions
- [PRIVILEGE_DROPPING_STATUS.md](../security/PRIVILEGE_DROPPING_STATUS.md) – Security options
- Maintainer plans (NEXT_STEPS, PRIORITIZED_DEVELOPMENT_PLAN) – Phase 2 and critical tasks
- Feature ideas / roadmap – Roadmap ideas
- docs/analysis (hot path, lazy attribute, search performance) – Optimization details
