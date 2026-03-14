/**
 * @file FontUtils_linux.cpp
 * @brief Implementation of Linux-specific ImGui font configuration
 *
 * This module implements font loading and configuration for Linux:
 * - Maps logical font families (Consolas, Segoe UI, Arial, etc.) to Linux
 *   system font file paths using Fontconfig (fc-list command)
 * - Configures stb_truetype renderer with optimized settings for crisp text
 * - Implements fallback chain (primary -> fallback -> secondary fallback)
 * - Falls back to default ImGui font if system fonts are unavailable
 */

#include "FontUtils_linux.h"

#ifdef __linux__

#include "utils/Logger.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include "platform/FontUtilsCommon.h"
#include "platform/EmbeddedFont_RobotoMedium.h"
#include "platform/EmbeddedFont_Cousine.h"
#include "platform/EmbeddedFont_Karla.h"

#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

// Helper function to find font file path using Fontconfig (fc-list command)
// Returns empty string if font not found
static std::string FindFontPath(std::string_view font_name) {
  // Use fc-list to find font file path
  // Format: fc-list "family:font_name" file | head -1
  // Output format: /path/to/font.ttf: font_name:style=Regular
  // We extract the path (first field before colon) - convert to string for concatenation
  std::string font_name_str(font_name);
  std::string command = "fc-list \"family:" + font_name_str + "\" file 2>/dev/null | head -1";

  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe) {
    return "";
  }

  std::string buffer(512, '\0');
  std::string result;
  if (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
    if (size_t len = buffer.find('\0'); len != std::string::npos) {
      buffer.resize(len);  // Trim to actual length
    }
    // Extract path (everything before the first colon)
    result = buffer;
    if (size_t colon_pos = result.find(':'); colon_pos != std::string::npos) {
      result = result.substr(0, colon_pos);
    }
    // Remove trailing whitespace
    while (!result.empty() && (result.back() == ' ' || result.back() == '\t' || result.back() == '\n' || result.back() == '\r')) {
      result.pop_back();
    }
  }

  if (int status = pclose(pipe); status != 0) {
    return "";  // Command failed
  }

  // Verify the file exists and is readable
  if (!result.empty()) {
    std::ifstream file(result);
    if (!file.good()) {
      return "";
    }
  }

  return result;
}

// Helper function to find font path with fallback chain
// Tries fonts in order and returns the first non-empty path found
static std::string FindFontPathWithFallback(const std::vector<std::string>& font_names) {
  for (const auto& font_name : font_names) {
    std::string path = FindFontPath(font_name);
    if (!path.empty()) {
      return path;
    }
  }
  return "";
}

// Embedded font configuration structure
struct EmbeddedFontConfig {
  const char* display_name;
  const unsigned char* compressed_data;
  int compressed_size;
};

// Map of embedded font family names to their configurations
static const std::unordered_map<std::string, EmbeddedFontConfig> kEmbeddedFonts = {  // NOSONAR(cpp:S7119) - Initialization order is safe: compressed_data and compressed_size are defined in header files before this point
    {"Roboto Medium", {"Roboto-Medium", RobotoMedium_compressed_data, static_cast<int>(RobotoMedium_compressed_size)}},
    {"Cousine", {"Cousine", Cousine_compressed_data, static_cast<int>(Cousine_compressed_size)}},
    {"Karla", {"Karla", Karla_compressed_data, static_cast<int>(Karla_compressed_size)}}};

// System font fallback configuration structure
struct SystemFontConfig {
  std::vector<std::string> primary_fonts;  // Fonts to try in order for primary
  std::string fallback_font;               // Single fallback font
  std::string secondary_fallback_font;      // Secondary fallback font
  bool use_fallback_chain_for_primary;      // Whether to use FindFontPathWithFallback for primary
};

