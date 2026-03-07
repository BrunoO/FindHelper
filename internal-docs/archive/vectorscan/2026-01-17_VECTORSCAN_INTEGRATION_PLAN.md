# VectorScan Integration Plan

**Date:** 2026-01-17  
**Status:** Planning  
**Related:** `docs/archive/HYPERSCAN_FEASIBILITY_STUDY.md`

## Executive Summary

This document outlines the plan to integrate **VectorScan** as a new search engine option, accessible via the `vs:` prefix. VectorScan is a high-performance regex matching library (fork of Intel Hyperscan) that provides SIMD-optimized pattern matching, potentially offering 10-100x performance improvements over `std::regex` for compatible patterns.

**Key Goals:**
- Add `vs:` prefix support for VectorScan-powered regex matching
- Platform-aware detection and conditional compilation
- Automatic fallback to `std::regex` for unsupported patterns
- Maintain existing API compatibility
- Zero impact when VectorScan is unavailable

---

## What is VectorScan?

**VectorScan** is an open-source fork of Intel Hyperscan, maintained by VectorCamp. It provides:
- **High-performance regex matching** with SIMD optimizations (SSE4.2, AVX2)
- **PCRE-compatible syntax** (with some limitations)
- **BSD-3-Clause license** (fully open-source)
- **Cross-platform support** (Windows, macOS, Linux)

**Key Differences from Hyperscan:**
- Actively maintained open-source fork
- Available via Homebrew on macOS (`brew install vectorscan`)
- Same API as Hyperscan (drop-in replacement)
- Better cross-platform support

**Performance Characteristics:**
- **10-100x faster** than `std::regex` for compatible patterns
- **SIMD-optimized** for modern CPUs
- **Database compilation** model (compile once, match many times)
- **Thread-safe** (databases shared, scratch space per-thread)

---

## Current Search Engine Architecture

### Pattern Prefixes

The codebase currently supports:
- **`rs:`** - Standard regex (ECMAScript via `std::regex`)
- **`pp:`** - PathPattern (custom path matching)
- **No prefix** - Auto-detected (PathPattern, Glob, or Substring)

### Implementation Flow

1. **Pattern Detection** (`SearchPatternUtils.h::DetectPatternType()`)
   - Checks for explicit prefixes (`rs:`, `pp:`)
   - Auto-detects pattern type based on content
   - Returns `PatternType` enum

2. **Pattern Matching** (`SearchPatternUtils.h::MatchPattern()`)
   - Routes to appropriate engine based on `PatternType`
   - `PatternType::StdRegex` → `std_regex_utils::RegexMatch()`

3. **Matcher Creation** (`SearchPatternUtils.h::CreateMatcherImpl()`)
   - Creates lightweight callable matchers for hot paths
   - Pre-compiles patterns when possible
   - Optimizes based on pattern analysis

### Current Regex Implementation

**Location:** `src/utils/StdRegexUtils.h`

**Features:**
- Pattern analysis (literal, simple, complex)
- Regex caching (thread-safe)
- Case-sensitive/insensitive support
- ReDoS protection (pattern length limits)
- Optimization routing (literal → substring, simple → SimpleRegex, complex → std::regex)

---

## Integration Strategy

### Phase 1: VectorScan Detection and Build Integration

#### 1.1 Platform Detection

**macOS:**
- Check for Homebrew installation: `brew list vectorscan` or `pkg-config --exists vectorscan`
- Library location: `/opt/homebrew/lib/libhs.a` (Apple Silicon) or `/usr/local/lib/libhs.a` (Intel)
- Headers: `/opt/homebrew/include/hs/` or `/usr/local/include/hs/`

**Linux:**
- Check via `pkg-config`: `pkg-config --exists vectorscan`
- Or find library: `find_library(VECTORSCAN_LIB NAMES hs vectorscan)`
- Headers: `/usr/include/hs/` or custom location

**Windows:**
- Check for vcpkg installation: `find_package(vectorscan CONFIG)`
- Or manual installation: `find_library()` and `find_path()`
- Library: `vectorscan.lib` or `hs.lib`
- Headers: `include/hs/`

