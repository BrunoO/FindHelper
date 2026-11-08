#pragma once

/**
 * @file EmbeddedFont_Inter.h
 * @brief Embedded Inter Regular font data
 *
 * This file provides access to the embedded Inter Regular font data.
 * The font is compressed and embedded directly in the binary for zero
 * file dependencies. This font is available as the modern default UI font
 * alongside system fonts on all platforms.
 *
 * License: SIL Open Font License 1.1 (Rasmus Andersson)
 */

// Forward declarations - actual data is in .cpp file
// NOLINTNEXTLINE(readability-identifier-naming) - generated name from binary_to_compressed_c tool
extern const unsigned int Inter_compressed_size;
// NOLINTNEXTLINE(readability-identifier-naming,cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) - generated embedded data
extern const unsigned char Inter_compressed_data[];
