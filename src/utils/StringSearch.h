#pragma once

#include <algorithm>  // For std::min
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Compile-time AVX2 availability: Check if we're on x86/x64 architecture
// CMakeLists.txt ensures AVX2 compiler flags are set for StringSearchAVX2.cpp on x86/x64
// Runtime detection (SupportsAVX2()) determines if the CPU actually supports AVX2
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    #define STRING_SEARCH_AVX2_AVAILABLE 1  // NOLINT(cppcoreguidelines-macro-usage) - retained suppression  // NOSONAR(cpp:S5028) - Must remain macro for #if preprocessor directives
#else
    // ARM or other architectures - AVX2 not available
    #define STRING_SEARCH_AVX2_AVAILABLE 0  // NOLINT(cppcoreguidelines-macro-usage) - retained suppression  // NOSONAR(cpp:S5028) - Must remain macro for #if preprocessor directives
#endif  // defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)

// Compile-time NEON availability: AArch64 (Apple Silicon, ARM64 Linux/Windows).
// vmaxvq_u8 (horizontal max) used in the implementation requires AArch64.
#if defined(__aarch64__) || defined(_M_ARM64)
    #define STRING_SEARCH_NEON_AVAILABLE 1  // NOLINT(cppcoreguidelines-macro-usage) - retained suppression  // NOSONAR(cpp:S5028) - Must remain macro for #if preprocessor directives
#else
    #define STRING_SEARCH_NEON_AVAILABLE 0  // NOLINT(cppcoreguidelines-macro-usage) - retained suppression  // NOSONAR(cpp:S5028) - Must remain macro for #if preprocessor directives
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

// Compile-time ASCII lowercase table (C-locale std::tolower mapping).
// Single source of truth for scalar case folding; SIMD ToLower variants
// implement the same mapping for 32/16-byte chunks.
constexpr std::array<char, 256> BuildAsciiLowerTable() {
  std::array<char, 256> table{};
  for (int i = 0; i < 256; ++i) {
    const auto c = static_cast<unsigned char>(i);
    table[static_cast<size_t>(i)] =  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index) - index validated by surrounding bounds check
        static_cast<char>((c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c);
  }
  return table;
}
inline constexpr std::array<char, 256> kAsciiLowerTable =
    BuildAsciiLowerTable();

static_assert(kAsciiLowerTable['A'] == 'a', "table must map A->a");  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index validated by surrounding bounds check
static_assert(kAsciiLowerTable['Z'] == 'z', "table must map Z->z");  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index validated by surrounding bounds check
static_assert(kAsciiLowerTable['a'] == 'a', "table must keep a");  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index validated by surrounding bounds check
static_assert(kAsciiLowerTable['0'] == '0', "table must keep digits");  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index validated by surrounding bounds check
static_assert(kAsciiLowerTable[200] == static_cast<char>(200),  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-magic-numbers) - index validated by surrounding bounds check
              "table must keep high bytes");

// Case-insensitive character comparison helper.
// Table-driven: one load per byte instead of a locale-aware std::tolower
// call. Mapping equals std::tolower in the C locale (ASCII-only, consistent
// with ToLowerAVX2/ToLowerNEON); built at compile time, no init cost.
// Measured (tolower_benchmark, Release, 2026-09-28): ~17% faster than
// branchless ASCII in the insensitive search loop, ~26% faster raw lowering.
// Do not switch to branchless on register-pressure theory alone: re-run the
// benchmark head-to-head first (it keeps all three variants standalone).
inline char ToLowerChar(unsigned char ch_value) {
  return kAsciiLowerTable[ch_value];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index) - index validated by surrounding bounds check
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
    // Reject common near-misses before the full loop by checking a middle
    // character and the final character; both are already known to be part of
    // the needle and are cheap to compare.
    if (len > 1 &&
        ToLowerChar(static_cast<unsigned char>(a[len - 1])) !=
            ToLowerChar(static_cast<unsigned char>(b[len - 1]))) {
      return false;
    }
    if (len > 2) {
      if (const size_t mid = len / 2;
          ToLowerChar(static_cast<unsigned char>(a[mid])) !=
              ToLowerChar(static_cast<unsigned char>(b[mid]))) {
        return false;
      }
    }
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
  bool use_avx2 = false;  // NOLINT(readability-identifier-naming) - public POD field; stable search API
  bool use_neon = false;  // NOLINT(readability-identifier-naming) - public POD field; stable search API
  bool pattern_is_ascii = false;  // NOLINT(readability-identifier-naming) - public POD field; stable search API
};

// Physical keyboard layout for typo-cost modeling (item 3). Only the
// *ranking* of typo matches depends on it: recall (whether a candidate
// matches at all) is layout-independent, so the boolean filter path takes
// no layout parameter.
enum class KeyboardLayout : std::uint8_t {
  Qwerty,
  Azerty,
  Union,  // Adjacent in either layout counts (default: no detection needed)
};

// Per-letter physical neighbor sets (generous 8-neighborhood with row
// stagger; a heuristic cost model, not a geometry proof). Letters only:
// digits/symbols keep full substitution cost on every layout.
constexpr size_t kKeyboardAlphabetSize = 26;
constexpr std::array<const char*, kKeyboardAlphabetSize> kQwertyNeighbors = {{
    "qwszx", "vghn", "xdfv", "serfxcv", "wrsdf", "drtgcvb", "ftyhvbn",  // a-g
    "gyujbnm", "uojkl", "huiknm", "jiolm", "kop", "njk", "bhjm",  // h-n
    "ipkl", "ol", "was", "etdfg", "awedzxc", "ryfgh", "yihjk",  // o-u
    "cfgb", "qeasd", "zsdc", "tughj", "asx",  // v-z
},};
constexpr std::array<const char*, kKeyboardAlphabetSize> kAzertyNeighbors = {{
    "zqs", "vghn", "xdfv", "serfxcv", "zrsdf", "drtgcvb", "ftyhvbn",  // a-g
    "gyujbn", "uojkl", "huikn", "jiol", "kopm", "lop", "bhj",  // h-n
    "ipklm", "olm", "azswx", "etdfg", "qzedwxc", "ryfgh", "yihjk",  // o-u
    "cfgb", "qsx", "wsdc", "tughj", "aeqsd",  // v-z
},};

// Strict physical adjacency (same character is equality, not adjacency).
// Inputs must already be ASCII-lowered; anything outside a-z returns false.
// Checked in both directions: the per-letter sets are a heuristic and need
// not list every pair symmetrically.
inline bool AreAdjacentKeys(char lowered_a, char lowered_b, KeyboardLayout layout) {
  if (lowered_a == lowered_b || lowered_a < 'a' || lowered_a > 'z' || lowered_b < 'a' ||
      lowered_b > 'z') {
    return false;
  }
  const auto ai = static_cast<size_t>(lowered_a - 'a');
  const auto bi = static_cast<size_t>(lowered_b - 'a');
  const auto ListsPair = [&](const std::array<const char*, kKeyboardAlphabetSize>& table) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - ai/bi range-checked a-z above
    return std::strchr(table[ai], lowered_b) != nullptr ||
           // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - ai/bi range-checked a-z above
           std::strchr(table[bi], lowered_a) != nullptr;
  };
  if (layout == KeyboardLayout::Azerty) {
    return ListsPair(kAzertyNeighbors);
  }
  if (ListsPair(kQwertyNeighbors)) {
    return true;
  }
  return layout == KeyboardLayout::Union && ListsPair(kAzertyNeighbors);
}

