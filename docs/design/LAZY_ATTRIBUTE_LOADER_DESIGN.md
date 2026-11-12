# LazyAttributeLoader Design & Safety Analysis

**Date:** 2025-12-29  
**Purpose:** Explain how LazyAttributeLoader will work and ensure safe refactoring

---

## Semantics (file size / modification time)

This section defines the contract for lazy-loaded attributes so changes do not reintroduce regressions (e.g. wrong size 0, caching failure as success).

### Three states for file size

| State | Constant / meaning | Who may set it |
|-------|---------------------|----------------|
| **Not loaded** | `kFileSizeNotLoaded` | Initial insert; MFT when we do not cache (e.g. MFT size 0); never after a load attempt. |
| **Loaded** | Any concrete value (including 0) | Lazy loader when provider returns `success == true`; MFT when we cache non-zero size only. |
| **Failed** | `kFileSizeFailed` | Lazy loader when provider returns `success == false`; path empty; never write 0 as “valid” on failure. |

### Who may write what

- **Initial index (MFT):** May call `UpdateFileSizeById(id, size)` only when MFT succeeded and `size > 0`. Do not cache MFT size 0; leave as `kFileSizeNotLoaded` so lazy loader runs.
- **Lazy loader:** On provider `success == true`, call `UpdateFileSize(id, size)` (size may be 0 for zero-byte files). On provider `success == false`, call `MarkFailed()` only; do not call `UpdateFileSize` with any value.
- **Attribute provider (`GetFileAttributes`):** On success, `result.success == true` and `result.fileSize` is logical size (0 only for true zero-byte files). On failure, `result.success == false`; consumers must not treat `result.fileSize` as valid.

### UI / display

- `GetValueOrZero()` is for display only; it is not a success signal. Failed and not-loaded both yield 0 for display; storage state is still Failed vs NotLoaded.
- “Loading attributes…” in the status bar is shown for the entire sort-by-Size/LastModified flow until the sort completes.

### Change checklist

Before merging changes that touch any of:

- `LazyAttributeLoader.cpp` / `.h`
- `InitialIndexPopulator.cpp` (MFT / `UpdateFileSizeById`)
- `FileSystemUtils.h` (`GetFileAttributes`, `GetFileSize`, `GetFileModificationTime`)
- `SearchResultUtils.cpp` (`StartAttributeLoadingAsync`, `EnsureDisplayStringsPopulated`, sort/display paths)

verify:

1. **Success vs failure:** Any place that decides "load succeeded" uses the provider result (e.g. `attrs.success`), not path non-empty or similar. On failure, storage is updated with "failed" (e.g. `MarkFailed()`), not with 0 as a valid size.
2. **MFT and 0:** `InitialIndexPopulator` does not cache MFT-reported size 0 as a final value; leave as `kFileSizeNotLoaded` so the lazy loader runs.
3. **Locks and I/O:** No lock is held across `GetFileAttributes` / `GetFileSize` / `GetFileModificationTime`. Double-check after acquiring the unique lock before writing the cache.
4. **Tests:** Run `lazy_attribute_loader_tests` (and the full test suite if available) after changes.
5. **AGENTS.md:** Follow project rules in [AGENTS.md](../../AGENTS.md) (naming, Windows-specific rules, C++17, etc.) when modifying these files.

---

## Current Implementation Analysis

### Current Lazy Loading Pattern

The current implementation in `FileIndex::GetFileSizeById()` and `GetFileModificationTimeById()` uses a **double-check locking pattern** with a critical optimization:

**Pattern:**
1. **Fast Path (Shared Lock)**: Check if value is already loaded → return immediately
2. **Path Extraction (Shared Lock)**: Get file path (brief, no I/O)
3. **I/O Operation (No Lock)**: Perform file system I/O **WITHOUT holding any lock**
4. **Update (Unique Lock)**: Double-check another thread didn't load it, then update cache

