# Implementation Plan: Folder Crawler

## Overview

Create a cross-platform file system crawler that recursively enumerates files and directories from a user-specified root folder. This crawler will be used:
- **Windows**: When admin rights are not available (fallback from USN Journal)
- **macOS**: When no index file is provided (primary method)

## Architecture Decisions

### 1. Single Cross-Platform Implementation
- One implementation using platform-specific directory enumeration APIs
- Conditional compilation for platform-specific code (`#ifdef _WIN32` / `#ifdef __APPLE__`)

### 2. Threading Model: Work-Stealing Queue
Based on research, WinDirStat is single-threaded, so we'll implement a modern multi-threaded approach:
- **Work Queue**: Thread-safe queue of directory paths to process
- **Thread Pool**: Configurable number of worker threads (default: `hardware_concurrency()`)
- **Work-Stealing**: Each thread processes directories from the queue; when a thread discovers subdirectories, it adds them to the queue for other threads to process
- **Load Balancing**: Natural load balancing as threads grab work from the shared queue

### 3. FileIndex Integration
- Use `FileIndex::insert_path(const std::string& full_path)` (same as file-based loading)
- **Batch Insertions**: Collect paths in a vector, flush periodically (every N paths or every M milliseconds) to reduce lock contention
- Batch size: Start with 1000 paths per batch (tunable)

### 4. Error Handling
- **Skip and Log**: Continue on permission errors, log warnings
- **Skip Symlinks**: Skip symbolic links and junction points (documented for future changes)
- **Error Tolerance**: Continue processing even if individual files/directories fail

### 5. Performance Assessment
- **Fast APIs**: Use platform-specific fast enumeration APIs
  - Windows: `FindFirstFileEx` with `FIND_FIRST_EX_LARGE_FETCH` flag
  - macOS: `readdir` with `opendir` (standard but fast)
- **Benchmark**: Test with ~1M files to compare fast API vs standard C++ filesystem
- **No Metadata**: Don't collect file size/modification time (consistent with current approach, documented for future)

### 6. User Interface
- **Root Path Dialog**: Show native folder picker dialog
  - Windows: `IFileDialog` with `FOS_PICKFOLDERS` flag
  - macOS: `NSOpenPanel` with `canChooseDirectories = YES`
- **Progress Reporting**: Update atomic counter (like `InitialIndexPopulator`)
- **Progress Updates**: Every 10,000 files (same as `InitialIndexPopulator`)

---

## File Structure

```
FolderCrawler.h          - Main interface and class declaration
FolderCrawler.cpp        - Cross-platform implementation
FolderCrawler_win.cpp    - Windows-specific directory enumeration
FolderCrawler_mac.mm     - macOS-specific directory enumeration
```

---

## API Design

### Header Interface

