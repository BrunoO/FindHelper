/**
 * @file FontUtils_mac.mm
 * @brief Implementation of macOS-specific ImGui font configuration
 *
 * This module implements font loading and configuration for macOS:
 * - Maps logical font families (Consolas, Segoe UI, Arial, etc.) to macOS
 *   system font file paths in /System/Library/Fonts/ and /Library/Fonts/
 * - Configures stb_truetype renderer with optimized settings for crisp text
 * - Implements fallback chain (primary -> fallback -> secondary fallback)
 * - Falls back to default ImGui font if system fonts are unavailable
 */

#include "FontUtils_mac.h"

#ifdef __APPLE__

#include "utils/Logger.h"

#include "imgui.h"
#include "imgui_impl_metal.h"

#include "platform/EmbeddedFont_Cousine.h"
#include "platform/EmbeddedFont_Karla.h"
#include "platform/EmbeddedFont_RobotoMedium.h"
#include "platform/FontUtilsCommon.h"

#include <string>
#include <vector>

#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>

// Helper function to find font file path using Core Text
// Returns empty string if font not found
static std::string FindFontPath(std::string_view font_name) {
  @autoreleasepool {
    std::string font_name_str(font_name);
    NSString *font_name_ns =
        [NSString stringWithUTF8String:font_name_str.c_str()];
    if (!font_name_ns) {
      return ""; // Invalid UTF-8 in font name
    }
    CTFontDescriptorRef descriptor = CTFontDescriptorCreateWithNameAndSize(
        (__bridge CFStringRef)font_name_ns, 0.0);

    if (descriptor == nullptr) {
      return "";
    }

    CFURLRef url = (CFURLRef)CTFontDescriptorCopyAttribute(descriptor,
                                                           kCTFontURLAttribute);

    CFRelease(descriptor);

    if (url == nullptr) {
      return "";
    }

    CFStringRef path_cf = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
    CFRelease(url);

    if (path_cf == nullptr) {
      return "";
    }

    NSString *path_ns = (__bridge NSString *)path_cf;
    std::string path = [path_ns UTF8String];
    CFRelease(path_cf);

    return path;
  }
}

// Helper function to find font path with fallback chain
// Tries fonts in order and returns the first non-empty path found
static std::string
FindFontPathWithFallback(const std::vector<std::string> &font_names) {
  for (const auto &font_name : font_names) {
    std::string path = FindFontPath(font_name);
    if (!path.empty()) {
      return path;
    }
  }
  return "";
}

namespace {

struct EmbeddedFontEntry {
  const char *family_name;
  const char *display_name;
  const unsigned char *data;
  int size;
};

static const EmbeddedFontEntry kEmbeddedFonts[] = {
    {"Roboto Medium", "Roboto-Medium", RobotoMedium_compressed_data,
     static_cast<int>(RobotoMedium_compressed_size)},
    {"Cousine", "Cousine", Cousine_compressed_data,
     static_cast<int>(Cousine_compressed_size)},
    {"Karla", "Karla", Karla_compressed_data,
     static_cast<int>(Karla_compressed_size)},
};

} // namespace

