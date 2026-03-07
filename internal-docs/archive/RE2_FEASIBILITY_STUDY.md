# RE2 Feasibility Study: Replacing std::regex

## Executive Summary

This document analyzes the feasibility of replacing `std::regex` with **Google RE2** for high-performance regular expression matching in the USN_WINDOWS project.

**Key Finding:** RE2 could provide significant performance improvements (5-10x faster) for regex matching while maintaining better safety guarantees (no catastrophic backtracking). RE2 is BSD-3-Clause licensed (fully open-source). Integration is straightforward with a C++ API that's easier to use than Hyperscan, but requires consideration of feature limitations (no backreferences, no lookahead/lookbehind).

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

## What is RE2?

### Overview

**RE2** is a fast, safe, and thread-friendly regular expression library developed by Google, designed to:

- **Avoid backtracking**: Uses DFA/NFA hybrid engine (no exponential time complexity)
- **Guarantee linear time**: O(n) matching time in input size
- **Thread-safe**: All operations are thread-safe by design
- **Memory-safe**: Bounded memory usage with configurable limits
- **Production-tested**: Used extensively in Google's production systems

### Key Features

1. **C++ API**: Native C++ interface (easier than C APIs like Hyperscan)
2. **Thread Safety**: All operations are thread-safe without additional synchronization
3. **Pattern Caching**: RE2 objects can be reused across threads
4. **Partial/Full Matching**: `RE2::PartialMatch` (substring) and `RE2::FullMatch` (entire string)
5. **Capture Groups**: Supports capture groups (unlike Hyperscan which ignores them)
6. **Error Handling**: Clear error reporting via `RE2::ok()` and `RE2::error()`

### Licensing Considerations

**RE2 is BSD-3-Clause licensed** - Fully open-source and free to use.

