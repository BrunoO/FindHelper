# Further Regex Optimization Opportunities

## Current State ✅

The regex implementation is already well-optimized:
- ✅ Thread-local cache (no mutex overhead)
- ✅ Pattern routing to SimpleRegex (fast path)
- ✅ `regex_match` for anchored patterns
- ✅ Optimal regex flags (`optimize`, `nosubs`)

## Additional Optimization Opportunities

### 1. Cache Key Optimization (LOW PRIORITY)

**Current**: String concatenation for cache key
```cpp
std::string MakeKey(const std::string& pattern, bool case_sensitive) const {
  return (case_sensitive ? "1:" : "0:") + pattern;  // String allocation
}
```

**Optimization**: Use `std::pair` as key (no string allocation)
```cpp
using CacheKey = std::pair<std::string, bool>;

// Custom hash for pair
namespace std {
  template<>
  struct hash<CacheKey> {
    size_t operator()(const CacheKey& key) const {
      return std::hash<std::string>{}(key.first) ^ 
             (std::hash<bool>{}(key.second) << 1);
    }
  };
}

std::unordered_map<CacheKey, CachedRegex> cache_;
```

**Expected Impact**: 5-15% faster cache lookups
**Complexity**: Low
**Priority**: Low (cache lookups are already fast)

---

### 2. Early Exit for Literal Patterns (MEDIUM PRIORITY)

**Opportunity**: If pattern has no special characters (after unescaping), use simple string comparison

**Implementation**:
```cpp
namespace detail {
  inline bool IsLiteralPattern(const std::string& pattern) {
    for (size_t i = 0; i < pattern.size(); ++i) {
      char c = pattern[i];
      
      // Check for escape sequence
      if (c == '\\' && i + 1 < pattern.size()) {
        ++i; // Skip escaped character
        continue;
      }
      
      // Check for special regex characters
      if (c == '.' || c == '*' || c == '^' || c == '$' || 
          c == '+' || c == '?' || c == '[' || c == '(' || c == '{') {
        return false;
      }
    }
    return true;
  }
}

// In RegexMatch:
if (detail::IsLiteralPattern(pattern)) {
  // Fast path: Simple string comparison
  if (case_sensitive) {
    return text.find(pattern) != std::string::npos;
  } else {
    // Case-insensitive search
    return ContainsSubstringI(text, pattern);
  }
}
```

**Expected Impact**: 10-50x faster for literal patterns (common case)
**Complexity**: Low-Medium
**Priority**: Medium (common case, significant speedup)

---

### 3. Prefix/Suffix Optimization (LOW PRIORITY)

**Opportunity**: For patterns starting with `^` or ending with `$`, use string prefix/suffix checks before regex

**Implementation**:
```cpp
namespace detail {
  inline bool HasPrefixAnchor(const std::string& pattern) {
    return !pattern.empty() && pattern[0] == '^';
  }
  
  inline bool HasSuffixAnchor(const std::string& pattern) {
    return pattern.size() > 1 && pattern.back() == '$' && 
           pattern[pattern.size() - 2] != '\\';
  }
  
  // Extract literal prefix (characters before first special char)
  inline std::string ExtractLiteralPrefix(const std::string& pattern) {
    std::string prefix;
    for (size_t i = 0; i < pattern.size(); ++i) {
      char c = pattern[i];
      if (c == '\\' && i + 1 < pattern.size()) {
        prefix += pattern[i + 1]; // Add escaped char
        ++i;
        continue;
      }
      if (c == '.' || c == '*' || c == '^' || c == '$' || 
          c == '+' || c == '?' || c == '[' || c == '(') {
        break; // Stop at first special char
      }
      prefix += c;
    }
    return prefix;
  }
}

// In RegexMatch:
if (detail::HasPrefixAnchor(pattern)) {
  std::string literal_prefix = detail::ExtractLiteralPrefix(pattern.substr(1));
  if (!literal_prefix.empty()) {
    // Quick check: text must start with literal prefix
    if (case_sensitive) {
      if (text.substr(0, literal_prefix.size()) != literal_prefix) {
        return false; // Fast exit
      }
    } else {
      // Case-insensitive prefix check
      if (!StartsWithI(text, literal_prefix)) {
        return false; // Fast exit
      }
    }
  }
}
```

**Expected Impact**: 20-50% faster for patterns with literal prefixes
**Complexity**: Medium (need to handle escapes correctly)
**Priority**: Low (only helps specific patterns)

---

### 4. Pattern Compilation Caching Across Searches (MEDIUM PRIORITY)

**Current**: Thread-local cache is per-thread, threads are created per search

**Opportunity**: Add a shared "warmup" cache that persists across searches