#### 1.2 CMake Integration

**Approach:** Use `find_package()` with fallback to `pkg-config` or manual detection.

```cmake
# Option to enable VectorScan (default: auto-detect)
option(USE_VECTORSCAN "Use VectorScan for regex matching (vs: prefix)" AUTO)

# Auto-detect VectorScan availability
if(USE_VECTORSCAN STREQUAL "AUTO" OR USE_VECTORSCAN)
    # Try find_package first (vcpkg, system installation)
    find_package(vectorscan QUIET)
    
    if(vectorscan_FOUND)
        message(STATUS "VectorScan: Found via find_package")
        set(VECTORSCAN_AVAILABLE TRUE)
    else()
        # Try pkg-config (macOS Homebrew, Linux)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(VECTORSCAN QUIET vectorscan)
            if(VECTORSCAN_FOUND)
                message(STATUS "VectorScan: Found via pkg-config")
                set(VECTORSCAN_AVAILABLE TRUE)
            endif()
        endif()
    endif()
    
    # Manual detection fallback
    if(NOT VECTORSCAN_AVAILABLE)
        find_library(VECTORSCAN_LIB
            NAMES hs vectorscan
            PATHS
                /opt/homebrew/lib
                /usr/local/lib
                /usr/lib
        )
        find_path(VECTORSCAN_INCLUDE_DIR
            NAMES hs/hs.h
            PATHS
                /opt/homebrew/include
                /usr/local/include
                /usr/include
        )
        if(VECTORSCAN_LIB AND VECTORSCAN_INCLUDE_DIR)
            message(STATUS "VectorScan: Found manually (${VECTORSCAN_LIB})")
            set(VECTORSCAN_AVAILABLE TRUE)
        endif()
    endif()
    
    if(VECTORSCAN_AVAILABLE)
        message(STATUS "VectorScan: Enabled (vs: prefix support)")
        target_compile_definitions(find_helper PRIVATE USE_VECTORSCAN)
        if(vectorscan_FOUND)
            target_link_libraries(find_helper PRIVATE vectorscan::vectorscan)
        elseif(VECTORSCAN_LIB)
            target_link_libraries(find_helper PRIVATE ${VECTORSCAN_LIB})
            target_include_directories(find_helper PRIVATE ${VECTORSCAN_INCLUDE_DIR})
        else()
            target_link_libraries(find_helper PRIVATE ${VECTORSCAN_LIBRARIES})
            target_include_directories(find_helper PRIVATE ${VECTORSCAN_INCLUDE_DIRS})
        endif()
    else()
        message(STATUS "VectorScan: Not found (vs: prefix will fallback to std::regex)")
    endif()
endif()
```

**Placement in CMakeLists.txt:**
- After platform-specific sections (Windows/macOS/Linux)
- Before test configuration
- Similar to how `FAST_LIBS_BOOST` is handled

---

### Phase 2: VectorScan Wrapper Implementation

#### 2.1 Create VectorScanUtils.h

**Location:** `src/utils/VectorScanUtils.h`

**Purpose:** Minimal wrapper around VectorScan API, similar to `StdRegexUtils.h`

**Interface:**
```cpp
#pragma once

#include <string>
#include <string_view>

#ifdef USE_VECTORSCAN
#include <hs/hs.h>
#endif

namespace vectorscan_utils {

// Check if VectorScan is available at compile time
constexpr bool IsAvailable() {
#ifdef USE_VECTORSCAN
    return true;
#else
    return false;
#endif
}

// Check if VectorScan is available at runtime
bool IsRuntimeAvailable();

// Match pattern against text using VectorScan
// Returns false on invalid pattern, no match, or if VectorScan unavailable
bool RegexMatch(std::string_view pattern, std::string_view text, bool case_sensitive = true);

// Clear the database cache
void ClearCache();

// Get cache size
size_t GetCacheSize();

} // namespace vectorscan_utils
```

#### 2.2 Implementation Details

