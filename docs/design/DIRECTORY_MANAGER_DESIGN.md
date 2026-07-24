# DirectoryManager Component Design

**Date:** January 1, 2026  
**Status:** Design Proposal  
**Priority:** Low (Optional Enhancement)

---

## Overview

The `DirectoryManager` component would encapsulate directory ID resolution and creation logic, currently implemented in `FileIndex::get_or_create_directory_id()`. This component handles the recursive creation of directory hierarchies and coordinates with `FileIndexStorage` for caching.

---

## Current Implementation

### Location
- **Header:** `FileIndex.h:712`
- **Implementation:** `FileIndex.cpp:213-242`
- **Usage:** Called from `FileIndex::InsertPath()` (line 201)

### Current Code
```cpp
uint64_t FileIndex::get_or_create_directory_id(const std::string &path) {
  if (path.empty()) {
    return 0; // Root directory
  }

  uint64_t cached_id = storage_.GetDirectoryId(path);
  if (cached_id != 0) {
    return cached_id;
  }

  // Directory not found, so we need to create it
  std::string parent_path;
  std::string dir_name;
  size_t last_slash = path.find_last_of("\\/");
  if (last_slash != std::string::npos) {
    parent_path = path.substr(0, last_slash);
    dir_name = path.substr(last_slash + 1);
  } else {
    dir_name = path;
  }

  uint64_t parent_id = get_or_create_directory_id(parent_path); // Recursive
  uint64_t dir_id = next_file_id_.fetch_add(1, std::memory_order_relaxed);

  // Use common logic
  InsertLocked(dir_id, parent_id, dir_name, true, {0, 0});

  storage_.CacheDirectory(path, dir_id);
  return dir_id;
}
```

### Responsibilities
1. **Cache Lookup** - Check if directory path exists in cache
2. **Path Parsing** - Extract parent path and directory name
3. **Recursive Creation** - Create parent directories if they don't exist
4. **ID Generation** - Generate new directory ID
5. **Entry Creation** - Insert directory entry into index
6. **Cache Management** - Store directory path -> ID mapping

---

## Proposed Component Design

### Class Structure

```cpp
class DirectoryManager {
public:
  /**
   * @brief Construct DirectoryManager
   *
   * @param storage Reference to FileIndexStorage (for cache operations)
   * @param operations Reference to IndexOperations (for directory insertion)
   * @param next_file_id Atomic counter for ID generation (owned by FileIndex)
   */
  DirectoryManager(FileIndexStorage& storage,
                   IndexOperations& operations,
                   std::atomic<uint64_t>& next_file_id);

  /**
   * @brief Get or create directory ID for a given path
   *
   * Recursively creates parent directories if they don't exist.
   * Uses cache for fast lookup of existing directories.
   *
   * @param path Directory path (e.g., "C:\\Users\\John\\Documents")
   * @return Directory ID (0 for root, >0 for created/cached directory)
   *
   * @pre Caller must hold unique_lock on mutex
   */
  uint64_t GetOrCreateDirectoryId(const std::string& path);

private:
  /**
   * @brief Parse path into parent path and directory name
   *
   * @param path Full directory path
   * @param out_parent_path Output: parent path (empty if root)
   * @param out_dir_name Output: directory name
   */
  void ParseDirectoryPath(const std::string& path,
                          std::string& out_parent_path,
                          std::string& out_dir_name) const;

  FileIndexStorage& storage_;
  IndexOperations& operations_;
  std::atomic<uint64_t>& next_file_id_;
};
```

### Implementation Details

```cpp
uint64_t DirectoryManager::GetOrCreateDirectoryId(const std::string& path) {
  if (path.empty()) {
    return 0; // Root directory
  }

  // Check cache first
  uint64_t cached_id = storage_.GetDirectoryId(path);
  if (cached_id != 0) {
    return cached_id;
  }

  // Parse path into parent and name
  std::string parent_path;
  std::string dir_name;
  ParseDirectoryPath(path, parent_path, dir_name);

  // Recursively create parent directories
  uint64_t parent_id = GetOrCreateDirectoryId(parent_path);

  // Generate new directory ID
  uint64_t dir_id = next_file_id_.fetch_add(1, std::memory_order_relaxed);

  // Insert directory entry (delegates to IndexOperations)
  operations_.Insert(dir_id, parent_id, dir_name, true, kFileTimeNotLoaded);

  // Cache the directory path -> ID mapping
  storage_.CacheDirectory(path, dir_id);
  return dir_id;
}
```

### Integration with FileIndex

**Before:**
```cpp
void FileIndex::InsertPath(const std::string &full_path) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  
  // ... parse path ...
  
  uint64_t parent_id = get_or_create_directory_id(directory_path);
  
  // ... create file entry ...
}
```

**After:**
```cpp
void FileIndex::InsertPath(const std::string &full_path) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  
  // ... parse path ...
  
  uint64_t parent_id = directory_manager_.GetOrCreateDirectoryId(directory_path);
  
  // ... create file entry ...
}
```

---

## Component Responsibilities

### What DirectoryManager Does:
- ✅ Directory ID resolution (cache lookup)
- ✅ Recursive directory creation
- ✅ Path parsing (extract parent/name)
- ✅ Coordinates with `FileIndexStorage` for caching
- ✅ Coordinates with `IndexOperations` for insertion

