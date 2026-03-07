# Linux File System Monitoring Research

## Overview

This document researches Linux equivalents to Windows USN (Update Sequence Number) Journal monitoring for real-time file system change detection. The goal is to identify viable options for implementing volume/folder monitoring on Linux similar to the Windows `UsnMonitor` implementation.

## Windows USN Journal Capabilities

The current Windows implementation (`UsnMonitor`) provides:

1. **Real-time monitoring** of file system changes via kernel-level USN Journal
2. **Event types tracked:**
   - File creation (`USN_REASON_FILE_CREATE`)
   - File deletion (`USN_REASON_FILE_DELETE`)
   - File rename (`USN_REASON_RENAME_OLD_NAME`, `USN_REASON_RENAME_NEW_NAME`)
   - File modification (`USN_REASON_DATA_EXTEND`, `USN_REASON_DATA_TRUNCATION`, `USN_REASON_DATA_OVERWRITE`)
   - File close (`USN_REASON_CLOSE`)
3. **Volume-level monitoring** - monitors entire volumes (e.g., `C:\`)
4. **Kernel-level filtering** - reduces data transfer by filtering at the kernel
5. **Batching** - efficient batching of events (8KB buffers)
6. **Two-thread architecture** - reader thread (I/O) + processor thread (parsing/updates)
7. **No recursive directory setup** - kernel handles all subdirectories automatically

## Linux Monitoring Options

### 1. inotify (Recommended for Most Use Cases)

**What it is:**
- Linux kernel subsystem (since kernel 2.6.13)
- Provides file system event notifications to user-space applications
- Most commonly used Linux file monitoring API

**Capabilities:**
- ✅ Real-time event notifications
- ✅ Event types: `IN_CREATE`, `IN_DELETE`, `IN_MODIFY`, `IN_MOVED_FROM`, `IN_MOVED_TO`, `IN_CLOSE_WRITE`, etc.
- ✅ Directory-level monitoring (can watch directories recursively)
- ✅ Efficient kernel-to-user-space communication
- ✅ Widely supported and stable

**Limitations:**
- ⚠️ **Watch descriptor limits** - Each directory requires a watch descriptor
  - Default limit: `/proc/sys/fs/inotify/max_user_watches` (typically 8192-65536)
  - For large directory trees, may need to increase: `echo 524288 | sudo tee /proc/sys/fs/inotify/max_user_watches`
  - Can be configured per-system or per-user
- ⚠️ **Manual recursive setup** - Must add watches for each subdirectory manually
  - When a new subdirectory is created, must add a watch for it
  - Requires maintaining a directory tree structure
- ⚠️ **No volume-level abstraction** - Must monitor specific directories
- ⚠️ **Event batching** - Events are delivered individually, not batched by kernel
  - Application must implement batching if needed
- ⚠️ **Race conditions** - Events may arrive out of order or miss events during high activity

**API Overview:**
```cpp
#include <sys/inotify.h>

int inotify_fd = inotify_init1(IN_NONBLOCK);
int wd = inotify_add_watch(inotify_fd, "/path/to/dir", 
    IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO);
// Read events from inotify_fd using read() or epoll/select
```

**Implementation Complexity:** Medium
- Need to maintain directory tree and add watches for new subdirectories
- Need to handle watch descriptor exhaustion
- Need to implement event batching if desired

**Performance:** Good
- Low overhead for moderate directory trees
- May struggle with very large directory trees (thousands of directories)

---

### 2. fanotify (Advanced, More Powerful)

**What it is:**
- Linux kernel subsystem (since kernel 2.6.37)
- More powerful than inotify, designed for security and access control
- Can monitor entire mount points (volumes)

**Capabilities:**
- ✅ **Volume-level monitoring** - Can monitor entire mount points (similar to USN Journal)
- ✅ Real-time event notifications
- ✅ More event types than inotify
- ✅ Can intercept file access (not just notifications)
- ✅ Better for security monitoring

**Limitations:**
- ⚠️ **Requires root privileges** - Must run as root or have `CAP_SYS_ADMIN` capability
  - This is a significant limitation for user-space applications
- ⚠️ **More complex API** - More difficult to use than inotify
- ⚠️ **Performance overhead** - Higher overhead than inotify (designed for security, not performance)
- ⚠️ **Less commonly used** - Fewer examples and less documentation

**API Overview:**
```cpp
#include <sys/fanotify.h>

int fanotify_fd = fanotify_init(FAN_CLOEXEC | FAN_CLASS_CONTENT | FAN_NONBLOCK,
                                 O_RDONLY | O_LARGEFILE);
fanotify_mark(fanotify_fd, FAN_MARK_ADD | FAN_MARK_MOUNT,
              FAN_ACCESS | FAN_MODIFY | FAN_CLOSE_WRITE | FAN_OPEN,
              AT_FDCWD, "/mnt/volume");
```

**Implementation Complexity:** High
- Requires privilege escalation
- More complex API
- Less documentation/examples

**Performance:** Moderate
- Higher overhead than inotify
- Better for volume-level monitoring

**Recommendation:** Not recommended for this use case unless volume-level monitoring is critical and root privileges are acceptable.

---

### 3. epoll + inotify (Optimized Approach)

**What it is:**
- Combination of `epoll` (Linux's efficient I/O event notification) with `inotify`
- Allows efficient handling of multiple inotify file descriptors
- Better performance for large-scale monitoring

**Benefits:**
- ✅ Efficient event handling for multiple directories
- ✅ Scales better than `select()` or `poll()`
- ✅ Lower CPU usage for high event rates

**Use Case:** When monitoring many directories simultaneously

---

### 4. Third-Party Libraries

#### Watchman (Facebook)
- **Pros:** Handles recursive watching, efficient, production-tested
- **Cons:** External dependency, requires separate daemon process
- **Use Case:** If willing to add external dependency

#### fswatch (Cross-platform)
- **Pros:** Cross-platform, multiple backends
- **Cons:** External dependency, may not be as efficient as native APIs
- **Use Case:** If cross-platform consistency is more important than performance

---

## Comparison: Windows USN vs Linux inotify

| Feature | Windows USN Journal | Linux inotify |
|---------|-------------------|---------------|
| **Volume-level monitoring** | ✅ Yes (entire volume) | ❌ No (directory-level) |
| **Recursive monitoring** | ✅ Automatic (kernel handles) | ⚠️ Manual (must add watches per directory) |
| **Kernel-level filtering** | ✅ Yes (ReasonMask) | ⚠️ Partial (event mask, but less granular) |
| **Event batching** | ✅ Yes (kernel batches) | ❌ No (events delivered individually) |
| **Watch limits** | ✅ No practical limit | ⚠️ Limited (default ~8K-65K, configurable) |
| **Privileges required** | ⚠️ Requires admin (volume handle) | ✅ User-level (no special privileges) |
| **Performance** | ✅ Excellent (kernel-level) | ✅ Good (kernel-to-user-space) |
| **Complexity** | ✅ Low (kernel handles recursion) | ⚠️ Medium (must manage directory tree) |

---

## Recommended Approach for Linux Implementation

### Option 1: inotify with Recursive Directory Management (Recommended)

**Architecture:**
1. **Initial Population:** Scan directory tree and add watches for all directories
2. **Event Processing:** 
   - Process `IN_CREATE` events for new directories → add watch
   - Process `IN_DELETE` events for directories → remove watch
   - Process file events → update FileIndex
3. **Watch Management:** Maintain a map of directory path → watch descriptor
4. **Watch Limit Handling:** Monitor watch count and handle exhaustion gracefully

**Implementation Pattern:**
```cpp
class InotifyMonitor {
  // Similar interface to UsnMonitor
  void Start();
  void Stop();
  
private:
  void AddWatchRecursive(const std::string& path);
  void RemoveWatch(int wd);
  void ProcessEvents();
  void HandleDirectoryCreated(const std::string& path);
  
  int inotify_fd_;
  std::unordered_map<int, std::string> wd_to_path_;
  std::unordered_map<std::string, int> path_to_wd_;
  std::atomic<size_t> watch_count_{0};
};
```

**Pros:**
- ✅ No special privileges required
- ✅ Well-documented and widely used
- ✅ Good performance for typical use cases
- ✅ Can monitor specific directories (more flexible than volume-level)

**Cons:**
- ⚠️ Must manage directory tree manually
- ⚠️ Watch descriptor limits may be an issue for very large trees
- ⚠️ Must handle race conditions (directory created between scan and watch)

**Estimated Effort:** 8-12 hours (matches existing plan)

---

### Option 2: fanotify for Volume-Level Monitoring

**Use Case:** Only if volume-level monitoring is critical and root privileges are acceptable.

**Pros:**
- ✅ Volume-level monitoring (closer to USN Journal)
- ✅ No recursive directory management needed

**Cons:**
- ❌ Requires root privileges (major limitation)
- ❌ More complex API
- ❌ Higher overhead

**Recommendation:** Not recommended unless volume-level monitoring is absolutely required.

---

## Implementation Considerations

### 1. Watch Descriptor Limits

**Problem:** Default limit may be too low for large directory trees.

**Solution:**
- Check limit: `cat /proc/sys/fs/inotify/max_user_watches`
- Increase if needed: `echo 524288 | sudo tee /proc/sys/fs/inotify/max_user_watches`
- Document requirement in build/install instructions
- Handle `ENOSPC` errors gracefully (watch limit exceeded)

### 2. Recursive Directory Management

**Problem:** Must add watches for all subdirectories and handle new directories.

**Solution:**
- Initial scan: Recursively add watches for all directories
- Event handling: On `IN_CREATE` with `IN_ISDIR`, add watch for new directory
- Cleanup: On `IN_DELETE` with `IN_ISDIR`, remove watch
- Maintain directory tree structure in memory

### 3. Event Batching

**Problem:** inotify delivers events individually, not batched.

**Solution:**
- Use `read()` to read multiple events in one call (events are packed in buffer)
- Implement application-level batching (collect events, process in batches)
- Similar to Windows implementation's queue-based approach

### 4. Race Conditions

**Problem:** Events may arrive out of order or miss events during high activity.

**Solution:**
- Use event queue (similar to Windows `UsnJournalQueue`)
- Process events in order
- Handle missing events gracefully (periodic validation scan)

### 5. Performance Optimization

**Strategies:**
- Use `epoll` for efficient event handling
- Batch event processing
- Filter events early (similar to Windows ReasonMask)
- Yield to UI thread periodically (similar to Windows implementation)

---

## Code Structure Recommendation

Based on the existing Windows `UsnMonitor` architecture:

```
src/usn/
  ├── UsnMonitor.h          (Windows implementation)
  ├── UsnMonitor.cpp        (Windows implementation)
  ├── InotifyMonitor.h      (Linux implementation - NEW)
  ├── InotifyMonitor.cpp    (Linux implementation - NEW)
  └── FileSystemMonitor.h   (Optional: Platform-agnostic interface)
```

**Interface Compatibility:**
- `InotifyMonitor` should match `UsnMonitor` interface where possible
- Same methods: `Start()`, `Stop()`, `IsActive()`, `GetQueueSize()`, etc.
- Similar metrics structure for consistency

---

## Testing Considerations

1. **Watch Limit Testing:** Test with directory trees exceeding default watch limits
2. **High Event Rate:** Test with rapid file creation/deletion
3. **Deep Directory Trees:** Test with very deep directory structures
4. **Concurrent Access:** Test with multiple processes accessing files
5. **Mount Point Changes:** Test behavior when directories are unmounted/remounted

---

## References

- [inotify(7) man page](https://man7.org/linux/man-pages/man7/inotify.7.html)
- [fanotify(7) man page](https://man7.org/linux/man-pages/man7/fanotify.7.html)
- [inotify-tools](https://github.com/rvoicilas/inotify-tools)
- [Watchman](https://github.com/facebook/watchman)
- [fswatch](https://github.com/emcrisostomo/fswatch)
- [Linux inotify Wikipedia](https://en.wikipedia.org/wiki/Inotify)

---

## Conclusion

**Recommended Solution:** Use **inotify** with recursive directory management for Linux file system monitoring.

**Rationale:**
1. ✅ No special privileges required (unlike fanotify)
2. ✅ Well-documented and widely used
3. ✅ Good performance for typical use cases
4. ✅ Flexible (can monitor specific directories, not just volumes)
5. ✅ Matches existing project plan (Phase 4 in `LINUX_CROSS_PLATFORM_PLAN.md`)

**Key Differences from Windows USN:**
- Must manage directory tree manually (add/remove watches)
- Watch descriptor limits may require configuration
- Events delivered individually (must implement batching)
- Directory-level, not volume-level (but more flexible)

**Implementation Effort:** 8-12 hours (as estimated in existing plan)

**Next Steps:**
1. Review this research document
2. Decide on implementation approach (inotify recommended)
3. Create `InotifyMonitor` class with similar interface to `UsnMonitor`
4. Implement recursive directory watch management
5. Add watch limit handling and error recovery
6. Test with various directory tree sizes and event rates
