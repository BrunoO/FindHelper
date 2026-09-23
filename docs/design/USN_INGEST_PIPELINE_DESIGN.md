# USN Ingest Pipeline Design

**Status (2026-09, verified against HEAD `05b69e63` 2026-09-16):** Description of
the live implementation after the `ReturnOnlyOnClose=TRUE` change and the
index-integrity patch series (metrics, per-buffer two-phase apply,
search-commit quarantine, `InsertOptions` convergence, differential oracle),
plus the follow-ups: integrity-latch consolidation into a single helper
(`4bbfe783`) and fail-closed post-population journal verification
(`05b69e63`). For the forensics — *why* each piece exists — see
`internal-docs/analysis/2026-09-15_USN_INDEX_INTEGRITY_REVIEW.md`.
This document describes *what the code is*.

## 1. Data flow

```text
NTFS volume
  │  DeviceIoControl(FSCTL_READ_USN_JOURNAL)   ReaderThread
  ▼                                            src/usn/UsnMonitor.cpp:1075-1177
UsnJournalQueue (bounded, drop-latched)
  │  Pop(buffer)                               ProcessorThread
  ▼                                            src/usn/UsnMonitor.cpp:1367-1394
ProcessOneBuffer                               src/usn/UsnMonitor.cpp:1232-1340
  │  phase 0 (no lock): parse + partition
  │  phases 1+2 (ONE exclusive index lock): ApplyOneUsnRecord each
  ▼
ProcessInterestingUsnRecord → ProcessUsnRecordReasons
  │  CREATE → InsertLocked │ DELETE → RemoveLocked
  │  RENAME_NEW → HandleRenameNewName │ data reasons → InvalidateSizeLocked
  ▼                                            (all under the buffer's lock)
FileIndex + IndexOperations (src/index/, src/crawler/)
  │  known parent → indexed entry
  │  unknown parent → awaiting staging (invisible to search)
  ▼
Search side (UI thread): PollResults → MergeAndConvertToSearchResults
  (quarantine: skip awaiting) → CommitNewSearchResults → results table
```

Slow paths alongside the flow: `SweepNeverArriving`
(processor thread, dual cadence), metrics snapshot → Metrics window
(UI thread, lock-free atomics).

## 2. Thread model and lock discipline

| Thread | Role | Lock held |
|---|---|---|
| Controlling (UI/logic) | `Start`/`Stop`/`UpdateConfig`, atomic reads (`IsActive`, metrics) | `mutex_` only across start/stop; I/O outside the lock (`UsnMonitor.h:599-600`) |
| ReaderThread | `DeviceIoControl` block, push complete buffers to queue; initial population + privilege drop before live loop | none on the index; queue push only |
| ProcessorThread | `Pop` → `ProcessOneBuffer`; sweep at buffer tail | ONE `unique_lock(file_index_.GetMutex())` per buffer covering phases 1+2; released between buffers so search gets `shared_lock` |
| Test thread | `ProcessBufferForTest` (no monitor threads running) | same as processor, via `ProcessOneBuffer` |

Rules (see also `LOCK_ORDERING_AND_CRITICAL_SECTIONS.md`):

- **No I/O under the index lock.** `InvalidateSizeForDataChange` calls
  `InvalidateSizeLocked`, never a stat (`UsnMonitor.cpp:200-222`).
- **One exclusive acquisition per buffer**, not per event: per-event
  writer acquisition starves under SRWLOCK; the single-hold rationale is
  documented at `UsnMonitor.cpp:1265-1273`.
- **Sweep takes the locks itself.** `SweepNeverArriving`
  must be entered *without* the index lock (`UsnMonitor.h:540-544`):
  it snapshots awaiting stats under the FileIndex helpers' shared lock,
  then acquires the `unique_lock` only for the eviction itself
  (`UsnMonitor.cpp:1342-1365`, eviction lock at `1355-1357`).
- **Never nest** FileIndex / SearchThreadPool / UsnMonitor mutexes.

## 3. Read configuration

`ReaderThread` setup (`UsnMonitor.cpp:1098-1115`):

