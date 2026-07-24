#pragma once

#include <algorithm>  // For std::min
#include <cctype>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

// Compile-time AVX2 availability: Check if we're on x86/x64 architecture
// CMakeLists.txt ensures AVX2 compiler flags are set for StringSearchAVX2.cpp on x86/x64
// Runtime detection (SupportsAVX2()) determines if the CPU actually supports AVX2
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    #define STRING_SEARCH_AVX2_AVAILABLE 1  // NOLINT(cppcoreguidelines-macro-usage)  // NOSONAR(cpp:S5028) - Must remain macro for #if preprocessor directives
#else
    // ARM or other architectures - AVX2 not available
    #define STRING_SEARCH_AVX2_AVAILABLE 0  // NOLINT(cppcoreguidelines-macro-usage)  // NOSONAR(cpp:S5028) - Must remain macro for #if preprocessor directives
#endif  // defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)

// Compile-time NEON availability: AArch64 (Apple Silicon, ARM64 Linux/Windows).
// vmaxvq_u8 (horizontal max) used in the implementation requires AArch64.
#if defined(__aarch64__) || defined(_M_ARM64)
    #define STRING_SEARCH_NEON_AVAILABLE 1  // NOLINT(cppcoreguidelines-macro-usage)  // NOSONAR(cpp:S5028) - Must remain macro for #if preprocessor directives
#else
    #define STRING_SEARCH_NEON_AVAILABLE 0  // NOLINT(cppcoreguidelines-macro-usage)  // NOSONAR(cpp:S5028) - Must remain macro for #if preprocessor directives
#endif  // defined(__aarch64__) || defined(_M_ARM64)

// Forward declaration for CPU feature detection
namespace cpu_features {
    bool SupportsAVX2();
    bool GetAVX2Support();
}

// Forward declaration for AVX2 functions
#if STRING_SEARCH_AVX2_AVAILABLE
namespace string_search::avx2 {
    [[nodiscard]] bool IsViewAsciiAVX2(const char* data, size_t len);
    bool ContainsSubstringIAVX2(const std::string_view& text,
                                const std::string_view& pattern);
    bool ContainsSubstringAVX2(const std::string_view& text,
                               const std::string_view& pattern);
    const char* StrStrCaseInsensitiveAVX2(const char* haystack, const char* needle);
} // namespace string_search::avx2
#endif  // STRING_SEARCH_AVX2_AVAILABLE

// Forward declaration for NEON functions (arm64 only)
#if STRING_SEARCH_NEON_AVAILABLE
namespace string_search::neon {
    [[nodiscard]] bool IsViewAsciiNEON(const char* data, size_t len);
    bool ContainsSubstringINEON(const std::string_view& text,
                                const std::string_view& pattern);
    bool ContainsSubstringNEON(const std::string_view& text,
                               const std::string_view& pattern);
    const char* StrStrCaseInsensitiveNEON(const char* haystack, const char* needle);
} // namespace string_search::neon
#endif  // STRING_SEARCH_NEON_AVAILABLE

