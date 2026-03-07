# USN Search Application Architecture (2025-12-19)

## 1. High-Level Overview

The USN Search application is a high-performance Windows GUI tool for real-time file system indexing and ultra-fast search over NTFS volumes.

At a high level, it consists of four primary layers:

- **USN ingestion layer** (`UsnMonitor`, `InitialIndexPopulator`):
  - Uses the NTFS USN Journal and MFT enumeration to build and maintain an in-memory index of files and folders.
  - Has a two-stage lifecycle:
    - **Initial index population**: a bulk scan of the MFT into `FileIndex` using `FSCTL_ENUM_USN_DATA`.
    - **Steady-state monitoring**: continuous consumption of USN journal records via `FSCTL_READ_USN_JOURNAL` to keep the index up to date.

- **Index layer** (`FileIndex`):
  - Maintains a highly optimized, cache-friendly **Structure of Arrays (SoA)** representation of all indexed paths and metadata.
  - Uses a single large contiguous `std::vector<char>` buffer for all paths, with parallel arrays holding offsets, IDs, flags, and pre-parsed filename/extension offsets.
  - Provides parallel search APIs (`Search`, `SearchAsync`, `SearchAsyncWithData`) that exploit multi-core CPUs with minimal contention.

- **Search and coordination layer** (`SearchWorker`, `SearchController`, `GuiState`):
  - `SearchWorker` runs a dedicated background thread that owns the search lifecycle and communicates with `FileIndex`.
  - `SearchController` lives on the GUI thread; it translates UI state (`GuiState`) into `SearchParams`, triggers searches (manual and debounced instant search), and reconciles results back into `GuiState`.

- **UI layer** (`main_gui.cpp`, `DirectXManager`, `GuiState`):
  - A single Windows GUI thread (Win32 + ImGui + DirectX via `DirectXManager`) renders the application and handles events.
  - The UI binds to `GuiState` and delegates search/index coordination to `SearchController` and `UsnMonitor`.

This architecture prioritizes **throughput**, **responsiveness**, and **cache efficiency** while keeping the core concurrency model relatively simple and explicit.

---

## 2. Key Components & Responsibilities

### 2.1 USN Ingestion Layer

#### 2.1.1 `UsnMonitor`

`UsnMonitor` encapsulates all logic for interacting with the NTFS USN journal and coordinating index updates.

- **Responsibilities**:
  - Owns a `UsnJournalQueue` for decoupling I/O (reader) from CPU-bound processing (processor).
  - Manages two dedicated threads:
    - **Reader thread** (`ReaderThread`):
      - Opens the volume (e.g. `\\.\C:`) using `VolumeHandle`.
      - Runs `PopulateInitialIndex` before live monitoring begins.
      - Issues `DeviceIoControl` calls (`FSCTL_READ_USN_JOURNAL`) to fetch USN records in 64KB buffers.
      - Applies kernel-level filtering via `ReasonMask` to reduce data volume.
      - Pushes filled buffers into `UsnJournalQueue` and updates metrics.
    - **Processor thread** (`ProcessorThread`):
      - Pops buffers from `UsnJournalQueue` and parses `USN_RECORD_V2` entries.
      - Translates USN events (create/delete/rename/modify) into operations on `FileIndex`.
      - Periodically yields to ensure the GUI/search threads can acquire `FileIndex` locks.
      - Optionally runs maintenance tasks (e.g. `FileIndex::Maintain()`).
  - Exposes thread-safe status and metrics:
    - `IsActive()`, `IsPopulatingIndex()`, `GetIndexedFileCount()`.
    - `UsnMonitorMetrics` snapshot for performance and health monitoring.

- **Design trade-offs**:
  - **Two-thread vs single-thread** for monitoring:
    - **Pros**:
      - Reader can continue fetching USN data while processor parses and updates the index.
      - Reduces risk of kernel dropping events during processing spikes.
      - Keeps GUI-responsive by keeping expensive work off the main thread.
    - **Cons**:
      - Adds complexity for queue coordination and backpressure.
      - Requires careful metrics and logging to detect overload conditions.

  - **Queue-based communication (`UsnJournalQueue`)**:
    - **Pros**:
      - Decouples blocking I/O from CPU work.
      - Smooths out bursts of file system activity.
      - Allows batching multiple records per buffer for efficient parsing.
    - **Cons**:
      - Requires a size limit and drop policy to avoid unbounded memory growth.
      - Dropped buffers imply lost events and a temporarily inconsistent index (though this is monitored via metrics).