// Internal implementation details for deduplication
namespace string_search_detail {

constexpr size_t kAvx2MinTextLength = 32;
constexpr size_t kNeonMinTextLength = 16;
constexpr size_t kMinSimdPatternLength = 4;
constexpr unsigned char kMaxAsciiByte = 127;

[[nodiscard]] inline bool IsViewAsciiScalar(const std::string_view text) {  // NOLINT(readability-identifier-naming) - param name; not a constant
  return std::none_of(text.begin(), text.end(),  //  - C++17; std::ranges requires C++20
                      [](const unsigned char ch) { return ch > kMaxAsciiByte; });  // NOLINT(readability-identifier-naming) - lambda param
}

// Full-haystack ASCII validation for case-insensitive SIMD (ToLowerAVX2/NEON is ASCII-only).
// Uses AVX2/NEON chunk scans when available; scalar tail for short buffers.
[[nodiscard]] inline bool IsViewAscii(const std::string_view text) {  // NOLINT(readability-identifier-naming) - param name; not a constant
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

  for (size_t i = 0; i < pattern.length(); ++i) {
    if (!ComparePolicy::Equal(text[i], pattern[i])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
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
    if (!ComparePolicy::Equal(text[start_pos + j - 1], pattern[j - 1])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
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
      if (!ComparePolicy::Equal(text[i + j], pattern[j])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
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

// Case-insensitive AVX2 dispatch: requires ASCII-only pattern (checked once per
// search in hints). The haystack needs no ASCII pre-scan: ToLowerAVX2 only maps
// bytes in [A,Z] and passes high-bit bytes through, so they can never falsely
// match an ASCII pattern byte; candidates are confirmed by scalar FullCompare.
template<typename AVX2Func>
inline std::optional<bool> TryAVX2PathCaseInsensitive(
    [[maybe_unused]] const std::string_view& text,
    [[maybe_unused]] const SimdSubstringHints& hints,
    AVX2Func avx2_func [[maybe_unused]]) {
  #if STRING_SEARCH_AVX2_AVAILABLE
  if (hints.use_avx2 && hints.pattern_is_ascii && text.length() >= kAvx2MinTextLength) {
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
      IsViewAscii(std::string_view(needle, needle_len))) {
    return avx2_func();  // AVX2 ran — wrap result (match pointer or nullptr) in optional
  }
  #endif  // STRING_SEARCH_AVX2_AVAILABLE
  return std::nullopt;  // AVX2 not used
}

// Case-sensitive NEON dispatch: haystack may contain non-ASCII bytes (raw byte compare).
template<typename NEONFunc>
inline std::optional<bool> TryNEONPathCaseSensitive(
    [[maybe_unused]] const std::string_view& text,
    [[maybe_unused]] const SimdSubstringHints& hints,
    NEONFunc neon_func [[maybe_unused]]) {
  #if STRING_SEARCH_NEON_AVAILABLE
  if (hints.use_neon && text.length() >= kNeonMinTextLength) {
    return neon_func();
  }
  #endif  // STRING_SEARCH_NEON_AVAILABLE
  return std::nullopt;
}

// Case-insensitive NEON dispatch: requires ASCII-only pattern (checked once per
// search in hints). Same no-haystack-pre-scan rationale as the AVX2 path above.
template<typename NEONFunc>
inline std::optional<bool> TryNEONPathCaseInsensitive(
    [[maybe_unused]] const std::string_view& text,
    [[maybe_unused]] const SimdSubstringHints& hints,
    NEONFunc neon_func [[maybe_unused]]) {
  #if STRING_SEARCH_NEON_AVAILABLE
  if (hints.use_neon && hints.pattern_is_ascii && text.length() >= kNeonMinTextLength) {
    return neon_func();
  }
  #endif  // STRING_SEARCH_NEON_AVAILABLE
  return std::nullopt;
}

// NEON dispatch helper for char* overloads (StrStrCaseInsensitive).
// Returns std::nullopt if NEON was not used (caller should fall back to scalar).
// Returns a const char* value if NEON ran — trust the result, skip scalar entirely.
template<typename NEONFunc>
inline std::optional<const char*> TryNEONPathStrStr([[maybe_unused]] const char* haystack,
                                                    [[maybe_unused]] const char* needle,
                                                    [[maybe_unused]] size_t haystack_len,
                                                    [[maybe_unused]] size_t needle_len,
                                                    NEONFunc neon_func [[maybe_unused]]) {
  #if STRING_SEARCH_NEON_AVAILABLE
  if (haystack_len >= kNeonMinTextLength && needle_len >= kMinSimdPatternLength &&
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

// Bounded Damerau-Levenshtein distance <= 1 check with O(1) length filter
template<typename ComparePolicy>
inline bool DamerauLevenshteinDistance1Internal(std::string_view pattern, std::string_view text) {
  const auto p_len = static_cast<int>(pattern.length());
  const auto t_len = static_cast<int>(text.length());
  if (std::abs(p_len - t_len) > 1) {
    return false;
  }

  int i = 0;
  int j = 0;
  int diffs = 0;

  while (i < p_len && j < t_len) {
    if (ComparePolicy::Equal(pattern[static_cast<size_t>(i)], text[static_cast<size_t>(j)])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      ++i;
      ++j;
      continue;
    }
    if (++diffs > 1) {
      return false;
    }

    // Check adjacent transposition
    if (i + 1 < p_len && j + 1 < t_len &&
        ComparePolicy::Equal(pattern[static_cast<size_t>(i)], text[static_cast<size_t>(j + 1)]) &&  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,bugprone-misplaced-widening-cast) - StringSearch hot-path invariant
        ComparePolicy::Equal(pattern[static_cast<size_t>(i + 1)], text[static_cast<size_t>(j)])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,bugprone-misplaced-widening-cast) - StringSearch hot-path invariant
      i += 2;
      j += 2;
      continue;
    }

    if (p_len > t_len) {
      ++i;  // Deletion from pattern (extra character in pattern)
    } else if (p_len < t_len) {
      ++j;  // Insertion into pattern (missing character in pattern)
    } else {
      ++i;  // Substitution
      ++j;
    }
  }

  if (i < p_len || j < t_len) {
    ++diffs;
  }
  return diffs <= 1;
}

// Detect word boundaries or camelCase transitions.
// v2: also treats digit/letter transitions, ':' (drive-letter paths) and
// UPPER-run endings ("HTMLParser": 'P' preceded by upper but followed by
// lower) as boundaries so acronym queries rank above accidental matches.
inline bool IsWordBoundary(std::string_view text, size_t index) {
  if (index == 0) {
    return true;
  }
  const char prev = text[index - 1];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
  if (prev == '_' || prev == '-' || prev == '.' || prev == '/' || prev == '\\' || prev == ' ' ||
      prev == ':') {
    return true;
  }
  const char curr = text[index];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
  const bool prev_lower = (prev >= 'a' && prev <= 'z');
  const bool prev_upper = (prev >= 'A' && prev <= 'Z');
  const bool prev_digit = (prev >= '0' && prev <= '9');
  const bool curr_lower = (curr >= 'a' && curr <= 'z');
  const bool curr_upper = (curr >= 'A' && curr <= 'Z');
  const bool curr_digit = (curr >= '0' && curr <= '9');
  if (prev_lower && curr_upper) {
    return true;
  }
  // Digit/letter transition either direction.
  if ((prev_digit != curr_digit) && ((prev_digit && (curr_lower || curr_upper)) ||
                                     (curr_digit && (prev_lower || prev_upper)))) {
    return true;
  }
  // End of an UPPER run: "HTMLParser" -> 'P' is a boundary.
  if (curr_upper && prev_upper && index + 1 < text.length()) {
    if (const char next = text[index + 1]; next >= 'a' && next <= 'z') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      return true;
    }
  }
  return false;
}

// Extension start (index after the last '.' that is not the first character).
// Returns text.length() when there is no extension.
inline size_t FindExtensionStart(std::string_view text) {
  if (text.length() <= 1) {
    return text.length();
  }
  for (size_t i = text.length(); i-- > 1;) {
    if (text[i] == '.') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      return i + 1;
    }
  }
  return text.length();
}


// Scoring weights v2 (see spec section 3.2). Tuned so compact/boundary/
// stem matches separate from bloated/accidental/extension matches by
// tens of points instead of 1-3 points (v1 used +25/-5/excess/2).
constexpr int kFuzzyBaseScore = 500;
constexpr int kFuzzyFirstCharAtZeroBonus = 45;
constexpr int kFuzzyFirstCharNearBonus = 20;
constexpr int kFuzzyFirstCharLatePenaltyPerChar = 3;
constexpr int kFuzzyConsecutiveBonus = 35;
constexpr int kFuzzyGapPenaltyPerChar = 4;
// Gaps landing on a word boundary cost less: skipping "earch" to reach the
// 'C' in "SearchController" is expected acronym behavior, while skipping
// mid-word characters is accidental. Two running maxima keep this O(1).
constexpr int kFuzzyGapToBoundaryPenaltyPerChar = 2;
constexpr int kFuzzyBoundaryBonus = 45;
constexpr int kFuzzyExtensionPenalty = 25;
constexpr int kFuzzyCaseExactBonus = 4;
constexpr int kFuzzyExcessPenaltyPerChar = 1;
constexpr size_t kFuzzyExcessFreeChars = 10;
constexpr size_t kFuzzyMaxExcessPenaltyChars = 60;
constexpr float kFuzzyMaxCoverageBonus = 20.0F;
constexpr size_t kFuzzyScoringMaxTextLen = 512;
constexpr size_t kFuzzyScoringMaxPatternLen = 128;

// First-character placement bonus (v2 §3.2): strong reward for index 0,
// smaller for 1-2, linear late-start penalty beyond.
inline int FirstCharStartBonus(size_t t) {
  if (t == 0) {
    return kFuzzyFirstCharAtZeroBonus;
  }
  if (t <= 2) {
    return kFuzzyFirstCharNearBonus;
  }
  return -static_cast<int>(t * static_cast<size_t>(kFuzzyFirstCharLatePenaltyPerChar));
}

// Best gapped-predecessor score for matching pattern[p] at text[t]: boundary
// landings use the cheaper gap rate (acronym skips are expected, mid-word
// skips are accidental). Returns invalid when no gapped predecessor exists.
inline int BestGappedPredecessor(int running_best_boundary, int running_best_plain, bool lands_on_boundary,
                                 size_t t, int invalid) {
  if (lands_on_boundary) {
    if (running_best_boundary == invalid) {
      return invalid;
    }
    return running_best_boundary -
           static_cast<int>(t * static_cast<size_t>(kFuzzyGapToBoundaryPenaltyPerChar)) +
           kFuzzyGapToBoundaryPenaltyPerChar;
  }
  if (running_best_plain == invalid) {
    return invalid;
  }
  return running_best_plain - static_cast<int>(t * static_cast<size_t>(kFuzzyGapPenaltyPerChar)) +
         kFuzzyGapPenaltyPerChar;
}

// Scores pattern[0] at every text position (first DP row). Split from
// EvaluateSubsequenceScore to keep nesting/complexity budgets.
template<typename ComparePolicy, typename PositionBonusFn>
inline void ScoreSubsequenceFirstRow(std::string_view text, char pat,
                                     const PositionBonusFn& position_bonus,
                                     std::array<int, kFuzzyScoringMaxTextLen>& prev, size_t t_len) {
  for (size_t t = 0; t < t_len; ++t) {
    if (!ComparePolicy::Equal(text[t], pat)) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      continue;
    }
    prev[t] = kFuzzyBaseScore + FirstCharStartBonus(t) + position_bonus(t, pat);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
  }
}

// Typo pricing in halves of an edit (item 3): transpositions and adjacent-key
// (fat-finger) substitutions cost 1, anything else costs 2. Score penalty is
// -25 per half, so a fat-finger typo scores 225 + credits where a random
// substitution scores 200 + credits (previous flat -50 behavior preserved
// for the random case).
constexpr int kTypoBaseScore = 250;
constexpr int kTypoCostPerHalfEdit = 25;
constexpr int kTypoK2BudgetHalves = 4;

template<typename ComparePolicy>
// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Single-pass best-alignment DP kernel: match/consecutive/gap/boundary branches are the recurrence itself; splitting would fragment it across extra passes over hot data
inline std::optional<uint16_t> EvaluateSubsequenceScore(std::string_view text, std::string_view pattern) {  // NOSONAR(cpp:S3776) - Same rationale as above
  if (pattern.empty() || text.empty() || pattern.length() > text.length()) {
    return std::nullopt;
  }
  if (pattern.length() > kFuzzyScoringMaxPatternLen || text.length() > kFuzzyScoringMaxTextLen) {
    return std::nullopt;
  }
  const size_t t_len = text.length();
  const size_t p_len = pattern.length();
  const size_t ext_start = FindExtensionStart(text);

  // O(P*T) best-alignment DP with O(T) memory (two stack rows, zero heap).
  // prev[t] = best score for pattern[0..p-1] with pattern[p-1] matched at text[t].
  // For p > 0, each cell takes max(consecutive, gapped) in O(1) via running
  // maxima over s <= t-2 of (prev[s] + g*s) for each gap rate g, since
  //   prev[s] - (t-s-1)*g = (prev[s] + g*s) - g*t + g.
  // v1 used greedy leftmost matching, which locked onto poor early alignments.
  const auto position_bonus = [&text, ext_start](size_t pos, char pat_char) {
    int bonus = 0;
    // Boundary only for pos > 0: index 0 already gets the early bonus, so
    // counting IsWordBoundary(text, 0) == true again would double-count.
    if (pos > 0 && IsWordBoundary(text, pos)) {
      bonus += kFuzzyBoundaryBonus;
    }
    if (pos >= ext_start) {
      bonus -= kFuzzyExtensionPenalty;
    }
    if (text[pos] == pat_char) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      bonus += kFuzzyCaseExactBonus;
    }
    return bonus;
  };

  static constexpr int kInvalid = -1000000;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init) - initialized by the loops immediately below; value-init would add a redundant 4KB memset per scoring call
  std::array<int, kFuzzyScoringMaxTextLen> prev;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init) - same rationale as above
  std::array<int, kFuzzyScoringMaxTextLen> curr;
  for (size_t t = 0; t < t_len; ++t) {
    prev[t] = kInvalid;  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
  }