**Critical Optimization:**
- If both size and modification time need loading, load both in **one system call** (`GetFileAttributes()`)
- This reduces I/O operations by 50% when both attributes are needed

### Current Code Structure

```cpp
// FileIndex::GetFileSizeById() - Current Implementation
uint64_t GetFileSizeById(uint64_t id) const {
  // Phase 1: Fast check (shared lock)
  bool needTime = false;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    const FileEntry* entry = storage_.GetEntry(id);
    if (entry == nullptr) return 0;
    if (!entry->fileSize.IsNotLoaded()) {
      return entry->fileSize.GetValueOrZero(); // Fast path
    }
    if (entry->isDirectory) return 0;
    needTime = entry->lastModificationTime.IsNotLoaded(); // Optimization check
  }

  // Phase 2: Get path (shared lock, brief)
  std::string path;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    path = GetPathLocked(id);
  }

  // Phase 3: I/O WITHOUT LOCK (critical for performance)
  uint64_t size = 0;
  FILETIME modTime = kFileTimeNotLoaded;
  bool success = false;
  
  if (needTime) {
    FileAttributes attrs = GetFileAttributes(path); // One system call
    size = attrs.fileSize;
    modTime = attrs.success ? attrs.lastModificationTime : kFileTimeFailed;
    success = attrs.success;
  } else {
    size = GetFileSize(path); // Single attribute
    success = !path.empty();
  }

  // Phase 4: Update cache (unique lock, brief)
  {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const FileEntry* entry = storage_.GetEntry(id);
    if (entry != nullptr) {
      // Double-check: Another thread may have loaded it
      if (!entry->fileSize.IsNotLoaded()) {
        return entry->fileSize.GetValueOrZero(); // Use cached value
      }
      
      // We're first - update cache
      if (success) {
        storage_.UpdateFileSize(id, size);
        if (needTime && entry->lastModificationTime.IsNotLoaded()) {
          storage_.UpdateModificationTime(id, modTime);
        }
      } else {
        // Mark as failed to avoid retry loops
        FileEntry* mutable_entry = storage_.GetEntryMutable(id);
        if (mutable_entry != nullptr) {
          mutable_entry->fileSize.MarkFailed();
        }
      }
      const FileEntry* updated_entry = storage_.GetEntry(id);
      return updated_entry != nullptr ? updated_entry->fileSize.GetValueOrZero() : 0;
    }
  }
  return 0;
}
```

---

## Proposed LazyAttributeLoader Design

### Class Structure

```cpp
class LazyAttributeLoader {
public:
  // Constructor - takes references to storage and path storage
  LazyAttributeLoader(FileIndexStorage& storage, PathStorage& pathStorage);
  
  // Main lazy loading methods (preserve exact behavior)
  uint64_t GetFileSize(uint64_t id) const;
  FILETIME GetModificationTime(uint64_t id, bool alsoLoadSize = false) const;
  
  // Manual loading methods (for UI optimization)
  bool LoadFileSize(uint64_t id);
  bool LoadModificationTime(uint64_t id);
  
  // Batch loading (for UI optimization)
  void PreloadAttributes(const std::vector<uint64_t>& ids) const;
  
private:
  FileIndexStorage& storage_;
  PathStorage& pathStorage_;
  mutable std::shared_mutex& mutex_; // Reference to shared mutex
  
  // I/O helpers (static, no state)
  static FileAttributes GetFileAttributes(const std::string& path);
  static uint64_t GetFileSize(const std::string& path);
  static FILETIME GetFileModificationTime(const std::string& path);
  
  // Double-check locking helper (template for reuse)
  template<typename T>
  T LoadWithDoubleCheck(
    uint64_t id,
    std::function<T()> loader,  // I/O operation (no lock)
    std::function<void(uint64_t, T)> updater,  // Cache update (with lock)
    std::function<T()> getter  // Get cached value
  ) const;
};
```

### Key Design Decisions

