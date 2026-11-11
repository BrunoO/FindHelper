# Specification: NTFS Hard Links and Filter Parity with Everything

**Document:** `specs/2026-09-19_NTFS_Hard_Links_And_Filter_Parity_Spec.md`
**Date:** 2026-09-19
**Status:** Proposed — **CONDITIONAL GO**; production approval is gated on Windows live-update validation
**Revision:** R3 (2026-11-09) — Replaced direct per-link USN mutation with authoritative asynchronous MFT reconciliation; aligned Option B with existing synthetic IDs and canonical FRN mapping; added rollout gates, failure semantics, and per-link rename/move coverage.
**Revision History:** R2 (2026-10-19) — §3.1 identity decision fixed to **Option B**; rejected alternatives recorded.
**Target:** USN_WINDOWS / FindHelper (C++17, Windows NTFS / Cross-Platform)

---

## 1. Overview, Value & Decision

### 1.1 Measured motivation

When indexing an identical Windows 7 NTFS system partition (C:), Voidtools **Everything** and **FindHelper** reported:
- **Everything:** 576,711 items
- **FindHelper:** 551,868 items
- **Count difference:** 24,843 items (+4.5% in Everything)

A full path-by-path diff established that **551,867 items matched bit-identically**. FindHelper also had one item absent from Everything, so 24,844 Everything paths were missing from FindHelper despite the 24,843 net count difference:

| Category | Missing Count | % of Difference | Root Cause in FindHelper |
|---|:---:|:---:|---|
| **NTFS Hard Links** | 23,585 | 94.93% | `FileIndexStorage` keys entries by 64-bit `NtfsFileReference`, collapsing multiple directory links into one entry. |
| **System & Recycle Bin Files** | 1,247 | 5.02% | `SystemPathFilter` prunes `$`-prefixed entries. |
| **Volume Root Entry** | 1 | < 0.01% | Everything exports the root object as an indexed row. |
| **Transient Runtime Files** | 11 | 0.04% | Files changed between scans. |
| **Total** | **24,844** | **100.0%** | |

Hard links therefore represented **94.93% of the measured missing paths** and approximately **4.09% of Everything's total rows**. This is a path-completeness defect, not merely result inflation: NTFS has no semantic "original" hard link, so retaining an arbitrary link can hide the path through which the user expects to find a file.

**Evidence limitation:** this measurement came from Windows 7, and the CSV files referenced by R1 are not present in the current checkout. Regenerate a current comparison before using exact counts as a release acceptance oracle.

### 1.2 Decision

**The feature is worth implementing, but the R1/R2 direct-USN plan must not be implemented unchanged.**

- **GO:** bounded cold-model implementation, Windows journal validation, and asynchronous reconciliation design.
- **NO-GO:** shipping direct per-link mutations inferred from close-only USN reason masks.
- **NO-GO:** shipping cold-index hard-link support while existing canonical-FRN live handlers continue to mutate only one link.

Production approval requires the gates in §6. If close-only notifications cannot reliably trigger convergence, or reconciliation violates correctness/performance constraints, defer the feature rather than ship an approximately correct live index.

### 1.3 Scope

This specification covers:
1. **NTFS hard links** so every user-visible directory path pointing to the same MFT file record is searchable.
2. **Configurable system-file filtering** for Everything parity.

Recycle Bin precedence remains defined by `specs/2026-10-11_WINDOWS_RECYCLE_BIN_INDEXING_SPEC.md`.

---

## 2. Technical Analysis & Existing Limitations

### 2.1 Hard-link collapse in `FileIndexStorage`

In NTFS, each file is represented by one MFT record and File Reference Number (FRN). Each hard link is a distinct `$FILE_NAME` attribute carrying a parent FRN and name. Multiple links are common in `WinSxS`, `System32`, `SysWOW64`, Edge `resiliencylinks`, and Git `libexec`.

`RawMftRecordParser::EmitEntriesForNames` already collects and emits every applicable non-DOS `$FILE_NAME`. The entries subsequently collapse because `FileIndexStorage` stores:
```cpp
flat_hash_map_t<ntfs_file_reference::NtfsFileReference, FileEntry> id_to_entry_;
```
All emitted links initially carry the same FRN, so `InsertLocked` refreshes one entry instead of retaining every directory path.

### 2.2 NTFS invariants

- **Directories cannot have hard links:** the directory hierarchy remains a strict tree/DAG.
- **Only non-directory files have hard links:** additional links are leaf entries.
- **FRN identifies the file, not an individual link:** an individual link is identified by its canonical parent FRN and exact name, subject to the DOS namespace policy.
- **No link is inherently original:** "primary" in this specification is only a FindHelper ID-compatibility role.

