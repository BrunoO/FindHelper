# LoadBalancingStrategy.cpp – Coverage Analysis

**Date:** 2026-02-23

This document explains why `src/utils/LoadBalancingStrategy.cpp` shows ~60% code coverage and whether that is expected from the current test approach.

---

## Why Work Stealing is gated on Boost (FAST_LIBS_BOOST)

**Yes, there was a valid reason.** The Work Stealing strategy is tied to Boost because:

1. **It uses a lock-free concurrent queue.** The implementation uses `boost::lockfree::queue<WorkChunk, boost::lockfree::capacity<...>>` for per-thread work queues. When a thread’s queue is empty, it steals (pops) from another thread’s queue; that requires **concurrent access without a mutex** to avoid contention and get the benefit of work stealing.

2. **The C++ standard library does not provide a lock-free queue.** C++17 and C++20 have no `std::lockfree_queue`. Without Boost (or another library), we would need either a custom lock-free queue (complex and error-prone) or a mutex-protected queue (simpler but more contention, reducing the advantage over the existing hybrid/dynamic strategies).

3. **The project already has an optional Boost switch.** `FAST_LIBS_BOOST` is used for `boost::regex` and `boost::unordered_map`/`unordered_set`. Tying work stealing to the same flag means:
   - No extra dependency: builds without Boost do not pull in Boost just for work stealing.
   - One opt-in (“use Boost for performance”) enables regex, hashmaps, and the work_stealing strategy.

So work stealing is **correctly** available only when Boost is enabled; the dependency is **Boost.Lockfree** (`boost/lockfree/queue.hpp`). See README (vcpkg example mentions `boost-lockfree`) and `LoadBalancingStrategy.cpp` (lines 45–46, 1097).

---

## 1. How load balancing is tested

### 1.1 Unit tests (`file_index_search_strategy_tests`)

- **Static, Hybrid, Dynamic, Interleaved** are all exercised:
  - Dedicated test suites for Static, Hybrid, Dynamic (many cases: single thread, many threads, small datasets, concurrent searches, edge cases).
  - **"All strategies return correct results"** runs all four strategies via `GetAllStrategies()` → `{"static", "hybrid", "dynamic", "interleaved"}`.
  - Pattern matcher and other tests use `RunTestForAllStrategiesWithSetup` / `RunTestForAllStrategies`, so **interleaved** is run there too.
- **Thread timings:** Some tests pass `&timings` into `SearchAsyncWithData` (e.g. "Static strategy distributes work evenly", "Hybrid strategy uses dynamic chunks"), so `RecordThreadTimingIfRequested` is called with non-null `thread_timings`.
- **Strategy creation:** Strategies are created via `CreateLoadBalancingStrategy()` from `ParallelSearchEngine` when a search runs (strategy name from settings). So the factory and the four main strategies are on the hot path.

### 1.2 ImGui test engine (regression / load_balancing)

- **Regression** tests run searches without setting strategy (default hybrid).
- **load_balancing** tests explicitly set **static**, **hybrid**, **dynamic** and assert result counts. They do **not** set **interleaved** (only unit tests do).

### 1.3 What is not exercised by tests

- **WorkStealingStrategy:** Only built when `FAST_LIBS_BOOST` is defined. There is a test under `#ifdef FAST_LIBS_BOOST`, but typical coverage runs are without Boost, so the whole `#ifdef FAST_LIBS_BOOST` block (WorkStealingStrategy + `work_stealing_detail`) is either not compiled or not run. That’s a large block (~170 lines).
- **ValidateThreadPool (failure path):** Returns false when `thread_pool.GetThreadCount() == 0`. No test creates a search with 0 threads, so the early return and empty `futures` path are never hit.
- **ProcessChunkWithExceptionHandling (catch blocks):** The three handlers (`bad_alloc`, `runtime_error`, `exception`) only run when `ProcessChunkRange` throws. Normal tests don’t trigger that.
- **ProcessDynamicChunksLoop – cancellation:** The branch that checks `context.cancel_flag` and sets `should_continue = false` is only hit when a search is cancelled. Unit tests don’t cancel in-flight searches.
- **ProcessDynamicChunksLoop – edge cases:**  
  - `initial_chunks_end_ >= total_items_` (no remaining work) may be hit in some hybrid/dynamic runs;  
  - `chunk_end <= chunk_start` is a defensive edge case, unlikely in normal runs.
