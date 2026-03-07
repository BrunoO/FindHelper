# FileIndex Facade Analysis
**Date:** December 31, 2025  
**Status:** Analysis of what's missing for FileIndex to be a true Facade

---

## What is a Facade Pattern?

A Facade is a structural design pattern that:
1. **Provides a simplified interface** to a complex subsystem
2. **Hides subsystem complexity** from clients
3. **Delegates work** to subsystem classes
4. **Coordinates** between components but doesn't implement business logic
5. **Acts as a single entry point** to the subsystem

---

## Current State of FileIndex

### ✅ What FileIndex Already Does Well (Facade-like):

1. **Provides unified interface** - Single entry point for all index operations
2. **Delegates search operations** - `SearchAsync()` delegates to `ParallelSearchEngine`
3. **Delegates lazy loading** - `GetFileSizeById()` delegates to `LazyAttributeLoader`
4. **Delegates maintenance** - `Maintain()` delegates to `FileIndexMaintenance`
5. **Implements ISearchableIndex** - Clean interface for search operations
6. **Manages component lifecycle** - Coordinates component initialization

### ❌ What's Missing (Still Has Business Logic):

#### 1. **IndexOperations Component** (HIGH PRIORITY)

**Problem:** `Insert()`, `Remove()`, `Rename()`, and `Move()` still contain complex coordination logic:

```cpp
// Current: FileIndex::InsertLocked() has business logic
void InsertLocked(...) {
  // Compute full path by walking parent chain
  std::string fullPath = PathBuilder::BuildFullPath(parentID, name, storage_);
  
  // Extract extension for interning
  std::string extension = GetExtension(name);
  
  // Insert into storage
  storage_.InsertLocked(...);
  
  // Update path arrays
  InsertPathLocked(id, fullPath, isDirectory);
}
```

**What Should Happen:**
```cpp
// Facade: FileIndex::Insert() should just delegate
void Insert(...) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  operations_.Insert(id, parentID, name, isDirectory, modificationTime);
}
```

**Missing Component:** `IndexOperations` class that:
- Coordinates between `FileIndexStorage` and `PathStorage`
- Handles extension extraction
- Manages path computation
- Handles directory cache updates
- Tracks statistics (remove counts)
- Manages prefix updates for directory operations

**Estimated Size:** ~300-400 lines

---

#### 2. **PathManager Component** (MEDIUM PRIORITY)

**Problem:** Path-related operations are still in FileIndex:

```cpp
// Current: FileIndex has these methods
void InsertPathLocked(uint64_t id, const std::string& path, bool isDirectory);
std::string GetPathLocked(uint64_t id) const;
PathComponentsView GetPathComponentsViewLocked(uint64_t id) const;
void UpdatePrefixLocked(const std::string& oldPrefix, const std::string& newPrefix);
```

**What Should Happen:**
- These should be encapsulated in a `PathManager` component
- `PathManager` would wrap `PathStorage` and provide higher-level operations
- `FileIndex` would delegate path operations to `PathManager`

**Estimated Size:** ~150-200 lines

---

#### 3. **DirectoryManager Component** (LOW PRIORITY)

**Problem:** Directory ID management is still in FileIndex:

```cpp
// Current: FileIndex::get_or_create_directory_id()
uint64_t get_or_create_directory_id(const std::string &path);
```

**What Should Happen:**
- Extract to `DirectoryManager` component
- Handles directory creation, caching, and ID resolution
- Coordinates with `FileIndexStorage` for directory cache

**Estimated Size:** ~100-150 lines

---

#### 4. **Extension Utilities** (VERY LOW PRIORITY)

**Problem:** `GetExtension()` is still in FileIndex (though it's a simple utility)

**What Should Happen:**
- Could be moved to a utility namespace or `PathUtils`
- Or kept in FileIndex if it's truly just a simple helper

**Estimated Size:** ~20-30 lines

---

## Summary: What's Missing

### High Priority (Blocks Facade Pattern):

1. **IndexOperations Component** (~300-400 lines)
   - Extract Insert/Remove/Rename/Move coordination logic
   - Handle extension extraction
   - Manage path computation
   - Coordinate between storage_ and path_storage_
   - Track statistics

### Medium Priority (Improves Facade):

2. **PathManager Component** (~150-200 lines)
   - Encapsulate path-related operations
   - Provide higher-level path API
   - Wrap PathStorage operations

### Low Priority (Nice to Have):

3. **DirectoryManager Component** (~100-150 lines)
   - Extract directory ID management
   - Handle directory creation/caching

4. **Extension Utilities** (~20-30 lines)
   - Move GetExtension() to utility namespace

---

## Target State: True Facade

After extracting these components, `FileIndex` would be:

```cpp
class FileIndex : public ISearchableIndex {
public:
  // Simple delegation - no business logic
  void Insert(...) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    operations_.Insert(id, parentID, name, isDirectory, modificationTime);
  }
  
  void Remove(uint64_t id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    operations_.Remove(id);
  }
  
  bool Rename(uint64_t id, const std::string& newName) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    return operations_.Rename(id, newName);
  }
  
  bool Move(uint64_t id, uint64_t newParentID) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    return operations_.Move(id, newParentID);
  }
  
  // Search delegates to ParallelSearchEngine
  std::vector<std::future<...>> SearchAsync(...) {
    SearchContext context = CreateSearchContext(...);
    return search_engine_->SearchAsync(*this, context, ...);
  }
  
  // Maintenance delegates to FileIndexMaintenance
  bool Maintain() {
    return maintenance_.Maintain();
  }
  
private:
  FileIndexStorage storage_;
  PathStorage path_storage_;
  IndexOperations operations_;      // NEW: Handles CRUD coordination
  PathManager path_manager_;        // NEW: Handles path operations
  DirectoryManager dir_manager_;     // NEW: Handles directory management
  LazyAttributeLoader lazy_loader_;
  ParallelSearchEngine search_engine_;
  FileIndexMaintenance maintenance_;
};
```

**Estimated FileIndex Size After Extraction:** ~600-700 lines (down from ~1,400 lines)

---

## Recommendation

**Priority 1:** Extract `IndexOperations` component
- This is the biggest blocker for a true Facade
- Contains the most complex coordination logic
- Would reduce FileIndex by ~300-400 lines

**Priority 2:** Extract `PathManager` component
- Improves separation of concerns
- Makes path operations more testable
- Would reduce FileIndex by ~150-200 lines

**Priority 3:** Extract `DirectoryManager` component (optional)
- Nice to have but not critical
- Would reduce FileIndex by ~100-150 lines

---

## Conclusion

**Current Status:** FileIndex is **~70% Facade** - it delegates search, lazy loading, and maintenance, but still contains significant business logic for CRUD operations.

**To Become True Facade:** Extract `IndexOperations` component (Priority 1) and optionally `PathManager` (Priority 2).

**Estimated Effort:**
- Priority 1: 4-6 hours
- Priority 2: 2-3 hours
- Priority 3: 1-2 hours (optional)

**Total:** 6-9 hours for full Facade pattern (or 4-6 hours for Priority 1 only)

