# Specification: Skip / Remap-After-Compact for the Trigram Index

**Feature Name:** Compaction-aware trigram refresh (skip / incremental remap / full rebuild)
**Status:** Draft (pre-implementation — revisit after current-implementation testing)
**Date:** 2026-10-05
**Target Platforms:** macOS (main dev), Windows (primary target), Linux
**Language Standard:** C++17
**Motivation:** Since 2026-10-05, `FileIndex::Maintain()` rebuilds the *entire* trigram index after every path-buffer compaction. Correct, but coarse: 1001 tombstones in a 1M-row index trigger a full O(N) extract + sort + bitmap build. This spec replaces the unconditional rebuild with a three-way strategy driven by how many rows actually moved.

---

## 1. Executive Summary & Objectives

### 1.1 Problem Statement

`PathStorage::RebuildPathBuffer` (`src/path/PathStorage.h:355-404`) compacts by collecting live entries **in SoA order**, clearing, and re-appending. New index = position in the compacted array. Because the order is preserved, every row before the first tombstone keeps its exact index — yet `FileIndex::Maintain()` (`src/index/FileIndex.cpp`) unconditionally snapshots and rebuilds all trigram postings afterwards. When nothing (or little) moved, that full rebuild is pure waste: background CPU, transient memory (up to the true peak, see the peak-transient log), and a not-ready window in which searches fall back to scan.

### 1.2 Proposed Solution

Count moved rows during compaction (old index ≠ new index) and branch:

| Moved rows | Strategy | Cost |
|---|---|---|
| `0` | **Skip** — postings untouched and valid, no rebuild at all | ~free |
| Small (fraction under a measured threshold, starting heuristic 10%) | **Remap** — per moved row `RemoveEntry(old)` + `AddEntry(new)` with the unchanged filename | O(moved × trigrams) |
| Large | **Rebuild** — today's full `BeginRebuild`/`SubmitBuild` | O(N) sort |

A `LOG_IMPORTANT_BUILD` line per compaction (`compacted=N moved=M strategy=skip|remap|rebuild`) makes the workload's actual regime visible in production logs *before* the remap threshold is tuned — measure, then commit to a number.

### 1.3 Non-Goals

- No change to *when* compaction triggers (thresholds untouched), to extraction/build internals, or to the search cost model.
- No surgical bitmap surgery below the `AddEntry`/`RemoveEntry` API (reuse tested primitives only).

---

## 2. Background & Key Insight

### 2.1 Why skip/remap is exactly equivalent

Three facts, each verifiable in code:

1. **Compaction copies paths verbatim.** `RebuildPathBuffer` re-appends `entry.path` byte-for-byte and reuses `entry.filename_start` unchanged (`PathStorage.h:391-397`; offsets are path-relative, so they stay valid). Filenames of surviving rows are identical before/after.
2. **Deletions are pre-maintained.** Every tombstone got its `RemoveEntry` at removal time (`PathOperations::RemovePath`, `src/path/PathOperations.cpp:65-75`). Dead rows have no postings left to clean.
3. **Compaction, snapshot, and the skip/remap decision share one lock scope.**
   `FileIndexMaintenance::RebuildPathBuffer` holds the index `unique_lock`
   across compact → (future) move detection → trigram snapshot, with only
   `SubmitBuild` running after unlock. Splitting compact and snapshot into
   separate lock scopes reopens a TOCTOU window where `IsReady()==true` but
   postings reference pre-compact rows (transient false negatives — found by
   code review 2026-10-05, fixed by keeping both phases in `RebuildPathBuffer`).
   No concurrent mutation mid-decision.

Therefore the post-compaction truth equals: old postings, minus already-removed dead rows, with moved rows re-keyed. Skip (moved==0) and remap (re-key moved rows) produce *bit-identical* posting sets to a full rebuild. The equivalence is directly testable (section 6).

### 2.2 Honest frequency assessment

Tail-only deletion (temp files, rolled-back bulk imports) compacts to zero moves; scattered USN deletes shift everything after them. Expect skip to fire *sometimes* and remap to cover the common small-churn case; large compactions still rebuild. The log line (1.2) exists precisely to replace this paragraph with data.

---

## 3. Architecture & Design

### 3.1 Data flow

```
FileIndex::Maintain()  (thin wrapper; all work below runs inside it)
  └─ maintenance_.Maintain()
       └─ FileIndexMaintenance::RebuildPathBuffer()   [unique_lock held across compact + trigram snapshot]
            ├─ PathStorage::RebuildPathBuffer(cb) → moved_count
            └─ trigram BeginRebuild snapshot (same lock scope: no TOCTOU gap)
       └─ trigram SubmitBuild (after unlock) + returns moved_count
  └─ branch on moved_count (see 3.2)
```

### 3.2 Decision logic (in `FileIndexMaintenance::RebuildPathBuffer`, replacing the unconditional rebuild)

