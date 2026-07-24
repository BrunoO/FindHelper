/**
 * @file FontUtils_win.cpp
 * @brief Implementation of Windows-specific ImGui font configuration
 *
 * This module implements font loading and configuration for Windows:
 * - Maps logical font families (Consolas, Segoe UI, Arial, etc.) to Windows
 *   font file paths in C:\Windows\Fonts\
 * - Configures stb_truetype renderer with optimized settings for crisp text
 * - Implements fallback chain (primary -> fallback -> secondary fallback)
 * - Falls back to default ImGui font if system fonts are unavailable
 */

#include "FontUtils_win.h"

#ifdef _WIN32

#include "imgui.h"
#include "imgui_impl_dx11.h"

#include "platform/EmbeddedFont_Cousine.h"
#include "platform/EmbeddedFont_Karla.h"
#include "platform/EmbeddedFont_RobotoMedium.h"
#include "platform/FontUtilsCommon.h"

#include "utils/Logger.h"

#include <array>
#include <string>

namespace {

struct EmbeddedFontEntry {
  const char* family_name;
  const char* display_name;
  const unsigned char* data;
  int size;
};

// Construct on first use to avoid S7119: initialization order across TUs is unspecified
// (kEmbeddedFonts depends on extern RobotoMedium_*, Cousine_*, Karla_* from other .cpp files).
// We keep function-local static rather than C++17 inline variable so extern font data is
// guaranteed initialized when first used.
const std::array<EmbeddedFontEntry, 3>& GetEmbeddedFonts() {
  static const std::array<EmbeddedFontEntry, 3> kEmbeddedFonts = {{  // NOSONAR(cpp:S6018) - Function-local static avoids cross-TU init-order issues for extern font data (see S7119)
    {"Roboto Medium", "Roboto-Medium", RobotoMedium_compressed_data,
     static_cast<int>(RobotoMedium_compressed_size)},
    {"Cousine", "Cousine", Cousine_compressed_data, static_cast<int>(Cousine_compressed_size)},
    {"Karla", "Karla", Karla_compressed_data, static_cast<int>(Karla_compressed_size)},
  }};
  return kEmbeddedFonts;
}

struct SystemFontPaths {
  const char* family_name;
  const char* primary;
  const char* fallback;
  const char* secondary;
};

inline const std::array<SystemFontPaths, 9> kSystemFontMap = {{
  {"Consolas", "C:\\Windows\\Fonts\\consola.ttf", "C:\\Windows\\Fonts\\segoeui.ttf",
   "C:\\Windows\\Fonts\\arial.ttf"},
  {"Segoe UI", "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\consola.ttf",
   "C:\\Windows\\Fonts\\arial.ttf"},
  {"Arial", "C:\\Windows\\Fonts\\arial.ttf", "C:\\Windows\\Fonts\\segoeui.ttf",
   "C:\\Windows\\Fonts\\consola.ttf"},
  {"Calibri", "C:\\Windows\\Fonts\\calibri.ttf", "C:\\Windows\\Fonts\\segoeui.ttf",
   "C:\\Windows\\Fonts\\arial.ttf"},
  {"Verdana", "C:\\Windows\\Fonts\\verdana.ttf", "C:\\Windows\\Fonts\\segoeui.ttf",
   "C:\\Windows\\Fonts\\arial.ttf"},
  {"Tahoma", "C:\\Windows\\Fonts\\tahoma.ttf", "C:\\Windows\\Fonts\\segoeui.ttf",
   "C:\\Windows\\Fonts\\arial.ttf"},
  {"Cascadia Mono", "C:\\Windows\\Fonts\\CascadiaMono.ttf",
   "C:\\Windows\\Fonts\\CascadiaMonoPL.ttf", "C:\\Windows\\Fonts\\consola.ttf"},
  {"Lucida Console", "C:\\Windows\\Fonts\\lucon.ttf", "C:\\Windows\\Fonts\\consola.ttf",
   "C:\\Windows\\Fonts\\segoeui.ttf"},
  {"Courier New", "C:\\Windows\\Fonts\\cour.ttf", "C:\\Windows\\Fonts\\courbd.ttf",
   "C:\\Windows\\Fonts\\consola.ttf"},
}};

constexpr const char* const kDefaultPrimary = "C:\\Windows\\Fonts\\consola.ttf";
constexpr const char* const kDefaultFallback = "C:\\Windows\\Fonts\\arial.ttf";
constexpr const char* const kDefaultSecondary = "C:\\Windows\\Fonts\\segoeui.ttf";

}  // namespace

void ConfigureFontsFromSettings(const AppSettings& settings) {
  ImGuiIO& io = ImGui::GetIO();

  io.Fonts->Clear();

  ImFontConfig font_config;
  font_utils::SetupDefaultFontConfig(font_config);

  const float font_size = settings.fontSize;
  const std::string family = settings.fontFamily;

  // Check for embedded fonts first (available on all platforms)
  // If family is empty (default), prioritizing Roboto Medium
  const std::string effective_family = family.empty() ? "Roboto Medium" : family;

  for (const auto& entry : GetEmbeddedFonts()) {
    if (effective_family == entry.family_name) {
      font_utils::LoadEmbeddedFont(io, font_config, font_size, entry.display_name, entry.data,
                                   entry.size);
      return;
    }
  }

  // Look up system font paths
  const char* primary_font_path = kDefaultPrimary;
  const char* fallback_font_path = kDefaultFallback;
  const char* secondary_fallback_font_path = kDefaultSecondary;

  for (const auto& entry : kSystemFontMap) {
    if (effective_family == entry.family_name) {
      primary_font_path = entry.primary;
      fallback_font_path = entry.fallback;
      secondary_fallback_font_path = entry.secondary;
      break;
    }
  }

  if (ImFont* font =
        font_utils::TryLoadFontChain(io.Fonts, primary_font_path, fallback_font_path,
                                     secondary_fallback_font_path, font_size, &font_config);
      font == nullptr) {
    // If system font lookup failed, fallback to default ImGui font
    font = io.Fonts->AddFontDefault();
    io.FontDefault = font;
    LOG_INFO("Using default ImGui font (system font lookup failed)");
  } else {
    io.FontDefault = font;
    LOG_INFO("Unicode font loaded successfully with optimized FreeType "
             "settings");
  }

  font_utils::MergeFontAwesomeIcons(io, font_size);
}

void ApplyFontSettings(const AppSettings& settings) {
  font_utils::ApplyFontSettingsCommon<ConfigureFontsFromSettings,
                                      ImGui_ImplDX11_InvalidateDeviceObjects>(settings);
}

#endif  // _WIN32
