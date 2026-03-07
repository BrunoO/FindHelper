# Deduplication Plan: LazyAttributeLoaderTests.cpp

**Target File:** `tests/LazyAttributeLoaderTests.cpp`  
**Current Duplicate Blocks:** 96  
**Priority:** High (Test file with most duplicates)

---

## Analysis Summary

The test file contains extensive duplication in the following areas:

1. **Test Setup Pattern** (repeated ~30+ times):
   - Creating `std::shared_mutex`, `FileIndexStorage`, `PathStorage`, `LazyAttributeLoader`
   - Creating `TempFileFixture`
   - Inserting file entry: `storage.InsertLocked(fileId, 0, "test.txt", false, kFileTimeNotLoaded, ".txt")`
   - Inserting path: `pathStorage.InsertPath(fileId, fixture.path, false)`

2. **Load and Verify Pattern** (repeated ~15+ times):
   - Load attribute (size or time)
   - Check if loaded successfully
   - Verify result matches expected value

3. **Failure Scenario Pattern** (repeated ~6+ times):
   - Setup with invalid path (deleted, non-existent, empty)
   - Attempt to load attribute
   - Verify returns safe default (0 for size, failed/sentinel for time)
   - Verify no retry (marked as failed)

4. **Concurrent Access Pattern** (repeated 2 times):
   - Setup with file entry
   - Launch multiple threads requesting same attribute
   - Verify all threads get same result
   - Verify attribute was actually loaded

5. **Optimization Verification Pattern** (repeated 2 times):
   - Request one attribute when both are unloaded
   - Verify both attributes were loaded together

---

## Deduplication Strategy

### Phase 1: Create Test Fixture (High Impact)

**Goal:** Eliminate repeated setup code (~30+ occurrences)

**Action:** Create `TestLazyAttributeLoaderFixture` in `TestHelpers.h/cpp`

**Pattern to Extract:**
```cpp
std::shared_mutex mutex;
FileIndexStorage storage(mutex);
PathStorage pathStorage;
LazyAttributeLoader loader(storage, pathStorage, mutex);
TempFileFixture fixture;
uint64_t fileId = 1;
storage.InsertLocked(fileId, 0, "test.txt", false, kFileTimeNotLoaded, ".txt");
pathStorage.InsertPath(fileId, fixture.path, false);
```

**Proposed Fixture:**
```cpp
class TestLazyAttributeLoaderFixture {
public:
  explicit TestLazyAttributeLoaderFixture(
      uint64_t file_id = 1,
      const std::string& filename = "test.txt",
      const std::string& extension = ".txt",
      bool is_directory = false);
  
  LazyAttributeLoader& GetLoader() { return loader_; }
  FileIndexStorage& GetStorage() { return storage_; }
  PathStorage& GetPathStorage() { return path_storage_; }
  TempFileFixture& GetTempFile() { return temp_file_; }
  uint64_t GetFileId() const { return file_id_; }
  
  // Helper to insert file entry with custom parameters
  void InsertFileEntry(uint64_t id, const std::string& name, bool is_dir, const std::string& path);
  
  // Helper to insert file entry with default temp file
  void InsertFileEntryWithTempFile(uint64_t id, const std::string& name, bool is_dir);
  
private:
  std::shared_mutex mutex_;
  FileIndexStorage storage_;
  PathStorage path_storage_;
  LazyAttributeLoader loader_;
  TempFileFixture temp_file_;
  uint64_t file_id_;
};
```

**Estimated Reduction:** ~200-300 lines of duplicate code

---

### Phase 2: Extract Common Test Helpers (Medium Impact)

**Goal:** Eliminate repeated assertion and verification patterns

**Actions:**

#### 2.1: Create Load and Verify Helpers

**Pattern to Extract:**
```cpp
bool loaded = loader.LoadFileSize(fileId);
CHECK(loaded == true);
uint64_t size = loader.GetFileSize(fileId);
CHECK(size == fixture.expectedSize);
```

**Proposed Helpers:**
```cpp
namespace lazy_loader_test_helpers {
  // Verify file size was loaded successfully
  bool VerifyFileSizeLoaded(LazyAttributeLoader& loader, uint64_t file_id, uint64_t expected_size);
  
  // Verify modification time was loaded successfully
  bool VerifyModificationTimeLoaded(LazyAttributeLoader& loader, uint64_t file_id);
  
  // Verify load operation returned expected result
  void VerifyLoadResult(bool loaded, bool expected = true);
  
  // Verify attribute is not loaded (still unloaded)
  bool VerifyAttributeNotLoaded(LazyAttributeLoader& loader, uint64_t file_id, bool check_size = true);
}
```

**Estimated Reduction:** ~50-100 lines

#### 2.2: Create Failure Scenario Helpers