  for (size_t p = 0; p < p_len; ++p) {
    const char pat = pattern[p];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
    for (size_t t = 0; t < t_len; ++t) {
      curr[t] = kInvalid;  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
    }
    if (p == 0) {
      ScoreSubsequenceFirstRow<ComparePolicy>(text, pat, position_bonus, prev, t_len);
      continue;
    }
    int running_best_plain = kInvalid;
    int running_best_boundary = kInvalid;
    for (size_t t = 0; t < t_len; ++t) {
      // Admit s = t-2 into both running maxima before scoring t.
      if (t >= 2 && prev[t - 2] != kInvalid) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index) - StringSearch hot-path invariant
        const int plain_cand = prev[t - 2] + static_cast<int>(  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
            (t - 2) * static_cast<size_t>(kFuzzyGapPenaltyPerChar));
        running_best_plain = (std::max)(running_best_plain, plain_cand);
        const int boundary_cand = prev[t - 2] + static_cast<int>(  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
            (t - 2) * static_cast<size_t>(kFuzzyGapToBoundaryPenaltyPerChar));
        running_best_boundary = (std::max)(running_best_boundary, boundary_cand);
      }
      if (!ComparePolicy::Equal(text[t], pat)) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
        continue;
      }
      int best_prev = kInvalid;
      if (t > 0 && prev[t - 1] != kInvalid) {  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
        best_prev = prev[t - 1] + kFuzzyConsecutiveBonus;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index) - StringSearch hot-path invariant
      }
      // Gapped predecessor uses the boundary gap rate when landing on a
      // boundary, the plain rate otherwise.
      best_prev = (std::max)(best_prev, BestGappedPredecessor(running_best_boundary, running_best_plain,
                                                                 t > 0 && IsWordBoundary(text, t), t, kInvalid));
      if (best_prev != kInvalid) {
        curr[t] = best_prev + position_bonus(t, pat);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      }
    }
    for (size_t t = 0; t < t_len; ++t) {
      prev[t] = curr[t];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
    }
  }

  int raw_score = kInvalid;
  for (size_t t = 0; t < t_len; ++t) {
    raw_score = (std::max)(raw_score, prev[t]);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
  }
  if (raw_score == kInvalid) {
    return std::nullopt;
  }

  // Length penalty & coverage bonus: -1 per excess char beyond a small free
  // allowance (cap 60) instead of v1's -0.5 with no allowance, so a 50-char
  // bloated wrapper scores ~50 points below a compact match while normal
  // filenames (<= 10 excess chars) are not punished for being descriptive.
  if (text.length() > pattern.length()) {
    const size_t excess = text.length() - pattern.length();
    const size_t billable =
        excess > kFuzzyExcessFreeChars ? excess - kFuzzyExcessFreeChars : 0;
    raw_score -= static_cast<int>((std::min)(billable, kFuzzyMaxExcessPenaltyChars) *
                                  static_cast<size_t>(kFuzzyExcessPenaltyPerChar));
    const float coverage_ratio = static_cast<float>(pattern.length()) / static_cast<float>(text.length());
    raw_score += static_cast<int>(coverage_ratio * kFuzzyMaxCoverageBonus);
  }

  return static_cast<uint16_t>((std::min)(899, (std::max)(400, raw_score)));  // NOLINT(readability-magic-numbers) - StringSearch hot-path invariant
}

constexpr size_t kMinTypoPatternLen = 3;
constexpr size_t kTypoK2MinPatternLen = 6;
constexpr size_t kTypoWindowMaxScanLen = 256;
// Banded-DP working set: patterns are capped here so the stack rows stay
// small (whole-name k=1 keeps its unbounded single-pass fast path).
constexpr size_t kTypoMaxDPDim = kFuzzyScoringMaxPatternLen + 3;

// Edit budget scales with query length: short patterns stay at k=1 to remain
// selective, longer ones (|P| >= 6) escalate to k=2 after k=1 fails.
constexpr int TypoBudgetForPatternLen(size_t p_len) {
  return p_len >= kTypoK2MinPatternLen ? 2 : 1;
}

