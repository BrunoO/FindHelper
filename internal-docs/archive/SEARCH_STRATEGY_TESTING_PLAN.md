# Search Strategy Testing Plan

## Overview

This document outlines a detailed plan for implementing doctest unit tests to validate the three load balancing strategies (Static, Hybrid, Dynamic) used in `FileIndex::SearchAsyncWithData()`.

## Goals

1. **Correctness**: Verify each strategy returns correct search results
2. **Load Balancing**: Verify work is distributed appropriately across threads
3. **Strategy Behavior**: Verify each strategy behaves as designed (initial chunks, dynamic chunks, etc.)
4. **Edge Cases**: Test with small datasets, large datasets, single thread, many threads
5. **Settings Integration**: Verify strategy selection based on settings

---

## Test File Structure

### File: `tests/FileIndexSearchStrategyTests.cpp`

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "FileIndex.h"
#include "Settings.h"
#include <vector>
#include <string>
#include <set>
#include <algorithm>
#include <thread>
#include <chrono>

// Test helper functions and fixtures
namespace {
    // Helper functions for test data setup
    // Helper functions for result validation
    // Helper functions for timing/metrics validation
}

TEST_SUITE("FileIndex Search Strategies") {
    // Test suites for each strategy
    // Test suites for edge cases
    // Test suites for settings integration
}
```

---

## Test Data Setup

### Helper Function: `CreateTestFileIndex()`

**Purpose**: Create a `FileIndex` instance populated with controlled test data.

**Design**:
```cpp
FileIndex CreateTestFileIndex(size_t file_count, const std::string& base_path = "C:\\Test") {
    FileIndex index;
    
    // Create a hierarchy:
    // - Root directory (id=1)
    // - Files with predictable names: file_0001.txt, file_0002.txt, ...
    // - Mix of files and directories
    // - Various extensions: .txt, .cpp, .h, .exe, etc.
    
    uint64_t root_id = 1;
    index.Insert(root_id, 0, base_path, true); // Root directory
    
    for (size_t i = 1; i <= file_count; ++i) {
        uint64_t file_id = i + 1;
        bool is_dir = (i % 10 == 0); // Every 10th item is a directory
        
        std::string name;
        if (is_dir) {
            name = "dir_" + std::to_string(i);
        } else {
            // Vary extensions for testing
            std::string ext = GetExtensionForIndex(i);
            name = "file_" + PadNumber(i, 4) + ext;
        }
        
        index.Insert(file_id, root_id, name, is_dir);
    }
    
    // CRITICAL: Must call RecomputeAllPaths() to populate path arrays
    index.RecomputeAllPaths();
    
    return index;
}

// Helper: Generate extension based on index for variety
std::string GetExtensionForIndex(size_t i) {
    const char* exts[] = {".txt", ".cpp", ".h", ".exe", ".dll", ".json", ".md"};
    return exts[i % 7];
}

// Helper: Pad number with zeros
std::string PadNumber(size_t num, size_t width) {
    std::string result = std::to_string(num);
    while (result.length() < width) {
        result = "0" + result;
    }
    return result;
}
```

**Key Points**:
- Must call `RecomputeAllPaths()` after inserting all files
- Use predictable naming for easy validation
- Mix files and directories
- Vary extensions for extension filter testing

---

### Helper Function: `SetStrategyInSettings()`

**Purpose**: Temporarily set the load balancing strategy in settings for testing.

**Design**:
```cpp
class StrategySettingsGuard {
public:
    StrategySettingsGuard(const std::string& strategy) {
        AppSettings settings;
        LoadSettings(settings);
        old_strategy_ = settings.loadBalancingStrategy;
        settings.loadBalancingStrategy = strategy;
        SaveSettings(settings);
    }
    
