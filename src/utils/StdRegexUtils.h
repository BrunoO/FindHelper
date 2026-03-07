#pragma once

#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "utils/Logger.h"
#include "utils/RegexAliases.h"
#include "utils/SimpleRegex.h"
#include "utils/StringSearch.h"
#include "utils/StringUtils.h"

namespace std_regex_utils {

// Thread-local regex cache
// Caches compiled std::regex objects to avoid expensive recompilation
// OPTIMIZED: Uses thread_local storage - no mutex needed, each thread has its own cache
//
// NOTE: This cache currently has no size limit. For a desktop application, this is acceptable
// because:
// - Each thread has its own cache instance (thread-local)
// - Typical usage: 5-20 unique regex patterns per session
// - Even 1000 entries = ~40 KB per thread (negligible for desktop apps)
// - Cache is only used for 'rs:' prefix patterns (most patterns use faster paths)
//
// FUTURE OPTIMIZATION: If profiling shows unbounded growth is a concern (e.g., long-running
// apps with 1000+ unique patterns), consider implementing LRU eviction:
// - Add std::list<std::string> for LRU tracking
// - Add max size constant (e.g., 128-512 entries)
// - On cache miss when full: evict least recently used entry
// - On cache hit: move entry to front of LRU list
// - Expected overhead: ~30-60 ns per lookup (negligible compared to regex compilation)
// - See docs/PERFORMANCE_IMPACT_ANALYSIS_FIXES_2025-01-02.md for detailed analysis
class ThreadLocalRegexCache {
public:
  struct CachedRegex {
    regex_t regex{};  // NOLINT(misc-non-private-member-variables-in-classes,readability-redundant-member-init) - simple POD-like cache entry; explicit default for clarity
    bool case_sensitive;  // NOLINT(misc-non-private-member-variables-in-classes)
    
    CachedRegex(std::string_view pattern, bool case_sensitive_flag) 
      : case_sensitive(case_sensitive_flag) {
      // Empty patterns are invalid and should not reach here (checked in GetRegex)
      // But add defensive check to prevent crashes
      if (pattern.empty()) {
        throw regex_error_t(regex_constants::error_collate);  // NOLINT(hicpp-exception-baseclass) - regex_error_t provided by RegexAliases, may not derive from std::exception on all platforms
      }
      // NOLINTNEXTLINE(hicpp-signed-bitwise) - bitmask flags by design; type comes from regex_t::flag_type
      auto flags = regex_constants::ECMAScript | 
                   regex_constants::optimize |
                   regex_constants::nosubs;
      if (!case_sensitive_flag) {
        flags |= regex_constants::icase;  // NOLINT(hicpp-signed-bitwise)
      }
      // Convert string_view to std::string for regex_t constructor (requires std::string)
      regex = regex_t(std::string(pattern), flags);
    }
    
    // Move constructor - noexcept for efficient container operations
    CachedRegex(CachedRegex&& other) noexcept = default;
    
    // Move assignment - noexcept for efficient container operations
    CachedRegex& operator=(CachedRegex&& other) noexcept = default;
    
    // Default destructor (regex_t handles cleanup)
    ~CachedRegex() = default;
    
    // Delete copy operations (regex_t is not copyable)
    CachedRegex(const CachedRegex&) = delete;
    CachedRegex& operator=(const CachedRegex&) = delete;
  };
  
  // Get a compiled regex from thread-local cache, or compile and cache it
  // Returns nullptr on invalid pattern
  // OPTIMIZED: No mutex needed - each thread has its own cache instance
  const regex_t* GetRegex(std::string_view pattern, bool case_sensitive) {
    if (pattern.empty()) {
      return nullptr;
    }
    
    auto key = MakeKey(pattern, case_sensitive);
    
    // Simple lookup - no locking needed (thread-local cache)
    if (auto it = cache_.find(key); it != cache_.end()) {
      return &it->second.regex;
    }
    
    // Pattern not in cache, compile it
    try {
      auto [inserted_it, inserted] = cache_.try_emplace(
        key, pattern, case_sensitive);
      
      if (inserted) {
        return &inserted_it->second.regex;
      }
    } catch (const regex_error_t& e) {
      (void)e;  // Suppress unused variable warning
      LOG_WARNING_BUILD("Invalid regex pattern: '" << pattern << "' - " << e.what());
      return nullptr;
    } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all needed for non-standard exceptions from regex compilation
      LOG_WARNING_BUILD("Unexpected error compiling regex pattern: '" << pattern << "'");
      return nullptr;
    }
    