```cpp
// moved == 0 → do nothing. Postings are valid; the index never leaves ready.
// (Deletions were removed incrementally; surviving rows kept their ids.)
// 0 < moved ≤ threshold → remap loop under the same unique_lock:
//   for each moved (id, old_idx, new_idx): RemoveEntry(old, filename) + AddEntry(new, filename)
//   with the filename read from the new row (identical bytes, §2.1).
// moved > threshold → BeginRebuild snapshot (same lock scope) + SubmitBuild (pool, after unlock).
```

Threshold: `constexpr double kRemapMaxMovedFraction = 0.10;` as a *starting heuristic*, to be confirmed or adjusted from the strategy log line on real workloads. Random roaring updates lose to one radix sort past some fraction — that crossover is what the log data will show.

### 3.3 Move detection

`PathStorage::RebuildPathBuffer` computes `moved_count` in the re-append loop (`old_idx != new_idx`) and returns it (`size_t`; `void` today). Two call-shape options, implementer picks one and documents why:

- **(a) Return value.** Minimal diff. Callers ignoring it: `PathStorageTests.cpp` direct call — discard explicitly with `(void)` per project rules.
- **(b) Extended callback `(file_id, old_index, new_index)`.** Mirrors the `UpdatePrefix` callback extended 2026-10-05. Enables the remap loop to reuse the same callback in a second pass — but the remap needs filenames, also available post-compact via `GetPathByIndex`, so (a) plus a targeted rescan is equivalent. Prefer (a) unless the remap loop demonstrably wants per-row callbacks.

`FileIndexMaintenance::RebuildPathBuffer` (private since 2026-10-05) returns the count to `Maintain()`, which returns it to `FileIndex::Maintain()`. Signature change is contained: `Maintain()` callers use boolean context today (`ApplicationLogic.cpp:185`, tests) — keep `Maintain()` returning `bool performed` and expose the count via a `LastCompactionMovedRows()` getter, **or** return a small `enum class MaintainResult { NoWork, Compacted }` plus getter. Do not break the existing boolean call sites; pick the shape that touches fewer of them.

### 3.4 Interaction with the row-epoch guard (spec `2026-10-05_TRIGRAM_ROW_EPOCH_GUARD_SPEC.md`)

If the epoch spec lands first (or after — order-independent if both respect this rule):

- **Skip path: bump nothing.** No row moved ⇒ row-id space identical ⇒ epoch unchanged ⇒ postings trivially in sync, no `MarkSynced` needed. This falls out naturally if the epoch bump lives in the move-detection site (`old_idx != new_idx`) rather than unconditionally in `RebuildPathBuffer`.
- **Remap path: `MarkSynced` after the loop**, same as any incrementally maintained op (it *is* one, just batched). If any posting was skipped by the `UINT32_MAX` guard, do not mark — bump the epoch instead (fail safe), per the epoch spec's overflow rule.
- **Rebuild path: unchanged** (ticket carries epoch; commit sets `built_epoch_`).

Stating it as a rule: *the epoch bump belongs at the point a row-id actually changes, never at the operation boundary.* That keeps skip/remap/rebuild and the epoch guard mutually consistent whichever lands first.

### 3.5 Observability

One `LOG_IMPORTANT_BUILD` line per performed compaction (in `FileIndex::Maintain`, next to the existing trigram lines):

```
FileIndex::Maintain: compacted=<live> moved=<m> strategy=skip|remap|rebuild
```

This is the calibration instrument for the threshold (3.2) and the report card for this feature on Windows. No per-row logging.

---

## 4. API Changes (per file)

| File | Change |
|---|---|
| `src/path/PathStorage.h` (template `RebuildPathBuffer` + `UpdatePrefix`-adjacent docs) | Return `size_t` moved count (option (a)) or extend callback (option (b)); document the order-preservation property the count relies on. |
| `src/index/FileIndexMaintenance.{h,cpp}` | Thread the count through private `RebuildPathBuffer()` → `Maintain()` → new getter (name TBD, e.g. `LastCompactionMovedRows()`); keep `Maintain()`'s `bool` contract. The skip/remap/rebuild branch lives in `RebuildPathBuffer` itself so the decision and the trigram snapshot share its `unique_lock` scope. |
| `src/index/FileIndex.{h,cpp}` | No logic change (`Maintain()` stays a thin wrapper); strategy log line can live in `FileIndexMaintenance`. Threshold as named `constexpr` (DRY, one place). |
| `src/path/PathOperations.cpp` | No change (remap reuses `AddEntry`/`RemoveEntry` semantics directly on the trigram index; no wrapper change needed). |
| `tests/PathStorageTests.cpp` | Handle the new return (discard or assert moved counts: tail-tombstones ⇒ 0, mid-array ⇒ >0). |
| `tests/FileIndexMaintenanceTests.cpp`, new remap tests | Skip/remap/rebuild selection tests (see §6). |