### 2.3 Current live-update assumptions

The live monitor currently:
- sets `READ_USN_JOURNAL_DATA_V0::ReturnOnlyOnClose = TRUE` (`src/usn/UsnMonitor.cpp`);
- resolves an FRN to one canonical indexed entry through `ResolveEntryReference`;
- applies create/delete/rename/move and metadata updates to that entry.

Those assumptions are valid for one entry per FRN but insufficient for a multi-link file. In particular, a rename can otherwise mutate the wrong link, and deleting the FRN-bearing entry can strand synthetic secondary entries.

---

## 3. Architecture & Data Flow

### 3.1 Identity model — DECIDED: Option B, aligned with current infrastructure

**Decision:** preserve `fileId == NtfsFileReference` for the normal single-link case and for one initial compatibility entry of a multi-link file. Assign synthetic IDs only to additional links. The identity exception is quarantined to multi-link records.

**Rejected alternatives** (recorded to prevent re-litigation):
- **Option A — composite `(FRN, parentFRN, name)` key for every entry:** changes `fileId` for all entries while still requiring an FRN-to-links relation for USN and MFT joins. Rejected.
- **Option C — multiple search rows sharing one `fileId`:** breaks fileId-keyed selection remap and Recycle Bin snapshot reconciliation. Rejected.

#### Required model

1. **Link identity:** define `LinkKey` as canonical parent `NtfsFileReference` + exact `FileName`. DOS 8.3 aliases continue to follow the existing namespace policy and are not separate user-visible hard links.
2. **Entry IDs:**
   - Single-link files retain their full FRN as `fileId`.
   - At initial ingestion, the first retained link uses the FRN; additional links use the **existing** `FileIndex::next_file_id_` synthetic-ID allocator.
   - Reuse the existing allocator and its collision invariants. Do not create a second counter and do not initialize a counter from the maximum observed MFT record number.
   - Never encode a link ordinal in FRN high bits: a real FRN uses all 64 bits (48-bit record + 16-bit sequence).
3. **Canonical lookup:** retain `record_number_to_id_` as the fast canonical-FRN lookup used by stale-sequence-tolerant resolution.
4. **Link-group relation:** extend storage with a sparse relation from `MftRecordNumber` to all links for multi-link files. Each relation must support lookup by `LinkKey`, enumeration for metadata fan-out, and reverse cleanup when an entry is removed. Only multi-link records should pay this storage cost.
5. **Stable reconciliation:** unchanged `LinkKey`s retain their entry IDs across MFT reconciliation. Do not renumber surviving links merely because the FRN-bearing link disappeared; the canonical mapping may designate a surviving synthetic entry while the full FRN remains the file-group identity.
6. **Metadata fan-out:** size, modification-time, and applicable attribute invalidation/update operations must target every entry in the link group.
7. **Batch ingestion:** thread link/group origin through `PopulationBatchEntry` and `InsertBatch`; the current unconditional `register_mft_record=true` batch policy must not accidentally register each synthetic secondary as a separate MFT record.

`RawMftRecordParser::EmitEntriesForNames` (`src/index/mft/RawMftRecordParser.cpp`) already supplies the required names. The main cold-index work is therefore identity/link-group ingestion, not attribute discovery.

```
               ┌───────────────────────────────┐
               │  MFT Record # 0x00012345      │
               └──────────────┬────────────────┘
                              │
              ┌───────────────┴────────────────┐
              ▼                                ▼
   [Compatibility entry: FRN ID]    [Secondary entry: synthetic ID]
   LinkKey: System32 + tracerpt.exe LinkKey: WinSxS\amd64_... + tracerpt.exe
```

### 3.2 Path resolution (`PathRecomputer`)

Because directory paths form a strict tree:
1. Secondary link IDs appear as leaf children of their respective parent directories.
2. Existing topological path recomputation resolves every link independently.
3. Every resolved path is inserted into `PathStorage`, making all links discoverable without adding fields to `SearchResult`.
4. Reconciliation must update membership, paths, `path_to_id` mappings, inverted indexes, and link-group relations in one atomic index commit.

### 3.3 Live USN monitoring — authoritative MFT reconciliation

#### Reason bits are notifications, not per-link opcodes