#### 1. **Preserve Double-Check Locking Pattern**

**Why:** This pattern is critical for performance:
- Fast path (already loaded) uses shared lock (allows concurrent readers)
- I/O operations happen **without any lock** (prevents blocking other threads)
- Update uses unique lock only briefly (just to write cached value)

**Safety:** Must preserve exact same pattern - any deviation risks:
- Performance regression (holding locks during I/O)
- Race conditions (incorrect double-check)
- Deadlocks (incorrect lock ordering)

#### 2. **Maintain Optimization (Load Both Attributes Together)**

**Why:** When both size and time need loading, `GetFileAttributes()` loads both in one system call, reducing I/O by 50%.

**Safety:** Must preserve this optimization:
- Check if both need loading before I/O
- Use `GetFileAttributes()` when both needed
- Use individual calls when only one needed

#### 3. **Handle Failure Cases**

**Why:** Files may be deleted, permission denied, or on network drives. Must mark as failed to avoid infinite retry loops.

**Safety:** Must preserve failure handling:
- Mark as failed (not just return sentinel)
- Prevent retry loops
- Return safe defaults

#### 4. **Thread Safety**

**Why:** Multiple threads may call lazy loading simultaneously. Double-check locking ensures only one thread does I/O.

**Safety:** Must preserve thread safety:
- Double-check after acquiring unique lock
- Handle case where another thread loaded value during I/O
- Use mutable fields correctly (FileEntry fields are mutable)

---

## Refactoring Safety Strategy

### Phase 1: Extract I/O Helpers (Low Risk)

**Step 1.1:** Move I/O functions to LazyAttributeLoader as static methods
- `GetFileSize()` → `LazyAttributeLoader::GetFileSize()`
- `GetFileModificationTime()` → `LazyAttributeLoader::GetFileModificationTime()`
- `GetFileAttributes()` → `LazyAttributeLoader::GetFileAttributes()`

**Safety:**
- ✅ Static methods, no state changes
- ✅ Same function signatures
- ✅ No lock dependencies
- ✅ Easy to test

**Verification:**
- Update FileIndex to call `LazyAttributeLoader::GetFileSize()` instead of `GetFileSize()`
- Run tests to verify behavior unchanged

### Phase 2: Extract Double-Check Locking Helper (Medium Risk)

**Step 2.1:** Create template helper `LoadWithDoubleCheck()` that encapsulates the pattern

```cpp
template<typename T>
T LazyAttributeLoader::LoadWithDoubleCheck(
  uint64_t id,
  std::function<T()> loader,  // I/O operation
  std::function<void(uint64_t, T)> updater,  // Cache update
  std::function<T()> getter  // Get cached value
) const {
  // Phase 1: Fast check (shared lock)
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    T cached = getter();
    if (!IsNotLoaded(cached)) {
      return cached; // Fast path
    }
  }
  
  // Phase 2: Get path (shared lock)
  std::string path;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    path = pathStorage_.GetPath(id);
  }
  
  // Phase 3: I/O WITHOUT LOCK
  T value = loader(path);
  
  // Phase 4: Update cache (unique lock, double-check)
  {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    T cached = getter();
    if (!IsNotLoaded(cached)) {
      return cached; // Another thread loaded it
    }
    updater(id, value);
    return getter();
  }
}
```

**Safety:**
- ⚠️ Template complexity - must handle both uint64_t and FILETIME
- ⚠️ Must preserve exact lock ordering
- ⚠️ Must preserve double-check logic

**Verification:**
- Unit tests for LoadWithDoubleCheck with both types
- Verify lock ordering (shared → none → unique)
- Verify double-check prevents redundant I/O

### Phase 3: Extract Main Methods (High Risk)

**Step 3.1:** Move `GetFileSizeById()` to `LazyAttributeLoader::GetFileSize()`

**Step 3.2:** Move `GetFileModificationTimeById()` to `LazyAttributeLoader::GetModificationTime()`

