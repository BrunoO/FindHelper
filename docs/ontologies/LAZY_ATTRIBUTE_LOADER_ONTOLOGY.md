# Lazy Attribute Loader Ontology

Single source of truth for the **lazy file-attribute loading** domain vocabulary.
Companion: `src/index/LazyAttributeLoader.h`, `src/utils/FileAttributeConstants.h`,
`src/utils/FileTimeTypes.h`, `docs/design/LAZY_ATTRIBUTE_LOADER_DESIGN.md`.

## Entities

| Term | Meaning | Code pointer | Not this |
|---|---|---|---|
| LazyAttributeLoader | Handles lazy loading of file size and modification time with double-check locking. | `LazyAttributeLoader` in `src/index/LazyAttributeLoader.h` | `FileIndex`, `AttributeCache` |
| FileAttributes | Pair of size + modification time loaded together (optimization). | `FileAttributes` (size, time, directory flag) | `FileEntry`, `Metadata` |

## Value objects

| Term | Meaning | Code pointer | Not this |
|---|---|---|---|
| FileSize | `uint64_t` size with tri-state sentinel semantics. | `kFileSizeNotLoaded`, `kFileSizeFailed` in `src/utils/FileAttributeConstants.h` | Bare `0` meaning unknown |
| FileTime (ModificationTime) | `FILETIME` with tri-state sentinel semantics. | `kFileTimeNotLoaded`, `kFileTimeFailed` in `src/utils/FileTimeTypes.h` | Bare zero time |
| AttributeState | One of `Loaded`, `NotLoaded`, `Failed`. | `IsSentinelTime`, `IsFailedTime`, `IsValidTime` | `bool loaded` |
| Sentinel (NotLoaded) | `UINT64_MAX` (size) / `{UINT32_MAX, UINT32_MAX}` (time): attribute not yet loaded. | constants above | `0` |
| Failed marker | `UINT64_MAX - 1` (size) / `{0, 1}` (time): I/O failed; do not retry. | constants above | `0`, error code |

## Invariants

- **Tri-state, never bare zero:** `0` bytes is a valid loaded size (empty file); unknown is `kFileSizeNotLoaded`. Conflating `0` with unknown triggers I/O storms.
- **`$DATA`-only sourcing:** file size comes **exclusively** from the `$DATA` attribute (0x80); `$FILE_NAME` size is used only when no `$DATA` attribute exists at all, and never overrides it.
- **Failure is sticky:** once an attribute is marked `Failed`, it is not retried (prevents retry loops on deleted/permission-denied files).
- **No locks during I/O:** double-check locking reads path under a shared lock, performs I/O lock-free, then takes a unique lock only to write the result.
- **Load together when both needed:** size and modification time are fetched in one I/O pass when both are requested.
- **Directory entries:** a directory's size is `kFileSizeNotLoaded` unless explicitly loaded; UI must treat it as "not applicable", not "0 bytes".
- **Sentinel checks:** use `IsSentinelTime`/`IsFailedTime`/`IsValidTime` helpers; do not compare raw fields ad hoc.

## Threading

- All public methods are thread-safe (use `FileIndex`'s `shared_mutex`).
- I/O happens with **no lock held**; the unique lock is taken only for the final write.
- Double-check locking prevents redundant I/O under contention.

## Forbidden (do not introduce)

- Synonyms: `AttributeCache`, `Metadata`, `FileMeta`, `LazyValue`.
- Using `0` to mean "not loaded" or "failed".
- Retrying failed attributes.
- Holding any lock during file I/O.
- Sourcing size from `$FILE_NAME` when `$DATA` is present.