# Spec: Sub-2-Second Cold Indexing via Raw MFT Bulk Pipeline

**Document:** `specs/2026-09-24_Sub2s_MFT_Indexing_Plan_Spec.md`
**Date:** 2026-09-24
**Status:** Proposed (pre-implementation)
**Scope:** Windows initial index population only (NTFS bulk path).
**Related:** `internal-docs/analysis/2026-09-23_HIGH_SPEED_MFT_BULK_READING_FINDINGS.md`
(problem analysis), `internal-docs/plans/2026-09-20_RAW_MFT_BULK_INDEXING_ARCHITECTURE_PLAN.md`,
`internal-docs/optimization/2026-09-21_RAW_MFT_LESSONS_LEARNED.md` (prior attempt retrospective).
**Target:** Cold-indexing parity with Everything/WizTree (~1–2 s for ~800k files)
on reference hardware, via a gated phased plan. Each phase buys an option plus
a measurement; no phase commits to the next.

## 1. Goal / non-goals

- **Goal:** Reduce Windows cold-index time for ~800k files to under 2 seconds on
  reference hardware (NVMe SSD, reasonably contiguous MFT — see §2), with
  byte-identical search results vs. the current pipeline.
- **Non-goal:** Changing search semantics, ranking, filters, or the USN live
  path. The bulk pipeline replaces *initial population* only; handoff to the
  journal preserves live behavior (see Phase 3).
- **Non-goal:** SATA / heavily fragmented MFT parity. Those get a measured
  floor (§7), not the 2 s target.
- **Non-goal:** A fixed-scope commitment to the folder-table model. Option 2
  (see §5) is a time-boxed spike behind a seam, promoted only on measurement.

## 2. Background and reference numbers (measured)

- USN enumeration + per-file `FSCTL_GET_NTFS_FILE_RECORD`: 20–60 s with
  metadata (~6–7 s without). Per-file metadata reads are the cost to eliminate.
- Prior raw-MFT attempt: ~4.8 s Release (~10.4 s Debug). Forensics identified
  four bottlenecks: lockstep sync I/O, per-record `memcpy` + per-name
  `WideCharToMultiByte`, the recompute wall, eager full-path materialization.
- `FileIndex::RecomputeAllPaths()`: ~5.6 s Debug / ~2.5 s Release standalone,
  split across depth sort, 800k path joins, `PathStorage` insert (hash pass 1),
  and `RebuildPathToIdMapLocked` (hash pass 2, ~1.5 s of pure waste).
- Folders are 3–5% of records (~30k of 800k): the structural fact behind Option 2.

**Reference hardware (normative for acceptance):** NVMe SSD, NTFS with 512 B
sectors (4Kn covered as a correctness case, not a performance target), MFT
fragmentation within the 90th percentile of field telemetry once collected;
otherwise a synthetic contiguous image. All timings are Release builds.

## 3. Architecture: four seams

Stage boundaries are virtual interfaces at batch granularity (never per-record;
hot loops stay template/flag-dispatched). Data crosses seams as plain structs:

| Seam | Responsibility | First implementation | Future swap |
|---|---|---|---|
| `ExtentProvider` | Volume geometry + `$MFT` runlist decode (extends `VolumeGateway`) | ioctl-based | Cached/fake extents (tests) |
| `ChunkReader` | Fill next sector-aligned buffer | Double-buffered ping-pong (I/O thread + worker) | Ring buffer with async overlapped I/O |
| `RecordParser` | Chunk bytes → flat record arrays (idempotent in-place USA fixup, fast-ASCII names, `$DATA` sizing per the $FILE_NAME-removal rule) | Parallel chunk dispatch | SIMD transcoding, forensic validation |
| `Ingestor` | Records → populated index | SoA bulk fill (current model) | Folder-table fill (Option 2) |

`VolumeGateway`'s scripted-fake pattern is the template for all four fakes.
`MftMetadataReaderTests.cpp` synthetic-record builders are the template for
parser tests. Streaming order is record-number order, **not** topological
(moves, reuse, parallel completion order) — the awaiting/heal pass stays under
both options, as does the double-click stale-view guard.

### Observability, Telemetry & Phase Timers (Mandatory)

To enable informed, data-driven decisions and eliminate the risk of silent fallbacks or hidden bottlenecks (Lessons 1 & 4 from retrospective):
- **Path-Taken Markers:** Explicitly log at startup whether the bulk fast path or classic USN fallback is engaged (e.g., `PopulateInitialIndex starting (fast_mft=true)`, `Attempting Fast Bulk MFT Stream...`). Any fallback to classic enumeration must log the specific error code and reason. Never fall back silently.
- **Granular Phase Timers (`ScopedTimer` / `LOG_INFO_BUILD`):** Every significant piece of code and pipeline stage must have dedicated duration timers:
  - Extent mapping & runlist decoding time (`ExtentProvider`).
  - Chunk streaming I/O time, throughput, and total bytes (`ChunkReader`: MB read, MB/s).
  - Parallel chunk parsing and record decoding time (`RecordParser`).
  - Extension record resolution time (`ResolveExtensionRecords`).
  - Ingestion and batch commit time (`Ingestor::CommitBatch`).
  - Sub-phase recomputation timers: BFS depth calculation, path string assembly, hash table insertion, and orphan pruning.