**Safety Concerns:**
- ⚠️ **Critical**: Must preserve exact lock ordering
- ⚠️ **Critical**: Must preserve I/O without lock
- ⚠️ **Critical**: Must preserve optimization (load both together)
- ⚠️ **Critical**: Must preserve failure handling

**Verification Strategy:**
1. **Unit Tests**: Test each phase independently
   - Fast path (already loaded)
   - I/O path (not loaded)
   - Double-check (concurrent access)
   - Failure cases

2. **Integration Tests**: Test with real FileIndex
   - Verify behavior matches current implementation
   - Test concurrent access (multiple threads)
   - Test failure cases (deleted files, permission denied)

3. **Performance Tests**: Verify no regression
   - Measure lock contention
   - Measure I/O performance
   - Verify optimization (load both together)

4. **Code Review**: Manual review of lock ordering
   - Verify no locks held during I/O
   - Verify double-check logic is correct
   - Verify failure handling is preserved

---

## Safety Checklist

### Before Refactoring

- [ ] **Understand current implementation**: Read and understand all lazy loading methods
- [ ] **Identify dependencies**: List all functions/methods that call lazy loading
- [ ] **Document lock ordering**: Document exact lock sequence in current code
- [ ] **Document I/O operations**: List all I/O operations and their timing
- [ ] **Document failure handling**: Understand how failures are handled

### During Refactoring

- [ ] **Preserve lock ordering**: Exact same sequence (shared → none → unique)
- [ ] **Preserve I/O timing**: I/O must happen without any lock
- [ ] **Preserve optimization**: Load both attributes together when both needed
- [ ] **Preserve failure handling**: Mark as failed, prevent retry loops
- [ ] **Preserve double-check**: Check again after acquiring unique lock

### After Refactoring

- [ ] **All tests pass**: No regressions
- [ ] **Performance verified**: No lock contention increase
- [ ] **Concurrent access tested**: Multiple threads work correctly
- [ ] **Failure cases tested**: Deleted files, permission denied, etc.
- [ ] **Code review**: Manual review of lock ordering and I/O timing

---

## Critical Safety Requirements

### 1. **Never Hold Lock During I/O** ⚠️ CRITICAL

**Current Pattern:**
```cpp
// ✅ CORRECT
{
  std::shared_lock lock(mutex_);
  path = GetPathLocked(id);
} // Lock released
uint64_t size = GetFileSize(path); // I/O WITHOUT LOCK
{
  std::unique_lock lock(mutex_);
  // Update cache
}
```

**Why Critical:**
- I/O operations can take 10-100ms (file system, network drives)
- Holding lock during I/O blocks all other threads
- Would cause severe performance degradation

**Safety Check:**
- ✅ Verify no locks are held during `GetFileSize()`, `GetFileModificationTime()`, or `GetFileAttributes()` calls
- ✅ Verify locks are released before I/O
- ✅ Verify locks are reacquired only for cache update

### 2. **Preserve Double-Check Locking** ⚠️ CRITICAL

**Current Pattern:**
```cpp
// Phase 1: Check with shared lock
{
  std::shared_lock lock(mutex_);
  if (!entry->fileSize.IsNotLoaded()) {
    return entry->fileSize.GetValueOrZero(); // Fast path
  }
}

// Phase 2: I/O without lock
uint64_t size = GetFileSize(path);

// Phase 3: Double-check with unique lock
{
  std::unique_lock lock(mutex_);
  // ⚠️ CRITICAL: Check again - another thread may have loaded it
  if (!entry->fileSize.IsNotLoaded()) {
    return entry->fileSize.GetValueOrZero(); // Use cached value
  }
  // We're first - update cache
  storage_.UpdateFileSize(id, size);
}
```

**Why Critical:**
- Prevents redundant I/O when multiple threads need same value
- Ensures only one thread does I/O
- Prevents race conditions