#### 2.1.2 `UsnJournalQueue`

`UsnJournalQueue` is a thread-safe queue with a bounded capacity.

- **Responsibilities**:
  - Stores USN buffers (64KB `std::vector<char>` objects) produced by the reader thread.
  - Provides `Push` and `Pop` operations with internal locking and a condition variable.
  - Maintains a fast, atomic `size_` counter for frequent size checks without locking.
  - Tracks `dropped_count_` for buffers dropped when the queue is full.

- **Design trade-offs**:
  - **Cap-and-drop policy**:
    - Prevents unbounded growth during sustained high activity.
    - Sacrifices guaranteed completeness of the index under extreme load (mitigated by metrics and future recovery strategies).

#### 2.1.3 `InitialIndexPopulator` and `PopulateInitialIndex`

`PopulateInitialIndex` runs once per monitored volume to perform a full MFT sweep and seed the index.

- **Responsibilities**:
  - Uses `FSCTL_ENUM_USN_DATA` to enumerate all USN records across the MFT.
  - Parses each `USN_RECORD_V2` and computes:
    - File ID (`FileReferenceNumber`).
    - Parent ID (`ParentFileReferenceNumber`).
    - Directory flag (from attributes).
    - Filename (converted from UTF-16 to UTF-8).
    - Initial modification time sentinel (for files) or `{0,0}` (for directories).
  - Inserts entries into `FileIndex` via `file_index.Insert(...)`.
  - Periodically updates an optional `indexed_file_count_` atomic for UI progress.
  - Calls `file_index.FinalizeInitialPopulation()` after enumeration to finalize path computation and handle out-of-order parents.

- **Performance considerations**:
  - Uses a 64KB buffer and robust bounds-checking to safely iterate records.
  - Does not perform expensive I/O for file size or modification time; uses sentinel values instead, deferring actual I/O to the UI’s lazy loading when needed.

---

### 2.2 Index Layer: `FileIndex`

`FileIndex` is the core data structure and is intentionally optimized for search throughput and memory locality.

#### 2.2.1 Structure of Arrays (SoA) design

- **Path storage**:
  - All paths are stored in a single contiguous `std::vector<char> path_storage_` as null-terminated UTF-8 strings placed back-to-back.
  - Each path is referenced by an offset into this buffer.

- **Parallel arrays**:
  - `path_offsets_`: index → offset into `path_storage_` where the path begins.
  - `path_ids_`: index → file ID (NTFS file reference number).
  - `filename_start_`: index → offset within the path string where the filename begins.
  - `extension_start_`: index → offset within the path string where the extension begins (or `SIZE_MAX` if none).
  - `is_deleted_`: index → deletion flag (0/1).
  - `is_directory_`: index → directory flag (0/1).
  - `id_to_path_index_`: hash map from file ID → SoA index.

- **Why not `std::vector<std::string>`?**
  - **Cache locality**:
    - Contiguous `char` storage lets search threads walk almost-linear memory.
    - CPU caches and hardware prefetchers perform significantly better when scanning a single large region than when chasing millions of independent heap allocations.
  - **Allocation overhead**:
    - Single (or a few) large allocations vs. millions of per-string allocations.
    - Greatly reduces allocator contention, fragmentation, and overhead.
  - **Memory footprint**:
    - No per-`std::string` object overhead (~24–32 bytes per string on typical 64-bit ABIs).
    - For 1M entries with ~100B average path length, this can save ~24–32MB.
  - **Parallel search performance**:
    - Multiple threads can each process a chunk of the contiguous buffer efficiently.
    - Avoids scattered loads and cache misses inherent in `std::vector<std::string>`.

- **Trade-offs**:
  - Slightly more complex code (offset arithmetic, manual `strlen`, etc.) in hot paths.
  - Requires careful synchronization and maintenance to keep the SoA structures consistent.

