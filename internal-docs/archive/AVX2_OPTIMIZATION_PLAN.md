# AVX2 Optimization Plan for String Search Functions

## Overview

This document outlines the detailed implementation plan for optimizing string search functions using AVX2 SIMD instructions. AVX2 can process 32 bytes (256 bits) at a time, providing significant performance improvements for string comparison operations.

## Current State

### Functions to Optimize

1. **`ContainsSubstringI`** - Case-insensitive substring search (HIGHEST PRIORITY)
   - Currently uses byte-by-byte `ToLowerChar()` comparisons
   - Most expensive operation (tolower per character)
   - Best candidate for AVX2 optimization

2. **`ContainsSubstring`** - Case-sensitive substring search (MEDIUM PRIORITY)
   - Already uses optimized paths (prefix check, reverse comparison)
   - AVX2 can still help for longer strings

3. **`StrStrCaseInsensitive`** - Case-insensitive strstr (MEDIUM PRIORITY)
   - Similar to `ContainsSubstringI` but returns pointer
   - Can benefit from AVX2 for needle matching

### Current Performance Characteristics

- **Case-insensitive search**: ~10-20% slower than case-sensitive (tolower overhead)
- **Hot path**: Called millions of times during large searches
- **Bottleneck**: Character-by-character case conversion in tight loops

## AVX2 Benefits Analysis

### Why AVX2 Will Help

1. **Parallel Processing**: Process 32 bytes simultaneously instead of 1 byte
2. **Case Conversion**: AVX2 can convert 32 characters to lowercase in parallel
3. **Character Comparison**: Compare 32 characters at once
4. **Memory Bandwidth**: Better utilization of memory bandwidth

### Expected Performance Gains

- **Case-insensitive search**: 2-4x faster for strings >= 32 bytes
- **Case-sensitive search**: 1.5-2x faster for strings >= 32 bytes
- **Overall search**: 15-30% faster for typical workloads

### When AVX2 is Beneficial

- ✅ Text length >= 32 bytes (one AVX2 register)
- ✅ Pattern length >= 4 bytes (worth SIMD overhead)
- ✅ ASCII characters (fast path)
- ⚠️ Short strings (< 32 bytes): Use scalar path (overhead not worth it)
- ⚠️ Non-ASCII: Fall back to scalar path

## Implementation Strategy

### Phase 1: Infrastructure Setup

#### 1.1 CPU Feature Detection

**File**: `CpuFeatures.h` / `CpuFeatures.cpp`

```cpp
// CpuFeatures.h
#pragma once

namespace cpu_features {
    // Detect AVX2 support at runtime
    bool SupportsAVX2();
    
    // Cache result (thread-safe, initialized on first call)
    bool GetAVX2Support();
}
```

**Implementation**:
- Use CPUID instruction to check for AVX2 (bit 5 of EBX register, leaf 7)
- Cache result in static variable (thread-safe initialization)
- Return false on non-x86/x64 architectures

#### 1.2 Compile-Time Guards

**File**: `StringSearch.h`

```cpp
// Only compile AVX2 code if compiler supports it
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    #if defined(__AVX2__) || (defined(_MSC_VER) && _MSC_VER >= 1900)
        #define STRING_SEARCH_AVX2_AVAILABLE 1
    #else
        #define STRING_SEARCH_AVX2_AVAILABLE 0
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #if defined(__AVX2__)
        #define STRING_SEARCH_AVX2_AVAILABLE 1
    #else
        #define STRING_SEARCH_AVX2_AVAILABLE 0
    #endif
#else
    #define STRING_SEARCH_AVX2_AVAILABLE 0
#endif
```

### Phase 2: AVX2 Implementation

#### 2.1 Create AVX2 Helper Functions

**File**: `StringSearch.cpp` (new file, or `StringSearchAVX2.cpp`)

