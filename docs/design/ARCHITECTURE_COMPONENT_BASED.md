# Component-Based Architecture - FileIndex System

**Date:** December 31, 2025  
**Status:** Current Architecture  
**Version:** 2.0 (Component-Based Refactoring Complete)

---

## Executive Summary

The FileIndex system has been refactored from a monolithic "God Class" (~2,823 lines) into a component-based architecture with clear separation of concerns. The refactoring reduced FileIndex to ~1,797 lines (~35% reduction) by extracting 5 major components.

**Key Achievements:**
- ✅ Extracted 5 major components (PathStorage, FileIndexStorage, PathBuilder, LazyAttributeLoader, ParallelSearchEngine)
- ✅ Eliminated all friend class dependencies (now using ISearchableIndex interface)
- ✅ Improved testability and maintainability
- ✅ Preserved performance characteristics (zero-copy access, cache efficiency)

---

## Architecture Overview

### Component Hierarchy

```
┌─────────────────────────────────────────────────────────────────┐
│                         FileIndex                                │
│              (Facade/Coordinator - ~1,797 lines)                │
│  - Coordinates components                                        │
│  - Implements ISearchableIndex interface                        │
│  - Manages thread pool lifecycle                                │
└─────────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
        ▼                     ▼                     ▼
┌──────────────┐      ┌──────────────┐      ┌──────────────┐
│ PathStorage  │      │FileIndexStorage│      │ParallelSearch│
│              │      │               │      │   Engine     │
├──────────────┤      ├───────────────┤      ├──────────────┤
│ • SoA arrays │      │ • FileEntry   │      │ • Search     │
│ • Path buffer│      │   storage     │      │   orchestration│
│ • Zero-copy  │      │ • StringPool  │      │ • Load       │
│   access     │      │ • Directory   │      │   balancing  │
│ • ~400 lines │      │   cache       │      │ • Pattern    │
│              │      │ • ~350 lines  │      │   matching   │
└──────────────┘      └───────────────┘      │ • ~600 lines │
        │                     │              └──────────────┘
        │                     │                     │
        └─────────────────────┼─────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
        ▼                     ▼                     ▼
┌──────────────┐      ┌──────────────┐      ┌──────────────┐
│ PathBuilder  │      │LazyAttribute │      │ISearchable   │
│              │      │   Loader     │      │   Index      │
├──────────────┤      ├──────────────┤      ├──────────────┤
│ • Path       │      │ • File size  │      │ • Interface  │
│   computation│      │   loading    │      │ • Eliminates │
│ • Stateless  │      │ • Mod time   │      │   friend     │
│ • ~150 lines │      │   loading    │      │   classes    │
│              │      │ • I/O ops    │      │ • ~100 lines │
│              │      │ • Caching    │      │              │
│              │      │ • ~300 lines │      │              │
└──────────────┘      └──────────────┘      └──────────────┘
```

---

## Component Responsibilities

### 1. FileIndex (Facade/Coordinator)

**Purpose:** Coordinates all components and provides the main public API.

**Responsibilities:**
- Coordinates component interactions
- Manages thread pool lifecycle
- Implements `ISearchableIndex` interface
- Provides high-level operations (Insert, Remove, Search, etc.)
- Delegates to specialized components

**Key Methods:**
- `Insert()`, `Remove()`, `Rename()`, `Move()` - File operations
- `SearchAsync()`, `SearchAsyncWithData()` - Search operations (delegates to ParallelSearchEngine)
- `GetFileSizeById()`, `GetFileModificationTimeById()` - Lazy loading (delegates to LazyAttributeLoader)

**Dependencies:**
- PathStorage (for path arrays)
- FileIndexStorage (for FileEntry storage)
- PathBuilder (for path computation)
- LazyAttributeLoader (for lazy I/O)
- ParallelSearchEngine (for search orchestration)

---

### 2. PathStorage

**Purpose:** Manages Structure of Arrays (SoA) for path data.

**Responsibilities:**
- Maintains SoA arrays (path_offsets, path_ids, is_directory, etc.)
- Manages contiguous path buffer
- Provides zero-copy read-only views
- Handles path insertion/deletion
- Optimizes for cache locality