#### 2.2.2 Thread safety & locking

- `FileIndex` uses a `std::shared_mutex mutex_` to protect its arrays and maps.

- **Read operations (search)**:
  - Acquire `std::shared_lock<std::shared_mutex>` for the duration of the search.
  - Launch multiple async tasks that operate only on immutable arrays and flags.
  - No per-entry locking; all threads read from a stable snapshot of the arrays.

- **Write operations (insert/update/delete/rebuild)**:
  - Acquire `std::unique_lock<std::shared_mutex>`.
  - Update SoA arrays, `id_to_path_index_`, and deletion flags.

- **Trade-offs**:
  - Optimized for **read-mostly** workloads: many concurrent searches, fewer structural modifications.
  - Writers are blocked by long-running searches; extremely large queries can temporarily delay index updates.

#### 2.2.3 Search APIs

- **`Search`**:
  - Synchronous function returning a vector of IDs.
  - Splits the SoA arrays into chunks and launches async tasks to scan each chunk in parallel.
  - For each chunk, threads apply a staged filter pipeline:
    - Skip deleted entries and non-folders (if `folders_only`).
    - Filter on extension using `ExtensionMatches` over `std::string_view` (O(1) hash lookup).
    - Apply filename matching (regex/glob/substring, case-sensitive or insensitive).
    - Optionally apply directory-path filter.
  - Returns a flattened vector of matching IDs.

- **`SearchAsync`**:
  - Similar to `Search`, but returns futures instead of collecting all results in place.
  - Allows the caller to coordinate fetching and post-processing of partial results.

- **`SearchAsyncWithData`**:
  - Extends `SearchAsync` by **extracting filename and extension** during the search.
  - Each shard returns a `SearchResultData` struct:
    - `{ id, isDirectory, fullPath, filename, extension }`.
  - Eliminates extra hash-map lookups and parsing in higher layers; post-processing only has to wrap these into UI-facing structures.

#### 2.2.4 Maintenance (`Maintain` and `RebuildPathBuffer`)

- Over time, entries are marked deleted in SoA arrays, leaving gaps in `path_storage_` and associated arrays.

- **`Maintain`**:
  - Uses a shared lock to read `deleted_count_` and total entries.
  - If the absolute number of deleted entries or their percentage exceeds thresholds, it triggers a rebuild.

- **`RebuildPathBuffer`**:
  - Acquires a unique lock.
  - Allocates new SoA arrays and a new contiguous path buffer.
  - Copies only non-deleted entries into the new structures, preserving pre-parsed offsets.
  - Swaps the new structures in and resets `deleted_count_`.

- **Trade-offs**:
  - Occasional expensive operation, but executed in the background and infrequently.
  - Restores maximum cache locality and reduces memory waste from deleted entries.

---

### 2.3 Search Layer: `SearchWorker` and `SearchController`

#### 2.3.1 `SearchWorker`

`SearchWorker` owns the background search thread and provides a simple API to start a search and retrieve results.

- **Thread & lifecycle**:
  - On construction, spawns a `std::thread` running `WorkerThread` and sets `running_ = true`.
  - Destructor signals shutdown, notifies the condition variable, and joins the thread.

- **Synchronization state**:
  - `mutex_` + `cv_` for coordination between GUI thread and search worker.
  - Protected members:
    - `next_params_`: latest requested `SearchParams`.
    - `search_requested_`: flag indicating a pending search.
    - `results_`: vector of `SearchResult` objects produced by the last completed search.
    - `has_new_results_`: flag to indicate that the GUI should read `results_`.
    - `is_searching_`, `search_complete_`: status flags.

- **Core loop (`WorkerThread`)**:
  1. Wait on `cv_` for `search_requested_` or `running_ == false`.
  2. When signaled, copy `next_params_` under lock and reset `search_requested_`.
  3. Parse and log search parameters, including extension lists and normalized queries.
  4. Choose search path:
     - **Filtered search** (common case): use `FileIndex::SearchAsyncWithData`.
     - **Show-all search** (no query and no extensions): iterate all entries with `FileIndex::ForEachEntry`.
  5. Collect all futures from `SearchAsyncWithData` and merge their results.
     - Perform periodic cancellation checks (`search_requested_`) to drop stale work.
  6. Convert `SearchResultData` into UI-facing `SearchResult` objects, with lazily-loaded metadata sentinels.
  7. Update search metrics (counts, times, maxima).
  8. Under lock, if no newer search was requested, publish `results_`, clear `is_searching_`, set `search_complete_`, and mark `has_new_results_ = true`.