// Banded optimal-string-alignment distance <= max_edits (insertion, deletion,
// substitution, adjacent transposition). O(k*min(m,n)) time, 3 stack rows,
// early exit when the row minimum exceeds the budget. Exact for the
// threshold: any alignment leaving the ±k band costs more than k.
template<typename ComparePolicy>
// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Banded OSA DP kernel: interdependent transitions (insert/delete/substitute/transpose) with early exit; splitting would obscure the recurrence
inline bool EditDistanceLEInternal(std::string_view pattern, std::string_view window, int max_edits) {  // NOSONAR(cpp:S3776) - Same rationale as above
  const size_t m = pattern.length();
  const size_t n = window.length();
  if (m > n + static_cast<size_t>(max_edits) || n > m + static_cast<size_t>(max_edits)) {
    return false;
  }
  if (m + 1 > kTypoMaxDPDim || n + 1 > kTypoMaxDPDim) {
    return false;
  }
  if (max_edits <= 1) {
    return DamerauLevenshteinDistance1Internal<ComparePolicy>(pattern, window);
  }
  static constexpr int kInf = 1000000;
  const int k = max_edits;
  const auto nn = static_cast<int>(n);
  const auto mm = static_cast<int>(m);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init) - every row fully written by init loops below before any read; value-init would add redundant stores per window
  std::array<std::array<int, kTypoMaxDPDim>, 3> rows;
  for (size_t j = 0; j <= n; ++j) {
    rows[0][j] = (static_cast<int>(j) <= k) ? static_cast<int>(j) : kInf;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index) - StringSearch hot-path invariant
  }
  for (int r = 1; r < 3; ++r) {
    for (size_t j = 0; j <= n; ++j) {
      rows[r][j] = kInf;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index) - StringSearch hot-path invariant
    }
  }
  for (int i = 1; i <= mm; ++i) {
    auto& curr = rows[i % 3];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
    const auto& prev = rows[(i + 2) % 3];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index) - StringSearch hot-path invariant
    const auto& prev2 = rows[(i + 1) % 3];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
    for (int j = 0; j <= nn; ++j) {
      curr[j] = kInf;  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
    }
    curr[0] = (i <= k) ? i : kInf;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
    int row_min = curr[0];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
    const int j_lo = (i - k > 1) ? i - k : 1;
    const int j_hi = (i + k < nn) ? i + k : nn;
    for (int j = j_lo; j <= j_hi; ++j) {
      const char pc = pattern[static_cast<size_t>(i - 1)];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      const char wc = window[static_cast<size_t>(j - 1)];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      const int sub_cost = ComparePolicy::Equal(pc, wc) ? 0 : 1;
      int best = prev[j] + 1;  // Deletion from pattern  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      best = (std::min)(best, curr[j - 1] + 1);  // Insertion into pattern  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      best = (std::min)(best, prev[j - 1] + sub_cost);  // Substitution or match  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      if (i > 1 && j > 1 &&
          ComparePolicy::Equal(pattern[static_cast<size_t>(i - 1)], window[static_cast<size_t>(j - 2)]) &&  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
          ComparePolicy::Equal(pattern[static_cast<size_t>(i - 2)], window[static_cast<size_t>(j - 1)])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
        // Adjacent transposition
        best = (std::min)(best, prev2[j - 2] + 1);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      }
      curr[j] = best;  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      row_min = (std::min)(row_min, best);
    }
    if (row_min > k) {
      return false;
    }
  }
  return rows[mm % 3][nn] <= k;  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
}

struct TypoWindowHit {
  size_t start = 0;
  size_t length = 0;
};

// N-gram presence prefilter for typo window scans.
// Soundness: ≤budget edits displace or destroy at most 2*budget pattern
// characters (adjacent transposition is the worst case at 2), leaving at
// least |P|-2*budget exact characters in at most budget+1 runs. The longest
// run has length ≥ ceil((|P|-2*budget)/(budget+1)) and appears verbatim in
// any matching window. Contrapositive: no such run in the scanned region ⟹
// no window can match. Case folding only over-accepts (folded runs are a
// superset of byte-exact runs), and hash collisions only over-accept, so a
// rejection is valid under every ComparePolicy and layout: it never drops a
// true match. Hashed (not exact) tables so non-alpha query bytes
// (digits, '_', '.', common in filenames) are checked uniformly instead of
// forcing a conservative accept.
inline size_t Fold5Hash(char c) {
  return static_cast<size_t>(ToLowerChar(static_cast<unsigned char>(c))) & 31u;  // NOLINT(readability-magic-numbers) - StringSearch hot-path invariant
}
inline size_t Fold4Hash(char c) {
  return static_cast<size_t>(ToLowerChar(static_cast<unsigned char>(c))) & 15u;  // NOLINT(readability-magic-numbers) - StringSearch hot-path invariant
}
// Requires one folded bigram of pattern in region. Sound when the
// longest-run bound is ≥ 2 (k=1 needs |P| ≥ 5, k=2 needs |P| ≥ 8).
inline bool TypoBigramGate(std::string_view pattern, std::string_view region) {
  if (region.length() < 2) {
    return true;
  }
  std::array<std::array<bool, 32>, 32> seen{};
  for (size_t i = 0; i + 1 < region.length(); ++i) {
    seen[Fold5Hash(region[i])][Fold5Hash(region[i + 1])] = true;  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
  }
  for (size_t i = 0; i + 1 < pattern.length(); ++i) {
    if (seen[Fold5Hash(pattern[i])][Fold5Hash(pattern[i + 1])]) {  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      return true;
    }
  }
  return false;
}
// Requires one folded trigram of pattern in region. Sound when the
// longest-run bound is ≥ 3 (k=2 with |P| ≥ 11).
inline bool TypoTrigramGate(std::string_view pattern, std::string_view region) {
  if (region.length() < 3) {
    return true;
  }
  std::array<std::array<std::array<bool, 16>, 16>, 16> seen{};
  for (size_t i = 0; i + 2 < region.length(); ++i) {
    seen[Fold4Hash(region[i])][Fold4Hash(region[i + 1])][Fold4Hash(region[i + 2])] = true;  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
  }
  for (size_t i = 0; i + 2 < pattern.length(); ++i) {
    if (seen[Fold4Hash(pattern[i])][Fold4Hash(pattern[i + 1])][Fold4Hash(pattern[i + 2])]) {  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      return true;
    }
  }
  return false;
}

// Multiset fallback for short patterns where no exact run is provable
// (k=1 with |P| ≤ 4, k=2 with |P| ≤ 7): a matching window shares at least
// |P|-budget folded bytes with the pattern — transpositions preserve the
// multiset, every other edit destroys at most one shared byte. Weaker than
// the N-gram gates but sound everywhere.
inline int SharedFoldedBytes(std::string_view pattern, std::string_view region, int need) {
  std::array<uint16_t, 256> freq{};
  for (const char c : region) {
    ++freq[static_cast<unsigned char>(ToLowerChar(static_cast<unsigned char>(c)))];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
  }
  int shared = 0;
  for (const char c : pattern) {
    if (const auto key = static_cast<unsigned char>(ToLowerChar(static_cast<unsigned char>(c)));
        freq[key] > 0) {  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      --freq[key];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index) - StringSearch hot-path invariant
      if (++shared >= need) {
        return shared;
      }
    }
  }
  return shared;
}

// Tiered gate dispatch: strongest sound requirement first.
inline bool TypoWindowGate(std::string_view pattern, std::string_view region, int max_edits) {
  const auto p_len = static_cast<int>(pattern.length());
  // Transpose-aware longest-run bound; callers guarantee p_len > 2*max_edits
  // (k=1 needs |P| ≥ 3, k=2 escalation needs |P| ≥ 6).
  const int run = (p_len - (2 * max_edits) + max_edits) / (max_edits + 1);
  if (run >= 3) {
    return TypoTrigramGate(pattern, region);
  }
  if (run == 2) {
    return TypoBigramGate(pattern, region);
  }
  if (max_edits >= 2 && p_len >= 6) {  // NOLINT(readability-magic-numbers) - StringSearch hot-path invariant
    // |P| = 6..7: a bare bigram is unsound (two transpositions can scatter
    // every pair), but bigram-free ∧ ≤2 edits then forces multiset ≥ |P|-1:
    // max run ≤ 1 over ≤ 3 runs caps exact chars at 3, so ≥ |P|-3 ≥ 3 chars
    // are destroyed, which with ≤ 2 edits is only possible as 2×transpose
    // (multiset intact) or transpose+sub/indel (multiset |P|-1).
    return TypoBigramGate(pattern, region) ||
           SharedFoldedBytes(pattern, region, p_len - 1) >= p_len - 1;
  }
  return SharedFoldedBytes(pattern, region, p_len - max_edits) >= p_len - max_edits;
}

// Matching-prefix credit shared by whole-name and window typo scoring:
// +15 for each of the first two pattern characters matching in place.
template<typename ComparePolicy>
inline int TypoWindowPrefixCredit(std::string_view pattern, std::string_view window) {
  size_t prefix_len = 0;
  while (prefix_len < pattern.length() && prefix_len < window.length() &&
         ComparePolicy::Equal(pattern[prefix_len], window[prefix_len])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
    ++prefix_len;
  }
  int credit = 0;
  if (prefix_len >= 1) {
    credit += 15;  // NOLINT(readability-magic-numbers) - StringSearch hot-path invariant
  }
  if (prefix_len >= 2) {
    credit += 15;  // NOLINT(readability-magic-numbers) - StringSearch hot-path invariant
  }
  return credit;
}

// Later windows score lower; boundary-anchored windows decay slower because
// acronym-style placement is deliberate while mid-word placement is accidental.
inline int TypoWindowPositionPenalty(std::string_view text, size_t start) {
  if (start == 0) {
    return 0;
  }
  if (IsWordBoundary(text, start)) {
    return (std::min)(30, static_cast<int>(start * 2));  // NOLINT(readability-magic-numbers) - StringSearch hot-path invariant
  }
  return (std::min)(50, static_cast<int>(start * 4));  // NOLINT(readability-magic-numbers) - StringSearch hot-path invariant
}

