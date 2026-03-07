#pragma once

#include <algorithm>  // For std::min
#include <cctype>
#include <cstring>
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

// Internal implementation details for deduplication
namespace string_search_detail {

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
  
  for (size_t i = 0; i < pattern.length(); ++i) {
    if (!ComparePolicy::Equal(text[i], pattern[i])) {
      return false;
    }
  }
  return true;
}

// Template function for reverse comparison
template<typename ComparePolicy>
inline bool ReverseCompare(const std::string_view& text,
                           const std::string_view& pattern,
                           size_t start_pos) {
  const size_t pattern_len = pattern.length();
  for (size_t j = pattern_len; j > 0; --j) {
    if (!ComparePolicy::Equal(text[start_pos + j - 1], pattern[j - 1])) {
      return false;
    }
  }
  return true;
}

// Template function for short pattern search
template<typename ComparePolicy>
inline bool ShortPatternSearch(const std::string_view& text,
                               const std::string_view& pattern) {
  if (text.length() < pattern.length()) {
    return false;
  }
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
  return false;  // NOSONAR(cpp:S935) - All paths return (false positive from template analysis)
}

// Helper to check ASCII and try AVX2 path
template<typename AVX2Func>
inline bool TryAVX2Path([[maybe_unused]] const std::string_view& text,    // Used when STRING_SEARCH_AVX2_AVAILABLE
                        [[maybe_unused]] const std::string_view& pattern,  // Used when STRING_SEARCH_AVX2_AVAILABLE
                        AVX2Func avx2_func [[maybe_unused]]) {
  #if STRING_SEARCH_AVX2_AVAILABLE
  if (text.length() >= 32 && pattern.length() >= 4 && cpu_features::GetAVX2Support()) {
    // Quick ASCII check (first 64 bytes) - AVX2 fast path is ASCII-only
    bool is_ascii = true;
    size_t check_len = (std::min)(text.length(), size_t(64));
    for (size_t i = 0; i < check_len; ++i) {
      if (static_cast<unsigned char>(text[i]) > 127) {
        is_ascii = false;
        break;
      }
    }
    
    if (is_ascii) {
      return avx2_func();  // Call AVX2 function
    }
  }
  #endif  // STRING_SEARCH_AVX2_AVAILABLE
  return false;  // AVX2 not used
}

// Helper for StrStrCaseInsensitive (uses char* instead of string_view)
template<typename AVX2Func>
inline const char* TryAVX2PathStrStr(const char* haystack [[maybe_unused]],
                                     [[maybe_unused]] size_t haystack_len,  // Used when STRING_SEARCH_AVX2_AVAILABLE
                                     [[maybe_unused]] size_t needle_len,    // Used when STRING_SEARCH_AVX2_AVAILABLE
                                     AVX2Func avx2_func [[maybe_unused]]) {
  #if STRING_SEARCH_AVX2_AVAILABLE
  if (haystack_len >= 32 && needle_len >= 4 && cpu_features::GetAVX2Support()) {
    // Quick ASCII check (first 64 bytes) - AVX2 fast path is ASCII-only
    bool is_ascii = true;
    size_t check_len = haystack_len < 64 ? haystack_len : 64;
    for (size_t i = 0; i < check_len; ++i) {
      if (static_cast<unsigned char>(haystack[i]) > 127) {
        is_ascii = false;
        break;
      }
    }

    if (is_ascii) {
      return avx2_func();  // Call AVX2 function
    }
  }
  #endif  // STRING_SEARCH_AVX2_AVAILABLE
  return nullptr;  // AVX2 not used
}

// NEON dispatch helper for string_view overloads (arm64 only, no runtime detection needed).
// Threshold: 16 bytes (NEON register width) vs 32 for AVX2.
template<typename NEONFunc>
inline bool TryNEONPath([[maybe_unused]] const std::string_view& text,  // NOLINT(misc-no-recursion) - static call graph ContainsSubstringI→TryNEONPath→ContainsSubstringINEON→ContainsSubstringI looks recursive but is guarded by length checks; no actual recursion
                        [[maybe_unused]] const std::string_view& pattern,
                        NEONFunc neon_func [[maybe_unused]]) {
  #if STRING_SEARCH_NEON_AVAILABLE
  constexpr size_t kMaxAscii = 127;
  constexpr size_t kAsciiCheckLen = 64;
  if (text.length() >= 16 && pattern.length() >= 4) {
    // Quick ASCII check (first 64 bytes) — NEON fast path is ASCII-only
    bool is_ascii = true;
    const size_t check_len = (std::min)(text.length(), kAsciiCheckLen);
    for (size_t i = 0; i < check_len; ++i) {
      if (static_cast<unsigned char>(text[i]) > kMaxAscii) {
        is_ascii = false;
        break;
      }
    }
    if (is_ascii) {
      return neon_func();
    }
  }
  #endif  // STRING_SEARCH_NEON_AVAILABLE
  return false;
}

