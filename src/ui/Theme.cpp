#include <algorithm>
#include <string_view>

#include "imgui.h"
#include "ui/Theme.h"

namespace ui {

// --- Static Color storage (current theme; updated by Apply) ---
// Initial values match Default Dark palette (opaque, neutral gray, visible border)
// NOLINTBEGIN(cert-err58-cpp,readability-magic-numbers) -- theme color constants; ImVec4 init
ImVec4 Theme::Colors::Surface = ImVec4(0.11F, 0.11F, 0.11F, 1.00F);
ImVec4 Theme::Colors::SurfaceHover = ImVec4(0.15F, 0.15F, 0.15F, 1.00F);
ImVec4 Theme::Colors::SurfaceActive = ImVec4(0.18F, 0.18F, 0.18F, 1.00F);
ImVec4 Theme::Colors::FrameBg = ImVec4(0.15F, 0.15F, 0.15F, 1.00F);
ImVec4 Theme::Colors::FrameBgHover = ImVec4(0.18F, 0.18F, 0.18F, 1.00F);
ImVec4 Theme::Colors::FrameBgActive = ImVec4(0.22F, 0.22F, 0.22F, 1.00F);
ImVec4 Theme::Colors::Border = ImVec4(0.38F, 0.38F, 0.38F, 1.00F);
ImVec4 Theme::Colors::Accent = ImVec4(0.00F, 0.48F, 0.80F, 1.00F);
ImVec4 Theme::Colors::AccentHover = ImVec4(0.10F, 0.55F, 0.85F, 1.00F);
ImVec4 Theme::Colors::AccentActive = ImVec4(0.00F, 0.40F, 0.70F, 1.00F);
ImVec4 Theme::Colors::Text = ImVec4(0.90F, 0.90F, 0.90F, 1.00F);
ImVec4 Theme::Colors::TextStrong = ImVec4(1.00F, 1.00F, 1.00F, 1.00F);
ImVec4 Theme::Colors::TextDim = ImVec4(0.60F, 0.60F, 0.60F, 1.00F);
ImVec4 Theme::Colors::TextDisabled = ImVec4(0.40F, 0.40F, 0.40F, 1.00F);
ImVec4 Theme::Colors::TextOnAccent = ImVec4(1.00F, 1.00F, 1.00F, 1.00F);

ImVec4 Theme::Colors::Success = ImVec4(0.35F, 0.65F, 0.35F, 1.00F);
ImVec4 Theme::Colors::Warning = ImVec4(0.85F, 0.65F, 0.20F, 1.00F);
ImVec4 Theme::Colors::Error = ImVec4(0.85F, 0.30F, 0.30F, 1.00F);
ImVec4 Theme::Colors::WindowBg = ImVec4(0.08F, 0.08F, 0.08F, 1.00F);
// NOLINTEND(cert-err58-cpp,readability-magic-numbers)

namespace {

// NOLINTBEGIN(readability-identifier-naming) -- internal aggregate; member order must match brace init
struct ThemePalette {
  ImVec4 surface_;
  ImVec4 surface_hover_;
  ImVec4 surface_active_;
  ImVec4 frame_bg_;
  ImVec4 frame_bg_hover_;
  ImVec4 frame_bg_active_;
  ImVec4 border_;
  ImVec4 accent_;
  ImVec4 accent_hover_;
  ImVec4 accent_active_;
  ImVec4 text_;
  ImVec4 text_strong_;
  ImVec4 text_dim_;
  ImVec4 text_disabled_;
  ImVec4 text_on_accent_;
  ImVec4 success_;
  ImVec4 warning_;
  ImVec4 error_;
  ImVec4 window_bg_;
};
// NOLINTEND(readability-identifier-naming)

// Default Dark: very dark backgrounds, clearer border, slightly stronger blue accent
// Default Dark: Neutral Pro Dark (VS Code style), opaque backgrounds
// NOLINTBEGIN(cert-err58-cpp,readability-magic-numbers) -- theme palette aggregate init
const ThemePalette kDefaultDark = {
  ImVec4(0.11F, 0.11F, 0.11F, 1.00F),  // Surface (#1C1C1C)
  ImVec4(0.15F, 0.15F, 0.15F, 1.00F),  // SurfaceHover (#262626)
  ImVec4(0.18F, 0.18F, 0.18F, 1.00F),  // SurfaceActive (#2D2D2D)
  ImVec4(0.15F, 0.15F, 0.15F, 1.00F),  // FrameBg (#262626)
  ImVec4(0.18F, 0.18F, 0.18F, 1.00F),  // FrameBgHover (#2D2D2D)
  ImVec4(0.22F, 0.22F, 0.22F, 1.00F),  // FrameBgActive (#383838)
  ImVec4(0.38F, 0.38F, 0.38F, 1.00F),  // Border: visible on neutral gray (avoids flat/washed look)
  ImVec4(0.00F, 0.48F, 0.80F, 1.00F),  // Accent (VS Code Blue #007ACC)
  ImVec4(0.10F, 0.55F, 0.85F, 1.00F),  // AccentHover
  ImVec4(0.00F, 0.40F, 0.70F, 1.00F),  // AccentActive
  ImVec4(0.90F, 0.90F, 0.90F, 1.00F),  // Text (#E6E6E6)
  ImVec4(1.00F, 1.00F, 1.00F, 1.00F),  // TextStrong: section headers (full white for clear hierarchy)
  ImVec4(0.60F, 0.60F, 0.60F, 1.00F),  // TextDim
  ImVec4(0.40F, 0.40F, 0.40F, 1.00F),  // TextDisabled
  ImVec4(1.00F, 1.00F, 1.00F, 1.00F),  // TextOnAccent: White on Blue (Contrast 4.8:1 - PASS)
  ImVec4(0.35F, 0.65F, 0.35F, 1.00F),  // Success
  ImVec4(0.85F, 0.65F, 0.20F, 1.00F),  // Warning
  ImVec4(0.85F, 0.30F, 0.30F, 1.00F),  // Error
  ImVec4(0.08F, 0.08F, 0.08F, 1.00F),  // WindowBg (#141414)
};
// NOLINTEND(cert-err58-cpp,readability-magic-numbers)

// NOLINTBEGIN(cert-err58-cpp,readability-magic-numbers)
// Dracula: very dark backgrounds, distinct border, accent hover with subtle pink (#ff79c6)
const ThemePalette kDracula = {
  ImVec4(0.106F, 0.112F, 0.144F, 0.80F),  // Surface
  ImVec4(0.160F, 0.166F, 0.202F, 0.90F),
  ImVec4(0.197F, 0.202F, 0.243F, 1.00F),
  ImVec4(0.150F, 0.154F, 0.189F, 1.00F),
  ImVec4(0.189F, 0.194F, 0.234F, 1.00F),
  ImVec4(0.234F, 0.240F, 0.280F, 1.00F),
  ImVec4(0.55F, 0.52F, 0.62F, 1.00F),     // Border more distinct
  ImVec4(0.741F, 0.576F, 0.976F, 1.00F),  // Accent #bd93f9
  ImVec4(0.88F, 0.65F, 0.98F, 1.00F),     // Hover: slight pink tint #ff79c6
  ImVec4(0.65F, 0.47F, 0.95F, 1.00F),
  ImVec4(0.973F, 0.973F, 0.949F, 1.00F),  // Text #f8f8f2
  ImVec4(1.00F, 1.00F, 0.98F, 1.00F),     // TextStrong
  ImVec4(0.72F, 0.72F, 0.78F, 1.00F),
  ImVec4(0.52F, 0.52F, 0.58F, 1.00F),
  ImVec4(0.106F, 0.112F, 0.144F, 1.00F),  // TextOnAccent: Dark for contrast on light Purple
  ImVec4(0.345F, 0.831F, 0.475F, 1.00F),
  ImVec4(0.976F, 0.749F, 0.000F, 1.00F),
  ImVec4(0.976F, 0.345F, 0.345F, 1.00F),
  ImVec4(0.106F, 0.112F, 0.144F, 1.00F),  // WindowBg
};

// Nord: very dark backgrounds, stronger Surface->Hover step, clearer frost border
const ThemePalette kNord = {
  ImVec4(0.122F, 0.138F, 0.170F, 0.80F),  // #2e3440
  ImVec4(0.170F, 0.187F, 0.232F, 0.90F),
  ImVec4(0.197F, 0.214F, 0.258F, 1.00F),
  ImVec4(0.157F, 0.174F, 0.219F, 1.00F),
  ImVec4(0.189F, 0.206F, 0.251F, 1.00F),
  ImVec4(0.226F, 0.242F, 0.288F, 1.00F),
  ImVec4(0.50F, 0.52F, 0.58F, 1.00F),     // Border more visible on cool gray
  ImVec4(0.533F, 0.753F, 0.816F, 1.00F),  // #88c0d0
  ImVec4(0.65F, 0.85F, 0.90F, 1.00F),
  ImVec4(0.43F, 0.67F, 0.73F, 1.00F),
  ImVec4(0.925F, 0.941F, 0.945F, 1.00F),  // #eceff4
  ImVec4(0.97F, 0.98F, 0.98F, 1.00F),     // TextStrong
  ImVec4(0.69F, 0.73F, 0.78F, 1.00F),
  ImVec4(0.51F, 0.56F, 0.61F, 1.00F),
  ImVec4(0.122F, 0.138F, 0.170F, 1.00F),  // TextOnAccent: Dark for contrast on light Cyan
  ImVec4(0.486F, 0.784F, 0.549F, 1.00F),
  ImVec4(0.929F, 0.769F, 0.490F, 1.00F),
  ImVec4(0.906F, 0.396F, 0.384F, 1.00F),
  ImVec4(0.122F, 0.138F, 0.170F, 1.00F),  // WindowBg
};

// One Dark: very dark backgrounds, warmer TextDim (#e5c07b tint), distinct border
const ThemePalette kOneDark = {
  ImVec4(0.106F, 0.118F, 0.138F, 0.80F),  // #282c34
  ImVec4(0.136F, 0.147F, 0.170F, 0.90F),
  ImVec4(0.160F, 0.170F, 0.194F, 1.00F),
  ImVec4(0.138F, 0.150F, 0.174F, 1.00F),
  ImVec4(0.170F, 0.182F, 0.206F, 1.00F),
  ImVec4(0.202F, 0.214F, 0.238F, 1.00F),
  ImVec4(0.50F, 0.52F, 0.58F, 1.00F),     // Border more distinct
  ImVec4(0.380F, 0.686F, 0.937F, 1.00F),  // #61afef
  ImVec4(0.51F, 0.77F, 0.96F, 1.00F),
  ImVec4(0.26F, 0.60F, 0.91F, 1.00F),
  ImVec4(0.925F, 0.925F, 0.902F, 1.00F),  // #abb2bf
  ImVec4(0.97F, 0.97F, 0.95F, 1.00F),     // TextStrong
  ImVec4(0.72F, 0.70F, 0.62F, 1.00F),     // TextDim warmer (#e5c07b tint)
  ImVec4(0.51F, 0.53F, 0.58F, 1.00F),
  ImVec4(0.106F, 0.118F, 0.138F, 1.00F),  // TextOnAccent: Dark for contrast on light Blue
  ImVec4(0.486F, 0.784F, 0.549F, 1.00F),
  ImVec4(0.898F, 0.753F, 0.482F, 1.00F),  // #e5c07b
  ImVec4(0.906F, 0.396F, 0.384F, 1.00F),
  ImVec4(0.106F, 0.118F, 0.138F, 1.00F),  // WindowBg
};

// Gruvbox Dark: very dark backgrounds, warmer border (brown), strong orange accent
// Gruvbox Dark Hard: sharper contrast, deeper background
const ThemePalette kGruvbox = {
  ImVec4(0.16F, 0.16F, 0.16F, 1.00F),     // Surface (#282828 is now Surface, was WindowBg)
  ImVec4(0.20F, 0.20F, 0.20F, 1.00F),     // SurfaceHover (#32302F)
  ImVec4(0.24F, 0.24F, 0.24F, 1.00F),     // SurfaceActive
  ImVec4(0.20F, 0.19F, 0.18F, 1.00F),     // FrameBg (#32302F)
  ImVec4(0.24F, 0.23F, 0.22F, 1.00F),     // FrameBgHover
  ImVec4(0.28F, 0.27F, 0.26F, 1.00F),     // FrameBgActive
  ImVec4(0.40F, 0.36F, 0.33F, 1.00F),     // Border (#665C54)
  ImVec4(0.996F, 0.502F, 0.098F, 1.00F),  // Accent (#fe8019)
  ImVec4(1.00F, 0.60F, 0.22F, 1.00F),     // AccentHover
  ImVec4(0.93F, 0.41F, 0.00F, 1.00F),     // AccentActive
  ImVec4(0.925F, 0.902F, 0.831F, 1.00F),  // Text (#ebdbb2)
  ImVec4(0.97F, 0.95F, 0.88F, 1.00F),     // TextStrong
  ImVec4(0.66F, 0.60F, 0.52F, 1.00F),     // TextDim (#a89984)
  ImVec4(0.57F, 0.51F, 0.45F, 1.00F),     // TextDisabled (#928374)
  ImVec4(0.11F, 0.12F, 0.13F, 1.00F),     // TextOnAccent: Dark (#1D2021)
  ImVec4(0.72F, 0.73F, 0.15F, 1.00F),     // Success (#b8bb26)
  ImVec4(0.98F, 0.75F, 0.18F, 1.00F),     // Warning (#fabd2f)
  ImVec4(0.98F, 0.29F, 0.20F, 1.00F),     // Error (#fb4934)
  ImVec4(0.11F, 0.12F, 0.13F, 1.00F),     // WindowBg (#1D2021 - Hard Dark)
};

// Everforest Dark Hard: deep black base (like Dracula), Everforest Hard palette; eliminates wash-out
// Uses official Everforest Hard bg_dim/bg0/bg1/bg2/bg3 with darker WindowBg for deeper black
const ThemePalette kEverforest = {
  ImVec4(0.118F, 0.137F, 0.149F, 1.00F),  // Surface (#1E2326 - Everforest Hard bg_dim)
  ImVec4(0.153F, 0.180F, 0.200F, 1.00F),  // SurfaceHover (#272E33 - bg0)
  ImVec4(0.180F, 0.220F, 0.235F, 1.00F),  // SurfaceActive (#2E383C - bg1)
  ImVec4(0.153F, 0.180F, 0.200F, 1.00F),  // FrameBg (#272E33)
  ImVec4(0.180F, 0.220F, 0.235F, 1.00F),  // FrameBgHover (#2E383C)
  ImVec4(0.216F, 0.255F, 0.271F, 1.00F),  // FrameBgActive (#374145)
  ImVec4(0.40F, 0.45F, 0.45F, 1.00F),     // Border: distinct on dark (Everforest grey1 tint)
  ImVec4(0.65F, 0.75F, 0.50F, 1.00F),     // Accent (#A7C080 - Green)
  ImVec4(0.72F, 0.80F, 0.58F, 1.00F),     // AccentHover (#B7CE90)
  ImVec4(0.55F, 0.65F, 0.40F, 1.00F),     // AccentActive (#8DA160)
  ImVec4(0.83F, 0.78F, 0.67F, 1.00F),     // Text (#D3C6AA)
  ImVec4(0.88F, 0.83F, 0.72F, 1.00F),     // TextStrong
  ImVec4(0.60F, 0.66F, 0.60F, 1.00F),     // TextDim (#9DA9A0)
  ImVec4(0.40F, 0.45F, 0.45F, 1.00F),     // TextDisabled
  ImVec4(0.10F, 0.12F, 0.13F, 1.00F),     // TextOnAccent: dark on pale green (#1A1E21)
  ImVec4(0.65F, 0.75F, 0.50F, 1.00F),     // Success (Same as Accent)
  ImVec4(0.86F, 0.74F, 0.50F, 1.00F),     // Warning (#DBBC7F)
  ImVec4(0.90F, 0.50F, 0.50F, 1.00F),     // Error (#E67E80)
  ImVec4(0.098F, 0.118F, 0.129F, 1.00F),  // WindowBg: deep black like Dracula, green tint (#191E21)
};

// Catppuccin Mocha: pastel palette, soft mauve/lavender accent; distinct from saturated themes
// Official palette: Base #1e1e2e, Surface 0/1/2, Mauve #cba6f7, Text #cdd6f4
const ThemePalette kCatppuccin = {
  ImVec4(0.192F, 0.196F, 0.267F, 1.00F),  // Surface (#313244 - Surface 0)
  ImVec4(0.271F, 0.278F, 0.353F, 1.00F),  // SurfaceHover (#45475a - Surface 1)
  ImVec4(0.345F, 0.357F, 0.439F, 1.00F),  // SurfaceActive (#585b70 - Surface 2)
  ImVec4(0.192F, 0.196F, 0.267F, 1.00F),  // FrameBg (#313244)
  ImVec4(0.271F, 0.278F, 0.353F, 1.00F),  // FrameBgHover (#45475a)
  ImVec4(0.345F, 0.357F, 0.439F, 1.00F),  // FrameBgActive (#585b70)
  ImVec4(0.424F, 0.439F, 0.525F, 1.00F),  // Border (#6c7086 - Overlay 0)
  ImVec4(0.796F, 0.651F, 0.969F, 1.00F),  // Accent (#cba6f7 - Mauve)
  ImVec4(0.961F, 0.761F, 0.906F, 1.00F),  // AccentHover (#f5c2e7 - Pink)
  ImVec4(0.65F, 0.50F, 0.92F, 1.00F),     // AccentActive (darker Mauve)
  ImVec4(0.804F, 0.839F, 0.957F, 1.00F),  // Text (#cdd6f4)
  ImVec4(0.88F, 0.90F, 0.98F, 1.00F),     // TextStrong
  ImVec4(0.729F, 0.761F, 0.871F, 1.00F),  // TextDim (#bac2de - Subtext 1)
  ImVec4(0.651F, 0.678F, 0.784F, 1.00F),  // TextDisabled (#a6adc8 - Subtext 0)
  ImVec4(0.118F, 0.118F, 0.180F, 1.00F),  // TextOnAccent: Base (#1e1e2e) on Mauve
  ImVec4(0.651F, 0.890F, 0.631F, 1.00F),  // Success (#a6e3a1)
  ImVec4(0.976F, 0.886F, 0.686F, 1.00F),  // Warning (#f9e2af)
  ImVec4(0.953F, 0.545F, 0.659F, 1.00F),  // Error (#f38ba8)
  ImVec4(0.118F, 0.118F, 0.180F, 1.00F),  // WindowBg (#1e1e2e - Base)
};
// NOLINTEND(cert-err58-cpp,readability-magic-numbers)

void CopyPaletteToColors(const ThemePalette& palette) {
  Theme::Colors::Surface = palette.surface_;
  Theme::Colors::SurfaceHover = palette.surface_hover_;
  Theme::Colors::SurfaceActive = palette.surface_active_;
  Theme::Colors::FrameBg = palette.frame_bg_;
  Theme::Colors::FrameBgHover = palette.frame_bg_hover_;
  Theme::Colors::FrameBgActive = palette.frame_bg_active_;
  Theme::Colors::Border = palette.border_;
  Theme::Colors::Accent = palette.accent_;
  Theme::Colors::AccentHover = palette.accent_hover_;
  Theme::Colors::AccentActive = palette.accent_active_;
  Theme::Colors::Text = palette.text_;
  Theme::Colors::TextStrong = palette.text_strong_;
  Theme::Colors::TextDim = palette.text_dim_;
  Theme::Colors::TextDisabled = palette.text_disabled_;
  Theme::Colors::TextOnAccent = palette.text_on_accent_;
  Theme::Colors::Success = palette.success_;
  Theme::Colors::Warning = palette.warning_;
  Theme::Colors::Error = palette.error_;
  Theme::Colors::WindowBg = palette.window_bg_;
}

const ThemePalette* ResolveThemeId(std::string_view theme_id) {
  if (theme_id == "dracula") {
    return &kDracula;
  }
  if (theme_id == "nord") {
    return &kNord;
  }
  if (theme_id == "one_dark") {
    return &kOneDark;
  }
  if (theme_id == "gruvbox") {
    return &kGruvbox;
  }
  if (theme_id == "everforest") {
    return &kEverforest;
  }
  if (theme_id == "catppuccin") {
    return &kCatppuccin;
  }
  return &kDefaultDark;  // "default_dark" or unknown
}

}  // namespace

void Theme::Apply(std::string_view theme_id, bool viewports_enabled) {
  ImGui::StyleColorsDark();
  // NOLINTNEXTLINE(cppcoreguidelines-init-variables) -- initialized by ResolveThemeId(theme_id)
  const ThemePalette* const palette = ResolveThemeId(theme_id);
  CopyPaletteToColors(*palette);
  SetupColors();
  SetupStyle(viewports_enabled);
}

void Theme::Apply(bool viewports_enabled) {
  Apply("default_dark", viewports_enabled);
}

ImVec4 Theme::AccentTint(float alpha) {
  const ImVec4& a = Colors::Accent;
  // NOLINTNEXTLINE(cppcoreguidelines-init-variables) -- initialized by (std::max)((std::min)(...))
  const float clamped = (std::max)(0.0F, (std::min)(1.0F, alpha));
  return {a.x, a.y, a.z, clamped};
}

void Theme::SetupColors() {
  ImGuiStyle& style = ImGui::GetStyle();
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) -- ImGui API uses C array
  ImVec4 (&colors)[ImGuiCol_COUNT] = style.Colors;  // NOSONAR(cpp:S5945) - ImGui::ImGuiStyle::Colors is C-style array; we cannot change external API