// Substitution price in halves: 0 for policy-equal, 1 for adjacent-key
// fat-fingers, 2 otherwise (case-only differences keep full cost, matching
// the pre-item-3 flat behavior for case-sensitive typos).
template<typename ComparePolicy>
inline int TypoSubCostHalves(char pat_char, char win_char, KeyboardLayout layout) {
  if (ComparePolicy::Equal(pat_char, win_char)) {
    return 0;
  }
  const char lowered_pat = ToLowerChar(static_cast<unsigned char>(pat_char));
  const char lowered_win = ToLowerChar(static_cast<unsigned char>(win_char));
  if (lowered_pat == lowered_win) {
    return 2;
  }
  return AreAdjacentKeys(lowered_pat, lowered_win, layout) ? 1 : 2;
}

// Halves-cost of a single-edit (k=1) alignment. Callers must guarantee the
// pair is within one true edit (accepted by DamerauLevenshteinDistance1):
// the walk prices the one differing site and needs no budget parameter.
template<typename ComparePolicy>
inline int SingleEditCostHalves(std::string_view pattern, std::string_view window,
                                KeyboardLayout layout) {
  size_t i = 0;
  size_t j = 0;
  const size_t m = pattern.length();
  const size_t n = window.length();
  while (i < m && j < n) {
    if (ComparePolicy::Equal(pattern[i], window[j])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      ++i;
      ++j;
      continue;
    }
    if (i + 1 < m && j + 1 < n &&
        ComparePolicy::Equal(pattern[i], window[j + 1]) &&  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
        ComparePolicy::Equal(pattern[i + 1], window[j])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      return 1;  // Adjacent transposition
    }
    if (m == n) {
      return TypoSubCostHalves<ComparePolicy>(pattern[i], window[j], layout);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
    }
    return 2;  // Insertion or deletion
  }
  if (i < m || j < n) {
    return 2;  // Trailing insertion/deletion (exact is unreachable here)
  }
  return 0;
}

// Banded halves-cost edit distance (insertion/deletion 2, substitution 1-2
// via TypoSubCostHalves, transposition 1). Scoring-only: predicates stay
// count-based, so this never admits a match — it only prices accepted ones.
// O(budget*min(m,n)) time, 3 stack rows, early exit past the budget.
template<typename ComparePolicy>
// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Banded halves-cost DP kernel; same rationale as EditDistanceLEInternal
// NOSONAR(cpp:S3776) - Banded halves-cost DP kernel; same rationale as EditDistanceLEInternal
inline int EditCostHalvesLEInternal(std::string_view pattern, std::string_view window,  // NOLINT(readability-function-cognitive-complexity) - StringSearch hot-path invariant
                                    int budget_halves, KeyboardLayout layout) {  // NOSONAR(cpp:S3776) - Same rationale as above
  const size_t m = pattern.length();
  const size_t n = window.length();
  const int over = budget_halves + 1;
  if (static_cast<int>(m > n ? m - n : n - m) * 2 > budget_halves) {
    return over;
  }
  if (m + 1 > kTypoMaxDPDim || n + 1 > kTypoMaxDPDim) {
    return over;
  }
  static constexpr int kInf = 1000000;
  const int band = budget_halves / 2;
  const auto nn = static_cast<int>(n);
  const auto mm = static_cast<int>(m);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init) - every row fully written by init loops below before any read; value-init would add redundant stores per window
  std::array<std::array<int, kTypoMaxDPDim>, 3> rows;
  for (size_t j = 0; j <= n; ++j) {
    rows[0][j] = (static_cast<int>(j) * 2 <= budget_halves) ? static_cast<int>(j) * 2 : kInf;  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
  }
  for (int r = 1; r < 3; ++r) {
    for (size_t j = 0; j <= n; ++j) {
      rows[r][j] = kInf;  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
    }
  }
  for (int i = 1; i <= mm; ++i) {
    auto& curr = rows[i % 3];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
    const auto& prev = rows[(i + 2) % 3];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
    const auto& prev2 = rows[(i + 1) % 3];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index) - StringSearch hot-path invariant
    for (int j = 0; j <= nn; ++j) {
      curr[j] = kInf;  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
    }
    curr[0] = (i * 2 <= budget_halves) ? i * 2 : kInf;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
    int row_min = curr[0];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
    const int j_lo = (i - band > 1) ? i - band : 1;
    const int j_hi = (i + band < nn) ? i + band : nn;
    for (int j = j_lo; j <= j_hi; ++j) {
      int best = prev[j] + 2;  // Deletion from pattern  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      best = (std::min)(best, curr[j - 1] + 2);  // Insertion into pattern  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      const int sub_cost = TypoSubCostHalves<ComparePolicy>(pattern[static_cast<size_t>(i - 1)],  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
                                                              window[static_cast<size_t>(j - 1)],  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
                                                              layout);
      best = (std::min)(best, prev[j - 1] + sub_cost);  // Substitution or match  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      if (i > 1 && j > 1 &&
          ComparePolicy::Equal(pattern[static_cast<size_t>(i - 1)], window[static_cast<size_t>(j - 2)]) &&  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
          ComparePolicy::Equal(pattern[static_cast<size_t>(i - 2)], window[static_cast<size_t>(j - 1)])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
        // Adjacent transposition
        best = (std::min)(best, prev2[j - 2] + 1);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index) - StringSearch hot-path invariant
      }
      curr[j] = best;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index) - StringSearch hot-path invariant
      row_min = (std::min)(row_min, best);
    }
    if (row_min > budget_halves) {
      return over;
    }
  }
  const int distance = rows[mm % 3][nn];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
  return distance <= budget_halves ? distance : over;
}

// Window lengths to try, longest-match first (substitution/transposition at
// |P|, then ±1, then ±2 for k=2 only).
struct TypoWindowPlan {
  std::array<size_t, 5> lengths{};  // NOLINT(readability-magic-numbers) - StringSearch hot-path invariant
  int count = 0;
};

[[nodiscard]] inline TypoWindowPlan MakeTypoWindowPlan(size_t p_len, int max_edits) {
  TypoWindowPlan plan;
  plan.lengths = {p_len, p_len + 1, p_len > 0 ? p_len - 1 : 0,
                  p_len + 2, p_len > 1 ? p_len - 2 : 0,};
  plan.count = max_edits >= 2 ? 5 : 3;  // NOLINT(readability-magic-numbers) - StringSearch hot-path invariant
  return {plan};
}

// Scores one window (nullopt when beyond budget): 250 - 25*halves-cost +
// prefix credit - position penalty. Scoring-only: callers first admit the
// window with the count-based predicate, so this never accepts a match.
template<typename ComparePolicy>
[[nodiscard]] inline std::optional<int> ScoreTypoWindow(std::string_view pattern, std::string_view text,
                                                       size_t start, size_t win_len, int max_edits,
                                                       KeyboardLayout layout) {
  const std::string_view window = text.substr(start, win_len);
  int cost_halves = 2;
  if (max_edits <= 1) {
    if (!DamerauLevenshteinDistance1Internal<ComparePolicy>(pattern, window)) {
      return std::nullopt;
    }
    cost_halves = SingleEditCostHalves<ComparePolicy>(pattern, window, layout);
  } else {
    if (!EditDistanceLEInternal<ComparePolicy>(pattern, window, max_edits)) {
      return std::nullopt;
    }
    cost_halves = EditCostHalvesLEInternal<ComparePolicy>(pattern, window, kTypoK2BudgetHalves, layout);
    if (cost_halves > kTypoK2BudgetHalves) {
      return std::nullopt;
    }
  }
  const int score = kTypoBaseScore - (kTypoCostPerHalfEdit * cost_halves) +
                    TypoWindowPrefixCredit<ComparePolicy>(pattern, window) -
                    TypoWindowPositionPenalty(text, start);
  return {score};
}

// First matching window (filter fast path with early exit).
template<typename ComparePolicy>
[[nodiscard]] inline std::optional<TypoWindowHit> FindFirstTypoWindow(
    std::string_view pattern, std::string_view text, size_t scan_len, const TypoWindowPlan& plan,
    int max_edits) {
  for (int li = 0; li < plan.count; ++li) {
    const size_t win_len = plan.lengths[static_cast<size_t>(li)];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
    if (win_len == 0 || win_len > scan_len) {
      continue;
    }
    for (size_t s = 0; s + win_len <= scan_len; ++s) {
      const std::string_view window = text.substr(s, win_len);
      bool hit = false;
      if (max_edits <= 1) {
        hit = DamerauLevenshteinDistance1Internal<ComparePolicy>(pattern, window);
      } else {
        hit = EditDistanceLEInternal<ComparePolicy>(pattern, window, max_edits);
      }
      if (hit) {
        return {TypoWindowHit{s, win_len}};
      }
    }
  }
  return std::nullopt;
}

