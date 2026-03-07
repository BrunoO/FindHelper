# FileIndex Refactoring Plan: Reducing Responsibilities Without Performance Penalty

**Date:** 2026-01-31  
**Goal:** Reduce FileIndex's number of responsibilities and methods (break the "God" class) without introducing performance penalties or code duplication.

---

## 1. Current State Summary

### 1.1 Responsibilities Already Extracted (Good)

| Component | Responsibility | Used By |
|-----------|----------------|---------|
| **FileIndexStorage** | ID→metadata, StringPool, directory cache, CRUD at storage level | FileIndex, IndexOperations |
| **PathStorage** | SoA path buffer, path offsets, contiguous layout | FileIndex, PathOperations, FileIndexMaintenance |
| **PathOperations** | Path insert/view/update (wraps PathStorage) | FileIndex, IndexOperations |
| **IndexOperations** | Insert/Remove/Rename/Move coordination (storage + path) | FileIndex |
| **FileIndexMaintenance** | Defragmentation, cleanup, rebuild path buffer | FileIndex |
| **DirectoryResolver** | GetOrCreateDirectoryId | FileIndex::InsertPath |
| **LazyAttributeLoader** | Lazy size/modification time loading | FileIndex |
| **ParallelSearchEngine** | Parallel search execution | FileIndex::SearchAsync* |

### 1.2 Responsibilities Still in FileIndex (Candidates for Reduction)

| Area | Methods / Logic | LOC / Complexity |
|------|------------------|-------------------|
| **Path recomputation** | `RecomputeAllPaths()`, `FinalizeInitialPopulation()` | ~60 lines, OneDrive logic, path rebuild |
| **Search entry point** | `SearchAsync`, `SearchAsyncWithData`, `CreateSearchContext()` | Context building, settings, delegation to engine |
| **Thread pool lifecycle** | `GetThreadPool`, `CreateDefaultThreadPool`, `SetThreadPool`, `ResetThreadPool`, `GetSearchThreadPoolCount` | ~80 lines in .cpp |
| **Load balancing config** | `GetLoadBalancingStrategy()` static | Settings read, enum mapping |
| **Full-path insertion** | `InsertPath(std::string_view full_path)` | Parse path, resolve parent, assign ID, InsertLocked |
| **Public path/entry API** | Many thin wrappers (lock + delegate): GetPathView, GetPathsView, GetPathComponentsView, ForEachEntry, ForEachEntryWithPath, GetEntryOptional, UpdateSize, UpdateModificationTime, UpdateFileSizeById, LoadFileSize, GetFileSizeById, GetFileModificationTimeById | Facade only |
| **Type ownership** | `SearchStats`, `SearchResultData`, `ThreadTiming`, `PathComponentsView` | Nested in FileIndex, used by search/ and path/ |

### 1.3 Public Method Count (Approximate)

- **~45 public methods** (including ISearchableIndex), plus several nested types.
- Many are one-liners (lock + delegate); the "weight" is in RecomputeAllPaths, search/thread-pool logic, and type surface.

---

## 2. Design Constraints

- **No performance penalty:** Hot paths (search, path access, Insert/Remove) must keep same lock discipline and call depth; no extra allocations in loops.
- **No code duplication:** Shared logic (e.g. CreateSearchContext, path rebuild) must live in one place.
- **YAGNI + SOLID + KISS + DRY:** Prefer small, focused extractions; avoid speculative abstractions.
- **Call sites:** Prefer refactors that keep `FileIndex` as the single facade where possible, so Application, UsnMonitor, FolderCrawler, SearchWorker, etc. do not need to hold multiple references.

---

## 3. Recommended Options (Best Impact / Risk Balance)

### Option A: Extract Path Recomputer (RecomputeAllPaths) — **High value, low risk**

**Idea:** Move the entire path-rebuild and OneDrive-reset logic into a dedicated class used by FileIndex.

- **New class:** `PathRecomputer` (or `IndexPathRecomputer`) in `index/PathRecomputer.h/.cpp`.
- **Responsibilities:** Hold references to `FileIndexStorage&`, `PathStorage&`, `PathOperations&` (or minimal surface). Implement `RecomputeAllPaths()` (and optionally a small `FinalizeInitialPopulation()` wrapper that just calls it).
- **Logic to move:** Current body of `RecomputeAllPaths()`: clear path storage and directory cache, loop over storage, build path via `PathBuilder::BuildFullPathWithLogging`, `InsertPathLocked`, OneDrive sentinel reset. `PathRecomputer` does **not** take the lock; **FileIndex** continues to take `unique_lock` and then call `path_recomputer_.RecomputeAllPaths()`.
- **Performance:** Unchanged (same single lock, same work, no extra indirection in hot path).
- **Duplication:** None; path-rebuild logic exists only in PathRecomputer.
- **FileIndex:** Shrinks by ~55 lines; keeps `RecomputeAllPaths()` / `FinalizeInitialPopulation()` as thin wrappers (lock + delegate).

