# Specification: Windows Recycle Bin Indexing & Filter Policy Consolidation

**Document ID:** `specs/2026-10-11_WINDOWS_RECYCLE_BIN_INDEXING_SPEC.md`  
**Date:** 2026-10-11  
**Status:** IMPLEMENTED  
**Revision:** R3 (2026-10-17) — Phases 1-4 completed (`2567aae4`, `40aa1087`, `4278f12d`, `9a31732a`).  
**Revision History:** R2 (2026-10-12) — Phase 4 correctness gaps closed: pool-string ownership & snapshot-hit reconciliation, metadata eviction lifecycle, policy tracker seeding / runtime SID discovery, `$I` queue producers. Helper citations corrected, Acceptance Criteria table restored, Quick Filter input form pinned.  
**Target:** Windows (NTFS USN / MFT), cross-platform testable components on macOS/Linux  
**Standard:** C++17 Strict, Dear ImGui, doctest  

---

## 1. Document Lineage, Scope & Precedence

### 1.1 Lineage & Precursor Documents
This specification synthesizes and succeeds previous architectural explorations:
- **Primary Precursor:** [`internal-docs/plans/2026-09-12_RECYCLE_BIN_INDEXING_FROM_SCRATCH.md`](file:///Users/brunoorsier/dev/USN_WINDOWS/internal-docs/plans/2026-09-12_RECYCLE_BIN_INDEXING_FROM_SCRATCH.md) (authoritative design for all-users SIDs, read-only search, parent-chain tracking, and immutable snapshot joins).
- **Historical Reference:** [`internal-docs/plans/2026-09-11_RECYCLE_BIN_INCLUSION_PLAN.md`](file:///Users/brunoorsier/dev/USN_WINDOWS/internal-docs/plans/2026-09-11_RECYCLE_BIN_INCLUSION_PLAN.md) (stopped after Phase 1 DRY centralization; historical context on `$I` binary formats).
- **Format Reference:** [`docs/platform/windows/NTFS_RECYCLE_BIN_PARSER.md`](../docs/platform/windows/NTFS_RECYCLE_BIN_PARSER.md) (comprehensive C++17 `$I` parser reference implementation).

### 1.2 Direct Conflict Resolution & Precedence
- **Superseded Specification:** [`specs/2026-09-19_NTFS_Hard_Links_And_Filter_Parity_Spec.md §3.4`](file:///Users/brunoorsier/dev/USN_WINDOWS/specs/2026-09-19_NTFS_Hard_Links_And_Filter_Parity_Spec.md#L113-L128) proposed a persistent setting `"include_recycle_bin": false` in `settings.json`.
- **Precedence Rule:** This specification **supersedes** that section. Recycle Bin contents are indexed **unconditionally** by default. Controlling visibility via a persistence setting forces an expensive, disruptive full-volume index rebuild every time the toggle is flipped. Instead, the Recycle Bin is always indexed into `FileIndex`, and search visibility is controlled via the **Quick Filter** (populating `$Recycle.Bin` into the Path filter) or normal query filtering.

### 1.3 Scope Decoupling: Focus on Recycled Files (Hard Links Explicitly Out-of-Scope)
While precursor documents (such as `2026-09-19_NTFS_Hard_Links_And_Filter_Parity_Spec.md`) paired hard link indexing with system file and Recycle Bin filtering:
- **This feature is strictly scoped to Recycle Bin indexing and filter policy consolidation.**
- **Hard Links are Out-of-Scope:** Multiple MFT file name attributes, secondary hard link IDs, `USN_REASON_HARD_LINK_CHANGE` handling, and `index_hard_links` settings are **completely decoupled** and deferred to future work.
- **Rationale for Decoupling:**
  1. **Minimal Blast Radius:** Indexing the Recycle Bin requires zero changes to the underlying `FileIndex` / `PathStorage` link-adjacency graph; it only requires container ingestion and name/path classification.
  2. **Isolated Verification:** Keeps performance gating, filter deduplication, and testing focused and reviewable in isolation without risking link-management regressions.
- **Legacy Pre-Vista Bin Out-of-Scope:** XP-era `C:\RECYCLED\`, `C:\RECYCLER\<SID>\INFO2` artifacts are not recognized; only Vista+ `$Recycle.Bin\<SID>\` `$R`/`$I` pairs are in scope.


---

## 2. Executive Summary & Problem Statement

### 2.1 Context
In Windows NTFS, deleting a file or folder in File Explorer triggers a **soft delete**:
1. The payload is moved to `X:\$Recycle.Bin\<User-SID>\`.
2. The payload is renamed to `$RXXXXXX.<ext>` (file) or `$RXXXXXX` (directory).
3. A companion metadata file `$IXXXXXX.<ext>` (file) or `$IXXXXXX` (directory) is written in the same directory, recording the original file path, deletion timestamp, and original file size.

### 2.2 Current Ingestion Filter Audit (Why Recycled Files are Absent)
FindHelper currently excludes all items in the Recycle Bin as a side effect of generic `$`-prefix system filtering:
- **`src/index/SystemPathFilter.h` / `cpp`**: Holds primitives `IsSystemPrefixedName` and `FilteredDirTracker`.
- **`src/index/InitialIndexPopulator.cpp`**:
  - In `IndexBuildRun::StageRecord` (journal enum): `$Recycle.Bin` matches `IsSystemPrefixedName`, is recorded in `filtered_dirs_`, and skipped. Per-user `<SID>` folders match `IsFilteredChild` and are skipped. All `$R` and `$I` files are skipped.
  - In `IndexBuildRun::RunRawMft` (raw MFT streaming): Exactly duplicates the `StageRecord` filtering lines.
- **`src/usn/UsnMonitor.cpp`**:
  - `HandleSystemFileFilter`: Filters `$`-names and children of filtered dirs.
  - `HandleRenameNewName`: Intercepts soft-delete renames to `$R...` and forcibly **evicts** the entry and its subtree from `FileIndex` via `ApplySubtreeRemoval`.
  - `HandleRenameForUnknownEntry`: Rejects recovery inserts for recycled artifacts.
- **`src/path/PathUtils.cpp` & `src/ui/FilterPanel.cpp`**:
  - The "Recycle Bin" quick-filter button calls `path_utils::GetTrashPath()`.
  - On Windows, `SHGetKnownFolderPath(FOLDERID_RecycleBinFolder)` fails because the Recycle Bin is a virtual shell namespace folder (`::{645FF040-5081-101B-9F08-00AA002F954E}`).
  - It falls back to `GetDefaultUserRootPath()` (`C:\Users\`), which matches zero recycled files.

### 2.3 Strict Performance & Architecture Constraints
1. **Zero Cold-Index Degradation (Gate ≤ 5.0%)**: Cold index scans over 1,000,000 files in under 2 seconds. Synchronously opening and reading `$I` metadata files from disk during initial indexing is strictly prohibited.
2. **Non-Blocking USN Stream**: Real-time USN consumption must never perform synchronous file I/O on the monitor thread (avoids journal overflows).
3. **Cache Line & Layout Pinning (INV-4)**: `SearchResult` layout is strictly pinned:
   `static_assert(offsetof(SearchResult, fileId) + sizeof(SearchResult::fileId) <= 64)` and `static_assert(sizeof(SearchResult) <= 208)`. No new fields may be added to `SearchResult`.
4. **UI Thread Confinement (.agents/rules/ui-state-write-confinement-cpp.md)**: Worker threads must never mutate `GuiState` display structures. Enrichment must use immutable snapshot handoff.

---

## 3. Technical Mechanics: Soft Delete, SIDs, & Edge Cases

### 3.1 Soft-Delete File System & USN Lifecycle
| Action | File System Behavior | USN Journal Record |
|---|---|---|
| **File Soft Delete** | `C:\docs\a.txt` moved & renamed to `C:\$Recycle.Bin\<SID>\$Ra1b2c3.txt`. `$Ia1b2c3.txt` created. | `USN_REASON_RENAME_OLD_NAME` (`a.txt`), `USN_REASON_RENAME_NEW_NAME` (`$Ra1b2c3.txt`), `USN_REASON_FILE_CREATE` (`$Ia1b2c3.txt`). |
| **Folder Soft Delete** | `C:\folder` moved & renamed to `C:\$Recycle.Bin\<SID>\$Rxyz123`. `$Ixyz123` created. Children remain inside `$Rxyz123` with original relative names. | Rename on folder; create on `$I`; **no per-child USN records emitted**. |
| **Restore** | `$R` file/folder moved & renamed back to original path. `$I` file deleted. | `USN_REASON_RENAME_NEW_NAME` back to live directory; `USN_REASON_FILE_DELETE` on `$I`. |
| **Empty Bin** | `$R` and `$I` files deleted permanently. | `USN_REASON_FILE_DELETE` on `$R` and `$I`. |

### 3.2 Parent-Chain Scoped Recognition (INV-5)
- **Rule:** A file or folder is **only** recognized as a Recycle Bin payload if its parent directory is a recognized `<SID>` directory located directly under `$Recycle.Bin`.
- **Desktop/User File Safety:** A user file named `$Report.docx` on `C:\` or on the Desktop starts with `$` and is NOT under a `<SID>` folder. It remains excluded under standard system-file filtering.
- **In-Bin Non-`$R` Files:** System files inside the bin (such as `desktop.ini` inside `$Recycle.Bin\<SID>\`) are indexed like normal files. This is acceptable and expected behavior for Windows directory crawling.

### 3.3 Case-Insensitivity
NTFS paths and filenames are case-insensitive. On-disk directories appear as `$RECYCLE.BIN`, `$Recycle.Bin`, or `$recycle.bin`, and artifacts as `$R...`, `$r...`, `$I...`, `$i...`.
- All matching primitives in `SystemPathFilter` and `RecycleBinIndexParser` must use **ASCII case-insensitive matching**. Canonical helpers `EqualsIgnoreCaseAscii(text, other)` and `StartsWithIgnoreCaseAscii(text, prefix)` are declared in `src/utils/StringUtils.h` (Phase 1 groundwork item), implemented over the existing `string_search::ToLowerChar` (`src/utils/StringSearch.h:93`, already included by `StringUtils.h`). The one-off `StartsWithAsciiNoCase` (`src/index/FileIndexStorage.h:25`) must be consolidated onto these helpers — do not create a third ASCII-case variant.
- Unit tests run on macOS must explicitly test case variations (`$recycle.bin`, `$R...`, `$r...`, `$I...`, `$i...`) to prevent Windows runtime regressions.

### 3.4 Multi-User SIDs & ACL Error Handling
- **Scope Decision:** FindHelper indexes **all users' SID folders** present on the volume.
- **Privilege Separation:** MFT enumeration operates with backup privileges, reading records for all user SIDs. However, direct Win32 file opens (for Phase 4 `$I` metadata reads) or attribute queries on another user's SID folder may fail with `ERROR_ACCESS_DENIED`.
- **Resilience Rule:** On `ERROR_ACCESS_DENIED` or `ERROR_FILE_NOT_FOUND`, the indexing pipeline and enricher must **skip-and-continue with `LOG_DEBUG_BUILD`**, never aborting the run. Unreadable `$I` files gracefully fall back to displaying the raw `$R` path.

### 3.5 Pairing Rules & Artifact Edge Cases
1. **Pairing Key:** Sibling files sharing the exact same 6+ character random token and extension under the same `<SID>` parent:
   `$I<Token>.<ext>` ↔ `$R<Token>.<ext>`.
2. **Orphan `$R` without `$I`:** Display raw `$R` path (no metadata available).
3. **Orphan `$I` without `$R`:** Ignored / discarded (never indexed into `FileIndex`).
4. **Corrupt / Truncated `$I`:** If `$I` header version is invalid or truncated, log warning and fall back to raw `$R` display.
5. **Race on Empty Bin:** If the user empties the Recycle Bin before the background enricher reads `$I`, the file open returns `ERROR_FILE_NOT_FOUND`. The worker discards the task cleanly.
6. **Recycled Folders:** Only the top-level folder has an `$I` sidecar. Children inside `$Rxxxx` derive their original path by appending their relative path to the folder's original path.
7. **Queue Payload Ownership:** The worker task queue must store **owned `std::string` copies**, never `std::string_view` into temporary buffers or the search path pool (strictly obeying the `string-view-pool-remap` rule). Queue capacity is bounded (e.g. max 16,384 entries) with drop/coalesce policy under extreme delete storms.
8. **Queue Producers & Drop Consequence:** Two producers enqueue `$I` paths: (a) **cold index** — every `SkipRecycleMetadata` classification in `StageRecord`/`RunRawMft` enqueues the owned `$I` path (this also covers files deleted while FindHelper was not running); (b) **live USN** — `$I` `FILE_CREATE` enqueues. `$I` `FILE_DELETE` cancels/discards any still-pending task for that path. A dropped (queue-full) task leaves that item's original unresolved until the next cold index; drops are counted and logged (`LOG_DEBUG_BUILD`), no mid-session retry pass is specified for v1.

### 3.6 Policy State: Ownership, Seeding & Runtime SID Discovery
- **One policy instance per volume.** `SystemPathFilterPolicy` embeds the `FilteredDirTracker` (hard system directories) and a `RecycleSubtreeTracker` (tracked `$Recycle.Bin` root FRNs plus discovered `<SID>` folder FRNs). `ClassifyEntry` is stateful — it updates trackers when it recognizes new containers — so each pipeline stage must operate on the volume's single instance rather than parallel copies.
- **Seeding handoff.** The existing `IndexBuildRun` → `UsnMonitor` handoff (today `RunInitialPopulationAndPrivileges` passes `filtered_dir_ref_nums`) must carry the **fully populated policy** (both trackers) after cold index. Live monitoring must never start with an empty recycle tracker.
- **Root recognition.** A directory named `$Recycle.Bin` (case-insensitive ASCII) whose parent is the volume root (MFT record 5) is recorded as that volume's recycle root. Record 5 itself must never enter either tracker (existing `IsExcludedIndexEntry` guard is preserved).
- **SID rule.** Every direct **directory** child of a tracked recycle root is accepted as a `<SID>` container without `S-1-*` pattern validation — Windows creates only SID directories and file artifacts at that level (`desktop.ini` is a file and is classified per §3.2). This keeps recognition cheap and order-independent.
- **Runtime SID creation.** SID folders are created lazily on a user's first delete. A USN `FILE_CREATE`/`RENAME_NEW_NAME` with `is_directory` and a tracked-recycle-root parent is added to the `RecycleSubtreeTracker` **before** subsequent records in the same batch are classified. `ProcessOneBuffer` must apply records in journal order so container discovery precedes payload classification (NTFS logs: SID dir create → rename-to-`$R` → `$I` create).
- **Tracker miss = safe degrade.** If a payload event arrives whose parent is not in the tracker (e.g., records skipped during journal replay), classification falls back to the standard `$`-prefix exclusion: the artifact is *absent* until the next cold index rather than wrongly indexed on name alone (guaranteed by INV-5). Log at `LOG_DEBUG_BUILD` with the parent FRN.

---

## 4. Architectural Invariants

| ID | Invariant | Enforcement Point | Description |
|---|---|---|---|
| **INV-1** | **Container Indexing** | `SystemPathFilterPolicy` | `$Recycle.Bin` and `<SID>` folders must always be inserted into `FileIndex` so `PathRecomputer` can resolve full paths for `$R` items. |
| **INV-2** | **No $I Ingestion** | `SystemPathFilterPolicy` | `$I` files must never enter `FileIndex` to prevent duplicate search results. |
| **INV-3** | **Off-Thread $I I/O** | `RecycleBinEnricher` | Disk reads of `$I` files must never occur inside `IndexBuildRun`, `UsnMonitor::ProcessOneBuffer`, or `SearchWorker`. |
| **INV-4** | **Layout Stability** | `SearchTypes.h` | `SearchResult` cache-line packing (`<= 64` hot bytes, `<= 208` total bytes) must not be violated. No fields added to `SearchResult`. |
| **INV-5** | **Parent-Chain Scoped Recognition** | `SystemPathFilterPolicy` | `$R`/`$I` classification applies exclusively to descendants of a verified `$Recycle.Bin\<SID>` subtree. Root or desktop `$Report.docx` stays excluded. |
| **INV-6** | **UI State Write Confinement** | `RecycleBinEnricher` & `GuiState` | Workers write to isolated staging buffers only. UI thread merges metadata via immutable `std::shared_ptr<const RecycleMetadataSnapshot>` with generation checks. |

---

## 5. Phased Implementation Plan

### Phase 1: Filter Policy Consolidation & Unification
**Goal:** Consolidate the scattered filtering logic across cold index and USN monitoring into `SystemPathFilterPolicy` without changing external behavior.

1. **Policy Class (`src/index/SystemPathFilter.h` / `.cpp`)**:
   Introduce `SystemPathFilterPolicy` owning:
   - `FilteredDirTracker` for hard system directories (`$Extend`, `$LogFile`, `$MFT`, etc.).
   - `RecycleSubtreeTracker` tracking `$Recycle.Bin` and child `<SID>` folder record numbers.
   - `ClassifyEntry(filename, frn, parent_frn, is_directory)` returning:
     - `IngestionDisposition::IncludeNormal`
     - `IngestionDisposition::IncludeRecycleBin` (container or `$R` payload)
     - `IngestionDisposition::SkipRecycleMetadata` (`$I` sidecar)
     - `IngestionDisposition::ExcludeSystem` (system file / system directory child)
2. **Consolidate Cold Indexing**:
   Replace duplicate filtering code in `IndexBuildRun::StageRecord` and `IndexBuildRun::RunRawMft` in `InitialIndexPopulator.cpp` with a single call to `policy.ClassifyEntry(...)`.
3. **Consolidate USN Handlers**:
   Replace duplicate checks in `HandleSystemFileFilter`, `HandleRenameNewName`, and `HandleRenameForUnknownEntry` in `UsnMonitor.cpp`.
4. **Case-Matching Helper Groundwork**:
   Declare `EqualsIgnoreCaseAscii` / `StartsWithIgnoreCaseAscii` in `src/utils/StringUtils.h` over `string_search::ToLowerChar` (see §3.3); migrate `StartsWithAsciiNoCase` (`src/index/FileIndexStorage.h:25`) onto them.
5. **Policy Ownership & Seeding** (per §3.6):
   One policy instance per volume; extend the `IndexBuildRun` → `UsnMonitor` handoff to pass the fully populated policy (both `FilteredDirTracker` and `RecycleSubtreeTracker`). Phase 1 is **behavior-preserving**: call sites treat `IncludeRecycleBin` exactly as the current exclusion (bin stays out of `FileIndex`); ingestion is activated only in Phase 2. This proves the consolidation is a pure refactor before any semantics change.
6. **Unit Tests**:
   Extend `tests/SystemPathFilterTests.cpp` covering:
   - Root `$Recycle.Bin` → recognized container.
   - Child `<SID>` → recognized container.
   - Child `$R...` under `<SID>` → `IncludeRecycleBin`.
   - Child `$I...` under `<SID>` → `SkipRecycleMetadata`.
   - Child `$Report.docx` on Desktop (not under `<SID>`) → `ExcludeSystem`.
   - Case-insensitive permutations (`$RECYCLE.BIN`, `$r...`, `$i...`).

### Phase 2: Ingestion & Quick Filter Integration (Raw $R + Containers)
**Goal:** Ingest `$Recycle.Bin`, `<SID>`, and `$R...` files into `FileIndex`. Fix the UI Quick Filter button.

1. **Ingestion Activation**:
   - Containers (`$Recycle.Bin` and `<SID>` dirs) and `$R...` items are inserted into `FileIndex`.
   - `$I...` items are skipped from `FileIndex`.
2. **USN Soft-Delete Handling**:
   - In `UsnMonitor::HandleRenameNewName`: If the destination parent is a tracked `<SID>` folder, route to normal `HandleFileRename` (updating parent and name to `$R...`) instead of `ApplySubtreeRemoval`.
   - In `UsnMonitor::HandleRenameForUnknownEntry`: Allow recovery insert for `$R` items under `<SID>`.
   - In `UsnMonitor::HandleSystemFileFilter`: via the consolidated `ClassifyEntry` call, `IncludeRecycleBin` dispositions must no longer be filtered as `$`-system names — `$R` creates/deletes/changes flow to the normal insert/delete path, and `$I` `FILE_CREATE`/`FILE_DELETE` route to the metadata queue (§3.5.8) instead of generic skip. The existing `RENAME_NEW_NAME` carve-out (`UsnMonitor.cpp:251-254`) and delete-eviction safety net (`:268-274`) are subsumed by the policy branch.
3. **Quick Filter Fix (`src/path/PathUtils.cpp` & `src/ui/FilterPanel.cpp`)**:
   - In `FilterPanel::RenderQuickFilters`: Clicking "Recycle Bin" populates the Path Filter input box with the raw literal `$Recycle.Bin` on Windows (on macOS: the **expanded absolute** home path `<home>/.Trash`; on Linux: `<home>/.local/share/Trash` — never a literal `~` string, which would match zero indexed absolute paths. Note this **changes** current Linux behavior in `GetTrashPath()`, which today resolves `.Trash`. Non-Windows trash was already crawled as a normal user folder, so the button change is display/filter-only — no non-Windows ingestion behavior changes).
   - In Path Filter matching: raw path input goes through `search_pattern_utils::CreatePathMatcher` (`src/search/SearchPatternUtils.h:604`) as a case-insensitive ASCII literal substring against each item's full path, so `$Recycle.Bin` isolates recycled files across all drive letters (`C:\$Recycle.Bin\...`, `D:\$Recycle.Bin\...`) and both on-disk casings (`$RECYCLE.BIN`). The 12-character input passes the raw-path debounce guards (`kMinPathLengthToSearch = 4`, `SearchController.h:122`; no trailing separator).
4. **App Actions Audit**:
   - Verify `file_operations::DeleteFileToRecycleBin` (`FileOperations_win.cpp`): In-app deletion of a live file renames it to `$R` in the index.
   - Audit context menu operations in `ResultsTable.cpp`: Ensure "Delete to Recycle Bin" is disabled or mapped to permanent delete if the selected item is already inside `$Recycle.Bin`.

### Phase 3: Cross-Version $I Parser Module
**Goal:** Implement a standalone, fully unit-tested parser for v1 and v2 `$I` metadata files.

1. **Parser Implementation (`src/index/recycle/RecycleBinIndexParser.{h,cpp}`)**:
   - Pure, dependency-free C++17 parser derived from `docs/platform/windows/NTFS_RECYCLE_BIN_PARSER.md`.
   - Parses v1 (`RecycleBinHeaderV1`, 544 bytes) and v2 (`RecycleBinHeaderV2`, variable length).
   - Validates structural bounds, converts UTF-16LE paths to UTF-8, and converts Win32 FILETIME.
2. **Test Target Registration**:
   - Add `tests/RecycleBinIndexParserTests.cpp` testing synthetic v1/v2 buffers, truncated buffers, oversized paths, and corrupt versions.
   - Add test target to `CMakeLists.txt` and append target name to `scripts/test_targets.txt`. Run via `./scripts/build_tests_macos.sh`.

### Phase 4: Asynchronous Metadata Enricher & True Original-Name Searchability
**Goal:** Resolve, search, and display original filenames and deletion timestamps without degrading hot-path search performance or violating `SearchResult` layout constraints.

1. **Metadata Store & Background Worker**:
   - `RecycleBinMetadataStore`: Thread-safe store publishing immutable `std::shared_ptr<const RecycleMetadataSnapshot>`.
   - `RecycleMetadataSnapshot`: Holds:
     - Direct map: `uint64_t fileId -> { std::string original_name, std::string original_path, FILETIME deletion_time, uint64_t original_size }`.
     - Compact searchable array: `std::vector<RecycleSearchEntry>` where each entry holds `{ uint64_t fileId, std::string original_name, std::string original_path, size_t filename_offset, size_t ext_offset, bool is_directory }`.
   - `RecycleBinEnricher`: Low-priority background worker thread consuming `$I` paths from a bounded work queue. Reads `$I` off-lock, parses with `RecycleBinIndexParser`, and updates the store.
2. **Search Architecture: Approach B (Secondary Snapshot Search)**:
   - **Zero Hot-Loop Overhead**: The main `ParallelSearchEngine::ProcessChunkRange` scanning 1,000,000 items in `PathStorage` SoA arrays remains 100% untouched and unburdened by hash-map checks or string redirection.
   - **Secondary Scan Execution**: When `SearchWorker::ExecuteSearch` runs, in addition to dispatching chunk tasks across the thread pool for `PathStorage`:
     - A lightweight task executes `ScanRecycleSnapshot(snapshot, context, local_results)`.
     - It runs `filename_matcher` and `path_matcher` against the snapshot's small collection (typically < 1,000 items, executing in < 0.05ms).
     - When matching, it appends the hit **by `fileId` only** (a `RecycleSnapshotHit{uint64_t fileId}` carried with the `SearchResultBatch`). The snapshot's original-name strings are **never** appended to the results path pool.
   - **Result Reconciliation (Dedup & Pool Ownership)** — applied once in `MergeAndConvertToSearchResults` before pool append:
      1. Drop any snapshot hit whose `fileId` is already present from the main `PathStorage` scan (a user searching `recycle` or `$R` would otherwise get two rows per file).
      2. For surviving snapshot-only hits, resolve the `fileId` through `FileIndex` and append the entry's **physical `$R` full path** to `searchResultPathPool` — producing exactly the row shape a main-scan hit produces.
      3. `SearchResult.fullPath` therefore **always references the physical path**; the original name surfaces only via render-time snapshot lookup (item 3). §8 operation call sites stay correct unchanged, and the `Clear results-before-pool` + `resultsBatchNumber` bump protocol (`GuiState.h` `ResultPoolOwner`) applies with no new invariants — dedup and selection remap key on `fileId`, which is stable across both scans.
      4. Rejected alternative (recorded to prevent re-litigation): appending snapshot original strings into the pool — it would make `fullPath` ambiguous and break every §8 physical-path operation.
3. **Display without Struct Bloat (INV-4)**:
   - No fields added to `SearchResult` (`static_assert(sizeof(SearchResult) <= 208)` strictly preserved).
   - In `ResultsTable.cpp`: For rows tagged as recycled — **both** a `fileId` hit in the current snapshot **and** the row's physical path still under a tracked `$Recycle.Bin\<SID>` subtree (belt-and-braces against event-ordering races and missed evictions, see item 5) — renders the original filename with a distinct `[Recycled]` badge and tooltip showing the original deletion timestamp and original location.
      - **Sort-key decision (v1 accepted limitation):** Filename/Path column sorting continues to operate on the indexed **physical `$R` strings**; displayed original names may therefore differ from the sort key. Display-name sorting (tie-break physical path) is deferred as a follow-up so Phase 4 ships without touching the hot compare loop.
4. **Single-Writer Concurrency (INV-6)**:
   - The enricher never mutates `GuiState` directly.
   - Publishes an immutable `std::shared_ptr<const RecycleMetadataSnapshot>`.
   - The UI thread captures the snapshot reference at the start of a search frame with a generation check.
    - **Enrichment visibility:** publishing a new snapshot generation does **not** itself trigger a re-search. Rows re-render (name/badge/tooltip) on the next paint because display is a render-time lookup; new original-name *matches* appear at the next search run — which the existing auto-refresh on index-change events provides promptly ($I/$R USN events bump `resultsBatchNumber` like any other change). Accepted v1 behavior; no extra re-trigger mechanism.
5. **Metadata Lifecycle & Eviction (prevents stale `[Recycled]` rows)**:
   - A restore renames the **same MFT record** (same `fileId`) out of the bin, so store entries must be actively evicted or restored live files would keep rendering with the stale original name + badge:
     - **Restore:** `RENAME_NEW_NAME` moving a tracked `fileId` **out** of a tracked `<SID>` subtree → evict the `fileId` from `RecycleBinMetadataStore` immediately (do not wait for the companion `$I` `FILE_DELETE` — ordering between the two events is not guaranteed relative to UI reads).
     - **`$I` `FILE_DELETE`:** evict the paired `fileId` (covers Empty Bin and post-restore cleanup).
     - **`$R` `FILE_DELETE`:** evict the `fileId` (belt-and-braces).
   - **Re-delete after restore** mints new `$R`/`$I` tokens on the **same** `fileId`: the restore eviction above plus the fresh `$I` `FILE_CREATE` enqueue produce a new entry naturally — no tombstones or generation tags in the store.
   - Missed evictions are visually contained by the render guard in item 3.


---

## 6. Acceptance Criteria & Traceability

| ID | Acceptance Criterion | Phase | Verification |
|---|---|---|---|
| **AC-1** | Cold index ingests `$Recycle.Bin`, `<SID>` folders, and `$R` subtrees; `$I` never appears in results; cold-index duration regression ≤ 5% | 1, 2 | T1 filter tests + T2 cold benchmark |
| **AC-2** | Explorer soft-delete keeps the item: the entry is renamed to `$R…` under `$Recycle.Bin\<SID>\` and remains indexed (no eviction) | 2 | T2 live USN |
| **AC-3** | Restore returns the item to its live path **and** it no longer renders as recycled (no stale badge/original-name lookup) | 2, 4 | T2 live USN |
| **AC-4** | Emptying the bin removes every `$R` entry (including nested children) from the index and purges their metadata-store entries | 2, 4 | T2 live USN |
| **AC-5** | Quick Filter button isolates recycled items across all drives and both on-disk casings (`$RECYCLE.BIN` disk vs `$Recycle.Bin` filter text) | 2 | T1 matcher semantics + T2 UI QA |
| **AC-6** | A user file named `$Report.docx` outside a tracked recycle subtree never enters the index and never receives recycle treatment (INV-5 regression case) | 1, 2 | T1 policy tests |
| **AC-7** | `$I` parser round-trips synthetic v1/v2 fixtures and rejects truncated/corrupt/oversized inputs without crashing or UB | 3 | T1 unit tests |
| **AC-8** | After enrichment, original filenames are searchable and displayed; Open/Delete/Copy/Export/drag continue to operate on the physical `$R` path exactly once per file (no duplicate rows) | 4 | T2 QA + §8 call-site audit |
| **AC-9** | Other users' SID folders are indexed and searchable; ACL-denied attribute/`$I` reads degrade silently to raw `$R` display (skip-and-continue, no error spam, no run abort) | 2, 4 | T2 non-elevated QA |

## 7. Verification Plan, Performance Gates & Platform Split

### 7.1 Developer Environment Reality: macOS vs. Windows Split
**Core Constraint:** Development and local coding take place on **macOS**. Because NTFS volumes, Win32 volume handles, MFT raw DMA streaming (`RunRawMft`), and the USN journal (`FSCTL_ENUM_USN_DATA`, `UsnMonitor`) are guarded by `if(WIN32)` / `#if defined(_WIN32)`, **they cannot be executed or benchmarked directly on macOS**.

To bridge this gap without risking regressions, the verification strategy is divided into two distinct tiers:

### 7.2 Tier 1: Local macOS Verification & In-Memory Microbenchmarks
What **can and must** be run on macOS prior to committing:
1. **Search Engine Regression Gate:**
   `scripts/run_search_benchmark.sh` compiles and runs natively on macOS using cross-platform path corpora (`tests/data/std-linux-filesystem.txt`). It gates that search execution, path filtering, and results conversion do not regress:
   ```bash
   # 1. Record baseline before Phase 1:
   ./scripts/run_search_benchmark.sh --config tests/benchmark_config_sample.json --record-baseline baseline_pre_recycle.json

   # 2. Verify equivalence and gate regressions after changes (<= 5.0% tolerance):
   ./scripts/run_search_benchmark.sh --config tests/benchmark_config_sample.json --compare baseline_pre_recycle.json --verify --max-regression-pct 5.0
   ```
2. **Pure Module Unit Tests (`./scripts/build_tests_macos.sh --no-asan`):**
   - `SystemPathFilterTests`: Rigorously exercises `SystemPathFilterPolicy` with 100,000+ synthetic filenames, SID paths, case variations (`$RECYCLE.BIN`, `$r...`, `$i...`), and non-recycle dollar files (`$Report.docx`).
   - `RecycleBinIndexParserTests`: Parses synthetic in-memory v1 and v2 binary buffers (including corrupted, truncated, and oversized buffers).
3. **In-Memory Filter Throughput Microbenchmark:**
   Ensure `ClassifyEntry` executes in **≤ 50 ns per item** on the macOS dev machine (in-memory hash/set lookups only; the previous single-digit-ns target is not realistic for unordered-set lookups and is relaxed to a measured, review-if-exceeded bound). The point is to confirm the policy adds a mathematically negligible increment to Windows cold-index duration ahead of the Tier-2 benchmark.
4. **Architectural Guardrails (Zero-I/O Invariant):**
   Code inspection guarantees that neither `StageRecord`, `RunRawMft`, nor `UsnMonitor` introduces any synchronous Win32 disk read or file open calls.

### 7.3 Tier 2: Windows CI & Staging Machine Verification
What **must be deferred to Windows CI or a Windows physical/VM machine**:
1. **Full Cold Index Benchmark (≤2s / 1M records):**
   Run cold MFT enumeration on a real NTFS volume with Recycle Bin populated; verify that total index duration does not regress beyond 5% vs. pre-refactor Windows build.
2. **Live USN Journal Lifecycle Verification:**
   - **Soft Delete:** Move `C:\test\doc.txt` to Recycle Bin in File Explorer -> verify item immediately appears as `$R...` in FindHelper without an index rebuild.
   - **Restore:** Restore item in File Explorer -> verify item immediately returns to live path `C:\test\doc.txt` **and renders with no `[Recycled]` badge and no stale original-name lookup** (store eviction, Phase 4 item 5).
   - **Re-delete after restore:** delete the same file again -> verify it re-enters the bin as a fresh `$R`/`$I` token pair on the **same** MFT `fileId` and enrichment resolves the new sidecar (no tombstones, Phase 4 item 5).
   - **Empty Bin:** Empty Recycle Bin -> verify `$R` entry disappears from FindHelper results.
   - **$I Isolation:** Verify searching for `$I*` returns 0 results.
   - **Multi-Drive:** Test on secondary NTFS drives (`D:\$Recycle.Bin`).
   - **Non-Elevated ACL Resilience:** Run as non-admin on a machine with multiple user accounts; verify other users' SIDs do not crash or spam errors.

---

## 8. Audit of UI & File Operation Call Sites

The following call sites in `src/` must be audited during Phase 2 & 4 to ensure safe handling of `$R` physical paths vs original display paths:

1. `src/ui/ResultsTable.cpp`: Context menu actions ("Open", "Open Folder", "Copy Path", "Delete"). File operations must continue using `SearchResult::fullPath` (the physical `$R` path).
2. `src/ui/ResultsTableKeyboard.cpp`: Keyboard shortcuts (Delete, Enter) must operate on physical paths.
3. `src/platform/windows/FileOperations_win.cpp:135-171` (`DeleteFileToRecycleBin`): If user initiates delete on an already recycled file, map to permanent delete or disable.
4. `src/services/ExportSearchResultsService.cpp`: Export to CSV/JSON should export physical path by default, adding optional columns for Original Path and Deletion Time in Phase 4.
5. `src/search/IncrementalSearchState.cpp`: Batch number and selection remap logic must remain stable.
