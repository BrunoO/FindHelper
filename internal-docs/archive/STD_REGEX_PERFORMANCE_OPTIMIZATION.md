# std::regex Performance Optimization Guide

## Current Performance Issues

`std::regex` is known to be slow, especially on Windows/MSVC. The current implementation already includes:
- ✅ Regex caching (critical optimization)
- ✅ `regex_constants::optimize` flag
- ✅ Thread-safe cache with mutex protection

However, `std::regex` still has fundamental performance limitations.

## Why std::regex is Slow

1. **MSVC Implementation**: Microsoft's `std::regex` implementation is notoriously slow (often 10-100x slower than alternatives)
2. **Backtracking**: Uses recursive backtracking which can be exponential for complex patterns
3. **No DFA**: Doesn't use deterministic finite automaton (DFA) for simple patterns
4. **Exception Handling**: Throws exceptions for invalid patterns (overhead)

## Optimization Strategies

### 1. Pattern-Specific Optimizations (Quick Wins)

**Detect simple patterns and route to SimpleRegex:**

```cpp
// In StdRegexUtils.h - add pattern analysis
inline bool IsSimplePattern(const std::string& pattern) {
  // Patterns that only use features supported by SimpleRegex
  // Features: ., *, ^, $, literal characters
  // Check if pattern only contains these characters
  for (char c : pattern) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && 
        c != '.' && c != '*' && c != '^' && c != '$') {
      return false; // Uses advanced features, need std::regex
    }
  }
  return true;
}

// Route simple patterns to fast SimpleRegex
inline bool RegexMatch(const std::string& pattern, const std::string& text, bool case_sensitive = true) {
  if (pattern.empty()) {
    return false;
  }
  
  // Fast path: Use SimpleRegex for patterns it can handle
  if (IsSimplePattern(pattern)) {
    if (case_sensitive) {
      return simple_regex::RegExMatch(pattern, text);
    } else {
      return simple_regex::RegExMatchI(pattern, text);
    }
  }
  
  // Slow path: Use std::regex for complex patterns
  // ... existing std::regex code ...
}
```

**Benefits:**
- Zero-cost for simple patterns (most common case)
- Falls back to std::regex only when needed
- Easy to implement, no external dependencies

### 2. Use `nosubs` Flag (If Not Capturing)

If you don't need capture groups, add `regex_constants::nosubs`:

```cpp
auto flags = std::regex_constants::ECMAScript | 
             std::regex_constants::optimize |
             std::regex_constants::nosubs;  // Add this if not capturing
```

**Benefits:**
- 10-30% faster matching
- Less memory usage
- Only works if you don't need capture groups

### 3. Pre-compile Common Patterns

For frequently used patterns, pre-compile them at startup:

```cpp
// In initialization code
void PrecompileCommonPatterns() {
  std::vector<std::string> common_patterns = {
    ".*\\.(cpp|h|hpp)$",  // C++ files
    ".*\\.(exe|dll)$",     // Executables
    "^[A-Z].*",            // Starts with capital
    // ... add more
  };
  
  for (const auto& pattern : common_patterns) {
    GetCache().GetRegex(pattern, true);   // Pre-compile
    GetCache().GetRegex(pattern, false);  // Case-insensitive too
  }
}
```

## Alternative Libraries (Better Performance)

### Option 1: RE2 (Google's Regex Library) ⭐ **RECOMMENDED**

**Why RE2:**
- **10-100x faster** than std::regex
- **Safe**: No catastrophic backtracking (guaranteed O(n) time)
- **C++ API**: Easy to integrate
- **Well-maintained**: Used by Google in production
- **Windows support**: Works on Windows

**Integration:**
```cpp
// Add RE2 as submodule or dependency
// CMakeLists.txt:
# find_package(re2) or add_subdirectory(external/re2)

// RE2RegexUtils.h (new file)
#include <re2/re2.h>
#include <unordered_map>
#include <mutex>

namespace re2_regex_utils {
  class RegexCache {
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<RE2>> cache_;
    
  public:
    const RE2* GetRegex(const std::string& pattern, bool case_sensitive) {
      std::string key = (case_sensitive ? "1:" : "0:") + pattern;
      
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = cache_.find(key);
      if (it != cache_.end()) {
        return it->second.get();
      }
      
      // Compile and cache
      RE2::Options options;
      options.set_case_sensitive(case_sensitive);
      auto regex = std::make_unique<RE2>(pattern, options);
      
      if (!regex->ok()) {
        return nullptr; // Invalid pattern
      }
      
      auto [inserted_it, inserted] = cache_.emplace(key, std::move(regex));
      return inserted_it->second.get();
    }
  };
  
  inline bool RegexMatch(const std::string& pattern, const std::string& text, bool case_sensitive = true) {
    static RegexCache cache;
    const RE2* compiled = cache.GetRegex(pattern, case_sensitive);
    if (!compiled) return false;
    
    return RE2::PartialMatch(text, *compiled);
  }
}
```