**Pattern to Extract:**
```cpp
// Setup with invalid path
std::string invalidPath = "/nonexistent/path/file.txt";
storage.InsertLocked(fileId, 0, "nonexistent.txt", false, kFileTimeNotLoaded, ".txt");
pathStorage.InsertPath(fileId, invalidPath, false);

// Try to load
uint64_t size = loader.GetFileSize(fileId);
CHECK(size == 0);

// Verify no retry
uint64_t size2 = loader.GetFileSize(fileId);
CHECK(size2 == 0);
```

**Proposed Helpers:**
```cpp
namespace lazy_loader_test_helpers {
  // Setup file entry with invalid path (non-existent, deleted, or empty)
  void SetupInvalidPathEntry(
      FileIndexStorage& storage,
      PathStorage& path_storage,
      uint64_t file_id,
      const std::string& filename,
      const std::string& invalid_path);
  
  // Verify failure scenario for file size (returns 0, no retry)
  void VerifyFileSizeFailure(LazyAttributeLoader& loader, uint64_t file_id);
  
  // Verify failure scenario for modification time (returns failed/sentinel, no retry)
  void VerifyModificationTimeFailure(LazyAttributeLoader& loader, uint64_t file_id);
}
```

**Estimated Reduction:** ~80-120 lines

---

### Phase 3: Use Parameterized Tests (Medium Impact)

**Goal:** Consolidate similar test cases into parameterized tests

**Actions:**

#### 3.1: Parameterize Static I/O Method Tests

**Current Pattern:** Multiple similar tests for `GetFileAttributes`, `GetFileSize`, `GetFileModificationTime`

**Proposed:**
```cpp
struct StaticIOTestCase {
  std::string test_name;
  std::function<void(const std::string&)> test_func;
  bool expect_success;
  uint64_t expected_size;  // For size tests
  bool expect_valid_time;  // For time tests
};

TEST_CASE("Static I/O Methods - File Operations") {
  std::vector<StaticIOTestCase> test_cases = {
    {"GetFileAttributes - returns correct file size", 
     [](const std::string& path) {
       FileAttributes attrs = LazyAttributeLoader::GetFileAttributes(path);
       CHECK(attrs.success == true);
       CHECK(attrs.fileSize > 0);
     }, true, 0, false},
    // ... more test cases
  };
  
  TempFileFixture fixture;
  for (const auto& tc : test_cases) {
    DOCTEST_SUBCASE(tc.test_name.c_str()) {
      tc.test_func(fixture.path);
    }
  }
}
```

**Estimated Reduction:** ~40-60 lines

#### 3.2: Parameterize Load Method Tests

**Current Pattern:** Similar tests for `LoadFileSize` and `LoadModificationTime` with same patterns

**Proposed:**
```cpp
struct LoadMethodTestCase {
  std::string test_name;
  std::function<bool(LazyAttributeLoader&, uint64_t)> load_func;
  std::function<void(LazyAttributeLoader&, uint64_t)> verify_func;
  bool expect_load_success;
};

TEST_CASE("Manual Loading Methods - Load and Verify") {
  std::vector<LoadMethodTestCase> test_cases = {
    {"LoadFileSize - loads size for unloaded file",
     [](LazyAttributeLoader& l, uint64_t id) { return l.LoadFileSize(id); },
     [](LazyAttributeLoader& l, uint64_t id) { 
       uint64_t size = l.GetFileSize(id);
       CHECK(size > 0);
     }, true},
    // ... more test cases
  };
  
  TestLazyAttributeLoaderFixture fixture;
  for (const auto& tc : test_cases) {
    DOCTEST_SUBCASE(tc.test_name.c_str()) {
      bool loaded = tc.load_func(fixture.GetLoader(), fixture.GetFileId());
      CHECK(loaded == tc.expect_load_success);
      if (tc.expect_load_success) {
        tc.verify_func(fixture.GetLoader(), fixture.GetFileId());
      }
    }
  }
}
```

**Estimated Reduction:** ~60-80 lines

---

### Phase 4: Consolidate Concurrent Access Tests (Low Impact)

**Goal:** Reduce duplication between `GetFileSize` and `GetModificationTime` concurrent access tests

**Action:** Create template helper for concurrent access testing

**Proposed:**
```cpp
template<typename ResultType, typename GetFunc, typename VerifyFunc>
void TestConcurrentAccess(
    LazyAttributeLoader& loader,
    uint64_t file_id,
    GetFunc get_func,
    VerifyFunc verify_func,
    const std::string& test_name) {
  
  const int numThreads = 10;
  std::vector<std::thread> threads;
  std::vector<ResultType> results(numThreads);
  std::atomic<int> completed(0);
  
  for (int i = 0; i < numThreads; ++i) {
    threads.emplace_back([&loader, file_id, &results, i, &completed, get_func]() {
      results[i] = get_func(loader, file_id);
      completed.fetch_add(1);
    });
  }
  
  for (auto& t : threads) {
    t.join();
  }
  
  // Verify all results are valid and consistent
  for (int i = 0; i < numThreads; ++i) {
    verify_func(results[i]);
  }
  
  // Verify attribute was actually loaded
  ResultType final_result = get_func(loader, file_id);
  verify_func(final_result);
}
```

