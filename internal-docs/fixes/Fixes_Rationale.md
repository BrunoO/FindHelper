# Production Readiness: Fixes and Rationale

This document provides a detailed explanation of the technical fixes implemented to ensure the application is robust, performant, and maintainable for production use.

## 1. FileIndex: Directory Cache Memory Leak
### The Issue
The `FileIndex` uses a `directory_path_to_id_` map to quickly resolve directory paths to their internal IDs. Previously, when a directory was removed, renamed, or moved, its entry in this map was not updated or removed. Over time, this led to a growing memory footprint containing "dead" path mappings.

### The Fix
Modified `Remove`, `Rename`, and `Move` in `FileIndex.h` to explicitly manage the cache:
- **Remove**: Now clears the path from the cache if the entry is a directory.
- **Rename/Move**: Clears the old path and ensures the new structure is correctly mapped.

### Rationale
Ensuring the cache stays in sync with the index prevents long-term memory bloat, which is critical for an application intended to run in the background for extended periods.

---

## 2. Search Performance: Thread Pool Integration
### The Issue
Parallel searches were previously using `std::async(std::launch::async, ...)`, which often results in the OS spawning a new thread for every search task. This creates significant overhead in thread creation/destruction and context switching, especially during rapid consecutive searches.

### The Fix
Refactored `FileIndex::SearchAsync` and `SearchAsyncWithData` to utilize a dedicated `SearchThreadPool`:
- Search tasks are now enqueued into a pre-allocated pool of worker threads.
- Added error handling to gracefully fall back or report errors if the thread pool is unavailable.

### Rationale
Thread reuse significantly reduces the "time-to-first-result" and lowers the overall system impact during heavy search activity.

---

## 3. Data Optimization: SearchResultData Offsets
### The Issue
The `SearchResultData` struct (used to pass results from worker threads) was storing `std::string` copies of the filename and extension for every matching file. This resulted in thousands of redundant allocations and string copies per search.

### The Fix
Updated `SearchResultData` to store only the `fullPath` and two `size_t` offsets: `filename_offset` and `extension_offset`.
- worker threads calculate these once during the scan.
- `SearchWorker` then uses these offsets to create `std::string_view` objects directly into the `fullPath`.

### Rationale
Reducing memory allocations is the most effective way to improve C++ performance in hot paths. This change dramatically speeds up the "Gathering Results" phase of the search.

---

## 4. Code Consolidation: InsertLocked
### The Issue
Insertion logic was duplicated across public `Insert` methods and internal USN monitoring helpers. This made the code harder to maintain and increased the risk of deadlocks due to inconsistent locking patterns.

### The Fix
Extracted all core insertion logic into a single private method: `InsertLocked`.
- Centralizes full path building, extension interning, and counter updates.
- Guaranteed to be called only when the caller holds a unique lock on the index.

### Rationale
Simplifying the "source of truth" for index modifications makes the code safer and easier to audit for thread-safety issues.

---

## 5. UsnMonitor: Structural Hardening and Throughput
### The Issue
The USN processor loop had a structural flaw where the `offset` increment was inside an `if` block, creating a risk of infinite loops on malformed records. Additionally, it updated the atomic `indexed_file_count_` for every single record, causing unnecessary cache contention.

### The Fix
- **Loop Safety**: Moved the record offset increment to the end of the loop, ensuring forward progress regardless of record content.
- **Batching**: Moved the `indexed_file_count_` update and UI yielding logic outside the per-record loop to the per-buffer level.
- **Metric Accuracy**: Restored missing metric increments for `records_processed`.

### Rationale
USN Journal processing is a high-traffic background task. Hardening this loop ensures the background monitor stays alive even under heavy load or edge-case file system events.