```cpp
// FolderCrawler.h
#pragma once

#include <atomic>
#include <string>
#include <vector>

class FileIndex;

/**
 * FolderCrawler - Cross-platform file system crawler for initial index population
 *
 * This class provides recursive directory traversal for populating FileIndex
 * when USN Journal is unavailable (Windows non-admin) or not available (macOS).
 *
 * THREADING MODEL:
 * - Uses a work-stealing queue pattern with multiple worker threads
 * - Each thread processes directories from a shared queue
 * - When a thread discovers subdirectories, it adds them to the queue
 * - Natural load balancing as threads grab work from the queue
 *
 * PERFORMANCE:
 * - Uses platform-specific fast enumeration APIs
 * - Batches insertions to reduce FileIndex lock contention
 * - Skips symbolic links and junction points (documented for future changes)
 *
 * ERROR HANDLING:
 * - Continues on permission errors (skips and logs)
 * - Tolerates individual file/directory failures
 */
class FolderCrawler {
public:
  struct Config {
    size_t thread_count = 0;  // 0 = auto (hardware_concurrency())
    size_t batch_size = 1000;  // Paths per batch insertion
    size_t progress_update_interval = 10000;  // Update progress every N files
  };

  FolderCrawler(FileIndex& file_index, const Config& config = Config());
  ~FolderCrawler();

  // Non-copyable, non-movable
  FolderCrawler(const FolderCrawler&) = delete;
  FolderCrawler& operator=(const FolderCrawler&) = delete;
  FolderCrawler(FolderCrawler&&) = delete;
  FolderCrawler& operator=(FolderCrawler&&) = delete;

  /**
   * Crawl a root directory and populate the FileIndex
   * 
   * @param root_path Root directory to start crawling from
   * @param indexed_file_count Optional atomic counter for progress updates
   * @param cancel_flag Optional atomic flag to cancel crawling
   * @return true on success, false on failure
   */
  bool Crawl(const std::string& root_path,
             std::atomic<size_t>* indexed_file_count = nullptr,
             const std::atomic<bool>* cancel_flag = nullptr);

  /**
   * Get the number of files processed so far (thread-safe)
   */
  size_t GetFilesProcessed() const { return files_processed_.load(); }

  /**
   * Get the number of directories processed so far (thread-safe)
   */
  size_t GetDirectoriesProcessed() const { return dirs_processed_.load(); }

  /**
   * Get the number of errors encountered (thread-safe)
   */
  size_t GetErrorCount() const { return error_count_.load(); }

private:
  // Platform-specific directory enumeration
  struct DirectoryEntry {
    std::string name;
    bool is_directory;
    bool is_symlink;  // For skipping symlinks
  };

  // Enumerate entries in a directory (platform-specific)
  bool EnumerateDirectory(const std::string& dir_path,
                          std::vector<DirectoryEntry>& entries);

  // Worker thread function
  void WorkerThread(std::atomic<size_t>* indexed_file_count,
                    const std::atomic<bool>* cancel_flag);

  // Batch insertion helper
  void FlushBatch(std::vector<std::string>& batch,
                  std::atomic<size_t>* indexed_file_count);

  FileIndex& file_index_;
  Config config_;
  
  // Thread pool
  std::vector<std::thread> worker_threads_;
  std::atomic<bool> should_stop_{false};
  
  // Work queue (thread-safe)
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::queue<std::string> work_queue_;
  std::atomic<size_t> active_workers_{0};
  
  // Statistics
  std::atomic<size_t> files_processed_{0};
  std::atomic<size_t> dirs_processed_{0};
  std::atomic<size_t> error_count_{0};
};
```

---

## Implementation Details

### 1. Windows Directory Enumeration (`FolderCrawler_win.cpp`)

```cpp
bool FolderCrawler::EnumerateDirectory(const std::string& dir_path,
                                     std::vector<DirectoryEntry>& entries) {
  // Use FindFirstFileEx with FIND_FIRST_EX_LARGE_FETCH for performance
  // Skip symlinks and junction points
  // Handle permission errors gracefully
}
```

**Key Points:**
- Use `FindFirstFileEx` with `FIND_FIRST_EX_LARGE_FETCH` flag
- Check `FILE_ATTRIBUTE_REPARSE_POINT` to skip symlinks/junctions
- Handle `ERROR_ACCESS_DENIED` by logging and continuing
- Convert paths to UTF-8 for cross-platform compatibility

### 2. macOS Directory Enumeration (`FolderCrawler_mac.mm`)

```cpp
bool FolderCrawler::EnumerateDirectory(const std::string& dir_path,
                                     std::vector<DirectoryEntry>& entries) {
  // Use readdir/opendir (standard but fast on macOS)
  // Skip symlinks using lstat
  // Handle permission errors gracefully
}
```

**Key Points:**
- Use `opendir` / `readdir` (standard POSIX, fast on macOS)
- Use `lstat` to detect symlinks (skip them)
- Handle `EACCES` by logging and continuing
- Paths are already UTF-8 on macOS

### 3. Work-Stealing Queue Pattern

```cpp
void FolderCrawler::WorkerThread(std::atomic<size_t>* indexed_file_count,
                               const std::atomic<bool>* cancel_flag) {
  std::vector<std::string> batch;
  batch.reserve(config_.batch_size);
  
  while (true) {
    // Check for cancellation
    if (cancel_flag && cancel_flag->load()) {
      break;
    }
    
    // Get work from queue
    std::string dir_path;
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(lock, [this] { 
        return !work_queue_.empty() || should_stop_.load(); 
      });
      
      if (should_stop_.load() && work_queue_.empty()) {
        break;
      }
      
      if (!work_queue_.empty()) {
        dir_path = work_queue_.front();
        work_queue_.pop();
        active_workers_.fetch_add(1);
      }
    }
    
    if (dir_path.empty()) continue;
    
    // Enumerate directory
    std::vector<DirectoryEntry> entries;
    if (!EnumerateDirectory(dir_path, entries)) {
      error_count_.fetch_add(1);
      active_workers_.fetch_sub(1);
      continue;
    }
    
    // Process entries
    for (const auto& entry : entries) {
      if (cancel_flag && cancel_flag->load()) {
        break;
      }
      
      std::string full_path = JoinPath(dir_path, entry.name);
      
      // Skip symlinks
      if (entry.is_symlink) {
        LOG_INFO_BUILD("Skipping symlink: " << full_path);
        continue;
      }
      
      // Add to batch
      batch.push_back(full_path);
      
      // If directory, add to work queue
      if (entry.is_directory) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        work_queue_.push(full_path);
        queue_cv_.notify_one();
        dirs_processed_.fetch_add(1);
      } else {
        files_processed_.fetch_add(1);
      }
      
      // Flush batch if full
      if (batch.size() >= config_.batch_size) {
        FlushBatch(batch, indexed_file_count);
        batch.clear();
      }
    }
    
    active_workers_.fetch_sub(1);
  }
  
  // Flush remaining batch
  if (!batch.empty()) {
    FlushBatch(batch, indexed_file_count);
  }
}
```

