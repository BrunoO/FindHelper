# Fix: Remove LoadSettings from Search Hot Path

**Date**: 2026-02-02  
**Related**: Tech Debt Review 2026-02-01 — Critical finding "Performance Bottleneck: File I/O in Search Hot Path"

## Goal

Eliminate synchronous `LoadSettings()` (file I/O on `settings.json`) from the search path. Application already has settings when triggering search; pass the search-relevant subset through the call chain so FileIndex and ParallelSearchEngine never call `LoadSettings()` during a search.

## Benefits Estimate

### Per-search latency reduction

Each `LoadSettings()` call does:

- Resolve path to `settings.json` (e.g. `%APPDATA%/FindHelper/settings.json` or next to executable).
- Open file: `std::ifstream file(path, ...)`.
- Read + parse JSON: `file >> j` plus `LoadBasicSettings`, `LoadSearchConfigSettings`, etc.

**Rough cost per call** (depends on disk and file size; settings file is typically &lt; 50 KB):

| Storage type      | Open + read + parse (per call) | 2–3 calls per search (today) |
|-------------------|---------------------------------|------------------------------|
| Local SSD         | ~0.2–1 ms                       | ~0.5–3 ms saved               |
| Local HDD         | ~1–5 ms                         | ~2–15 ms saved                |
| Network / slow FS | ~5–50+ ms                       | ~10–150+ ms saved             |

**After fix**: 0 file I/O in the search path when settings are passed in. The app already reports `last_search_time_ms_`; that value will drop by the amounts above on slow storage, and stay similar on fast SSD (small but consistent gain).

### When benefits compound

- **Debounced instant search**: User types → 400 ms debounce → search runs. Each run currently does 2–3× LoadSettings. Removing that makes each keystroke-triggered search faster and more predictable.
- **Auto-refresh**: Index changes → search re-runs. Same I/O removed per run.
- **Manual search (Enter / button)**: One search, same latency reduction.
- **Many quick searches**: e.g. user refines query several times; each search pays the I/O cost today. After fix, only the first load at app startup (or settings change) touches the file.

### Other benefits

- **Predictability**: Search latency no longer depends on `settings.json` location or filesystem speed; it depends on index size and CPU, which are already the main factors.
- **No I/O contention**: Avoids competing with crawler or other disk activity for the same drive.
- **Simpler mental model**: Search uses “current in-memory settings” instead of re-reading the file every time.
- **Tests**: Callers that don’t pass settings can still fall back to one `LoadSettings()` in FileIndex; no behavioral change, but fewer redundant reads in production.

### Summary

| Scenario                    | Benefit                                              |
|----------------------------|------------------------------------------------------|
| Latency per search (SSD)   | ~0.5–3 ms lower (small but consistent)               |
| Latency per search (HDD)   | ~2–15 ms lower (noticeable)                          |
| Latency per search (network/slow) | ~10–150+ ms lower (very noticeable)          |
| Debounced / auto-refresh   | Same gain every time a search runs                   |
| Code / ops                 | No file I/O in hot path; more predictable latency    |

**Verdict**: Highest impact on slow or network-backed storage; still a clear win on HDD; small improvement on SSD with the main gain being consistency and removal of a hidden dependency on disk for every search.

## Search-Relevant Settings

Only these fields are needed in the search path:

| AppSettings field        | Used in                         | Today                         |
|--------------------------|----------------------------------|-------------------------------|
| `dynamicChunkSize`       | FileIndex → SearchContext        | FileIndex.cpp:238             |
| `hybridInitialWorkPercent` | FileIndex → SearchContext     | FileIndex.cpp:239             |
| `loadBalancingStrategy`  | ParallelSearchEngine             | ParallelSearchEngine.cpp:199–220 |
| `searchThreadPoolSize`   | ParallelSearchEngine::DetermineThreadCount | ParallelSearchEngine.cpp:284–288 |

## Fix (Step-by-Step)

### 1. Extend SearchContext

**File**: `src/search/SearchContext.h`

- Add two members (with defaults so existing code keeps working):
  - `std::string load_balancing_strategy = "hybrid";`
  - `int search_thread_pool_size = 0;`  // 0 = auto (hardware_concurrency)

- Ensure `ValidateAndClamp()` (if present) clamps these if needed.

### 2. Pass settings into SearchWorker when starting a search

**File**: `src/search/SearchWorker.h` / `.cpp`

- Add overload or optional parameter to `StartSearch`:
  - `void StartSearch(const SearchParams& params, const AppSettings* settings = nullptr);`
- Store `const AppSettings* search_settings_` (or a copy of the four search fields) for the duration of the search. Use it when calling FileIndex (step 3). Pointer is valid: it points to Application’s `settings_` which outlives the search.