- `ReasonMask = kInterestingReasons` = CREATE | DELETE | RENAME_OLD |
  RENAME_NEW | DATA_EXTEND | TRUNCATION | OVERWRITE | CLOSE
  (`UsnMonitor.h:232-236`). Close-only records carry no action:
  `kActionReasons = kInterestingReasons & ~CLOSE` (`UsnMonitor.h:239`),
  and `ApplyOneUsnRecord` skips records with no action bits
  (`UsnMonitor.cpp:1220`).
- **`ReturnOnlyOnClose = TRUE`** (`UsnMonitor.cpp:1108`): the
  journal delivers one coalesced record per close, so intermediate
  CREATE/DELETE partials never appear. DELETE wins over CREATE on the
  same close record (`ProcessUsnRecordReasons`, `UsnMonitor.cpp:232-241`).
- Empty reads (`<= SizeOfUsn`) are never queued
  (`ProcessSuccessfulReadAndEnqueue`, `UsnMonitor.cpp:983-985`).
  Consequence: on a quiet system the processor thread can stall, which
  is why the sweep has a wall-clock backstop (§9).

## 4. Two-phase per-buffer apply

`ProcessOneBuffer` (`UsnMonitor.cpp:1232-1340`):

- **Phase 0, no lock** (`1238-1263`): walk records from the leading
  next-USN offset, stable-partition into `establish_parents` vs
  `remaining`. Record pointers stay valid because the buffer is
  read-only for the whole call (`1239-1240`).
- **Phases 1+2, one exclusive lock** (`1275-1282`): parent-establishing
  records first, then everything else, both via the shared
  `ApplyOneUsnRecord` body (`1217-1226`).

The phase-0 predicate (`IsParentEstablishingRecord`,
`UsnMonitor.cpp:63-66`):

```cpp
return (record->Reason & USN_REASON_FILE_CREATE) != 0 &&
       (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
```

Design notes (`UsnMonitor.cpp:57-62`): renames need no phase-1
priority (FRNs are stable; buckets are keyed by record number), and
directory DELETEs stay in phase 2 so a create-then-delete inside one
buffer nets to deleted. Net effect: parents precede children *within*
a buffer; cross-buffer awaiting entries are the true minimum.

## 5. Reason dispatch

`ProcessUsnRecordReasons` (`UsnMonitor.cpp:226-279`):

- DELETE present → delete path (wins over CREATE on the same record,
  `232-241`).
- CREATE → `InsertLocked` (`246-250`).
- RENAME_NEW → `HandleRenameNewName` (`260-261`); lone RENAME_OLD is a
  diagnostic log, not a fault (`262-269`).
- Data reasons → `InvalidateSizeForDataChange` (cached-size
  invalidation, miss log throttled every 1000), unless DELETE is also
  set (`271-278`).

Rename handling: `HandleFileRename` (`382-447`) distinguishes move
from rename by `SameRecordNumber(current_parent, parent_ref)`
(`413-414`) and recovers unknown FRNs with a CREATE (call site
`396-410`, helper `HandleRenameForUnknownEntry`, `285-310`, recovery
insert `306-307`). A rename that still cannot resolve latches
integrity (`431-436`) — an unknown rename is a lost file, not a
cosmetic gap.

## 6. Insert path and `InsertOptions`

`IndexOperations::Insert` (`src/crawler/IndexOperations.cpp:302-373`)
resolves the parent, computes one of four path shapes (parent-known
join; volume-root record 5 join; non-Windows parent-0 join; otherwise
**bare-name placeholder** + awaiting, shape comment `316-323`,
branches `327-343`), stores via `InsertLocked` (`346-347`), then heals
or tracks (`359-372`).

The three ingest policies converge behind one named type
(`IndexOperations.h:38-67`); the comment there documents the full
2×2 matrix:

| Combination | Meaning | User |
|---|---|---|
| register + resolve | live USN default | `FileIndex::Insert`, USN path |
| register, no resolve | bulk MFT (per-insert heal is O(N²); `RecomputeAllPaths` covers it) | `InsertBatch` |
| no register + resolve | resolver-owned dirs; poison-pill tests | DirectoryResolver, tests |
| neither | synthetic inserts under already-resolved parents | `InsertPathUnderLock` |