### 4. Batch Insertion

```cpp
void FolderCrawler::FlushBatch(std::vector<std::string>& batch,
                              std::atomic<size_t>* indexed_file_count) {
  // Insert all paths in batch
  for (const auto& path : batch) {
    file_index_.insert_path(path);
  }
  
  // Update progress counter
  if (indexed_file_count) {
    size_t total = files_processed_.load() + dirs_processed_.load();
    if (total % config_.progress_update_interval == 0) {
      indexed_file_count->store(total, std::memory_order_relaxed);
      LOG_INFO_BUILD("Crawled " << total << " entries...");
    }
  }
}
```

### 5. Root Path Dialog

#### Windows (`AppBootstrap.cpp` or new utility)

```cpp
// In FileOperations.h or new DialogUtils.h
bool ShowFolderPickerDialog(std::string& out_path);
```

Implementation:
```cpp
bool ShowFolderPickerDialog(std::string& out_path) {
  IFileDialog* pfd = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&pfd));
  if (FAILED(hr)) return false;
  
  DWORD dwOptions;
  hr = pfd->GetOptions(&dwOptions);
  if (FAILED(hr)) { pfd->Release(); return false; }
  
  hr = pfd->SetOptions(dwOptions | FOS_PICKFOLDERS);
  if (FAILED(hr)) { pfd->Release(); return false; }
  
  hr = pfd->Show(NULL);
  if (FAILED(hr)) { pfd->Release(); return false; }
  
  IShellItem* psi = nullptr;
  hr = pfd->GetResult(&psi);
  if (FAILED(hr)) { pfd->Release(); return false; }
  
  PWSTR pszPath = nullptr;
  hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
  if (FAILED(hr)) { psi->Release(); pfd->Release(); return false; }
  
  // Convert to UTF-8
  char buffer[32767];
  int converted = WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, buffer,
                                       sizeof(buffer), nullptr, nullptr);
  CoTaskMemFree(pszPath);
  psi->Release();
  pfd->Release();
  
  if (converted > 0) {
    out_path.assign(buffer);
    return true;
  }
  return false;
}
```

#### macOS (`AppBootstrap_mac.mm` or new utility)

```cpp
bool ShowFolderPickerDialog(std::string& out_path) {
  NSOpenPanel* panel = [NSOpenPanel openPanel];
  [panel setCanChooseFiles:NO];
  [panel setCanChooseDirectories:YES];
  [panel setAllowsMultipleSelection:NO];
  
  if ([panel runModal] == NSModalResponseOK) {
    NSURL* url = [[panel URLs] firstObject];
    if (url) {
      const char* path = [[url path] UTF8String];
      out_path.assign(path);
      return true;
    }
  }
  return false;
}
```

---

## Integration Points

### 1. Windows (`AppBootstrap.cpp`)

Modify the admin check section:

```cpp
// Check for admin privileges
if (!IsProcessElevated()) {
  // Instead of showing elevation prompt, show folder picker
  std::string root_path;
  if (ShowFolderPickerDialog(root_path)) {
    // Use FolderCrawler to crawl
    FolderCrawler crawler(file_index);
    std::atomic<size_t> indexed_count{0};
    if (crawler.Crawl(root_path, &indexed_count)) {
      file_index.FinalizeInitialPopulation();
      LOG_INFO("Index populated via FolderCrawler");
    } else {
      LOG_ERROR("Failed to populate index via FolderCrawler");
      return result; // IsValid() will be false
    }
  } else {
    // User cancelled
    return result;
  }
} else {
  // Existing USN Journal code...
}
```