According to the [Google RE2 GitHub repository](https://github.com/google/re2):
- **License**: BSD-3-Clause (see LICENSE file in repository)
- **Status**: Open-source, actively maintained by Google
- **Latest Release**: Continuously updated (as of 2024)

**Installation Options:**
1. **Git Submodule**: Add RE2 as submodule (consistent with `imgui` approach)
2. **vcpkg**: `vcpkg install re2` (Windows package manager)
3. **Conan**: Package manager integration
4. **CMake FetchContent**: Download and build automatically

**Recommendation**: Use **Git Submodule** for consistency with existing dependencies (like `imgui`).

---

## Performance Comparison

### Expected Performance Gains

RE2 can be **5-10x faster** than `std::regex` for:
- Complex pattern matching (character classes, quantifiers)
- Long text matching (linear time complexity)
- High-throughput scenarios (file indexing, content search)

### Performance Characteristics

| Aspect | std::regex | RE2 |
|--------|------------|-----|
| **Compilation** | Slow (cached) | Fast (RE2 object creation) |
| **Matching Speed** | Slow (backtracking) | Fast (DFA/NFA hybrid) |
| **Time Complexity** | Exponential (worst case) | Linear (guaranteed) |
| **Memory Usage** | Unbounded (worst case) | Bounded (configurable) |
| **Thread Safety** | Requires synchronization | Thread-safe by design |
| **SIMD Support** | No | No (but still fast) |

### Real-World Impact

For this project:
- **FileIndex searches**: Could see 3-8x speedup for regex filename/path matching
- **Content searches**: Could see 5-10x speedup for regex content matching
- **Safety**: Protection against ReDoS (Regular Expression Denial of Service) attacks

### Benchmark Data (Typical)

- **std::regex** (cached): ~500ns per match
- **RE2** (cached): ~50-100ns per match
- **Improvement**: 5-10x faster in typical cases

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

### RE2 API (C++ API, native)

```cpp
// RE2 usage:
// 1. Create RE2 object (can be cached and reused)
RE2::Options options;
options.set_case_sensitive(case_sensitive);
RE2 re(pattern, options);

if (!re.ok()) {
  // Invalid pattern - handle error
  return false;
}

// 2. Match (thread-safe, can be called from multiple threads)
bool matched = RE2::PartialMatch(text, re);  // Substring match (like regex_search)
// or
bool matched = RE2::FullMatch(text, re);      // Full string match (like regex_match)

// 3. With capture groups (if needed)
std::string captured;
if (RE2::PartialMatch(text, re, &captured)) {
  // Use captured value
}
```

### Key Differences

1. **Object-Based**: RE2 uses objects (`RE2`) that can be cached and reused (similar to current cache)
2. **Thread-Safe**: RE2 objects are thread-safe (can be shared across threads)
3. **Error Handling**: Clear error reporting (`re.ok()`, `re.error()`)
4. **Options Object**: Case sensitivity set via `RE2::Options` during construction
5. **Partial vs Full**: Two matching modes (`PartialMatch` vs `FullMatch`)

### Wrapper Compatibility

The current `StdRegexUtils.h` interface can be maintained with RE2 backend:

```cpp
namespace std_regex_utils {
  // Same interface, RE2 backend
  bool RegexMatch(const std::string& pattern, const std::string& text, bool case_sensitive = true);
}
```

---

## Integration Challenges

### 1. API Adaptation

**Challenge**: RE2 uses object-based API (`RE2` objects) vs `std::regex`'s value-based API.

**Solution**: Create wrapper similar to `StdRegexUtils.h` that:
- Maintains same interface (`RegexMatch(pattern, text, case_sensitive)`)
- Caches `RE2` objects (similar to current regex cache)
- Handles RE2 object lifecycle (construction, error checking)

### 2. Pattern Syntax Differences

**Challenge**: RE2 uses a different regex syntax (Perl-like) with some limitations compared to ECMAScript.

**Key Differences:**
- **Mostly compatible**: Basic patterns work the same
- **No backreferences**: `\1`, `\2`, etc. are NOT supported
- **No lookahead/lookbehind**: `(?=...)`, `(?!...)`, `(?<=...)`, `(?<!...)` are NOT supported
- **Unicode**: Limited Unicode support (can be enhanced with ICU library)

**Solution**: 
- Test pattern compatibility
- Document limitations
- **Implement automatic fallback to `std::regex` for unsupported patterns**
- Detect unsupported patterns at compile time (when possible) or handle errors gracefully

### 3. Memory Management

**Challenge**: RE2 objects need to be managed (construction, destruction).

**Solution**:
- Cache RE2 objects (similar to current regex cache)
- Use `std::unique_ptr<RE2>` for automatic cleanup
- RE2 objects are thread-safe, so one object can be shared across threads

### 4. Error Handling

**Challenge**: RE2 has different error reporting (`re.ok()`, `re.error()`) vs `std::regex` exceptions.

**Solution**: Wrap errors and convert to simple boolean return (match/no-match), log errors appropriately.

### 5. PartialMatch vs FullMatch

**Challenge**: RE2 has two matching modes, while current code uses `regex_search` (partial match).

**Solution**: Use `RE2::PartialMatch` which matches `std::regex_search` behavior (substring match).

### 6. Case Sensitivity

**Challenge**: RE2 handles case sensitivity via `RE2::Options` during construction.

**Solution**: 
- Compile separate RE2 objects for case-sensitive and case-insensitive
- Cache both versions (similar to current approach)

---

## Patterns Requiring Fallback to std::regex

RE2 does **not support** several regex features that `std::regex` (ECMAScript) supports. Patterns using these features must fall back to `std::regex`.

### Unsupported Features in RE2

#### 1. **Lookahead and Lookbehind Assertions** ⚠️ **CRITICAL**

**Not Supported:**
- Positive lookahead: `(?=...)`
- Negative lookahead: `(?!...)`
- Positive lookbehind: `(?<=...)`
- Negative lookbehind: `(?<!...)`

**Example from Codebase:**
```cpp
// main_gui.cpp line 1867 - "DoesNotContain" template
"^(?!.*" + text + ").*$"  // ⚠️ Uses negative lookahead - won't work with RE2
```

**Why it fails**: The pattern `^(?!.*text).*$` uses negative lookahead `(?!...)` to assert that `.*text` does NOT appear. Since RE2 doesn't support lookahead assertions, this pattern will fail to compile.

**Alternative Solutions for "DoesNotContain":**

Since RE2 can't express "does not contain" directly via regex, we have several options:

1. **Application-level filtering** (Recommended for RE2):
   ```cpp
   // Instead of regex pattern, use substring search + negation
   bool DoesNotContain(const std::string& text, const std::string& search) {
     return text.find(search) == std::string::npos;
   }
   ```

2. **Match everything, filter results**:
   ```cpp
   // Match all files, then filter out those containing the text
   // Less efficient but works with RE2
   ```

3. **Fallback to std::regex** (Current approach):
   ```cpp
   // For "DoesNotContain" patterns, automatically use std::regex
   if (pattern_uses_lookahead) {
     return std_regex_utils::RegexMatch(pattern, text, case_sensitive);
   }
   ```

**Recommendation**: For the "DoesNotContain" template specifically, consider implementing it as a special case that uses substring search (option 1) rather than regex, which would be:
- **Faster** than regex (even RE2)
- **Simpler** (no regex compilation needed)
- **Works with all engines** (no fallback complexity)

#### 2. **Backreferences**

**Not Supported:**
- `\1`, `\2`, etc. (referencing previous capture groups)
- `\k<name>` (named backreferences)

**Example:**
```cpp
// Match repeated words
"(\\w+)\\s+\\1"  // ⚠️ Requires fallback
```

**Impact**: Low - backreferences are rarely used in file search patterns.

#### 3. **Capture Groups (Functional)**

**Supported:**
- Parentheses `()` work for grouping and capturing
- **Captured content IS accessible** (unlike Hyperscan)
- Can extract captured groups: `RE2::PartialMatch(text, re, &captured1, &captured2)`

**Example:**
```cpp
// This works for matching AND extracting
RE2 re("(\\d{4})-(\\d{2})-(\\d{2})");
std::string year, month, day;
if (RE2::PartialMatch(text, re, &year, &month, &day)) {
  // Use captured values
}
```

**Impact**: **No impact for this project** - The codebase only uses `regex_search` for boolean matching, never extracts capture groups. However, RE2's capture support is a bonus if needed in the future.

#### 4. **Unicode Support**

**Partially Supported:**
- Basic Unicode matching works
- Shorthand classes (`\w`, `\s`, `\d`, `\b`) are ASCII-only by default
- Can be enhanced with ICU library for full Unicode support

**Impact**: Low - file search patterns typically use ASCII.

### Patterns Currently Used in Codebase

Based on code analysis:

1. **Simple patterns** (✅ RE2 compatible):
   - `.*\.cpp$` - File extension matching
   - `^test.*` - Starts with
   - `.*\.(cpp|h|hpp)$` - Multiple extensions

2. **Complex patterns** (✅ RE2 compatible):
   - `.*\d+.*` - Contains digits
   - `.*\d{4}[-_]\d{2}[-_]\d{2}.*` - Date patterns

3. **Requires fallback** (⚠️ Not RE2 compatible):
   - `^(?!.*text).*$` - **DoesNotContain template** (negative lookahead)

### Fallback Strategy

**Recommended Approach:**

1. **Try RE2 compilation first**:
   ```cpp
   RE2::Options options;
   options.set_case_sensitive(case_sensitive);
   RE2 re(pattern, options);
   ```

2. **On compilation failure**, check error:
   - If `!re.ok()` with unsupported feature → fallback to `std::regex`
   - If other error (invalid syntax, etc.) → return false (invalid pattern)

3. **Pattern detection** (optional optimization):
   - Pre-scan pattern for unsupported constructs (lookahead, lookbehind, backreferences)
   - Route directly to `std::regex` if detected
   - This avoids compilation attempt overhead

**Implementation Example:**
```cpp
namespace re2_regex_utils {
  bool RegexMatch(const std::string& pattern, const std::string& text, bool case_sensitive) {
    // Quick check for known unsupported features
    if (ContainsUnsupportedFeatures(pattern)) {
      return std_regex_utils::RegexMatch(pattern, text, case_sensitive);
    }
    
    // Try RE2
    RE2* re = GetOrCompileRE2(pattern, case_sensitive);
    if (re == nullptr || !re->ok()) {
      // Compilation failed - fallback to std::regex
      return std_regex_utils::RegexMatch(pattern, text, case_sensitive);
    }
    
    // Use RE2 for matching
    return RE2::PartialMatch(text, *re);
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
- Backreferences

**No Impact** (already compatible):
- Most file search patterns (extensions, prefixes, character classes, quantifiers)

**Conclusion**: The fallback mechanism is **essential** but will be used **infrequently** (primarily for the DoesNotContain template). Most real-world file search patterns are RE2-compatible.

---

## Wrapper Implementation Strategy

To maintain compatibility with existing code, we recommend implementing a **custom wrapper** that provides the same interface as `StdRegexUtils.h`.

### Why Custom Wrapper?

**Benefits:**
- **Maintains existing interface**: No changes needed in calling code
- **Drop-in replacement**: Can replace `std::regex` backend with RE2 backend
- **Fallback support**: Can fall back to `std::regex` for unsupported patterns
- **Full control**: Can optimize for specific use cases

---

## Proposed Integration Approach

### Phase 1: RE2 Wrapper Implementation (Recommended)

Create a wrapper `Re2Utils.h` that provides the same interface as `StdRegexUtils.h`:

```cpp
namespace re2_regex_utils {
  // Match pattern against text using RE2
  // Returns false on invalid pattern or no match
  bool RegexMatch(const std::string& pattern, const std::string& text, bool case_sensitive = true);
  
  // Clear the RE2 cache
  void ClearCache();
  
  // Get cache size
  size_t GetCacheSize();
}
```

**Implementation Details:**
- **Thread-safe RE2 cache**: Similar to current `StdRegexUtils.h` regex cache
- **RE2 object reuse**: RE2 objects are thread-safe and can be shared
- **Error handling**: Graceful fallback to `std::regex` for unsupported patterns
- **Same interface**: Drop-in replacement for `std_regex_utils::RegexMatch`

**Key Implementation Components:**

1. **RE2 Cache** (similar to `RegexCache` in `StdRegexUtils.h`):
   ```cpp
   class Re2Cache {
     // Thread-safe map: pattern+case_sensitive -> std::unique_ptr<RE2>
     // Compile on first use, cache for reuse
   };
   ```

2. **Pattern Detection** (for fallback):
   ```cpp
   bool RequiresFallback(const std::string& pattern) {
     // Quick check for lookahead/lookbehind
     return pattern.find("(?=") != std::string::npos || 
            pattern.find("(?!") != std::string::npos ||
            // ... other unsupported features
   }
   ```

3. **Match Function**:
   ```cpp
   bool RegexMatch(const std::string& pattern, const std::string& text, bool case_sensitive) {
     // Check for unsupported features
     if (RequiresFallback(pattern)) {
       return std_regex_utils::RegexMatch(pattern, text, case_sensitive);
     }
     
     // Get or compile RE2 object
     RE2* re = GetOrCompileRE2(pattern, case_sensitive);
     if (!re || !re->ok()) {
       // Compilation failed - fallback
       return std_regex_utils::RegexMatch(pattern, text, case_sensitive);
     }
     
     // Use RE2 for matching (thread-safe)
     return RE2::PartialMatch(text, *re);
   }
   ```

### Phase 2: Integration Points

1. **StdRegexUtils.h**: Add option to use RE2 backend
   - Compile-time flag: `USE_RE2`
   - Runtime fallback if RE2 unavailable
   - Or: Create separate `Re2Utils.h` and update `SearchPatternUtils.h` to use it

2. **SearchPatternUtils.h**: Update to use RE2 backend (if enabled)
   - Change `std_regex_utils::RegexMatch` to `re2_regex_utils::RegexMatch`
   - Or: Add compile-time selection

3. **FileIndex.cpp**: No changes needed (uses pattern utilities)

4. **SearchWorker.cpp**: No changes needed (uses pattern utilities)

### Phase 3: Build System Integration

**RE2 Library Integration Options:**

**Option 1: Git Submodule (Recommended for consistency)**
```cmake
# Add RE2 as submodule (like imgui)
# external/re2/

option(USE_RE2 "Use RE2 for regex matching" OFF)

if(USE_RE2)
  add_subdirectory(external/re2)
  target_link_libraries(FindHelper PRIVATE re2)
endif()
```

**Option 2: FetchContent (No submodule needed)**
```cmake
option(USE_RE2 "Use RE2 for regex matching" OFF)

if(USE_RE2)
  include(FetchContent)
  FetchContent_Declare(
    re2
    GIT_REPOSITORY https://github.com/google/re2.git
    GIT_TAG master  # Or specific tag
  )
  FetchContent_MakeAvailable(re2)
  target_link_libraries(FindHelper PRIVATE re2)
endif()
```

**Option 3: vcpkg (Package manager)**
```cmake
option(USE_RE2 "Use RE2 for regex matching" OFF)

if(USE_RE2)
  find_package(re2 CONFIG REQUIRED)
  target_link_libraries(FindHelper PRIVATE re2::re2)
endif()
```

**Dependencies:**
- **RE2 library**: C++ library, can be linked statically (preferred) or dynamically
- **Platform**: Windows 8+ (compatible with project requirements)
- **Compiler**: MSVC 2017+ or GCC/Clang with C++11 support
- **Build time**: RE2 needs to be built (unless using pre-built)
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
- **Compilation Time**: RE2 object creation overhead
- **Memory Usage**: RE2 object memory
- **Cache Hit Rate**: RE2 cache effectiveness
- **Thread Scalability**: Performance with multiple threads
- **Fallback Rate**: How often patterns require fallback to std::regex

---

## Comparison: RE2 vs Hyperscan

| Aspect | RE2 | Hyperscan |
|--------|-----|-----------|
| **Performance** | 5-10x faster | 10-100x faster |
| **API Complexity** | Simple (C++) | Complex (C API) |
| **Thread Safety** | Built-in | Requires scratch space |
| **Memory Usage** | Low | Higher (scratch space) |
| **Pattern Support** | Most patterns | Limited (no lookahead) |
| **Capture Groups** | Supported | Ignored |
| **SIMD Support** | No | Yes (SSE4.2, AVX2) |
| **Integration Effort** | Low | Medium-High |
| **License** | BSD-3-Clause | BSD-3-Clause |

**Recommendation**: 
- **RE2** if you want simpler integration and good performance (5-10x improvement)
- **Hyperscan** if you need maximum performance (10-100x improvement) and can handle more complex integration

---

## Recommendations

### Option 1: Full RE2 Integration (Recommended for Simplicity)

**Pros:**
- Significant performance improvements (5-10x)
- Simpler API than Hyperscan (C++ vs C)
- Thread-safe by design (no scratch space management)
- Better pattern support than Hyperscan (capture groups work)
- Maintains current API (wrapper hides complexity)
- Lower memory overhead than Hyperscan

**Cons:**
- Additional dependency (RE2 library - but can be statically linked)
- Potential compatibility issues with some regex patterns (handled via fallback)
- Not as fast as Hyperscan (but still much faster than std::regex)

**Implementation Effort**: 
- **RE2 wrapper**: Low-Medium (1-2 days for wrapper + testing)
- Includes: wrapper implementation, build integration, testing, fallback mechanism

### Option 2: Hybrid Approach (Recommended for Safety)

**Pros:**
- Best of both worlds: RE2 for performance, std::regex for compatibility
- Gradual migration path
- Fallback for unsupported patterns

**Cons:**
- More complex code (two backends)
- Need to decide which patterns use which backend

**Implementation:**
- Use RE2 for supported patterns
- Fall back to `std::regex` for unsupported patterns
- Compile-time flag to enable/disable RE2

### Option 3: Keep std::regex (Status Quo)

**Pros:**
- No changes needed
- Standard library (no external dependency)
- Full ECMAScript compatibility

**Cons:**
- Slower performance (known bottleneck)
- No protection against ReDoS attacks
- Limited scalability

---

## Decision Matrix

| Factor | Weight | RE2 | std::regex |
|--------|--------|-----|------------|
| **Performance** | High | ⭐⭐⭐⭐⭐ | ⭐⭐ |
| **Compatibility** | High | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| **Complexity** | Medium | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| **Dependencies** | Medium | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| **Maintainability** | Medium | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ |
| **Safety** | Medium | ⭐⭐⭐⭐⭐ | ⭐⭐ |
| **Future-Proof** | Low | ⭐⭐⭐⭐ | ⭐⭐⭐ |

**Weighted Score**: RE2 wins on performance and safety, but std::regex wins on simplicity and compatibility.

---

## Next Steps

If proceeding with RE2 integration:

1. **Research Phase** (1-2 hours):
   - Test RE2 compilation on Windows
   - Verify pattern compatibility with current usage
   - Measure baseline performance with std::regex
   - Review RE2 API documentation

2. **Prototype Phase** (4-8 hours):
   - Create `Re2Utils.h` wrapper
   - Implement RE2 cache (similar to `RegexCache` in `StdRegexUtils.h`)
   - Implement basic `RegexMatch` function with fallback detection
   - Add error handling and logging

3. **Integration Phase** (2-4 hours):
   - Integrate RE2 into build system (CMake)
   - Add compile-time flag `USE_RE2`
   - Update `SearchPatternUtils.h` to optionally use RE2 backend
   - Ensure graceful fallback when RE2 unavailable

4. **Testing Phase** (4-8 hours):
   - Unit tests for RE2 wrapper
   - Test fallback mechanism (unsupported patterns)
   - Performance benchmarks (compare with std::regex)
   - Compatibility testing (pattern syntax)
   - Thread safety testing
   - Memory leak testing

5. **Documentation Phase** (1-2 hours):
   - Update build instructions
   - Document pattern limitations
   - Add performance notes
   - Document fallback behavior

**Total Estimated Effort**: 
- **RE2 wrapper**: 12-22 hours (1.5-2.75 days)

---

## References

- [RE2 GitHub Repository](https://github.com/google/re2)
- [RE2 API Documentation](https://github.com/google/re2/wiki/Syntax)
- [RE2 Performance](https://github.com/google/re2/wiki/WhyRE2)
- [RE2 Installation Guide](https://github.com/google/re2/wiki/Install)
- [RE2 vs std::regex Comparison](https://www.scnlib.dev/group__regex.html)

---

## Conclusion

RE2 offers significant performance benefits for regex matching in this project, especially for file indexing and content search operations. The integration is **more straightforward** than Hyperscan due to:

1. **Simpler API**: C++ interface vs C API
2. **No scratch space**: Thread-safe by design, no per-thread allocation needed
3. **Better pattern support**: Capture groups work (unlike Hyperscan)

The integration requires:

1. **Custom wrapper implementation** (in-project, similar to `StdRegexUtils.h`)
2. **Fallback mechanism** to `std::regex` for unsupported patterns (primarily lookahead/lookbehind)
3. **Build system integration** for RE2 library (submodule, FetchContent, or vcpkg)
4. **Thorough testing** for pattern compatibility and performance

**Key Finding**: Most patterns used in the codebase are RE2-compatible. The main exception is the **DoesNotContain template** which uses negative lookahead (`^(?!.*text).*$`) and will require fallback to `std::regex`.

**Recommendation**: Proceed with **Option 2 (Hybrid Approach)** - implement RE2 with automatic fallback to std::regex for unsupported patterns. This provides:
- **Significant performance improvement** for compatible patterns (5-10x speedup)
- **Full compatibility** for all patterns (via fallback)
- **Transparent operation** (users don't need to know which engine is used)
- **Simpler integration** than Hyperscan (C++ API, no scratch space)
- **Minimal dependencies**: RE2 library needed (can be statically linked)

**Implementation Strategy**:
- **Custom wrapper**: Implement `Re2Utils.h` directly in project (similar to `StdRegexUtils.h`)
- **RE2 library**: Integrate via submodule (consistent with `imgui` approach) or FetchContent
- **Static linking**: Prefer static linking of RE2 to avoid runtime dependencies

