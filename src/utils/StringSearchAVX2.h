#pragma once

#include "utils/CpuFeatures.h"
#include "utils/StringSearch.h"

#if STRING_SEARCH_AVX2_AVAILABLE
#include <immintrin.h>  // AVX2 intrinsics

namespace string_search::avx2 {

// Convert 32 bytes to lowercase using AVX2
// Fast path: ASCII only (characters 0-127)
// Input: 32 bytes in __m256i register
// Output: 32 bytes converted to lowercase
// GCC/Clang: ensure AVX2 codegen when TU is not built with -mavx2 (Linux correctness).
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#endif  // defined(__GNUC__) || defined(__clang__)
inline __m256i ToLowerAVX2(__m256i input) {  // NOSONAR(cpp:S1238) - __m256i is SIMD register type, passed by value (correct for AVX2 intrinsics)
    // ASCII lowercase conversion: if char is in [A-Z] (0x41-0x5A), add 0x20
    // Create mask: input >= 'A' && input <= 'Z'
    __m256i is_upper = _mm256_and_si256(
        _mm256_cmpgt_epi8(input, _mm256_set1_epi8('A' - 1)),
        _mm256_cmpgt_epi8(_mm256_set1_epi8('Z' + 1), input)
    );

    // Add 0x20 to uppercase letters (convert to lowercase)
    __m256i lower = _mm256_add_epi8(input, _mm256_and_si256(
        _mm256_set1_epi8(0x20), is_upper
    ));

    return lower;
}

// Case-insensitive comparison of 32 bytes
// Returns true if all 32 bytes match (case-insensitive), false otherwise
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#endif  // defined(__GNUC__) || defined(__clang__)
inline bool Compare32CaseInsensitive(const char* a, const char* b) {
    // Load 32 bytes from each pointer (unaligned load)
    const auto* a_vec =
        reinterpret_cast<const __m256i*>(a);  // NOSONAR(cpp:S3630) - Required to feed 32-byte char buffer into AVX2 load intrinsic; alternatives (memcpy/bit_cast) add overhead in this hot path
    const auto* b_vec =
        reinterpret_cast<const __m256i*>(b);  // NOSONAR(cpp:S3630) - Required to feed 32-byte char buffer into AVX2 load intrinsic; alternatives (memcpy/bit_cast) add overhead in this hot path
    __m256i va = _mm256_loadu_si256(a_vec);
    __m256i vb = _mm256_loadu_si256(b_vec);

    // Convert both to lowercase
    __m256i lower_a = ToLowerAVX2(va);
    __m256i lower_b = ToLowerAVX2(vb);

    // Compare bytes (0xFF if equal, 0x00 if not)
    __m256i cmp = _mm256_cmpeq_epi8(lower_a, lower_b);

    // Extract comparison mask (1 bit per byte)
    int mask = _mm256_movemask_epi8(cmp);

    // All 32 bytes match if mask is 0xFFFFFFFF
    return mask == 0xFFFFFFFF;
}

// Case-sensitive comparison of 32 bytes
// Returns true if all 32 bytes match, false otherwise
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#endif  // defined(__GNUC__) || defined(__clang__)
inline bool Compare32(const char* a, const char* b) {
    // Load 32 bytes from each pointer (unaligned load)
    const auto* a_vec =
        reinterpret_cast<const __m256i*>(a);  // NOSONAR(cpp:S3630) - Required to feed 32-byte char buffer into AVX2 load intrinsic; alternatives (memcpy/bit_cast) add overhead in this hot path
    const auto* b_vec =
        reinterpret_cast<const __m256i*>(b);  // NOSONAR(cpp:S3630) - Required to feed 32-byte char buffer into AVX2 load intrinsic; alternatives (memcpy/bit_cast) add overhead in this hot path
    __m256i va = _mm256_loadu_si256(a_vec);
    __m256i vb = _mm256_loadu_si256(b_vec);

    // Compare bytes directly
    __m256i cmp = _mm256_cmpeq_epi8(va, vb);

    // Extract comparison mask
    int mask = _mm256_movemask_epi8(cmp);

    // All 32 bytes match if mask is 0xFFFFFFFF
    return mask == 0xFFFFFFFF;
}

// Find first occurrence of pattern in text using AVX2 (case-insensitive)
// This is a helper function - the main ContainsSubstringI will call this
// Returns true if pattern found, false otherwise
// Assumes: text.length() >= 32, pattern.length() >= 4, ASCII text
bool ContainsSubstringIAVX2(const std::string_view& text,
                            const std::string_view& pattern);

// Find first occurrence of pattern in text using AVX2 (case-sensitive)
// Returns true if pattern found, false otherwise
// Assumes: text.length() >= 32, pattern.length() >= 4
bool ContainsSubstringAVX2(const std::string_view& text,
                           const std::string_view& pattern);

// Case-insensitive strstr using AVX2
// Returns pointer to first match or nullptr
// Assumes: haystack length >= 32, needle length >= 4, ASCII text
const char* StrStrCaseInsensitiveAVX2(const char* haystack, const char* needle);

} // namespace string_search::avx2

#endif  // STRING_SEARCH_AVX2_AVAILABLE