### 2. macOS (`AppBootstrap_mac.mm`)

Modify the index loading section:

```cpp
// If indexing from file, do that now
if (!cmd_args.index_from_file.empty()) {
  // Existing file loading...
} else {
  // Show folder picker and use FolderCrawler
  std::string root_path;
  if (ShowFolderPickerDialog(root_path)) {
    FolderCrawler crawler(file_index);
    std::atomic<size_t> indexed_count{0};
    if (crawler.Crawl(root_path, &indexed_count)) {
      file_index.FinalizeInitialPopulation();
      LOG_INFO("Index populated via FolderCrawler");
    } else {
      LOG_ERROR("Failed to populate index via FolderCrawler");
      glfwTerminate();
      return result;
    }
  } else {
    // User cancelled
    glfwTerminate();
    return result;
  }
}
```

---

## Performance Benchmarking Plan

### Test Setup
1. Create a test directory structure with ~1M files
2. Measure time for:
   - Fast API (FindFirstFileEx on Windows, readdir on macOS)
   - Standard C++ filesystem API (`std::filesystem::directory_iterator`)
3. Compare results and document findings

### Expected Results
- Fast API should be significantly faster (2-5x) due to:
  - Less overhead (direct OS calls vs. C++ abstraction)
  - Better buffering (FIND_FIRST_EX_LARGE_FETCH on Windows)
  - Less memory allocation

---

## Documentation Requirements

### Code Comments
1. **Symlink Skipping**: Document why we skip symlinks and how to enable following them if needed
2. **Metadata Collection**: Document that we don't collect metadata (consistent with USN approach) and how to add it if needed
3. **Threading Model**: Document the work-stealing queue pattern and rationale
4. **Batch Size**: Document why batch size is 1000 and how to tune it

### User-Facing Documentation
1. Update README to explain when FolderCrawler is used
2. Document performance characteristics
3. Explain the folder picker dialog requirement

---

## Testing Plan

### Unit Tests
1. Test directory enumeration (Windows and macOS)
2. Test symlink detection and skipping
3. Test error handling (permission denied, etc.)
4. Test batch insertion

### Integration Tests
1. Test with small directory tree (< 1000 files)
2. Test with medium directory tree (~100K files)
3. Test with large directory tree (~1M files)
4. Test cancellation
5. Test progress reporting

### Performance Tests
1. Benchmark fast API vs standard API
2. Measure lock contention with different batch sizes
3. Measure scaling with different thread counts

---

## Implementation Phases

### Phase 1: Core Implementation (Week 1)
- [ ] Create FolderCrawler class structure
- [ ] Implement Windows directory enumeration
- [ ] Implement macOS directory enumeration
- [ ] Implement work-stealing queue
- [ ] Implement batch insertion
- [ ] Add error handling and logging

### Phase 2: Integration (Week 2)
- [ ] Create folder picker dialogs (Windows and macOS)
- [ ] Integrate with AppBootstrap (Windows)
- [ ] Integrate with AppBootstrap_mac (macOS)
- [ ] Add progress reporting
- [ ] Test with small directories

### Phase 3: Optimization & Testing (Week 3)
- [ ] Performance benchmarking (fast API vs standard)
- [ ] Tune batch size and thread count
- [ ] Add cancellation support
- [ ] Comprehensive testing
- [ ] Documentation

---

## Future Enhancements (Documented, Not Implemented)

1. **Symlink Following**: Add option to follow symlinks (with cycle detection)
2. **Metadata Collection**: Option to collect file size/modification time during crawl
3. **Resume Support**: Save progress and allow resuming interrupted crawls
4. **Filtering**: Skip certain file patterns during crawl (e.g., node_modules, .git)
5. **Incremental Updates**: Track last crawl time and only process changed files

---

## Notes

- **Symlink Handling**: Currently skipped to avoid infinite loops and redundant processing. To enable following symlinks in the future, add cycle detection (track visited inodes) and a config option.

- **Metadata Collection**: Currently skipped to stay consistent with USN Journal approach (lazy loading). To enable metadata collection, modify `EnumerateDirectory` to also call `stat`/`GetFileAttributes` and pass to a modified `insert_path` that accepts metadata.

- **Thread Count**: Defaults to `hardware_concurrency()` but can be tuned based on I/O vs CPU bound workload. For I/O bound directory enumeration, more threads help keep multiple I/O operations in flight.

- **Batch Size**: 1000 is a starting point. Too small = more lock contention, too large = delayed progress updates and higher memory usage. Tune based on profiling.