`FileIndex::InsertBatch` (`FileIndex.h:110`, `FileIndex.cpp:355-384`)
holds a single `unique_lock` for the whole batch and bumps
`mutation_count_` once (`381`).

## 7. Filter-first

System-directory filtering (`$`-prefixed names, filtered subtrees) is
decided **before** insertion, never repaired after — except for the one
ordering hazard below. Shared predicate:
`system_path_filter::IsSystemPrefixedName`
(`src/index/SystemPathFilter.h:26`); filtered-subtree membership is
tracked by `FilteredDirTracker::{IsFilteredChild, MarkFilteredDir,
EraseOnDelete}` (`SystemPathFilter.h:38-62`).

- Bulk MFT: skip `$-names`, mark filtered dirs, propagate to
  grandchildren (`InitialIndexPopulator.cpp:121-129, 165-170`).
- Live USN: `HandleSystemFileFilter` (`UsnMonitor.cpp:120-159`) skips
  `$-names` and filtered children; directories mark themselves and
  evict already-indexed descendants
  (`EvictDescendantsOfFilteredParentLocked`, `147`) to cover the
  out-of-order hazard (child CREATE arrives before the parent is
  marked filtered). DELETEs erase the mark (`151-154`).
- Deliberate exception: a rename *to* a `$-name` is NOT filtered here
  (`117-118, 136-138`) — that is the Recycle-Bin soft-delete, handled
  by `HandleRenameNewName` → `RemoveIndexedSubtreeLocked`
  (`UsnMonitor.cpp:365-376`).

## 8. Pending staging (invisible by construction)

Unresolved children wait in `awaiting_parent_`, a
`flat_hash_map<uint64_t, vector<UnresolvedReference>>`
(`IndexOperations.h:339-348`) **keyed by the 48-bit MFT record number
of the missing parent**. Two properties make it safe:

- It is not the searchable index — entries graduate via
  `HealAwaiting` (`IndexOperations.cpp:237-300`, BFS from
  the newly linked parent, transitive grandchildren included) or age
  out via the sweep (§9). Display never sees it except through the
  quarantine (§11).
- `healed_awaiting_count_` (surfaced as `healed_awaiting_total`)
  counts only genuine heals (`IndexOperations.cpp:286`): the
  parent-gone re-track, moved-away, gone-entry, and empty-path skips
  (`258-277`) are explicitly not heals (`283-285`), so the counter
  cannot inflate under churn.

Related details: dedup-by-child-id on track (`115-119`); parent-0 and
root-record-5 never tracked (`110-112`); `ClearAwaiting` on `Clear` and
`RecomputeAllPaths` (`IndexOperations.cpp:64-66`).

## 9. Sweep (dual cadence, dual policy)