- **Design trade-offs**:
  - **Single background search thread, many internal shards**:
    - Keeps coordination simple: only one user-visible search is processed at a time.
    - Still exploits multi-core CPUs by using `FileIndex`’s internal parallelism.
  - **Cancellation** is opportunistic at natural boundaries (after futures, before heavy post-processing), which is sufficient for UI responsiveness.

#### 2.3.2 `SearchController`

`SearchController` mediates between `GuiState`, `SearchWorker`, and `UsnMonitor`.

- **Responsibilities**:
  - Enforces policy: when to trigger searches, when to auto-refresh, and how to deal with result changes.
  - Implements **debounced instant search** and **auto-refresh when index changes**.

- **Key methods**:
  - `Update(GuiState &state, SearchWorker &searchWorker, const UsnMonitor *monitor)`:
    - If `monitor && monitor->IsPopulatingIndex()`, prohibits new searches while polling for any old results.
    - For instant search, calls `ShouldTriggerDebouncedSearch`:
      - Checks `state.instantSearch`, `state.inputChanged`, elapsed time since `lastInputTime`, and `!searchWorker.IsBusy()`.
      - On trigger, builds `SearchParams` and calls `searchWorker.StartSearch`.
    - For auto-refresh:
      - Reads latest `index_size` from `monitor`.
      - If `autoRefresh`, `searchActive`, and size changed (and worker not busy), triggers a new search with same `SearchParams`.
    - Polls for new results with `PollResults`.

  - `PollResults(GuiState &state, SearchWorker &searchWorker)`:
    - If `searchWorker.HasNewResults()`:
      - Moves results from `searchWorker` and inspects `IsSearchComplete()`.
      - Uses fast paths to handle transitions like empty → populated, populated → empty.
      - For complete results, may call `CompareSearchResults` to avoid unnecessary UI refreshes.
      - For partial (incremental) results, always updates to show progress.

  - `CompareSearchResults`:
    - Compares metadata of old vs new results keyed by file ID.
    - Treats certain “downgrades” (e.g. from loaded file size to sentinel) as non-changes to avoid visual flicker from lazy-loading semantics.

- **Trade-offs**:
  - Extra logic and metadata handling complexity keeps the UI stable and flicker-free, especially when auto-refresh is enabled and the index is constantly changing.

#### 2.3.3 `GuiState`

`GuiState` is a simple data struct holding UI-related state and the latest search results.

- Holds:
  - User inputs: `filenameInput`, `pathInput`, `extensionInput`, `foldersOnly`, `caseSensitive`, etc.
  - Search flags: `instantSearch`, `autoRefresh`, `searchActive`, `inputChanged`, `lastInputTime`.
  - Search results: `std::vector<SearchResult> searchResults`, plus `resultsUpdated` flag and `lastIndexSize`.

- Provides helpers like:
  - `MarkInputChanged()`: sets `inputChanged` and updates `lastInputTime`.
  - `ClearInputs()`: resets search inputs and flags.

---

### 2.4 UI Layer

The UI layer is primarily contained in `main_gui.cpp` (a large, multi-responsibility file) and `DirectXManager`.

- **Responsibilities**:
  - Create and manage the Windows window, message loop, and DirectX graphics context.
  - Initialize ImGui and render all panels, input widgets, tables, and status bars.
  - Bind to `GuiState` to:
    - Read and display search results.
    - Edit search inputs and toggle flags.
    - Show indexing progress and USN monitor metrics.

- **Current design**:
  - `main_gui.cpp` is a “god file” that mixes UI rendering, input handling, state management, and bits of business logic.
  - A recommended future refactor is to split this into separate classes (`AppState`, `UIManager`, `EventHandler`) to reduce coupling and complexity.