// NEON dispatch helper for char* overloads (StrStrCaseInsensitive).
template<typename NEONFunc>
inline const char* TryNEONPathStrStr([[maybe_unused]] const char* haystack,  // NOLINT(misc-no-recursion) - static call graph StrStrCaseInsensitive→TryNEONPathStrStr→StrStrCaseInsensitiveNEON→StrStrCaseInsensitive looks recursive but is guarded by length checks; no actual recursion
                                     [[maybe_unused]] size_t haystack_len,
                                     [[maybe_unused]] size_t needle_len,
                                     NEONFunc neon_func [[maybe_unused]]) {
  #if STRING_SEARCH_NEON_AVAILABLE
  constexpr size_t kMaxAscii = 127;
  constexpr size_t kAsciiCheckLen = 64;
  if (haystack_len >= 16 && needle_len >= 4) {
    bool is_ascii = true;
    const size_t check_len = (std::min)(haystack_len, kAsciiCheckLen);
    for (size_t i = 0; i < check_len; ++i) {
      if (static_cast<unsigned char>(haystack[i]) > kMaxAscii) {  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) - Pointer offset into char buffer is intentional for ASCII check
        is_ascii = false;
        break;
      }
    }
    if (is_ascii) {
      return neon_func();
    }
  }
  #endif  // STRING_SEARCH_NEON_AVAILABLE
  return nullptr;
}

} // namespace string_search_detail

// Optimized substring matching for search operations
// Returns true if 'text' contains 'pattern' as a substring
// Optimizations:
// - Fast prefix check (common case for "starts with" searches)
// - Reverse character comparison for longer patterns (fails faster)
// - Uses string_view to avoid allocations
inline bool ContainsSubstring(const std::string_view &text,
                              const std::string_view &pattern) {
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
  if (string_search_detail::TryAVX2Path(text, pattern, [&text, &pattern]() {  // NOSONAR(cpp:S1481) - text, pattern used when AVX2 path taken
    #if STRING_SEARCH_AVX2_AVAILABLE
    return string_search::avx2::ContainsSubstringAVX2(text, pattern);
    #else
    return false;
    #endif  // STRING_SEARCH_AVX2_AVAILABLE
  })) {
    return true;
  }

  // NEON optimization: arm64 (Apple Silicon, ARM64 Linux/Windows)
  if (string_search_detail::TryNEONPath(text, pattern, [&text, &pattern]() {  // NOSONAR(cpp:S1481) - text, pattern used when NEON path taken
    #if STRING_SEARCH_NEON_AVAILABLE
    return string_search::neon::ContainsSubstringNEON(text, pattern);
    #else
    return false;
    #endif  // STRING_SEARCH_NEON_AVAILABLE
  })) {
    return true;
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

  if (const char* avx2_result = string_search_detail::TryAVX2PathStrStr(haystack, haystack_len, needle_len, [&haystack, &needle]() {
    return string_search::avx2::StrStrCaseInsensitiveAVX2(haystack, needle);
  }); avx2_result != nullptr) {
    return avx2_result;  // AVX2 found a match
  }
  #elif STRING_SEARCH_NEON_AVAILABLE
  const size_t haystack_len = strlen(haystack);  // NOSONAR(cpp:S1081) - Safe: null-terminated C-string contract
  const size_t needle_len   = strlen(needle);    // NOSONAR(cpp:S1081) - Safe: null-terminated C-string contract

  if (const char* neon_result = string_search_detail::TryNEONPathStrStr(haystack, haystack_len, needle_len, [&haystack, &needle]() {
    return string_search::neon::StrStrCaseInsensitiveNEON(haystack, needle);
  }); neon_result != nullptr) {
    return neon_result;  // NEON found a match
  }
  #endif  // STRING_SEARCH_AVX2_AVAILABLE / STRING_SEARCH_NEON_AVAILABLE

  // Scalar fallback (existing implementation)
  for (const char* haystack_it = haystack; *haystack_it != '\0'; ++haystack_it) {  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) - Pointer iteration over C-string is intentional and performance-critical here
    const char* needle_it = needle;
    const char* haystack_probe = haystack_it;
    while (*needle_it != '\0' && *haystack_probe != '\0' &&
           ToLowerChar(static_cast<unsigned char>(*needle_it)) ==
               ToLowerChar(static_cast<unsigned char>(*haystack_probe))) {
      ++needle_it;      // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) - Pointer increment over C-string is intentional here
      ++haystack_probe; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) - Pointer increment over C-string is intentional here
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
                               const std::string_view &pattern) {
  if (pattern.empty()) {
    return true;
  }
  if (text.empty() || pattern.length() > text.length()) {
    return false;
  }

  // AVX2 optimization: Use for longer strings
  if (string_search_detail::TryAVX2Path(text, pattern, [&text, &pattern]() {  // NOSONAR(cpp:S1481) - text, pattern used when AVX2 path taken
    #if STRING_SEARCH_AVX2_AVAILABLE
    return string_search::avx2::ContainsSubstringIAVX2(text, pattern);
    #else
    return false;
    #endif  // STRING_SEARCH_AVX2_AVAILABLE
  })) {
    return true;
  }

  // NEON optimization: arm64 (Apple Silicon, ARM64 Linux/Windows)
  if (string_search_detail::TryNEONPath(text, pattern, [&text, &pattern]() {  // NOSONAR(cpp:S1481) - text, pattern used when NEON path taken
    #if STRING_SEARCH_NEON_AVAILABLE
    return string_search::neon::ContainsSubstringINEON(text, pattern);
    #else
    return false;
    #endif  // STRING_SEARCH_NEON_AVAILABLE
  })) {
    return true;
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


} // namespace string_search