Naming per `docs/standards/CXX17_NAMING_CONVENTIONS.md`; `[[nodiscard]]` on the count-returning functions; braced returns; no `[&]`/`[=]` captures in any template touched.

---

## 5. Threading & Locking

- Detection and the skip/remap branch run inside `RebuildPathBuffer`, which holds the index `unique_lock` — no new lock, no ordering change (`docs/design/LOCK_ORDERING_AND_CRITICAL_SECTIONS.md` unaffected). The remap loop and the `BeginRebuild` snapshot share that same scope; only `SubmitBuild` runs after unlock.
- Skip: no lock traffic beyond today's compaction.
- Remap loop: under the same `unique_lock` (rows must not move under us; it is O(moved), bounded by the threshold fraction).
- Rebuild: unchanged two-phase pattern. The not-ready window (scan fallback) now only occurs on the large-move path — strictly fewer fallbacks than today.

---

## 6. Acceptance Criteria & Test Plan

| ID | Criterion | Measurable check |
|---|---|---|
| AC1 | Tail-tombstone compaction skips the rebuild | Unit/integration: index with live rows + tail deletes; `Maintain()` returns true; trigram `IsReady()` never flaps (stays true throughout); `QueryCandidates` for a known trigram identical before/after (same rows). |
| AC2 | Small-move compaction remaps exactly | Equivalence test: same churn applied twice — once forcing remap (low threshold override or small churn), once forcing full rebuild; `QueryCandidates` bitmaps **equal** for a sample of trigrams (iterate all postings if cheap at test scale) and search returns identical hits. |
| AC3 | Large-move compaction rebuilds | Churn > threshold fraction; strategy log asserts `rebuild` (capture via test log sink if available, else assert `IsReady` flapped / build telemetry incremented); search correct after. |
| AC4 | Threshold is data-driven | Production log sample reviewed; threshold value either confirmed or adjusted with a dated note in the spec. No code change required to re-tune (single `constexpr`). |
| AC5 | No regressions | Full `scripts/build_tests_macos.sh`, `pre-commit-clang-tidy.sh` exit 0, no new Sonar issues; existing `Maintain`/rebuild/UpdatePrefix regression tests green. |

Test placement: `path_storage_tests` (moved-count unit tests), `file_index_maintenance_tests` + `parallel_search_engine_tests` TrigramAcceleration suite (AC1–AC3 end-to-end, mirroring the existing "survives path-buffer rebuild" test shape: keeper file + bulk churn + `Maintain()` + search).

---

## 7. Task Breakdown

| Phase | Task | Deps | Est. | Acceptance / tests |
|---|---|---|---|---|
| 1 | Move detection + count plumbing (`PathStorage` → `FileIndexMaintenance` → `FileIndex`), strategy log line; rebuild path byte-identical to today | — | 2h | Existing tests green; log line visible in test/manual runs |
| 2 | Skip path (moved==0) + remap loop + threshold `constexpr` | 1 | 2h | AC1, AC2 |
| 3 | Threshold calibration pass on real/Windows-like workload data + AC4 note; full verification (AC5) | 2 | 1–2h | AC3, AC4, AC5 |

Estimated total: ~5–6h. Each phase leaves the tree buildable with all prior tests green; phase 1 alone is shippable (instrumentation + refactor, no behavior change except the log line).

---

## 8. Risks & Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Remap loop subtly diverges from full rebuild (missed row) | False negatives (the original bug, reintroduced) | AC2 equivalence test is the gate; remap uses the same `AddEntry`/`RemoveEntry` primitives as live mutation, no new bitmap logic |
| Threshold wrong (remap slower than rebuild, or vice versa) | Perf only, both paths correct | Strategy log makes it measurable; single `constexpr` to re-tune; default errs toward rebuild (known-good cost) |
| `Maintain()` signature churn breaks callers | Compile noise | Keep `bool` contract + getter (3.3); grep callers (`ApplicationLogic`, tests) before changing |
| Interaction with epoch guard if both land | Epoch/skipped-rebuild inconsistency | Follow §3.4 rule (bump at actual row change, not operation boundary); cross-check both specs at implementation time |
| Scope creep into compaction-trigger tuning | Delays | Explicit non-goal (1.3); trigger thresholds are a separate decision with separate data |

---

## 9. References

- Current behavior: `FileIndex::Maintain` unconditional rebuild + `Predicted*CostUs` + mispredict log (2026-10-05 work); strategy log proposed here composes with those lines.
- Epoch guard: `specs/2026-10-05_TRIGRAM_ROW_EPOCH_GUARD_SPEC.md` (§3.4 interaction above).
- Prior review: `internal-docs/review/FILENAME_TRIGRAM_INDEX_REVIEW_2026-10-04.md`.
- Rules: `AGENTS.md` (naming, `scoped_lock`, `explicit`, NOLINT same-line), `docs/design/LOCK_ORDERING_AND_CRITICAL_SECTIONS.md`, `.agents/rules/cpp-atomic-memory-order-cpp.md`.