    return nullptr;
  }
  
  void Clear() {
    cache_.clear();
  }
  
  [[nodiscard]] size_t Size() const {
    return cache_.size();
  }
  
private:
  std::unordered_map<std::string, CachedRegex> cache_;  // NOLINT(readability-identifier-naming) - project convention: snake_case_ for members
  
  [[nodiscard]] std::string MakeKey(std::string_view pattern, bool case_sensitive) const {
    return (case_sensitive ? "1:" : "0:") + std::string(pattern);
  }
};

// Get thread-local regex cache instance
// OPTIMIZED: Each thread gets its own cache - no mutex needed, zero contention
inline ThreadLocalRegexCache& GetCache() {
  thread_local ThreadLocalRegexCache cache;  // NOSONAR(cpp:S6018) - Function-local thread_local is correct pattern, inline not allowed in block scope
  return cache;
}

namespace detail {
  // Helper: Route simple patterns to SimpleRegex (case-sensitive or case-insensitive)
  inline bool RouteSimplePattern(std::string_view pattern, std::string_view text, bool case_sensitive) {
    if (case_sensitive) {
      return simple_regex::RegExMatch(pattern, text);
    }
    return simple_regex::RegExMatchI(pattern, text);
  }
  
  // Helper: Get compiled regex from cache (returns nullptr on failure)
  inline const regex_t* GetCompiledRegex(std::string_view pattern, bool case_sensitive) {
    return GetCache().GetRegex(pattern, case_sensitive);
  }
  