// Map of system font family names to their fallback configurations
static const std::unordered_map<std::string, SystemFontConfig> kSystemFontConfigs = {
    {"Consolas", {{"DejaVu Sans Mono", "Liberation Mono", "Courier New"}, "Ubuntu Mono", "Monospace", true}},
    {"Segoe UI", {{"Ubuntu", "DejaVu Sans", "Liberation Sans"}, "Arial", "Sans", true}},
    {"Calibri", {{"Ubuntu", "DejaVu Sans", "Liberation Sans"}, "Arial", "Sans", true}},
    {"Arial", {{"Arial", "Liberation Sans", "DejaVu Sans"}, "Ubuntu", "Sans", true}},
    {"Verdana", {{"Verdana", "DejaVu Sans"}, "Liberation Sans", "Sans", true}},
    {"Tahoma", {{"Tahoma", "DejaVu Sans"}, "Liberation Sans", "Sans", true}},
    {"Cascadia Mono", {{"Cascadia Mono", "DejaVu Sans Mono", "Liberation Mono"}, "Ubuntu Mono", "Monospace", true}},
    {"Lucida Console", {{"Lucida Console", "DejaVu Sans Mono", "Liberation Mono"}, "Ubuntu Mono", "Monospace", true}},
    {"Courier New", {{"Courier New", "Liberation Mono", "DejaVu Sans Mono"}, "Ubuntu Mono", "Monospace", true}},
    {"DejaVu Sans Mono", {{"DejaVu Sans Mono"}, "Liberation Mono", "Ubuntu Mono", false}},
    {"Ubuntu", {{"Ubuntu"}, "DejaVu Sans", "Liberation Sans", false}},
    {"Ubuntu Mono", {{"Ubuntu Mono"}, "DejaVu Sans Mono", "Liberation Mono", false}}};

void ConfigureFontsFromSettings(const AppSettings& settings) {
  ImGuiIO& io = ImGui::GetIO();

  io.Fonts->Clear();

  ImFontConfig font_config;
  font_utils::SetupDefaultFontConfig(font_config);

  const float font_size = settings.fontSize;
  const std::string family = settings.fontFamily;

  // Check for embedded fonts first (available on all platforms)
  if (const auto it = kEmbeddedFonts.find(family); it != kEmbeddedFonts.end()) {
    const auto& config = it->second;
    font_utils::LoadEmbeddedFont(io, font_config, font_size, config.display_name,
                                config.compressed_data, config.compressed_size);
    return;
  }

  // Look up system font configuration
  std::string primary_font_path;
  std::string fallback_font_path;
  std::string secondary_fallback_font_path;

  if (const auto it = kSystemFontConfigs.find(family); it != kSystemFontConfigs.end()) {
    const auto& config = it->second;
    if (config.use_fallback_chain_for_primary) {
      primary_font_path = FindFontPathWithFallback(config.primary_fonts);
    } else {
      primary_font_path = FindFontPath(config.primary_fonts[0]);
    }
    fallback_font_path = FindFontPath(config.fallback_font);
    secondary_fallback_font_path = FindFontPath(config.secondary_fallback_font);
  } else {
    // Unknown family: fall back to Linux defaults
    primary_font_path = FindFontPathWithFallback({"DejaVu Sans", "Ubuntu", "Liberation Sans"});
    fallback_font_path = FindFontPath("Sans");
    secondary_fallback_font_path = FindFontPath("Arial");
  }

  if (ImFont* font = font_utils::TryLoadFontChain(
          io.Fonts,
          primary_font_path.empty() ? nullptr : primary_font_path.c_str(),
          fallback_font_path.empty() ? nullptr : fallback_font_path.c_str(),
          secondary_fallback_font_path.empty() ? nullptr : secondary_fallback_font_path.c_str(),
          font_size,
          &font_config);
      font == nullptr) {
    font = io.Fonts->AddFontDefault();
    io.FontDefault = font;
    LOG_INFO("Using default ImGui font");
  } else {
    io.FontDefault = font;
    LOG_INFO("Unicode font loaded successfully with optimized FreeType "
             "settings");
  }

  font_utils::MergeFontAwesomeIcons(io, font_size);
}

void ApplyFontSettings(const AppSettings& settings) {
  font_utils::ApplyFontSettingsCommon<ConfigureFontsFromSettings,
                                      ImGui_ImplOpenGL3_DestroyDeviceObjects>(settings);
}

#endif  // __linux__
