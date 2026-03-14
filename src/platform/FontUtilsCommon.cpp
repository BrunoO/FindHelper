/**
 * @file FontUtilsCommon.cpp
 * @brief Shared font configuration implementation (platform-agnostic)
 */

#include "platform/FontUtilsCommon.h"

#include <cfloat>
#include <string>

#include "imgui.h"
#include "platform/EmbeddedFont_FontAwesome.h"
#include "utils/Logger.h"

namespace font_utils {

void SetupDefaultFontConfig(ImFontConfig& config) {
  config.FontDataOwnedByAtlas = false;
  config.PixelSnapH = true;
  config.OversampleH = kFreeTypeOversampleH;
  config.OversampleV = kFreeTypeOversampleV;
  config.RasterizerDensity = kFreeTypeRasterizerDensity;
  config.GlyphMinAdvanceX = 0.0F;
  config.GlyphMaxAdvanceX = FLT_MAX;
}

void MergeFontAwesomeIcons(ImGuiIO& io, float font_size) {
  ImFontConfig icon_config;
  icon_config.MergeMode = true;
  icon_config.FontDataOwnedByAtlas = false;
  icon_config.GlyphMinAdvanceX = font_size;
  icon_config.PixelSnapH = true;

  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays,readability-identifier-naming) - ImGui API requires C-style array
  static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};  // NOSONAR(cpp:S5945) - ImGui API AddFontFromMemoryCompressedTTF requires C-style array
  const ImFont* icon_font = io.Fonts->AddFontFromMemoryCompressedTTF(
      FontAwesome_compressed_data,  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay) - ImGui API takes pointer
      static_cast<int>(FontAwesome_compressed_size),
      font_size,
      &icon_config,
      icon_ranges);  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay) - ImGui API takes pointer

  if (icon_font == nullptr) {
    LOG_WARNING("Failed to merge FontAwesome icons - icons will not be available");
  }
}

bool LoadEmbeddedFont(ImGuiIO& io,
                     ImFontConfig& font_config,
                     float font_size,
                     const char* font_name,  // NOLINT(misc-unused-parameters) NOSONAR(cpp:S1172) - used in LOG_INFO/LOG_WARNING below (macro expansion)
                     const unsigned char* compressed_data,
                     int compressed_size) {
  font_config.FontDataOwnedByAtlas = false;
  ImFont* font = io.Fonts->AddFontFromMemoryCompressedTTF(
      compressed_data,
      compressed_size,
      font_size,
      &font_config,
      nullptr);

  if (font != nullptr) {
    io.FontDefault = font;
    MergeFontAwesomeIcons(io, font_size);
    LOG_INFO(std::string(font_name) + " font loaded successfully from embedded data");
    return true;
  }
  LOG_WARNING(std::string("Failed to load embedded ") + font_name + " font, falling back to default");
  font = io.Fonts->AddFontDefault();
  io.FontDefault = font;
  return false;
}

static bool IsPathNonEmpty(const char* path) {
  return path != nullptr && *path != '\0';
}

ImFont* TryLoadFontChain(ImFontAtlas* atlas,
                        const char* primary_path,
                        const char* fallback_path,
                        const char* secondary_fallback_path,
                        float font_size,
                        const ImFontConfig* font_config) {
  if (atlas == nullptr || font_config == nullptr) {
    return nullptr;
  }
  ImFont* font = nullptr;  // NOLINT(misc-const-correctness) NOSONAR(cpp:S125) - ImGui io.FontDefault expects non-const ImFont*
  if (IsPathNonEmpty(primary_path)) {
    font = atlas->AddFontFromFileTTF(primary_path, font_size, font_config, nullptr);
  }
  if (font == nullptr && IsPathNonEmpty(fallback_path)) {
    font = atlas->AddFontFromFileTTF(fallback_path, font_size, font_config, nullptr);
  }
  if (font == nullptr && IsPathNonEmpty(secondary_fallback_path)) {
    font = atlas->AddFontFromFileTTF(secondary_fallback_path, font_size, font_config, nullptr);
  }
  return font;
}


}  // namespace font_utils