### What DirectoryManager Does NOT Do:
- ❌ Lock management (caller must hold lock)
- ❌ ID generation logic (uses atomic counter from FileIndex)
- ❌ File entry creation (only directories)
- ❌ Path storage management (handled by PathOperations)

---

## Dependencies

### Required References:
1. **FileIndexStorage&** - For directory cache operations
   - `GetDirectoryId(path)` - Cache lookup
   - `CacheDirectory(path, id)` - Cache storage

2. **IndexOperations&** - For directory insertion
   - `Insert(id, parentID, name, isDirectory, ...)` - Create directory entry

3. **std::atomic<uint64_t>&** - For ID generation
   - `fetch_add(1)` - Generate next directory ID

### Thread Safety:
- All methods assume caller holds `unique_lock` on `FileIndex`'s mutex
- No internal locking (lock is managed by `FileIndex`)

---

## Estimated Size

- **Header:** ~50-60 lines
- **Implementation:** ~80-100 lines
- **Unit Tests:** ~150-200 lines
- **Total:** ~280-360 lines

---

## Benefits

1. **Separation of Concerns** - Directory logic isolated from FileIndex
2. **Testability** - Can unit test directory creation logic independently
3. **Maintainability** - Easier to modify directory creation behavior
4. **Facade Pattern** - Moves FileIndex closer to pure facade

---

## Drawbacks

1. **Low Usage** - Only used in `InsertPath()` method
2. **Simple Logic** - Current implementation is straightforward
3. **Tight Coupling** - Requires references to multiple components
4. **Minimal Benefit** - Small reduction in FileIndex size (~30-40 lines)

---

## Alternative Names

### Option 1: `DirectoryManager` (Current Proposal)
**Pros:**
- Clear, descriptive name
- Follows naming pattern of other components (`IndexOperations`, `PathOperations`)
- Indicates it "manages" directories

**Cons:**
- Generic name (could mean many things)
- "Manager" suffix sometimes indicates God Object

---

### Option 2: `DirectoryResolver`
**Pros:**
- Emphasizes the "resolve ID from path" responsibility
- More specific than "Manager"
- Common pattern in software (e.g., `DependencyResolver`)

**Cons:**
- Doesn't convey "creation" aspect
- "Resolver" typically implies lookup only, not creation

---

### Option 3: `DirectoryFactory`
**Pros:**
- Emphasizes the "create directories" responsibility
- Well-known design pattern name
- Clear intent (creates directory entries)

**Cons:**
- Factory pattern typically creates objects, not IDs
- Doesn't convey "lookup/cache" aspect
- May be confusing (not a traditional factory)

---

### Option 4: `DirectoryOperations`
**Pros:**
- Consistent with `IndexOperations` and `PathOperations` naming
- Indicates it handles directory "operations"
- Clear that it's an operations class

**Cons:**
- Less specific (what operations?)
- Could be confused with general directory operations

---

### Option 5: `DirectoryCache`
**Pros:**
- Emphasizes the caching aspect
- Clear that it manages directory cache
- Simple, focused name

**Cons:**
- Doesn't convey "creation" aspect
- Implies it's only about caching, not creation
- May be misleading (also creates directories)

---

### Option 6: `DirectoryIdResolver`
**Pros:**
- Very specific name (resolves directory IDs)
- Clear responsibility
- Combines "directory" and "ID resolution"

**Cons:**
- Longer name
- Doesn't convey "creation" aspect
- "Resolver" implies lookup only

---

### Option 7: `DirectoryHierarchyBuilder`
**Pros:**
- Emphasizes recursive hierarchy creation
- Clear that it builds directory structures
- Descriptive of the recursive nature

**Cons:**
- Long name
- Doesn't convey "lookup/cache" aspect
- "Builder" pattern typically for complex object construction

---

## Recommendation

**Primary Recommendation:** `DirectoryResolver`

**Rationale:**
1. **Specific and Clear** - Clearly indicates it resolves directory IDs from paths
2. **Common Pattern** - "Resolver" is a well-understood pattern in software
3. **Balanced** - While it emphasizes lookup, the creation aspect is secondary (cache miss triggers creation)
4. **Concise** - Short, easy to type and remember

**Alternative:** `DirectoryOperations` (if consistency with other components is preferred)

**Rationale:**
1. **Consistency** - Matches `IndexOperations` and `PathOperations` naming
2. **Clear Pattern** - Follows established naming convention
3. **Flexible** - Can accommodate future directory-related operations

---

## Implementation Considerations

### Lock Management
- All methods assume caller holds `unique_lock` on `FileIndex`'s mutex
- No internal locking (lock is managed by `FileIndex`)
- Document this clearly in method comments

### Error Handling
- What happens if `IndexOperations::Insert()` fails?
- Should we validate path format?
- How to handle invalid paths (e.g., "C:\\..\\..")?

### Performance
- Cache lookup is O(1) (hash map)
- Recursive creation is O(depth) where depth is directory nesting level
- Path parsing is O(path_length)

### Testing
- Unit tests for cache lookup (hit/miss)
- Unit tests for recursive creation
- Unit tests for path parsing edge cases
- Integration tests with `FileIndexStorage` and `IndexOperations`

---

## Conclusion

The `DirectoryManager` (or `DirectoryResolver`) component would:
- Extract ~30-40 lines from `FileIndex`
- Improve separation of concerns
- Make directory logic more testable
- Move `FileIndex` closer to a pure facade pattern

**Priority:** Low (optional enhancement)
**Estimated Effort:** 1-2 hours
**Risk:** Low (isolated functionality)

