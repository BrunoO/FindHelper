# macOS File Attributes Implementation Plan

## Overview

This document outlines the plan to implement file size and modification date retrieval for macOS, enabling the macOS application to display file sizes and modification times in search results, matching Windows functionality.

---

## Current State

### Windows Implementation
- **File**: `FileSystemUtils.h`
- **Function**: `GetFileAttributes(std::string_view path)`
- **Implementation**:
  1. Uses `GetFileAttributesExW()` (fast, single system call)
  2. Falls back to `IShellItem2` for cloud files (OneDrive, etc.)
  3. Returns both size and modification time in one call
  4. Returns `FILETIME` structure for modification time

### macOS Implementation (Current)
- **Status**: Stub implementation (returns sentinel values)
- **Location**: `FileSystemUtils.h` line 75-78
- **Current Code**:
  ```cpp
  #else
    // Test stub: return sentinel values (tests don't exercise file I/O)
    (void)path;  // Suppress unused parameter warning
  #endif
  ```

### Impact
- **macOS users cannot see file sizes** in search results
- **macOS users cannot see modification dates** in search results
- **Time filters may not work correctly** on macOS (depends on modification time)

---

## Implementation Plan

### Phase 1: Core File Attributes Retrieval

#### 1.1 Implement `GetFileAttributes()` for macOS

**File**: `FileSystemUtils.h`

**macOS API Choice**: `stat()` / `stat64()`

**Rationale**:
- POSIX standard, available on all Unix-like systems
- Single system call returns both size and modification time
- Efficient (no Objective-C overhead)
- Works with all file types (regular files, directories, symlinks)
- Handles UTF-8 paths correctly

**Implementation Approach**:
```cpp
#elif defined(__APPLE__) || defined(__unix__)
  // macOS/Unix implementation using stat()
  struct stat file_stat;
  
  // Convert path to C string (stat requires null-terminated string)
  std::string path_str(path);
  
  if (stat(path_str.c_str(), &file_stat) == 0) {
    // Success: extract size and modification time
    result.fileSize = static_cast<uint64_t>(file_stat.st_size);
    
    // Convert timespec to FILETIME
    result.lastModificationTime = TimespecToFileTime(file_stat.st_mtime);
    result.success = true;
  } else {
    // Failure: return sentinel values
    result.fileSize = 0;
    result.lastModificationTime = kFileTimeFailed;
    result.success = false;
  }
#else
  // Other platforms: stub
  (void)path;
#endif
```

**Key Functions Needed**:
1. `TimespecToFileTime()` - Convert `struct timespec` to `FILETIME`
2. Error handling for file access errors

#### 1.2 Implement Time Conversion Functions

**File**: `FileSystemUtils.h` (or new `FileTimeConversion.h`)

**Function**: `TimespecToFileTime(struct timespec ts) -> FILETIME`

**Conversion Details**:
- **macOS/Unix**: `timespec` uses seconds + nanoseconds since Unix epoch (1970-01-01 00:00:00 UTC)
- **Windows FILETIME**: Uses 100-nanosecond intervals since Windows epoch (1601-01-01 00:00:00 UTC)
- **Epoch difference**: 11644473600 seconds (between 1601 and 1970)

**Conversion Formula**:
```
FILETIME = (Unix timestamp + epoch_difference) * 10,000,000 + nanoseconds / 100
```

**Implementation**:
```cpp
inline FILETIME TimespecToFileTime(const struct timespec &ts) {
  // Windows epoch: 1601-01-01 00:00:00 UTC
  // Unix epoch: 1970-01-01 00:00:00 UTC
  // Difference: 11644473600 seconds
  const int64_t EPOCH_DIFF_SECONDS = 11644473600LL;
  
  // Convert seconds to 100-nanosecond intervals
  int64_t total_100ns = (static_cast<int64_t>(ts.tv_sec) + EPOCH_DIFF_SECONDS) * 10000000LL;
  
  // Add nanoseconds (convert to 100-nanosecond intervals)
  total_100ns += ts.tv_nsec / 100;
  
  // Split into high and low parts (FILETIME structure)
  FILETIME ft;
  ft.dwLowDateTime = static_cast<uint32_t>(total_100ns & 0xFFFFFFFF);
  ft.dwHighDateTime = static_cast<uint32_t>((total_100ns >> 32) & 0xFFFFFFFF);
  
  return ft;
}
```

**Edge Cases**:
- Handle negative timestamps (files before 1970 - rare but possible)
- Handle very large timestamps (future dates)
- Handle invalid `timespec` values

#### 1.3 Implement `FormatFileTime()` for macOS

**File**: `FileSystemUtils.h`

**Current State**: Stub returns empty string on non-Windows

**macOS Implementation**:
- Convert `FILETIME` to `struct tm` (local time)
- Format as "YYYY-MM-DD HH:MM"