**Key Features:**
- Zero-copy access via `SoAView`
- Contiguous memory layout (cache-friendly)
- Pre-parsed filename/extension offsets
- Thread-safe (uses shared_mutex from FileIndex)

**API:**
```cpp
class PathStorage {
  void InsertPath(uint64_t id, const std::string& path, bool is_directory);
  void RemovePath(uint64_t id);
  SoAView GetReadOnlyView() const;
  size_t GetSize() const;
  size_t GetStorageSize() const;
};
```

**Design Decisions:**
- SoA layout chosen for cache efficiency (better than AoS for parallel search)
- Pre-parsed offsets eliminate repeated string scanning
- Zero-copy views prevent unnecessary allocations

---

### 3. FileIndexStorage

**Purpose:** Manages core transactional data model.

**Responsibilities:**
- Stores FileEntry map (ID → FileEntry)
- Manages StringPool for extension interning
- Maintains directory cache
- Handles extension interning (memory optimization)
- Tracks storage size

**Key Features:**
- Hash map for O(1) lookups
- StringPool for extension deduplication
- Directory cache for fast parent lookups
- Thread-safe (uses shared_mutex from FileIndex)

**API:**
```cpp
class FileIndexStorage {
  void InsertLocked(uint64_t id, ...);
  void RemoveLocked(uint64_t id);
  const FileEntry* GetEntry(uint64_t id) const;
  const std::string* InternExtension(const std::string& ext);
  size_t Size() const;
};
```

**Design Decisions:**
- StringPool uses stable hash set to prevent dangling pointers
- Extension interning reduces memory usage (extensions are often repeated)
- Directory cache speeds up path computation

---

### 4. PathBuilder

**Purpose:** Stateless helper for path computation.

**Responsibilities:**
- Builds full paths from parent IDs
- Handles path computation logic
- No state (pure functions)

**Key Features:**
- Stateless (thread-safe by design)
- Efficient parent chain traversal
- Handles edge cases (root, missing parents)

**API:**
```cpp
class PathBuilder {
  static std::string BuildFullPath(uint64_t parentID, 
                                    const std::string& name,
                                    const FileIndexStorage& storage);
};
```

**Design Decisions:**
- Stateless design allows concurrent use without synchronization
- Static methods for simplicity
- Handles missing parents gracefully

---

### 5. LazyAttributeLoader

**Purpose:** Handles lazy loading of file attributes.

**Responsibilities:**
- Lazy loads file size (on-demand I/O)
- Lazy loads modification time (on-demand I/O)
- Implements double-check locking pattern
- Caches loaded values
- Performs I/O operations

**Key Features:**
- Double-check locking for thread safety
- Single I/O call for both size and time (FileAttributes struct)
- Caching prevents repeated I/O
- Thread-safe (uses shared_mutex from FileIndex)

**API:**
```cpp
class LazyAttributeLoader {
  uint64_t GetFileSize(uint64_t id);
  FILETIME GetModificationTime(uint64_t id);
  static uint64_t GetFileSize(const std::string& path);
  static FILETIME GetFileModificationTime(const std::string& path);
  static FileAttributes GetFileAttributes(const std::string& path);
};
```

**Design Decisions:**
- Lazy loading improves initial load time
- Double-check locking reduces lock contention
- Single I/O call optimizes performance
- Caching prevents redundant I/O

---

### 6. ParallelSearchEngine

**Purpose:** Coordinates parallel search operations.

**Responsibilities:**
- Orchestrates parallel search across threads
- Coordinates load balancing strategies
- Manages pattern matcher creation
- Aggregates search results
- Determines optimal thread count

**Key Features:**
- Supports multiple load balancing strategies (Static, Hybrid, Dynamic, Interleaved)
- Pattern matcher optimization (created once per thread)
- Thread pool integration
- Result aggregation

**API:**
```cpp
class ParallelSearchEngine {
  std::vector<std::future<std::vector<uint64_t>>>
    SearchAsync(const ISearchableIndex& index, ...);
  
  std::vector<std::future<std::vector<SearchResultData>>>
    SearchAsyncWithData(const ISearchableIndex& index, ...);
  
  static void ProcessChunkRange(...);
  static void ProcessChunkRangeIds(...);
};
```