// String search utility functions
// Extracted from StringUtils.h for better organization and testability
namespace string_search {

// Case-insensitive character comparison helper
inline char ToLowerChar(unsigned char ch_value) {
  return static_cast<char>(std::tolower(ch_value));
}

// Scalar byte-range comparison used by SIMD search implementations after a
// first-character hit.  Templated for case-sensitivity so the case-insensitive
// branch is eliminated at compile time.  Callers are expected to be inlined
// into target-attributed functions (AVX2 / NEON), so no target attribute is
// needed here.
template <bool CaseSensitive>
inline bool FullCompare(const char* a, const char* b, size_t len) {
  if constexpr (CaseSensitive) {
    return std::memcmp(a, b, len) == 0;
  } else {
    // Called only after a first-char match, so per-byte cost is acceptable.
    for (size_t i = 0; i < len; ++i) {
      if (ToLowerChar(static_cast<unsigned char>(a[i])) !=
          ToLowerChar(static_cast<unsigned char>(b[i]))) {
        return false;
      }
    }
    return true;
  }
}

// Precomputed once per search pattern (e.g. in CreatePatternMatchers) to avoid
// repeating pattern-length and CPU-capability checks on every indexed path.
struct SimdSubstringHints {
  bool use_avx2 = false;
  bool use_neon = false;
  bool pattern_is_ascii = false;
};

// Internal implementation details for deduplication
namespace string_search_detail {

constexpr size_t kAvx2MinTextLength = 32;
constexpr size_t kNeonMinTextLength = 16;
constexpr size_t kMinSimdPatternLength = 4;
constexpr unsigned char kMaxAsciiByte = 127;

[[nodiscard]] inline bool IsViewAsciiScalar(const std::string_view text) {
  return std::none_of(text.begin(), text.end(),  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
                      [](const unsigned char ch) { return ch > kMaxAsciiByte; });
}

// Full-haystack ASCII validation for case-insensitive SIMD (ToLowerAVX2/NEON is ASCII-only).
// Uses AVX2/NEON chunk scans when available; scalar tail for short buffers.
[[nodiscard]] inline bool IsViewAscii(const std::string_view text) {
  if (text.empty()) {
    return true;
  }

  const char* const data = text.data();

  #if STRING_SEARCH_AVX2_AVAILABLE
  if (const size_t len = text.length(); len >= kAvx2MinTextLength && cpu_features::GetAVX2Support()) {
    return string_search::avx2::IsViewAsciiAVX2(data, len);
  }
  #endif  // STRING_SEARCH_AVX2_AVAILABLE

  #if STRING_SEARCH_NEON_AVAILABLE
  if (const size_t len = text.length(); len >= kNeonMinTextLength) {
    return string_search::neon::IsViewAsciiNEON(data, len);
  }
  #endif  // STRING_SEARCH_NEON_AVAILABLE

  return IsViewAsciiScalar(text);
}

// Character comparison policies for template-based deduplication
struct CaseSensitive {
  static inline bool Equal(char left_char, char right_char) {
    return left_char == right_char;
  }
};

struct CaseInsensitive {
  static inline bool Equal(char left_char, char right_char) {
    return ToLowerChar(static_cast<unsigned char>(left_char)) ==
           ToLowerChar(static_cast<unsigned char>(right_char));
  }
};

// Template function for prefix check
template<typename ComparePolicy>
inline bool CheckPrefix(const std::string_view& text,
                        const std::string_view& pattern) {
  if (text.length() < pattern.length()) {
    return false;
  }

  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - pointer arithmetic bounds established by caller; text+size pattern
  for (size_t i = 0; i < pattern.length(); ++i) {
    if (!ComparePolicy::Equal(text[i], pattern[i])) {
      return false;
    }
  }
  // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  return true;
}

// Template function for reverse comparison
template<typename ComparePolicy>
inline bool ReverseCompare(const std::string_view& text,
                           const std::string_view& pattern,
                           size_t start_pos) {
  const size_t pattern_len = pattern.length();
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - pointer arithmetic bounds established by caller; text+size pattern
  for (size_t j = pattern_len; j > 0; --j) {
    if (!ComparePolicy::Equal(text[start_pos + j - 1], pattern[j - 1])) {
      return false;
    }
  }
  // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  return true;
}

// Template function for short pattern search
template<typename ComparePolicy>
inline bool ShortPatternSearch(const std::string_view& text,
                               const std::string_view& pattern) {
  if (text.length() < pattern.length()) {
    return false;
  }
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - pointer arithmetic bounds established by caller; text+size pattern
  for (size_t i = 0; i <= text.length() - pattern.length(); ++i) {
    bool is_match = true;
    for (size_t j = 0; j < pattern.length(); ++j) {
      if (!ComparePolicy::Equal(text[i + j], pattern[j])) {
        is_match = false;
        break;
      }
    }
    if (is_match) {
      return true;
    }
  }
  // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  return false;  // NOSONAR(cpp:S935) - All paths return (false positive from template analysis)
}

// Case-sensitive AVX2 dispatch: haystack may contain non-ASCII bytes (raw byte compare).
// Returns std::nullopt if AVX2 was not used (caller should fall back to scalar).
template<typename AVX2Func>
inline std::optional<bool> TryAVX2PathCaseSensitive(
    [[maybe_unused]] const std::string_view& text,
    [[maybe_unused]] const SimdSubstringHints& hints,
    AVX2Func avx2_func [[maybe_unused]]) {
  #if STRING_SEARCH_AVX2_AVAILABLE
  if (hints.use_avx2 && text.length() >= kAvx2MinTextLength) {
    return avx2_func();
  }
  #endif  // STRING_SEARCH_AVX2_AVAILABLE
  return std::nullopt;
}

// Case-insensitive AVX2 dispatch: ASCII-only haystack and pattern (ToLowerAVX2 vs std::tolower).
template<typename AVX2Func>
inline std::optional<bool> TryAVX2PathCaseInsensitive(
    [[maybe_unused]] const std::string_view& text,
    [[maybe_unused]] const SimdSubstringHints& hints,
    AVX2Func avx2_func [[maybe_unused]]) {
  #if STRING_SEARCH_AVX2_AVAILABLE
  if (hints.use_avx2 && hints.pattern_is_ascii && text.length() >= kAvx2MinTextLength &&
      IsViewAscii(text)) {
    return avx2_func();
  }
  #endif  // STRING_SEARCH_AVX2_AVAILABLE
  return std::nullopt;
}

// Helper for StrStrCaseInsensitive (uses char* instead of string_view).
// Returns std::nullopt if AVX2 was not used (caller should fall back to scalar).
// Returns a const char* value if AVX2 ran — trust the result, skip scalar entirely.
template<typename AVX2Func>
inline std::optional<const char*> TryAVX2PathStrStr(const char* haystack [[maybe_unused]],
                                                    const char* needle [[maybe_unused]],
                                                    [[maybe_unused]] size_t haystack_len,
                                                    [[maybe_unused]] size_t needle_len,
                                                    AVX2Func avx2_func [[maybe_unused]]) {
  #if STRING_SEARCH_AVX2_AVAILABLE
  if (haystack_len >= kAvx2MinTextLength && needle_len >= kMinSimdPatternLength &&
      cpu_features::GetAVX2Support() &&
      IsViewAscii(std::string_view(haystack, haystack_len)) &&
      IsViewAscii(std::string_view(needle, needle_len))) {
    return avx2_func();  // AVX2 ran — wrap result (match pointer or nullptr) in optional
  }
  #endif  // STRING_SEARCH_AVX2_AVAILABLE
  return std::nullopt;  // AVX2 not used
}

// Case-sensitive NEON dispatch: haystack may contain non-ASCII bytes (raw byte compare).
template<typename NEONFunc>
inline std::optional<bool> TryNEONPathCaseSensitive(
    [[maybe_unused]] const std::string_view& text,  // NOLINT(misc-no-recursion) - static call graph ContainsSubstring→TryNEONPathCaseSensitive→ContainsSubstringNEON→ContainsSubstring looks recursive but is guarded by length checks; no actual recursion
    [[maybe_unused]] const SimdSubstringHints& hints,
    NEONFunc neon_func [[maybe_unused]]) {
  #if STRING_SEARCH_NEON_AVAILABLE
  if (hints.use_neon && text.length() >= kNeonMinTextLength) {
    return neon_func();
  }
  #endif  // STRING_SEARCH_NEON_AVAILABLE
  return std::nullopt;
}

// Case-insensitive NEON dispatch: ASCII-only haystack and pattern.
template<typename NEONFunc>
inline std::optional<bool> TryNEONPathCaseInsensitive(
    [[maybe_unused]] const std::string_view& text,  // NOLINT(misc-no-recursion) - static call graph ContainsSubstringI→TryNEONPathCaseInsensitive→ContainsSubstringINEON→ContainsSubstringI looks recursive but is guarded by length checks; no actual recursion
    [[maybe_unused]] const SimdSubstringHints& hints,
    NEONFunc neon_func [[maybe_unused]]) {
  #if STRING_SEARCH_NEON_AVAILABLE
  if (hints.use_neon && hints.pattern_is_ascii && text.length() >= kNeonMinTextLength &&
      IsViewAscii(text)) {
    return neon_func();
  }
  #endif  // STRING_SEARCH_NEON_AVAILABLE
  return std::nullopt;
}

// NEON dispatch helper for char* overloads (StrStrCaseInsensitive).
// Returns std::nullopt if NEON was not used (caller should fall back to scalar).
// Returns a const char* value if NEON ran — trust the result, skip scalar entirely.
template<typename NEONFunc>
inline std::optional<const char*> TryNEONPathStrStr([[maybe_unused]] const char* haystack,  // NOLINT(misc-no-recursion) - static call graph StrStrCaseInsensitive→TryNEONPathStrStr→StrStrCaseInsensitiveNEON→StrStrCaseInsensitive looks recursive but is guarded by length checks; no actual recursion
                                                    [[maybe_unused]] const char* needle,
                                                    [[maybe_unused]] size_t haystack_len,
                                                    [[maybe_unused]] size_t needle_len,
                                                    NEONFunc neon_func [[maybe_unused]]) {
  #if STRING_SEARCH_NEON_AVAILABLE
  if (haystack_len >= kNeonMinTextLength && needle_len >= kMinSimdPatternLength &&
      IsViewAscii(std::string_view(haystack, haystack_len)) &&
      IsViewAscii(std::string_view(needle, needle_len))) {
    return neon_func();  // NEON ran — wrap result (match pointer or nullptr) in optional
  }
  #endif  // STRING_SEARCH_NEON_AVAILABLE
  return std::nullopt;  // NEON not used
}

// Template function for fuzzy matching
template<typename ComparePolicy>
inline bool FuzzyMatchInternal(std::string_view text, std::string_view pattern) {
  if (pattern.empty()) {
    return true;
  }
  if (text.empty()) {
    return false;
  }

  // Use auto (not const auto*) so MSVC deduces iterator type; .begin() returns an iterator, not a pointer.
  auto it_text = text.begin();  // NOLINT(llvm-qualified-auto,readability-qualified-auto) - MSVC C3535 if const auto* used with iterator
  for (const char p : pattern) {
    while (it_text != text.end() && !ComparePolicy::Equal(*it_text, p)) {
      ++it_text;
    }
    if (it_text == text.end()) {
      return false;
    }
    ++it_text;
  }
  return true;
}

} // namespace string_search_detail

[[nodiscard]] inline SimdSubstringHints MakeSimdSubstringHints(const std::string_view pattern) {
  SimdSubstringHints hints;
  if (pattern.length() < string_search_detail::kMinSimdPatternLength) {
    return hints;
  }
  hints.pattern_is_ascii = string_search_detail::IsViewAscii(pattern);
  #if STRING_SEARCH_AVX2_AVAILABLE
  hints.use_avx2 = cpu_features::GetAVX2Support();
  #endif  // STRING_SEARCH_AVX2_AVAILABLE
  #if STRING_SEARCH_NEON_AVAILABLE
  hints.use_neon = true;
  #endif  // STRING_SEARCH_NEON_AVAILABLE
  return hints;
}

// Optimized substring matching for search operations
// Returns true if 'text' contains 'pattern' as a substring
// Optimizations:
// - Fast prefix check (common case for "starts with" searches)
// - Reverse character comparison for longer patterns (fails faster)
// - Uses string_view to avoid allocations
inline bool ContainsSubstring(const std::string_view &text,
                              const std::string_view &pattern,
                              const SimdSubstringHints& hints) {
  if (pattern.empty()) {
    return true;
  }
  if (text.empty() || pattern.length() > text.length()) {
    return false;
  }

  // Fast path: Check if pattern is a prefix (very common case)
  // Many searches are "starts with" which this handles efficiently
  if (string_search_detail::CheckPrefix<string_search_detail::CaseSensitive>(text, pattern)) {
    return true;
  }

  // AVX2 optimization: Use for longer strings
  if (const auto avx2_result = string_search_detail::TryAVX2PathCaseSensitive(text, hints, [&text, &pattern]() {  // NOSONAR(cpp:S1481) - text, pattern used when AVX2 path taken
    #if STRING_SEARCH_AVX2_AVAILABLE
    return string_search::avx2::ContainsSubstringAVX2(text, pattern);
    #else
    return false;
    #endif  // STRING_SEARCH_AVX2_AVAILABLE
  })) {
    return *avx2_result;  // AVX2 ran — trust result (both true and false), skip scalar
  }

  // NEON optimization: arm64 (Apple Silicon, ARM64 Linux/Windows)
  if (const auto neon_result = string_search_detail::TryNEONPathCaseSensitive(text, hints, [&text, &pattern]() {  // NOSONAR(cpp:S1481) - text, pattern used when NEON path taken
    #if STRING_SEARCH_NEON_AVAILABLE
    return string_search::neon::ContainsSubstringNEON(text, pattern);
    #else
    return false;
    #endif  // STRING_SEARCH_NEON_AVAILABLE
  })) {
    return *neon_result;  // NEON ran — trust result (both true and false), skip scalar
  }

  // For longer patterns (>5 chars), use reverse comparison
  // This often fails faster because mismatches are detected sooner
  if (const size_t kReverseCompareMinPatternLength = 5; pattern.length() > kReverseCompareMinPatternLength) {
    const size_t pattern_len = pattern.length();
    const size_t text_len = text.length();
    const size_t max_start = text_len - pattern_len;

    for (size_t i = 0; i <= max_start; ++i) {
      if (string_search_detail::ReverseCompare<string_search_detail::CaseSensitive>(text, pattern, i)) {
        return true;
      }
    }
    return false;
  }
  // For short patterns, use standard find (already well-optimized)
  return text.find(pattern) != std::string_view::npos;  // NOSONAR(cpp:S935) - All paths return (false positive from template/conditional compilation analysis)
}

inline bool ContainsSubstring(const std::string_view &text,
                              const std::string_view &pattern) {
  return ContainsSubstring(text, pattern, MakeSimdSubstringHints(pattern));
}


// Case-insensitive version of strstr
// Returns pointer to first occurrence of needle in haystack (case-insensitive)
// Returns nullptr if not found
// Note: Windows API has StrStrI in shlwapi.h, so we use a different name
inline const char* StrStrCaseInsensitive(const char* haystack, const char* needle) {
  if (needle == nullptr || *needle == '\0') {
    return haystack;
  }
  if (haystack == nullptr) {
    return nullptr;
  }

  // AVX2 / NEON optimization: Use for longer strings
  #if STRING_SEARCH_AVX2_AVAILABLE
  size_t haystack_len = strlen(haystack);  // NOSONAR(cpp:S1081) - Safe: function parameter expected to be null-terminated C string (standard contract)
  size_t needle_len = strlen(needle);  // NOSONAR(cpp:S6004,cpp:S1081) - Variable used after if block; safe: function parameter expected to be null-terminated C string

  if (const auto avx2_result = string_search_detail::TryAVX2PathStrStr(haystack, needle, haystack_len, needle_len, [&haystack, &needle]() {
    return string_search::avx2::StrStrCaseInsensitiveAVX2(haystack, needle);
  }); avx2_result.has_value()) {
    return *avx2_result;  // AVX2 ran — trust result (match pointer or nullptr), skip scalar
  }
  #elif STRING_SEARCH_NEON_AVAILABLE
  const size_t haystack_len = strlen(haystack);  // NOSONAR(cpp:S1081) - Safe: null-terminated C-string contract
  const size_t needle_len   = strlen(needle);    // NOSONAR(cpp:S1081) - Safe: null-terminated C-string contract

  if (const auto neon_result = string_search_detail::TryNEONPathStrStr(haystack, needle, haystack_len, needle_len, [&haystack, &needle]() {
    return string_search::neon::StrStrCaseInsensitiveNEON(haystack, needle);
  }); neon_result.has_value()) {
    return *neon_result;  // NEON ran — trust result (match pointer or nullptr), skip scalar
  }
  #endif  // STRING_SEARCH_AVX2_AVAILABLE / STRING_SEARCH_NEON_AVAILABLE

  // Scalar fallback (existing implementation)
  for (const char* haystack_it = haystack; *haystack_it != '\0'; ++haystack_it) {
    const char* needle_it = needle;
    const char* haystack_probe = haystack_it;
    while (*needle_it != '\0' && *haystack_probe != '\0' &&
           ToLowerChar(static_cast<unsigned char>(*needle_it)) ==
               ToLowerChar(static_cast<unsigned char>(*haystack_probe))) {
      ++needle_it;
      ++haystack_probe;
    }
    if (*needle_it == '\0') {
      return haystack_it;  // Found match
    }
  }
  return nullptr;  // NOSONAR(cpp:S935) - All paths return (false positive from conditional compilation analysis)
}

// Case-insensitive substring search
// Returns true if 'text' contains 'pattern' as a substring (case-insensitive)
// Optimizations similar to ContainsSubstring but with case-insensitive comparison
inline bool ContainsSubstringI(const std::string_view &text,
                               const std::string_view &pattern,
                               const SimdSubstringHints& hints) {
  if (pattern.empty()) {
    return true;
  }
  if (text.empty() || pattern.length() > text.length()) {
    return false;
  }

  // AVX2 optimization: Use for longer strings
  if (const auto avx2_result = string_search_detail::TryAVX2PathCaseInsensitive(text, hints, [&text, &pattern]() {  // NOSONAR(cpp:S1481) - text, pattern used when AVX2 path taken
    #if STRING_SEARCH_AVX2_AVAILABLE
    return string_search::avx2::ContainsSubstringIAVX2(text, pattern);
    #else
    return false;
    #endif  // STRING_SEARCH_AVX2_AVAILABLE
  })) {
    return *avx2_result;  // AVX2 ran — trust result (both true and false), skip scalar
  }

  // NEON optimization: arm64 (Apple Silicon, ARM64 Linux/Windows)
  if (const auto neon_result = string_search_detail::TryNEONPathCaseInsensitive(text, hints, [&text, &pattern]() {  // NOSONAR(cpp:S1481) - text, pattern used when NEON path taken
    #if STRING_SEARCH_NEON_AVAILABLE
    return string_search::neon::ContainsSubstringINEON(text, pattern);
    #else
    return false;
    #endif  // STRING_SEARCH_NEON_AVAILABLE
  })) {
    return *neon_result;  // NEON ran — trust result (both true and false), skip scalar
  }

  // Fast path: Check if pattern is a prefix (very common case)
  if (string_search_detail::CheckPrefix<string_search_detail::CaseInsensitive>(text, pattern)) {
    return true;
  }

  // For longer patterns (>5 chars), use reverse comparison
  if (const size_t kReverseCompareMinPatternLengthI = 5; pattern.length() > kReverseCompareMinPatternLengthI) {
    const size_t pattern_len = pattern.length();
    const size_t text_len = text.length();
    const size_t max_start = text_len - pattern_len;

    for (size_t i = 0; i <= max_start; ++i) {
      if (string_search_detail::ReverseCompare<string_search_detail::CaseInsensitive>(text, pattern, i)) {
        return true;
      }
    }
    return false;
  }
  // For short patterns, use case-insensitive search
  return string_search_detail::ShortPatternSearch<string_search_detail::CaseInsensitive>(text, pattern);
}

inline bool ContainsSubstringI(const std::string_view &text,
                               const std::string_view &pattern) {
  return ContainsSubstringI(text, pattern, MakeSimdSubstringHints(pattern));
}

// Case-sensitive dispatch: routes to ContainsSubstringI or ContainsSubstring based on flag.
// Eliminates the repeated if (case_sensitive) ... else ... pattern across callers.
inline bool ContainsSubstring(std::string_view text, std::string_view pattern,
                               bool case_sensitive) {
  if (case_sensitive) {
    return ContainsSubstring(text, pattern);
  }
  return ContainsSubstringI(text, pattern);
}

/**
 * @brief Fuzzy matching (subsequence matching)
 *
 * Returns true if all characters in 'pattern' appear in 'text' in the same order.
 * Example: "fbr" matches "foobar", "fiber", "foo/bar"
 *
 * @param text The string to search in
 * @param pattern The sequence of characters to look for
 * @return true if pattern is a subsequence of text
 */
inline bool FuzzyMatch(std::string_view text, std::string_view pattern) {
  return string_search_detail::FuzzyMatchInternal<string_search_detail::CaseSensitive>(text, pattern);
}

/**
 * @brief Case-insensitive fuzzy matching (subsequence matching)
 *
 * Returns true if all characters in 'pattern' appear in 'text' in the same order,
 * ignoring case.
 *
 * @param text The string to search in
 * @param pattern The sequence of characters to look for
 * @return true if pattern is a case-insensitive subsequence of text
 */
inline bool FuzzyMatchI(std::string_view text, std::string_view pattern) {
  return string_search_detail::FuzzyMatchInternal<string_search_detail::CaseInsensitive>(text, pattern);
}


} // namespace string_search
