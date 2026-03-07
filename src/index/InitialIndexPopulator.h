#pragma once

#include <atomic>
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem

// Forward declarations
class FileIndex;

/**
 * InitialIndexPopulator - Populates the FileIndex with all existing files on a volume
 *
 * This module handles the initial enumeration of all files on a Windows volume by
 * reading the Master File Table (MFT) using FSCTL_ENUM_USN_DATA. It is called once
 * at application startup, before USN journal monitoring begins, to establish a
 * complete baseline of all files on the volume.
 *
 * THREADING MODEL:
 * - This function is called once at application startup from within a background thread
 *   (UsnMonitor::ReaderThread) to keep the GUI responsive during the potentially long-running
 *   enumeration (1-5 seconds for typical volumes with 100k-500k files). The call chain
 *   is: GUI thread creates UsnMonitor instance and calls Start() -> which creates ReaderThread -> which
 *   calls this function. The thread is created specifically to handle both initial
 *   population and subsequent USN journal monitoring. This function performs the initial
 *   MFT enumeration (using FSCTL_ENUM_USN_DATA) to establish a baseline of all existing
 *   files. After this function completes, the same thread then switches to USN journal
 *   monitoring (using FSCTL_READ_USN_JOURNAL) to track incremental changes. These are two
 *   distinct operations - initial population reads the entire MFT, while monitoring reads
 *   incremental changes from the USN journal.
 * - Each call to fileIndex.Insert() acquires a unique_lock on FileIndex's shared_mutex,
 *   which blocks concurrent readers (e.g., search operations) and writers. During initial
 *   population, there are no concurrent writers yet (USN monitoring hasn't started),
 *   but this exclusive locking ensures consistency and prevents searches from seeing
 *   incomplete data.
 * - Progress updates are communicated to the UI thread via an atomic counter passed
 *   as a parameter (indexedFileCount), which is updated periodically (every 10,000 files)
 *   using relaxed memory ordering. This provides sufficient visibility for progress
 *   display without the overhead of stronger memory ordering guarantees.
 * - After all files are inserted, RecomputeAllPaths() is called, which also holds a
 *   unique_lock. This ensures all paths are correctly computed even when MFT records
 *   arrive out of order (e.g., a child file before its parent directory).
 *
 * DESIGN TRADE-OFFS:
 * 1. Single-threaded enumeration: The MFT enumeration API (FSCTL_ENUM_USN_DATA) is
 *    inherently sequential, so parallelizing the enumeration itself would be difficult.
 *    We could theoretically parallelize path computation, but the sequential I/O dominates
 *    the time, making this optimization not worthwhile.
 *
 * 2. Deferred path computation: Rather than computing full paths during insertion (which
 *    would fail for out-of-order parents), we insert all entries first, then recompute all
 *    paths in a single pass. This trades a small amount of extra work (recomputing paths
 *    that might have been correct) for correctness and simplicity.
 *
 * 3. Periodic progress updates: We update the indexedFileCount counter every 10,000 files
 *    rather than every file. This balances UI responsiveness (users see progress) with
 *    performance (avoiding atomic write overhead on every insertion). The 10,000 threshold
 *    is a reasonable compromise for typical volumes.
 *
 * 4. Background execution: Running in a background thread keeps the UI responsive, but
 *    means the index isn't immediately available for searches. The UI must wait for
 *    population to complete or rely on UsnMonitor's indexed file count before performing
 *    searches. This is acceptable because initial enumeration is fast (1-5 seconds) and
 *    users typically don't need to search immediately on startup.
 *
 * 5. Exclusive locking during population: Using unique_lock (exclusive write lock) during
 *    the entire population phase prevents concurrent searches, but this is necessary to
 *    ensure the index is in a consistent state. Note that during initial population, USN
 *    monitoring has not yet started (it begins after population completes), so there are no
 *    concurrent writers at this stage. The alternative would be to allow concurrent reads
 *    during population, but this would require more complex synchronization and could lead
 *    to incomplete or inconsistent search results.
 */

// Note: The indexed file count is now managed by UsnMonitor class.
// Access it via UsnMonitor::GetIndexedFileCount() instead of a global variable.

// Populates the FileIndex with all existing files on the volume
// by enumerating the MFT using FSCTL_ENUM_USN_DATA.
// Returns true on success, false on failure.
// indexed_file_count: Optional, caller-owned pointer to atomic counter for progress updates.
//   Must outlive the call; may be nullptr. Caller (e.g. UsnMonitor) owns the atomic.
// CODE QUALITY FIX #5: Implementation moved to .cpp file to reduce compile time
bool PopulateInitialIndex(HANDLE volume_handle, FileIndex &file_index,
                          std::atomic<size_t>* indexed_file_count = nullptr);
