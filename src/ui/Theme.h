#pragma once

#include "imgui.h"

#include <string_view>

namespace ui {

/**
 * @brief Centralized theme management for the application.
 * Supports multiple palettes (Default Dark, Dracula, Nord, One Dark, Gruvbox).
 * Apply(theme_id, viewports_enabled) sets the active palette; Colors holds the current values.
 */
class Theme {
 public:
  // Semantic color names for use in UI code (PushStyleColor, etc.).
  // Values are updated by Apply() to match the active theme.
  struct Colors {
    static ImVec4 Surface;         // NOLINT(readability-identifier-naming) - PascalCase semantic color API
    static ImVec4 SurfaceHover;    // NOLINT(readability-identifier-naming) - PascalCase semantic color API
    static ImVec4 SurfaceActive;   // NOLINT(readability-identifier-naming) - PascalCase semantic color API

    // Input fields (FrameBg) - lighter than headers for distinction
    static ImVec4 FrameBg;         // NOLINT(readability-identifier-naming) - PascalCase semantic color API
    static ImVec4 FrameBgHover;    // NOLINT(readability-identifier-naming) - PascalCase semantic color API
    static ImVec4 FrameBgActive;   // NOLINT(readability-identifier-naming) - PascalCase semantic color API

    // Border
    static ImVec4 Border;          // NOLINT(readability-identifier-naming) - PascalCase semantic color API

    // Accent (primary actions; quick filters, etc.)
    static ImVec4 Accent;         // NOLINT(readability-identifier-naming) - PascalCase semantic color API
    static ImVec4 AccentHover;     // NOLINT(readability-identifier-naming) - PascalCase semantic color API
    static ImVec4 AccentActive;    // NOLINT(readability-identifier-naming) - PascalCase semantic color API

    // Text (match ImGui dark style)
    static ImVec4 Text;            // NOLINT(readability-identifier-naming) - PascalCase semantic color API
    static ImVec4 TextStrong;      // NOLINT(readability-identifier-naming) - Section headers; ~5% brighter than Text
    static ImVec4 TextDim;         // NOLINT(readability-identifier-naming) - PascalCase semantic color API
    static ImVec4 TextDisabled;   // NOLINT(readability-identifier-naming) - PascalCase semantic color API
    static ImVec4 TextOnAccent;    // NOLINT(readability-identifier-naming) - PascalCase semantic color API

    // Functional (for components that push them)
    static ImVec4 Success;         // NOLINT(readability-identifier-naming) - PascalCase semantic color API
    static ImVec4 Warning;         // NOLINT(readability-identifier-naming) - PascalCase semantic color API
    static ImVec4 Error;           // NOLINT(readability-identifier-naming) - PascalCase semantic color API

    // Tool/modal window background (Search Help, Metrics, Settings)
    static ImVec4 WindowBg;        // NOLINT(readability-identifier-naming) - PascalCase semantic color API
  };

  /**
   * Named alpha values for AccentTint (headers, hover states, etc.).
   * Adjust in one place to retheme app accents.
   */
  struct HeaderAlphas {
    static constexpr float Base = 0.25F;         // NOLINT(readability-identifier-naming) - PascalCase semantic API
    static constexpr float Hover = 0.35F;       // NOLINT(readability-identifier-naming) - PascalCase semantic API
    static constexpr float Active = 0.45F;       // NOLINT(readability-identifier-naming) - PascalCase semantic API
    static constexpr float TableHeader = 0.22F;  // NOLINT(readability-identifier-naming) - PascalCase semantic API
  };

  /**
   * @brief Returns Accent color with given alpha for subtle tinting (headers, etc.).
   * @param alpha Alpha value in [0, 1]; clamped if out of range.
   */
  static ImVec4 AccentTint(float alpha);

  /**
   * @brief Applies the theme by ID. Updates Colors and ImGui style.
   * @param theme_id One of: "default_dark", "dracula", "nord", "one_dark", "gruvbox", "everforest", "catppuccin". Unknown IDs
   * fall back to default_dark.
   * @param viewports_enabled Whether multi-viewports are enabled (affects window rounding).
   */
  static void Apply(std::string_view theme_id, bool viewports_enabled = false);

  /**
   * @brief Applies the default dark theme (convenience overload for bootstrap before settings
   * load).
   * @param viewports_enabled Whether multi-viewports are enabled.
   */
  static void Apply(bool viewports_enabled = false);

  /**
   * @brief Push the 4 accent button style colors (Button, ButtonHovered, ButtonActive, Text).
   * Must be matched by a call to PopAccentButtonStyle().
   */
  static void PushAccentButtonStyle();

  /**
   * @brief Pop the 4 accent button style colors pushed by PushAccentButtonStyle().
   */
  static void PopAccentButtonStyle();

 private:
  static void SetupColors();
  static void SetupStyle(bool viewports_enabled);
};

}  // namespace ui
