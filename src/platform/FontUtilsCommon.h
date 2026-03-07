#pragma once

/**
 * @file FontUtilsCommon.h
 * @brief Shared font configuration helpers used by Win/Linux/macOS FontUtils
 *
 * Platform-agnostic: FreeType constants, default ImFontConfig setup,
 * MergeFontAwesomeIcons, LoadEmbeddedFont, TryLoadFontChain.
 * Platform code provides path resolution and backend invalidation.
 */

#include "imgui.h"

namespace font_utils {

// FreeType font configuration constants (optimized for FreeType hinting).
constexpr int kFreeTypeOversampleH = 3;
constexpr int kFreeTypeOversampleV = 1;
constexpr float kFreeTypeRasterizerDensity = 1.0F;

/** Set default FreeType-optimized ImFontConfig (pixel snap, oversample, density, glyph range). */
void SetupDefaultFontConfig(ImFontConfig& config);

/** Merge FontAwesome icon font into the current font atlas (merge mode). */
void MergeFontAwesomeIcons(ImGuiIO& io, float font_size);

/**
 * Load embedded font from compressed TTF; merge FontAwesome on success.
 * On failure, sets io.FontDefault to AddFontDefault() and returns false.
 */
bool LoadEmbeddedFont(ImGuiIO& io,
                     ImFontConfig& font_config,
                     float font_size,
                     const char* font_name,
                     const unsigned char* compressed_data,
                     int compressed_size);

/**
 * Try loading from primary, then fallback, then secondary path.
 * nullptr or empty path is skipped. Returns first non-null font or nullptr.
 */
ImFont* TryLoadFontChain(ImFontAtlas* atlas,
                        const char* primary_path,
                        const char* fallback_path,
                        const char* secondary_fallback_path,
                        float font_size,
                        const ImFontConfig* font_config);

}  // namespace font_utils