Add `HardLinkChange = 0x00010000U` to `UsnReason` and include it in both `kInterestingReasons` and `kActionReasons` when this feature is active, so a `HARD_LINK_CHANGE | CLOSE` record is not discarded by `CarriesAction`. Dispatch affected multi-link records to reconciliation **before** the existing canonical single-entry create/delete/rename handlers. Do **not** assume:
- link creation is always `HARD_LINK_CHANGE | FILE_CREATE`;
- non-final deletion is always `HARD_LINK_CHANGE | FILE_DELETE`;
- final deletion is distinguishable solely from a bare `FILE_DELETE`.

[Microsoft's USN documentation](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ns-winioctl-usn_record_v4) defines `FILE_CREATE` as first-time file creation and `HARD_LINK_CHANGE` only as link addition/removal. Close records summarize accumulated reasons without preserving operation order. Observed reason combinations may be logged for diagnostics but must not define the correctness contract.

Do not solve this by globally disabling `ReturnOnlyOnClose`: that increases journal traffic and reintroduces partial-record ordering hazards that the current pipeline deliberately avoids.

#### Reconciliation algorithm

For `HardLinkChange`, and for relevant create/delete/rename/data events belonging to a known multi-link group:

1. The USN processor enqueues/coalesces the **full FRN**. It performs no synchronous disk I/O.
2. A background worker reads the complete current logical MFT record through `volume_gateway::VolumeGateway::GetFileRecord`, including extension records where applicable.
3. Parse the authoritative current `LinkKey` set and shared metadata.
4. Diff it against the indexed link group: additions, removals, renames, and moves are all expressed as current-set differences.
5. Apply the complete diff under one `FileIndex` lock, preserving IDs for unchanged links and updating all dependent indexes and metadata.
6. Publish the normal domain events after the index commit; subscribers retain their existing no-I/O/no-index-mutation contract.

#### Mandatory failure and ordering semantics

- **Record still present:** replace the link set with exactly what the MFT contains, regardless of the journal reason mask.
- **Authoritatively deleted full FRN:** remove the entire group, including all synthetic entries.
- **Transient read failure/access error:** retain old state, mark the FRN dirty, and retry with bounded backpressure. Never interpret arbitrary I/O failure as deletion.
- **Sequence mismatch/record reuse:** never apply names from a new file generation to the old group.
- **Per-FRN ordering:** serialize reconciliation per FRN or use generation guards so stale worker completions cannot overwrite newer state.
- **Queue saturation:** coalesce by FRN; an unrecoverable drop must raise the existing integrity latch and trigger the established rescan path, not silently lose correctness.
- **Journal gaps, journal-ID changes, and buffer loss:** retain existing volume-invalidation/rescan behavior. Reconciliation cannot recover notifications that were never observed.
- **Cold-only milestone:** keep multi-link ingestion test-only until reconciliation or a conservative whole-group invalidation/rescan fallback is active.

### 3.4 Configurable system & Recycle Bin filtering

> **Precedence Note (2026-10-11):** the earlier `"include_recycle_bin"` persistence proposal is superseded by `specs/2026-10-11_WINDOWS_RECYCLE_BIN_INDEXING_SPEC.md`. Recycle Bin contents are indexed unconditionally; visibility is controlled through filtering rather than a rebuild-triggering toggle.

Proposed parity settings in `Settings.h` / `settings.json`:
```json
{
  "indexing": {
    "index_hard_links": true,
    "include_system_files": false
  }
}
```
- `include_system_files == false`: retain normal filtering of NTFS metadata such as `$MFT`, `$LogFile`, and `$Extend`.
- `include_system_files == true`: allow raw-volume parity with Everything.
- Hard-link support must not change the established Recycle Bin visibility policy.

---

## 4. User Stories & Acceptance Criteria

| ID | Title | Given | When | Then |
|---|---|---|---|---|
| **US-1** | Hard-link discovery | A file has multiple links | User searches for its name | Every retained hard-link path appears with consistent shared metadata. |
| **US-2** | Link-specific deletion | Links A and B exist | A is deleted | A disappears; B remains searchable; no ghost or duplicate remains after reconciliation. |
| **US-3** | Shared data update | Links A and B exist | A is modified | Size/time invalidation and subsequent loaded metadata remain consistent for both links. |
| **US-4** | Everything parity | Parity filters are enabled | A current comparison dataset is generated | Counts and paths match within the documented transient-file margin. |
| **US-5** | Clean default mode | Default settings are used | User searches the drive | NTFS metadata clutter remains filtered; hard-link paths are not arbitrarily discarded. |
| **US-6** | Per-link rename/move | A file has multiple links | Either the FRN-bearing or a synthetic link is renamed/moved | After quiescence, indexed paths exactly equal a fresh MFT scan. |
| **US-7** | Final deletion | A multi-link file exists | Its final link is deleted | The complete link group is removed, including synthetic entries. |
| **US-8** | Failure safety | Reconciliation is delayed or reads fail | Events complete out of order, records are reused, or the journal gaps | Stale completions never win; transient errors never become deletes; integrity loss forces rescan. |

---

## 5. Performance & Memory Hypotheses

The Windows 7 sample adds **23,585 entries (~4.27% of FindHelper's former count)**:
- `FileEntry` payload estimate: 40 bytes × 23,585 ≈ 943 KB.
- Path payload estimate: ~120 bytes × 23,585 ≈ 2.8 MB.

These are **payload estimates, not a total heap bound**. Link-group relations, hash-table capacity, allocator overhead, and path indexing must be measured. The expected search-scan growth is roughly proportional to entry count, but the <4% R1 estimate is not an acceptance guarantee.

Requirements:
- No per-candidate hard-link checks in the search hot loop; link rows are ordinary `PathStorage` entries after ingestion.
- Sparse link-group storage only for multi-link records.
- No synchronous MFT I/O on the USN monitor thread or under the `FileIndex` lock.
- Cold-index and search regressions must remain within the project's existing **≤5% gate**.
- Reconciliation queue depth, retries, and convergence latency must be bounded and observable.

---

## 6. Phased Implementation & Verification Gates

### Gate 0 — Current value and Windows journal validation

1. Regenerate an Everything/FindHelper path comparison on a currently supported Windows installation; do not depend on the absent Windows 7 CSVs.
2. Capture close-only journal records for:
   - second and third link creation, same and different parents;
   - deletion of the FRN-bearing link and a synthetic link;
   - final-link deletion;
   - same-parent rename and cross-directory move of each link;
   - data modification through each link;
   - multiple link operations before handle close and rapid add/delete/rename sequences.
3. Record full self/parent FRNs, names, reason masks, USNs, and close flags.
4. **Pass criterion for close-only reconciliation:** every supported state-changing sequence produces at least one usable close-only notification carrying the file FRN, and subsequent MFT reconciliation converges to the actual final link set.
5. If any operation has no usable trigger, stop and redesign notification coverage; do not fall back to undocumented reason-mask assumptions.

### Gate 1 — Cold link model (test-only until live safety exists)

Implement Option B and verify on macOS with synthetic fixtures:
- `HardLinkIdentityTests`: single-link FRN stability, synthetic-ID uniqueness, canonical lookup, stable IDs across reconciliation, and no high-bit/record-number aliasing.
- `HardLinkInsertionTests`: duplicate FRNs with distinct parents/names resolve independently in `PathRecomputer`.
- `HardLinkRemovalTests`: remove one link, remove the FRN-bearing link without stranding survivors, and remove the final link.
- `HardLinkSizeInvalidationTests`: metadata fan-out reaches every link.
- Namespace, extension-record, primary-deletion, selection-remap, and Recycle Bin fileId interaction cases.

Run:
```bash
./scripts/build_tests_macos.sh --no-asan
```

This milestone must not ship as complete live support without Gate 2 or a conservative whole-group invalidation/rescan fallback.

### Gate 2 — Live convergence and fault handling

Starting from a cold index, execute the Gate 0 operation matrix through the real monitor, reconciliation queue, and MFT reader. After quiescence, require exact equality with a fresh scan: no ghosts, missing links, duplicates, or wrong-link renames.

Also verify:
- file deleted before the worker reads it;
- several coalesced notifications for one FRN;
- delayed and out-of-order worker completion;
- transient links created and removed between reads;
- transient read failures and retries;
- sequence mismatch/record reuse, fault-injected where necessary;
- queue saturation and journal-gap rescan;
- long-held-handle visibility delay is understood and accepted.

### Gate 3 — Performance and release approval

1. Record and compare the standardized search benchmark:
   ```bash
   ./scripts/run_search_benchmark.sh --config tests/benchmark_config_sample.json --record-baseline baseline_pre_hardlinks.json
   ./scripts/run_search_benchmark.sh --config tests/benchmark_config_sample.json --compare baseline_pre_hardlinks.json --verify --max-regression-pct 5.0
   ```
2. On Windows, measure cold indexing with a populated hard-link corpus; require ≤5% duration regression.
3. Measure actual heap delta and reconciliation CPU/I/O under event bursts.
4. Confirm Open/Delete/Copy/Export/drag and selection behavior operate on each physical link path exactly once.
5. Only after Gates 0–3 pass may the status change from **Proposed / Conditional GO** to **Implemented / Approved for Release**.
