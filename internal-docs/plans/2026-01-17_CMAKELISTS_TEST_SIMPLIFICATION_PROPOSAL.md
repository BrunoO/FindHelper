# CMakeLists.txt Test Simplification Proposal

**Date:** 2026-01-17  
**Status:** Proposal

## Problem

Currently, VectorScan configuration (include directories, compile definitions, library linking) is duplicated across 12+ test targets. This creates:
- **Maintenance burden**: Adding new dependencies requires updating many targets
- **Error-prone**: Easy to miss a target when adding configuration
- **Verbose**: CMakeLists.txt has ~200 lines of repetitive VectorScan configuration

## Current Approach

Each test target manually includes:
```cmake
# Add VectorScan include directory if available
if(VECTORSCAN_AVAILABLE)
    if(VECTORSCAN_INCLUDE_DIR)
        target_include_directories(test_target PRIVATE ${VECTORSCAN_INCLUDE_DIR})
    elseif(VECTORSCAN_INCLUDE_DIRS)
        if(NOT "${VECTORSCAN_INCLUDE_DIRS}" STREQUAL "")
            target_include_directories(test_target PRIVATE ${VECTORSCAN_INCLUDE_DIRS})
        endif()
    endif()
endif()

# Define USE_VECTORSCAN if VectorScan is available
if(VECTORSCAN_AVAILABLE)
    target_compile_definitions(test_target PRIVATE USE_VECTORSCAN)
    
    # Link VectorScan library
    if(VECTORSCAN_TARGET)
        target_link_libraries(test_target PRIVATE ${VECTORSCAN_TARGET})
    elseif(VECTORSCAN_LIB AND VECTORSCAN_INCLUDE_DIR AND EXISTS "${VECTORSCAN_LIB}")
        target_link_libraries(test_target PRIVATE ${VECTORSCAN_LIB})
    elseif(VECTORSCAN_LIBRARIES AND VECTORSCAN_INCLUDE_DIRS)
        if(NOT "${VECTORSCAN_LIBRARIES}" STREQUAL "")
            target_link_libraries(test_target PRIVATE ${VECTORSCAN_LIBRARIES})
        endif()
    endif()
endif()
```

And each test target manually lists `src/utils/VectorScanUtils.cpp` in its sources.

## Proposed Solution

### 1. Add VectorScanUtils.cpp to Object Library

Add `VectorScanUtils.cpp` to `test_utils_obj` (or create `test_vectorscan_obj` if it needs different flags):

```cmake
# Add VectorScanUtils to test_utils_obj
add_library(test_utils_obj OBJECT
    src/utils/CpuFeatures.cpp
    src/utils/StringSearchAVX2.cpp
    src/utils/VectorScanUtils.cpp  # Add here
)
```

**Benefits:**
- Compiles once, reused by all tests
- Linker removes unused code automatically (with `--gc-sections` or `/OPT:REF`)
- No need to list it in each test target

### 2. Create Helper Function for VectorScan Configuration

```cmake
# Helper function to apply VectorScan configuration to a test target
function(configure_vectorscan_for_test target_name)
    if(VECTORSCAN_AVAILABLE)
        # Add include directory
        if(VECTORSCAN_INCLUDE_DIR)
            target_include_directories(${target_name} PRIVATE ${VECTORSCAN_INCLUDE_DIR})
        elseif(VECTORSCAN_INCLUDE_DIRS)
            if(NOT "${VECTORSCAN_INCLUDE_DIRS}" STREQUAL "")
                target_include_directories(${target_name} PRIVATE ${VECTORSCAN_INCLUDE_DIRS})
            endif()
        endif()
        
        # Add compile definition
        target_compile_definitions(${target_name} PRIVATE USE_VECTORSCAN)
        
        # Link library
        if(VECTORSCAN_TARGET)
            target_link_libraries(${target_name} PRIVATE ${VECTORSCAN_TARGET})
        elseif(VECTORSCAN_LIB AND VECTORSCAN_INCLUDE_DIR AND EXISTS "${VECTORSCAN_LIB}")
            target_link_libraries(${target_name} PRIVATE ${VECTORSCAN_LIB})
        elseif(VECTORSCAN_LIBRARIES AND VECTORSCAN_INCLUDE_DIRS)
            if(NOT "${VECTORSCAN_LIBRARIES}" STREQUAL "")
                target_link_libraries(${target_name} PRIVATE ${VECTORSCAN_LIBRARIES})
            endif()
        endif()
    endif()
endfunction()
```

### 3. Simplify Test Target Configuration

Replace all the repetitive code with a single function call:

```cmake
add_executable(parallel_search_engine_tests
    tests/ParallelSearchEngineTests.cpp
    tests/TestHelpers.cpp
    src/search/ParallelSearchEngine.cpp
    # ... other sources ...
    $<TARGET_OBJECTS:test_utils_obj>  # Includes VectorScanUtils.cpp
)

target_include_directories(parallel_search_engine_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DOCTEST_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/external/nlohmann_json/single_include
)

target_compile_features(parallel_search_engine_tests PRIVATE cxx_std_17)

# Single line to configure VectorScan!
configure_vectorscan_for_test(parallel_search_engine_tests)
```

## Benefits

1. **Reduced duplication**: ~200 lines → ~30 lines (helper function)
2. **Easier maintenance**: Add new dependencies in one place
3. **Less error-prone**: Can't forget to add configuration to a target
4. **Faster builds**: Object library compiles once, reused everywhere
5. **Dead code elimination**: Linker removes unused VectorScan code automatically

## Dead Code Elimination

The linker will automatically remove unused code when:
- **Linux**: `-Wl,--gc-sections` (already enabled)
- **Windows**: `/OPT:REF /OPT:ICF` (already enabled)
- **macOS**: Not supported by `ld`, but unused symbols are still removed

**Verification**: Test executables that don't use VectorScan will have zero VectorScan code in the final binary.

## Implementation Steps

1. Add `VectorScanUtils.cpp` to `test_utils_obj` object library
2. Create `configure_vectorscan_for_test()` helper function
3. Replace all manual VectorScan configuration with function calls
4. Remove `src/utils/VectorScanUtils.cpp` from individual test target source lists
5. Test that all tests still build and pass
6. Verify dead code elimination (check binary sizes)

## Alternative: Common Sources List

Instead of object library, we could create a common sources list:

```cmake
set(COMMON_TEST_SOURCES
    src/utils/VectorScanUtils.cpp
    # Add other common sources here
)

# Then in each test:
add_executable(test_target
    tests/TestTarget.cpp
    ${COMMON_TEST_SOURCES}
    # ... other sources ...
)
```

**Pros:**
- Simpler (no object library)
- Works on all platforms

**Cons:**
- Compiles multiple times (slower builds)
- Object library approach is already established in the project

## Recommendation

**Use object library approach** because:
1. Project already uses object libraries (`test_utils_obj`, `test_path_obj`, etc.)
2. Faster incremental builds (compile once, reuse)
3. Consistent with existing patterns
4. Dead code elimination works perfectly

## Testing

After implementation:
1. Run `scripts/build_tests_macos.sh` - should build successfully
2. Run all tests - should pass
3. Check binary sizes - tests not using VectorScan should be same size or smaller
4. Verify VectorScan still works in tests that use it