void ConfigureFontsFromSettings(const AppSettings &settings) {
  ImGuiIO &io = ImGui::GetIO();

  io.Fonts->Clear();

  ImFontConfig font_config;
  font_utils::SetupDefaultFontConfig(font_config);

  const float font_size = settings.fontSize;
  const std::string family = settings.fontFamily;

  // Check for embedded fonts first (available on all platforms)
  // If family is empty (default), prioritize Roboto Medium
  const std::string effective_family =
      family.empty() ? "Roboto Medium" : family;

  for (const auto &entry : kEmbeddedFonts) {
    if (effective_family == entry.family_name) {
      font_utils::LoadEmbeddedFont(io, font_config, font_size,
                                   entry.display_name, entry.data, entry.size);
      return;
    }
  }

  std::string primary_font_path;
  std::string fallback_font_path;
  std::string secondary_fallback_font_path;

  if (effective_family == "Consolas") {
    primary_font_path = FindFontPathWithFallback({"Menlo", "SF Mono"});
    fallback_font_path = FindFontPath("Monaco");
    secondary_fallback_font_path = FindFontPath("Courier New");
  } else if (effective_family == "Segoe UI") {
    primary_font_path = FindFontPathWithFallback({"SF Pro", "Helvetica Neue"});
    fallback_font_path = FindFontPath("Helvetica");
    secondary_fallback_font_path = FindFontPath("Arial");
  } else if (effective_family == "Arial") {
    primary_font_path = FindFontPath("Arial");
    fallback_font_path = FindFontPath("Helvetica");
    secondary_fallback_font_path = FindFontPath("SF Pro");
  } else if (effective_family == "Calibri") {
    primary_font_path = FindFontPathWithFallback({"SF Pro", "Helvetica Neue"});
    fallback_font_path = FindFontPath("Arial");
    secondary_fallback_font_path = FindFontPath("Helvetica");
  } else if (effective_family == "Verdana") {
    primary_font_path = FindFontPath("Verdana");
    fallback_font_path = FindFontPath("Helvetica");
    secondary_fallback_font_path = FindFontPath("Arial");
  } else if (effective_family == "Tahoma") {
    primary_font_path = FindFontPath("Tahoma");
    fallback_font_path = FindFontPath("Helvetica");
    secondary_fallback_font_path = FindFontPath("Arial");
  } else if (effective_family == "Cascadia Mono") {
    primary_font_path =
        FindFontPathWithFallback({"Cascadia Mono", "SF Mono", "Menlo"});
    fallback_font_path = FindFontPath("Monaco");
    secondary_fallback_font_path = FindFontPath("Courier New");
  } else if (effective_family == "Lucida Console") {
    primary_font_path = FindFontPathWithFallback({"Lucida Console", "Menlo"});
    fallback_font_path = FindFontPath("Monaco");
    secondary_fallback_font_path = FindFontPath("Courier New");
  } else if (effective_family == "Courier New") {
    primary_font_path = FindFontPath("Courier New");
    fallback_font_path = FindFontPath("Menlo");
    secondary_fallback_font_path = FindFontPath("Monaco");
  } else if (effective_family == "Menlo") {
    primary_font_path = FindFontPath("Menlo");
    fallback_font_path = FindFontPath("SF Mono");
    secondary_fallback_font_path = FindFontPath("Monaco");
  } else if (effective_family == "SF Mono") {
    primary_font_path = FindFontPath("SF Mono");
    fallback_font_path = FindFontPath("Menlo");
    secondary_fallback_font_path = FindFontPath("Monaco");
  } else if (effective_family == "SF Pro") {
    primary_font_path = FindFontPath("SF Pro");
    fallback_font_path = FindFontPath("Helvetica Neue");
    secondary_fallback_font_path = FindFontPath("Helvetica");
  } else {
    // Default fallback
    primary_font_path =
        FindFontPathWithFallback({"Menlo", "SF Mono", "Monaco"});
    fallback_font_path = FindFontPath("Helvetica");
    secondary_fallback_font_path = FindFontPath("Arial");
  }

  ImFont *font = font_utils::TryLoadFontChain(
      io.Fonts, primary_font_path.empty() ? nullptr : primary_font_path.c_str(),
      fallback_font_path.empty() ? nullptr : fallback_font_path.c_str(),
      secondary_fallback_font_path.empty()
          ? nullptr
          : secondary_fallback_font_path.c_str(),
      font_size, &font_config);

  if (font == nullptr) {
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

void ApplyFontSettings(const AppSettings &settings) {
  ImGuiIO &io = ImGui::GetIO();

  io.FontGlobalScale = settings.fontScale;

  ConfigureFontsFromSettings(settings);
  io.Fonts->Build();

  ImGui_ImplMetal_DestroyDeviceObjects();
}

#endif  // __APPLE__
