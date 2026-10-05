# Specification: Trigram Row-Epoch Guard (fail-safe staleness detection)

**Feature Name:** Row-epoch guard for the filename trigram index
**Status:** Draft (pre-implementation)
**Date:** 2026-10-05
**Target Platforms:** macOS (main dev), Windows (primary target), Linux
**Language Standard:** C++17
**Motivation:** 2026-10-05 production regression — a file found after cold start disappeared after a path-buffer rebuild because trigram posting row-ids went stale and the engine treated "index doesn't know" as "definitely empty". Fixed at the two known sites (`FileIndex::Maintain` rebuilds; `PathOperations::UpdatePrefix` moves postings). This spec makes the *next* bug of that class degrade to a slower search instead of a wrong empty result.

---

## 1. Executive Summary & Objectives

### 1.1 Problem Statement

`FilenameTrigramIndex::QueryCandidates` (`src/search/FilenameTrigramIndex.cpp:376-389`) returns `std::nullopt` for two distinct situations:

1. **Unknown** — index not ready, or no trigrams could be planned.
2. **Known empty** — a required trigram has zero postings.

`ParallelSearchEngine::SearchAsyncWithData` (`src/search/ParallelSearchEngine.cpp:215-219`) treats both as "return empty immediately, no fallback scan". Case 2 is sound **only** while the posting row-ids exactly match `PathStorage` row-ids. Any mutation that relocates rows without maintaining the trigram (past example: `FileIndexMaintenance::RebuildPathBuffer`) turns case 1-in-disguise into hard false negatives.

### 1.2 Proposed Solution

Version the row-id space with a monotonic **row epoch**:

- `PathStorage` owns a `uint64_t row_epoch_` (in-class init `0`), bumped by every low-level primitive that invalidates row-ids.
- `FilenameTrigramIndex` records `built_epoch_` — the epoch its postings reflect — plus a monotonic `confirmed_epoch_` for incrementally maintained operations.
- Query time compares the live epoch against the built epoch. **Mismatch → verdict `Unknown` → full parallel scan** (slow but correct). Match → today's fast path, including the known-empty early exit (now sound).

Forgetting trigram maintenance in a future mutator therefore costs performance, never correctness. That fail-safe direction is the entire point.

### 1.3 Non-Goals

- No change to trigram extraction, planning, posting layout, or the cost model.
- No fallback for genuinely empty results (that early exit stays).
- No persistence of the epoch (both stores are in-memory; epoch restarts at 0 per process, consistent by construction).

---

## 2. Background & Fault Model

### 2.1 What is already fixed (context, not this spec)

| Site | Fix |
|---|---|
| `FileIndexMaintenance::RebuildPathBuffer` (`src/index/FileIndexMaintenance.cpp`) | After compaction, `BeginRebuild` snapshot under the *same* `unique_lock` scope + `SubmitBuild` off-lock. (An earlier revision split these into two lock scopes; review found the TOCTOU window — fixed by keeping compact + snapshot atomic.) |
| `PathOperations::UpdatePrefix` (`src/path/PathOperations.cpp:89-128`) | Relocated rows move their trigram postings to the new row-id. |
| `FileIndexMaintenance::RebuildPathBuffer` | Now `private`; must run via `Maintain()`. |
| `UINT32_MAX` row guards in `PathOperations` | Emit `LOG_ERROR_BUILD` on the skip path. |

### 2.2 Fault model for this spec

- **Covered:** any future `PathStorage` mutator that relocates rows through the epoch-bumping primitives but forgets trigram maintenance → detected at query time → scan fallback.
- **Not covered:** a mutator that hand-rolls row relocation *outside* `PathStorage` primitives (bypassing the epoch bump). That surface is small and grep-able (`path_storage_` is only touched via `PathOperations`, `PathRecomputer`, `FileIndexMaintenance`, `FileIndex::Clear`).
- **Overflow:** `uint64_t` epoch wraparound is not a practical concern (one bump per compaction/relocation; no ABA handling required).

---

## 3. Architecture & Design

### 3.1 Component view (text C4-L3)

