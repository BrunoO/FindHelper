#pragma once

#include "utils/StringSearch.h"

#if STRING_SEARCH_NEON_AVAILABLE
#include <arm_neon.h>

namespace string_search::neon {

// Convert 16 bytes to lowercase using NEON (ASCII only, characters 0–127).
// Adds 0x20 to lanes in ['A','Z']; matches the ToLowerAVX2 logic exactly.
inline uint8x16_t ToLowerNEON(uint8x16_t input) {
    const uint8x16_t upper_A  = vdupq_n_u8(static_cast<uint8_t>('A'));
    const uint8x16_t upper_Z  = vdupq_n_u8(static_cast<uint8_t>('Z'));
    const uint8x16_t add_case = vdupq_n_u8(0x20u);
    // is_upper: 0xFF where lane in [A,Z], 0x00 elsewhere
    const uint8x16_t is_upper = vandq_u8(vcgeq_u8(input, upper_A), vcleq_u8(input, upper_Z));
    return vaddq_u8(input, vandq_u8(add_case, is_upper));
}

// Out-of-line NEON search functions — declared in StringSearch.h.
// This header is provided for tests and callers that need ToLowerNEON directly.

} // namespace string_search::neon

#endif  // STRING_SEARCH_NEON_AVAILABLE