- **ValidateAndNormalizeStrategyName (invalid name):** The path that logs a warning and returns `GetDefaultStrategyName()` is only hit when an invalid strategy name is passed. Unit tests only use valid names; the app uses this when loading settings or CLI, which isn’t exercised by `file_index_search_strategy_tests`.
- **GetAvailableStrategyNames / GetDefaultStrategyName:** Used by SearchBenchmark and Settings/UI. If coverage is collected only from unit tests (no benchmark, no GUI), these may not run.
- **CreateLoadBalancingStrategy fallback:** The final `return std::make_unique<HybridStrategy>()` is unreachable after `ValidateAndNormalizeStrategyName`; it’s defensive only.
- **InterleavedChunkingStrategy – small chunk fallback:** When `context.dynamic_chunk_size < 100`, the code uses `k_default_sub_chunk_size` (256). Default settings usually have `dynamicChunkSize >= 100`, so this branch may be uncovered.
- **ExecuteInterleavedStrategyTask:** The branch `last_chunk_idx >= params.num_sub_chunks_` (setting `last_chunk_end = params.total_items_`) is an edge case that might not be hit depending on data size and thread count.

---

## 2. Why ~60% coverage is expected

Rough breakdown:

| Category | Approx. share | Covered by current tests? |
|----------|----------------|---------------------------|
| Static / Hybrid / Dynamic / Interleaved – main paths | ~50% | Yes (unit + ImGui for static/hybrid/dynamic; unit for interleaved) |
| Helpers (SetupThreadWorkAfterLock, RecordThreadTimingIfRequested, ProcessChunkWithExceptionHandling, ProcessDynamicChunksLoop) – happy path | ~15% | Partially (timing path yes; exception and cancel paths no) |
| WorkStealingStrategy + work_stealing_detail | ~20% | No (Boost not used in typical coverage; or strategy not selected) |
| ValidateThreadPool failure, invalid strategy name, GetAvailableStrategyNames, GetDefaultStrategyName, factory fallback | ~5% | No |
| Exception handlers, cancellation, edge cases (chunk_end <= chunk_start, no remaining work, interleaved last-chunk edge) | ~10% | No |

So a large fraction of the “missing” coverage comes from:

1. **WorkStealingStrategy** when not built or not run.
2. **Error / edge paths** (0 threads, invalid name, cancellation, exceptions, defensive branches).

That aligns with **~60% line coverage**: the main strategies and their normal execution paths are well covered; the rest is optional (Boost), defensive, or error handling.

---

## 3. Conclusion

- **Yes, ~60% coverage for `LoadBalancingStrategy.cpp` is expected** with the current test suite:
  - All four main strategies (static, hybrid, dynamic, interleaved) are run by unit tests; static/hybrid/dynamic are also run by ImGui load_balancing tests.
  - What’s missing is mostly: WorkStealingStrategy (Boost), 0-thread validation, invalid strategy name, cancellation, exception handling inside workers, and a few edge branches.

- No change is **required** for correctness: the paths that are covered are the ones used in production under normal and tested configurations.

---

## 4. Optional improvements (if you want higher coverage)

- **ValidateAndNormalizeStrategyName (invalid name):** Add a small unit test (e.g. in a new test file or in `SettingsTests`) that calls `ValidateAndNormalizeStrategyName("invalid")` and checks the return value is the default (e.g. `"hybrid"`). That would also cover `GetDefaultStrategyName()`.
- **GetAvailableStrategyNames:** Call from a test (or from the same test as above) and check the list contains the expected strategy names.
- **ValidateThreadPool (0 threads):** Only testable with a mock or a custom executor that reports 0 threads; more invasive and lower priority.
- **ProcessChunkWithExceptionHandling:** Would require injecting a failure inside `ProcessChunkRange` (e.g. mock or special test index); generally not worth it for coverage alone.
- **Cancellation:** A test that starts a long search and sets `cancel_flag` would cover the cancellation branch in `ProcessDynamicChunksLoop`; useful but requires careful test design.
- **WorkStealingStrategy:** Enable `FAST_LIBS_BOOST` in the coverage build and run the existing work_stealing test so that block is compiled and executed.

If you want to pursue any of these, the highest leverage with minimal code is: **add a test for invalid strategy name and for GetAvailableStrategyNames/GetDefaultStrategyName** (e.g. in a small “LoadBalancingStrategyUtils” or “Settings” test).