```
PathStorage                     FilenameTrigramIndex           ParallelSearchEngine
(row_epoch_ ++)                 (built_epoch_,                (QueryTrigramCandidates
   │                             confirmed_epoch_)              + verdict switch)
   │                                     │                              │
   ├─ RebuildPathBuffer: bump            │                              │
   ├─ UpdatePrefix relocate: bump        │                              │
   ├─ InsertPath tombstone+append: bump  │                              │
   ├─ Clear: NO bump (both stores        │                              │
   │    emptied together; consistent     │                              │
   │    by construction)                 │                              │
   │                                     │                              │
PathOperations wrappers         MarkSynced(epoch) after each           │
(Insert/Remove/UpdatePrefix) ─► incrementally maintained op            │
FileIndexMaintenance::         Ticket carries epoch;                  │
  RebuildPathBuffer /          commit sets built_epoch_                │
  RecomputeAllPaths ─────────► (snapshot shares the compaction lock)   │
                                                               epoch ==
                                                          built_epoch_?── YES ──► trust filter
                                                               │                    (incl. known-empty exit)
                                                               NO ──► full scan (correct, slower)
```

### 3.2 Core invariant

> *The epoch advances only via full rebuild or explicit re-baseline.*
> Primitives bump unconditionally on relocation; maintained wrappers re-baseline afterwards (`MarkSynced` for incremental ops, ticket commit for full rebuilds). Full rebuilds (`BeginRebuild`/`BuildFromPostings`) replace postings wholesale from a fresh snapshot, so they re-sync by construction regardless of the epoch.

Consequences used by the design:

- `built_epoch_ == row_epoch_` ⟹ postings reflect every row-id change so far (every change either rebuilt, or bumped + re-baselined).
- `built_epoch_ != row_epoch_` ⟹ something relocated rows without re-baselining ⟹ trust nothing, scan.

### 3.3 Verdict type (replaces the `nullopt` conflation)

```cpp
enum class TrigramStatus { Unknown, Empty, Filter };

struct TrigramResult {
  TrigramStatus status = TrigramStatus::Unknown;
  roaring::Roaring candidates;  // valid iff status == Filter
};
```

- `FilenameTrigramIndex::QueryCandidates(trigrams, epoch)` returns:
  - `Unknown` — not ready, no trigrams, **or `epoch != built_epoch_`**.
  - `Empty` — ready, epoch matches, but a required trigram has zero postings (no candidates member needed).
  - `Filter` — ready, epoch matches, non-trivial candidate bitmap.