struct TypoWindowResult {
  TypoWindowHit hit{};
  int score = 0;
};

// Best-scoring window (ties go to the earliest start).
template<typename ComparePolicy>
[[nodiscard]] inline std::optional<TypoWindowResult> FindBestTypoWindow(
    std::string_view pattern, std::string_view text, size_t scan_len, const TypoWindowPlan& plan,
    int max_edits, KeyboardLayout layout) {
  TypoWindowResult best{};
  bool found = false;
  for (int li = 0; li < plan.count; ++li) {
    const size_t win_len = plan.lengths[static_cast<size_t>(li)];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
    if (win_len == 0 || win_len > scan_len) {
      continue;
    }
    for (size_t s = 0; s + win_len <= scan_len; ++s) {
      if (const auto score =
              ScoreTypoWindow<ComparePolicy>(pattern, text, s, win_len, max_edits, layout);
          score.has_value() && *score > best.score) {
        best.hit.start = s;
        best.hit.length = win_len;
        best.score = *score;
        found = true;
      }
    }
  }
  if (found) {
    return {best};
  }
  return std::nullopt;
}

// Single-edit (k=1) or double-edit (k=2, patterns >= 6 chars) match of pattern
// against sliding windows over the name. Window lengths span |P|±max_edits
// (two deletions/insertions need P∓2 windows). With find_best, scans all
// windows and returns the highest-scoring one (ties → earliest); otherwise
// returns on the first hit for filter-path early exit. Scoring prices each
// hit in halves (transposition / fat-finger 1, else 2); the boolean filter
// outcome is layout-independent, only the rank moves with the layout.
template<typename ComparePolicy>
inline bool FindTypoWindowInternal(std::string_view pattern, std::string_view text,
                                   TypoWindowHit* out_hit, bool find_best, int max_edits,
                                   KeyboardLayout layout = KeyboardLayout::Union) {
  if (pattern.length() < kMinTypoPatternLen || text.empty() ||
      pattern.length() > text.length() + static_cast<size_t>(max_edits)) {
    return false;
  }
  if (max_edits >= 2 && pattern.length() > kFuzzyScoringMaxPatternLen) {
    return false;
  }
  const size_t scan_len = (std::min)(text.length(), kTypoWindowMaxScanLen);
  // Tiered necessary-condition prefilter over the same capped region the
  // windows come from (strongest sound tier first).
  if (!TypoWindowGate(pattern, text.substr(0, scan_len), max_edits)) {
    return false;
  }
  const auto plan = MakeTypoWindowPlan(pattern.length(), max_edits);
  if (!find_best) {
    if (const auto hit =
            FindFirstTypoWindow<ComparePolicy>(pattern, text, scan_len, plan, max_edits);
        hit.has_value()) {
      if (out_hit != nullptr) {
        *out_hit = *hit;
      }
      return true;
    }
    return false;
  }
  if (const auto result =
          FindBestTypoWindow<ComparePolicy>(pattern, text, scan_len, plan, max_edits, layout);
      result.has_value()) {
    if (out_hit != nullptr) {
      *out_hit = result->hit;
    }
    return true;
  }
  return false;
}

// Boolean typo predicate for the worker filter path: whole-name k=1 fast
// path, then k=1 substring windows with early exit, then (patterns >= 6
// chars) k=2 windows with early exit.
template<typename ComparePolicy>
inline bool FuzzyTypoMatchInternal(std::string_view text, std::string_view pattern) {
  if (pattern.length() < kMinTypoPatternLen) {
    return false;
  }
  if (std::abs(static_cast<int>(pattern.length()) - static_cast<int>(text.length())) <= 1 &&
      DamerauLevenshteinDistance1Internal<ComparePolicy>(pattern, text)) {
    return true;
  }
  if (FindTypoWindowInternal<ComparePolicy>(pattern, text, nullptr, false, 1)) {
    return true;
  }
  if (TypoBudgetForPatternLen(pattern.length()) >= 2) {
    return FindTypoWindowInternal<ComparePolicy>(pattern, text, nullptr, false, 2);
  }
  return false;
}

template<typename ComparePolicy>
inline std::optional<uint16_t> EvaluateTypoScore(std::string_view text, std::string_view pattern,
                                                 KeyboardLayout layout) {
  if (pattern.length() < kMinTypoPatternLen) {
    return std::nullopt;
  }
  // Fast path: whole-basename single edit (O(P)).
  if (std::abs(static_cast<int>(pattern.length()) - static_cast<int>(text.length())) <= 1 &&
      DamerauLevenshteinDistance1Internal<ComparePolicy>(pattern, text)) {
    const int cost_halves = SingleEditCostHalves<ComparePolicy>(pattern, text, layout);
    const int typo_score = kTypoBaseScore - (kTypoCostPerHalfEdit * cost_halves) +
                           TypoWindowPrefixCredit<ComparePolicy>(pattern, text);
    return static_cast<uint16_t>((std::min)(300, (std::max)(100, typo_score)));  // NOLINT(readability-magic-numbers) - StringSearch hot-path invariant
  }
  // Fallback: single edit inside a window over longer basenames, so
  // "finq" matches "find_helper.cpp" and not just the bare name "find".
  TypoWindowHit hit{};
  if (FindTypoWindowInternal<ComparePolicy>(pattern, text, &hit, true, 1, layout)) {
    const std::string_view window = text.substr(hit.start, hit.length);
    const int cost_halves = SingleEditCostHalves<ComparePolicy>(pattern, window, layout);
    const int typo_score = kTypoBaseScore - (kTypoCostPerHalfEdit * cost_halves) +
                           TypoWindowPrefixCredit<ComparePolicy>(pattern, window) -
                           TypoWindowPositionPenalty(text, hit.start);
    return static_cast<uint16_t>((std::min)(300, (std::max)(100, typo_score)));  // NOLINT(readability-magic-numbers) - StringSearch hot-path invariant
  }
  // Escalation: two edits for patterns >= 6 chars (e.g. "Cotnroler" matching
  // "Controller": transposition + deleted letter). The window search already
  // prices each hit, so recompute the winning window's halves-cost here.
  if (TypoBudgetForPatternLen(pattern.length()) >= 2 &&
      FindTypoWindowInternal<ComparePolicy>(pattern, text, &hit, true, 2, layout)) {
    const std::string_view window = text.substr(hit.start, hit.length);
    const int cost_halves =
        EditCostHalvesLEInternal<ComparePolicy>(pattern, window, kTypoK2BudgetHalves, layout);
    const int typo_score = kTypoBaseScore - (kTypoCostPerHalfEdit * cost_halves) +
                           TypoWindowPrefixCredit<ComparePolicy>(pattern, window) -
                           TypoWindowPositionPenalty(text, hit.start);
    return static_cast<uint16_t>((std::min)(300, (std::max)(100, typo_score)));  // NOLINT(readability-magic-numbers) - StringSearch hot-path invariant
  }
  return std::nullopt;
}

// Compute relevance score in [0, 1000]
template<typename ComparePolicy>
inline uint16_t ComputeFuzzyScoreInternal(std::string_view text, std::string_view pattern,
                                          KeyboardLayout layout) {
  if (pattern.empty()) {
    return 1000;  // NOLINT(readability-magic-numbers) - StringSearch hot-path invariant
  }
  if (text.empty()) {
    return 0;
  }

  // Exact match
  if (text.length() == pattern.length() && CheckPrefix<ComparePolicy>(text, pattern)) {
    return 1000;  // NOLINT(readability-magic-numbers) - StringSearch hot-path invariant
  }

  // Exact prefix match
  if (CheckPrefix<ComparePolicy>(text, pattern)) {
    const auto len_diff = static_cast<int>(text.length() - pattern.length());
    const int bonus = (std::min)(50, (std::max)(0, 50 - len_diff));
    return static_cast<uint16_t>(900 + bonus);  // NOLINT(readability-magic-numbers) - StringSearch hot-path invariant
  }

  // Subsequence match evaluation (forward single-pass)
  if (const auto subseq_score = EvaluateSubsequenceScore<ComparePolicy>(text, pattern); subseq_score.has_value()) {
    return *subseq_score;
  }

  // Typo match evaluation (Damerau-Levenshtein k <= 1, k <= 2 for |P| >= 6)
  if (const auto typo_score = EvaluateTypoScore<ComparePolicy>(text, pattern, layout);
      typo_score.has_value()) {
    return *typo_score;
  }

  return 0;
}

} // namespace string_search_detail