  colors[ImGuiCol_Text] = Colors::Text;
  colors[ImGuiCol_TextDisabled] = Colors::TextDisabled;

  colors[ImGuiCol_FrameBg] = Colors::FrameBg;
  colors[ImGuiCol_FrameBgHovered] = Colors::FrameBgHover;
  colors[ImGuiCol_FrameBgActive] = Colors::FrameBgActive;

  colors[ImGuiCol_WindowBg] = Colors::WindowBg;
  colors[ImGuiCol_ChildBg] = Colors::WindowBg;
  colors[ImGuiCol_PopupBg] = Colors::Surface;

  colors[ImGuiCol_Border] = Colors::Border;
  colors[ImGuiCol_BorderShadow] = ImVec4(0.00F, 0.00F, 0.00F, 0.00F);

  colors[ImGuiCol_TitleBg] = Colors::Surface;
  colors[ImGuiCol_TitleBgActive] = Colors::SurfaceHover;
  colors[ImGuiCol_TitleBgCollapsed] = Colors::Surface;

  colors[ImGuiCol_MenuBarBg] = Colors::Surface;

  colors[ImGuiCol_ScrollbarBg] = Colors::FrameBg;
  colors[ImGuiCol_ScrollbarGrab] = Colors::Border;          // Distinct from FrameBg for visibility
  colors[ImGuiCol_ScrollbarGrabHovered] = Colors::Accent;   // Clear feedback on hover
  colors[ImGuiCol_ScrollbarGrabActive] = Colors::AccentActive;  // Clear feedback when dragging