**Safety Check:**
- ✅ Verify double-check after acquiring unique lock
- ✅ Verify cached value is returned if another thread loaded it
- ✅ Verify only first thread updates cache

### 3. **Preserve Optimization (Load Both Together)** ⚠️ IMPORTANT

**Current Pattern:**
```cpp
// Check if both need loading
bool needTime = entry->lastModificationTime.IsNotLoaded();

if (needTime) {
  // Load both in one system call
  FileAttributes attrs = GetFileAttributes(path);
  size = attrs.fileSize;
  modTime = attrs.lastModificationTime;
} else {
  // Load only size
  size = GetFileSize(path);
}
```

**Why Important:**
- Reduces I/O operations by 50% when both needed
- `GetFileAttributes()` is more efficient than two separate calls
- Common case: UI displays both size and time

**Safety Check:**
- ✅ Verify both attributes checked before I/O
- ✅ Verify `GetFileAttributes()` used when both needed
- ✅ Verify individual calls used when only one needed

### 4. **Preserve Failure Handling** ⚠️ IMPORTANT

**Current Pattern:**
```cpp
if (success) {
  storage_.UpdateFileSize(id, size);
} else {
  // Mark as failed to avoid retry loops
  FileEntry* mutable_entry = storage_.GetEntryMutable(id);
  if (mutable_entry != nullptr) {
    mutable_entry->fileSize.MarkFailed();
  }
}
```

**Why Important:**
- Prevents infinite retry loops for deleted files
- Prevents repeated I/O failures (permission denied, network errors)
- Provides clear failure state

**Safety Check:**
- ✅ Verify failures are marked (not just return sentinel)
- ✅ Verify failed state prevents retries
- ✅ Verify safe defaults are returned

---

## Refactoring Steps (Safe Incremental Approach)

### Step 1: Create LazyAttributeLoader Class (No Behavior Change)

**Action:**
- Create `LazyAttributeLoader.h` and `LazyAttributeLoader.cpp`
- Add constructor that takes `FileIndexStorage&` and `PathStorage&`
- Add static I/O helper methods (move from FileIndex)
- **Do NOT change FileIndex yet**

**Safety:**
- ✅ No behavior change (just new class)
- ✅ Static methods are safe
- ✅ Easy to verify (compile check)

**Verification:**
- Compile successfully
- No test changes needed (not used yet)

### Step 2: Move I/O Helpers to LazyAttributeLoader

**Action:**
- Move `GetFileSize()`, `GetFileModificationTime()`, `GetFileAttributes()` to LazyAttributeLoader
- Update FileIndex to call `LazyAttributeLoader::GetFileSize()` etc.
- **Keep lazy loading methods in FileIndex for now**

**Safety:**
- ✅ Static methods, no state
- ✅ Same signatures
- ✅ Easy to verify (same behavior)

**Verification:**
- All tests pass
- No behavior change

### Step 3: Extract Double-Check Locking Helper

**Action:**
- Create `LoadWithDoubleCheck()` template helper
- Test helper with unit tests
- **Do NOT use in FileIndex yet**

**Safety:**
- ✅ Isolated helper, easy to test
- ✅ Can verify lock ordering in tests
- ✅ Can verify double-check logic

**Verification:**
- Unit tests for LoadWithDoubleCheck
- Verify lock ordering (shared → none → unique)
- Verify double-check prevents redundant I/O

### Step 4: Extract GetFileSizeById() to LazyAttributeLoader

**Action:**
- Move `GetFileSizeById()` logic to `LazyAttributeLoader::GetFileSize()`
- Update FileIndex to delegate to LazyAttributeLoader
- **Preserve exact same implementation**

**Safety:**
- ⚠️ **Critical**: Must preserve exact lock ordering
- ⚠️ **Critical**: Must preserve I/O without lock
- ⚠️ **Critical**: Must preserve double-check

**Verification:**
- All tests pass
- Performance test (verify no regression)
- Concurrent access test (multiple threads)

