#pragma once

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "utils/Logger.h"
#include "utils/RegexAliases.h"
#include "utils/StringSearch.h"
#include "utils/StringUtils.h"

namespace std_regex_utils {

// Thread-local regex cache
// Caches compiled regex_t objects to avoid expensive recompilation
// OPTIMIZED: Uses thread_local storage - no mutex needed, each thread has its own cache
//
// NOTE: This cache currently has no size limit. For a desktop application, this is acceptable
// because:
// - Each thread has its own cache instance (thread-local)
// - Typical usage: 5-20 unique regex patterns per session
// - Even 1000 entries = ~40 KB per thread (negligible for desktop apps)
// - Cache is only used for 're:' prefix patterns (most patterns use faster paths)
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
    regex_t regex{};  // NOLINT(readability-identifier-naming,readability-redundant-member-init) - public cache POD; explicit default for clarity
    bool case_sensitive;  // NOLINT(readability-identifier-naming) - public cache POD field; stable API

    explicit CachedRegex(std::string_view pattern, bool case_sensitive_flag)
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
    std::string key;
    key.reserve(2 + pattern.size());
    key = case_sensitive ? "1:" : "0:";
    key += pattern;
    return key;
  }
};

// Get thread-local regex cache instance
// OPTIMIZED: Each thread gets its own cache - no mutex needed, zero contention
inline ThreadLocalRegexCache& GetCache() {
  thread_local ThreadLocalRegexCache cache;  // NOSONAR(cpp:S6018) - Function-local thread_local is correct pattern, inline not allowed in block scope
  return cache;
}