- `SearchAsyncWithData`: `Unknown` → existing full parallel scan path (today's `!query_attempted` behavior); `Empty` → existing early empty return (now provably sound); `Filter` → existing `ShouldUseTrigramFilter` cost model unchanged.

### 3.4 Epoch plumbing

- `PathStorage::SoAView` gains a `uint64_t row_epoch` field, set by `GetReadOnlyView()`. Zero new locks: `QueryTrigramCandidates` already holds the index `shared_lock` when it fetches the view, so the epoch read is ordered with the mutation lock for free. Cost per search: one integer compare.
- `TrigramBuildTicket` carries the epoch captured in `BeginRebuild` (alongside generation + postings).
- `BuildFromPostings` commits `built_epoch_ = confirmed_epoch_.load()` (see 3.5), but only on the generation-match path that already exists; stale generations discard as today.
- `Clear()` on the trigram resets `built_epoch_ = confirmed_epoch_ = 0`; `PathStorage::Clear` does not bump (both empty ⇒ consistent; search short-circuits on `total_items == 0` before consulting the trigram).
- Overflow-guard trips (`PathOperations` `UINT32_MAX` skips): log (already present) **and** bump the epoch *without* re-baselining — the skipped row is genuinely unindexed, so the fast path must stay off. Wrappers therefore call `MarkSynced` only when every posting in the op was fully indexed (track a local `fully_indexed` flag).

### 3.5 Incremental maintenance during a background build

`confirmed_epoch_` is `std::atomic<uint64_t>` (default `seq_cst`; per project rules no explicit `memory_order`, hence no `NOSONAR(cpp:S8417)` needed):

- Mutators under the index `unique_lock` call `MarkSynced(current_epoch)` after maintaining postings; it advances monotonically (`fetch_max` semantics — implement as compare-exchange loop, C++17 has no `fetch_max`).
- The pool thread in `BuildFromPostings` loads it once at commit. Sound because commit state = snapshot(ticket epoch) + replayed `pending_mutations_` (every post-ticket incremental op, each MarkSynced) ⇒ postings reflect all epochs ≤ loaded value.
- `MarkSynced` during a build never touches `pending_mutations_` and never interferes with the generation guard.
- `AbortBuild` with a matching generation implies no newer `BeginRebuild` happened; by the section-3.2 invariant the epoch cannot have advanced without one (or a re-baseline, which only moves `confirmed_epoch_` forward — still covered by the commit-time load). No epoch rollback needed on abort.

---

## 4. API Changes (per file)

| File | Change |
|---|---|
| `src/path/PathStorage.h` / `.cpp` | Add `uint64_t row_epoch_{0}` (`snake_case_`); bump in `RebuildPathBuffer`, `UpdatePrefix` relocation branch, `InsertPath` tombstone+append branch. Add `row_epoch` to `SoAView`; set it in `GetReadOnlyView()`. `Clear`/`ClearAll` do **not** bump (document why). |
| `src/search/FilenameTrigramIndex.h` / `.cpp` | Add `built_epoch_`, `confirmed_epoch_` (atomic); `MarkSynced(uint64_t)` (monotonic, thread-safe, callable under index lock); ticket carries epoch; `BuildFromPostings` commits `built_epoch_`; `Clear` resets both to 0; `QueryCandidates` takes the live epoch and returns `TrigramResult`. |
| `src/search/ParallelSearchEngine.cpp` (`.h` for `TrigramCandidateQuery` if the status is threaded through) | `QueryTrigramCandidates` reads `row_epoch` from the searchable view, maps the verdict to `query_attempted`/`candidates`/new `known_empty` outcome; `SearchAsyncWithData` branches `Unknown → scan`, `Empty → early empty`, `Filter → cost model` (unchanged). |
| `src/path/PathOperations.cpp` | After each maintaining op, `MarkSynced` with the live epoch — only when `fully_indexed` (no overflow-guard trip); on trip, bump epoch instead (fail safe). |
| `src/index/ISearchableIndex.h`, `tests/MockSearchableIndex.h` | Only if the epoch travels via interface instead of `SoAView` — preferred design avoids this; no change expected. |
| `tests/FilenameTrigramIndexTests.cpp` | Update to the three-way return (`has_value()`/`cardinality()` call sites ≈ 20). |
| `src/index/FileIndexStorage.h` (comment only) | Document the epoch invariant next to `trigram_index_`. |

Naming per `docs/standards/CXX17_NAMING_CONVENTIONS.md`: `PascalCase` methods (`MarkSynced`, `QueryCandidates`), `snake_case_` members (`row_epoch_`, `built_epoch_`), `[[nodiscard]]` on the query, braced returns, explicit lambda captures.

---

## 5. Threading & Locking

- Epoch reads (query path): under the existing index `shared_lock`. No new mutexes, no lock-ordering change (`docs/design/LOCK_ORDERING_AND_CRITICAL_SECTIONS.md` unaffected).
- Epoch bumps + `MarkSynced`: under the existing index `unique_lock` held by all mutators.
- `confirmed_epoch_` atomic bridges the pool-thread commit; single load per build, single store per maintaining op — negligible contention (mutators already serialize on the unique lock).
- No allocations in the search hot path: verdict is a value enum + moved bitmap (already moved today); the epoch check is one compare before any bitmap work.

---

## 6. Acceptance Criteria

| ID | Criterion | Measurable check |
|---|---|---|
| AC1 | Epoch mismatch forces scan fallback | Unit: `Build` → `MarkSynced(5)` → `QueryCandidates(trigrams, 6)` returns `Unknown`. |
| AC2 | Epoch match preserves fast path | Unit: same setup, `QueryCandidates(trigrams, 5)` returns `Filter` with the live row; missing trigram returns `Empty` (not `Unknown`). |
| AC3 | End-to-end staleness is a slowdown, not a miss | Integration: index with `key` file at last row; relocate rows via `PathStorage` primitives *without* trigram maintenance (simulates a future forgetful mutator); `SearchAsyncWithData("key")` still returns 1 hit (via fallback). |
| AC4 | Known-empty early exit retained | `SearchAsyncWithData("xyz-no-match")` returns empty futures without scanning (thread-timings assertion mirroring the existing `xyz` test). |
| AC5 | No latency regression, no new warnings | `search_benchmark` on `tests/data/std-linux-filesystem.txt` within noise of baseline; `scripts/pre-commit-clang-tidy.sh` exit 0; no new Sonar issues on touched files. |
| AC6 | Existing regression tests stay green | `Trigram search survives path-buffer rebuild`, `UpdatePrefix moves trigram postings`, all `FilenameTrigramIndexTests` (updated), `FileIndexMaintenanceTests`, `PathOperationsTests`, `PathStorageTests`. |

---

## 7. Task Breakdown

| Phase | Task | Deps | Est. | Acceptance / tests |
|---|---|---|---|---|
| 1 | `PathStorage`: `row_epoch_` + bump sites + `SoAView::row_epoch` + `GetReadOnlyView` wiring; document the no-bump-on-`Clear` rationale | — | 2h | `path_storage_tests` pass; new unit test asserts epoch bumps on compact/relocate/tombstone-append and not on `Clear`/in-place update |
| 2 | `FilenameTrigramIndex`: `built_epoch_`/`confirmed_epoch_`, ticket epoch, `MarkSynced`, commit/reset semantics, three-way `QueryCandidates` | 1 | 3h | `filename_trigram_index_tests` updated + AC1/AC2 unit tests |
| 3 | `ParallelSearchEngine`: verdict switch (`Unknown→scan`, `Empty→early`, `Filter→cost model`) | 2 | 2h | AC3/AC4 integration tests in `ParallelSearchEngineTests.cpp` TrigramAcceleration suite |
| 4 | `PathOperations`: `MarkSynced`-when-`fully_indexed`, epoch-bump on overflow trip; invariant comment on `FileIndexStorage::trigram_index_` | 2 | 2h | AC3-style test via `UpdatePrefix`; overflow path covered by code review (4B rows untestable) + tidy |
| 5 | Full verification: `scripts/build_tests_macos.sh`, `pre-commit-clang-tidy.sh`, benchmark compare (AC5) | 3, 4 | 1h | AC5, AC6 |

Each phase leaves the tree buildable with all prior tests green. Estimated total: ~10h.

---

## 8. Risks & Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| `MarkSynced` forgotten in a future *maintained* op → permanent fallback (slow, correct) | Perf only | Fail-safe direction by design; `LOG_DEBUG_BUILD` in fallback path makes it visible in benchmarks |
| Epoch compared against a torn `SoAView` (view outlives mutation) | False mismatch → spurious fallback (correct, slower) | Views are lock-scoped by existing contract; mismatch can only cause fallback, never a miss — one-directional safety |
| `std::atomic<uint64_t>` not lock-free on some target | Negligible (one op per mutation/build) | `static_assert(std::atomic<uint64_t>::is_always_lock_free)` where relied upon; x64/ARM64 are fine |
| Test churn in `FilenameTrigramIndexTests` (≈20 call sites) | Mechanical | Keep old `optional`-based helper in the test file mapping verdict→optional during transition, then remove |
| Scope creep into planner/cost-model tuning | Delays | Explicit non-goal (1.3); reviewer rejects planner changes in this diff |

---

## 9. Test Plan (summary)

- **Unit** (`filename_trigram_index_tests`, `path_storage_tests`): epoch bump sites; AC1/AC2 verdict matrix; `MarkSynced` monotonicity under interleaved build (deterministic: drive `BeginRebuild`/`SubmitBuild(nullptr)` synchronously).
- **Integration** (`parallel_search_engine_tests`, `file_index_maintenance_tests`): AC3 simulated-forgetful-mutator test; AC4 early-exit timings test; existing rebuild/UpdatePrefix regression tests unchanged and green.
- **Manual smoke (Windows):** cold start → search `key` → force churn past defrag threshold → search again (hit, possibly via fallback — confirm via thread timings); check log for absence of new error lines.
- **Benchmarks:** `search_benchmark` + `tolower_benchmark` unaffected; trigram build log lines (`postings=`, `transient_mb=`) unchanged in shape.

## 10. References

- Regression analysis & fix: `FileIndex::Maintain` trigram rebuild, `UpdatePrefix` posting moves, private `RebuildPathBuffer`, overflow logging (2026-10-05 work).
- Prior review: `internal-docs/review/FILENAME_TRIGRAM_INDEX_REVIEW_2026-10-04.md` (async build generation guard — the ticket this spec extends with an epoch).
- Benchmarks: `internal-docs/benchmarks/2026-10-03_TRIGRAM_GATE_PP_PREFILTER.md` (cost model the `Filter` branch keeps).
- Rules: `AGENTS.md` (naming, `scoped_lock`, `explicit`, NOLINT same-line, no `[&]`/`[=]` in templates), `docs/design/LOCK_ORDERING_AND_CRITICAL_SECTIONS.md`, `.agents/rules/cpp-atomic-memory-order-cpp.md` (default `seq_cst`, no explicit order).
