#include "utils/StringSearchAVX2.h"
#include "utils/StringSearch.h"

#if STRING_SEARCH_AVX2_AVAILABLE

#include <immintrin.h>
#include <cstring>
#include <algorithm>
#include <vector>

#ifdef _MSC_VER
#include <intrin.h> // For _BitScanForward
#endif  // _MSC_VER

namespace string_search::avx2 {

namespace { // Anonymous namespace for internal linkage

// Internal helper to compare pattern.
// GCC/Clang: ensure AVX2 codegen when TU is not built with -mavx2 (Linux correctness).
template <bool CaseSensitive>
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#endif  // defined(__GNUC__) || defined(__clang__)
inline bool FullCompare(const char* a, const char* b, size_t len) {
    if constexpr (CaseSensitive) {
        return std::memcmp(a, b, len) == 0;
    } else {
        // This is only called after a first-char match, so the cost is acceptable.
        for (size_t i = 0; i < len; ++i) {
            if (ToLowerChar(static_cast<unsigned char>(a[i])) != ToLowerChar(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    }
}

// The core AVX2 search implementation, templated for case-sensitivity.
// Assumes needle_len >= 4.
// GCC/Clang: ensure AVX2 codegen when TU is not built with -mavx2 (Linux correctness).
template <bool CaseSensitive>
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#endif  // defined(__GNUC__) || defined(__clang__)
const char* InternalStrStrAVX2(const char* haystack, size_t haystack_len, const char* needle, size_t needle_len) {
    const size_t max_start = haystack_len - needle_len;

    const char first_char_needle = CaseSensitive ? needle[0] : ToLowerChar(static_cast<unsigned char>(needle[0]));
    const __m256i v_first_char = _mm256_set1_epi8(first_char_needle);

    size_t i = 0;
    // Main AVX2 loop. Process haystack in 32-byte chunks.
    for (; i <= haystack_len - 32; i += 32) {
        const auto* chunk_ptr =
            reinterpret_cast<const __m256i*>(haystack + i);  // NOSONAR(cpp:S3630) - Required to load unaligned 256-bit chunk from byte buffer into AVX2 intrinsic; safer alternatives (memcpy/bit_cast) would add overhead in this hot path
        __m256i haystack_chunk = _mm256_loadu_si256(chunk_ptr);
        
        __m256i cmp_mask_vec;
        if constexpr (CaseSensitive) {
            cmp_mask_vec = _mm256_cmpeq_epi8(haystack_chunk, v_first_char);
        } else {
            __m256i lower_haystack_chunk = ToLowerAVX2(haystack_chunk);
            cmp_mask_vec = _mm256_cmpeq_epi8(lower_haystack_chunk, v_first_char);
        }

        unsigned int mask = _mm256_movemask_epi8(cmp_mask_vec);

        while (mask != 0) {
            unsigned long bit_pos;
#ifdef _MSC_VER
            _BitScanForward(&bit_pos, mask);
#else
            bit_pos = __builtin_ctz(mask);
#endif  // _MSC_VER
            const size_t match_pos = i + bit_pos;

            if (match_pos > max_start) {
                break;
            }

            if (FullCompare<CaseSensitive>(haystack + match_pos, needle, needle_len)) {
                return haystack + match_pos;
            }
            
            mask &= mask - 1; // Clear LSB
        }
    }
    
    // Scalar loop for the remainder of the haystack (less than 32 bytes).
    for (size_t k = i; k <= max_start; ++k) {
        if (FullCompare<CaseSensitive>(haystack + k, needle, needle_len)) {
            return haystack + k;
        }
    }

    return nullptr;
}

} // namespace

// --- Public Functions ---

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#endif  // defined(__GNUC__) || defined(__clang__)
bool ContainsSubstringIAVX2(const std::string_view& text, const std::string_view& pattern) {
    if (pattern.empty()) return true;
    if (text.length() < pattern.length()) return false;
    
    // For small patterns, the AVX2 setup overhead is not worth it. Use scalar.
    if (pattern.length() < 4) {
        return string_search::ContainsSubstringI(text, pattern);
    }
    
    return InternalStrStrAVX2<false>(text.data(), text.length(), pattern.data(), pattern.length()) != nullptr;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#endif  // defined(__GNUC__) || defined(__clang__)
bool ContainsSubstringAVX2(const std::string_view& text, const std::string_view& pattern) {
    if (pattern.empty()) return true;
    if (text.length() < pattern.length()) return false;

    if (pattern.length() < 4) {
        return string_search::ContainsSubstring(text, pattern);
    }

    return InternalStrStrAVX2<true>(text.data(), text.length(), pattern.data(), pattern.length()) != nullptr;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#endif  // defined(__GNUC__) || defined(__clang__)
const char* StrStrCaseInsensitiveAVX2(const char* haystack, const char* needle) {
    if (!needle || !*needle) return haystack;
    if (!haystack) return nullptr;
    
    size_t haystack_len = strlen(haystack);  // NOSONAR(cpp:S1081) - Safe: function parameter expected to be null-terminated C string (standard contract)
    size_t needle_len = strlen(needle);  // NOSONAR(cpp:S1081) - Safe: function parameter expected to be null-terminated C string (standard contract)
    
    if (haystack_len < needle_len) return nullptr;

    if (needle_len < 4) {
        return string_search::StrStrCaseInsensitive(haystack, needle);
    }
    
    return InternalStrStrAVX2<false>(haystack, haystack_len, needle, needle_len);
}

} // namespace string_search::avx2

#endif  // STRING_SEARCH_AVX2_AVAILABLE
