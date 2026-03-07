# DRY LoadBalancingStrategy – Verification and Fix Plan

**Date:** 2026-02-13  
**Source:** docs/2026-02-13_IMPROVEMENT_OPPORTUNITIES.md #5  
**Strict Constraints:** docs/prompts/AGENT_STRICT_CONSTRAINTS.md

---

## 1. Issue Verification

### 1.1 Original Claim

> Replace duplicated ProcessChunkRange lambdas with shared ProcessChunkRangeForSearch; ~400 lines duplication across Static/Hybrid/Dynamic

### 1.2 Current State (Verified)

**Most duplication has already been eliminated.** The codebase uses:

| Helper | Purpose | Used By |
|-------|---------|---------|
| `ProcessChunkWithExceptionHandling` | Wraps `ParallelSearchEngine::ProcessChunkRange` with try-catch | All strategies |
| `SetupThreadWorkAfterLock` | SoA view, matchers, storage size | All strategies |
| `RecordThreadTimingIfRequested` | Thread timing metrics | All strategies |
| `ExecuteHybridStrategyTask` | Extracted task body | Hybrid |
| `ExecuteDynamicStrategyTask` | Extracted task body | Dynamic |
| `ExecuteInterleavedStrategyTask` | Extracted task body | Interleaved |

**Note:** `ProcessChunkRangeForSearch` was removed from FileIndex (dead code). The canonical implementation is `ParallelSearchEngine::ProcessChunkRange` in `ParallelSearchEngine.h`, called via `ProcessChunkWithExceptionHandling`.

### 1.3 Remaining Duplication

**StaticChunkingStrategy** is the only strategy that still uses an **inline lambda** (lines 447–484 in `LoadBalancingStrategy.cpp`) instead of an extracted executor:

```cpp
// Current: ~38 lines inline lambda
futures.push_back(thread_pool.Enqueue(
  [&index, start_index, end_index, thread_idx = t, context, thread_timings]() mutable {
    const auto start_time = std::chrono::high_resolution_clock::now();
    std::vector<SearchResultData> local_results;
    constexpr size_t k_match_rate_heuristic = 20;
    local_results.reserve((end_index - start_index) / k_match_rate_heuristic);
    std::shared_lock lock(index.GetMutex());
    const auto setup = load_balancing_detail::SetupThreadWorkAfterLock(index, context);
    load_balancing_detail::ProcessChunkWithExceptionHandling(...);
    load_balancing_detail::RecordThreadTimingIfRequested(...);
    return local_results;
  }));
```

**Conclusion:** The "~400 lines" claim is outdated. Remaining work is ~38 lines in Static only. Effort estimate: **S (~30–60 min)** instead of M (2–3 h).

---

## 2. Fix Plan (Strict Constraints)

### 2.1 Goal

Extract `ExecuteStaticStrategyTask` to match the pattern of Hybrid, Dynamic, and Interleaved. No new abstractions beyond what exists.

### 2.2 Steps

| Step | Action | Constraints |
|------|--------|-------------|
| 1 | Add `StaticStrategyTaskParams` struct in `load_balancing_detail` | Mirror `HybridStrategyTaskParams` pattern; use in-class initializers (S3230); const ref for read-only params (S995/S5350) |
| 2 | Add `ExecuteStaticStrategyTask(const StaticStrategyTaskParams& params)` | Same pattern as `ExecuteHybridStrategyTask`; single responsibility |
| 3 | Replace Static inline lambda with `ExecuteStaticStrategyTask` call | Minimal change; no platform #ifdef modifications |
| 4 | Run `scripts/build_tests_macos.sh` | Required per AGENTS.md |
| 5 | Verify no new Sonar/clang-tidy issues | Per AGENT_STRICT_CONSTRAINTS.md |

### 2.3 Proposed `StaticStrategyTaskParams`

```cpp
struct StaticStrategyTaskParams {
  const ISearchableIndex& index;
  size_t start_index_ = 0;
  size_t end_index_ = 0;
  int thread_idx_ = 0;
  const SearchContext& context;
  std::vector<ThreadTiming>* thread_timings_ = nullptr;
};
```

### 2.4 Proposed `ExecuteStaticStrategyTask`

- Acquire `shared_lock`
- Call `SetupThreadWorkAfterLock`
- Call `ProcessChunkWithExceptionHandling`
- Call `RecordThreadTimingIfRequested`
- Return `local_results`

Placement: In `load_balancing_detail` namespace, before `StaticChunkingStrategy::LaunchSearchTasks`, following the order used for Hybrid/Dynamic/Interleaved.

### 2.5 Strict Constraints Checklist

- [ ] No new SonarQube or clang-tidy violations
- [ ] Preferred style: in-class init, const T&, no `} if (` on one line
- [ ] No performance penalties (no extra allocations in hot path)
- [ ] No duplication (extract and reuse)
- [ ] No changes inside platform `#ifdef` blocks
- [ ] All `#include` at top of file
- [ ] Minimal change; no new abstractions beyond extraction
- [ ] Verification: run build_tests_macos.sh; confirm no new Sonar/clang-tidy issues

### 2.6 Files to Modify

| File | Change |
|------|--------|
| `src/utils/LoadBalancingStrategy.cpp` | Add `StaticStrategyTaskParams`, `ExecuteStaticStrategyTask`; replace Static lambda |

---

## 3. Post-Fix: Update Improvement Doc

After implementation, update `docs/2026-02-13_IMPROVEMENT_OPPORTUNITIES.md`:

- Change item #5 notes from "~400 lines duplication" to "Completed: Static extracted to ExecuteStaticStrategyTask; Hybrid/Dynamic/Interleaved already use extracted executors"
- Or mark as done and move to completed section if such exists

---

## 4. Out of Scope

- `ProcessChunkRangeForSearch` – Not needed; `ParallelSearchEngine::ProcessChunkRange` is the canonical implementation
- WorkStealingStrategy – Already uses `ProcessChunkWithExceptionHandling`; no inline ProcessChunkRange lambda
- Further consolidation of `Execute*StrategyTask` – YAGNI; each strategy has distinct logic (initial chunk vs dynamic loop vs interleaved loop)

---

## 5. Effort Estimate (Revised)

| Original | Revised | Rationale |
|----------|---------|------------|
| M (2–3 h) | S (~30–60 min) | Most DRY work done; only Static needs extraction |

---

## 6. References

- `src/utils/LoadBalancingStrategy.cpp` – Current implementation
- Previous refactoring (partial) – see project maintainer for historical doc
- `docs/prompts/AGENT_STRICT_CONSTRAINTS.md` – Strict constraints
- `AGENTS.md` – Project rules (DRY, SOLID, KISS, YAGNI)
