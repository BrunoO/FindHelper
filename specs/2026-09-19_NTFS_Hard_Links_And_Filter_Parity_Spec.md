# Specification: NTFS Hard Links and Filter Parity with Everything

**Document:** `specs/2026-09-19_NTFS_Hard_Links_And_Filter_Parity_Spec.md`  
**Date:** 2026-09-19  
**Status:** Proposed / For Future Reference  
**Target:** USN_WINDOWS / FindHelper (C++17, Windows NTFS / Cross-Platform)

---

## 1. Overview & Motivation

When indexing an identical Windows 7 NTFS system partition (C:), Voidtools **Everything** and **FindHelper** report a significant discrepancy in indexed items:
- **Everything:** 576,711 items
- **FindHelper:** 551,868 items
- **Difference:** 24,843 items (+4.5% in Everything)

A full path-by-path diff analysis established that **551,867 items match bit-identically** between both engines (99.9998% of FindHelper items are in Everything). The remaining 24,844 items not indexed by FindHelper fall into distinct, quantifiable categories:

| Category | Missing Count | % of Difference | Root Cause in FindHelper |
|---|:---:|:---:|---|
| **NTFS Hard Links** | 23,585 | 94.93% | `FileIndexStorage` keys on 64-bit `NtfsFileReference`, collapsing multiple directory links into a single file entry. |
| **System & Recycle Bin Files** | 1,247 | 5.02% | `SystemPathFilter` unconditionally prunes `$`-prefixed entries (`$Recycle.Bin`, `$av_asw`, `$Extend`, `$MFT`). |
| **Volume Root Entry** | 1 | < 0.01% | Everything exports the root object (`\C:`) as an indexed row. |
| **Transient Runtime Files** | 11 | 0.04% | Files created/deleted during the interval between scans (indexer files, exports, definitions). |
| **Total** | **24,844** | **100.0%** | |

This specification defines the architectural design, data structures, and algorithms required to achieve full parity with Everything by:
1. Supporting **NTFS hard links** (multi-link indexing) so that all directory paths pointing to the same MFT file record are searchable.
2. Providing **configurable filtering** for NTFS system files and the Recycle Bin.

---

## 2. Technical Analysis & Existing Limitations

### 2.1 The Hard Link Problem in `FileIndexStorage`

In NTFS, each file is represented by a Master File Table (MFT) record. When a file has multiple hard links (common in `WinSxS`, `System32`, `SysWOW64`, Edge `resiliencylinks`, and Git `libexec`), each link is a distinct directory entry (`$FILE_NAME` attribute) sharing the same MFT File Reference Number (FRN).

In FindHelper:
1. `FSCTL_ENUM_USN_DATA` produces a `USN_RECORD_V2` for each `$FILE_NAME` attribute.
2. However, `FileIndexStorage` stores entries in a map keyed directly by `NtfsFileReference`:
   ```cpp
   flat_hash_map_t<ntfs_file_reference::NtfsFileReference, FileEntry> id_to_entry_;
   ```
3. In `FileIndexStorage::InsertLocked`:
   ```cpp
   name_cache_.InsertOrAssign(id.raw, name);
   auto [it, inserted] = id_to_entry_.try_emplace(id, new_entry);
   if (!inserted) {
     // Overwrites previous link's parent linkage and name cache!
     it->second.parentID = parent_id;
     it->second.isDirectory = is_directory;
   }
   ```
4. Only the **last visited hard link** is retained. All earlier links are overwritten and discarded from the index.

### 2.2 NTFS Invariants for Hard Links
- **Directories cannot have hard links:** NTFS strictly forbids hard links to directories to prevent cycles. Therefore, every directory has exactly one parent and one MFT record. The directory hierarchy is guaranteed to remain a strict tree / directed acyclic graph (DAG).
- **Only non-directory files have hard links:** Multiple hard links only introduce multiple leaf nodes pointing to the same file content.

---

## 3. Architecture & Data Flow

### 3.1 Primary Key Decoupling (`FileIndexStorage`)

To support multiple links without disrupting existing lookups:
1. Each indexed row (directory entry / link) receives a unique 64-bit `uint64_t` entry ID.
   - For primary links (or non-hardlinked files), the ID is the standard `NtfsFileReference` (`file_ref_num`).
   - For secondary hard links of an already-indexed MFT record, allocate a synthetic unique ID (e.g. high-order bit set or sequential from `next_file_id_`).
2. Maintain a 1-to-many reverse map for MFT record updates:
   ```cpp
   flat_hash_map_t<ntfs_file_reference::MftRecordNumber, std::vector<uint64_t>> record_number_to_all_ids_;
   ```
3. When metadata (size, modification time) is updated (via lazy loading or MFT read), update all entries registered under that `MftRecordNumber`.

