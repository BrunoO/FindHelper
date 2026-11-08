# Spec: `FILE_ATTRIBUTE_OFFLINE` Reading & Usage (Attribute-Bitmask Indexing)

**Document:** `specs/2026-09-25_OfflineFileAttribute_Plan_Spec.md`
**Date:** 2026-09-25
**Status:** Phases 1–3 shipped (2026-09-25); Phase 4 optional follow-up.
**Scope:** Windows NTFS paths (bulk MFT, USN live, folder crawl).
**Decisions (locked):** store the full SI attribute bitmask (`uint32_t`,
future-proof for hidden/compressed/encrypted/sparse); user-visible changes
(cloud badge, offline filter, aggregate handling) are a separate optional
follow-up phase, not part of the core plan.

## 1. Goal / non-goals

- **Goal:** Know the offline-vs-local classification for every indexed entry
  at index time, from authoritative filesystem flags instead of heuristics,
  and use it in filtering, deferred loading, and cloud workflows.
- **Non-goal:** Fetching attribute *values* eagerly. Sizes/times stay lazy;
  only the classification bitmask becomes eager.
- **Non-goal:** UI changes (badge, filters, aggregates) — Phase 4, optional,
  separate commit.
- **Non-goal:** Changing search semantics. Phases 1–3 are behavior-preserving
  except for fixing the false-positive misclassification documented below.

## 2. Background (measured in-tree)

- Cloud detection today is two heuristics: `IsLikelyCloudFile`
  (`src/utils/FileSystemUtils.h:318`, plus an always-false non-Windows stub
  at :371) treats `GetFileAttributesExW` *failure*
  as "likely cloud" (per-hit syscall at filter time; access-denied/locked/
  transient files misclassify and get optimistically included in time-filter
  results via `src/search/SearchResultUtils.cpp:546`), and `IsOneDriveFile`
  (plus stub at :376) matches the substring "OneDrive" in the path
  (`PathRecomputer.cpp:133`, `FileIndex.cpp:679`).
- `CloudFileWorkflowState` (`src/gui/GuiState.h:705`) plus background futures
  exists to contain slow cloud-backed metadata loads.
- Three authoritative sources, all free: bulk MFT
  (`STANDARD_INFORMATION.FileAttributes`, already in the parsed record but not
  emitted by `RawMftRecordParser`); USN live (`USN_RECORD.FileAttributes`,
  already dereferenced in `UsnMonitor.cpp:173,242,303`); crawl (the single
  `GetFileAttributes` already issued per file).
- `FileEntry` (`src/index/FileIndexStorage.h:139`) holds only `fileSize` +
  `lastModificationTime`; no `sizeof` assert constrains growth.
  `PopulationBatchEntry` (`src/index/FileIndex.h:105`) is the bulk handoff.
- No `UpdateFileAttributes` primitive exists yet (only `UpdateFileSize` /
  `UpdateModificationTime`, `FileIndexStorage.h:211-212`), and `UsnRecord`
  carries no attributes field (only derived `is_directory`) — Phase 1.3 must
  add both the field threading and the primitive (see below).

## 3. Design

- New `FileEntry` member: `uint32_t fileAttributes = 0;` — raw NTFS attribute
  bitmask (`FILE_ATTRIBUTE_*`); a comment lists which bits consumers may trust.
- New defaulted `PopulationBatchEntry` member: `uint32_t file_attributes = 0;`
  so existing aggregate initializations (dedup tests, differential harness,
  seam fakes) keep compiling.
- Offline test everywhere: `(attrs & FILE_ATTRIBUTE_OFFLINE) != 0`
  (modern cloud files also carry recall-on-data-access, same word).
- Reference hardware / perf bar: parser emission must be within benchmark
  noise on the existing Stage-4 benchmark (4-byte copy per record).

## 4. Phases

### Phase 1 — Model + plumbing (no behavior change)

1. Add the `FileEntry` field (§3) with alignment-aware placement. Set it at
   insert time only: Step-1 `entry_ptr` capture forbids mid-recompute
   storage mutation, so lazy backfill is not an option — the current
   insert-time threading already satisfies this.
2. Add the `PopulationBatchEntry` field (§3); thread through `InsertBatch` →
   `InsertLocked`/`InsertOptions` into the stored entry.
3. **USN path:** add a flags field to `UsnRecord` from the raw record,
   carry it on live inserts, and add a `FileIndexStorage::UpdateFileAttributes`
   primitive refreshed on attribute-change USNs, so offline↔local transitions
   track without re-indexing.
4. **Crawl path:** classify from the real flag of the already-issued
   `GetFileAttributes` — no heuristic.
5. **Acceptance:** full suite green (`scripts/build_tests_macos.sh`);
   nothing reads the field yet.

### Phase 2 — Bulk parser emission

1. Copy SI `FileAttributes` into the batch entry in `ParseSingleRecord`.
2. Extend `MftDifferentialHarnessTests` to compare the mask record-for-record
   against classic-population entries.
3. **Acceptance:** harness green; Stage-4 benchmark delta within noise.

### Phase 3 — Consumer swap (the payoff)

1. Replace the three `IsLikelyCloudFile(path)` call sites
   (`SearchResultUtils.cpp:462,478,536`) with an index lookup on the stored
   mask — deletes per-hit ExW syscalls and the denied/locked false positives.
2. Replace both `IsOneDriveFile` substring checks with the flag test. The
   `PathRecomputer.cpp:133` site runs inline on the entry being processed,
   so it becomes a direct field test with no lookup at all.
3. `cloud_files.deferred_ids` is intentionally NOT seeded at population:
   the set dedups in-flight background loads spawned per filter, so seeding
   all offline entries would mark them loaded-without-a-future and their
   times would never resolve. Classification is exact at each call site
   instead; the deferred mechanism keeps working on demand.
   (Supersedes the earlier seed-at-population draft.)
4. Delete `IsLikelyCloudFile` + `IsOneDriveFile` **including their
   non-Windows stubs** (`FileSystemUtils.h:371,376`) and their tests after
   verifying no other callers; keep a thin exact-flag helper only for paths
   the index doesn't cover.
5. **Acceptance:** full suite green, plus a new fixture mixing offline,
   access-denied, and local files proving time-filter results no longer
   misclassify (the regression test for the false-positive fix).

### Phase 4 — Optional UI follow-up (separate commit, out of core scope)

Cloud badge in results, "available offline" filter, offline-size handling in
folder aggregates, `RELEASE_NOTES.md` entry.

## 5. Risks

- **USN mask staleness** if attribute-change events are filtered upstream —
  covered by Phase 1.3; test with a synthetic offline→local transition.
- **Non-NTFS sources** (exFAT/FAT32 crawls): flags remain valid Windows
  attributes with fewer bits set — no special-casing.
- **Struct growth** (~4 B × entry count): negligible vs. the path pool;
  no layout assert exists.

## 6. Effort estimate

Phases 1–2: ~200–300 lines plus tests. Phase 3: ~100 lines, net-negative
(deletions dominate). Phase 4: scoped separately when promoted.