`SweepNeverArriving(current_usn)` (buffer's leading USN), constants
at `UsnMonitor.h:218-229`:

- Trigger: every 250th processed buffer **or** 60 s wall-clock since
  the last sweep. The wall-clock arm exists because empty reads are
  never queued (§3) — buffer-count alone would stall on a quiet system.
- OR policy: entries older than 300 000 ms (5 min, rationale at
  `UsnMonitor.h:218-223`) are evicted via
  `FileIndex::EvictNeverArrivingLocked`, **or** entries tracked more
  than 1 MB of journal activity behind `current_usn`
  (`kNeverArrivingMaxUsnGap`, rationale at `UsnMonitor.h:230-238`) via
  `FileIndex::EvictNeverArrivingByJournalGapLocked`. Both remove per
  entry to keep buckets and `path_to_id_` consistent;
  `never_arriving_evicted` counts both. Unknown-position entries
  (`usn_at_track == 0`: bulk, synthetic, move paths) are exempt from
  the gap arm (wall-clock only). The gap arm is steady-state only —
  skipped while populating, where the replay drain advances USNs in
  bulk and parents legitimately lag buffers.
- Every sweep publishes `awaiting_count`,
  `awaiting_oldest_age_ms`, and `healed_awaiting_total` to the metrics
  snapshot, so the Metrics window shows staging pressure even when no
  eviction fires. Evicted events carry both `age_ms` and `usn_at_track`.

## 10. Repair-path catalog

Each path below handles exactly one fault; do not merge them without
updating this table.

| Repair | Fault handled | Location |
|---|---|---|
| Bare-name invariant (`CheckBareNameInvariant`) | separator-less stored path that is neither awaiting nor a drive-letter root | `IndexOperations.cpp:449-464`; `Rename` keeps bare placeholders bare (`481-487`) so they stay "unresolved" until heal |
| Mis-graft guard (`IsDescendantOf`) | healing a child under the wrong parent; bounded 64-step walk, 48-bit record compare, stale-sequence tolerant | `IndexOperations.cpp:142-171`; used by heal cascade (`213`) and filtered eviction (`421`) |
| Volume-root fallbacks | record-5 / parent-0 must never become bare placeholders or be rewritten | `ResolveParentIdForStorage` (`IndexOperations.cpp:30-47`); Insert/Move use it (`315`, `515-516`); `Rename` instead keeps bare placeholders bare (`481-487`); prune exemption (`FileIndex.cpp:88-90, 116-125`); no-cascade rule (`RemoveIndexedSubtree.cpp:47-65`) |
| Orphan prune | bulk-population leftovers with unresolvable parents | `PruneBrokenParentSubtreesLocked` (`FileIndex.cpp:108-159`), via `RecomputeAllPaths` |
| Filtered-parent eviction | out-of-order child indexed before parent marked filtered | `CollectDescendantIds` (`IndexOperations.cpp:413-431`) → `FileIndex::EvictDescendantsOfFilteredParentLocked` (`FileIndex.cpp:470-479`) |
| Recycle-Bin subtree remove | rename-to-`$` soft delete | `RemoveIndexedSubtreeLocked` (`src/index/RemoveIndexedSubtree.cpp:73-123`, wrapper `125-128`) |
| Integrity latch (set-once, survives `ResetMetrics`) + cause events | journal wrap/delete/disable, 100 consecutive read errors, corrupt record, queue drop while active, post-population wrap/ID-change, unresolvable rename | flag `UsnMonitor.h:621`; single consolidated helper (`LatchIntegrityCompromised`); every latch publishes an `index_domain_events::IntegrityEvent` with the cause (`JournalWrapped/IdChanged/Lost`, `QueueBuffersDropped`, `CorruptBufferTail`, `ConsecutiveErrorsExceeded`, `RenameDivergence` in `src/index/IndexDomainEvents.h`), subscribed via `SetIntegrityEventSink` (null disables) |

## 11. Quarantine at search commit

Awaiting placeholders must never reach the results table. The
single enforcement point is `MergeAndConvertToSearchResults`
(`src/search/SearchResultUtils.cpp:83`, quarantine skip at `119`),
called from `PollResults` (`SearchController.cpp:560`):

- Predicate: `ShouldQuarantineHit` = has-awaiting AND
  `FileIndex::IsAwaitingChild(id)` (`SearchResultUtils.cpp:56-58`;
  backing check `IndexOperations.cpp:433-447` — true iff the entry
  exists and its stored parent's bucket still contains the child).
- Fast path: `has_awaiting = GetAwaitingStats().count > 0`, computed
  once per merge (`SearchResultUtils.cpp:110-114`), so the common
  no-awaiting case takes no per-hit lock.
- Rationale (`52-55`): awaiting entries are internal backlog, not
  display truth. The sweep (§9) is what eventually resolves or evicts
  quarantined entries; the quarantine only hides them meanwhile.

## 12. Result-pool and sort-cancellation contracts

The pipeline's output side is owned by `SearchController` + `GuiState`
(`ResultPoolOwner`); the ingest code must preserve two protocols:

- **Pool lifecycle / batch number.** Clear the results vector *before*
  the path pool (`ResultPoolOwner::Clear`, `GuiState.h:201-209`), and
  bump the batch number on every replacement
  (`CommitNewSearchResults`, `SearchController.cpp:341`) so
  `IncrementalSearchState::CheckBatchNumber` drops stale filtered
  copies (`SearchController.cpp:337-340`). Selection remap happens
  *before* the pool move (`311-315`, move at `324`).
- **Sort drain before replace.** Every replacement goes through
  `PrepareResultsStateForUpdate` (`SearchController.cpp:273-286`) →
  `WaitForAllAttributeLoadingFutures` (`144-168`: cancel token, spin
  until the task counter is zero, reset staging). Never call
  `CleanupAttributeLoadingFutures` first: dropping the counter
  `shared_ptr` makes the spin a no-op and lets mid-I/O tasks
  write into the freed buffer. For auto-refresh — which cannot block
  on a full drain immediately — the documented contract (AGENTS.md §
  Sort cancellation) is cancel-only (`token.Cancel()`, counter left
  alive) with the drain at finalization.

## 13. Observability

`UsnMonitorMetrics` (`UsnMonitor.h:262-427`): processing counters
(`buffers_read/processed`, `records_processed`,
`files_created/deleted/renamed/modified`), nested error counters
(`UsnMonitorErrorStats`, `UsnMonitor.h:274-291`, with
`consecutive`/`max_consecutive`), queue gauges
(`current/max_depth`, `buffers_dropped`), awaiting gauges
(`awaiting_count`, `awaiting_oldest_age_ms`,
`never_arriving_evicted`, **`healed_awaiting_total`**,
`UsnMonitor.h:299-315`), **`max_buffer_process_time_ms`** (CAS max per
buffer, `UsnMonitor.cpp:1300-1301`), and timing totals
(`UsnMonitor.h:318-325`). `GetSnapshot()` is lock-free
(`UsnMonitor.h:383-426`); the Metrics window renders it
(`MetricsWindow.cpp:146-405`, Windows-gated) alongside maintenance
stats fetched via try-lock with a 250 ms TTL cache (`160-175`,
`kMaintenanceStatsCacheTtl` at `164`) so the UI never blocks on the
USN writer lock.

## 14. Tests and invariant oracles

- `usn_two_phase_apply_tests` (Windows-only,
  `tests/UsnTwoPhaseApplyTests.cpp`; wired via `CMakeLists.txt:1618-1624`
  + `scripts/test_targets.txt:21`): synthetic buffers through
  `ProcessBufferForTest` — same-buffer clean join (`:70`), lone-child
  harness (`:97`), corrupt-tail latch (`:116`). The hook requires a
  leading next-USN like `DeviceIoControl` output (`UsnMonitor.h:508-513`).
- `WalkIndexedPath` (`FileIndexInsertPathDedupTests.cpp:35`, test-side
  helper): ground-truth path by parent-chain walk. The
  `file_index_insert_path_dedup_tests` differential cases (walk ==
  stored path, e.g. `:1090`) assert walk-result == stored path for
  every entry — the same oracle a future debug-build auditor would run
  continuously.
- `MergeAndConvertSearchResultsTests`: quarantine skip cases
  (`:174`).
- `IndexOperationsTests`: heal-counter and staging cases (tracks and
  heals out-of-order children, counts each healed placeholder once,
  awaiting-age stats, never-arriving collection and eviction).

## 15. Rules for future editors

1. **Moratorium on new side structures.** Ingest state lives in
   `FileIndex`/`IndexOperations`/`UsnMonitorMetrics` or it does not
   exist. A new map beside `awaiting_parent_` needs a design note
   justifying why the table in §10 cannot cover the fault.
2. **One writer per field.** Check the GuiState FIELD OWNERSHIP MAP
   pattern; `UsnMonitor`'s equivalents: processor thread owns
   `awaiting_*` / `never_arriving_*` staging and metrics publication; controlling thread
   owns start/stop/config; search threads take only `shared_lock`
   snapshots or atomic loads.
3. **Every result-replacement site goes through `ClearResultPool` or
   bumps the batch number** (§12) — stale `string_view`s are the
   failure mode.
4. **No I/O under the index lock; no lock nesting** (§2).
5. **Filter before insert** (§7). A filtered subtree must never exist
   downstream; resolution failure degrades to staging, never to a
   user-visible placeholder.
6. **Count only genuine heals** (§8). Counters feed the Metrics
   window; an inflated counter hides churn.
