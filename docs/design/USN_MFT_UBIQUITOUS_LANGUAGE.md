# USN/MFT Ubiquitous Language

Single source of truth for domain terms used in USN journal and MFT
monitoring code. Use these terms in conversations, docs, and new code.
Do not introduce synonyms.

Companion docs: `USN_INGEST_PIPELINE_DESIGN.md` (how it works),
`internal-docs/analysis/2026-09-18_USN_MFT_DDD_DOMAIN_MAP.md` (DDD grading
and hardening plan).

## Entities

| Term | Meaning | Code pointer | Not this |
|---|---|---|---|
| IndexedFile | A file **or directory** on the volume, tracked across rename/move. Identity is the NTFS file reference; parent, name, and attributes are mutable. Note: `File` always means "NTFS file record", i.e. a regular file or a directory — check `isDirectory` / `FileAttributes` to discriminate. `FileIndex` keeps its name for continuity. | `struct FileEntry` in `src/index/FileIndexStorage.h` (name lives in `NameArena` / `PathStorage`) | `FileEntry`, `IndexedEntry`, `record`, bare `id` |
| MonitoredVolume | The NTFS volume under observation: path, open handle, journal identity, read position. Note: `VolumeHandle` is the infrastructure RAII wrapper, not this domain entity. | Scattered today: `MonitoringConfig::volume_path`, `UsnMonitor::volume_handle_` (`src/usn/UsnMonitor.h`), `USN_JOURNAL_DATA_V0` in `src/usn/UsnMonitor.cpp` | Loose `next_usn` locals, `hVol` |
| FilteredDirectory | A `$`-prefixed system directory and, transitively, its subtree — never indexed. Identified by its 48-bit MFT record number; `File` always means file-or-directory (see `IndexedFile`). | `FilteredDirectory` + `FilteredDirTracker` + `IsSystemPrefixedName` in `src/index/SystemPathFilter.h` | Raw `uint64_t` sets, `filtered_dir_ref_nums_` as a bare container |
| UnresolvedReference | A child seen before its parent (child-before-parent from `ReturnOnlyOnClose` coalescing). Carries `usn_at_track` (journal position of the tracking record, 0 = unknown) alongside wall-clock `tracked_at`. Heals on parent arrival or is evicted as never-arriving (the evicted event carries both). | `UnresolvedReference`, `awaiting_parent_`, `AwaitingStats` / `NeverArriving` in `src/crawler/IndexOperations.h`; `usn_at_track` set from `InsertOptions` (live USN via `VolumeState::ApplyFileCreated`) | `pending_children_`, `placeholder`, `pending` |
| IndexBuildRun | One baseline run: reserve, stage batches, commit, hand over to the live tail (recompute/prune stay with the caller). Owns filter-tracker ref, MFT reader + counters, totals, and progress; reports a `Result` (`files`, `iterations`, `filtered`, MFT hits/misses/total, integrity flag). | `index_build_run::IndexBuildRun` + `Result` in `src/index/IndexBuildRun.h`, driven by `PopulateInitialIndex` (`src/index/InitialIndexPopulator.cpp`); `PopulationBatchEntry` (`src/index/FileIndex.h`), `is_populating_index_` (`src/usn/UsnMonitor.h`) | Out-param progress, `is_populating` flags, `PopulationContext` |

## Value objects