```
               ┌───────────────────────────────┐
               │  MFT Record # 0x00012345      │
               └──────────────┬────────────────┘
                              │
             ┌────────────────┴────────────────┐
             ▼                                 ▼
   [Link 1: Primary ID]              [Link 2: Secondary ID]
   Name: "tracerpt.exe"              Name: "tracerpt.exe"
   Parent: System32                  Parent: WinSxS\amd64_...\
   Path: C:\Windows\System32\...     Path: C:\Windows\WinSxS\...
```

### 3.2 Path Resolution (`PathRecomputer`)

Because directory paths form a strict tree:
1. `children` adjacency in `PathRecomputer::RecomputeAllPaths` will include secondary link IDs as children of their respective parent directories.
2. BFS topological sort visits each directory level; leaf files are assigned depth `parent_depth + 1`.
3. Memoized path construction effortlessly resolves both paths:
   - `C:\Windows\System32\tracerpt.exe`
   - `C:\Windows\WinSxS\amd64_...\tracerpt.exe`
4. Both paths are inserted into `PathStorage` SoA arrays, making both discoverable during pattern search.

### 3.3 Live USN Monitoring (`UsnMonitor`)

1. **Reason Bit Definition:**
   Add `HardLinkChange = 0x00010000U` (`USN_REASON_HARD_LINK_CHANGE`) to `usn_reason::UsnReason` in `src/usn/UsnReason.h`.
2. **Link Creation (`USN_REASON_HARD_LINK_CHANGE | USN_REASON_FILE_CREATE`):**
   Insert a new hard link entry under `ParentFileReferenceNumber` with `FileName`.
3. **Link Deletion (`USN_REASON_HARD_LINK_CHANGE | USN_REASON_FILE_DELETE`):**
   Locate the specific link entry matching `(parentID, fileName)` under that MFT record and remove only that link.
4. **File Deletion (`USN_REASON_FILE_DELETE` without remaining links):**
   Remove all remaining links for the file.
5. **Data Modification (`USN_REASON_DATA_EXTEND | USN_REASON_DATA_OVERWRITE | ...`):**
   Invalidate cached file size for all linked entry IDs sharing that MFT record number.

### 3.4 Configurable System & Recycle Bin Filtering

Add configuration settings to `Settings.h` (serialized in `settings.json`):
```json
{
  "indexing": {
    "index_hard_links": true,
    "include_system_files": false,
    "include_recycle_bin": false
  }
}
```
- When `include_system_files == false`: Continue filtering `$`-prefixed files and NTFS system metadata (`$MFT`, `$LogFile`, `$Extend`).
- When `include_recycle_bin == false`: Filter `$Recycle.Bin` and its descendant SID subfolders.
- When set to `true`: Bypass pruning in `SystemPathFilter.cpp`, allowing full raw volume parity with Everything.

---

## 4. User Stories & Acceptance Criteria

| ID | Title | Given | When | Then |
|---|---|---|---|---|
| **US-1** | Hard link search discovery | A file exists with multiple hard links on disk | User searches for the file name | All hard link paths appear in search results with identical size and timestamp. |
| **US-2** | Link-specific deletion | A file has two hard links (A and B) | Link A is deleted via Windows Explorer or CLI | Link A is removed from the index; Link B remains searchable in FindHelper. |
| **US-3** | Shared data update | A file has two hard links | Link A is modified and its file size changes | Size cache is invalidated for both Link A and Link B; subsequent inspect reflects new size. |
| **US-4** | Everything parity mode | Both `index_hard_links` and `include_system_files` / `include_recycle_bin` are enabled | User benchmarks total index count against Everything on the same volume | Item count matches Everything within 0.01% (transient file margin). |
| **US-5** | Clean default mode | Fresh installation with default settings | User searches the drive | Search results do not show clutter from `$Recycle.Bin\S-1-5-...` or `$MFT` metadata files. |

---

## 5. Performance & Memory Impact

- **Entry Count Increase:** +23,585 entries on a typical Windows 7 installation (~4.2% increase from 551K to 575K entries).
- **RAM Overhead:**
  - `FileEntry` struct: 40 bytes × 23,585 ≈ 943 KB.
  - `PathStorage` paths: ~120 bytes average path × 23,585 ≈ 2.8 MB.
  - Overall heap delta is under 5 MB, well within acceptable bounds.
- **Search Latency:** Less than 4% difference in SoA linear scan time.

---

## 6. Verification Plan

1. **Automated Unit Tests (`doctest`):**
   - `HardLinkInsertionTests`: Test inserting duplicate FRNs with distinct parents/names; verify both resolve correctly in `PathRecomputer`.
   - `HardLinkRemovalTests`: Test deleting one link while retaining another; test final link deletion.
   - `HardLinkSizeInvalidationTests`: Verify size invalidation propagates across all IDs linked to an MFT record.
   - `SystemPathFilterConfigTests`: Test toggling system and recycle bin exclusions.
2. **Automated Test Run:**
   - Execute `./scripts/build_tests_macos.sh --no-asan` to ensure zero regressions in existing search and indexing suites.
3. **Dataset Verification:**
   - Validate against `Testing/search results/everything 26-09-19.csv` and `search_results_2026-09-19_205419.csv` using Python comparison tooling.
