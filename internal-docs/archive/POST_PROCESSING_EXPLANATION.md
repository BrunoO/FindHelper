# Post-Processing Step Explanation

## Overview

The post-processing step converts **file IDs** (returned from parallel search) into **SearchResult objects** (needed by the UI for display).

## The Problem: Data Separation

### What ContiguousStringBuffer Returns
- **Only file IDs** (`uint64_t` values)
- Example: `[12345, 67890, 11111, ...]`
- These are just numbers - no display information

### What the UI Needs
- **SearchResult objects** with:
  - `filename` - "document.txt"
  - `extension` - "txt"
  - `fullPath` - "C:\Users\John\Documents\document.txt"
  - `fileSize` - 1024 (bytes)
  - `fileSizeDisplay` - "1.0 KB" (formatted for display)
  - `fileId` - 12345 (for lazy loading)

## Why This Separation Exists

**ContiguousStringBuffer** is optimized for **fast parallel path searching**:
- Stores only paths (for substring matching)
- Minimal memory footprint
- Fast parallel search

**FileIndex** stores **complete file metadata**:
- File name, full path, extension, size, directory flag
- Used for display and other operations
- Requires lock access (shared_mutex)

## What Post-Processing Does (Step by Step)

### Step 1: Get FileEntry from FileIndex
```cpp
m_fileIndex.ForEachEntryByIds(chunk, [&](uint64_t id, const FileEntry &entry) {
  // entry contains: name, fullPath, isDirectory, fileSize, extension, etc.
```

**Why:** Need to look up the full file metadata for each ID

**Cost:** Lock acquisition (shared_lock on FileIndex) + hash map lookup

### Step 2: Apply Folders-Only Filter
```cpp
if (params.foldersOnly && !entry.isDirectory) {
  return true; // Skip files, only keep folders
}
```

**Why:** User may have selected "folders only" filter

**Note:** Extension filtering was already done in parallel search, so we skip it here

### Step 3: Create SearchResult Object
```cpp
localResults.push_back(CreateSearchResult(id, entry));
```

**What CreateSearchResult() does:**
```cpp
SearchResult result;
result.filename = entry.name;                    // "document.txt"
result.extension = GetExtension(entry.name);    // "txt"
result.fullPath = entry.fullPath;               // "C:\Users\...\document.txt"
result.fileId = id;                              // 12345
result.fileSize = entry.fileSize;                // 1024

// Format display string
if (entry.isDirectory) {
  result.fileSizeDisplay = "";                   // Folders have no size
} else if (result.fileSize == FILE_SIZE_NOT_LOADED) {
  result.fileSizeDisplay = "...";                // Loading indicator
} else {
  result.fileSizeDisplay = FormatFileSize(result.fileSize);  // "1.0 KB"
}
```

**Why:** UI needs formatted strings for display (e.g., "1.0 KB" instead of 1024)

## Why It's Slow

### Before Optimization (Sequential)
1. **For each candidate ID:**
   - Acquire `shared_lock` on FileIndex (expensive!)
   - Lookup FileEntry in hash map
   - Release lock
   - Copy entire FileEntry struct (contains strings)
   - Apply filter
   - Create SearchResult
   - Copy SearchResult to results vector

2. **Problems:**
   - **Lock overhead:** Thousands of lock acquisitions/releases
   - **Sequential:** Only one thread processing
   - **Memory copies:** Copying FileEntry and SearchResult objects

### After Optimization (Parallel + Batching)
1. **Split IDs into chunks, process in parallel**
2. **Use `ForEachEntryByIds()`:**
   - Single lock per thread (not per entry!)
   - Batch all lookups with one lock acquisition
3. **Parallel processing:**
   - Multiple threads working simultaneously
   - Each thread processes its chunk independently

## Example Flow

**Input (from parallel search):**
```
candidateIds = [12345, 67890, 11111, 22222, ...]
```

**Post-processing:**
```
Thread 1: Process [12345, 67890]
  - GetEntry(12345) → FileEntry{name="doc.txt", isDirectory=false, ...}
  - GetEntry(67890) → FileEntry{name="folder", isDirectory=true, ...}
  - Filter: Keep both (foldersOnly=false)
  - Create SearchResult objects

Thread 2: Process [11111, 22222]
  - GetEntry(11111) → FileEntry{name="file.txt", isDirectory=false, ...}
  - GetEntry(22222) → FileEntry{name="data", isDirectory=false, ...}
  - Filter: Keep both
  - Create SearchResult objects

Merge results from all threads
```

**Output:**
```cpp
results = [
  SearchResult{filename="doc.txt", fullPath="C:\...", fileSize=1024, ...},
  SearchResult{filename="folder", fullPath="C:\...", isDirectory=true, ...},
  SearchResult{filename="file.txt", fullPath="C:\...", fileSize=2048, ...},
  ...
]
```

## Summary

**Post-processing converts:**
- **Input:** List of file IDs (numbers)
- **Output:** List of SearchResult objects (with display strings)

**It does this by:**
1. Looking up FileEntry metadata for each ID
2. Applying folders-only filter
3. Creating formatted SearchResult objects for UI display

**Why it was slow:**
- Sequential processing
- Thousands of individual lock acquisitions
- No parallelization

**How we optimized it:**
- Parallel processing (multiple threads)
- Batch lookups (single lock per thread)
- Reduced lock overhead by 100x+
