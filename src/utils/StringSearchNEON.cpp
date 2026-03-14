#include "utils/StringSearchNEON.h"
#include "utils/StringSearch.h"

#if STRING_SEARCH_NEON_AVAILABLE

#include <arm_neon.h>
#include <array>
#include <cstring>

namespace string_search::neon {

namespace {

// Scalar full-pattern comparison, templated for case sensitivity.
// Only called after a first-character NEON hit, so the overhead is acceptable.
template <bool CaseSensitive>
inline bool FullCompare(const char* a, const char* b, size_t len) {
    if constexpr (CaseSensitive) {
        return std::memcmp(a, b, len) == 0;
    } else {
        for (size_t i = 0; i < len; ++i) {
            if (ToLowerChar(static_cast<unsigned char>(a[i])) !=
                ToLowerChar(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    }
}

// Core NEON search: processes the haystack in 16-byte chunks.
// Strategy: broadcast first char of needle → vceqq_u8 → vmaxvq_u8 for fast
// zero-chunk skip → vst1q_u8 + scalar scan only when a candidate is found.
// Assumes haystack_len >= needle_len and needle_len >= 4.
template <bool CaseSensitive>
const char* InternalStrStrNEON(const char* haystack, size_t haystack_len,
                               const char* needle, size_t needle_len) {
    const size_t max_start = haystack_len - needle_len;

    const uint8_t first = CaseSensitive
        ? static_cast<uint8_t>(needle[0])
        : static_cast<uint8_t>(ToLowerChar(static_cast<unsigned char>(needle[0])));
    const uint8x16_t v_first = vdupq_n_u8(first);

    size_t i = 0;
    for (; i + 16 <= haystack_len; i += 16) {
        // vld1q_u8 handles unaligned loads natively on arm64.
        const uint8x16_t chunk = vld1q_u8(  // NOSONAR(cpp:S3630) - Required to load 16-byte char buffer into NEON intrinsic; no safer zero-copy alternative
            reinterpret_cast<const uint8_t*>(haystack + i));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) - NEON vld1q_u8 requires uint8_t*; pointer offset over char buffer is intentional

        const uint8x16_t cmp = CaseSensitive
            ? vceqq_u8(chunk, v_first)
            : vceqq_u8(ToLowerNEON(chunk), v_first);

        // vmaxvq_u8: horizontal max — zero means no first-char match in this chunk.
        // This skips the vast majority of chunks in one instruction.
        if (vmaxvq_u8(cmp) == 0) {
            continue;
        }

        // A first-char candidate exists somewhere in these 16 bytes.
        // Store the match mask and check each lane with a scalar full compare.
        // std::array avoids C-style array and keeps index-checked element access.
        std::array<uint8_t, 16> match_lane{};
        vst1q_u8(match_lane.data(), cmp);
        for (size_t b = 0; b < 16; ++b) {
            if (match_lane[b] == 0) {  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - NEON SIMD, loop-guarded; b is in [0,15], array size is 16
                continue;
            }
            const size_t pos = i + b;
            if (pos > max_start) {
                break;
            }
            if (FullCompare<CaseSensitive>(haystack + pos, needle, needle_len)) {
                return haystack + pos;
            }
        }
    }

    // Scalar tail: fewer than 16 bytes remain.
    for (size_t k = i; k <= max_start; ++k) {
        if (FullCompare<CaseSensitive>(haystack + k, needle, needle_len)) {
            return haystack + k;
        }
    }
    return nullptr;
}

} // namespace

bool ContainsSubstringINEON(const std::string_view& text, const std::string_view& pattern) {  // NOLINT(misc-no-recursion) - apparent recursion via ContainsSubstringI→TryNEONPath is guarded by length checks; no actual recursion at runtime
    if (pattern.empty()) { return true; }
    if (text.length() < pattern.length()) { return false; }
    if (pattern.length() < 4) {
        return string_search::ContainsSubstringI(text, pattern);
    }
    return InternalStrStrNEON<false>(text.data(), text.length(),
                                    pattern.data(), pattern.length()) != nullptr;
}

bool ContainsSubstringNEON(const std::string_view& text, const std::string_view& pattern) {  // NOLINT(misc-no-recursion) - apparent recursion via ContainsSubstring→TryNEONPath is guarded by length checks; no actual recursion at runtime
    if (pattern.empty()) { return true; }
    if (text.length() < pattern.length()) { return false; }
    if (pattern.length() < 4) {
        return string_search::ContainsSubstring(text, pattern);
    }
    return InternalStrStrNEON<true>(text.data(), text.length(),
                                   pattern.data(), pattern.length()) != nullptr;
}

const char* StrStrCaseInsensitiveNEON(const char* haystack, const char* needle) {  // NOLINT(misc-no-recursion) - apparent recursion via StrStrCaseInsensitive→TryNEONPathStrStr is guarded by length checks; no actual recursion at runtime
    if (needle == nullptr || *needle == '\0') { return haystack; }
    if (haystack == nullptr) { return nullptr; }
    const size_t haystack_len = strlen(haystack);  // NOSONAR(cpp:S1081) - Safe: null-terminated C-string contract
    const size_t needle_len   = strlen(needle);    // NOSONAR(cpp:S1081) - Safe: null-terminated C-string contract
    if (haystack_len < needle_len) { return nullptr; }
    if (needle_len < 4) {
        return string_search::StrStrCaseInsensitive(haystack, needle);
    }
    return InternalStrStrNEON<false>(haystack, haystack_len, needle, needle_len);
}

} // namespace string_search::neon

#endif  // STRING_SEARCH_NEON_AVAILABLE