[[nodiscard]] inline SimdSubstringHints MakeSimdSubstringHints(const std::string_view pattern) {  // NOLINT(readability-identifier-naming) - param name; not a constant
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
  #if STRING_SEARCH_AVX2_AVAILABLE
  if (const auto avx2_result = string_search_detail::TryAVX2PathCaseSensitive(text, hints, [&text, &pattern] {
    return string_search::avx2::ContainsSubstringAVX2(text, pattern);
  }); avx2_result.has_value()) {
    return *avx2_result;  // AVX2 ran — trust result (both true and false), skip scalar
  }
  #endif  // STRING_SEARCH_AVX2_AVAILABLE

  // NEON optimization: arm64 (Apple Silicon, ARM64 Linux/Windows)
  #if STRING_SEARCH_NEON_AVAILABLE
  if (const auto neon_result = string_search_detail::TryNEONPathCaseSensitive(text, hints, [&text, &pattern] {
    return string_search::neon::ContainsSubstringNEON(text, pattern);
  }); neon_result.has_value()) {
    return *neon_result;  // NEON ran — trust result (both true and false), skip scalar
  }
  #endif  // STRING_SEARCH_NEON_AVAILABLE

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

  if (const auto avx2_result = string_search_detail::TryAVX2PathStrStr(haystack, needle, haystack_len, needle_len, [&haystack, &needle] {
    return string_search::avx2::StrStrCaseInsensitiveAVX2(haystack, needle);
  }); avx2_result.has_value()) {
    return *avx2_result;  // AVX2 ran — trust result (match pointer or nullptr), skip scalar
  }
  #elif STRING_SEARCH_NEON_AVAILABLE
  const size_t haystack_len = strlen(haystack);  // NOSONAR(cpp:S1081) - Safe: null-terminated C-string contract
  const size_t needle_len   = strlen(needle);    // NOSONAR(cpp:S1081) - Safe: null-terminated C-string contract

  if (const auto neon_result = string_search_detail::TryNEONPathStrStr(haystack, needle, haystack_len, needle_len, [&haystack, &needle] {
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

  // Fast path: Check if pattern is a prefix (very common case)
  if (string_search_detail::CheckPrefix<string_search_detail::CaseInsensitive>(text, pattern)) {
    return true;
  }

  // AVX2 optimization: Use for longer strings
  #if STRING_SEARCH_AVX2_AVAILABLE
  if (const auto avx2_result = string_search_detail::TryAVX2PathCaseInsensitive(text, hints, [&text, &pattern] {
    return string_search::avx2::ContainsSubstringIAVX2(text, pattern);
  }); avx2_result.has_value()) {
    return *avx2_result;  // AVX2 ran — trust result (both true and false), skip scalar
  }
  #endif  // STRING_SEARCH_AVX2_AVAILABLE

  // NEON optimization: arm64 (Apple Silicon, ARM64 Linux/Windows)
  #if STRING_SEARCH_NEON_AVAILABLE
  if (const auto neon_result = string_search_detail::TryNEONPathCaseInsensitive(text, hints, [&text, &pattern] {
    return string_search::neon::ContainsSubstringINEON(text, pattern);
  }); neon_result.has_value()) {
    return *neon_result;  // NEON ran — trust result (both true and false), skip scalar
  }
  #endif  // STRING_SEARCH_NEON_AVAILABLE

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
/**
 * @brief Bounded Damerau-Levenshtein distance <= 1 check with O(1) length prefiltering.
 *
 * Checks if 'pattern' matches 'text' with at most 1 insertion, deletion, substitution,
 * or adjacent character transposition.
 *
 * @param pattern The search query pattern
 * @param text The candidate text (e.g. filename)
 * @param case_sensitive Whether comparison is case-sensitive
 * @return true if distance <= 1
 */
inline bool DamerauLevenshteinDistance1(std::string_view pattern, std::string_view text, bool case_sensitive = false) {
  if (case_sensitive) {
    return string_search_detail::DamerauLevenshteinDistance1Internal<string_search_detail::CaseSensitive>(pattern, text);
  }
  return string_search_detail::DamerauLevenshteinDistance1Internal<string_search_detail::CaseInsensitive>(pattern, text);
}

/**
 * @brief Computes a normalized relevance score in [0, 1000] for fuzzy search.
 *
 * Bands:
 * - Exact / Prefix match: [900, 1000]
 * - Subsequence match: [400, 899]
 * - Typo match: [100, 300] (250 - 25 per half-edit + prefix/position terms;
 *   transpositions and adjacent-key fat-fingers outrank random substitutions)
 * - Non-match: 0
 *
 * @param text The candidate text (e.g. filename)
 * @param pattern The search query pattern
 * @param case_sensitive Whether comparison is case-sensitive
 * @param layout Physical keyboard layout for typo pricing (Union default)
 * @return Relevance score in [0, 1000]
 */
inline uint16_t ComputeFuzzyScore(std::string_view text, std::string_view pattern, bool case_sensitive = false,
                                  KeyboardLayout layout = KeyboardLayout::Union) {
  if (case_sensitive) {
    return string_search_detail::ComputeFuzzyScoreInternal<string_search_detail::CaseSensitive>(text, pattern,
                                                                                                layout);
  }
  return string_search_detail::ComputeFuzzyScoreInternal<string_search_detail::CaseInsensitive>(text, pattern,
                                                                                                layout);
}

/**
 * @brief Fuzzy matching (subsequence matching with optional typo tolerance)
 *
 * Returns true if all characters in 'pattern' appear in 'text' in the same order,
 * or (if allow_typos is true and pattern.length() >= 3) typo tolerance matches:
 * a single edit against the whole text or any |pattern|±1 window of it, escalating
 * to two edits (patterns >= 6 chars) against |pattern|±2 windows.
 * Recall is layout-independent (pure edit counts); the physical layout only
 * affects relevance ranking via ComputeFuzzyScore.
 *
 * @param text The string to search in
 * @param pattern The sequence of characters to look for
 * @param allow_typos Whether to allow 1-edit typo tolerance on queries length >= 3
 * @return true if pattern matches
 */
inline bool FuzzyMatch(std::string_view text, std::string_view pattern, bool allow_typos = false) {
  if (string_search_detail::FuzzyMatchInternal<string_search_detail::CaseSensitive>(text, pattern)) {
    return true;
  }
  if (allow_typos && pattern.length() >= 3) {
    return string_search_detail::FuzzyTypoMatchInternal<string_search_detail::CaseSensitive>(text, pattern);
  }
  return false;
}

/**
 * @brief Case-insensitive fuzzy matching (subsequence matching with optional typo tolerance)
 *
 * Returns true if all characters in 'pattern' appear in 'text' in the same order ignoring case,
 * or (if allow_typos is true and pattern.length() >= 3) typo tolerance matches:
 * a single edit against the whole text or any |pattern|±1 window of it, escalating
 * to two edits (patterns >= 6 chars) against |pattern|±2 windows.
 * Recall is layout-independent (pure edit counts); the physical layout only
 * affects relevance ranking via ComputeFuzzyScore.
 *
 * @param text The string to search in
 * @param pattern The sequence of characters to look for
 * @param allow_typos Whether to allow 1-edit typo tolerance on queries length >= 3
 * @return true if pattern matches
 */
inline bool FuzzyMatchI(std::string_view text, std::string_view pattern, bool allow_typos = false) {
  if (string_search_detail::FuzzyMatchInternal<string_search_detail::CaseInsensitive>(text, pattern)) {
    return true;
  }
  if (allow_typos && pattern.length() >= 3) {
    return string_search_detail::FuzzyTypoMatchInternal<string_search_detail::CaseInsensitive>(text, pattern);
  }
  return false;
}

/**
 * @brief Byte range [start, end) into a haystack for match highlighting.
 *
 * Offsets are byte indices (not UTF-8 code points). Callers must only split on
 * boundaries produced by the matchers below (ASCII-safe for typical queries).
 */
struct MatchSpan {
  size_t start_ = 0;  // NOLINT(readability-identifier-naming) - public API uses trailing underscore
  size_t end_ = 0;    // NOLINT(readability-identifier-naming) - public API uses trailing underscore
};

/**
 * @brief Sort spans by start, then merge overlapping or adjacent ranges.
 *
 * Used when combining Name-field and Path-field highlights on the same string.
 */
inline void SortAndMergeSpans(std::vector<MatchSpan>& spans) {
  if (spans.size() < 2U) {
    return;
  }
  std::sort(spans.begin(), spans.end(),
            [](const MatchSpan& lhs, const MatchSpan& rhs) {
              if (lhs.start_ != rhs.start_) {
                return lhs.start_ < rhs.start_;
              }
              return lhs.end_ < rhs.end_;
            });
  size_t write = 0;
  for (size_t read = 1; read < spans.size(); ++read) {
    MatchSpan& cur = spans[write];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - retained suppression
    const MatchSpan& next = spans[read];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - retained suppression
    if (next.start_ <= cur.end_) {
      cur.end_ = (std::max)(cur.end_, next.end_);
    } else {
      ++write;
      spans[write] = next;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - retained suppression
    }
  }
  spans.resize(write + 1U);
}

namespace string_search_detail {

inline void MergeAdjacentSpans(std::vector<MatchSpan>& spans) {
  if (spans.size() < 2U) {
    return;
  }
  size_t write = 0;
  for (size_t read = 1; read < spans.size(); ++read) {
    MatchSpan& cur = spans[write];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - retained suppression
    const MatchSpan& next = spans[read];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - retained suppression
    if (cur.end_ == next.start_) {
      cur.end_ = next.end_;
    } else {
      ++write;
      spans[write] = next;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - retained suppression
    }
  }
  spans.resize(write + 1U);
}

template <typename ComparePolicy>
inline bool FuzzyMatchSpansInternal(std::string_view text, std::string_view pattern,
                                    std::vector<MatchSpan>& out_spans) {
  out_spans.clear();
  if (pattern.empty()) {
    return true;
  }
  if (text.empty()) {
    return false;
  }

  out_spans.reserve(pattern.size());

  size_t text_idx = 0;
  for (const char p : pattern) {
    while (text_idx < text.size() &&
           !ComparePolicy::Equal(text[text_idx], p)) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - retained suppression
      ++text_idx;
    }
    if (text_idx >= text.size()) {
      out_spans.clear();
      return false;
    }
    out_spans.push_back(MatchSpan{text_idx, text_idx + 1U});
    ++text_idx;
  }
  MergeAdjacentSpans(out_spans);
  return true;
}

// Aligned-character spans for a typo match between pattern and one window.
// Length comparison uses the window (not the full text): the edit decision
// must agree with the Damerau-Levenshtein check that admitted the window.
template <typename ComparePolicy>
inline void CollectTypoAlignmentSpans(std::string_view pattern, std::string_view window_text,
                                      size_t base_offset, std::vector<MatchSpan>& out_spans) {
  const auto p_len = static_cast<int>(pattern.length());
  const auto w_len = static_cast<int>(window_text.length());
  int i = 0;
  int j = 0;
  while (i < p_len && j < w_len) {
    if (ComparePolicy::Equal(pattern[static_cast<size_t>(i)], window_text[static_cast<size_t>(j)])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - StringSearch hot-path invariant
      out_spans.push_back(MatchSpan{base_offset + static_cast<size_t>(j),
                                    base_offset + static_cast<size_t>(j + 1),});
      ++i;
      ++j;
      continue;
    }
    // Check adjacent transposition
    if (i + 1 < p_len && j + 1 < w_len &&
        ComparePolicy::Equal(pattern[static_cast<size_t>(i)], window_text[static_cast<size_t>(j + 1)]) &&  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,bugprone-misplaced-widening-cast) - StringSearch hot-path invariant
        ComparePolicy::Equal(pattern[static_cast<size_t>(i + 1)], window_text[static_cast<size_t>(j)])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,bugprone-misplaced-widening-cast) - StringSearch hot-path invariant
      out_spans.push_back(MatchSpan{base_offset + static_cast<size_t>(j),
                                    base_offset + static_cast<size_t>(j + 2),});
      i += 2;
      j += 2;
      continue;
    }
    if (p_len > w_len) {
      ++i;
    } else if (p_len < w_len) {
      ++j;
    } else {
      ++i;
      ++j;
    }
  }
}

template <typename ComparePolicy>
inline bool FuzzyOrTypoMatchSpansInternal(std::string_view text, std::string_view pattern,
                                          std::vector<MatchSpan>& out_spans,
                                          KeyboardLayout layout = KeyboardLayout::Union) {
  if (FuzzyMatchSpansInternal<ComparePolicy>(text, pattern, out_spans)) {
    return true;
  }
  out_spans.clear();
  if (pattern.length() < kMinTypoPatternLen) {
    return false;
  }
  // Whole-text single edit (preserves v1 highlight behavior).
  if (std::abs(static_cast<int>(pattern.length()) - static_cast<int>(text.length())) <= 1 &&
      DamerauLevenshteinDistance1Internal<ComparePolicy>(pattern, text)) {
    CollectTypoAlignmentSpans<ComparePolicy>(pattern, text, 0, out_spans);
    MergeAdjacentSpans(out_spans);
    return !out_spans.empty();
  }
  // Substring-scoped single edit: highlight the best-scoring window.
  TypoWindowHit hit{};
  if (FindTypoWindowInternal<ComparePolicy>(pattern, text, &hit, true, 1, layout)) {
    CollectTypoAlignmentSpans<ComparePolicy>(pattern, text.substr(hit.start, hit.length), hit.start,
                                             out_spans);
    MergeAdjacentSpans(out_spans);
    return !out_spans.empty();
  }
  // Substring-scoped double edit (patterns >= 6 chars): same alignment
  // walk; spans stay plausible because every branch advances the cursors.
  if (TypoBudgetForPatternLen(pattern.length()) >= 2 &&
      FindTypoWindowInternal<ComparePolicy>(pattern, text, &hit, true, 2, layout)) {
    CollectTypoAlignmentSpans<ComparePolicy>(pattern, text.substr(hit.start, hit.length), hit.start,
                                             out_spans);
    MergeAdjacentSpans(out_spans);
    return !out_spans.empty();
  }
  return false;
}

}  // namespace string_search_detail

/**
 * @brief Find all non-overlapping substring matches as spans.
 *
 * Empty pattern → no spans (nothing to highlight). Clears out_spans on entry.
 * @return true if at least one match was found.
 */
inline bool FindSubstringSpans(std::string_view text, std::string_view pattern, bool case_sensitive,
                               std::vector<MatchSpan>& out_spans) {
  out_spans.clear();
  if (pattern.empty() || text.empty() || pattern.size() > text.size()) {
    return false;
  }

  constexpr size_t kMaxInitialSpans = 16;
  out_spans.reserve((std::min)(text.size() / pattern.size(), kMaxInitialSpans));

  if (case_sensitive) {
    size_t search_pos = 0;
    while (search_pos + pattern.size() <= text.size()) {
      const size_t pos = text.find(pattern, search_pos);
      if (pos == std::string_view::npos) {
        break;
      }
      out_spans.push_back(MatchSpan{pos, pos + pattern.size()});
      search_pos = pos + pattern.size();
    }
    return !out_spans.empty();
  }

  // Same advance pattern as the case-sensitive branch (avoid for-loop index surgery; cpp:S886).
  size_t search_pos = 0;
  while (search_pos + pattern.size() <= text.size()) {
    if (string_search_detail::CheckPrefix<string_search_detail::CaseInsensitive>(
            text.substr(search_pos), pattern)) {
      out_spans.push_back(MatchSpan{search_pos, search_pos + pattern.size()});
      search_pos += pattern.size();
    } else {
      ++search_pos;
    }
  }
  return !out_spans.empty();
}

/**
 * @brief Collect fuzzy (subsequence) match character spans; merges adjacent runs.
 * @return true if pattern is a subsequence of text (same contract as FuzzyMatch).
 */
inline bool FindFuzzySpans(std::string_view text, std::string_view pattern, bool case_sensitive,
                           std::vector<MatchSpan>& out_spans,
                           KeyboardLayout layout = KeyboardLayout::Union) {
  if (case_sensitive) {
    return string_search_detail::FuzzyOrTypoMatchSpansInternal<string_search_detail::CaseSensitive>(
        text, pattern, out_spans, layout);
  }
  return string_search_detail::FuzzyOrTypoMatchSpansInternal<string_search_detail::CaseInsensitive>(
      text, pattern, out_spans, layout);
}

/**
 * @brief Highlight literal runs in a glob pattern (* and ? are wildcards, not highlighted).
 *
 * Walks literal segments left-to-right, finding each in @p text after the previous match.
 * Returns true if at least one literal span was recorded. Patterns with only wildcards
 * yield no spans.
 */
inline bool FindGlobLiteralSpans(std::string_view text, std::string_view pattern,
                                 bool case_sensitive, std::vector<MatchSpan>& out_spans) {
  out_spans.clear();
  if (text.empty() || pattern.empty()) {
    return false;
  }

  out_spans.reserve((pattern.size() + 1U) / 2U);

  size_t text_pos = 0;
  size_t i = 0;
  std::vector<MatchSpan> one;
  one.reserve(1);
  while (i < pattern.size()) {
    while (i < pattern.size() && (pattern[i] == '*' || pattern[i] == '?')) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - retained suppression
      ++i;
    }
    const size_t lit_start = i;
    while (i < pattern.size() && pattern[i] != '*' && pattern[i] != '?') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - retained suppression
      ++i;
    }
    if (lit_start == i) {
      continue;
    }
    const std::string_view lit = pattern.substr(lit_start, i - lit_start);
    if (text_pos > text.size()) {
      break;
    }
    if (const std::string_view remaining = text.substr(text_pos);
        !FindSubstringSpans(remaining, lit, case_sensitive, one) || one.empty()) {
      continue;
    }
    const MatchSpan& hit = one.front();
    out_spans.push_back(MatchSpan{text_pos + hit.start_, text_pos + hit.end_});
    text_pos = text_pos + hit.end_;
  }

  string_search_detail::MergeAdjacentSpans(out_spans);
  return !out_spans.empty();
}

} // namespace string_search