```cpp
// StringSearchAVX2.cpp
#include "StringSearch.h"
#include <immintrin.h>  // AVX2 intrinsics

#if STRING_SEARCH_AVX2_AVAILABLE

namespace string_search {
namespace avx2 {

// Convert 32 bytes to lowercase using AVX2
// Fast path: ASCII only (characters 0-127)
inline __m256i ToLowerAVX2(__m256i input) {
    // ASCII lowercase conversion: if char is in [A-Z] (0x41-0x5A), add 0x20
    // Create mask: input >= 'A' && input <= 'Z'
    __m256i is_upper = _mm256_and_si256(
        _mm256_cmpgt_epi8(input, _mm256_set1_epi8('A' - 1)),
        _mm256_cmpgt_epi8(_mm256_set1_epi8('Z' + 1), input)
    );
    
    // Add 0x20 to uppercase letters
    __m256i lower = _mm256_add_epi8(input, _mm256_and_si256(
        _mm256_set1_epi8(0x20), is_upper
    ));
    
    return lower;
}

// Case-insensitive comparison of 32 bytes
inline bool Compare32CaseInsensitive(const char* a, const char* b) {
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b));
    
    __m256i lower_a = ToLowerAVX2(va);
    __m256i lower_b = ToLowerAVX2(vb);
    
    __m256i cmp = _mm256_cmpeq_epi8(lower_a, lower_b);
    int mask = _mm256_movemask_epi8(cmp);
    
    return mask == 0xFFFFFFFF;  // All 32 bytes match
}

// Find first occurrence of pattern in text using AVX2
// Returns true if pattern found, false otherwise
bool ContainsSubstringIAVX2(const std::string_view& text, 
                            const std::string_view& pattern) {
    if (pattern.empty()) return true;
    if (text.length() < pattern.length()) return false;
    
    size_t text_len = text.length();
    size_t pattern_len = pattern.length();
    size_t max_start = text_len - pattern_len;
    
    const char* text_ptr = text.data();
    const char* pattern_ptr = pattern.data();
    
    // For patterns >= 32 bytes, use AVX2 for pattern matching
    if (pattern_len >= 32) {
        // Load first 32 bytes of pattern
        __m256i pattern_vec = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(pattern_ptr)
        );
        __m256i pattern_lower = ToLowerAVX2(pattern_vec);
        
        // Search through text in 32-byte chunks
        for (size_t i = 0; i <= max_start; i += 32) {
            size_t remaining = max_start - i + 1;
            if (remaining < 32) {
                // Handle remaining bytes with scalar
                break;
            }
            
            __m256i text_vec = _mm256_loadu_si256(
                reinterpret_cast<const __m256i*>(text_ptr + i)
            );
            __m256i text_lower = ToLowerAVX2(text_vec);
            
            // Compare first 32 bytes
            __m256i cmp = _mm256_cmpeq_epi8(text_lower, pattern_lower);
            int mask = _mm256_movemask_epi8(cmp);
            
            if (mask == 0xFFFFFFFF) {
                // First 32 bytes match, verify full pattern
                if (i + 32 <= max_start + pattern_len) {
                    // Check remaining pattern bytes
                    bool full_match = true;
                    for (size_t j = 32; j < pattern_len; ++j) {
                        if (ToLowerChar(text_ptr[i + j]) != 
                            ToLowerChar(pattern_ptr[j])) {
                            full_match = false;
                            break;
                        }
                    }
                    if (full_match) return true;
                }
            }
        }
    }
    
    // Fall back to scalar for remaining cases
    return false;  // Will be handled by scalar path
}

} // namespace avx2
} // namespace string_search

#endif // STRING_SEARCH_AVX2_AVAILABLE
```

#### 2.2 Integrate AVX2 into Main Functions

**File**: `StringSearch.h` (update existing functions)

```cpp
inline bool ContainsSubstringI(const std::string_view &text,
                               const std::string_view &pattern) {
    if (pattern.empty())
        return true;
    if (text.empty() || pattern.length() > text.length())
        return false;

    // AVX2 optimization: Use for longer strings
    #if STRING_SEARCH_AVX2_AVAILABLE
    if (text.length() >= 32 && pattern.length() >= 4) {
        if (cpu_features::SupportsAVX2()) {
            // Check if text contains only ASCII (fast path)
            bool is_ascii = true;
            for (size_t i = 0; i < std::min(text.length(), size_t(64)); ++i) {
                if (static_cast<unsigned char>(text[i]) > 127) {
                    is_ascii = false;
                    break;
                }
            }
            
            if (is_ascii) {
                bool result = avx2::ContainsSubstringIAVX2(text, pattern);
                if (result) return true;
                // If AVX2 didn't find it, continue to scalar path
                // (AVX2 might have false negatives at boundaries)
            }
        }
    }
    #endif

    // Existing scalar implementation (fallback and short strings)
    // ... existing code ...
}
```