  colors[ImGuiCol_CheckMark] = Colors::Accent;  // Use Accent for checkmark for visibility
  colors[ImGuiCol_SliderGrab] = Colors::Accent;
  colors[ImGuiCol_SliderGrabActive] = Colors::AccentActive;

  // Buttons
  colors[ImGuiCol_Button] = Colors::Surface;
  colors[ImGuiCol_ButtonHovered] = Colors::SurfaceHover;
  colors[ImGuiCol_ButtonActive] = Colors::SurfaceActive;

  // Header (Selectable)
  colors[ImGuiCol_Header] = Colors::Surface;
  colors[ImGuiCol_HeaderHovered] = Colors::SurfaceHover;
  colors[ImGuiCol_HeaderActive] = Colors::SurfaceActive;

  // Tabs
  colors[ImGuiCol_Tab] = Colors::Surface;
  colors[ImGuiCol_TabHovered] = Colors::SurfaceHover;
  colors[ImGuiCol_TabActive] = Colors::SurfaceActive;
  colors[ImGuiCol_TabUnfocused] = Colors::Surface;
  colors[ImGuiCol_TabUnfocusedActive] = Colors::SurfaceActive;

  // Tables
  colors[ImGuiCol_TableHeaderBg] = Colors::FrameBgActive;  // Distinct from rows for header visibility
  colors[ImGuiCol_TableBorderStrong] = Colors::Border;
  colors[ImGuiCol_TableBorderLight] =
    ImVec4(Colors::Border.x, Colors::Border.y, Colors::Border.z, 0.50F);
  colors[ImGuiCol_TableRowBg] = Colors::WindowBg;
  colors[ImGuiCol_TableRowBgAlt] =
    ImVec4(Colors::Surface.x, Colors::Surface.y, Colors::Surface.z, 0.20F);  // Subtle pattern

  // Text Selection
  ImVec4 selection = Colors::Accent;
  selection.w = 0.35F;
  colors[ImGuiCol_TextSelectedBg] = selection;

  // Resize Grip
  colors[ImGuiCol_ResizeGrip] = Colors::SurfaceHover;
  colors[ImGuiCol_ResizeGripHovered] = Colors::Accent;
  colors[ImGuiCol_ResizeGripActive] = Colors::AccentActive;

  // Modal Dimming
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00F, 0.00F, 0.00F, 0.70F);
}

void Theme::SetupStyle(bool viewports_enabled) {
  ImGuiStyle& style = ImGui::GetStyle();

  constexpr float kFrameRounding = 8.0F;
  constexpr float kGrabRounding = 6.0F;
  style.FrameRounding = kFrameRounding;
  style.GrabRounding = kGrabRounding;
  style.FrameBorderSize = 1.0F;

  if (viewports_enabled) {
    style.WindowRounding = 0.0F;
    style.Colors[ImGuiCol_WindowBg].w = 1.0F;
  }
}

}  // namespace ui