  // Helper: Execute regex match with exception handling
  // Template parameter: MatchFunc is either regex_match or regex_search
  template<typename MatchFunc>
  inline bool ExecuteRegexMatch([[maybe_unused]] std::string_view pattern_str, std::string_view text_str,  // NOSONAR(cpp:S1172) - pattern_str used in LOG_WARNING_BUILD
                                const regex_t* compiled_regex, MatchFunc match_func) {
    try {
      // Convert string_view to std::string for regex functions (regex requires std::string)
      const std::string text_string(text_str);
      return match_func(text_string, *compiled_regex);
    } catch (const regex_error_t& e) {
      (void)e;  // Suppress unused variable warning
      LOG_WARNING_BUILD("Regex match error for pattern '" << pattern_str << "': " << e.what());
      return false;
    } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all needed for non-standard exceptions from regex matching
      LOG_WARNING_BUILD("Unexpected error during regex match for pattern '" << pattern_str << "'");
      return false;
    }
  }
  
  // Helper: Match using regex (regex_match for full match, regex_search otherwise)
  inline bool MatchWithRegex(std::string_view pattern_str, std::string_view text_str,
                             const regex_t* compiled_regex, bool requires_full_match) {
    if (requires_full_match) {
      return ExecuteRegexMatch(pattern_str, text_str, compiled_regex,
                               [](const std::string& t, const regex_t& r) { return regex_match(t, r); });
    }
    return ExecuteRegexMatch(pattern_str, text_str, compiled_regex,
                            [](const std::string& t, const regex_t& r) { return regex_search(t, r); });
  }
  
  /**
   * @brief Count consecutive backslashes immediately before the $ character
   *
   * Helper function to determine if $ is escaped by counting trailing backslashes.
   * Used to detect if a pattern ends with an anchor ($) or an escaped dollar sign.
   *
   * @param pattern Pattern to analyze (must have at least 2 characters and end with $)
   * @return Number of consecutive backslashes before the $
   */
  inline size_t CountTrailingBackslashes(std::string_view pattern) {
    size_t backslash_count = 0;
    if (pattern.size() < 2) {
      return backslash_count;
    }

    // Walk backwards from the character before '$' until we hit a non-backslash
    // or reach the start of the string. Use early returns instead of break to
    // keep loop structure simple and avoid multiple break statements.
    for (size_t i = pattern.size() - 2;;) {
      if (pattern[i] == '\\') {
        ++backslash_count;
        if (i == 0) {
          return backslash_count;
        }
        --i;
        continue;
      }
      return backslash_count;
    }
  }

  // Check if pattern only uses SimpleRegex features (., *, ^, $, literals)
  // If true, we can route to fast SimpleRegex instead of slow std::regex
  // OPTIMIZED: Uses string_view to avoid unnecessary string allocations
  inline bool IsSimplePattern(std::string_view pattern) {
    for (size_t i = 0; i < pattern.size(); ++i) {  // NOSONAR(cpp:S886) - Index-based loop required for character-by-character pattern checking
      const char c = pattern[i];
      
      // Allow: alphanumeric, ., *, ^, $ (SimpleRegex features)
      if ((std::isalnum(static_cast<unsigned char>(c)) != 0) || 
          c == '.' || c == '*' || c == '^' || c == '$') {
        continue;
      }
      
      // Allow escaped characters (backslash followed by any char)
      if (c == '\\' && i + 1 < pattern.size()) {
        ++i; // Skip the escaped character
        continue;
      }
      
      // Anything else requires std::regex (character classes, quantifiers, etc.)  // NOSONAR - Documentation comment
      return false;
    }
    return true;
  }
  
  // Check if pattern is a literal string (no regex special characters)
  // If true, we can use fast string search instead of regex
  // OPTIMIZED: Detects literal patterns for 10-50x speedup, uses string_view to avoid allocations
  inline bool IsLiteralPattern(std::string_view pattern) {
    for (size_t i = 0; i < pattern.size(); ++i) {  // NOSONAR(cpp:S886) - Index-based loop required for character-by-character pattern checking
      const char c = pattern[i];
      
      // Check for escape sequence
      if (c == '\\' && i + 1 < pattern.size()) {
        ++i; // Skip escaped character (it's literal)
        continue;
      }
      
      // Check for regex special characters
      // If we find any, it's not a literal pattern
      if (c == '.' || c == '*' || c == '^' || c == '$' || 
          c == '+' || c == '?' || c == '[' || c == ']' || 
          c == '(' || c == ')' || c == '{' || c == '}' || 
          c == '|' || c == '\\') {
        return false;
      }
    }
    return true; // No special characters found, it's a literal pattern
  }
  
  // Check if pattern requires full string match (starts with ^ and ends with $)
  // If true, we can use regex_match instead of regex_search for better performance
  // OPTIMIZED: Detects anchored patterns to use more efficient regex_match, uses string_view to avoid allocations
  inline bool RequiresFullMatch(std::string_view pattern) {
    if (pattern.size() < 2) {
      return false; // Need at least ^ and $ for full match
    }
    
    // Check for ^ at start (not escaped)
    const bool starts_with_anchor = (pattern[0] == '^');
    
    // Check for $ at end (not escaped)
    // Count consecutive backslashes before $: if even number (or zero), $ is not escaped
    bool ends_with_anchor = false;
    if (pattern.back() == '$') {
      if (pattern.size() == 1) {
        // Just $, not escaped
        ends_with_anchor = true;
      } else {
        ends_with_anchor = CountTrailingBackslashes(pattern) % 2 == 0;
      }
    }
    
    return starts_with_anchor && ends_with_anchor;
  }
}

// Pattern analysis result (for pre-analysis optimization)
struct PatternAnalysis {
  bool is_literal;  // NOLINT(misc-non-private-member-variables-in-classes) - POD-style analysis result; public fields are intentional
  bool is_simple;  // NOLINT(misc-non-private-member-variables-in-classes)
  bool requires_full_match;  // NOLINT(misc-non-private-member-variables-in-classes)
  
  explicit PatternAnalysis(std::string_view pattern)
    : is_literal(detail::IsLiteralPattern(pattern)),
      is_simple(detail::IsSimplePattern(pattern)),
      requires_full_match(detail::RequiresFullMatch(pattern)) {
  }
};