- **Telemetry Counters:** Log total bytes streamed, chunks read, base records parsed, extension records merged, records filtered, USA fixup failures, and parse errors.

## 4. Phased plan with gates

### Phase 0 — Measure (prerequisite, no product code)

Break Release Stage-4 time into components on reference hardware: depth
sort/BFS, path joins, hash pass 1, hash pass 2, map rebuild, pool commit.
**Exit:** a committed benchmark table with per-component seconds.
**Gate:** numbers replace the §2 estimates everywhere below; if hash pass 2 is
not ~1 s+, re-derive Phase 1 scope.

### Phase 1 — Pipelined I/O + parsing, current data model (bounded)

1. `ExtentProvider` + double-buffered `ChunkReader` + parallel `RecordParser`
   (idempotent in-place fixup, fast-ASCII with `WideCharToMultiByte` fallback,
   `$DATA`-only sizing, and base/extension record stitching via
   `ResolveExtensionRecords`).
2. Ingest through the existing model: `Ingestor` = SoA bulk fill + current
   recompute minus `RebuildPathToIdMapLocked` (fold map updates into the build
   pass — delete the second hash).
3. Differential harness (normative): classic pipeline vs. fast pipeline must
   agree record-for-record (ids, parent links, sizes, times, materialized
   paths) on synthetic images covering §6 edge cases.
4. USN handoff: snapshot `NextUsn`/`UsnJournalId` before the walk. Post-walk,
   re-query to verify `UsnJournalId` unchanged and no journal wrap; replay live
   events forward (unchanged semantics).

**Exit:** green harness + measured total on reference hardware.
**Gate:** if total < 2 s, **stop and ship**. Else proceed; the measured gap
sizes Phase 2.

### Phase 2 — Folder-table spike (time-boxed, behind the seam)

1. Implement folder-table `Ingestor`: full paths for folders only (~30k);
   per-file `{folder_index, leaf_name, size, timestamp}`; on-demand path
   assembly for display/search/export.
2. Blast-radius survey first: enumerate every consumer of eager file paths
   (search matching, results table, sorting, export, `path_to_id` lookups) and
   adapt behind the seam.
3. Same differential harness must pass; add a search-equivalence suite
   (result sets identical for a query corpus).

**Exit:** spike timing + survey cost estimate.
**Gate:** promote to migration only if spike projects total < 2 s *and* the
survey cost fits a single follow-up phase. Otherwise stay on Phase 1 output.

### Phase 3 — Harden and ship (either option)

Deterministic tests for §6, fragmented-MFT and SATA floor measurements,
release notes with the hardware qualifier, and removal (not coexistence) of
the superseded bulk path.

## 5. Options recap

- **Option 1** (Phase 1 endpoint): current model, single hash pass, parallel
  frontier BFS, pre-sized arena. Ceiling ~2–3 s; fallback that ships regardless.
- **Option 2** (Phase 2): folder table, finalization < 50 ms. Only route to
  the 2 s target; adopted only via the Phase 2 gate.

## 6. Correctness edge cases (normative test list)

Idempotent fixup on pre-fixed (`FSCTL_GET_NTFS_FILE_RECORD`) vs. raw sectors;
4Kn sector geometry for USA fixup; multi-record file link resolution (non-zero
`BaseFileRecordNumber` merged into base record via `ResolveExtensionRecords`);
fragmented `$MFT` with `$ATTRIBUTE_LIST` extension records; sparse/compressed
runs; resident vs. non-resident `$DATA` with non-zero `LowestVcn`; short-name
vs. long-name `$FILE_NAME` records (skip DOS 8.3 aliases); moved-file
parent-newer-than-child ordering; USN replay gap (events landing mid-scan
applied exactly once); empty-file vs. missing-`$DATA` size semantics
(`kFileSizeNotLoaded` → lazy loader, never cached 0).

## 7. Risks

- **I/O-Parsing serialization bottleneck:** synchronous I/O waiting on CPU
  parsing leaves SSD bandwidth idle; mitigated by double-buffering in Phase 1
  so disk DMA never waits for CPU parsing.
- **Reference-hardware myopia:** field MFTs fragment; mitigate with the
  measured floor and the hardware qualifier in release notes.
- **Option 2 migration stall:** mitigate with the time-box + Phase 1 fallback.
- **Parallel nondeterminism:** chunk completion order must not affect output;
  the differential harness runs with forced interleave seeds.
- **Handoff gaps:** snapshot-before-walk + replay is load-bearing; test with
  synthetic journal writes during the scan and verify journal ID consistency.

## 8. Effort estimate

Phase 0: benchmarks only (~100 lines harness). Phase 1: ~600–900 lines
(seams + fakes, parser, map-fold, harness) — the only committed scope.
Phase 2 spike (time-boxed): ~400–600 lines + survey. Phase 3 follows measurement.
