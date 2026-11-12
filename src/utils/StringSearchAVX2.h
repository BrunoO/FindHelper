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

} // namespace string_search::avx2

#endif  // STRING_SEARCH_AVX2_AVAILABLE