**Design Decisions:**
- Uses ISearchableIndex interface (eliminates friend classes)
- Pattern matchers created once per thread (performance optimization)
- Delegates to LoadBalancingStrategy for work distribution
- Template ProcessChunkRange for flexibility

---

### 7. ISearchableIndex

**Purpose:** Interface for searchable index implementations.

**Responsibilities:**
- Defines read-only access contract
- Eliminates friend class dependencies
- Provides zero-copy access to SoA arrays
- Exposes synchronization primitives

**Key Features:**
- Pure interface (no implementation)
- Read-only access (thread-safe)
- Zero-copy SoA view access

**API:**
```cpp
class ISearchableIndex {
  virtual PathStorage::SoAView GetSearchableView() const = 0;
  virtual std::shared_mutex& GetMutex() const = 0;
  virtual const FileEntry* GetEntry(uint64_t id) const = 0;
  virtual size_t GetTotalItems() const = 0;
  virtual size_t GetStorageSize() const = 0;
};
```

**Design Decisions:**
- Interface pattern eliminates friend classes
- Read-only access ensures thread safety
- Zero-copy views maintain performance

---

## Data Flow Diagrams

### Search Flow

```
User Query
    │
    ▼
FileIndex::SearchAsync()
    │
    ▼
ParallelSearchEngine::SearchAsync()
    │
    ├─► Acquire shared_lock (ISearchableIndex::GetMutex())
    ├─► Get SoA view (ISearchableIndex::GetSearchableView())
    ├─► Determine thread count
    ├─► Create pattern matchers (once per thread)
    │
    ├─► Thread 1 ──► ProcessChunkRangeIds(chunk_1)
    ├─► Thread 2 ──► ProcessChunkRangeIds(chunk_2)
    ├─► Thread N ──► ProcessChunkRangeIds(chunk_N)
    │
    └─► Aggregate results ──► Return futures
```

### Insert Flow

```
File Operation (Insert/Remove/Rename)
    │
    ▼
FileIndex::Insert() / Remove() / Rename()
    │
    ├─► Acquire unique_lock (mutex_)
    │
    ├─► PathBuilder::BuildFullPath() ──► Compute path
    │
    ├─► FileIndexStorage::InsertLocked() ──► Store FileEntry
    │   ├─► Intern extension (StringPool)
    │   └─► Update directory cache
    │
    └─► PathStorage::InsertPathLocked() ──► Update SoA arrays
        └─► Update path buffer and offsets
```

### Lazy Loading Flow

```
Request for File Size/Time
    │
    ▼
FileIndex::GetFileSizeById() / GetFileModificationTimeById()
    │
    ▼
LazyAttributeLoader::GetFileSize() / GetModificationTime()
    │
    ├─► Check cache (double-check locking)
    │   ├─► If cached ──► Return cached value
    │   └─► If not cached ──► Continue
    │
    ├─► Acquire unique_lock
    ├─► Check cache again (double-check)
    ├─► If still not cached ──► Perform I/O
    │   ├─► GetFileAttributes() ──► Single I/O call
    │   └─► Cache result
    │
    └─► Return value
```

---

## Dependency Graph

```
FileIndex
  ├─► PathStorage (owns)
  ├─► FileIndexStorage (owns)
  ├─► LazyAttributeLoader (owns)
  ├─► ParallelSearchEngine (owns)
  ├─► PathBuilder (uses - static methods)
  └─► ISearchableIndex (implements)

ParallelSearchEngine
  ├─► ISearchableIndex (uses - interface)
  ├─► LoadBalancingStrategy (uses)
  ├─► SearchThreadPool (uses)
  └─► SearchStatisticsCollector (uses)

LazyAttributeLoader
  ├─► FileIndexStorage (uses - for path lookup)
  └─► PathStorage (uses - for path lookup)

PathBuilder
  └─► FileIndexStorage (uses - for parent lookup)
```

---

## Thread Safety Model