**Database Cache:**
- Thread-safe map: `(pattern + case_sensitive) → hs_database_t*`
- Similar to `RegexCache` in `StdRegexUtils.h`
- Compile on first use, cache for reuse

**Thread-Local Scratch Space:**
```cpp
thread_local hs_scratch_t* g_scratch = nullptr;

hs_scratch_t* GetThreadLocalScratch(hs_database_t* db) {
    if (g_scratch == nullptr) {
        hs_error_t err = hs_alloc_scratch(db, &g_scratch);
        if (err != HS_SUCCESS) {
            return nullptr;
        }
    }
    return g_scratch;
}
```

**Pattern Compatibility Detection:**
- Pre-scan for unsupported features (lookahead, lookbehind, backreferences)
- Route directly to `std::regex` if detected
- Try VectorScan compilation, fallback on `HS_COMPILER_ERROR`

**Match Function:**
```cpp
bool RegexMatch(std::string_view pattern, std::string_view text, bool case_sensitive) {
    if (!IsRuntimeAvailable()) {
        return std_regex_utils::RegexMatch(std::string(pattern), std::string(text), case_sensitive);
    }
    
    // Check for unsupported features
    if (RequiresFallback(pattern)) {
        return std_regex_utils::RegexMatch(std::string(pattern), std::string(text), case_sensitive);
    }
    
    // Get or compile database
    hs_database_t* db = GetOrCompileDatabase(pattern, case_sensitive);
    if (!db) {
        // Compilation failed - fallback
        return std_regex_utils::RegexMatch(std::string(pattern), std::string(text), case_sensitive);
    }
    
    // Get thread-local scratch
    hs_scratch_t* scratch = GetThreadLocalScratch(db);
    if (!scratch) {
        return false;
    }
    
    // Match callback (simple boolean result)
    bool matched = false;
    auto onMatch = [](unsigned int, unsigned long long, unsigned long long,
                      unsigned int, void* ctx) -> int {
        *static_cast<bool*>(ctx) = true;
        return 1; // Stop after first match
    };
    
    hs_error_t err = hs_scan(db, text.data(), text.size(), 0, scratch, onMatch, &matched);
    if (err != HS_SUCCESS) {
        return false;
    }
    
    return matched;
}
```

**Unsupported Features Detection:**
```cpp
bool RequiresFallback(std::string_view pattern) {
    // Check for lookahead/lookbehind
    if (pattern.find("(?=") != std::string_view::npos ||  // positive lookahead
        pattern.find("(?!") != std::string_view::npos ||  // negative lookahead
        pattern.find("(?<=") != std::string_view::npos || // positive lookbehind
        pattern.find("(?<!") != std::string_view::npos) { // negative lookbehind
        return true;
    }
    
    // Check for backreferences (simple check - \1, \2, etc.)
    // More complex patterns would need regex to detect properly
    // For now, let compilation fail and fallback
    
    return false;
}
```

#### 2.3 VectorScanUtils.cpp Implementation

**Key Components:**
1. **DatabaseCache class** (similar to `RegexCache` in `StdRegexUtils.h`)
   - Thread-safe `std::unordered_map` with mutex
   - Key: `std::pair<std::string, bool>` (pattern, case_sensitive)
   - Value: `std::unique_ptr<hs_database_t, DatabaseDeleter>`

2. **DatabaseDeleter** (RAII for `hs_database_t`)
   ```cpp
   struct DatabaseDeleter {
       void operator()(hs_database_t* db) const {
           if (db) {
               hs_free_database(db);
           }
       }
   };
   ```

3. **Compilation flags**
   ```cpp
   unsigned int GetCompileFlags(bool case_sensitive) {
       unsigned int flags = HS_FLAG_DOTALL; // '.' matches newlines
       if (!case_sensitive) {
           flags |= HS_FLAG_CASELESS;
       }
       return flags;
   }
   ```

4. **Error handling**
   - Log compilation errors
   - Log scan errors
   - Graceful fallback to `std::regex`

---