### Phase 3: Testing & Validation

#### 3.1 Equivalence Tests

**File**: `tests/StringSearchTests.cpp` (add new test suite)

```cpp
TEST_SUITE("AVX2 Equivalence Tests") {
    TEST_CASE("AVX2 matches scalar results") {
        // Test that AVX2 path produces identical results to scalar
        std::vector<std::pair<std::string, std::string>> test_cases = {
            {"Hello World", "hello"},
            {"TEST STRING", "test"},
            // ... many more test cases
        };
        
        for (const auto& [text, pattern] : test_cases) {
            bool scalar_result = ContainsSubstringI_Scalar(text, pattern);
            bool avx2_result = ContainsSubstringI(text, pattern);
            REQUIRE(scalar_result == avx2_result);
        }
    }
}
```

#### 3.2 Performance Benchmarks

**File**: `tests/StringSearchBenchmarks.cpp` (optional)

```cpp
TEST_CASE("AVX2 performance benchmark") {
    std::string long_text(10000, 'a');
    std::string pattern = "test_pattern_here";
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; ++i) {
        ContainsSubstringI(long_text, pattern);
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    // Compare with scalar implementation
    // Verify AVX2 is faster
}
```

### Phase 4: Optimization & Refinement

#### 4.1 Threshold Tuning

- **Minimum text length**: Start with 32 bytes, tune based on benchmarks
- **Minimum pattern length**: Start with 4 bytes, tune based on benchmarks
- **ASCII check**: May skip for very long strings (overhead not worth it)

#### 4.2 Boundary Handling

- Handle unaligned data (use `_mm256_loadu_si256` for unaligned loads)
- Handle remaining bytes after AVX2 blocks (scalar fallback)
- Handle pattern spanning AVX2 block boundaries

#### 4.3 Advanced Optimizations

- **Pattern preprocessing**: Pre-convert pattern to lowercase once
- **Multiple pattern matching**: Use AVX2 to search for multiple patterns
- **Boyer-Moore integration**: Combine with AVX2 for very long patterns

## Implementation Steps

### Step 1: Setup Infrastructure (1-2 hours)

1. ✅ Create `CpuFeatures.h/cpp` with AVX2 detection
2. ✅ Add compile-time guards to `StringSearch.h`
3. ✅ Add runtime feature detection
4. ✅ Test CPU detection works correctly

### Step 2: Implement AVX2 Helpers (4-6 hours)

1. ✅ Create `StringSearchAVX2.cpp` with AVX2 functions
2. ✅ Implement `ToLowerAVX2()` - 32-byte lowercase conversion
3. ✅ Implement `Compare32CaseInsensitive()` - 32-byte comparison
4. ✅ Implement `ContainsSubstringIAVX2()` - main search function
5. ✅ Test AVX2 functions in isolation

### Step 3: Integration (2-3 hours)

1. ✅ Integrate AVX2 into `ContainsSubstringI()`
2. ✅ Add threshold checks (length >= 32, pattern >= 4)
3. ✅ Add ASCII detection (fast path)
4. ✅ Add fallback to scalar path
5. ✅ Ensure backward compatibility

### Step 4: Testing (2-3 hours)

1. ✅ Run all existing tests (75 test cases)
2. ✅ Add equivalence tests (AVX2 vs scalar)
3. ✅ Add boundary tests (32-byte boundaries)
4. ✅ Test on systems without AVX2 (fallback)
5. ✅ Performance benchmarks

### Step 5: Optimization (2-4 hours)

1. ✅ Tune thresholds based on benchmarks
2. ✅ Optimize boundary handling
3. ✅ Reduce ASCII check overhead
4. ✅ Profile and optimize hot paths

### Step 6: Extend to Other Functions (3-4 hours)

1. ✅ Add AVX2 to `ContainsSubstring()` (case-sensitive)
2. ✅ Add AVX2 to `StrStrCaseInsensitive()`
3. ✅ Test all functions
4. ✅ Update documentation

