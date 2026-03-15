# Lock Ordering and Critical Sections (2026-03-15)

This document centralizes the threading and locking rules for the FindHelper codebase. It addresses the architecture review finding on lock scope and reader/writer usage (SearchThreadPool, FileIndex, UsnMonitor).

## Rule: No I/O or Heavy Work Inside Critical Sections

**All code that holds a mutex (including `shared_lock` or `unique_lock`) must avoid:**

- **I/O** – file system, network, or other blocking I/O. Do not call `stat()`, `GetFileAttributes`, `ReadFile`, or similar while holding any lock.
- **Heavy computation** – keep work inside a critical section to the minimum: read/write of in-memory structures, simple condition checks, and short bookkeeping.

**Rationale:** Holding a lock across I/O blocks other threads (e.g. search workers or UI) and can cause contention, latency spikes, or deadlock if another component needs the same lock. FileIndex uses a single `shared_mutex` for the index; long-held locks serialize all readers and block writers.

## Lock Ordering

There is **no global lock ordering** between components: each subsystem uses its own mutex(es). To avoid deadlock:

1. **Never acquire more than one of these mutexes at a time** in the same thread unless the order is explicitly documented below.
2. **FileIndex (`index_mutex_`)**: Only lock this mutex. Do not acquire SearchThreadPool’s mutex or UsnMonitor’s mutex while holding `index_mutex_`.
3. **SearchThreadPool (`mutex_`)**: Protects the task queue and shutdown flag only. Workers run user code (e.g. search) which may acquire FileIndex’s mutex; the pool itself does not take FileIndex’s lock.
4. **UsnMonitor (`mutex_`)**: Protects start/stop state, volume handle, and queue pointer. I/O (e.g. `ReadDirectoryChangesW`, `CloseHandle`) is done **outside** the lock; see UsnMonitor.cpp (e.g. `Stop()`, handle close after `guard` scope).
5. **UsnJournalQueue (`mutex_`)**: Protects the queue container only. No I/O inside this lock.
6. **SearchWorker / StreamingResultsCollector / Logger / IndexBuildState**: Each has its own mutex; no ordering is required with FileIndex or UsnMonitor as long as no lock is held across I/O.

**Summary:** Lock ordering is trivial because we do not nest locks across these components. The main discipline is to keep critical sections short and never perform I/O (or heavy work) while holding any lock.

## Component Summary

| Component            | Mutex / lock type   | Protects                               | I/O under lock? |
|---------------------|---------------------|----------------------------------------|------------------|
| FileIndex           | `std::shared_mutex` | PathStorage, FileIndexStorage, path_to_id, etc. | No – see UpdateSize(), LazyAttributeLoader |
| SearchThreadPool    | `std::mutex`        | Task queue, shutdown_                  | No               |
| UsnMonitor          | `std::mutex`        | Start/stop, volume_handle_, queue_     | No – handle close outside lock |
| UsnJournalQueue     | `std::mutex`        | queue_, stop_, dropped_count_          | No               |
| LazyAttributeLoader | FileIndex’s mutex   | Cache updates only                     | No – read path under lock, I/O outside, write under lock |
| FileIndexMaintenance| FileIndex’s mutex   | Path storage reads; RebuildPathBuffer  | No – rebuild is in-memory only |
| ParallelSearchEngine| FileIndex’s mutex   | Index view for search                  | No – workers only read index data |

## References

- **FileIndex:** `GetMutex()` returns the shared_mutex; workers take `shared_lock`, Insert/Remove/Clear take `unique_lock`. `UpdateSize()` and LazyAttributeLoader use the 3-phase pattern: read path under shared lock, I/O with no lock, write under unique lock.
- **PathOperations.h / IndexOperations.h / DirectoryResolver.h:** All document “caller must hold unique_lock (or shared_lock where stated) on mutex”; they do not acquire locks themselves.
- **UsnMonitor.cpp:** Comments at handle close: “close it outside the critical section”, “Cancel any pending I/O operations and close the handle outside the mutex”.

When adding new code that holds a lock, add a brief comment if non-obvious (e.g. “No I/O under this lock – see docs/design/2026-03-15_LOCK_ORDERING_AND_CRITICAL_SECTIONS.md”).