### Phase 3: SearchPatternUtils Integration

#### 3.1 Add VectorScan Pattern Type

**Update `PatternType` enum:**
```cpp
enum class PatternType {
  StdRegex,    // rs: prefix - full ECMAScript regex
  VectorScan,   // vs: prefix - VectorScan (if available, else std::regex)
  PathPattern, // pp: prefix OR auto-detected
  Glob,        // contains * or ? - wildcard pattern
  Substring    // default - substring search
};
```

#### 3.2 Update DetectPatternType()

**Add `vs:` prefix detection:**
```cpp
inline PatternType DetectPatternType(std::string_view pattern) {
  // 1. Explicit prefixes (highest priority)
  if (pattern.size() >= 3) {
    auto prefix = pattern.substr(0, 3);
    if (prefix == "rs:") {
      return PatternType::StdRegex;
    }
    if (prefix == "vs:") {
      return PatternType::VectorScan;
    }
    if (prefix == "pp:") {
      return PatternType::PathPattern;
    }
  }
  // ... rest of detection logic
}
```

#### 3.3 Update ExtractPattern()

**Handle `vs:` prefix:**
```cpp
inline std::string ExtractPattern(std::string_view input) {
  if (input.size() >= 3 &&
      (input.substr(0, 3) == "rs:" || 
       input.substr(0, 3) == "vs:" ||
       input.substr(0, 3) == "pp:")) {
    return std::string(input.substr(3));
  }
  return std::string(input);
}
```

#### 3.4 Update MatchPattern()

**Add VectorScan case:**
```cpp
case PatternType::VectorScan: {
  std::string regex_pattern = ExtractPattern(pattern);
  if (regex_pattern.empty()) {
    return false;
  }
  if (regex_pattern.length() > kMaxPatternLength) {
    return false;
  }
  // Use VectorScan if available, fallback to std::regex
  if (vectorscan_utils::IsRuntimeAvailable()) {
    return vectorscan_utils::RegexMatch(regex_pattern, text, case_sensitive);
  } else {
    // Fallback to std::regex
    return std_regex_utils::RegexMatch(regex_pattern, text, case_sensitive);
  }
}
```

#### 3.5 Update CreateMatcherImpl()

**Add VectorScan matcher creation:**
```cpp
case PatternType::VectorScan: {
  std::string regex_pattern = ExtractPattern(pattern);
  if (regex_pattern.empty()) {
    return [](TextType) { return false; };
  }
  if (regex_pattern.length() > kMaxPatternLength) {
    return [](TextType) { return false; };
  }
  
  // Use VectorScan if available, fallback to std::regex
  if (vectorscan_utils::IsRuntimeAvailable()) {
    // Create VectorScan matcher
    // Similar to std::regex matcher, but using VectorScan database
    // Implementation details in VectorScanUtils.h
    return vectorscan_utils::CreateMatcher<TextType>(regex_pattern, case_sensitive);
  } else {
    // Fallback to std::regex matcher (existing code)
    // ... std::regex matcher creation ...
  }
}
```

---

### Phase 4: Documentation and Testing

#### 4.1 Update Search Help

**Location:** `src/ui/SearchHelpWindow.cpp`

**Add VectorScan documentation:**
```cpp
// In search help text
"vs:pattern    - VectorScan regex (high-performance, if available)\n"
"               - Falls back to std::regex if VectorScan unavailable\n"
"               - Example: vs:.*\\.cpp$\n"
```

#### 4.2 Update Gemini API Help

**Location:** `src/api/GeminiApiUtils.cpp`

**Add VectorScan to prompt:**
```cpp
// In BuildSearchConfigPrompt()
"- **vs: (VectorScan)** - High-performance regex (if available):\n"
"  - Same syntax as rs:, but uses VectorScan engine\n"
"  - 10-100x faster than std::regex for compatible patterns\n"
"  - Automatically falls back to std::regex if unavailable\n"
"  - Example: vs:.*\\.(cpp|hpp)$\n"
```

#### 4.3 Unit Tests