### Synchronization Primitives

1. **shared_mutex (FileIndex::mutex_)**
   - Owned by FileIndex
   - Shared with PathStorage, FileIndexStorage, LazyAttributeLoader
   - Multiple readers OR single writer
   - Used for: Insert/Remove (unique_lock), Search (shared_lock), Lazy loading (unique_lock for writes)

2. **Thread Pool (SearchThreadPool)**
   - Manages worker threads for parallel search
   - Thread-safe task queue
   - Reused across searches

### Thread Safety Guarantees

- **FileIndex operations:**
  - Insert/Remove/Rename: Thread-safe (unique_lock)
  - Search: Thread-safe (shared_lock, allows concurrent searches)
  - Lazy loading: Thread-safe (double-check locking with unique_lock)

- **Component operations:**
  - PathStorage: Thread-safe (uses FileIndex mutex)
  - FileIndexStorage: Thread-safe (uses FileIndex mutex)
  - LazyAttributeLoader: Thread-safe (uses FileIndex mutex)
  - ParallelSearchEngine: Thread-safe (acquires locks via ISearchableIndex)

---

## Performance Characteristics

### Memory Layout

- **SoA (Structure of Arrays):** Cache-friendly layout for parallel search
- **Contiguous path buffer:** Reduces cache misses
- **Pre-parsed offsets:** Eliminates repeated string scanning
- **Extension interning:** Reduces memory usage (deduplication)

### Search Performance

- **Parallel processing:** Utilizes all CPU cores
- **Zero-copy access:** No allocations during search
- **Pattern matcher reuse:** Created once per thread
- **Load balancing:** Distributes work efficiently

### I/O Performance

- **Lazy loading:** Defers I/O until needed
- **Single I/O call:** Gets both size and time together
- **Caching:** Prevents redundant I/O
- **Double-check locking:** Minimizes lock contention

---

## Migration from Monolithic Design

### Before Refactoring

- **FileIndex:** ~2,823 lines (God Class)
- **Friend classes:** Required for access
- **Tight coupling:** Hard to test components independently
- **Mixed concerns:** Search, storage, I/O all in one class

### After Refactoring

- **FileIndex:** ~1,797 lines (Facade/Coordinator)
- **No friend classes:** Uses ISearchableIndex interface
- **Loose coupling:** Components can be tested independently
- **Clear separation:** Each component has single responsibility

### Benefits Achieved

1. **Maintainability:** Easier to understand and modify
2. **Testability:** Components can be tested in isolation
3. **Reusability:** Components can be reused in other contexts
4. **Performance:** No performance degradation (zero-copy preserved)
5. **Extensibility:** Easy to add new search strategies or components

---

## Future Extensions

### Potential Additional Components

1. **FileIndexMaintenance**
   - Extract `Maintain()` and `RebuildPathBuffer()`
   - Separate maintenance logic from core indexing

2. **SearchStatisticsCollector** ✅ (Already extracted)
   - Statistics collection logic
   - Thread timing, chunk bytes, result aggregation

### Potential Improvements

1. **Async I/O for lazy loading**
   - Use async I/O APIs for file attribute loading
   - Could improve responsiveness

2. **Search result caching**
   - Cache recent search results
   - Could speed up repeated queries

3. **Incremental path updates**
   - Update paths incrementally instead of full recompute
   - Could improve update performance

---

## References

- **Component Design Documents:**
  - `docs/design/PARALLEL_SEARCH_ENGINE_DESIGN.md`
  - `docs/design/ISEARCHABLE_INDEX_DESIGN.md`
  - `docs/design/STRING_POOL_DESIGN.md`

- **Refactoring Documentation:**
  - `docs/REFACTORING_CONTINUATION_PLAN.md`
  - `docs/ARCHITECTURAL_REVIEW_REMAINING_ISSUES-2025-12-29.md`

---

## Revision History

| Date | Version | Author | Changes |
|------|---------|--------|---------|
| 2025-12-31 | 2.0 | AI Assistant | Created component-based architecture document |
| 2025-12-29 | 1.0 | Original | Monolithic FileIndex architecture |