**Performance Comparison:**
- std::regex: ~500ns per match (cached)
- RE2: ~50-100ns per match (cached)
- **5-10x faster** in typical cases

**Trade-offs:**
- ✅ Much faster
- ✅ Safer (no exponential backtracking)
- ✅ Better error handling
- ⚠️ External dependency (but header-only or easy to integrate)
- ⚠️ Slightly different syntax (mostly compatible with ECMAScript)

### Option 2: PCRE2 (Perl Compatible)

**Why PCRE2:**
- Very fast (often faster than RE2 for complex patterns)
- Full Perl regex compatibility
- Well-optimized C library

**Trade-offs:**
- ✅ Very fast
- ✅ Full feature set
- ⚠️ C API (need C++ wrapper)
- ⚠️ Can still have catastrophic backtracking (less safe than RE2)

### Option 3: Boost.Regex

**Why Boost.Regex:**
- Might be faster than std::regex on some platforms
- Similar API to std::regex (easy migration)
- Well-tested

**Trade-offs:**
- ✅ Easy migration from std::regex
- ✅ Better than std::regex on some platforms
- ⚠️ Still slower than RE2/PCRE2
- ⚠️ Boost dependency (large)

### Option 4: Expand SimpleRegex

**Add more features to SimpleRegex.h:**
- Character classes: `[a-z]`, `[0-9]`
- Quantifiers: `+`, `?`, `{n,m}`
- Alternation: `(a|b)`

**Trade-offs:**
- ✅ No external dependencies
- ✅ Very fast (header-only)
- ⚠️ Limited feature set
- ⚠️ More implementation work

## Recommended Approach

### Phase 1: Quick Wins (Implement Now)
1. **Pattern routing**: Detect simple patterns, use SimpleRegex
2. **Add `nosubs` flag**: If not using capture groups
3. **Pre-compile common patterns**: At startup

**Expected improvement: 20-50% faster for common cases**

### Phase 2: Consider RE2 (If Still Too Slow)
1. Add RE2 as optional dependency
2. Create RE2RegexUtils.h wrapper
3. Make it configurable: `USE_RE2` flag
4. Fallback to std::regex if RE2 not available

**Expected improvement: 5-10x faster overall**

## Implementation Priority

1. **High Priority**: Pattern routing to SimpleRegex (easy, big win)
2. **Medium Priority**: Add `nosubs` flag (if applicable)
3. **Low Priority**: RE2 integration (if Phase 1 not enough)

## Code Example: Pattern Routing

```cpp
// Add to StdRegexUtils.h
namespace std_regex_utils {
  namespace detail {
    // Check if pattern only uses SimpleRegex features
    inline bool IsSimplePattern(const std::string& pattern) {
      for (size_t i = 0; i < pattern.size(); ++i) {
        char c = pattern[i];
        
        // Allow: alphanumeric, ., *, ^, $
        if (std::isalnum(static_cast<unsigned char>(c)) || 
            c == '.' || c == '*' || c == '^' || c == '$') {
          continue;
        }
        
        // Allow escaped characters
        if (c == '\\' && i + 1 < pattern.size()) {
          ++i; // Skip next character
          continue;
        }
        
        // Anything else requires std::regex
        return false;
      }
      return true;
    }
  }
  
  // Enhanced RegexMatch with fast path
  inline bool RegexMatch(const std::string& pattern, const std::string& text, bool case_sensitive = true) {
    if (pattern.empty()) {
      return false;
    }
    
    // Fast path: Route simple patterns to SimpleRegex
    if (detail::IsSimplePattern(pattern)) {
      if (case_sensitive) {
        return simple_regex::RegExMatch(pattern, text);
      } else {
        return simple_regex::RegExMatchI(pattern, text);
      }
    }
    
    // Slow path: Use std::regex for complex patterns
    const std::regex* compiled_regex = GetCache().GetRegex(pattern, case_sensitive);
    if (compiled_regex == nullptr) {
      return false;
    }
    
    try {
      return std::regex_search(text, *compiled_regex);
    } catch (const std::regex_error& [[maybe_unused]] e) {
      LOG_WARNING_BUILD("Regex match error for pattern '" << pattern << "': " << e.what());
      return false;
    } catch (...) {
      LOG_WARNING_BUILD("Unexpected error during regex match for pattern '" << pattern << "'");
      return false;
    }
  }
}
```

## Performance Testing

Before and after optimization, measure:
1. Time per match (cached vs uncached)
2. Cache hit rate
3. Pattern complexity distribution
4. Memory usage

Use a profiler to identify actual bottlenecks.