**Create:** `tests/VectorScanUtilsTests.cpp`

**Test Cases:**
1. **Availability Detection**
   - `IsRuntimeAvailable()` returns correct value
   - Compile-time vs runtime availability

2. **Pattern Matching**
   - Simple patterns (`.cpp$`, `^test`)
   - Complex patterns (character classes, quantifiers)
   - Case-sensitive vs case-insensitive
   - Unsupported patterns (lookahead) → fallback

3. **Fallback Mechanism**
   - Unsupported patterns fall back to `std::regex`
   - Unavailable VectorScan falls back to `std::regex`
   - Compilation errors fall back to `std::regex`

4. **Cache Management**
   - Database caching works correctly
   - Cache size limits
   - Cache clearing

5. **Thread Safety**
   - Multiple threads using VectorScan simultaneously
   - Thread-local scratch space

6. **Performance**
   - Benchmark vs `std::regex` (if VectorScan available)
   - Measure compilation overhead
   - Measure matching speed

#### 4.4 Integration Tests

**Update:** `tests/SearchPatternUtilsTests.cpp`

**Add tests for `vs:` prefix:**
- Pattern detection (`vs:` → `PatternType::VectorScan`)
- Pattern extraction (remove `vs:` prefix)
- Matching with `vs:` prefix
- Fallback behavior when VectorScan unavailable

---

## Platform-Specific Considerations

### macOS

**Installation:**
```bash
brew install vectorscan
```

**Detection:**
- Homebrew installs to `/opt/homebrew/` (Apple Silicon) or `/usr/local/` (Intel)
- `pkg-config` should work: `pkg-config --cflags --libs vectorscan`
- Library: `libhs.a` or `libhs.dylib`
- Headers: `hs/hs.h`

**Build Integration:**
- Use `pkg-config` if available
- Fallback to manual detection in `/opt/homebrew/` or `/usr/local/`

### Linux

**Installation Options:**
1. **System Package Manager:**
   ```bash
   # Ubuntu/Debian (if available)
   sudo apt-get install libvectorscan-dev
   ```
2. **Build from Source:**
   - Clone VectorScan repository
   - Build and install to system or custom location
3. **vcpkg (if using vcpkg):**
   ```bash
   vcpkg install vectorscan
   ```

**Detection:**
- Try `pkg-config` first
- Fallback to `find_library()` and `find_path()`
- Check common locations: `/usr/lib/`, `/usr/local/lib/`

### Windows

**Installation Options:**
1. **vcpkg (Recommended):**
   ```bash
   vcpkg install vectorscan
   ```
2. **Manual Build:**
   - Build VectorScan from source
   - Install to custom location
   - Set `VECTORSCAN_ROOT` environment variable

**Detection:**
- Try `find_package(vectorscan CONFIG)` first (vcpkg)
- Fallback to `find_library()` and `find_path()`
- Check `VECTORSCAN_ROOT` environment variable

**Note:** VectorScan on Windows may require additional dependencies (Boost, etc.). Check VectorScan documentation for Windows build requirements.

---

## Fallback Strategy

### When VectorScan is Unavailable

1. **Compile-time:** If `USE_VECTORSCAN` is not defined, `vs:` patterns fall back to `std::regex`
2. **Runtime:** If VectorScan library is not found, `vs:` patterns fall back to `std::regex`
3. **Pattern Compatibility:** If pattern uses unsupported features, fall back to `std::regex`

### Fallback Implementation

**In `SearchPatternUtils.h`:**
```cpp
case PatternType::VectorScan: {
  // Always try VectorScan first (if available)
  if (vectorscan_utils::IsRuntimeAvailable()) {
    bool result = vectorscan_utils::RegexMatch(regex_pattern, text, case_sensitive);
    // VectorScan::RegexMatch() handles its own fallback internally
    return result;
  } else {
    // VectorScan not available - use std::regex
    return std_regex_utils::RegexMatch(regex_pattern, text, case_sensitive);
  }
}
```

