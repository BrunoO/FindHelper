# USN Journal Data Loss Analysis & Optimization

## The Problem: Circular Buffer Overflow
The USN Journal is a **circular buffer** with a fixed size. If the application reads entries slower than the file system generates them (e.g., during massive file operations like deleting thousands of files), the journal will "wrap around," and older unread entries will be overwritten.

The current implementation detects this via `ERROR_JOURNAL_ENTRY_DELETED` and skips to the current position, which prevents crashing but confirms that **events have been lost**.

## Mitigation Strategies
1.  **Increase Journal Size**: Programmatically request a larger USN Journal size from Windows.
2.  **Optimize the Reader**: Decouple reading from processing to ensure the reader keeps up with the kernel.

## Proposed Architecture: Producer-Consumer Pattern

To optimize the reader, we should decouple **reading** (must be fast) from **processing** (can be slow).

### 1. The Producer (Reader Thread)
*   **Sole Responsibility:** Read raw data from the USN Journal as fast as possible.
*   **Action:** Calls `DeviceIoControl` to get a chunk of data.
*   **Output:** Pushes the *raw binary buffer* (e.g., `std::vector<char>`) into a thread-safe queue.
*   **Behavior:** Immediately loops back to read the next chunk. It does **zero** parsing and **zero** indexing.

### 2. The Queue
*   A thread-safe FIFO (First-In-First-Out) queue.
*   Stores `std::vector<char>` (raw memory blocks).
*   Protected by a `std::mutex` and a `std::condition_variable`.

### 3. The Consumer (Worker Thread)
*   **Sole Responsibility:** Parse the raw data and update the `FileIndex`.
*   **Action:** Pops a buffer from the queue.
*   **Processing:** Iterates through the USN records, converts strings, and calls `g_fileIndex.Insert/Remove/etc`.

## Impact Analysis

### 1. Reliability (Positive)
*   **No Missed Events:** The Reader thread will almost never be blocked by CPU-intensive tasks. It can drain the kernel's USN buffer much faster than the file system can fill it.

### 2. Memory Usage (Negative)
*   **Potential Spikes:** If a massive folder is deleted, the Reader will push buffers faster than the Consumer can process them.
*   **Risk:** The queue grows in memory. We are trading "lost events" for "increased memory usage."
*   **Mitigation:** Implement a "max queue size" (e.g., 500MB). If hit, decide whether to block the reader (risk overflow) or drop events.

### 3. UI Responsiveness & Locking (Nuanced)
*   **Current State:** The single thread pauses for I/O, giving the UI time to acquire the `FileIndex` lock.
*   **New State Risk:** The Consumer thread does *not* do I/O. With a large backlog, it might run a tight loop updating the index. Since `FileIndex::Insert` takes a **unique lock**, a fast Consumer could "starve" the UI search queries.
*   **Mitigation:** The Consumer should be "polite"—processing a batch of events and then yielding (sleeping for 1ms) to let UI readers acquire the lock.

### 4. Complexity (Negative)
*   Requires managing two threads.
*   Graceful shutdown requires signaling the Reader to stop, waiting for it, then signaling the Consumer to finish the queue, and waiting for it.
