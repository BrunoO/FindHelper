#pragma once

/**
 * @file FontUtils_linux.h
 * @brief Linux-specific ImGui font configuration helpers
 *
 * This module provides Linux-specific font configuration for ImGui:
 * - Maps logical font family names to Linux system font file paths using Fontconfig
 * - Configures FreeType renderer with optimized settings (pixel snapping,
 *   oversampling, rasterization density)
 * - Handles font loading with fallback chains
 * - Rebuilds font atlas and invalidates OpenGL device objects
 *
 * These functions are kept in a dedicated module so we can provide platform-specific
 * font configuration without cluttering platform-neutral code.
 */

#ifdef __linux__

#include "core/Settings.h"

/**
 * @brief Configure ImGui fonts (family and size) from the given settings
 *
 * This function:
 * - Clears the existing font atlas
 * - Configures FreeType renderer with optimized settings:
 *   * Pixel snapping for crisp rendering
 *   * Optimized oversampling (FreeType handles hinting automatically)
 *   * Standard rasterization density (FreeType's superior hinting makes higher density unnecessary)
 * - Maps logical font family to Linux font file paths using Fontconfig
 * - Loads primary font with fallback chain (up to 3 fonts)
 * - Sets ImGuiIO::FontDefault to the loaded font
 *
 * The caller is responsible for:
 * - Calling io.Fonts->Build() after this function
 * - Invalidating renderer device objects (e.g., ImGui_ImplOpenGL3_InvalidateDeviceObjects)
 *
 * @param settings Application settings containing font family and size
 */
void ConfigureFontsFromSettings(const AppSettings &settings);

/**
 * @brief Apply font-related settings (family, size, global scale) immediately
 *
 * This function applies all font-related settings and updates the rendering:
 * - Updates global UI scale (io.FontGlobalScale)
 * - Reconfigures fonts via ConfigureFontsFromSettings()
 * - Rebuilds the font atlas
 * - Invalidates OpenGL3 device objects so the new atlas is uploaded
 *
 * This should be called when font settings change (e.g., from settings dialog).
 *
 * @param settings Application settings containing font family, size, and scale
 */
void ApplyFontSettings(const AppSettings &settings);

#endif  // __linux__