**In `VectorScanUtils.cpp`:**
```cpp
bool RegexMatch(std::string_view pattern, std::string_view text, bool case_sensitive) {
  // Check runtime availability
  if (!IsRuntimeAvailable()) {
    return std_regex_utils::RegexMatch(std::string(pattern), std::string(text), case_sensitive);
  }
  
  // Check pattern compatibility
  if (RequiresFallback(pattern)) {
    return std_regex_utils::RegexMatch(std::string(pattern), std::string(text), case_sensitive);
  }
  
  // Try VectorScan compilation
  hs_database_t* db = GetOrCompileDatabase(pattern, case_sensitive);
  if (!db) {
    // Compilation failed - fallback
    return std_regex_utils::RegexMatch(std::string(pattern), std::string(text), case_sensitive);
  }
  
  // Use VectorScan for matching
  // ... matching code ...
}
```

---

## Unsupported Patterns

### VectorScan Limitations

VectorScan does **not support**:
1. **Lookahead/lookbehind** (`(?=...)`, `(?!...)`, `(?<=...)`, `(?<!...)`)
2. **Backreferences** (`\1`, `\2`, etc.)
3. **Capture groups** (grouping works, but captures aren't accessible)
4. **Atomic grouping** (`(?>...)`)
5. **Possessive quantifiers** (`*+`, `++`, `?+`)
6. **Conditional patterns** (`(?(condition)yes|no)`)
7. **Recursive patterns** (`(?R)`)
8. **Some special escapes** (`\C`, `\R`, `\K`)

### Detection Strategy

1. **Pre-scan for known unsupported features** (fast, avoids compilation attempt)
2. **Try VectorScan compilation** (if pre-scan passes)
3. **Fallback on compilation error** (if pattern fails to compile)

### Impact Assessment

**High Impact (requires fallback):**
- Patterns using lookahead/lookbehind (e.g., `^(?!.*text).*$`)

**Low Impact (rarely used):**
- Backreferences, atomic grouping, possessive quantifiers, conditionals, recursive patterns

**No Impact (compatible):**
- Most file search patterns (extensions, prefixes, character classes, quantifiers)

**Conclusion:** Most real-world file search patterns are VectorScan-compatible. Fallback will be used primarily for patterns using lookahead/lookbehind.

---

## Implementation Checklist

### Phase 1: Build Integration
- [ ] Add CMake detection for VectorScan (find_package, pkg-config, manual)
- [ ] Add `USE_VECTORSCAN` compile definition when available
- [ ] Link VectorScan library to `find_helper` target
- [ ] Test build on macOS (Homebrew installation)
- [ ] Test build on Linux (system package or manual)
- [ ] Test build on Windows (vcpkg or manual)

### Phase 2: VectorScan Wrapper
- [ ] Create `src/utils/VectorScanUtils.h`
- [ ] Create `src/utils/VectorScanUtils.cpp`
- [ ] Implement database cache (thread-safe)
- [ ] Implement thread-local scratch space
- [ ] Implement pattern compatibility detection
- [ ] Implement `RegexMatch()` with fallback
- [ ] Implement `CreateMatcher()` for matcher creation
- [ ] Add error handling and logging

### Phase 3: SearchPatternUtils Integration
- [ ] Add `PatternType::VectorScan` enum value
- [ ] Update `DetectPatternType()` to detect `vs:` prefix
- [ ] Update `ExtractPattern()` to handle `vs:` prefix
- [ ] Update `MatchPattern()` to handle VectorScan
- [ ] Update `CreateMatcherImpl()` to create VectorScan matchers
- [ ] Test pattern detection and matching

### Phase 4: Documentation and Testing
- [ ] Update `SearchHelpWindow.cpp` with `vs:` documentation
- [ ] Update `GeminiApiUtils.cpp` with VectorScan info
- [ ] Create `tests/VectorScanUtilsTests.cpp`
- [ ] Add integration tests for `vs:` prefix
- [ ] Update README with VectorScan installation instructions
- [ ] Document fallback behavior

### Phase 5: Validation
- [ ] Test on macOS with VectorScan installed
- [ ] Test on macOS without VectorScan (fallback)
- [ ] Test on Linux with VectorScan installed
- [ ] Test on Linux without VectorScan (fallback)
- [ ] Test on Windows with VectorScan (if available)
- [ ] Test on Windows without VectorScan (fallback)
- [ ] Performance benchmarks (if VectorScan available)
- [ ] Test unsupported patterns (fallback to std::regex)

---

## Performance Expectations

### Expected Improvements

**VectorScan vs std::regex:**
- **Simple patterns:** 5-20x faster
- **Complex patterns:** 10-100x faster
- **Multiple patterns:** VectorScan excels (database approach)

### Benchmark Scenarios

1. **Filename Matching:**
   - Pattern: `.*\.cpp$`
   - Text: 10,000 filenames
   - Expected: 5-10x speedup

2. **Path Matching:**
   - Pattern: `.*/src/.*\.cpp$`
   - Text: 10,000 paths
   - Expected: 10-20x speedup

3. **Content Matching:**
   - Pattern: `.*\d{4}-\d{2}-\d{2}.*`
   - Text: Large file content
   - Expected: 20-50x speedup

### Overhead Considerations

**Compilation Overhead:**
- VectorScan database compilation is slower than `std::regex` compilation
- **Mitigation:** Database caching (compile once, reuse many times)

**Memory Overhead:**
- Scratch space per thread (typically 1-10 MB)
- Database memory (depends on pattern complexity)
- **Mitigation:** Thread-local scratch (allocated once per thread)

**Conclusion:** Performance gains outweigh overhead for repeated matching (typical use case).

---

## Risk Assessment

### Low Risk
- ✅ VectorScan is well-tested and stable
- ✅ Fallback mechanism ensures compatibility
- ✅ No breaking changes to existing API
- ✅ Optional dependency (graceful degradation)

### Medium Risk
- ⚠️ Pattern compatibility (some patterns require fallback)
- ⚠️ Platform availability (not available on all platforms)
- ⚠️ Build complexity (additional dependency)

### Mitigation Strategies
1. **Comprehensive fallback:** Always fallback to `std::regex` when VectorScan unavailable
2. **Pattern detection:** Pre-scan for unsupported features to avoid compilation attempts
3. **Clear documentation:** Document limitations and fallback behavior
4. **Optional dependency:** VectorScan is optional, not required

---

## Future Enhancements

### Potential Improvements

1. **Multi-Pattern Database:**
   - Compile multiple patterns into one database
   - Match all patterns simultaneously
   - Useful for extension filtering or multiple search queries

2. **Streaming Mode:**
   - Use VectorScan streaming mode for large file content matching
   - Better performance for very large files

3. **Pattern Analysis:**
   - Analyze patterns to determine if VectorScan is beneficial
   - Route simple patterns to faster engines (substring, SimpleRegex)
   - Only use VectorScan for complex patterns

4. **Performance Metrics:**
   - Track VectorScan vs std::regex performance
   - Log fallback frequency
   - Optimize based on real-world usage

---

## References

- [VectorScan GitHub Repository](https://github.com/VectorCamp/vectorscan)
- [Hyperscan Developer Reference](https://intel.github.io/hyperscan/dev-reference/) (VectorScan uses same API)
- [VectorScan Homebrew Formula](https://formulae.brew.sh/formula/vectorscan)
- [Hyperscan Feasibility Study](./archive/HYPERSCAN_FEASIBILITY_STUDY.md) (existing study in codebase)

---

## Conclusion

VectorScan integration provides a high-performance regex matching option with minimal risk. The implementation plan ensures:
- **Platform-aware detection** (works when available, graceful fallback when not)
- **Backward compatibility** (no breaking changes)
- **Performance benefits** (10-100x speedup for compatible patterns)
- **Maintainability** (clean wrapper, similar to existing `StdRegexUtils.h`)

The `vs:` prefix provides users with an explicit way to opt into high-performance regex matching when VectorScan is available, while maintaining full compatibility through automatic fallback to `std::regex`.