// Optimized RegexMatch that uses pre-analyzed pattern flags
// OPTIMIZED: Skips pattern analysis (done once when creating matcher)
inline bool RegexMatchOptimized(std::string_view pattern, std::string_view text, 
                                 const PatternAnalysis& analysis, bool case_sensitive = true) {
  if (pattern.empty()) {
    return false;
  }
  
  // Fastest path: Literal patterns (pre-analyzed)
  if (analysis.is_literal) {
    if (case_sensitive) {
      return string_search::ContainsSubstring(text, pattern);
    }
    return string_search::ContainsSubstringI(text, pattern);
  }
  
  // Fast path: Simple patterns (pre-analyzed)
  if (analysis.is_simple) {
    return detail::RouteSimplePattern(pattern, text, case_sensitive);
  }
  
  // Slow path: Complex patterns (pre-compile regex)
  const regex_t* compiled_regex = detail::GetCompiledRegex(pattern, case_sensitive);
  if (compiled_regex == nullptr) {
    return false;
  }
  
  // Use pre-analyzed flag (no need to re-analyze)
  return detail::MatchWithRegex(pattern, text, compiled_regex, analysis.requires_full_match);
}

// Match pattern against text using std::regex (strict mode - always uses std::regex, no routing)
// Returns false on invalid pattern or no match
// Use this for rs: patterns where user explicitly wants std::regex
inline bool RegexMatchStrict(std::string_view pattern, std::string_view text, bool case_sensitive = true) {
  if (pattern.empty()) {
    return false; // Empty pattern is invalid
  }
  if (text.data() == nullptr) {
    return false; // nullptr text cannot match
  }
  
  // Always use std::regex (no routing to SimpleRegex or string search)
  // regex requires std::string, so we must convert (unavoidable)
  const regex_t* compiled_regex = detail::GetCompiledRegex(pattern, case_sensitive);
  if (compiled_regex == nullptr) {
    return false; // Invalid pattern
  }
  
  // OPTIMIZED: Use regex_match for anchored patterns (^...$) - more efficient than regex_search
  return detail::MatchWithRegex(pattern, text, compiled_regex, detail::RequiresFullMatch(pattern));
}

// Match pattern against text using std::regex
// Returns false on invalid pattern or no match
// OPTIMIZED: Routes patterns to fastest matching method, uses string_view where possible
inline bool RegexMatch(std::string_view pattern, std::string_view text, bool case_sensitive = true) {
  if (pattern.empty()) {
    return false; // Empty pattern is invalid
  }
  if (text.data() == nullptr) {
    return false; // nullptr text cannot match
  }
  
  // Fastest path: Literal patterns (no regex special characters)
  // Use simple string search - 10-50x faster than regex, uses string_view directly (no allocation)
  if (detail::IsLiteralPattern(pattern)) {
    if (case_sensitive) {
      return string_search::ContainsSubstring(text, pattern);
    }
    return string_search::ContainsSubstringI(text, pattern);
  }
  
  // Fast path: Route simple patterns to SimpleRegex (much faster than std::regex)
  // SimpleRegex supports: ., *, ^, $, and literal characters, uses string_view directly (no allocation)
  if (detail::IsSimplePattern(pattern)) {
    return detail::RouteSimplePattern(pattern, text, case_sensitive);
  }
  
  // Slow path: Use regex for complex patterns (character classes, quantifiers, etc.)
  // regex requires std::string, so we must convert (unavoidable)
  const regex_t* compiled_regex = detail::GetCompiledRegex(pattern, case_sensitive);
  if (compiled_regex == nullptr) {
    return false; // Invalid pattern
  }
  
  // OPTIMIZED: Use regex_match for anchored patterns (^...$) - more efficient than regex_search
  return detail::MatchWithRegex(pattern, text, compiled_regex, detail::RequiresFullMatch(pattern));
}


// Clear the regex cache (useful for testing or memory management)
inline void ClearCache() {
  GetCache().Clear();
}

// Get cache size (useful for debugging)
inline size_t GetCacheSize() {
  return GetCache().Size();
}

} // namespace std_regex_utils
