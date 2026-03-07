# 2026-01-30 FileIndex S1448 Refactoring vs. True Facade Goal

## Facade Goal (from project docs)

From `docs/plans/refactoring/FACADE_ANALYSIS.md` and `docs/2026-01-31_FILEINDEX_REFACTORING_PLAN.md`:

- **Single entry point:** Callers use `FileIndex` only; they do not hold references to internal components.
- **Hide subsystem complexity:** Implementation lives in components (IndexOperations, PathRecomputer, SearchThreadPoolManager, FileIndexMaintenance, etc.); FileIndex **delegates** via thin wrappers.
- **No business logic in Facade:** FileIndex coordinates and delegates; it does not implement path parsing, search context building, or pool lifecycle logic.
- **Constraint (refactoring plan):** “Prefer refactors that keep FileIndex as the **single facade** where possible, so Application, UsnMonitor, FolderCrawler, SearchWorker, etc. do not need to hold **multiple references**.”

---

## Compatibility of the S1448 Refactoring

### Compatible with Facade (keeps single entry point, hides components)

| Change | Why it’s compatible |
|--------|----------------------|
| **FinalizeInitialPopulation → RecomputeAllPaths** | Callers still use FileIndex only; same single method, same behavior. |
| **Remove GetLoadBalancingStrategy** | Unused; no surface change for Facade. |
| **Merge ForEachEntryWithPath** | Single method on FileIndex; callers still use `file_index.ForEachEntryWithPath(...)`. |
| **GetPathAccessor() + PathAccessor** | Callers use `file_index.GetPathAccessor().GetPathView(id)`. They never hold PathStorage or PathOperations; path access is a **grouped sub-interface** of the Facade. Aligns with Option E (IIndexRead could be implemented via this accessor). |
| **Remove GetEntryOptional; use GetEntry** | Single entry point (GetEntry); no component exposure. |

### In tension with Facade (exposes internal components)

| Change | Issue | Facade target |
|--------|--------|----------------|
| **GetThreadPoolManager()** instead of SetThreadPool / ResetThreadPool / GetSearchThreadPoolCount | Callers now do `file_index.GetThreadPoolManager().SetThreadPool(...)` and similar. They **hold a reference** to the internal component (SearchThreadPoolManager). The plan says Option C keeps “the same public search/pool API” and “remains the single facade” — i.e. thin wrappers on FileIndex, not exposing the manager. | True Facade: FileIndex keeps **SetThreadPool**, **ResetThreadPool**, **GetSearchThreadPoolCount** as thin wrappers that delegate to `thread_pool_manager_`; no **GetThreadPoolManager()** in the public API. |
| **GetMaintenance()** instead of Maintain / GetMaintenanceStats | Callers now do `file_index.GetMaintenance().Maintain()` and `file_index.GetMaintenance().GetMaintenanceStats()`. They **hold a reference** to FileIndexMaintenance. Option E lists **IIndexMaintenance: Maintain, GetMaintenanceStats** as methods on the facade, not “GetMaintenance()”. | True Facade: FileIndex keeps **Maintain()** and **GetMaintenanceStats()** as thin wrappers that delegate to `maintenance_`; no **GetMaintenance()** in the public API. |

---

## Conclusion

- **Most of the S1448 refactoring is compatible** with making FileIndex a true Facade: single entry point and hidden components are preserved for path access, iteration, entry access, and RecomputeAllPaths.
- **GetThreadPoolManager()** and **GetMaintenance()** **weaken** the Facade: they expose internal components and encourage “bypass” of the single surface (FileIndex). That conflicts with the stated goal of keeping FileIndex as the single facade and not giving callers multiple references.

---

## Recommendation for Facade alignment

To align with the ultimate “true Facade” goal:

1. **Reintroduce thin wrappers on FileIndex** (no new logic):
   - **Maintain()** → `return maintenance_.Maintain();`
   - **GetMaintenanceStats()** → `return maintenance_.GetMaintenanceStats();`
   - **SetThreadPool(pool)** → delegate to `thread_pool_manager_.SetThreadPool(...)` and refresh `search_engine_` as today.
   - **ResetThreadPool()** → `thread_pool_manager_.ResetThreadPool();`
   - **GetSearchThreadPoolCount()** → `return thread_pool_manager_.GetThreadPoolCount();`

2. **Remove or narrow exposure of components:**
   - **Option A (strict Facade):** Remove **GetMaintenance()** and **GetThreadPoolManager()** from the public API; callers use Maintain(), GetMaintenanceStats(), SetThreadPool(), ResetThreadPool(), GetSearchThreadPoolCount() again.
   - **Option B (pragmatic):** Keep **GetThreadPoolManager()** and **GetMaintenance()** for tests or advanced use, but **document** that the supported, Facade-compliant API is the thin wrappers (Maintain, GetMaintenanceStats, SetThreadPool, ResetThreadPool, GetSearchThreadPoolCount). New code should prefer the wrappers.

3. **S1448 method count:** Re-adding those wrappers increases FileIndex method count again. To satisfy both Facade and S1448 you can:
   - Rely on other reductions (path accessor, merged ForEachEntryWithPath, removed GetEntryOptional / FinalizeInitialPopulation / GetLoadBalancingStrategy) and accept a count slightly above 35, or
   - Add a single **NOSONAR** on the class with a short comment referencing Facade and the plan, or
   - Extract one more cohesive group behind a different accessor (without exposing raw component references) so the net count stays ≤35.

---

## Summary

| Refactoring | Facade compatible? |
|-------------|--------------------|
| FinalizeInitialPopulation → RecomputeAllPaths | Yes |
| Remove GetLoadBalancingStrategy | Yes |
| Merge ForEachEntryWithPath | Yes |
| GetPathAccessor() + PathAccessor | Yes (grouped sub-interface) |
| Remove GetEntryOptional | Yes |
| **GetThreadPoolManager()** (replace 3 wrappers) | **No** — exposes component |
| **GetMaintenance()** (replace 2 wrappers) | **No** — exposes component |

---

## Update: Facade Alignment Applied (2026-01-30)

- **Thin wrappers restored:** Maintain(), GetMaintenanceStats(), SetThreadPool(), ResetThreadPool(), GetSearchThreadPoolCount() are again the public API; they delegate to maintenance_ and thread_pool_manager_. GetMaintenance() and GetThreadPoolManager() were removed from the public API.
- **Call sites reverted:** Application, SettingsWindow, MetricsWindow, ApplicationLogic, and tests use the thin wrappers (file_index.Maintain(), file_index.SetThreadPool(...), etc.).
- **cpp:S1448:** NOSONAR(cpp:S1448) added on the FileIndex class declaration with the rationale that a high method count is acceptable for a Facade (single entry point delegating to components).
- FileIndex is now aligned with the true Facade–compliant design: single entry point, no public exposure of SearchThreadPoolManager or FileIndexMaintenance.