**Implementation Approach**:
```cpp
#elif defined(__APPLE__) || defined(__unix__)
  // macOS/Unix implementation
  // Convert FILETIME to Unix timestamp
  struct timespec ts = FileTimeToTimespec(ft);
  
  // Convert to local time
  struct tm *timeinfo = localtime(&ts.tv_sec);
  if (!timeinfo) {
    return "";  // Invalid time
  }
  
  // Format as "YYYY-MM-DD HH:MM"
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d",
                timeinfo->tm_year + 1900,  // tm_year is years since 1900
                timeinfo->tm_mon + 1,      // tm_mon is 0-11
                timeinfo->tm_mday,
                timeinfo->tm_hour,
                timeinfo->tm_min);
  return buffer;
#else
  // Other platforms: stub
  (void)ft;
  return "";
#endif
```

**Helper Function Needed**:
- `FileTimeToTimespec(FILETIME ft) -> struct timespec` (reverse conversion)

---

### Phase 2: Error Handling & Edge Cases

#### 2.1 File Access Errors

**Common Errors**:
- `ENOENT`: File does not exist
- `EACCES`: Permission denied
- `ENOTDIR`: Path component is not a directory
- `ENAMETOOLONG`: Path too long
- `ELOOP`: Too many symbolic links

**Handling Strategy**:
- Return `kFileTimeFailed` for modification time
- Return `0` for file size (or `kFileSizeFailed` if we want to distinguish)
- Set `success = false`
- Optionally log errors in debug builds

#### 2.2 Special File Types

**Symlinks**:
- `stat()` follows symlinks by default (returns target file attributes)
- Use `lstat()` if we want symlink attributes instead
- **Decision**: Follow symlinks (match Windows behavior)

**Directories**:
- `stat()` returns directory size (usually 0 or small value)
- Modification time is valid
- **Decision**: Return directory size as 0 (match Windows behavior)

**Special Files**:
- Device files, FIFOs, sockets
- `stat()` works but size may be 0
- **Decision**: Return actual size from `stat()` (may be 0)

#### 2.3 Large Files

**macOS Support**:
- `stat64()` for files > 2GB (if needed)
- `st_size` is `off_t` (64-bit on macOS)
- **Decision**: Use `stat()` (64-bit on macOS by default)

---

### Phase 3: Testing & Validation

#### 3.1 Unit Tests

**Test Cases**:
1. Regular file (size > 0, valid modification time)
2. Empty file (size = 0)
3. Large file (> 1GB)
4. Directory (size = 0)
5. Symlink (follows to target)
6. Non-existent file (error handling)
7. Permission denied (error handling)
8. Files with special characters in path
9. Files with very long paths
10. Files with modification times in different timezones
11. Files with modification times before 1970 (edge case)
12. Files with modification times far in the future

**Test File**: `tests/FileSystemUtilsTests.cpp` (extend existing or create new)

#### 3.2 Integration Tests

**Test Cases**:
1. Load file size in UI (verify display)
2. Load modification time in UI (verify display)
3. Sort by size (verify correct ordering)
4. Sort by modification time (verify correct ordering)
5. Time filter (Today, This Week, etc.) - verify correct filtering

#### 3.3 Cross-Platform Validation

**Validation Steps**:
1. Generate index file on Windows
2. Load index file on macOS
3. Verify file sizes match (for same files)
4. Verify modification times match (accounting for timezone)
5. Verify time filters work correctly

---

## Implementation Details

### File Structure

**Files to Modify**:
1. `FileSystemUtils.h` - Add macOS implementation to `GetFileAttributes()`, `FormatFileTime()`
2. `FileSystemUtils.h` - Add time conversion helper functions

**New Helper Functions**:
- `TimespecToFileTime(const struct timespec &ts) -> FILETIME`
- `FileTimeToTimespec(const FILETIME &ft) -> struct timespec`

### Dependencies

**Required Headers**:
```cpp
#include <sys/stat.h>    // For stat()
#include <time.h>        // For struct timespec, localtime()
#include <errno.h>       // For error codes (optional, for better error handling)
```

**No Additional Libraries**: Uses standard POSIX APIs

### Performance Considerations

**Optimization Opportunities**:
1. **Single System Call**: `stat()` returns both size and modification time (already optimal)
2. **No String Conversion Overhead**: `stat()` accepts C strings directly
3. **Cache-Friendly**: Results are cached in `FileIndex` (lazy loading)

**Expected Performance**:
- `stat()` call: ~1-5 microseconds (local files)
- Time conversion: < 1 microsecond (pure arithmetic)
- **Total**: Comparable to Windows `GetFileAttributesExW()`

### Error Handling Strategy

**Error Return Values**:
- **File Size**: Return `0` on error (indistinguishable from empty file, but acceptable)
- **Modification Time**: Return `kFileTimeFailed` on error
- **Success Flag**: Set `success = false` on any error

**Error Logging**:
- Log errors in debug builds only (`LOG_ERROR_BUILD`)
- Use `errno` to determine error type (optional, for better diagnostics)

---

## Implementation Steps

### Step 1: Add Time Conversion Functions

