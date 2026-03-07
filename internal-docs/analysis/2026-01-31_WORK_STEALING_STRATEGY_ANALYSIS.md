# 2026-01-31 WorkStealingStrategy – Local Changes Analysis

## Summary of local changes

The following files were modified to add a new **WorkStealingStrategy** load-balancing option:

| File | Change |
|------|--------|
| `src/utils/LoadBalancingStrategy.h` | New class `WorkStealingStrategy` (inherits `LoadBalancingStrategy`) with `GetName()` / `GetDescription()`; description depends on `FAST_LIBS_BOOST`. |
| `src/utils/LoadBalancingStrategy.cpp` | Full implementation under `#ifdef FAST_LIBS_BOOST` (Boost lockfree queues), factory/names include `work_stealing`, fallback implementation returns empty when Boost is off. |
| `tests/FileIndexSearchStrategyTests.cpp` | New test suite `"Work Stealing Strategy"` under `#ifdef FAST_LIBS_BOOST` that runs a search with `work_stealing` and validates results. |

---

## What the strategy does

- **Goal:** Reduce contention on a single atomic counter by using **per-thread work queues** and **work stealing** (threads that run out of local work steal chunks from others).
- **Mechanism:**
  1. One `boost::lockfree::queue<WorkChunk>` per thread (capacity 1024).
  2. Chunks are pushed in **round-robin** into these queues (chunk size from `context.dynamic_chunk_size` or 1024).
  3. Each worker: (a) drains its **local queue**, (b) then **steals** from other queues in a fixed order until no work remains.
  4. Shared index access and pattern matching follow the same pattern as other strategies (shared lock, `SetupThreadWorkAfterLock`, `ProcessChunkWithExceptionHandling`).
- **When it runs:** Only when `FAST_LIBS_BOOST` is defined at compile time; otherwise `LaunchSearchTasks` returns an empty vector and logs an error.

---

## Relevance

- **Use case:** High core counts / high contention where a single `atomic<size_t>` (e.g. in Hybrid/Dynamic) might become a bottleneck.
- **Overlap:** Hybrid and Dynamic already give good load balance with one atomic and guided scheduling; work stealing adds another option with different tradeoffs (more memory, Boost dependency, no single global counter).
- **Consistency:** The strategy is **not selectable** in practice today (see bugs below).

---

## Bugs and issues

1. **Strategy never selectable (critical)**  
   `ValidateAndNormalizeStrategyName()` only allows `"static"`, `"hybrid"`, `"dynamic"`, `"interleaved"`. It does **not** allow `"work_stealing"`. So when the user (or config) chooses `work_stealing`:
   - The validator replaces it with the default (`"hybrid"`) and logs a warning.
   - `CreateLoadBalancingStrategy()` always receives a validated name, so the `work_stealing` branch is never taken.  
   **Effect:** WorkStealingStrategy cannot be used through the normal UI/settings path.

2. **Windows (std::min) rule**  
   In `LoadBalancingStrategy.cpp` (work-stealing block), line 1156 uses `std::min(...)` without parentheses. Per AGENTS document, use `(std::min)(...)` to avoid Windows macro conflicts.

3. **Typo**  
   Comment in the steal loop: "Try to steal **untill** empty" → should be "**until**".

4. **Test never runs with Boost**  
   The Work Stealing test is guarded by `#ifdef FAST_LIBS_BOOST`, but the test target `file_index_search_strategy_tests` (and `test_core_obj`) do **not** get `FAST_LIBS_BOOST`. Only `find_helper` does. So even with `-DFAST_LIBS_BOOST=ON`, the Work Stealing test block is not compiled for the test executable. The test is effectively dead unless the test target is given `FAST_LIBS_BOOST` when the option is ON.

---

## Assessment: keep or drop?

| Criterion | Keep | Drop |
|-----------|------|------|
| **YAGNI** | Only if you have (or plan) benchmarks showing benefit at high core count. | Current hybrid/dynamic are likely sufficient; work stealing adds code and a Boost lockfree dependency. |
| **Correctness** | Fix validator + (std::min) + typo so the strategy is selectable and build-clean. | Removing it avoids the validator/fallback complexity and “strategy in UI but never used” confusion. |
| **Maintenance** | More code paths, conditional Boost, and test wiring (test target needs FAST_LIBS_BOOST when ON). | Fewer strategies and no Boost-specific load-balancing path. |
| **Consistency** | If kept: add `work_stealing` to `ValidateAndNormalizeStrategyName` (only when `FAST_LIBS_BOOST`), fix `GetAvailableStrategyNames()` to include `work_stealing` only when Boost is enabled, fix (std::min) and typo, and optionally enable the test when FAST_LIBS_BOOST=ON. | If dropped: remove class, factory branch, name from list, validator special-case, and the test suite. |

**Recommendation**

- **Keep** only if you have a concrete need (e.g. benchmarks or known contention at high core count) and you are willing to:
  - Fix the validator so `work_stealing` is accepted when `FAST_LIBS_BOOST` is defined.
  - Fix the (std::min) and "untill" typo.
  - Optionally add `FAST_LIBS_BOOST` to the test target when the option is ON so the Work Stealing test runs.
- **Drop** if you prefer fewer moving parts and no extra Boost-dependent strategy; current hybrid/dynamic are enough for most workloads.

---

## Fixes applied in code (if keeping)

- In `LoadBalancingStrategy.cpp`: use `(std::min)(current_idx + chunk_size, total_items)` and fix "untill" → "until".
- In `ValidateAndNormalizeStrategyName`: when `FAST_LIBS_BOOST` is defined, treat `"work_stealing"` as valid; otherwise keep current behavior (so configs with `work_stealing` on non-Boost builds fall back to default).
- In `GetAvailableStrategyNames()`: include `"work_stealing"` only when `FAST_LIBS_BOOST` is defined, so the UI does not offer it on builds without Boost.
- In CMake: when `FAST_LIBS_BOOST` is ON, add `FAST_LIBS_BOOST` to `file_index_search_strategy_tests` (and any object libs it uses that need the same define) so the Work Stealing test is compiled and run.