**Dependency:** PathRecomputer needs `PathBuilder`, `FileIndexStorage`, `PathStorage`, `PathOperations`, and OneDrive detection (can stay in FileIndex as a free function or move to PathRecomputer). `IsOneDriveFile` is already used in RecomputeAllPaths — can be moved to PathUtils or kept next to PathRecomputer.

---

### Option B: Move Search Types Out of FileIndex — **High value, low risk**

**Idea:** Stop owning search-related types in FileIndex; move them to a shared search/types header so FileIndex is not the central dependency for search result shapes.

- **New (or existing) header:** `search/SearchTypes.h` (or `index/SearchResultTypes.h`).
- **Types to move:** `SearchResultData`, `ThreadTiming`, `SearchStats`. Optionally `PathComponentsView` (already mirrored in PathOperations; could use a single type in path/ and re-export for callers).
- **FileIndex:** Uses these types only as return/parameter types; no logic change. `SearchAsync` / `SearchAsyncWithData` return types become e.g. `std::vector<std::future<std::vector<SearchResultData>>>` from `SearchTypes.h`.
- **Impact:** LoadBalancingStrategy, ParallelSearchEngine, SearchWorker, SearchStatisticsCollector include `SearchTypes.h` instead of `FileIndex.h` where only these types are needed. Reduces FileIndex header pull-in and clarifies that these are “search” types, not “index” types.
- **Performance:** None (types only).
- **Duplication:** None if a single definition is used everywhere.

**Note:** `PathComponentsView` is currently duplicated (FileIndex vs PathOperations). Unifying on `PathOperations::PathComponentsView` (or a type in path/) and using it from FileIndex’s public API removes that duplication; call sites that only need path components can depend on path/ instead of FileIndex where appropriate.

---

### Option C: Extract Search Context and Thread-Pool Creation — **Medium value, low risk**

**Idea:** Group “search setup” into one place: context building + thread pool creation/configuration, so FileIndex doesn’t own settings-reading and pool lifecycle details.

- **SearchContext creation:** Move `CreateSearchContext` and the static `CompilePatternIfNeeded` helper into a `SearchContextBuilder` (or free functions in `search/SearchContextBuilder.h`). FileIndex receives a built `SearchContext` from the builder and passes it to `ParallelSearchEngine`. Builder reads query, path_query, extensions, folders_only, case_sensitive, cancel_flag; optional: load settings for dynamic_chunk_size, hybrid percent (today only SearchAsyncWithData needs those). **Performance:** No extra lock or allocation in hot path; same work, different place.
- **Thread pool:** Introduce a small `SearchThreadPoolManager` (or reuse a factory) that owns:
  - `CreateDefaultThreadPool()` (including LoadSettings, hardware_concurrency fallback),
  - `GetThreadPool()` (lazy create),
  - `SetThreadPool`, `ResetThreadPool`,
  - `GetSearchThreadPoolCount()`.
  FileIndex holds `SearchThreadPoolManager` and delegates these methods to it; FileIndex still owns `search_engine_` and passes the pool to it. **Performance:** Unchanged.
- **Load balancing strategy:** Move `GetLoadBalancingStrategy()` to a free function in `search/` or `core/Settings` (e.g. `LoadBalancingStrategyType GetLoadBalancingStrategyFromSettings()`). ParallelSearchEngine (or callers) use that instead of `FileIndex::GetLoadBalancingStrategy()`. **Duplication:** None.

**Result:** FileIndex loses ~100 lines of “search setup” and “pool lifecycle” implementation; it keeps the same public search/pool API and remains the single facade for the app.

---

### Option D: Extract Full-Path Inserter (InsertPath(full_path)) — **Medium value, low risk**

**Idea:** Move parsing of full path + parent resolution + ID assignment + InsertLocked into a dedicated helper used only for “insert by full path”.

- **New class:** `FullPathInserter` (or `PathToIndexInserter`) in e.g. `index/FullPathInserter.h/.cpp`.
- **Takes:** References to mutex, DirectoryResolver, `next_file_id_` (or a wrapper), and the insert operation (e.g. a callback or reference to IndexOperations + path_operations for the actual insert). Alternatively, take `FileIndex&` and call a single “insert locked” entry point to avoid exposing internals.
- **Method:** `InsertPath(std::string_view full_path)` that: parses directory vs filename, gets parent via DirectoryResolver, fetches next ID, determines is_directory, then calls back into FileIndex (or IndexOperations) under lock. **Lock:** Caller (FileIndex) holds the lock and calls `inserter.InsertPathLocked(full_path)` so locking stays in one place.
- **Call sites:** FolderCrawler, IndexFromFilePopulator, and any other `file_index.InsertPath(path)` continue to call `FileIndex::InsertPath`; FileIndex implements it as lock + `full_path_inserter_.InsertPathLocked(full_path)`.
- **Performance:** Same as today (one lock, same work). **Duplication:** None.

This reduces FileIndex’s “knowledge” of path parsing and directory resolution for bulk insert, and keeps a single place for “full path → index entry” rules (including the documented heuristic for is_directory).

