# ISearchableIndex Interface Design

**Date:** December 31, 2025  
**Status:** Production  
**Version:** 1.0

---

## Purpose and Responsibilities

`ISearchableIndex` is a pure interface that provides read-only access to FileIndex data for search operations. It was created to eliminate friend class dependencies and enable better separation of concerns.

### Primary Responsibilities

1. **Provide Read-Only Access**
   - Zero-copy access to SoA arrays
   - Read-only FileEntry access
   - Thread-safe synchronization primitives

2. **Eliminate Friend Classes**
   - Replaces friend class declarations
   - Enables interface-based design
   - Improves testability

3. **Enable Polymorphism**
   - Allows different index implementations
   - Enables mock implementations for testing
   - Supports future extensions

---

## Interface Definition

```cpp
class ISearchableIndex {
public:
  virtual ~ISearchableIndex() = default;

  virtual PathStorage::SoAView GetSearchableView() const = 0;
  virtual std::shared_mutex& GetMutex() const = 0;
  virtual const FileEntry* GetEntry(uint64_t id) const = 0;
  virtual size_t GetTotalItems() const = 0;
  virtual size_t GetStorageSize() const = 0;
};
```

---

## Method Specifications

### `GetSearchableView()`

```cpp
virtual PathStorage::SoAView GetSearchableView() const = 0;
```

**Purpose:** Returns a zero-copy read-only view of the Structure of Arrays (SoA) layout.

**Returns:** `SoAView` struct containing pointers to all arrays:
- `path_storage` - Contiguous path buffer
- `path_offsets` - Array of path offsets
- `path_ids` - Array of file IDs
- `is_deleted` - Array of deletion flags
- `is_directory` - Array of directory flags
- `filename_start` - Array of filename start offsets
- `extension_start` - Array of extension start offsets
- `size` - Number of items

**Thread Safety:**
- Must be called while holding a shared_lock
- View is only valid while lock is held
- No modifications allowed while view is in use

**Usage:**
```cpp
std::shared_lock<std::shared_mutex> lock(index.GetMutex());
auto view = index.GetSearchableView();
// Use view for zero-copy access...
// Lock released when view goes out of scope
```

---

### `GetMutex()`

```cpp
virtual std::shared_mutex& GetMutex() const = 0;
```

**Purpose:** Returns the shared_mutex used for thread-safe access to the index.

**Returns:** Reference to the shared_mutex

**Thread Safety:**
- Returns mutable reference (required for lock acquisition)
- Caller must acquire appropriate lock before accessing data
- Multiple readers OR single writer

**Usage:**
```cpp
// For reading (multiple readers allowed):
std::shared_lock<std::shared_mutex> lock(index.GetMutex());
// Access data...

// For writing (exclusive access):
std::unique_lock<std::shared_mutex> lock(index.GetMutex());
// Modify data...
```

---

### `GetEntry()`

```cpp
virtual const FileEntry* GetEntry(uint64_t id) const = 0;
```

**Purpose:** Returns a pointer to the FileEntry for the given ID.

**Parameters:**
- `id`: File ID to look up

**Returns:**
- Pointer to FileEntry if found
- `nullptr` if not found

**Thread Safety:**
- Must be called while holding a shared_lock
- Pointer is only valid while lock is held
- No modifications allowed while pointer is in use

**Usage:**
```cpp
std::shared_lock<std::shared_mutex> lock(index.GetMutex());
const FileEntry* entry = index.GetEntry(file_id);
if (entry) {
    // Use entry...
}
```

---

### `GetTotalItems()`

```cpp
virtual size_t GetTotalItems() const = 0;
```

**Purpose:** Returns the total number of items in the index.

**Returns:** Total number of items (including deleted entries)

**Thread Safety:**
- Must be called while holding a shared_lock
- Value may change if called without lock (not thread-safe)

**Usage:**
```cpp
std::shared_lock<std::shared_mutex> lock(index.GetMutex());
size_t total = index.GetTotalItems();
```

---

### `GetStorageSize()`

```cpp
virtual size_t GetStorageSize() const = 0;
```

**Purpose:** Returns the total size of the path storage buffer in bytes.

**Returns:** Total bytes in path storage

**Thread Safety:**
- Must be called while holding a shared_lock
- Used for determining optimal thread count

**Usage:**
```cpp
std::shared_lock<std::shared_mutex> lock(index.GetMutex());
size_t bytes = index.GetStorageSize();
int thread_count = DetermineThreadCount(-1, bytes);
```

---

## Design Decisions

### 1. Pure Interface (No Implementation)

**Decision:** Interface contains only pure virtual methods.

**Rationale:**
- Clear contract definition
- Forces implementers to provide all methods
- No hidden behavior

**Trade-offs:**
- No default implementations
- All methods must be implemented

---

### 2. Read-Only Access

**Decision:** Interface provides only read-only access.

**Rationale:**
- Search operations don't need write access
- Prevents accidental modifications
- Clear separation of concerns

**Trade-offs:**
- Write operations must go through FileIndex directly
- Better encapsulation

---

### 3. Zero-Copy SoA View

**Decision:** `GetSearchableView()` returns zero-copy view.

**Rationale:**
- Performance critical (hot path)
- No allocations during search
- Cache-friendly access

**Trade-offs:**
- View lifetime tied to lock
- Must be careful about lifetime

---

### 4. Mutable Mutex Reference

**Decision:** `GetMutex()` returns mutable reference.