| Term | Meaning | Code pointer | Not this |
|---|---|---|---|
| NtfsFileReference | 64-bit NTFS file reference = 16-bit sequence number + 48-bit record number. `RecordNumber()` strips the sequence; `SameRecordNumber()` is rename-vs-move equality. | `namespace ntfs_file_reference` in `src/index/NtfsFileReference.h` | Bare `uint64_t` FRN, mixing full FRN with stripped record number |
| MftRecordNumber | 48-bit MFT record number with the sequence already stripped. Map keys that match by record (`record_number_to_id_`, `awaiting_parent_`) use this so a full FRN cannot flow into a stripped-key lookup without an explicit strip (`FromFileReference`). | `ntfs_file_reference::MftRecordNumber` in `src/index/NtfsFileReference.h` (+ `std::hash` / `boost::hash` for map keys) | Bare `uint64_t` record keys, full FRN passed where a record is expected |
| JournalCursor | Where the reader stands: `(journal_id, next_usn, lowest_valid_usn)`. `Advance()` moves the read position; `HasIdChanged()` / `IsWrappedBy()` collapse the wrap/ID checks. Steady-state `StartUsn` chaining stays on `READ_USN_JOURNAL_DATA_V0`. **Checkpoint persistence is rejected**: restart re-enumerates from FRN 0; the trade-off is accepted. | `usn_journal::JournalCursor` in `src/usn/JournalCursor.h`, wired through `RunInitialPopulationAndPrivileges` / `DrainReplayJournalEvents` | Loose `USN` locals, `StartUsn`/`NextUsn` as separate variables |
| UsnRecord | One parsed `USN_RECORD_V2`: strong self/parent references, name view (valid while source buffer lives), USN, typed reasons, kind. Specs `IsParentEstablishing()` / `IsActionable()`. Journal timestamps are always 0 (no time field). | `usn_record::UsnRecord` in `src/usn/UsnRecord.h`, built in `ProcessInterestingUsnRecord`; validation in `src/usn/UsnRecordUtils.cpp` | `record`, `buffer`, `.cpp`-local context structs |
| UsnReason | Why the record exists: `FileCreate`, `FileDelete`, `RenameOld/NewName`, `DataExtend/Truncation/Overwrite`, `Close`. `Close`-only records are skips (`IsActionable`). Bits mirror `winioctl.h`, verified by `static_assert` in `UsnMonitor.cpp`. | `usn_reason::{UsnReason, ReasonSet, kInterestingReasons, kActionReasons, kDataChangeReasons}` in `src/usn/UsnReason.h` | Raw `DWORD` masks, `reasons`, `flags` |
| FileName / FilePath | `FileName` is the normalized bare name (UTF-16 to UTF-8, embedded-null truncated). `FilePath` is the derived full path (parent-chain walk). `file_name::FileName` (`src/index/FileName.h`, owned, explicit from `string_view`) marks batch/insert-boundary bare names (`PopulationBatchEntry::name`); `Insert`/`UsnRecord` still take raw views — type those boundaries next. In new code use `FileName`/`FilePath` in identifiers even when the type is still `std::string_view`. | `file_name::FileName`, `TruncateAtEmbeddedNull`, `NameArena`, `PathStorage`, `PathRecomputer` | `name` vs `path` used interchangeably, raw journal names |
| AttributesSnapshot | Size/time with explicit unknown states; MFT size 0 is never cached. | `LazyFileSize` / `LazyFileTime` (`src/index/LazyValue.h`), sentinels in `src/utils/FileTimeTypes.h`, `MftMetadataReader` | `size`, `mtime`, bare `0` meaning unknown |
| MftEnumerationPosition | One `FSCTL_ENUM_USN_DATA` cursor: `StartFileReferenceNumber`, `LowUsn`, `HighUsn`. | `mft_enum_position::MftEnumerationPosition` in `src/index/MftEnumerationPosition.h` (`Start()` factory + `Advance()`), threaded through `IndexBuildRun::Run` / `ProcessBuffer` | `StartFileReferenceNumber` used as both cursor and identity |
| IntegrityLatch | Set-once flag: the in-memory view is no longer trustworthy (wrap, ID change, queue drop, corrupt tail). Survives metric resets. Causes are published as `JournalWrapped` / `JournalIdChanged` / `JournalLost` / `QueueBuffersDropped` / `CorruptBufferTail` / `ConsecutiveErrorsExceeded` / `RenameDivergence` events. | `index_integrity_compromised_` in `src/usn/UsnMonitor.h` | `bool` flags, resettable error bits, bare counters |

## Aggregates and events