**Usage:**
```cpp
TEST_CASE("GetFileSize - concurrent access with double-check locking") {
  TestLazyAttributeLoaderFixture fixture;
  
  TestConcurrentAccess<uint64_t>(
    fixture.GetLoader(),
    fixture.GetFileId(),
    [](LazyAttributeLoader& l, uint64_t id) { return l.GetFileSize(id); },
    [expected = fixture.GetTempFile().expectedSize](uint64_t size) {
      CHECK(size == expected);
    },
    "GetFileSize concurrent access");
}
```

**Estimated Reduction:** ~40-50 lines

---

### Phase 5: Consolidate Optimization Verification Tests (Low Impact)

**Goal:** Reduce duplication between size/time optimization tests

**Action:** Create helper that tests both directions

**Proposed:**
```cpp
void TestOptimizationLoadsBothAttributes(
    LazyAttributeLoader& loader,
    uint64_t file_id,
    uint64_t expected_size,
    bool request_size_first) {
  
  if (request_size_first) {
    // Request size first, verify time was also loaded
    uint64_t size = loader.GetFileSize(file_id);
    CHECK(size == expected_size);
    
    FILETIME time = loader.GetModificationTime(file_id);
    CHECK(IsValidTime(time));
    CHECK(!IsSentinelTime(time));
  } else {
    // Request time first, verify size was also loaded
    FILETIME time = loader.GetModificationTime(file_id);
    CHECK(IsValidTime(time));
    
    uint64_t size = loader.GetFileSize(file_id);
    CHECK(size == expected_size);
  }
}
```

**Estimated Reduction:** ~20-30 lines

---

## Implementation Order

1. **Phase 1** (Test Fixture) - Highest impact, enables other phases
2. **Phase 2** (Test Helpers) - Medium impact, improves readability
3. **Phase 3** (Parameterized Tests) - Medium impact, consolidates similar tests
4. **Phase 4** (Concurrent Tests) - Low impact, but easy win
5. **Phase 5** (Optimization Tests) - Low impact, but easy win

---

## Expected Results

### Before Deduplication
- **Total Lines:** ~605 lines
- **Duplicate Blocks:** 96
- **Estimated Duplicate Lines:** ~300-400 lines

### After Deduplication
- **Total Lines:** ~300-350 lines (estimated 40-50% reduction)
- **Duplicate Blocks:** <10 (estimated 90% reduction)
- **Maintainability:** Significantly improved (changes in one place affect all tests)

---

## Benefits

1. **Reduced Code Duplication:** ~300-400 lines of duplicate code eliminated
2. **Improved Maintainability:** Changes to test setup only need to be made once
3. **Better Readability:** Test intent is clearer without boilerplate
4. **Easier to Add New Tests:** New tests can reuse fixtures and helpers
5. **Consistent Test Patterns:** All tests follow same structure
6. **Faster Test Development:** Less code to write for new test cases

---

## Risks and Mitigation

### Risk 1: Fixture Complexity
**Risk:** Fixture becomes too complex, making tests harder to understand  
**Mitigation:** Keep fixture simple, provide clear helper methods, document usage

### Risk 2: Test Isolation
**Risk:** Shared fixture might cause test interference  
**Mitigation:** Each test creates its own fixture instance (RAII pattern)

### Risk 3: Over-Abstraction
**Risk:** Too many helpers make tests harder to read  
**Mitigation:** Only extract patterns that appear 3+ times, keep helpers focused

### Risk 4: Breaking Existing Tests
**Risk:** Refactoring might break existing tests  
**Mitigation:** 
- Run tests after each phase
- Keep old test structure until new one is verified
- Use git to track changes

---

## Testing Strategy

1. **Run existing tests** before starting refactoring
2. **Refactor one phase at a time**
3. **Run tests after each phase** to ensure nothing broke
4. **Compare test output** before and after (should be identical)
5. **Re-run Duplo** after completion to measure improvement

---

## Success Criteria

- [ ] All existing tests still pass
- [ ] Duplicate blocks reduced from 96 to <10
- [ ] Total lines reduced by 40-50%
- [ ] Test execution time unchanged or improved
- [ ] Code is more maintainable (easier to add new tests)
- [ ] No functionality lost (all test cases still covered)

---

## Notes

- Keep `TempFileFixture` as-is (it's already well-designed)
- Consider moving `TempFileFixture` to `TestHelpers.h` if it's only used in this test file
- Parameterized tests should use `DOCTEST_SUBCASE` for proper test reporting
- All helpers should be in `test_helpers` namespace or `lazy_loader_test_helpers` sub-namespace
- Follow existing naming conventions (PascalCase for classes, snake_case for functions)

---

## Next Steps

1. Review and approve this plan
2. Implement Phase 1 (Test Fixture)
3. Run tests to verify Phase 1
4. Implement Phase 2 (Test Helpers)
5. Run tests to verify Phase 2
6. Continue with remaining phases
7. Re-run Duplo to measure improvement
8. Update `DEDUPLICATION_CANDIDATES.md` to mark as complete
