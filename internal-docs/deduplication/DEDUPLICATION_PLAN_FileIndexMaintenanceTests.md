# Deduplication Plan: FileIndexMaintenanceTests.cpp

## Overview

**File:** `tests/FileIndexMaintenanceTests.cpp`  
**Current Status:** 28 duplicate blocks  
**Total Lines:** 183  
**Priority:** High (3rd highest priority test file)

---

## Analysis

### Duplication Patterns Identified

1. **Repeated Setup Code** (appears in all 6 tests):
   ```cpp
   PathStorage path_storage;
   std::shared_mutex mutex;
   std::atomic<size_t> remove_not_in_index_count{0};
   std::atomic<size_t> remove_duplicate_count{0};
   std::atomic<size_t> remove_inconsistency_count{0};
   
   auto get_alive_count = []() { return size_t(0); };
   
   FileIndexMaintenance maintenance(path_storage, mutex, get_alive_count,
                                    remove_not_in_index_count,
                                    remove_duplicate_count,
                                    remove_inconsistency_count);
   ```
   **Frequency:** 6 times (every test case)  
   **Lines per occurrence:** ~13 lines  
   **Total duplicate lines:** ~78 lines

2. **Path Insertion Patterns**:
   ```cpp
   path_storage.InsertPath(1, "C:\\test\\file1.txt", false);
   path_storage.InsertPath(2, "C:\\test\\file2.txt", false);
   path_storage.InsertPath(3, "C:\\test\\file3.txt", false);
   ```
   **Frequency:** 4+ times  
   **Pattern:** Sequential path insertion with predictable names

3. **Path Removal Patterns**:
   ```cpp
   path_storage.RemovePath(1);
   path_storage.RemovePath(2);
   path_storage.RemovePath(3);
   ```
   **Frequency:** 4+ times  
   **Pattern:** Sequential path removal

4. **get_alive_count Lambda Patterns**:
   - `[]() { return size_t(0); }` - appears 3 times
   - `[]() { return size_t(1); }` - appears 2 times
   - `[]() { return size_t(2); }` - appears 1 time
   - `[&path_storage]() { return path_storage.GetSize() - path_storage.GetDeletedCount(); }` - appears 1 time

---

## Deduplication Strategy

### Phase 1: Create RAII Fixture (Highest Impact)

**Goal:** Eliminate repeated setup code by creating a fixture class

**Action:** Create `TestFileIndexMaintenanceFixture` in `TestHelpers.h/cpp`

**Proposed Fixture:**
```cpp
namespace test_helpers {

/**
 * RAII fixture for managing FileIndexMaintenance test setup.
 * 
 * Automatically creates and configures all dependencies needed for FileIndexMaintenance tests:
 * - PathStorage
 * - std::shared_mutex
 * - Atomic counters (remove_not_in_index_count, remove_duplicate_count, remove_inconsistency_count)
 * - FileIndexMaintenance instance
 * 
 * This eliminates the repetitive 13-line setup pattern in FileIndexMaintenance tests.
 */
class TestFileIndexMaintenanceFixture {
public:
  /**
   * Create fixture with default atomic counter values (all 0).
   * 
   * @param get_alive_count Lambda function to get alive count (default: returns 0)
   */
  explicit TestFileIndexMaintenanceFixture(
      std::function<size_t()> get_alive_count = []() { return size_t(0); });
  
  /**
   * Create fixture with custom atomic counter values.
   * 
   * @param remove_not_in_index_count Initial value for remove_not_in_index_count
   * @param remove_duplicate_count Initial value for remove_duplicate_count
   * @param remove_inconsistency_count Initial value for remove_inconsistency_count
   * @param get_alive_count Lambda function to get alive count (default: returns 0)
   */
  explicit TestFileIndexMaintenanceFixture(
      size_t remove_not_in_index_count,
      size_t remove_duplicate_count,
      size_t remove_inconsistency_count,
      std::function<size_t()> get_alive_count = []() { return size_t(0); });
  
  /**
   * Create fixture with custom get_alive_count lambda that captures path_storage.
   * 
   * @param get_alive_count Lambda function that captures path_storage reference
   */
  explicit TestFileIndexMaintenanceFixture(
      std::function<size_t()> get_alive_count);
  
  ~TestFileIndexMaintenanceFixture() = default;
  
  // Accessors
  FileIndexMaintenance& GetMaintenance() { return maintenance_; }
  const FileIndexMaintenance& GetMaintenance() const { return maintenance_; }
  
  PathStorage& GetPathStorage() { return path_storage_; }
  const PathStorage& GetPathStorage() const { return path_storage_; }
  
  std::shared_mutex& GetMutex() { return mutex_; }
  
  std::atomic<size_t>& GetRemoveNotInIndexCount() { return remove_not_in_index_count_; }
  std::atomic<size_t>& GetRemoveDuplicateCount() { return remove_duplicate_count_; }
  std::atomic<size_t>& GetRemoveInconsistencyCount() { return remove_inconsistency_count_; }
  
  // Helper methods
  void InsertTestPath(uint64_t id, const std::string& path, bool is_dir = false);
  void InsertTestPaths(uint64_t start_id, uint64_t count, const std::string& base_path = "C:\\test");
  void RemoveTestPath(uint64_t id);
  void RemoveTestPaths(uint64_t start_id, uint64_t count);
  
  // Non-copyable, non-movable (RAII fixture should be created in place)
  TestFileIndexMaintenanceFixture(const TestFileIndexMaintenanceFixture&) = delete;
  TestFileIndexMaintenanceFixture& operator=(const TestFileIndexMaintenanceFixture&) = delete;
  TestFileIndexMaintenanceFixture(TestFileIndexMaintenanceFixture&&) = delete;
  TestFileIndexMaintenanceFixture& operator=(TestFileIndexMaintenanceFixture&&) = delete;

private:
  PathStorage path_storage_;
  std::shared_mutex mutex_;
  std::atomic<size_t> remove_not_in_index_count_;
  std::atomic<size_t> remove_duplicate_count_;
  std::atomic<size_t> remove_inconsistency_count_;
  FileIndexMaintenance maintenance_;
};

} // namespace test_helpers
```