namespace detail {

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
      // Iterator-based matching: no per-call std::string allocation in the hot loop.
      return match_func(text_str, *compiled_regex);
    } catch (const regex_error_t& e) {
      (void)e;  // Suppress unused variable warning
      LOG_WARNING_BUILD("Regex match error for pattern '" << pattern_str << "': " << e.what());
      return false;
    } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all needed for non-standard exceptions from regex matching
      LOG_WARNING_BUILD("Unexpected error during regex match for pattern '" << pattern_str << "'");
      return false;
    }
  }

  // Helper: Match using regex (regex_match for full match, regex_search otherwise).
  // Iterator-based: no per-call allocation. Start-anchored-only patterns (^...)
  // pass match_continuous so regex_search skips the start-position scan.
  inline bool MatchWithRegex(std::string_view pattern_str, std::string_view text_str,
                             const regex_t* compiled_regex, bool requires_full_match,
                             bool match_at_start) {
    if (requires_full_match) {
      return ExecuteRegexMatch(pattern_str, text_str, compiled_regex,
                               [](std::string_view t, const regex_t& r) { return regex_match(t.begin(), t.end(), r); });
    }
    if (match_at_start) {
      return ExecuteRegexMatch(pattern_str, text_str, compiled_regex,
                               [](std::string_view t, const regex_t& r) {
                                 return regex_search(t.begin(), t.end(), r,
                                                     regex_constants::match_continuous);
                               });
    }
    return ExecuteRegexMatch(pattern_str, text_str, compiled_regex,
                             [](std::string_view t, const regex_t& r) { return regex_search(t.begin(), t.end(), r); });
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
      if (pattern[i] == '\\') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - regex utility, loop-guarded or match-result indexed
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

  // Check if pattern is a literal string (no regex special characters)
  // If true, we can use fast string search instead of regex
  // OPTIMIZED: Detects literal patterns for 10-50x speedup, uses string_view to avoid allocations
  inline bool IsLiteralPattern(std::string_view pattern) {
    for (size_t i = 0; i < pattern.size(); ++i) {  // NOSONAR(cpp:S886) - Index-based loop required for character-by-character pattern checking
      const char c = pattern[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - regex utility, loop-guarded or match-result indexed

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

  inline void CommitIfLarger(std::string& best, std::string_view current) {
    if (current.size() > best.size()) {
      best.assign(current);
    }
  }

  inline void LiteralResetRun(std::string& best, std::string& current) {
    CommitIfLarger(best, current);
    current.clear();
  }

  // Advance i from the current position until after `terminator`, or end of pattern.
  inline void SkipToChar(std::string_view pattern, size_t& i, char terminator) {
    while (i < pattern.size() && pattern[i] != terminator) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - loop-guarded
      ++i;
    }
    if (i < pattern.size()) {
      ++i;
    }
  }

  struct ParseStepResult {
    bool handled;  // NOLINT(readability-identifier-naming) - public POD field; internal parse helper
    bool abort_no_literal;  // NOLINT(readability-identifier-naming) - public POD field; internal parse helper
  };

  // Character-class escapes (\d, \w, \s, ...) that stand for a set, never a literal.
  inline bool IsClassEscapeChar(char c) {
    return c == 'd' || c == 'D' || c == 'w' || c == 'W' ||
           c == 's' || c == 'S' || c == 'n' || c == 't' ||
           c == 'r' || c == 'b' || c == 'B' || c == 'a' ||
           c == 'f' || c == 'v';
  }

  // Escapes where the backslash literally represents the character itself.
  // Unknown escapes (\x41, \u0041, \1 backreferences) are not literal chars.
  inline bool IsLiteralEscapeChar(char c) {
    return c == '.' || c == '*' || c == '+' || c == '?' ||
           c == '^' || c == '$' || c == '(' || c == ')' ||
           c == '[' || c == ']' || c == '{' || c == '}' ||
           c == '|' || c == '\\' || c == '/' || c == '-';
  }

  // Regex metacharacters that break a literal run.
  inline bool IsRegexMetachar(char c) {
    return c == '.' || c == '*' || c == '+' || c == '?' ||
           c == '^' || c == '$' || c == '(' || c == ')' ||
           c == '[' || c == ']' || c == '{' || c == '}' || c == '|';
  }

  // pattern[i] == '\\' and i + 1 < pattern.size() (caller must ensure).
  inline ParseStepResult AdvanceAfterEscapeSequence(std::string_view pattern, size_t& i, std::string& best,
                                                    std::string& current) {
    const char next = pattern[i + 1];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i + 1 < pattern.size() (caller)
    if (IsClassEscapeChar(next)) {
      LiteralResetRun(best, current);
      i += 2;
      return {true, false};
    }
    // Only characters where a backslash escape literally represents the character itself
    // (regex metacharacters and punctuation) are safe to treat as literal characters.
    // Unknown escapes (like \x41, \u0041, \c, backreferences \1-\9) are not literal 'x', 'u', etc.
    if (const bool is_safe_literal_escape = IsLiteralEscapeChar(next); !is_safe_literal_escape) {
      LiteralResetRun(best, current);
      return {true, true};
    }
    if (const size_t after = i + 2; after < pattern.size()) {
      const char q = pattern[after];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - after < pattern.size()
      if (q == '*' || q == '?') {
        LiteralResetRun(best, current);
        i = after + 1;
        return {true, false};
      }
      if (q == '{') {
        LiteralResetRun(best, current);
        size_t j = after;
        SkipToChar(pattern, j, '}');
        i = j;
        return {true, false};
      }
    }
    current += next;
    i += 2;
    return {true, false};
  }

  inline void ConsumeCharClass(std::string_view pattern, size_t& i, std::string& best,
                               std::string& current) {
    LiteralResetRun(best, current);
    ++i;
    SkipToChar(pattern, i, ']');
  }

  inline void ConsumeDot(std::string_view pattern, size_t& i, std::string& best,
                         std::string& current) {
    LiteralResetRun(best, current);
    ++i;
    if (i >= pattern.size()) {
      return;
    }
    const char q = pattern[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < pattern.size() checked above
    if (q == '*' || q == '+' || q == '?') {
      ++i;
      return;
    }
    if (q == '{') {
      SkipToChar(pattern, i, '}');
    }
  }

  inline void ConsumeBraceBlock(std::string_view pattern, size_t& i, std::string& best,
                                std::string& current) {
    LiteralResetRun(best, current);
    SkipToChar(pattern, i, '}');
  }

  inline void ConsumePlainChar(std::string_view pattern, size_t& i, std::string& best,
                                std::string& current) {
    const char c = pattern[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - loop-guarded by caller
    if (const size_t after = i + 1; after < pattern.size()) {
      const char q = pattern[after];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - after < pattern.size()
      if (q == '*' || q == '?') {
        LiteralResetRun(best, current);
        i = after + 1;
        return;
      }
      if (q == '{') {
        LiteralResetRun(best, current);
        i = after;
        SkipToChar(pattern, i, '}');
        return;
      }
      if (q == '+') {
        current += c;
        i = after + 1;
        return;
      }
    }
    current += c;
    ++i;
  }

  inline ParseStepResult ConsumeTopLevelToken(std::string_view pattern, size_t& i, int& paren_depth,
                                              std::string& best, std::string& current) {
    const char c = pattern[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - loop-guarded by caller
    if (c == '(') {
      LiteralResetRun(best, current);
      ++paren_depth;
      ++i;
      return {true, false};
    }
    if (c == ')') {
      if (paren_depth > 0) {
        --paren_depth;
      }
      ++i;
      return {true, false};
    }
    if (paren_depth > 0) {
      ++i;
      return {true, false};
    }
    if (c == '^' || c == '$') {
      ++i;
      return {true, false};
    }
    if (c == '|') {
      return {true, true};
    }
    if (c == '\\' && i + 1 < pattern.size()) {
      return AdvanceAfterEscapeSequence(pattern, i, best, current);
    }
    if (c == '[') {
      ConsumeCharClass(pattern, i, best, current);
      return {true, false};
    }
    if (c == '.') {
      ConsumeDot(pattern, i, best, current);
      return {true, false};
    }
    if (c == '*' || c == '+' || c == '?') {
      ++i;
      return {true, false};
    }
    if (c == '{') {
      ConsumeBraceBlock(pattern, i, best, current);
      return {true, false};
    }
    return {false, false};
  }

  /// Extract the longest literal substring that any match must contain.
  /// Used as a fast pre-filter: if the literal is absent from the text the
  /// regex engine is never called, avoiding the ~1 µs per-file regex cost.
  ///
  /// Returns empty when no reliable literal can be extracted (alternation at
  /// the top level, pure character-class patterns such as \d{4}, etc.).
  inline std::string ExtractRequiredLiteral(std::string_view pattern) {
    std::string best;
    std::string current;
    int paren_depth = 0;

    for (size_t i = 0; i < pattern.size();) {
      if (const ParseStepResult step = ConsumeTopLevelToken(pattern, i, paren_depth, best, current);
          step.handled) {
        if (step.abort_no_literal) {
          return {};
        }
        continue;
      }
      ConsumePlainChar(pattern, i, best, current);
    }

    CommitIfLarger(best, current);
    return best;
  }

  // Check if pattern requires full string match (starts with ^ and ends with $)
  // If true, we can use regex_match instead of regex_search for better performance
  // OPTIMIZED: Detects anchored patterns to use more efficient regex_match, uses string_view to avoid allocations
  inline bool RequiresFullMatch(std::string_view pattern) {
    if (pattern.size() < 2) {
      return false; // Need at least ^ and $ for full match
    }

    // Check for ^ at start (not escaped)
    const bool starts_with_anchor = (pattern[0] == '^');  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - regex utility, loop-guarded or match-result indexed

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

  // Check if pattern is anchored at the start (^...) without requiring a full
  // match. Lets callers pass match_continuous to regex_search, skipping the
  // start-position scan. Position 0 needs no escape check: a leading
  // backslash would itself be pattern[0], not an escape prefix.
  inline bool RequiresMatchAtStart(std::string_view pattern) {
    if (pattern.empty()) {
      return false;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size checked above
    if (pattern[0] != '^') {
      return false;
    }
    return !RequiresFullMatch(pattern);
  }

  // -------------------------------------------------------------------------
  // Required-suffix extraction (end-anchored patterns).
  //
  // For patterns like `.*\.(cpp|h)$` no single required literal exists, so the
  // literal pre-filter cannot fire. The whole pattern is still end-anchored by
  // `$`, so every match must end with one of a small set of literal suffixes.
  // We extract that set soundly (only when certain) to reject most rows cheaply.
  //
  // Only the strongest, unambiguous shapes are handled; anything else returns
  // an empty set and the caller falls back to running the regex.
  // -------------------------------------------------------------------------

  inline constexpr size_t kMinRequiredSuffixLength = 2;
  inline constexpr size_t kMaxRequiredSuffixAlternatives = 8;

  // True when the pattern ends with an unescaped '$' end-anchor.
  inline bool HasUnescapedEndAnchor(std::string_view pattern) {
    if (pattern.empty() || pattern.back() != '$') {
      return false;
    }
    return CountTrailingBackslashes(pattern) % 2 == 0;
  }

  // Maximal literal run at the end of `pattern` (escaped literals included).
  // Any other token resets the run, so the result is a suffix every match must
  // end with (empty when no such run exists).
  inline std::string TrailingLiteralRun(std::string_view pattern) {
    std::string run;
    for (size_t i = 0; i < pattern.size();) {
      const char c = pattern[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - loop-guarded
      if (c == '\\') {
        if (i + 1 >= pattern.size()) {
          run.clear();
          ++i;
          continue;
        }
        const char next = pattern[i + 1];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i + 1 < size
        if (IsClassEscapeChar(next) || !IsLiteralEscapeChar(next)) {
          run.clear();
          i += 2;
          continue;
        }
        run += next;
        i += 2;
        continue;
      }
      if (IsRegexMetachar(c)) {
        run.clear();
        ++i;
        continue;
      }
      run += c;
      ++i;
    }
    return run;
  }

  // Advance `i` past an escape (`\x`) or a character class (`[...]`) at s[i] and
  // return true; leave `i` unchanged and return false otherwise. Keeps the
  // pattern scanners from mistaking escapes/classes for structural characters,
  // and keeps the index advance out of their loop bodies (cpp:S886).
  inline bool SkipEscapedOrClass(std::string_view s, size_t& i) {
    const char c = s[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - caller guarantees i < s.size()
    if (c == '\\') {
      ++i;  // the escaped character (i may reach s.size() for a trailing '\')
      return true;
    }
    if (c != '[') {
      return false;
    }
    ++i;
    while (i < s.size() && s[i] != ']') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - loop-guarded
      if (s[i] == '\\') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - loop-guarded
        ++i;
      }
      ++i;
    }
    if (i < s.size()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - loop-guarded
      ++i;  // past ']'
    }
    return true;
  }

  // Split at top-level (outside groups and classes) '|'.
  inline std::vector<std::string_view> SplitTopLevelAlternation(std::string_view s) {
    std::vector<std::string_view> alternatives;
    size_t start = 0;
    int depth = 0;
    size_t i = 0;
    while (i < s.size()) {
      if (SkipEscapedOrClass(s, i)) {
        continue;  // i already advanced past the escape/class
      }
      if (const char c = s[i]; c == '(') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < size (loop condition)
        ++depth;
      } else if (c == ')') {
        if (depth > 0) {
          --depth;
        }
      } else if (c == '|' && depth == 0) {
        alternatives.push_back(s.substr(start, i - start));
        start = i + 1;
      }
      ++i;
    }
    alternatives.push_back(s.substr(start));
    return alternatives;
  }

  // Convert a substring to its literal characters; false if it holds any
  // metacharacter (caller then bails conservatively).
  inline bool LiteralCharsOf(std::string_view s, std::string& out) {
    out.clear();
    for (size_t i = 0; i < s.size();) {
      const char c = s[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - loop-guarded
      if (c == '\\') {
        if (i + 1 >= s.size() || !IsLiteralEscapeChar(s[i + 1])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i + 1 checked
          return false;
        }
        out += s[i + 1];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i + 1 checked
        i += 2;
        continue;
      }
      if (IsRegexMetachar(c)) {
        return false;
      }
      out += c;
      ++i;
    }
    return true;
  }

  // Index of the '(' matching the final ')' of `s` (npos when it does not end
  // with a balanced group). Forward scan with a paren stack avoids backwards
  // escape-parity pitfalls.
  inline size_t FindEnclosingGroupStart(std::string_view s) {
    if (s.empty() || s.back() != ')') {
      return std::string_view::npos;
    }
    std::vector<size_t> open_positions;
    size_t i = 0;
    while (i < s.size()) {
      if (SkipEscapedOrClass(s, i)) {
        continue;
      }
      if (const char c = s[i]; c == '(') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < size (loop condition)
        open_positions.push_back(i);
      } else if (c == ')') {
        if (open_positions.empty()) {
          return std::string_view::npos;
        }
        const size_t open = open_positions.back();
        open_positions.pop_back();
        if (i == s.size() - 1) {
          return open;
        }
      }
      ++i;
    }
    return std::string_view::npos;
  }

  // If `body` ends with a group, append the group's literal alternatives to
  // `suffixes` and move `head_end` to the group's '('; returns false to bail.
  inline bool AppendTrailingGroupSuffixes(std::string_view body,
                                          std::vector<std::string>& suffixes,
                                          size_t& head_end) {
    if (body.empty() || body.back() != ')') {
      return true;  // no trailing group: the caller uses the trailing literal run
    }
    const size_t open = FindEnclosingGroupStart(body);
    if (open == std::string_view::npos) {
      return false;
    }
    std::string_view inner = body.substr(open + 1, body.size() - 1 - (open + 1));
    if (inner.size() >= 2 && inner[0] == '?') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size >= 2
      if (inner[1] != ':') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size >= 2; lookaheads/inline flags are not literal groups
        return false;
      }
      inner.remove_prefix(2);  // non-capturing group marker
    }
    for (const std::string_view alt : SplitTopLevelAlternation(inner)) {
      std::string literal;
      if (!LiteralCharsOf(alt, literal) || literal.empty()) {
        return false;
      }
      suffixes.push_back(literal);
    }
    head_end = open;
    return true;
  }

  // A suffix set is usable only when bounded in size and long enough to be
  // selective. Short members cannot be dropped (that would be unsound), so the
  // whole set is rejected instead.
  inline bool SuffixSetIsUsable(const std::vector<std::string>& suffixes) {
    if (suffixes.empty() || suffixes.size() > kMaxRequiredSuffixAlternatives) {
      return false;
    }
    size_t shortest = suffixes.front().size();
    for (const std::string& s : suffixes) {
      shortest = (std::min)(shortest, s.size());
    }
    return shortest >= kMinRequiredSuffixLength;
  }

  // Required literal suffix(es) for an end-anchored pattern. Returns an empty
  // vector when no sound suffix can be derived. A match of such a pattern must
  // end with one of the returned strings (see the caller for trailing-newline
  // tolerance, since '$' may match before a final newline).
  inline std::vector<std::string> ExtractRequiredSuffixes(std::string_view pattern) {
    if (!HasUnescapedEndAnchor(pattern)) {
      return {};
    }
    const std::string_view body = pattern.substr(0, pattern.size() - 1);
    // '$' binds only the last top-level alternative; bail on a top-level '|'.
    if (body.empty() || SplitTopLevelAlternation(body).size() > 1) {
      return {};
    }

    std::vector<std::string> suffixes;
    size_t head_end = body.size();
    if (!AppendTrailingGroupSuffixes(body, suffixes, head_end)) {
      return {};
    }

    if (const std::string head = TrailingLiteralRun(body.substr(0, head_end)); !head.empty()) {
      if (suffixes.empty()) {
        suffixes.push_back(head);
      } else {
        for (std::string& s : suffixes) {
          s.insert(0, head);
        }
      }
    }

    if (!SuffixSetIsUsable(suffixes)) {
      return {};
    }
    return suffixes;
  }

  // True when every character can appear literally in a file extension: no dot,
  // no path separator, and no regex metacharacter. (Keeps the extension set
  // exactly the pattern's match set, with no internal-dot ambiguity.)
  inline bool IsPlainExtensionChars(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) {
             return c != '.' && c != '/' && c != '\\' && !IsRegexMetachar(c);
           });
  }

  // Recognize an `re:` pattern that is *exactly* an extension match under
  // regex_search, i.e.
  //   [.*] \. EXT $        or        [.*] \. (EXT|EXT|...) $
  // where each EXT is plain extension characters. A match is precisely "the text
  // ends with '.' + one of the extensions", so callers may replace the regex with
  // a suffix comparison and treat the extension set as exact. Returns the
  // extensions without dots, or nullopt when the pattern is anything else.
  //
  // A leading '^' is rejected: `^.*\.EXT$` forces a full match (regex_match, with
  // stricter trailing-newline semantics than regex_search) and `^\.EXT$` means the
  // text equals ".EXT"; neither is the regex_search suffix predicate implemented
  // here.
  [[nodiscard]] inline std::optional<std::vector<std::string>> RecognizeExactExtensionPattern(
      std::string_view pattern) {
    std::string_view p = pattern;
    if (p.size() >= 2 && p[0] == '.' && p[1] == '*') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size >= 2
      p.remove_prefix(2);
    }
    if (p.size() < 2 || p[0] != '\\' || p[1] != '.') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size >= 2
      return std::nullopt;
    }
    p.remove_prefix(2);
    if (p.empty() || p.back() != '$') {
      return std::nullopt;
    }
    p.remove_suffix(1);
    if (p.empty()) {
      return std::nullopt;
    }

    std::vector<std::string> extensions;
    if (p.front() == '(' && p.back() == ')') {
      const std::string_view inner = p.substr(1, p.size() - 2);
      if (!inner.empty() && inner.front() == '?') {  // lookahead/flag group
        return std::nullopt;
      }
      for (const std::string_view alt : SplitTopLevelAlternation(inner)) {
        if (!IsPlainExtensionChars(alt)) {
          return std::nullopt;
        }
        extensions.emplace_back(alt);
      }
    } else if (IsPlainExtensionChars(p)) {
      extensions.emplace_back(p);
    } else {
      return std::nullopt;
    }

    if (extensions.empty()) {
      return std::nullopt;
    }
    return extensions;
  }

  // -------------------------------------------------------------------------
  // Required-prefix extraction (start-anchored patterns) — the forward mirror
  // of the suffix extractor above. Valid because start-anchored patterns are
  // matched at index 0: RequiresMatchAtStart() drives match_continuous, and
  // RequiresFullMatch() drives regex_match.
  // -------------------------------------------------------------------------

  inline constexpr size_t kMinRequiredPrefixLength = 2;
  inline constexpr size_t kMaxRequiredPrefixAlternatives = 8;

  // Index of the ')' matching the '(' at `open`. npos when unbalanced.
  inline size_t FindGroupEnd(std::string_view s, size_t open) {
    int depth = 0;
    size_t i = open;
    while (i < s.size()) {
      if (SkipEscapedOrClass(s, i)) {
        continue;
      }
      if (const char c = s[i]; c == '(') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i < size (loop condition)
        ++depth;
      } else if (c == ')') {
        --depth;
        if (depth == 0) {
          return i;
        }
      }
      ++i;
    }
    return std::string_view::npos;
  }

  // Quantifier immediately following a literal in a pattern (decides whether the
  // literal is required at a fixed index).
  enum class PrefixQuantifier : std::uint8_t {
    None,            // no quantifier: required exactly once
    Optional,        // '*' or '?': not required at all
    RepeatRequired,  // '+' or '{...}': required once, but repeats shift later chars
  };

  inline PrefixQuantifier QuantifierAfter(std::string_view s, size_t after) {
    if (after >= s.size()) {
      return PrefixQuantifier::None;
    }
    const char q = s[after];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - after < size
    if (q == '*' || q == '?') {
      return PrefixQuantifier::Optional;
    }
    if (q == '+' || q == '{') {
      return PrefixQuantifier::RepeatRequired;
    }
    return PrefixQuantifier::None;
  }

  // Append the literal characters at the start of `s` (from `start`) that every
  // match must have at a fixed position. Stops at the first optional or
  // non-literal token; a `+`/`{m>=1}`-quantified literal is included and then
  // stops the run (repetition moves any following char off a fixed index).
  // Returns the index it stopped at.
  inline size_t ScanRequiredLiteralRun(std::string_view s, size_t start, std::string& out) {
    size_t i = start;
    while (i < s.size()) {
      const char c = s[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - loop-guarded
      char literal = '\0';
      size_t next = 0;
      if (c == '\\') {
        if (i + 1 >= s.size()) {
          return i;
        }
        const char escaped = s[i + 1];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - i + 1 < size
        if (IsClassEscapeChar(escaped) || !IsLiteralEscapeChar(escaped)) {
          return i;
        }
        literal = escaped;
        next = i + 2;
      } else {
        if (IsRegexMetachar(c)) {
          return i;
        }
        literal = c;
        next = i + 1;
      }
      out += literal;
      const PrefixQuantifier quantifier = QuantifierAfter(s, next);
      if (quantifier == PrefixQuantifier::Optional) {
        out.pop_back();  // optional: not required at this index
        return i;
      }
      if (quantifier == PrefixQuantifier::RepeatRequired) {
        return next;  // required once, but repeats shift any following literal
      }
      i = next;
    }
    return i;
  }

  // Leading-group case for ExtractRequiredPrefixes: append the group's literal
  // alternatives and return true, or return false to bail.
  inline bool PrefixesFromLeadingGroup(std::string_view p, std::vector<std::string>& prefixes) {
    const size_t close = FindGroupEnd(p, 0);
    if (close == std::string_view::npos) {
      return false;
    }
    // An optional group (`?`/`*`) is not required at index 0; `+`/`{m}` is.
    // '{' is treated as possibly-optional (conservative).
    if (const char after = close + 1 < p.size() ? p[close + 1] : '\0';  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded
        after == '*' || after == '?' || after == '{') {
      return false;
    }
    std::string_view inner = p.substr(1, close - 1);
    if (!inner.empty() && inner.front() == '?') {
      if (inner.size() < 2 || inner[1] != ':') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size >= 2; lookahead/flag group
        return false;
      }
      inner.remove_prefix(2);
    }
    for (const std::string_view alt : SplitTopLevelAlternation(inner)) {
      std::string literal;
      if (!LiteralCharsOf(alt, literal) || literal.empty()) {
        return false;
      }
      prefixes.push_back(literal);
    }
    return true;
  }

  // Literal prefix(es) every match of a start-anchored pattern must begin with,
  // or {} when none can be proven. Mirrors ExtractRequiredSuffixes: handles a
  // leading literal run and a leading `(a|b|...)`/`(?:a|b|...)` group of literal
  // alternatives, and bails on inline flags (which can change `^` semantics).
  inline std::vector<std::string> ExtractRequiredPrefixes(std::string_view pattern) {
    if (pattern.empty() || pattern[0] != '^') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - size checked
      return {};
    }
    if (pattern.find("(?") != std::string_view::npos) {  // inline flags/lookarounds
      return {};
    }
    const std::string_view p = pattern.substr(1);
    if (p.empty()) {
      return {};
    }

    std::vector<std::string> prefixes;
    if (p.front() == '(') {
      if (!PrefixesFromLeadingGroup(p, prefixes)) {
        return {};
      }
    } else {
      std::string run;
      ScanRequiredLiteralRun(p, 0, run);
      if (run.empty()) {
        return {};
      }
      prefixes.push_back(run);
    }

    if (prefixes.empty() || prefixes.size() > kMaxRequiredPrefixAlternatives) {
      return {};
    }
    size_t shortest = prefixes.front().size();
    for (const std::string& prefix : prefixes) {
      shortest = (std::min)(shortest, prefix.size());
    }
    if (shortest < kMinRequiredPrefixLength) {
      return {};
    }
    return prefixes;
  }
}