---

## 3. Threading Model & Concurrency

### 3.1 Thread Inventory

At runtime, the application typically involves the following threads:

1. **GUI Thread**
   - Runs the Win32 message loop and ImGui/DirectX rendering.
   - Owns and mutates `GuiState`.
   - Calls into `SearchController::Update`, `UsnMonitor::Start/Stop`, and metrics getters.

2. **Search-Worker Thread** (from `SearchWorker`)
   - Runs `SearchWorker::WorkerThread`.
   - Waits on a condition variable for new `SearchParams`.
   - Executes filtered and show-all searches by delegating to `FileIndex`.
   - Publishes `SearchResult` vectors back to the GUI via thread-safe flags.

3. **USN Reader Thread** (from `UsnMonitor`)
   - Runs `UsnMonitor::ReaderThread`.
   - Performs initial index population (`PopulateInitialIndex`).
   - Then enters a loop calling `DeviceIoControl(FSCTL_READ_USN_JOURNAL, ...)` to read new USN records and push buffers into `UsnJournalQueue`.

4. **USN Processor Thread** (from `UsnMonitor`)
   - Runs `UsnMonitor::ProcessorThread`.
   - Pops buffers from `UsnJournalQueue`, parses `USN_RECORD_V2` entries, and applies updates to `FileIndex`.
   - Periodically triggers `FileIndex::Maintain()` and updates metrics.

5. **Parallel Search Tasks** (from `FileIndex`)
   - For each search, `FileIndex::Search`, `SearchAsync`, or `SearchAsyncWithData` spawn several short-lived async tasks.
   - These tasks share a snapshot of the SoA arrays under a shared lock and perform read-only scanning and filtering.

### 3.2 Multithreading Approach

- **Clear separation by responsibility**:
  - GUI is isolated from heavy I/O and CPU work.
  - USN ingestion is split into reader (I/O-bound) and processor (CPU-bound) threads.
  - Search coordination is centralized in a single background thread, which then fans out into many small search tasks.

- **Locking discipline**:
  - Shared data is encapsulated (e.g. `FileIndex`, `UsnJournalQueue`, `SearchWorker` internal state).
  - `FileIndex` uses a shared mutex to enable concurrent read-only searches with exclusive writes.
  - `UsnJournalQueue` combines a regular mutex for queue integrity with an atomic size counter for frequent sampling.

- **Producer–consumer patterns**:
  - USN Reader → `UsnJournalQueue` → USN Processor.
  - GUI → `SearchWorker` (via `StartSearch`) → Search results back to GUI.

- **Parallelism strategy**:
  - Use **coarse-grained** threads for major responsibilities (GUI, search, reader, processor).
  - Inside `FileIndex`, use **fine-grained** async tasks to maximize CPU utilization during searches.

- **Responsiveness and cancellation**:
  - Searching is debounced and suppressed while the index is still being populated.
  - The search worker checks for new search requests (cancellation) at safe boundaries to avoid publishing stale results.
  - `SearchController` avoids unnecessary UI updates by comparing old and new results when appropriate.

---

## 4. Sequence Diagrams

This section provides sequence diagrams for the three core flows: startup/indexing, user search, and filesystem change propagation.

### 4.1 Startup and Initial Index Population