### Step 5: Extract GetFileModificationTimeById() to LazyAttributeLoader

**Action:**
- Move `GetFileModificationTimeById()` logic to `LazyAttributeLoader::GetModificationTime()`
- Update FileIndex to delegate to LazyAttributeLoader
- **Preserve exact same implementation**

**Safety:**
- ⚠️ **Critical**: Same concerns as Step 4
- ⚠️ **Critical**: Must preserve optimization (load both together)

**Verification:**
- All tests pass
- Performance test (verify optimization preserved)
- Concurrent access test

### Step 6: Extract Manual Loading Methods

**Action:**
- Move `LoadFileSize()` and `LoadFileModificationTime()` to LazyAttributeLoader
- Update FileIndex to delegate

**Safety:**
- ✅ Lower risk (simpler methods)
- ✅ Less complex locking

**Verification:**
- All tests pass

---

## Testing Strategy

### Unit Tests

**Test LazyAttributeLoader in isolation:**
- Fast path (value already loaded)
- I/O path (value not loaded)
- Double-check (concurrent access simulation)
- Failure cases (deleted file, permission denied)
- Optimization (load both together)

### Integration Tests

**Test with real FileIndex:**
- Verify behavior matches current implementation
- Test concurrent access (multiple threads calling lazy loading)
- Test failure cases
- Test optimization (both attributes loaded together)

### Performance Tests

**Verify no regression:**
- Measure lock contention (should be same or better)
- Measure I/O performance (should be same)
- Verify optimization works (load both together reduces I/O)

### Concurrent Access Tests

**Verify thread safety:**
- Multiple threads calling `GetFileSizeById()` for same ID
- Multiple threads calling `GetFileModificationTimeById()` for same ID
- Verify only one thread does I/O
- Verify all threads get correct value

---

## Risk Assessment

### Low Risk
- ✅ Moving I/O helpers to static methods
- ✅ Extracting manual loading methods

### Medium Risk
- ⚠️ Creating double-check locking helper (template complexity)
- ⚠️ Extracting main lazy loading methods (must preserve exact behavior)

### High Risk
- ⚠️ **Lock ordering**: Must preserve exact sequence
- ⚠️ **I/O without lock**: Must ensure no locks held during I/O
- ⚠️ **Double-check logic**: Must preserve to prevent race conditions

### Mitigation Strategies

1. **Incremental approach**: Extract one method at a time
2. **Comprehensive testing**: Test each phase independently
3. **Code review**: Manual review of lock ordering
4. **Performance verification**: Measure to ensure no regression
5. **Rollback plan**: Keep old implementation until new one is verified

---

## Success Criteria

### Functional Correctness
- ✅ All existing tests pass
- ✅ Behavior matches current implementation exactly
- ✅ No regressions in functionality

### Performance
- ✅ No increase in lock contention
- ✅ I/O performance maintained
- ✅ Optimization preserved (load both together)

### Thread Safety
- ✅ Concurrent access works correctly
- ✅ Only one thread does I/O per attribute
- ✅ All threads get correct values

### Code Quality
- ✅ Lazy loading logic isolated in LazyAttributeLoader
- ✅ FileIndex complexity reduced
- ✅ Code is testable and maintainable

---

## Conclusion

**LazyAttributeLoader extraction is feasible but requires careful attention to:**
1. **Lock ordering** (shared → none → unique)
2. **I/O without lock** (critical for performance)
3. **Double-check locking** (prevents race conditions)
4. **Optimization** (load both attributes together)

**Recommended approach:**
- Incremental extraction (one method at a time)
- Comprehensive testing at each step
- Manual code review of lock ordering
- Performance verification

**Estimated effort:** 2-3 hours (with testing and verification)

---

**Next Steps:**
1. Review this design document
2. Create LazyAttributeLoader class structure
3. Extract I/O helpers first (low risk)
4. Extract main methods incrementally (with testing at each step)

