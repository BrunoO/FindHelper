# 2026-01-30 FileIndex cpp:S1448 – Reduce Method Count from 44 to ≤35

## Context

- **Rule:** cpp:S1448 – Class has 44 methods, which is greater than the 35 authorized.
- **Goal:** Reduce FileIndex method count to ≤35 with minimal API churn and no behavior change.
- **References:** `docs/2026-01-31_FILEINDEX_REFACTORING_PLAN.md`, `docs/2026-01-31_OPEN_SONAR_ISSUES_PLAN.md`, `docs/plans/refactoring/FileIndex_God_Class_Analysis.md`.

---

## Strategy: Remove + Delegate + Merge (No New Abstractions for Callers)

| # | Change | Net methods | Call sites |
|---|--------|--------------|------------|
| 1 | Remove **FinalizeInitialPopulation**; callers use **RecomputeAllPaths** | -1 | InitialIndexPopulator, FolderCrawlerIndexBuilder, WindowsIndexBuilder, AppBootstrapCommon |
| 2 | Remove **GetLoadBalancingStrategy** (dead code; ParallelSearchEngine uses GetLoadBalancingStrategyFromSettings) | -1 | None (CMakeLists comment only) |
| 3 | Add **GetThreadPoolManager()**; remove GetSearchThreadPoolCount, SetThreadPool, ResetThreadPool | -2 | Application (SetThreadPool), SettingsWindow (ResetThreadPool), MetricsWindow (GetSearchThreadPoolCount) |
| 4 | Add **GetMaintenance()**; remove Maintain, GetMaintenanceStats | -1 | ApplicationLogic (Maintain x2), MetricsWindow (GetMaintenanceStats) |
| 5 | Merge **ForEachEntryWithPath** overloads into one (optional ids parameter) | -1 | CommandLineArgs, SearchWorker (if uses ids overload) |
| 6 | Add **GetPathAccessor()** returning object with GetPathView, GetPathsView, GetPathComponentsView; remove those 3 from FileIndex | -2 | SearchWorker (GetPathsView), SearchResultUtils (GetPathView); GetPathComponentsView call sites if any |
| 7 | Remove **GetEntryOptional**; callers use GetEntry(id) and check nullptr / copy | -1 | SearchWorker |

**Total:** -9 → FileIndex ends at 35 methods.

---

## Implementation Order

1. **1 + 2** – Remove FinalizeInitialPopulation and GetLoadBalancingStrategy (no new types).
2. **3** – GetThreadPoolManager() + remove 3 pool methods; update 3 call sites.
3. **4** – GetMaintenance() + remove Maintain, GetMaintenanceStats; update 3 call sites.
4. **5** – Merge ForEachEntryWithPath overloads (default parameter or single signature).
5. **6** – FileIndexPathAccessor (nested or detail) + GetPathAccessor(); remove GetPathView, GetPathsView, GetPathComponentsView; update call sites.
6. **7** – Remove GetEntryOptional; SearchWorker uses GetEntry.

---

## Verification

- Run `scripts/build_tests_macos.sh`: **build passes**. One test fails: `ParallelSearchEngineTests` "SearchAsync - case-sensitive search" (CHECK results.empty()) — likely pre-existing (case-insensitive filesystem on macOS); unrelated to FileIndex refactor.
- **Estimated method count:** 36–38 (Sonar may count overloads separately). If next Sonar run still reports >35, options: (1) Remove const overloads of GetThreadPoolManager, GetMaintenance, GetPathAccessor (saves 3); (2) One more extraction (e.g. attribute updater).
- No behavior change; call sites (including tests) updated to use RecomputeAllPaths, GetThreadPoolManager(), GetMaintenance(), GetPathAccessor(), GetEntry().