```text
Participants:
  [GUI Thread]          main_gui / DirectXManager / ImGui
  [GUI Logic]           SearchController, GuiState
  [Monitor API]         UsnMonitor
  [ReaderThread]        UsnMonitor::ReaderThread
  [ProcessorThread]     UsnMonitor::ProcessorThread
  [Index Populator]     PopulateInitialIndex
  [Index]               FileIndex

1. [GUI Thread] creates:
   - FileIndex index
   - UsnMonitor monitor(index, config)
   - SearchWorker searchWorker(index)   // spawns Search-Worker thread
   - GuiState state
   - SearchController controller

2. [GUI Thread] → monitor.Start()
   - Creates UsnJournalQueue
   - Starts ReaderThread
   - Starts ProcessorThread
   - Sets monitoring_active_ = true

3. [ReaderThread] entry:
   - is_populating_index_ = true
   - Opens VolumeHandle("\\\\.\\C:")
   - Calls PopulateInitialIndex(volume_handle, index, &indexed_file_count_)

4. [Index Populator] PopulateInitialIndex:
   - Loop:
     - DeviceIoControl(FSCTL_ENUM_USN_DATA, ...) → fill 64KB buffer
     - For each USN_RECORD_V2 in buffer:
       - Decode fileRef, parentRef, attributes, filename
       - WideToUtf8(filename)
       - Compute isDirectory, modTime sentinel
       - index.Insert(fileRef, parentRef, filename, isDirectory, modTime)
         - Unique-lock FileIndex
         - Append to path_storage_ and SoA arrays
       - Periodically update indexed_file_count_
   - After enumeration:
     - index.FinalizeInitialPopulation()
     - indexed_file_count_ = index.Size()
     - is_populating_index_ = false

5. [ReaderThread] transitions to steady-state:
   - Loop:
     - DeviceIoControl(FSCTL_READ_USN_JOURNAL, ...) → 64KB buffer
     - queue_->Push(std::move(buffer))
     - Update metrics (buffers_read, total_read_time_ms)

6. [ProcessorThread] loop:
   - queue_->Pop(buffer)
   - For each USN record in buffer:
     - Decode event flags and attributes
     - Apply insert/delete/rename/modify to FileIndex
       - Unique-lock FileIndex briefly
   - metrics.records_processed++, etc.
   - Optionally call index.Maintain()

7. [GUI Thread] render loop:
   - Each frame:
     - monitor.IsPopulatingIndex() ? show "Building index…" and suppress searches
     - controller.Update(state, searchWorker, &monitor)
```

### 4.2 User Search (Search-as-You-Type)

```text
Participants:
  [GUI Thread]      ImGui + main_gui
  [GUI State]       GuiState
  [Controller]      SearchController
  [Search API]      SearchWorker
  [Search-Worker]   SearchWorker::WorkerThread
  [Index]           FileIndex
  [Search Shards]   N async tasks from FileIndex::SearchAsyncWithData

1. [GUI Thread] user types in search inputs:
   - ImGui modifies state.filenameInput / pathInput / extensionInput
   - state.MarkInputChanged() sets inputChanged = true and updates lastInputTime

2. [GUI Thread] each frame:
   - controller.Update(state, searchWorker, &monitor)

3. [Controller] Update:
   - If monitor && monitor->IsPopulatingIndex():
     - Only PollResults; no new search
   - Else:
     - If ShouldTriggerDebouncedSearch(state, searchWorker):
       - state.inputChanged = false
       - state.searchActive = true
       - BuildSearchParams(state) → SearchParams params
       - searchWorker.StartSearch(params)
   - PollResults(state, searchWorker)

4. [Search API] StartSearch:
   - lock(mutex_):
     - next_params_ = params
     - search_requested_ = true
   - cv_.notify_one()

5. [Search-Worker] WorkerThread:
   - Wait on cv_ until search_requested_ || !running_
   - If !running_ → exit
   - lock(mutex_):
     - params = next_params_
     - search_requested_ = false
     - is_searching_ = true
     - search_complete_ = false

6. [Search-Worker] performs search:
   - Parse extension filters (ParseExtensions)
   - Normalize queries (Trim, ToLower as needed)
   - Decide path:
     - Filtered search → FileIndex::SearchAsyncWithData
     - Show-all → FileIndex::ForEachEntry

7. FileIndex::SearchAsyncWithData:
   - Acquire shared_lock(mutex_)
   - Precompute lowercased queries, regex/glob flags, extension hash_set
   - Determine thread_count and chunk_size based on number of entries and bytes
   - For each chunk [start_index, end_index):
     - Launch async shard:
       - Iterate entries in range
       - Skip deleted entries and non-folders (if folders_only)
       - Filter extension via ExtensionMatches over std::string_view
       - Apply filename and directory-path matchers
       - For matches, build SearchResultData (id, isDirectory, fullPath, filename, extension)
   - Return vector<future<vector<SearchResultData>>> and release shared_lock

8. [Search-Worker] collects futures:
   - For each future:
     - chunkData = future.get()
     - Append chunkData into allSearchData (move)
     - Periodically check search_requested_ for cancellation
   - totalCandidates = allSearchData.size()

9. [Search-Worker] post-processes results:
   - For each SearchResultData:
     - Build SearchResult:
       - fullPath, fileId, isDirectory
       - fileSize = (dir ? 0 : kFileSizeNotLoaded)
       - lastModificationTime = (dir ? {0,0} : kFileTimeNotLoaded)
       - filename/extension as string_view slices over fullPath
   - Update metrics (counts, times, maxima)

10. [Search-Worker] publishes results:
    - lock(mutex_):
      - If !search_requested_:
        - results_ = std::move(results)
        - has_new_results_ = true
        - is_searching_ = false
        - search_complete_ = true

11. [GUI Thread] polling:
    - controller.PollResults(state, searchWorker)

12. [Controller] PollResults:
    - If searchWorker.HasNewResults():
      - new_results = searchWorker.GetResults() (move)
      - is_complete = searchWorker.IsSearchComplete()
      - Handle special cases (empty→populated, populated→empty)
      - For partial results: always update
      - For complete results: optionally CompareSearchResults to avoid unnecessary refresh
      - Update state.searchResults and state.resultsUpdated when appropriate

13. [GUI Thread] renders updated search results from state.searchResults
```