    ~StrategySettingsGuard() {
        AppSettings settings;
        LoadSettings(settings);
        settings.loadBalancingStrategy = old_strategy_;
        SaveSettings(settings);
    }
    
private:
    std::string old_strategy_;
};
```

**Alternative (Simpler)**: Directly modify settings file or use a test-specific settings path.

---

### Helper Function: `CollectSearchResults()`

**Purpose**: Collect all results from futures returned by `SearchAsyncWithData()`.

**Design**:
```cpp
std::vector<FileIndex::SearchResultData> CollectSearchResults(
    FileIndex& index,
    const std::string& query,
    int thread_count = -1,
    const std::vector<std::string>* extensions = nullptr,
    bool folders_only = false,
    bool case_sensitive = false,
    const std::string& path_query = "",
    bool full_path_search = false) {
    
    auto futures = index.SearchAsyncWithData(
        query, thread_count, extensions, folders_only,
        case_sensitive, path_query, full_path_search);
    
    std::vector<FileIndex::SearchResultData> all_results;
    
    for (auto& future : futures) {
        auto thread_results = future.get();
        all_results.insert(all_results.end(), 
                          thread_results.begin(), 
                          thread_results.end());
    }
    
    return all_results;
}
```

---

### Helper Function: `ValidateResults()`

**Purpose**: Validate that search results match expected criteria.

**Design**:
```cpp
void ValidateResults(
    const std::vector<FileIndex::SearchResultData>& results,
    const std::string& expected_query,
    bool case_sensitive,
    const std::vector<std::string>* expected_extensions = nullptr,
    bool folders_only = false) {
    
    // 1. Check all results contain the query (case-sensitive or not)
    for (const auto& result : results) {
        std::string search_target = result.filename;
        std::string query = expected_query;
        
        if (!case_sensitive) {
            search_target = ToLower(search_target);
            query = ToLower(query);
        }
        
        REQUIRE(search_target.find(query) != std::string::npos);
        
        // 2. Check extension filter if provided
        if (expected_extensions && !expected_extensions->empty()) {
            bool matches = false;
            for (const auto& ext : *expected_extensions) {
                if (result.extension == ext || 
                    (!case_sensitive && ToLower(result.extension) == ToLower(ext))) {
                    matches = true;
                    break;
                }
            }
            REQUIRE(matches);
        }
        
        // 3. Check folders_only filter
        if (folders_only) {
            REQUIRE(result.isDirectory);
        }
    }
}
```

---

## Test Cases: Static Strategy

### Test Suite: `TEST_SUITE("Static Strategy")`

#### Test Case 1: Basic Functionality
```cpp
TEST_CASE("Static strategy returns correct results") {
    FileIndex index = CreateTestFileIndex(1000);
    SetStrategyInSettings("static");
    
    auto results = CollectSearchResults(index, "file_", 4);
    
    REQUIRE(results.size() > 0);
    ValidateResults(results, "file_", false);
    
    // Verify all results are files (not directories)
    for (const auto& result : results) {
        REQUIRE_FALSE(result.isDirectory);
    }
}
```

#### Test Case 2: Thread Distribution
```cpp
TEST_CASE("Static strategy distributes work evenly") {
    FileIndex index = CreateTestFileIndex(10000);
    SetStrategyInSettings("static");
    
    int thread_count = 4;
    auto futures = index.SearchAsyncWithData("file_", thread_count);
    
    REQUIRE(futures.size() == static_cast<size_t>(thread_count));
    
    // Collect results and verify all items are processed
    size_t total_results = 0;
    for (auto& future : futures) {
        auto thread_results = future.get();
        total_results += thread_results.size();
        REQUIRE(thread_results.size() > 0); // Each thread should have work
    }
    
    // Verify we got results from all threads
    REQUIRE(total_results > 0);
}
```

#### Test Case 3: Single Thread
```cpp
TEST_CASE("Static strategy works with single thread") {
    FileIndex index = CreateTestFileIndex(100);
    SetStrategyInSettings("static");
    
    auto results = CollectSearchResults(index, "file_", 1);
    
    REQUIRE(results.size() > 0);
    ValidateResults(results, "file_", false);
}
```

#### Test Case 4: Many Threads
```cpp
TEST_CASE("Static strategy works with many threads") {
    FileIndex index = CreateTestFileIndex(1000);
    SetStrategyInSettings("static");
    
    int thread_count = 16;
    auto results = CollectSearchResults(index, "file_", thread_count);
    
    REQUIRE(results.size() > 0);
    ValidateResults(results, "file_", false);
}
```

#### Test Case 5: Small Dataset
```cpp
TEST_CASE("Static strategy handles small datasets") {
    FileIndex index = CreateTestFileIndex(10);
    SetStrategyInSettings("static");
    
    auto results = CollectSearchResults(index, "file_", 4);
    
    // Should still work, even if some threads get no work
    ValidateResults(results, "file_", false);
}
```

---

## Test Cases: Hybrid Strategy

### Test Suite: `TEST_SUITE("Hybrid Strategy")`

#### Test Case 1: Basic Functionality
```cpp
TEST_CASE("Hybrid strategy returns correct results") {
    FileIndex index = CreateTestFileIndex(1000);
    SetStrategyInSettings("hybrid");
    
    auto results = CollectSearchResults(index, "file_", 4);
    
    REQUIRE(results.size() > 0);
    ValidateResults(results, "file_", false);
}
```

#### Test Case 2: Dynamic Chunks Are Used
```cpp
TEST_CASE("Hybrid strategy uses dynamic chunks") {
    FileIndex index = CreateTestFileIndex(10000);
    SetStrategyInSettings("hybrid");
    
    // Set hybridInitialWorkPercent to 50% to ensure dynamic chunks are available
    AppSettings settings;
    LoadSettings(settings);
    settings.hybridInitialWorkPercent = 50;
    SaveSettings(settings);
    
    auto futures = index.SearchAsyncWithData("file_", 4);
    
    // Collect results and verify dynamic chunks were processed
    // NOTE: This requires access to ThreadTiming data, which may need to be exposed
    // or we can infer from result distribution
    
    size_t total_results = 0;
    for (auto& future : futures) {
        auto thread_results = future.get();
        total_results += thread_results.size();
    }
    
    REQUIRE(total_results > 0);
}
```

#### Test Case 3: Initial Chunks Are Capped
```cpp
TEST_CASE("Hybrid strategy caps initial chunks correctly") {
    FileIndex index = CreateTestFileIndex(10000);
    SetStrategyInSettings("hybrid");
    
    // Set hybridInitialWorkPercent to 75%
    AppSettings settings;
    LoadSettings(settings);
    settings.hybridInitialWorkPercent = 75;
    SaveSettings(settings);
    
    auto futures = index.SearchAsyncWithData("file_", 4);
    
    // Verify all threads complete (no deadlock)
    for (auto& future : futures) {
        auto thread_results = future.get();
        REQUIRE(thread_results.size() >= 0); // May be empty, but should complete
    }
}
```

#### Test Case 4: Different Percentages
```cpp
TEST_CASE("Hybrid strategy respects initial work percentage") {
    FileIndex index = CreateTestFileIndex(5000);
    SetStrategyInSettings("hybrid");
    
    for (int percent : {50, 60, 70, 80, 90}) {
        AppSettings settings;
        LoadSettings(settings);
        settings.hybridInitialWorkPercent = percent;
        SaveSettings(settings);
        
        auto results = CollectSearchResults(index, "file_", 4);
        REQUIRE(results.size() > 0);
        ValidateResults(results, "file_", false);
    }
}
```

---

## Test Cases: Dynamic Strategy

### Test Suite: `TEST_SUITE("Dynamic Strategy")`

#### Test Case 1: Basic Functionality
```cpp
TEST_CASE("Dynamic strategy returns correct results") {
    FileIndex index = CreateTestFileIndex(1000);
    SetStrategyInSettings("dynamic");
    
    auto results = CollectSearchResults(index, "file_", 4);
    
    REQUIRE(results.size() > 0);
    ValidateResults(results, "file_", false);
}
```

#### Test Case 2: All Work Is Dynamic
```cpp
TEST_CASE("Dynamic strategy processes all work dynamically") {
    FileIndex index = CreateTestFileIndex(10000);
    SetStrategyInSettings("dynamic");
    
    auto futures = index.SearchAsyncWithData("file_", 4);
    
    // Verify all threads participate
    REQUIRE(futures.size() == 4);
    
    size_t total_results = 0;
    for (auto& future : futures) {
        auto thread_results = future.get();
        total_results += thread_results.size();
    }
    
    REQUIRE(total_results > 0);
}
```

#### Test Case 3: Chunk Size Configuration
```cpp
TEST_CASE("Dynamic strategy respects chunk size setting") {
    FileIndex index = CreateTestFileIndex(10000);
    SetStrategyInSettings("dynamic");
    
    // Test with different chunk sizes
    for (int chunk_size : {100, 500, 1000, 2000}) {
        AppSettings settings;
        LoadSettings(settings);
        settings.dynamicChunkSize = chunk_size;
        SaveSettings(settings);
        
        auto results = CollectSearchResults(index, "file_", 4);
        REQUIRE(results.size() > 0);
        ValidateResults(results, "file_", false);
    }
}
```

#### Test Case 4: No Results Case
```cpp
TEST_CASE("Dynamic strategy handles no matches gracefully") {
    FileIndex index = CreateTestFileIndex(1000);
    SetStrategyInSettings("dynamic");
    
    auto results = CollectSearchResults(index, "nonexistent_pattern_xyz", 4);
    
    REQUIRE(results.size() == 0);
}
```

---

## Test Cases: Strategy Comparison

### Test Suite: `TEST_SUITE("Strategy Comparison")`

#### Test Case 1: Same Results
```cpp
TEST_CASE("All strategies return same results for same query") {
    FileIndex index = CreateTestFileIndex(1000);
    std::string query = "file_";
    
    SetStrategyInSettings("static");
    auto static_results = CollectSearchResults(index, query, 4);
    std::sort(static_results.begin(), static_results.end(), 
              [](const auto& a, const auto& b) { return a.id < b.id; });
    
    SetStrategyInSettings("hybrid");
    auto hybrid_results = CollectSearchResults(index, query, 4);
    std::sort(hybrid_results.begin(), hybrid_results.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    
    SetStrategyInSettings("dynamic");
    auto dynamic_results = CollectSearchResults(index, query, 4);
    std::sort(dynamic_results.begin(), dynamic_results.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    
    // All should return same number of results
    REQUIRE(static_results.size() == hybrid_results.size());
    REQUIRE(hybrid_results.size() == dynamic_results.size());
    
    // All should return same IDs (order may differ)
    for (size_t i = 0; i < static_results.size(); ++i) {
        REQUIRE(static_results[i].id == hybrid_results[i].id);
        REQUIRE(hybrid_results[i].id == dynamic_results[i].id);
    }
}
```

---

## Test Cases: Edge Cases

### Test Suite: `TEST_SUITE("Edge Cases")`

#### Test Case 1: Empty Index
```cpp
TEST_CASE("All strategies handle empty index") {
    FileIndex index;
    index.RecomputeAllPaths();
    
    for (const char* strategy : {"static", "hybrid", "dynamic"}) {
        SetStrategyInSettings(strategy);
        auto results = CollectSearchResults(index, "anything", 4);
        REQUIRE(results.size() == 0);
    }
}
```

#### Test Case 2: Single Item
```cpp
TEST_CASE("All strategies handle single item") {
    FileIndex index = CreateTestFileIndex(1);
    
    for (const char* strategy : {"static", "hybrid", "dynamic"}) {
        SetStrategyInSettings(strategy);
        auto results = CollectSearchResults(index, "file_", 4);
        REQUIRE(results.size() <= 1);
    }
}
```

#### Test Case 3: More Threads Than Items
```cpp
TEST_CASE("All strategies handle more threads than items") {
    FileIndex index = CreateTestFileIndex(5);
    
    for (const char* strategy : {"static", "hybrid", "dynamic"}) {
        SetStrategyInSettings(strategy);
        auto results = CollectSearchResults(index, "file_", 16);
        REQUIRE(results.size() <= 5);
    }
}
```

#### Test Case 4: Extension Filter
```cpp
TEST_CASE("All strategies work with extension filter") {
    FileIndex index = CreateTestFileIndex(1000);
    std::vector<std::string> extensions = {".txt", ".cpp"};
    
    for (const char* strategy : {"static", "hybrid", "dynamic"}) {
        SetStrategyInSettings(strategy);
        auto results = CollectSearchResults(index, "file_", 4, &extensions);
        
        ValidateResults(results, "file_", false, &extensions);
    }
}
```

#### Test Case 5: Folders Only
```cpp
TEST_CASE("All strategies work with folders_only filter") {
    FileIndex index = CreateTestFileIndex(1000);
    
    for (const char* strategy : {"static", "hybrid", "dynamic"}) {
        SetStrategyInSettings(strategy);
        auto results = CollectSearchResults(index, "dir_", 4, nullptr, true);
        
        for (const auto& result : results) {
            REQUIRE(result.isDirectory);
        }
    }
}
```

---

## Test Cases: Settings Integration

### Test Suite: `TEST_SUITE("Settings Integration")`

#### Test Case 1: Strategy Selection
```cpp
TEST_CASE("Strategy selection from settings works") {
    FileIndex index = CreateTestFileIndex(1000);
    
    for (const char* strategy : {"static", "hybrid", "dynamic"}) {
        SetStrategyInSettings(strategy);
        auto results = CollectSearchResults(index, "file_", 4);
        REQUIRE(results.size() > 0);
    }
}
```

#### Test Case 2: Invalid Strategy Defaults
```cpp
TEST_CASE("Invalid strategy defaults to hybrid") {
    FileIndex index = CreateTestFileIndex(1000);
    
    // Set invalid strategy
    AppSettings settings;
    LoadSettings(settings);
    settings.loadBalancingStrategy = "invalid_strategy";
    SaveSettings(settings);
    
    // Should default to hybrid and still work
    auto results = CollectSearchResults(index, "file_", 4);
    REQUIRE(results.size() > 0);
}
```

#### Test Case 3: Thread Pool Size
```cpp
TEST_CASE("Thread pool size setting is respected") {
    FileIndex index = CreateTestFileIndex(1000);
    SetStrategyInSettings("hybrid");
    
    for (int pool_size : {0, 2, 4, 8}) {
        AppSettings settings;
        LoadSettings(settings);
        settings.searchThreadPoolSize = pool_size;
        SaveSettings(settings);
        
        // Note: Thread pool is singleton, so we may need to reset it
        // or test in separate processes
        auto results = CollectSearchResults(index, "file_", 4);
        REQUIRE(results.size() > 0);
    }
}
```

---

## Implementation Challenges and Solutions

### Challenge 1: Thread Pool Singleton

**Problem**: `SearchThreadPool` is a singleton, so tests may interfere with each other.

**Solutions**:
1. **Reset between tests**: Add a `Reset()` method to `FileIndex` to clear the thread pool
2. **Separate test processes**: Run each test in a separate process (doctest supports this)
3. **Mock thread pool**: Create a test-only thread pool implementation

**Recommended**: Option 1 (Reset method) for simplicity.

### Challenge 2: Settings File Persistence

**Problem**: Tests modify settings file, which may affect other tests or the main application.

**Solutions**:
1. **Test-specific settings file**: Use a separate settings file for tests
2. **Restore after test**: Use RAII guard to restore original settings
3. **In-memory settings**: Add a test-only mode that doesn't write to disk

**Recommended**: Option 2 (RAII guard) for isolation.

### Challenge 3: Thread Timing Data

**Problem**: `ThreadTiming` data is not exposed, making it hard to verify load balancing.

**Solutions**:
1. **Expose timing data**: Add a method to retrieve `ThreadTiming` data after search
2. **Log-based validation**: Parse logs to verify behavior (fragile)
3. **Infer from results**: Use result distribution to infer behavior (less precise)

**Recommended**: Option 1 (expose timing data) for accurate testing.

### Challenge 4: Deterministic Results

**Problem**: Thread scheduling is non-deterministic, making exact timing/ordering tests difficult.

**Solutions**:
1. **Focus on correctness**: Test that results are correct, not exact timing
2. **Statistical validation**: Run multiple times and check averages
3. **Bounds checking**: Verify timing/metrics are within reasonable bounds

**Recommended**: Option 1 (correctness focus) with Option 3 (bounds) for metrics.

### Challenge 5: Windows Dependencies

**Problem**: `FileIndex` uses Windows-specific types (FILETIME, etc.).

**Solutions**:
1. **Mock Windows types**: Define minimal Windows types for tests
2. **Conditional compilation**: Use `#ifdef` to provide test-only implementations
3. **Test on Windows only**: Accept that tests must run on Windows

**Recommended**: Option 3 (Windows only) since this is a Windows-specific project.

---

## Test Execution Plan

### Phase 1: Basic Infrastructure (Week 1)
1. Create `tests/FileIndexSearchStrategyTests.cpp`
2. Implement helper functions:
   - `CreateTestFileIndex()`
   - `CollectSearchResults()`
   - `ValidateResults()`
   - `SetStrategyInSettings()` / `StrategySettingsGuard`
3. Add basic Static strategy test
4. Verify test compiles and runs

### Phase 2: Static Strategy Tests (Week 1)
1. Implement all Static strategy test cases
2. Verify correctness
3. Fix any issues found

### Phase 3: Hybrid Strategy Tests (Week 2)
1. Implement all Hybrid strategy test cases
2. Add timing data exposure if needed
3. Verify dynamic chunks are used
4. Fix any issues found

### Phase 4: Dynamic Strategy Tests (Week 2)
1. Implement all Dynamic strategy test cases
2. Verify all work is processed
3. Fix any issues found

### Phase 5: Comparison and Edge Cases (Week 3)
1. Implement strategy comparison tests
2. Implement edge case tests
3. Implement settings integration tests
4. Comprehensive validation

### Phase 6: Integration and Documentation (Week 3)
1. Add tests to CMakeLists.txt
2. Update documentation
3. Run full test suite
4. Address any remaining issues

---

## CMakeLists.txt Integration

Add to `CMakeLists.txt` in the test section:

```cmake
# FileIndex search strategy tests
add_executable(file_index_search_strategy_tests
    tests/FileIndexSearchStrategyTests.cpp
    FileIndex.cpp
    Settings.cpp
    SearchThreadPool.cpp
    # ... other dependencies
)

target_include_directories(file_index_search_strategy_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DOCTEST_DIR}
)

target_compile_features(file_index_search_strategy_tests PRIVATE cxx_std_17)

# Windows-specific includes and libraries
if(WIN32)
    target_compile_definitions(file_index_search_strategy_tests PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

add_test(NAME FileIndexSearchStrategyTests COMMAND file_index_search_strategy_tests)
```

---

## Success Criteria

1. ✅ All three strategies return correct results
2. ✅ All strategies handle edge cases (empty, single item, many threads)
3. ✅ Hybrid strategy uses dynamic chunks when configured
4. ✅ Dynamic strategy processes all work
5. ✅ Settings integration works correctly
6. ✅ Tests run reliably and deterministically
7. ✅ Tests complete in reasonable time (< 10 seconds total)
8. ✅ Tests are maintainable and well-documented

---

## Future Enhancements

1. **Performance Benchmarks**: Add timing tests to compare strategy performance
2. **Load Balancing Metrics**: Add tests to verify load balancing effectiveness
3. **Stress Tests**: Test with very large datasets (100k+ files)
4. **Concurrency Tests**: Test with concurrent searches
5. **Memory Leak Tests**: Verify no memory leaks during searches

---

## Notes

- Tests must run on Windows (due to Windows-specific dependencies)
- Thread pool singleton requires careful test isolation
- Settings file modifications should be isolated per test
- Focus on correctness over exact timing/ordering
- Use doctest's `SUBCASE` for parameterized tests if needed
