# DirectoryResolverTests.cpp Duplication Analysis

## Summary

**Status:** ⚠️ **Significant duplication detected**

DirectoryResolverTests.cpp has **high duplication** in test setup code. Every test case repeats the same 12-line setup pattern.

## Duplication Pattern

### Repeated Setup Code (12 lines, repeated 5 times)

Every test case contains this identical setup:

```cpp
std::shared_mutex mutex;
FileIndexStorage storage(mutex);
PathStorage path_storage;
PathOperations path_operations(path_storage);
std::atomic<size_t> remove_not_in_index_count{0};
std::atomic<size_t> remove_duplicate_count{0};
std::atomic<size_t> remove_inconsistency_count{0};
IndexOperations operations(storage, path_operations, mutex,
                          remove_not_in_index_count,
                          remove_duplicate_count,
                          remove_inconsistency_count);
std::atomic<uint64_t> next_file_id{1};
DirectoryResolver resolver(storage, operations, next_file_id);
std::unique_lock lock(mutex);
```

**Occurrences:** 5 test cases × 12 lines = **60 lines of duplicated code**

## Test Cases Affected

1. `DirectoryResolver::GetOrCreateDirectoryId returns 0 for empty path` (lines 12-32)
2. `DirectoryResolver::GetOrCreateDirectoryId creates single directory` (lines 34-66)
3. `DirectoryResolver::GetOrCreateDirectoryId creates nested directories recursively` (lines 68-119)
4. `DirectoryResolver::GetOrCreateDirectoryId uses cache for existing directories` (lines 121-152)
5. `DirectoryResolver::GetOrCreateDirectoryId handles Windows-style paths` (lines 154-184)
6. `DirectoryResolver::GetOrCreateDirectoryId handles Unix-style paths` (lines 186-216)

## Duplo Analysis

**Duplo Status:** Not detected in current report (likely because duplication is < 15 lines per block, which is duplo's minimum threshold)

**SonarQube Status:** 0 issues (no duplication rule violations)

However, the duplication is **clearly visible** in the code and follows the same pattern we've been refactoring.

## Recommended Solution

Create a **TestDirectoryResolverFixture** in `TestHelpers.h`:

```cpp
class TestDirectoryResolverFixture {
public:
  TestDirectoryResolverFixture();
  
  DirectoryResolver& GetResolver() { return resolver_; }
  FileIndexStorage& GetStorage() { return storage_; }
  // ... other getters as needed
  
private:
  std::shared_mutex mutex_;
  FileIndexStorage storage_;
  PathStorage path_storage_;
  PathOperations path_operations_;
  std::atomic<size_t> remove_not_in_index_count_;
  std::atomic<size_t> remove_duplicate_count_;
  std::atomic<size_t> remove_inconsistency_count_;
  IndexOperations operations_;
  std::atomic<uint64_t> next_file_id_;
  DirectoryResolver resolver_;
  std::unique_lock<std::shared_mutex> lock_;
};
```

**Before:**
```cpp
TEST_CASE("DirectoryResolver::GetOrCreateDirectoryId returns 0 for empty path") {
  std::shared_mutex mutex;
  FileIndexStorage storage(mutex);
  // ... 10 more lines of setup ...
  std::unique_lock lock(mutex);
  
  uint64_t root_id = resolver.GetOrCreateDirectoryId("");
  CHECK(root_id == 0);
}
```

**After:**
```cpp
TEST_CASE("DirectoryResolver::GetOrCreateDirectoryId returns 0 for empty path") {
  test_helpers::TestDirectoryResolverFixture fixture;
  
  uint64_t root_id = fixture.GetResolver().GetOrCreateDirectoryId("");
  CHECK(root_id == 0);
}
```

## Impact

- **Code reduction:** ~60 lines → ~5 lines per test (92% reduction in setup code)
- **Maintainability:** Setup changes only need to be made in one place
- **Readability:** Test intent is clearer without boilerplate
- **Consistency:** All tests use same setup pattern

## Priority

**Medium Priority** - This is a good candidate for Phase 5 or future refactoring:
- Clear duplication pattern
- Easy to extract into fixture
- Significant code reduction
- Not detected by duplo (below threshold) but clearly visible

## Notes

- The duplication is below duplo's 15-line minimum threshold, so it wasn't flagged
- SonarQube doesn't flag this as duplication (likely because it's test setup code)
- However, it's the same type of duplication we've been eliminating in other test files
- This would be a good candidate for a future phase of deduplication