// Pattern analysis result (for pre-analysis optimization)
struct PatternAnalysis {
  bool is_literal;  // NOLINT(readability-identifier-naming) - public POD field; stable analysis API
  bool requires_full_match;  // NOLINT(readability-identifier-naming) - public POD field; stable analysis API
  bool match_at_start;  // NOLINT(readability-identifier-naming) - public POD field; start-anchored only (^... without ...$)

  explicit PatternAnalysis(std::string_view pattern)
    : is_literal(detail::IsLiteralPattern(pattern)),
      requires_full_match(detail::RequiresFullMatch(pattern)),
      match_at_start(detail::RequiresMatchAtStart(pattern)) {
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
    return string_search::ContainsSubstring(text, pattern, case_sensitive);
  }

  // Slow path: Complex patterns (pre-compile regex)
  const regex_t* compiled_regex = detail::GetCompiledRegex(pattern, case_sensitive);
  if (compiled_regex == nullptr) {
    return false;
  }

  // Use pre-analyzed flag (no need to re-analyze)
  return detail::MatchWithRegex(pattern, text, compiled_regex, analysis.requires_full_match,
                                analysis.match_at_start);
}

// Match pattern against text using std::regex (strict mode - always uses std::regex, no routing)
// Returns false on invalid pattern or no match
// Use this for re: patterns where user explicitly wants std::regex
inline bool RegexMatchStrict(std::string_view pattern, std::string_view text, bool case_sensitive = true) {
  if (pattern.empty()) {
    return false; // Empty pattern is invalid
  }
  if (text.data() == nullptr) {
    return false; // nullptr text cannot match
  }

  // Always use the regex engine (no routing to string search)
  const regex_t* compiled_regex = detail::GetCompiledRegex(pattern, case_sensitive);
  if (compiled_regex == nullptr) {
    return false; // Invalid pattern
  }

  // OPTIMIZED: Use regex_match for anchored patterns (^...$) - more efficient than regex_search
  return detail::MatchWithRegex(pattern, text, compiled_regex, detail::RequiresFullMatch(pattern),
                                detail::RequiresMatchAtStart(pattern));
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
    return string_search::ContainsSubstring(text, pattern, case_sensitive);
  }

  // Slow path: Use regex for complex patterns (character classes, quantifiers, etc.)
  const regex_t* compiled_regex = detail::GetCompiledRegex(pattern, case_sensitive);
  if (compiled_regex == nullptr) {
    return false; // Invalid pattern
  }

  // OPTIMIZED: Use regex_match for anchored patterns (^...$) - more efficient than regex_search
  return detail::MatchWithRegex(pattern, text, compiled_regex, detail::RequiresFullMatch(pattern),
                                detail::RequiresMatchAtStart(pattern));
}