### 4.3 Filesystem Change → USN → Index Update → Auto-Refresh

```text
Participants:
  [OS]               NTFS + USN Journal
  [ReaderThread]     UsnMonitor::ReaderThread
  [ProcessorThread]  UsnMonitor::ProcessorThread
  [Index]            FileIndex
  [GUI Thread]       main_gui + ImGui
  [Monitor API]      UsnMonitor
  [Controller]       SearchController
  [Search API]       SearchWorker / Search-Worker

1. [OS] user changes the filesystem:
   - Creates, deletes, renames, or modifies a file/folder

2. [OS] records event in USN Journal

3. [ReaderThread] blocking in DeviceIoControl(FSCTL_READ_USN_JOURNAL,...):
   - Once enough data or timeout reached:
     - Returns a buffer containing multiple USN records

4. [ReaderThread] → queue_->Push(buffer):
   - If queue not full: enqueue buffer, size_++
   - If full: drop buffer, dropped_count_++
   - metrics.buffers_read++

5. [ProcessorThread] → queue_->Pop(buffer):
   - Parses each USN record:
     - Decode id, parentId, flags, attributes, filename
     - Map flags to FileIndex operations:
       - Create: insert new entry
       - Delete: mark entry deleted / remove mappings
       - Rename: update path / prefix for affected entries
       - Modify: update cached size/modTime when applicable
     - Each operation:
       - Unique-lock FileIndex briefly
   - metrics.records_processed++, files_created/deleted/renamed/modified++

6. [ProcessorThread] periodically:
   - metrics.current_queue_depth = queue_->Size()
   - Optionally calls index.Maintain()
   - Updates indexed_file_count_ from index.Size()

7. [GUI Thread] next frame:
   - controller.Update(state, searchWorker, &monitor)

8. [Controller] Update:
   - current_index_size = monitor->GetIndexedFileCount()
   - If state.autoRefresh && state.searchActive && current_index_size != state.lastIndexSize:
     - If !searchWorker.IsBusy():
       - state.lastIndexSize = current_index_size
       - state.inputChanged = false
       - BuildSearchParams(state)
       - searchWorker.StartSearch(params)
   - PollResults(state, searchWorker)

9. [Search-Worker] executes search on updated index as in previous flow

10. [GUI Thread] receives new results via PollResults and re-renders the table
```

---

## 5. Summary

- The architecture is **USN-driven**, **index-centric**, and **search-optimized**, with a deliberate focus on cache locality and parallelism.
- Concurrency is managed via:
  - Separate long-lived threads for GUI, search coordination, and USN ingestion.
  - A shared-mutex-protected `FileIndex` with read-heavy, write-light access patterns.
  - A bounded queue to decouple USN I/O from index updates.
- The design trades some code simplicity for significant performance, especially in the hot search paths operating over millions of file system entries.