**Implementation**:
```cpp
// Shared cache for warmup (persists across searches)
static ThreadLocalRegexCache& GetSharedWarmupCache() {
  static ThreadLocalRegexCache cache;  // Shared across all threads
  static std::shared_mutex mutex;
  
  // Use read-write lock for shared cache
  std::shared_lock<std::shared_mutex> lock(mutex);
  return cache;
}

// Hybrid approach: Try thread-local first, then shared
const std::regex* GetRegex(const std::string& pattern, bool case_sensitive) {
  // First, try thread-local (fast, no mutex)
  auto* local = GetCache().GetRegex(pattern, case_sensitive);
  if (local) return local;
  
  // Not in thread-local, try shared warmup cache
  auto* shared = GetSharedWarmupCache().GetRegex(pattern, case_sensitive);
  if (shared) {
    // Copy to thread-local for future use
    GetCache().CopyFromShared(pattern, case_sensitive, shared);
    return GetCache().GetRegex(pattern, case_sensitive);
  }
  
  // Not in either cache, compile and add to both
  // ... compile and cache in both ...
}
```

**Expected Impact**: Faster first search in new threads (patterns already compiled)
**Complexity**: Medium (hybrid approach adds complexity)
**Priority**: Medium (only helps if threads are reused or patterns persist)

---

### 5. SIMD-Optimized String Matching (LOW PRIORITY)

**Opportunity**: Use SIMD instructions for literal substring matching

**Implementation**: Use platform-specific SIMD (SSE/AVX) for string search
- Only for literal patterns or literal prefixes
- Requires platform-specific code
- Significant complexity

**Expected Impact**: 2-4x faster for literal matching
**Complexity**: High (platform-specific, complex)
**Priority**: Low (only for specific cases, high complexity)

---

### 6. Regex Engine Replacement (FUTURE CONSIDERATION)

**Opportunity**: Replace `std::regex` with faster alternatives

**Options**:
1. **RE2** (Google) - 5-10x faster, guaranteed O(n) time
2. **Hyperscan** (Intel) - 10-100x faster, SIMD-optimized
3. **PCRE2** - Fast, full Perl compatibility

**Trade-offs**:
- ✅ Much faster (5-100x)
- ⚠️ External dependency
- ⚠️ Different API (need wrapper)
- ⚠️ Feature differences

**Expected Impact**: 5-100x faster regex matching
**Complexity**: High (external dependency, API changes)
**Priority**: Low (only if std::regex becomes bottleneck)

**Note**: Already documented in `docs/RE2_FEASIBILITY_STUDY.md` and `docs/HYPERSCAN_FEASIBILITY_STUDY.md`

---

### 7. Pattern Analysis and Optimization Hints (LOW PRIORITY)

**Opportunity**: Analyze patterns to provide optimization hints to regex engine

**Implementation**: Detect patterns that can benefit from specific optimizations:
- Literal-only patterns → use string search
- Anchored patterns → use regex_match
- Prefix patterns → check prefix first
- Character class patterns → optimize character class matching

**Expected Impact**: 10-30% faster for specific pattern types
**Complexity**: Medium (pattern analysis logic)
**Priority**: Low (overlaps with existing optimizations)

---

## Recommended Implementation Order

### Phase 1: Quick Wins (Implement Now)
1. ✅ **Early Exit for Literal Patterns** - High impact, low complexity
   - Detects patterns with no special characters
   - Uses simple string search instead of regex
   - 10-50x faster for common case

### Phase 2: Evaluate Results
- Profile after Phase 1
- Measure actual performance gains
- Identify remaining bottlenecks

### Phase 3: Additional Optimizations (If Needed)
2. **Cache Key Optimization** - Low complexity, moderate benefit
3. **Prefix/Suffix Optimization** - Medium complexity, specific cases
4. **Shared Warmup Cache** - Medium complexity, helps with thread reuse

### Phase 4: Major Changes (Only If Profiling Shows Need)
5. **SIMD Optimizations** - High complexity, specific cases
6. **Regex Engine Replacement** - High complexity, external dependency

---

## Performance Impact Estimates

| Optimization | Impact | Complexity | Priority |
|-------------|--------|------------|----------|
| Literal Pattern Detection | 10-50x | Low | **HIGH** |
| Cache Key Optimization | 5-15% | Low | Low |
| Prefix/Suffix Check | 20-50% | Medium | Low |
| Shared Warmup Cache | 10-30% | Medium | Medium |
| SIMD String Matching | 2-4x | High | Low |
| Regex Engine Replacement | 5-100x | High | Low |

---

## Conclusion

**Top Recommendation**: Implement **Literal Pattern Detection** (#2)
- High impact (10-50x faster for common case)
- Low complexity
- No external dependencies
- Significant speedup for simple searches

The current implementation is already very well optimized. Additional optimizations should be driven by profiling data to identify actual bottlenecks.
