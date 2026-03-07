# Hyperscan Feasibility Study: Replacing std::regex

## Executive Summary

This document analyzes the feasibility of replacing `std::regex` with **Hyperscan** for high-performance regular expression matching in the USN_WINDOWS project.

**Key Finding:** Hyperscan could provide significant performance improvements (potentially 10-100x faster) for regex matching, especially when matching against many files. Hyperscan is BSD-3-Clause licensed (fully open-source). Integration requires careful consideration of API differences and the library's design philosophy.

---

## Current State: std::regex Usage

### Implementation Overview

The project currently uses `std::regex` through `StdRegexUtils.h`:

- **Location**: `StdRegexUtils.h` (wrapper around `std::regex`)
- **Pattern Prefix**: `rs:` (e.g., `rs:.*\.cpp$`)
- **Features**: ECMAScript regex syntax, case-sensitive/insensitive matching
- **Optimization**: Routes simple patterns to `SimpleRegex.h` (Rob Pike's algorithm) for better performance
- **Caching**: Thread-safe regex cache to avoid recompilation overhead

### Usage Points

1. **FileIndex.cpp**: Filename and path matching during file searches
2. **SearchWorker.cpp**: Content matching during file content searches
3. **SearchPatternUtils.h**: Pattern matching utilities

### Current Performance Characteristics

- **std::regex** is known to be slow (backtracking engine, no SIMD optimizations)
- Simple patterns are already optimized by routing to `SimpleRegex.h`
- Complex patterns (character classes, quantifiers, etc.) still use `std::regex`
- Regex compilation is cached to reduce overhead

---

## What is Hyperscan?

### Overview

**Hyperscan** is a high-performance regex matching library developed by Intel, designed for:
- **Simultaneous matching** of large sets of patterns
- **Streaming/block-based** matching across data streams
- **SIMD optimizations** (SSSE3, SSE4.2, AVX2) for x86 architectures
- **PCRE-compatible** syntax (with some limitations)

### Key Features

1. **Compile Database**: Patterns are compiled into a database that can match multiple patterns simultaneously
2. **Scratch Space**: Requires pre-allocated scratch space for matching operations
3. **Callback-Based**: Matches are reported via callbacks
4. **Block Mode**: Optimized for block-based matching (better than streaming for most use cases)
5. **Thread Safety**: Scratch space is per-thread, but databases can be shared

### Licensing Considerations

? **Hyperscan is BSD-3-Clause licensed** - Fully open-source and free to use.

According to the [Intel Hyperscan GitHub repository](https://github.com/intel/hyperscan):
- **License**: BSD-3-Clause (see LICENSE file in repository)
- **Status**: Open-source, actively maintained by Intel
- **Latest Release**: 5.4.2 (as of April 2023)

**Options:**
1. **Intel Hyperscan** - Official BSD-3-Clause licensed version (recommended)
2. **Vectorscan** - Open-source fork maintained by VectorCamp, also BSD licensed (alternative if Intel version has issues)

**Recommendation**: Use **Intel Hyperscan** directly from the official repository - it's fully open-source under BSD-3-Clause license.

---

## Performance Comparison

### Expected Performance Gains

Hyperscan can be **10-100x faster** than `std::regex` for:
- Multiple pattern matching (compiled database)
- Long text matching (SIMD optimizations)
- High-throughput scenarios (file indexing, content search)

### Performance Characteristics

| Aspect | std::regex | Hyperscan |
|--------|------------|----------------------|
| **Compilation** | Slow (cached) | Fast (database compilation) |
| **Matching Speed** | Slow (backtracking) | Very Fast (DFA/NFA hybrid, SIMD) |
| **Memory Usage** | Low | Higher (scratch space + database) |
| **Multi-pattern** | Sequential | Simultaneous (database) |
| **SIMD Support** | No | Yes (SSE4.2, AVX2) |

### Real-World Impact

For this project:
- **FileIndex searches**: Could see 5-20x speedup for regex filename/path matching
- **Content searches**: Could see 10-50x speedup for regex content matching
- **Multiple patterns**: Hyperscan's database approach excels when matching multiple patterns

---

## API Comparison

### Current std::regex API

```cpp
// Current usage in StdRegexUtils.h
bool RegexMatch(const std::string& pattern, const std::string& text, bool case_sensitive);

// Usage
if (std_regex_utils::RegexMatch(pattern, filename, case_sensitive)) {
  // match found
}
```

### Hyperscan API (C API, usable from C++)

```cpp
// Hyperscan requires:
// 1. Compile pattern into database
hs_database_t* database;
hs_compile_error_t* compile_err;
hs_error_t err = hs_compile(pattern, flags, mode, platform, &database, &compile_err);

// 2. Allocate scratch space (per-thread)
hs_scratch_t* scratch = nullptr;
hs_alloc_scratch(database, &scratch);

// 3. Scan text
int onMatch(unsigned int id, unsigned long long from, unsigned long long to,
            unsigned int flags, void* context) {
  // Match callback
  return 0; // Continue matching
}
hs_scan(database, text, text_len, 0, scratch, onMatch, context);

// 4. Cleanup
hs_free_scratch(scratch);
hs_free_database(database);
```

### Key Differences

1. **Compilation Model**: Hyperscan requires explicit compilation (like current cache, but more complex)
2. **Scratch Space**: Per-thread scratch space allocation (memory overhead)
3. **Callback-Based**: Matches reported via callbacks (different from boolean return)
4. **Database Sharing**: Can compile multiple patterns into one database (powerful feature)
5. **Error Handling**: More complex error handling (compile errors, scan errors)

---

## Integration Challenges

### 1. API Complexity

**Challenge**: Hyperscan's API is more complex than `std::regex`'s simple boolean match.

**Solution**: Create a wrapper similar to `StdRegexUtils.h` that:
- Hides Hyperscan complexity
- Maintains same interface (`RegexMatch(pattern, text, case_sensitive)`)
- Manages database compilation and caching
- Handles scratch space allocation (per-thread)

### 2. Pattern Syntax Differences

**Challenge**: Hyperscan uses PCRE syntax, which is mostly compatible with ECMAScript, but has significant limitations:
- **Lookahead/lookbehind are NOT supported** (critical for some patterns)
- **Backreferences are NOT supported**
- **Capture groups are ignored** (grouping works, but captures aren't accessible)
- Various other advanced features are unsupported

**Solution**: 
- Test pattern compatibility
- Document limitations
- **Implement automatic fallback to `std::regex` for unsupported patterns**
- Detect unsupported patterns at compile time (when possible) or handle compile errors gracefully

### 3. Memory Management

**Challenge**: 
- Scratch space must be allocated per-thread
- Database compilation has memory overhead
- Need to manage lifecycle of databases and scratch space

**Solution**:
- Use thread-local storage for scratch space
- Cache compiled databases (similar to current regex cache)
- Consider memory limits (Hyperscan can use significant memory for complex patterns)

### 4. Error Handling

**Challenge**: Hyperscan has more complex error reporting (compile errors, scan errors).

**Solution**: Wrap errors and convert to simple boolean return (match/no-match), log errors appropriately.

### 5. Single Pattern vs. Database

**Challenge**: Hyperscan is optimized for matching multiple patterns simultaneously, but current usage is single-pattern.

**Solution**: 
- For single patterns: Use `hs_compile` with single pattern
- For future: Consider database approach if multiple patterns need matching
- Current single-pattern usage still benefits from SIMD optimizations

### 6. Case Sensitivity

**Challenge**: Hyperscan handles case sensitivity via flags during compilation.

**Solution**: 
- Compile separate databases for case-sensitive and case-insensitive
- Cache both versions (similar to current approach)

---

## Patterns Requiring Fallback to std::regex

Hyperscan does **not support** several regex features that `std::regex` (ECMAScript) supports. Patterns using these features must fall back to `std::regex`.

### Unsupported Features in Hyperscan

#### 1. **Lookahead and Lookbehind Assertions** ?? **CRITICAL**

**Not Supported:**
According to the [Hyperscan documentation](https://intel.github.io/hyperscan/dev-reference/compilation.html), Hyperscan does not support "Arbitrary zero-width assertions", which includes:
- Positive lookahead: `(?=...)`
- Negative lookahead: `(?!...)`
- Positive lookbehind: `(?<=...)`
- Negative lookbehind: `(?<!...)`

**Note**: Some zero-width assertions ARE supported (e.g., `\b`, `\B` for word boundaries), but lookahead/lookbehind are specifically not supported.

**Example from Codebase:**
```cpp
// main_gui.cpp line 1867 - "DoesNotContain" template
"^(?!.*" + text + ").*$"  // ? Uses negative lookahead - won't compile with Hyperscan
```

**Why it fails**: The pattern `^(?!.*text).*$` uses negative lookahead `(?!...)` to assert that `.*text` does NOT appear. Since Hyperscan doesn't support lookahead assertions, this pattern will fail to compile with a `HS_COMPILER_ERROR`.

**Alternative Solutions for "DoesNotContain":**

Since Hyperscan can't express "does not contain" directly via regex, we have several options:

1. **Application-level filtering** (Recommended for Hyperscan):
   ```cpp
   // Instead of regex pattern, use substring search + negation
   bool DoesNotContain(const std::string& text, const std::string& search) {
     return text.find(search) == std::string::npos;
   }
   ```

2. **Match everything, filter results**:
   ```cpp
   // Match all files, then filter out those containing the text
   // Less efficient but works with Hyperscan
   ```

3. **Fallback to std::regex** (Current approach):
   ```cpp
   // For "DoesNotContain" patterns, automatically use std::regex
   if (pattern_uses_lookahead) {
     return std_regex_utils::RegexMatch(pattern, text, case_sensitive);
   }
   ```

**Recommendation**: For the "DoesNotContain" template specifically, consider implementing it as a special case that uses substring search (option 1) rather than regex, which would be:
- **Faster** than regex (even Hyperscan)
- **Simpler** (no regex compilation needed)
- **Works with all engines** (no fallback complexity)

#### 2. **Backreferences**

**Not Supported:**
- `\1`, `\2`, etc. (referencing previous capture groups)
- `\k<name>` (named backreferences)

**Example:**
```cpp
// Match repeated words
"(\\w+)\\s+\\1"  // ? Requires fallback
```

**Impact**: Low - backreferences are rarely used in file search patterns.

#### 3. **Capture Groups (Functional)**

**Partially Supported:**
- Parentheses `()` work for grouping
- **But**: Captured content is **not accessible** (Hyperscan ignores captures)

**Example:**
```cpp
// This works for matching, but you can't extract the captured parts
"(\\d{4})-(\\d{2})-(\\d{2})"  // ? Matches, but captures ignored
```

**Impact**: **Low for this project** - The codebase only uses `regex_search` for boolean matching, never extracts capture groups (see `CAPTURE_GROUPS_EXPLANATION.md`). The `StdRegexUtils.h` uses `nosubs` flag, indicating captures aren't needed.

#### 4. **Atomic Grouping and Possessive Quantifiers**

**Not Supported:**
- Atomic groups: `(?>...)`
- Possessive quantifiers: `*+`, `++`, `?+`, `{n,m}+`

**Example:**
```cpp
"(?>a|b)*+c"  // ? Requires fallback
```

**Impact**: Low - rarely used in typical file search patterns.

#### 5. **Conditional Patterns**

**Not Supported:**
- `(?(condition)yes|no)` - conditional expressions

**Example:**
```cpp
"(?(?=.*\\.cpp$)\\.cpp|\\.h)"  // ? Requires fallback
```

**Impact**: Low - rarely used.

#### 6. **Subroutine References and Recursive Patterns**

**Not Supported:**
- `(?R)` - recursive patterns
- `(?1)`, `(?2)` - subroutine references

**Example:**
```cpp
"((?R)|.)*"  // ? Requires fallback
```

**Impact**: Very low - advanced feature, unlikely in file search.

#### 7. **Special Escape Sequences**

**Not Supported:**
- `\C` - match single byte (even in UTF-8)
- `\R` - match any Unicode newline
- `\K` - reset start of match

**Impact**: Low - rarely used.

#### 8. **Embedded Anchors**

**Partially Supported:**
- `^` and `$` only at start/end of pattern ?
- Embedded anchors within pattern ?

**Example:**
```cpp
"^start.*middle$.*end$"  // ? Embedded $ requires fallback
```

**Impact**: Low - most patterns use anchors only at boundaries.

#### 9. **Bounded Repeats Limitations**

**Partially Supported:**
- Small bounded repeats: `{n,m}` ?
- Very large bounds (>32,767): ? May fail compilation
- Large finite bounds on complex patterns: ? May cause "Pattern too large" error

**Example:**
```cpp
".{1,50000}"  // ?? May fail or be slow
```

**Impact**: Medium - depends on pattern complexity.

#### 10. **Lazy Quantifiers (Semantic Difference)**

**Partially Supported:**
- Syntax recognized: `*?`, `+?`, `??` ?
- **But**: Treated same as greedy (reports all matches)

**Example:**
```cpp
".*?text"  // ? Works, but semantics differ from std::regex
```

**Impact**: Low - for boolean matching (match/no-match), the difference is usually irrelevant.

### Patterns Currently Used in Codebase

Based on code analysis:

1. **Simple patterns** (? Hyperscan compatible):
   - `.*\.cpp$` - File extension matching
   - `^test.*` - Starts with
   - `.*\.(cpp|h|hpp)$` - Multiple extensions

2. **Complex patterns** (? Hyperscan compatible):
   - `.*\d+.*` - Contains digits
   - `.*\d{4}[-_]\d{2}[-_]\d{2}.*` - Date patterns

3. **Requires fallback** (? Not Hyperscan compatible):
   - `^(?!.*text).*$` - **DoesNotContain template** (negative lookahead)

### Fallback Strategy

**Recommended Approach:**

1. **Try Hyperscan compilation first**:
   ```cpp
   hs_error_t err = hs_compile(pattern, flags, mode, platform, &database, &compile_err);
   ```

2. **On compilation failure**, check error type:
   - If `HS_COMPILER_ERROR` with unsupported feature ? fallback to `std::regex`
   - If other error (invalid syntax, etc.) ? return false (invalid pattern)

3. **Pattern detection** (optional optimization):
   - Pre-scan pattern for unsupported constructs (lookahead, lookbehind, backreferences)
   - Route directly to `std::regex` if detected
   - This avoids compilation attempt overhead

**Implementation Example:**
```cpp
namespace hyperscan_utils {
  bool RegexMatch(const std::string& pattern, const std::string& text, bool case_sensitive) {
    // Quick check for known unsupported features
    if (ContainsUnsupportedFeatures(pattern)) {
      return std_regex_utils::RegexMatch(pattern, text, case_sensitive);
    }
    
    // Try Hyperscan
    hs_database_t* db = GetOrCompileDatabase(pattern, case_sensitive);
    if (db == nullptr) {
      // Compilation failed - fallback to std::regex
      return std_regex_utils::RegexMatch(pattern, text, case_sensitive);
    }
    
    // Use Hyperscan for matching
    return HyperscanMatch(db, text);
  }
  
  bool ContainsUnsupportedFeatures(const std::string& pattern) {
    // Check for lookahead/lookbehind
    if (pattern.find("(?=") != std::string::npos ||  // positive lookahead
        pattern.find("(?!") != std::string::npos ||  // negative lookahead
        pattern.find("(?<=") != std::string::npos || // positive lookbehind
        pattern.find("(?<!") != std::string::npos) { // negative lookbehind
      return true;
    }
    
    // Check for backreferences (simple check)
    // More complex patterns would need regex to detect properly
    // For now, let compilation fail and fallback
    
    return false;
  }
}
```

### Impact Assessment

**High Impact Patterns** (require fallback):
- **DoesNotContain template**: Uses negative lookahead - **currently used in GUI**

**Low Impact Patterns** (rarely used):
- Backreferences, atomic groups, conditionals, recursive patterns

**No Impact** (already compatible):
- Most file search patterns (extensions, prefixes, character classes, quantifiers)

**Conclusion**: The fallback mechanism is **essential** but will be used **infrequently** (primarily for the DoesNotContain template). Most real-world file search patterns are Hyperscan-compatible.

---

## Wrapper Implementation Strategy

To minimize installation dependencies, we recommend implementing a **custom minimal wrapper** directly in the project rather than adding external wrapper dependencies.

### Why Custom Wrapper?

**Benefits:**
- **No external dependencies**: Code lives in the project, no submodules or external packages needed
- **Minimal interface**: Only implement what's needed for `StdRegexUtils.h` compatibility
- **Full control**: Can optimize for specific use cases
- **Simpler build**: No need to integrate wrapper's build system

**Note**: Hyperscan library itself still needs to be integrated (see Build System Integration section), but the wrapper code can be self-contained in the project.

---

## Proposed Integration Approach

### Phase 1: Custom Wrapper Implementation (Recommended)

Create a minimal custom wrapper `HyperscanUtils.h` directly in the project to minimize dependencies:

```cpp
namespace hyperscan_utils {
  // Match pattern against text using Hyperscan
  // Returns false on invalid pattern or no match
  bool RegexMatch(const std::string& pattern, const std::string& text, bool case_sensitive = true);
  
  // Clear the database cache
  void ClearCache();
  
  // Get cache size
  size_t GetCacheSize();
}
```

**Implementation Details:**
- **Thread-safe database cache**: Similar to current `StdRegexUtils.h` regex cache
- **Thread-local scratch space**: Allocate once per thread, reuse for all matches
- **Error handling**: Graceful fallback to `std::regex` for unsupported patterns
- **Minimal interface**: Only what's needed for `StdRegexUtils.h` compatibility
- **RAII resource management**: Proper cleanup of databases and scratch space

**Key Implementation Components:**

1. **Database Cache** (similar to `RegexCache` in `StdRegexUtils.h`):
   ```cpp
   class HyperscanDatabaseCache {
     // Thread-safe map: pattern+case_sensitive -> hs_database_t*
     // Compile on first use, cache for reuse
   };
   ```

2. **Thread-Local Scratch Space**:
   ```cpp
   thread_local hs_scratch_t* g_scratch = nullptr;
   // Allocate on first use, reuse for all matches in thread
   ```

3. **Pattern Detection** (for fallback):
   ```cpp
   bool RequiresFallback(const std::string& pattern) {
     // Quick check for lookahead/lookbehind
     return pattern.find("(?=") != std::string::npos || 
            pattern.find("(?!") != std::string::npos ||
            // ... other unsupported features
   }
   ```

4. **Match Function**:
   ```cpp
   bool RegexMatch(const std::string& pattern, const std::string& text, bool case_sensitive) {
     // Check for unsupported features
     if (RequiresFallback(pattern)) {
       return std_regex_utils::RegexMatch(pattern, text, case_sensitive);
     }
     
     // Get or compile database
     hs_database_t* db = GetOrCompileDatabase(pattern, case_sensitive);
     if (!db) {
       // Compilation failed - fallback
       return std_regex_utils::RegexMatch(pattern, text, case_sensitive);
     }
     
     // Get thread-local scratch
     hs_scratch_t* scratch = GetThreadLocalScratch(db);
     
     // Match callback (simple boolean result)
     bool matched = false;
     auto onMatch = [](unsigned int, unsigned long long, unsigned long long,
                       unsigned int, void* ctx) -> int {
       *static_cast<bool*>(ctx) = true;
       return 0; // Continue (though we only need first match)
     };
     
     hs_scan(db, text.data(), text.size(), 0, scratch, onMatch, &matched);
     return matched;
   }
   ```

**Alternative: Copy Existing Wrapper Code**

If you find an existing wrapper with permissive license (MIT, Apache-2.0, BSD) that matches your needs:
- **Copy the wrapper code** directly into the project (e.g., `external/hyperscan_wrapper/`)
- **No submodule or external dependency** needed
- **Adapt as needed** for your interface
- **Note**: Still need to integrate Hyperscan library itself

**Recommendation**: Implement custom minimal wrapper for maximum control and minimal dependencies.

### Phase 2: Integration Points

1. **StdRegexUtils.h**: Add option to use Hyperscan backend
   - Compile-time flag: `USE_HYPERSCAN`
   - Runtime fallback if Hyperscan unavailable

2. **SearchPatternUtils.h**: No changes needed (uses `std_regex_utils::RegexMatch`)

3. **FileIndex.cpp**: No changes needed (uses pattern utilities)

4. **SearchWorker.cpp**: No changes needed (uses pattern utilities)

### Phase 3: Build System Integration

**Hyperscan Library Integration Options:**

The wrapper code will be in the project, but Hyperscan library itself needs to be integrated. Options (in order of preference for minimizing dependencies):

**Option 1: Git Submodule (Recommended for consistency)**
```cmake
# Add Hyperscan as submodule (like imgui)
# external/hyperscan/

option(USE_HYPERSCAN "Use Hyperscan for regex matching" OFF)

if(USE_HYPERSCAN)
  add_subdirectory(external/hyperscan)
  target_link_libraries(FindHelper PRIVATE hs)
endif()
```

**Option 2: FetchContent (No submodule needed)**
```cmake
option(USE_HYPERSCAN "Use Hyperscan for regex matching" OFF)

if(USE_HYPERSCAN)
  include(FetchContent)
  FetchContent_Declare(
    hyperscan
    GIT_REPOSITORY https://github.com/intel/hyperscan.git
    GIT_TAG v5.4.2  # Or latest stable
  )
  FetchContent_MakeAvailable(hyperscan)
  target_link_libraries(FindHelper PRIVATE hs)
endif()
```

**Option 3: Pre-built Library (Minimal build-time dependency)**
```cmake
option(USE_HYPERSCAN "Use Hyperscan for regex matching" OFF)

if(USE_HYPERSCAN)
  find_library(HYPERSCAN_LIB 
    NAMES hs hyperscan
    PATHS ${CMAKE_SOURCE_DIR}/external/hyperscan/lib
  )
  find_path(HYPERSCAN_INCLUDE
    NAMES hs/hs.h
    PATHS ${CMAKE_SOURCE_DIR}/external/hyperscan/include
  )
  target_link_libraries(FindHelper PRIVATE ${HYPERSCAN_LIB})
  target_include_directories(FindHelper PRIVATE ${HYPERSCAN_INCLUDE})
endif()
```

**Dependencies:**
- **Hyperscan library**: C library, can be linked statically (preferred) or dynamically
- **Platform**: Windows 8+ (compatible with project requirements)
- **Compiler**: MSVC 2017+ or Intel C++ Compiler
- **Build time**: Hyperscan needs to be built (unless using pre-built)
- **Runtime**: No additional runtime dependencies if statically linked

---

## Performance Testing Strategy

### Benchmark Scenarios

1. **Single Pattern Matching**:
   - Simple patterns (`.`, `*`, `^`, `$`)
   - Complex patterns (character classes, quantifiers)
   - Long text matching (file content)

2. **Filename Matching**:
   - Typical filename patterns (`.*\.cpp$`, `test.*`)
   - Case-sensitive vs. case-insensitive

3. **Path Matching**:
   - Directory path patterns
   - Long paths

4. **Content Matching**:
   - File content regex searches
   - Large file matching

### Metrics to Measure

- **Matching Speed**: Time per match operation
- **Compilation Time**: Database compilation overhead
- **Memory Usage**: Scratch space + database memory
- **Cache Hit Rate**: Database cache effectiveness
- **Thread Scalability**: Performance with multiple threads

---

## Recommendations

### Option 1: Full Hyperscan Integration (Recommended for Performance)

**Pros:**
- Significant performance improvements (10-100x)
- SIMD optimizations leverage modern CPUs
- Better scalability for future multi-pattern use cases
- Maintains current API (custom wrapper hides complexity)
- **Minimal dependencies**: Custom wrapper in project, only Hyperscan library needed

**Cons:**
- Additional dependency (Hyperscan library - but can be statically linked)
- More complex implementation (scratch space, database management)
- Potential compatibility issues with some regex patterns (handled via fallback)
- Memory overhead (scratch space per thread)

**Implementation Effort**: 
- **Custom minimal wrapper**: Medium (2-3 days for wrapper + testing)
- Includes: wrapper implementation, build integration, testing, fallback mechanism

### Option 2: Hybrid Approach (Recommended for Safety)

**Pros:**
- Best of both worlds: Hyperscan for performance, std::regex for compatibility
- Gradual migration path
- Fallback for unsupported patterns

**Cons:**
- More complex code (two backends)
- Need to decide which patterns use which backend

**Implementation:**
- Use Hyperscan for supported patterns
- Fall back to `std::regex` for unsupported patterns
- Compile-time flag to enable/disable Hyperscan

### Option 3: Keep std::regex (Status Quo)

**Pros:**
- No changes needed
- Standard library (no external dependency)
- Full ECMAScript compatibility

**Cons:**
- Slower performance (known bottleneck)
- No SIMD optimizations
- Limited scalability

---

## Decision Matrix

| Factor | Weight | Hyperscan | std::regex |
|--------|--------|-----------|------------|
| **Performance** | High | ????? | ?? |
| **Compatibility** | High | ???? | ????? |
| **Complexity** | Medium | ?? | ????? |
| **Dependencies** | Medium | ??? | ????? |
| **Maintainability** | Medium | ??? | ???? |
| **Future-Proof** | Low | ????? | ??? |

**Weighted Score**: Hyperscan wins on performance, but std::regex wins on simplicity.

---

## Next Steps

If proceeding with Hyperscan integration:

1. **Research Phase** (1-2 hours):
   - Test Hyperscan compilation on Windows
   - Verify pattern compatibility with current usage
   - Measure baseline performance with std::regex
   - Review Hyperscan API documentation

2. **Prototype Phase** (6-10 hours):
   - Create `HyperscanUtils.h` custom wrapper
   - Implement database cache (similar to `RegexCache` in `StdRegexUtils.h`)
   - Implement thread-local scratch space management
   - Implement basic `RegexMatch` function with fallback detection
   - Add error handling and logging

3. **Integration Phase** (2-4 hours):
   - Integrate Hyperscan into build system (CMake)
   - Add compile-time flag `USE_HYPERSCAN`
   - Update `StdRegexUtils.h` to optionally use Hyperscan backend
   - Ensure graceful fallback when Hyperscan unavailable

4. **Testing Phase** (4-8 hours):
   - Unit tests for Hyperscan wrapper
   - Test fallback mechanism (unsupported patterns)
   - Performance benchmarks (compare with std::regex)
   - Compatibility testing (pattern syntax)
   - Thread safety testing
   - Memory leak testing (scratch space cleanup)

5. **Documentation Phase** (1-2 hours):
   - Update build instructions
   - Document pattern limitations
   - Add performance notes
   - Document fallback behavior

**Total Estimated Effort**: 
- **Custom minimal wrapper**: 14-26 hours (1.75-3.25 days)

---

## References

- [Hyperscan Developer Reference](https://intel.github.io/hyperscan/dev-reference/)
- [Intel Hyperscan (Official Repository)](https://github.com/intel/hyperscan)
- [Hyperscan Performance Guide](https://intel.github.io/hyperscan/dev-reference/performance.html)
- [Getting Started with Hyperscan on Windows](https://www.intel.com/content/www/us/en/developer/articles/guide/get-started-with-hyperscan-on-windows.html)
- [Hyperscan API Reference](https://intel.github.io/hyperscan/dev-reference/api_files.html)

---

## Conclusion

Hyperscan offers significant performance benefits for regex matching in this project, especially for file indexing and content search operations. The integration is feasible but requires:

1. **Custom minimal wrapper implementation** (in-project, no external wrapper dependency)
2. **Fallback mechanism** to `std::regex` for unsupported patterns (primarily lookahead/lookbehind)
3. **Build system integration** for Hyperscan library (submodule, FetchContent, or pre-built)
4. **Thorough testing** for pattern compatibility and performance

**Key Finding**: Most patterns used in the codebase are Hyperscan-compatible. The main exception is the **DoesNotContain template** which uses negative lookahead (`^(?!.*text).*$`) and will require fallback to `std::regex`.

**Recommendation**: Proceed with **Option 2 (Hybrid Approach)** - implement Hyperscan with automatic fallback to std::regex for unsupported patterns. This provides:
- **Maximum performance** for compatible patterns (10-100x speedup)
- **Full compatibility** for all patterns (via fallback)
- **Transparent operation** (users don't need to know which engine is used)
- **Minimal dependencies**: Custom wrapper in project, only Hyperscan library needed (can be statically linked)

**Implementation Strategy**:
- **Custom wrapper**: Implement `HyperscanUtils.h` directly in project (no external wrapper dependency)
- **Hyperscan library**: Integrate via submodule (consistent with `imgui` approach) or FetchContent
- **Static linking**: Prefer static linking of Hyperscan to avoid runtime dependencies
