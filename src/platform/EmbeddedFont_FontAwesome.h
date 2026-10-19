#pragma once

/**
 * @file EmbeddedFont_FontAwesome.h
 * @brief Embedded FontAwesome 6 Solid icon font data
 *
 * This file provides access to the embedded FontAwesome 6 Solid icon font data.
 * The font is compressed and embedded directly in the binary for zero
 * file dependencies. FontAwesome icons are merged with the main font using
 * ImGui's merge mode, allowing icons to be used alongside text.
 *
 * Generated from: resources/fonts/fontawesome-solid-900.ttf
 * Using: binary_to_compressed_c tool
 * License: Font Awesome Free License (SIL OFL 1.1)
 * Source: https://fontawesome.com/
 */

// Forward declarations - actual data is in .cpp file (embedded resource pattern)
extern const unsigned int FontAwesome_compressed_size;  // NOLINT(readability-identifier-naming) - generated name from binary_to_compressed_c
extern const unsigned char FontAwesome_compressed_data[];  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) - generated; extern C array for embedded font

// Icon range for FontAwesome 6
// Macros required for ImGui font range arrays (e.g., static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 })
#define ICON_MIN_FA 0xf000  // NOSONAR(cpp:S5028) NOLINT(cppcoreguidelines-macro-usage) - ImGui array initializer requires macro
#define ICON_MAX_FA 0xf8ff  // NOSONAR(cpp:S5028) NOLINT(cppcoreguidelines-macro-usage) - ImGui array initializer requires macro