**File**: `src/search/SearchController.cpp`

- In `TriggerManualSearch` and `HandleAutoRefresh`, pass settings into StartSearch:
  - `search_worker.StartSearch(state.BuildCurrentSearchParams(), &settings);`

### 3. FileIndex::SearchAsyncWithData — accept optional settings, stop calling LoadSettings

**File**: `src/index/FileIndex.h` / `FileIndex.cpp`

- Add optional parameter to `SearchAsyncWithData` (only the overload that builds context and calls ParallelSearchEngine):
  - e.g. `const AppSettings* optional_settings = nullptr` as last (or grouped) parameter.
- In the implementation:
  - If `optional_settings != nullptr`: set `context.dynamic_chunk_size`, `context.hybrid_initial_percent`, `context.load_balancing_strategy`, `context.search_thread_pool_size` from `optional_settings` and **do not** call `LoadSettings(settings)`.
  - If `optional_settings == nullptr` (e.g. tests): keep current behavior (call `LoadSettings(settings)` once to fill context). That reduces to at most one file read per search for legacy callers.

### 4. SearchWorker calls FileIndex with settings

**File**: `src/search/SearchWorker.cpp`

- In `RunFilteredSearchPath`, when calling `file_index_.SearchAsyncWithData(...)`, pass the stored settings pointer (e.g. `search_settings_`). If your design uses a copy of four integers + string instead of a pointer, pass that and have FileIndex build the context from it.

### 5. ParallelSearchEngine::SearchAsyncWithData — use context only, no LoadSettings

**File**: `src/search/ParallelSearchEngine.cpp`

- Remove the local `AppSettings settings; LoadSettings(settings);` block.
- Use `context.load_balancing_strategy` to create the strategy:
  - Replace `GetLoadBalancingStrategyFromSettings()` with logic that uses `context.load_balancing_strategy` (e.g. pass it to `CreateLoadBalancingStrategy(context.load_balancing_strategy)`).
- Replace `CreateLoadBalancingStrategy(settings.loadBalancingStrategy)` with `CreateLoadBalancingStrategy(context.load_balancing_strategy)`.

### 6. DetermineThreadCount — use context, no LoadSettings

**File**: `src/search/ParallelSearchEngine.cpp` and `.h`

- Change `DetermineThreadCount(int thread_count, size_t total_bytes)` to also receive the thread-pool size from context, e.g.:
  - `DetermineThreadCount(int thread_count, size_t total_bytes, int search_thread_pool_size_from_context)`
- In `SearchAsyncWithData`, call it with `context.search_thread_pool_size` (e.g. `DetermineThreadCount(thread_count, total_bytes, context.search_thread_pool_size)`).
- Inside `DetermineThreadCount`: when `thread_count <= 0`, use `search_thread_pool_size_from_context` (if > 0) instead of calling `LoadSettings`. If it is 0, use `std::thread::hardware_concurrency()` as today. Remove the `LoadSettings` call entirely from this function.

### 7. Optional: narrow API surface

- If you prefer not to pass `AppSettings*` through the stack, define a small struct (e.g. `SearchEngineSettings` or `SearchConfig`) with the four fields and pass that from Application → SearchController → SearchWorker → FileIndex → SearchContext. Same behavior, no dependency on full `AppSettings` in the index/search layer.

## Call Flow After Fix

1. **Application** holds `settings_` (already loaded at startup / on change).
2. **SearchController::TriggerManualSearch(state, search_worker, settings)** calls **search_worker.StartSearch(params, &settings)**.
3. **SearchWorker** stores the pointer (or a copy of the four fields) and, in **RunFilteredSearchPath**, calls **file_index_.SearchAsyncWithData(..., optional_settings)**.
4. **FileIndex::SearchAsyncWithData** builds `SearchContext` from `optional_settings` (no `LoadSettings`), then calls **ParallelSearchEngine::SearchAsyncWithData(..., context)**.
5. **ParallelSearchEngine::SearchAsyncWithData** uses `context.load_balancing_strategy` and **DetermineThreadCount(..., context.search_thread_pool_size)**; no `LoadSettings` anywhere in the search path when settings are passed in.

## Testing

- **With settings passed**: No `LoadSettings` calls during search; same behavior as before.
- **Without settings (e.g. unit tests)**: FileIndex falls back to one `LoadSettings` when `optional_settings == nullptr`; ParallelSearchEngine uses context only (context filled by FileIndex from that single load or from defaults).
- Run existing search tests and manual search (instant + manual trigger, auto-refresh) to confirm results and performance.

## Effort

About 2 hours: add two fields to SearchContext, thread optional settings through SearchController → SearchWorker → FileIndex, and remove LoadSettings from FileIndex (when settings provided) and from ParallelSearchEngine and DetermineThreadCount.