**File**: `FileSystemUtils.h`

**Tasks**:
1. Add `#include <sys/stat.h>` and `#include <time.h>`
2. Implement `TimespecToFileTime()` helper function
3. Implement `FileTimeToTimespec()` helper function
4. Add unit tests for conversion functions

**Estimated Time**: 1-2 hours

### Step 2: Implement `GetFileAttributes()` for macOS

**File**: `FileSystemUtils.h`

**Tasks**:
1. Replace stub implementation with `stat()` call
2. Extract file size from `st_size`
3. Convert modification time using `TimespecToFileTime()`
4. Add error handling
5. Test with various file types

**Estimated Time**: 2-3 hours

### Step 3: Implement `FormatFileTime()` for macOS

**File**: `FileSystemUtils.h`

**Tasks**:
1. Replace stub implementation with time formatting
2. Convert `FILETIME` to `struct timespec`
3. Convert to local time using `localtime()`
4. Format as "YYYY-MM-DD HH:MM"
5. Test with various dates and timezones

**Estimated Time**: 1-2 hours

### Step 4: Testing & Validation

**Tasks**:
1. Create unit tests for all functions
2. Test with various file types and edge cases
3. Test cross-platform consistency (Windows vs macOS)
4. Integration testing in UI
5. Performance testing

**Estimated Time**: 3-4 hours

### Step 5: Documentation & Code Review

**Tasks**:
1. Add function documentation (Doxygen comments)
2. Document edge cases and error handling
3. Update cross-platform documentation
4. Code review for correctness and style

**Estimated Time**: 1-2 hours

---

## Total Estimated Time

**Total**: 8-13 hours

**Breakdown**:
- Implementation: 4-7 hours
- Testing: 3-4 hours
- Documentation: 1-2 hours

---

## Success Criteria

### Functional Requirements
- ✅ File sizes display correctly in macOS search results
- ✅ Modification dates display correctly in macOS search results
- ✅ Time filters work correctly on macOS
- ✅ Sorting by size works correctly
- ✅ Sorting by modification time works correctly
- ✅ Error handling works for non-existent files, permission errors, etc.

### Performance Requirements
- ✅ File attribute retrieval is fast (< 10 microseconds for local files)
- ✅ No noticeable UI lag when loading file attributes
- ✅ Lazy loading works correctly (only loads visible rows)

### Cross-Platform Requirements
- ✅ File sizes match between Windows and macOS (for same files)
- ✅ Modification times match (accounting for timezone differences)
- ✅ Time filters produce consistent results across platforms

### Code Quality Requirements
- ✅ Follows existing code style and conventions
- ✅ Proper error handling and edge case coverage
- ✅ Comprehensive unit tests
- ✅ Documentation complete

---

## Alternative Approaches Considered

### Option 1: NSFileManager (Objective-C)

**Pros**:
- Native macOS API
- Better integration with macOS file system features
- Can handle resource forks, extended attributes

**Cons**:
- Requires Objective-C++ compilation
- More overhead than POSIX
- More complex error handling
- Not needed for basic file attributes

**Decision**: ❌ Not chosen - `stat()` is simpler and sufficient

### Option 2: std::filesystem (C++17)

**Pros**:
- Standard C++ library
- Cross-platform (same API on Windows and macOS)
- Modern C++ style

**Cons**:
- Requires C++17 (project uses C++17, so this is fine)
- May have performance overhead
- Less control over error handling
- `last_write_time()` returns `std::filesystem::file_time_type` (needs conversion)

**Decision**: ⚠️ Considered but not chosen - `stat()` is more direct and efficient

### Option 3: Hybrid Approach

**Use `stat()` for basic attributes, `NSFileManager` for advanced features**

**Decision**: ✅ Chosen - Start with `stat()`, can add `NSFileManager` later if needed

---

## Future Enhancements

### Potential Improvements
1. **Extended Attributes**: Use `NSFileManager` to retrieve extended attributes (tags, etc.)
2. **Resource Forks**: Handle macOS resource forks (if needed)
3. **Performance Monitoring**: Add performance metrics for file attribute retrieval
4. **Caching Strategy**: Consider caching file attributes more aggressively
5. **Batch Operations**: Load multiple file attributes in parallel (if performance becomes an issue)

---

## References

- [POSIX stat() man page](https://man7.org/linux/man-pages/man2/stat.2.html)
- [FILETIME structure (Windows)](https://docs.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-filetime)
- [timespec structure (POSIX)](https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/time.h.html)
- [Epoch conversion reference](https://en.wikipedia.org/wiki/File_time)

---

## Notes

- **Timezone Handling**: macOS uses system timezone, Windows uses system timezone. Both should match for same files (assuming same system timezone setting).
- **Precision**: FILETIME has 100-nanosecond precision, `timespec` has nanosecond precision. We truncate to 100-nanosecond precision (acceptable loss).
- **Symlinks**: `stat()` follows symlinks by default. This matches Windows behavior (GetFileAttributesEx follows symlinks).