## File Structure

```
StringSearch.h              // Main interface (existing)
StringSearch.cpp            // AVX2 implementations (new)
StringSearchAVX2.cpp       // Alternative: separate AVX2 file
CpuFeatures.h               // CPU feature detection (new)
CpuFeatures.cpp             // CPU feature detection impl (new)
tests/
  StringSearchTests.cpp     // Unit tests (existing, add AVX2 tests)
  StringSearchBenchmarks.cpp // Performance tests (new, optional)
```

## Compilation Requirements

### Compiler Flags

**MSVC**:
```cmake
target_compile_options(string_search PRIVATE 
    /arch:AVX2  # Enable AVX2 instructions
)
```

**GCC/Clang**:
```cmake
target_compile_options(string_search PRIVATE 
    -mavx2      # Enable AVX2 instructions
)
```

### Runtime Requirements

- **CPU**: Intel Haswell (2013+) or AMD Excavator (2015+)
- **OS**: Windows 7+ (AVX2 support)
- **Fallback**: Automatic fallback to scalar on older CPUs

## Testing Strategy

### 1. Correctness Tests

- ✅ All 75 existing tests must pass
- ✅ Add AVX2-specific boundary tests
- ✅ Add equivalence tests (AVX2 == scalar)
- ✅ Test on systems with/without AVX2

### 2. Performance Tests

- ✅ Benchmark AVX2 vs scalar
- ✅ Measure improvement for different string lengths
- ✅ Verify no regression for short strings

### 3. Compatibility Tests

- ✅ Test on AVX2-capable CPUs
- ✅ Test on non-AVX2 CPUs (fallback)
- ✅ Test with various string lengths
- ✅ Test with ASCII and non-ASCII

## Success Criteria

### Performance Goals

- **Case-insensitive search**: 2-4x faster for strings >= 32 bytes
- **Case-sensitive search**: 1.5-2x faster for strings >= 32 bytes
- **No regression**: Short strings (< 32 bytes) same or faster
- **Overall**: 15-30% faster search operations

### Correctness Goals

- ✅ 100% test pass rate (75+ tests)
- ✅ Identical results to scalar implementation
- ✅ No crashes or undefined behavior
- ✅ Proper fallback on non-AVX2 systems

## Risks & Mitigations

### Risk 1: Incorrect AVX2 Implementation

**Mitigation**: 
- Comprehensive test suite (75+ tests)
- Equivalence tests comparing AVX2 vs scalar
- Code review of AVX2 intrinsics

### Risk 2: Performance Regression

**Mitigation**:
- Benchmark before/after
- Use thresholds to avoid AVX2 overhead on short strings
- Profile and optimize hot paths

### Risk 3: Compatibility Issues

**Mitigation**:
- Runtime CPU detection
- Automatic fallback to scalar
- Test on multiple CPU generations

### Risk 4: Non-ASCII Characters

**Mitigation**:
- ASCII detection before AVX2 path
- Fallback to scalar for non-ASCII
- Test with extended ASCII and UTF-8

## Timeline Estimate

- **Phase 1 (Infrastructure)**: 1-2 hours
- **Phase 2 (AVX2 Implementation)**: 4-6 hours
- **Phase 3 (Integration)**: 2-3 hours
- **Phase 4 (Testing)**: 2-3 hours
- **Phase 5 (Optimization)**: 2-4 hours
- **Phase 6 (Extension)**: 3-4 hours

**Total**: 14-22 hours (2-3 days of focused work)

## Next Steps

1. ✅ **DONE**: Extract functions to `StringSearch.h`
2. ✅ **DONE**: Create comprehensive test suite (75 tests)
3. ⏭️ **NEXT**: Implement CPU feature detection
4. ⏭️ **NEXT**: Implement AVX2 helper functions
5. ⏭️ **NEXT**: Integrate AVX2 into main functions
6. ⏭️ **NEXT**: Test and benchmark
7. ⏭️ **NEXT**: Optimize and refine

## References

- [Intel AVX2 Intrinsics Guide](https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html)
- [AVX2 Programming Reference](https://software.intel.com/sites/landingpage/IntrinsicsGuide/)
- [doctest Documentation](https://github.com/doctest/doctest) (for testing)