/**
 * @brief First regex match as a highlight span (same ECMAScript cache as re: search).
 *
 * Uses thread-local GetCache() compilation. Anchored ^...$ patterns highlight the
 * whole string when they match; otherwise highlights the first regex_search hit.
 * Invalid / empty patterns, zero-length matches, and exceptions yield no spans.
 */
inline bool FindRegexSpans(std::string_view pattern, std::string_view text, bool case_sensitive,
                           std::vector<string_search::MatchSpan>& out_spans) {
  out_spans.clear();
  if (pattern.empty() || text.empty()) {
    return false;
  }
  // Keep in sync with search_pattern_utils::kMaxPatternLength (avoid ReDoS on UI paint).
  if (constexpr size_t kMaxHighlightPatternLength = 1000;
      pattern.length() > kMaxHighlightPatternLength) {
    return false;
  }

  const regex_t* const compiled = GetCache().GetRegex(pattern, case_sensitive);
  if (compiled == nullptr) {
    return false;
  }

  try {
    if (detail::RequiresFullMatch(pattern)) {
      if (regex_match(text.begin(), text.end(), *compiled)) {
        out_spans.push_back(string_search::MatchSpan{0, text.size()});
        return true;
      }
      return false;
    }

    // Loop to collect all non-overlapping matches.
    // Use default-arg position()/length() — Boost has position(char_type*) so
    // position(0) is ambiguous (0 → size_type vs null pointer).
    auto search_begin = text.begin();  // NOLINT(llvm-qualified-auto,readability-qualified-auto) - iterator is mutated each iteration; const* would prevent advancement
    match_results_t<std::string_view::const_iterator> match;
    while (regex_search(search_begin, text.end(), match, *compiled) && !match.empty()) {
      const auto match_len = static_cast<size_t>(match.length());
      if (match_len == 0U) {
        break;  // Zero-length match guard: prevents infinite loop on patterns like .* or a*
      }
      const auto offset = static_cast<size_t>(search_begin - text.begin());
      const auto match_pos = offset + static_cast<size_t>(match.position());
      out_spans.push_back(string_search::MatchSpan{match_pos, match_pos + match_len});
      search_begin += static_cast<std::ptrdiff_t>(match.position()) +
                      static_cast<std::ptrdiff_t>(match_len);
    }
    return !out_spans.empty();
  } catch (const regex_error_t& e) {
    (void)e;
    LOG_WARNING_BUILD("FindRegexSpans: regex error: " << e.what());
    return false;
  }
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