---

### Option E: Introduce Narrow Interfaces (Facade Only) — **Lower immediate impact, prepares for future extractions**

**Idea:** Do not remove methods from FileIndex yet; add interfaces so callers depend on the smallest surface.

- **Interfaces (e.g.):**
  - `IIndexRead`: GetEntry, GetPathView, GetPathsView, GetPathComponentsView, ForEachEntry, ForEachEntryWithPath, Size, GetSearchableView, GetMutex, GetTotalItems, GetStorageSize, GetFileSizeById, GetFileModificationTimeById.
  - `IIndexWrite`: Insert, InsertLocked, InsertPath, Remove, Rename, Move, Clear, UpdateSize, UpdateModificationTime, UpdateFileSizeById, RecomputeAllPaths, FinalizeInitialPopulation.
  - `IIndexSearch`: SearchAsync, SearchAsyncWithData (and optionally thread pool / strategy getters used only for search).
  - `IIndexMaintenance`: Maintain, GetMaintenanceStats.
- **FileIndex:** Implements all; Application and others can store `IIndexRead*` or `IIndexWrite*` where only that capability is needed. No change to implementation; only dependency inversion at call sites.
- **Performance:** No change. **Duplication:** None. **Benefit:** Clearer contracts and easier future extraction (e.g. a separate “search service” implementing IIndexSearch).

This can be done in parallel or after A–D to avoid large call-site churn in one step.

---

## 4. Options Not Recommended (or Defer)

- **Splitting FileIndex into multiple “service” objects that callers hold directly:** Would force Application, UsnMonitor, FolderCrawler, SearchWorker to take 2–3 pointers and know which to use for what. Higher churn and risk of performance regressions (e.g. wrong lock ordering). Prefer extraction behind the same FileIndex facade.
- **Replacing PathStorage’s contiguous buffer with `std::vector<std::string>`:** Would violate the documented performance design (cache locality, allocation count). Out of scope for this refactor.
- **Moving all path access (GetPathView, etc.) into PathOperations only:** Callers would need both FileIndex (for entry/metadata) and PathOperations (for path), and PathOperations would need the same lock as FileIndex. Possible later if we introduce a single “Index + Path” read interface, but not required to reduce FileIndex’s responsibilities in the short term.

---

## 5. Suggested Order of Implementation

1. **Option B (Search types out of FileIndex)** — Small, mechanical, reduces header coupling and clarifies ownership. Unify `PathComponentsView` with PathOperations where possible.
2. **Option A (PathRecomputer)** — Removes the largest single block of logic from FileIndex with no behavior or performance change.
3. **Option C (SearchContext + thread pool)** — Shrinks FileIndex’s “search and pool” implementation; keep API on FileIndex.
4. **Option D (FullPathInserter)** — Further reduces FileIndex’s “insert by path” logic.
5. **Option E (Narrow interfaces)** — Apply where call sites can be updated with minimal risk (e.g. new code or isolated modules first).

---

## 6. Success Criteria

- **Responsibilities:** RecomputeAllPaths, search setup, thread pool lifecycle, full-path insertion, and search/result types are no longer “inside” FileIndex’s implementation (moved to dedicated types/classes or free functions).
- **Methods:** FileIndex public API can stay for backward compatibility, but many become one-line delegates; total “logic” in FileIndex (lines in .cpp/.h that are not simple delegation) drops significantly.
- **Performance:** No regression in search latency, index update throughput, or path access; same locking and allocation patterns.
- **Duplication:** No new duplication; shared logic (path rebuild, context build, pool creation) lives in one place.
- **Tests:** Existing tests (and benchmarks) pass without modification where behavior is unchanged; new types/classes get unit tests as appropriate.

---

## 7. Summary Table

| Option | What moves out of FileIndex | Performance | Duplication | Risk |
|--------|-----------------------------|-------------|-------------|------|
| **A**  | RecomputeAllPaths body → PathRecomputer | No change | None | Low |
| **B**  | SearchResultData, ThreadTiming, SearchStats (and optionally PathComponentsView) → SearchTypes / path | None | None (unify PathComponentsView) | Low |
| **C**  | CreateSearchContext, CompilePatternIfNeeded, CreateDefaultThreadPool, GetLoadBalancingStrategy → SearchContextBuilder + SearchThreadPoolManager + free function | No change | None | Low |
| **D**  | InsertPath(full_path) logic → FullPathInserter | No change | None | Low |
| **E**  | No logic move; add IIndexRead / IIndexWrite / IIndexSearch / IIndexMaintenance | None | None | Low |

Implementing A–D yields a FileIndex that is a **thin coordinator**: locking + delegation to Storage, PathOperations, IndexOperations, PathRecomputer, FullPathInserter, LazyAttributeLoader, FileIndexMaintenance, SearchContextBuilder, SearchThreadPoolManager, and ParallelSearchEngine, with search-related types defined outside FileIndex. Option E then makes the remaining surface area explicit per client.