| Term | Meaning | Code pointer |
|---|---|---|
| VolumeState | Consistency boundary: entries, paths, awaiting set, filter set, cursor, and latch advance atomically per buffer (single `unique_lock` per buffer). Aggregate root with domain verbs `ApplyFileCreated/Deleted/Renamed/Moved/DataChange/BaselineBatch` delegating to `FileIndex::*Locked` (no lock-model change; `ProcessOneBuffer` reads as domain logic). | `volume_state::VolumeState` in `src/index/VolumeState.h` wrapping `FileIndex`; per-buffer lock rule in `USN_INGEST_PIPELINE_DESIGN.md`, `ProcessOneBuffer` in `src/usn/UsnMonitor.cpp` |
| FileCreated / FileDeleted / FileRenamed / FileModified | Facts published per applied record; the metrics object subscribes its `files_*` counters. Published instead of bare increments so forensics and metrics share one mechanism. | `index_domain_events::FileLifecycleEvent` (variant) + `FileEventSink` in `src/index/IndexDomainEvents.h`; publishers in `UsnMonitor.cpp` dispatch; subscriber `CountFileLifecycleEvent` |
| JournalWrapped / JournalIdChanged / JournalLost / QueueBuffersDropped / CorruptBufferTail / ConsecutiveErrorsExceeded / RenameDivergence | Journal is lossy; each latch publishes its cause (`expected_usn/lowest_valid_usn`, journal ids, error code, drop count + depth, corrupt offset, error count, FRN). Threading differs per cause: under-lock (processor thread) for `RenameDivergence`; lock-free for the rest (reader thread: loss/drop/errors; population thread: wrap/ID; processor thread post-lock: corrupt tail). Null sink disables. | `index_domain_events::IntegrityEvent` (variant) + `IntegrityEventSink` in `src/index/IndexDomainEvents.h` (per-family threading contract in the header comment); publishers alongside every latch in `src/usn/UsnMonitor.cpp` (`PublishIntegrityEvent` member, guarded `PublishIntegrityEventIfSet` free helper for under-lock rename paths); subscriber `SetIntegrityEventSink` |
| ParentHealed / NeverArrivingEvicted | Awaiting lifecycle facts: healed on parent arrival (child + parent ids), evicted by the never-arriving sweep with an age. Published to sinks; `FileIndex` subscribes its healed total. Metrics subscription for evictions is follow-up work. | `index_domain_events::{ParentHealed, NeverArrivingEvicted}` in `src/index/IndexDomainEvents.h`; publishers `HealAwaiting`, `EvictNeverArrivingLocked` |

## Infrastructure

| Term | Meaning | Code pointer | Not this |
|---|---|---|---|
| VolumeGateway | The kernel ioctl boundary: `QueryJournal` / `ReadJournal` (owned by `UsnMonitor`), `EnumMft` / `GetVolumeData` (owned by `IndexBuildRun`), `GetFileRecord` (owned by `MftMetadataReader`) behind an injectable interface; the gateway reference flows `UsnMonitor` → `IndexBuildRun` → `MftMetadataReader`. Thin translation only (no logging, retry, or error mapping); on false `GetLastError()` holds the kernel error. Live `DeviceIoControl` impl by default; fakes substitute in tests. | `volume_gateway::{VolumeGateway, DeviceIoControlVolumeGateway}` in `src/usn/VolumeGateway.h`, owned by `UsnMonitor` (`SetVolumeGateway`) | Raw `DeviceIoControl` at call sites |

## Rules for new code

- Use the preferred term (left column) in new identifiers, comments, and docs.
- Do not add a synonym for an existing term; extend this file instead.
- When a term has no type yet (MonitoredVolume), keep the term in names and
  comments so the later strong type has a stable vocabulary to land on.
- Domain events are published synchronously inside the per-buffer `VolumeState`
  lock. Subscribers must not perform I/O or mutate the index; they may only
  update their own derived state (metrics counters, activity trackers, sinks).