**Rationale:**
- Required for lock acquisition
- Lock objects need mutable reference
- Standard C++ pattern

**Trade-offs:**
- Slightly unusual (const method returning mutable reference)
- Required for proper locking

---

## Implementation: FileIndex

`FileIndex` implements `ISearchableIndex` to provide access to its internal data:

```cpp
class FileIndex : public ISearchableIndex {
public:
  // ISearchableIndex implementation
  PathStorage::SoAView GetSearchableView() const override {
    return path_storage_.GetReadOnlyView();
  }

  std::shared_mutex& GetMutex() const override {
    return mutex_;
  }

  const FileEntry* GetEntry(uint64_t id) const override {
    return storage_.GetEntry(id);
  }

  size_t GetTotalItems() const override {
    return path_storage_.GetSize();
  }

  size_t GetStorageSize() const override {
    return path_storage_.GetStorageSize();
  }
};
```

---

## Mock Implementation: MockSearchableIndex

For testing, `MockSearchableIndex` provides a mock implementation:

```cpp
class MockSearchableIndex : public ISearchableIndex {
public:
  // Implement all ISearchableIndex methods
  // Provide configurable test data
  // Thread-safe (uses shared_mutex)
};
```

**Features:**
- Easy test data setup
- Configurable scenarios
- Thread-safe
- Supports all interface methods

---

## Thread Safety Model

### Locking Requirements

All methods that access data must:
1. Acquire appropriate lock (`shared_lock` for reading, `unique_lock` for writing)
2. Hold lock for entire operation
3. Release lock when done

### Lock Acquisition Pattern

```cpp
// Correct pattern:
std::shared_lock<std::shared_mutex> lock(index.GetMutex());
auto view = index.GetSearchableView();
// Use view...
// Lock automatically released when lock goes out of scope

// Incorrect pattern:
auto view = index.GetSearchableView(); // ❌ No lock!
// View may be invalid
```

### Concurrent Access

- **Multiple readers:** Allowed (shared_lock)
- **Reader + writer:** Writer blocks readers
- **Multiple writers:** Not allowed (unique_lock exclusive)

---

## Usage Examples

### Example 1: Parallel Search

```cpp
void Search(const ISearchableIndex& index) {
    // Acquire lock
    std::shared_lock<std::shared_mutex> lock(index.GetMutex());
    
    // Get zero-copy view
    auto view = index.GetSearchableView();
    
    // Process view...
    for (size_t i = 0; i < view.size; ++i) {
        const char* path = view.path_storage + view.path_offsets[i];
        // Process path...
    }
    
    // Lock automatically released
}
```

### Example 2: Entry Lookup

```cpp
const FileEntry* GetEntryById(const ISearchableIndex& index, uint64_t id) {
    std::shared_lock<std::shared_mutex> lock(index.GetMutex());
    return index.GetEntry(id);
    // Lock released, but entry pointer may be invalid!
    // Better: return while lock is held or copy data
}
```

### Example 3: Thread Count Determination

```cpp
int DetermineOptimalThreadCount(const ISearchableIndex& index) {
    std::shared_lock<std::shared_mutex> lock(index.GetMutex());
    size_t bytes = index.GetStorageSize();
    size_t items = index.GetTotalItems();
    return CalculateThreadCount(bytes, items);
}
```

---

## Benefits

### 1. Eliminates Friend Classes

**Before:**
```cpp
class FileIndex {
    friend class StaticChunkingStrategy;
    friend class HybridStrategy;
    // ... more friend classes
};
```

**After:**
```cpp
class FileIndex : public ISearchableIndex {
    // No friend classes needed!
};
```

---

### 2. Enables Testing

**Before:**
- Hard to test search logic without full FileIndex
- Tight coupling prevents mocking

**After:**
- Can test with `MockSearchableIndex`
- Isolated unit tests
- Easy to create test scenarios

---

### 3. Improves Extensibility

**Before:**
- Hard to add new search strategies
- Requires friend class access

**After:**
- Any class can implement `ISearchableIndex`
- Easy to add new implementations
- Supports future extensions

---

## Testing

### Unit Tests

Located in `tests/ParallelSearchEngineTests.cpp`:
- Uses `MockSearchableIndex` for testing
- Tests all interface methods
- Verifies thread safety

### Mock Implementation

`MockSearchableIndex` provides:
- Configurable test data
- Thread-safe implementation
- All interface methods implemented

---

## Dependencies

### Direct Dependencies

- `PathStorage` - For SoA view
- `FileIndexStorage` - For FileEntry access
- `<shared_mutex>` - For synchronization

### No Dependencies On

- `FileIndex` - Interface is independent
- `ParallelSearchEngine` - Interface is independent
- Any concrete implementations

---

## Future Enhancements

### Potential Extensions

1. **Async Methods**
   - Could add async versions of methods
   - Would enable async search operations

2. **Iterator Interface**
   - Could add iterator-based access
   - Would enable range-based for loops

3. **Query Interface**
   - Could add query builder pattern
   - Would enable more complex queries

---

## References

- **Main Architecture:** `docs/ARCHITECTURE_COMPONENT_BASED.md`
- **ParallelSearchEngine:** `docs/design/PARALLEL_SEARCH_ENGINE_DESIGN.md`
- **Refactoring Plan:** `docs/REFACTORING_CONTINUATION_PLAN.md`

---

## Revision History

| Date | Version | Author | Changes |
|------|---------|--------|---------|
| 2025-12-31 | 1.0 | AI Assistant | Initial interface design document |