**Usage Example:**
```cpp
// Before (13 lines):
PathStorage path_storage;
std::shared_mutex mutex;
std::atomic<size_t> remove_not_in_index_count{0};
std::atomic<size_t> remove_duplicate_count{0};
std::atomic<size_t> remove_inconsistency_count{0};
auto get_alive_count = []() { return size_t(0); };
FileIndexMaintenance maintenance(path_storage, mutex, get_alive_count,
                                 remove_not_in_index_count,
                                 remove_duplicate_count,
                                 remove_inconsistency_count);

// After (1 line):
test_helpers::TestFileIndexMaintenanceFixture fixture;
```

**Estimated Reduction:** ~70-80 lines (6 tests × ~13 lines setup)

---

### Phase 2: Extract Path Manipulation Helpers (Medium Impact)

**Goal:** Simplify path insertion/removal patterns

**Action:** Add helper methods to fixture or create standalone helpers

**Proposed Helpers:**
```cpp
namespace test_helpers {
namespace maintenance_test_helpers {

// Insert multiple test paths with predictable names
void InsertTestPaths(PathStorage& path_storage, uint64_t start_id, uint64_t count, 
                     const std::string& base_path = "C:\\test");

// Remove multiple test paths
void RemoveTestPaths(PathStorage& path_storage, uint64_t start_id, uint64_t count);

// Insert and immediately remove paths (for deleted entry creation)
void InsertAndRemovePaths(PathStorage& path_storage, uint64_t start_id, uint64_t count,
                          const std::string& base_path = "C:\\test");

} // namespace maintenance_test_helpers
} // namespace test_helpers
```

**Usage Example:**
```cpp
// Before:
path_storage.InsertPath(1, "C:\\test\\file1.txt", false);
path_storage.InsertPath(2, "C:\\test\\file2.txt", false);
path_storage.InsertPath(3, "C:\\test\\file3.txt", false);

// After:
fixture.InsertTestPaths(1, 3);
```

**Estimated Reduction:** ~30-40 lines

---

### Phase 3: Extract Common Assertion Patterns (Low Impact)

**Goal:** Consolidate repeated assertion patterns

**Action:** Create helper functions for common checks

**Proposed Helpers:**
```cpp
namespace test_helpers {
namespace maintenance_test_helpers {

// Verify maintenance result and deleted count
void VerifyMaintenanceResult(FileIndexMaintenance& maintenance, bool expected_result,
                             PathStorage& path_storage, size_t expected_deleted_count = 0);

// Verify maintenance stats
void VerifyMaintenanceStats(const FileIndexMaintenance::MaintenanceStats& stats,
                            size_t expected_deleted_count,
                            size_t expected_total_entries,
                            size_t expected_remove_not_in_index_count = 0,
                            size_t expected_remove_duplicate_count = 0,
                            size_t expected_remove_inconsistency_count = 0);

} // namespace maintenance_test_helpers
} // namespace test_helpers
```

**Estimated Reduction:** ~20-30 lines

---

## Implementation Order

1. **Phase 1** (RAII Fixture) ✅ COMPLETE - Highest impact, eliminates ~70-80 lines
2. **Phase 2** (Path Helpers + Assertion Helpers) ✅ COMPLETE - Combined Phase 2 & 3
   - Added `InsertAndRemoveTestPaths` helper method to fixture
   - Added `VerifyMaintenanceResult` helper function
   - Added `VerifyMaintenanceStats` helper function
3. **Phase 3** (Assertion Helpers) ✅ COMPLETE - Integrated into Phase 2

---

## Expected Results

### Before Deduplication
- **Total Lines:** 183 lines
- **Duplicate Blocks:** 28
- **Estimated Duplicate Lines:** ~100-150 lines

### After Deduplication ✅ COMPLETE
- **Total Lines:** 93 lines (49% reduction from 183)
- **Duplicate Blocks:** 0 (100% reduction from 28)
- **Maintainability:** Significantly improved
- **Phase 1:** Eliminated 13-line setup pattern (183 → 106 lines)
- **Phase 2:** Added assertion helpers and InsertAndRemoveTestPaths (106 → 93 lines)

---

## Implementation Notes

1. **Fixture Design:**
   - Support multiple constructor overloads for different use cases
   - Provide accessors for all components (maintenance, path_storage, mutex, counters)
   - Include helper methods for common operations (InsertTestPaths, RemoveTestPaths)

2. **Lambda Support:**
   - Default lambda: `[]() { return size_t(0); }`
   - Custom lambda: Support capturing path_storage reference
   - Use `std::function<size_t()>` for flexibility

3. **Path Generation:**
   - Use predictable naming: `"C:\\test\\file" + std::to_string(id) + ".txt"`
   - Support custom base paths
   - Support both files and directories

4. **Testing:**
   - All existing tests should pass after refactoring
   - No functional changes, only code organization
   - Verify fixture properly initializes all components

---

## Success Criteria

- ✅ All 6 test cases pass
- ✅ Duplicate blocks reduced from 28 to <5
- ✅ Code is more maintainable and readable
- ✅ No functional changes to test logic
