# UX Review: Existing UI Themes — Sharper and More Memorable

**Date:** 2026-02-14  
**Scope:** Theme.h / Theme.cpp (Default Dark, Dracula, Nord, One Dark, Gruvbox)  
**Goal:** Professional UX assessment and concrete improvements for clarity, hierarchy, and memorability.

---

## 1. Executive Summary

The current theme system has solid semantic naming and five distinct palettes. To make themes feel **sharper** and **more memorable**:

1. **Apply theme to all ImGui color slots** — Text, TitleBg, Scrollbar, and (if used) Tab are still using ImGui’s default dark style. Wiring them to Theme::Colors makes every part of the UI respect the chosen theme.
2. **Strengthen contrast and hierarchy** — Slightly more distinct borders, clearer hover/active steps, and a bit more accent saturation (within readability limits) improve affordance and recognition.
3. **Tighten palette identity** — Small tweaks per theme (e.g. warmer borders for Gruvbox, clearer frost steps for Nord) make each theme more recognizable.

---

## 2. Current State Assessment

### 2.1 What Works Well

- **Semantic color set** (Surface, FrameBg, Accent, Text, Success/Warning/Error, WindowBg) is clear and used consistently where UI code pushes colors.
- **Named themes** (Dracula, Nord, One Dark, Gruvbox) have recognizable references; palettes are broadly aligned with those references.
- **Single source of truth** — Apply(theme_id) copies a palette into Theme::Colors and into ImGui for FrameBg, Header, Button, Border, WindowBg.

### 2.2 Gaps (Why Themes Feel Less Sharp)

| Issue | Impact |
|-------|--------|
| **ImGuiCol_Text and ImGuiCol_TextDisabled not set** | Labels, inputs, and any widget using default text color keep ImGui’s built-in dark values. Theme only affects text where we explicitly PushStyleColor(ImGuiCol_Text, Theme::Colors::*). Result: inconsistent “theme feel.” |
| **Title bar colors not set** | TitleBg, TitleBgActive, TitleBgCollapsed stay at ImGui defaults. Window chrome doesn’t feel part of the palette. |
| **Scrollbar colors not set** | ScrollbarBg, ScrollbarGrab* use defaults. Scrollbars look generic across themes. |
| **Borders too close to background** | In several palettes, Border is only a small step from FrameBg. Edges of panels and inputs lack definition. |
| **Hover/active steps too subtle** | Some themes have very small luminance deltas between Surface and SurfaceHover (e.g. ~0.06). Hover feedback feels weak. |
| **Accent saturation** | Accents are safe but could be slightly more saturated (where contrast allows) to make each theme more memorable. |

---

## 3. Per-Theme UX Notes

### Default Dark

- **Character:** Neutral gray-blues, “IDE dark” feel.
- **Improvements:** Slightly deeper WindowBg for more depth; accent blue a touch more saturated so primary actions pop; border clearly lighter than FrameBg for panel definition.

### Dracula

- **Character:** Purple-gray background, purple accent (#bd93f9).
- **Improvements:** Border more distinct from FrameBg so panels read clearly; AccentHover can have a slight pink tint (#ff79c6) for a more “Dracula” hover feel while staying readable.

### Nord

- **Character:** Cold blue-grays, frost blue accent (#88c0d0).
- **Improvements:** Stronger step from Surface → SurfaceHover so headers/buttons have clearer feedback; border slightly brighter so it reads on the cool gray.

### One Dark

- **Character:** Warm dark gray, blue accent (#61afef), warm text (#abb2bf / #e5c07b).
- **Improvements:** TextDim slightly warmer (toward One Dark’s yellow) so secondary text feels more “One Dark”; keep blue accent; ensure border is distinct.

### Gruvbox Dark

- **Character:** Warm brown/dark bg (#282828, #3c3836), orange accent (#fe8019).
- **Improvements:** Border slightly warmer (brownish) to match the palette; Success (green #b8bb26) already on-theme; keep strong orange accent for memorability.

---

## 4. Recommended Implementation Changes

### 4.1 SetupColors() — Full Theme Coverage

In `Theme::SetupColors()`, after applying FrameBg, Header, Button, Border, WindowBg:

- Set **ImGuiCol_Text** = Colors::Text.
- Set **ImGuiCol_TextDisabled** = Colors::TextDisabled.
- Set **ImGuiCol_TitleBg** = Colors::Surface (or a dedicated TitleBg if added later).
- Set **ImGuiCol_TitleBgActive** = Colors::SurfaceHover.
- Set **ImGuiCol_TitleBgCollapsed** = Colors::Surface (slightly dimmed if desired; same as TitleBg is fine).
- Set **ImGuiCol_ScrollbarBg** = Colors::FrameBg (or Surface).
- Set **ImGuiCol_ScrollbarGrab** = Colors::SurfaceHover.
- Set **ImGuiCol_ScrollbarGrabHovered** = Colors::SurfaceActive.
- Set **ImGuiCol_ScrollbarGrabActive** = Colors::SurfaceActive.

Optional for future tab usage: Tab = Surface, TabHovered = SurfaceHover, TabActive = Accent or SurfaceActive.

### 4.2 Palette Tweaks (Theme.cpp)

- **Default Dark:** Slightly deeper WindowBg; Border ~0.45–0.50 luminance; Accent a bit more saturated.
- **Dracula:** Border more distinct; AccentHover with subtle pink tint.
- **Nord:** Larger Surface → SurfaceHover delta; Border a bit brighter.
- **One Dark:** TextDim slightly warmer (yellow tint); Border distinct.
- **Gruvbox:** Border warmer (brown); keep existing accent and Success.

All tweaks should preserve WCAG-compliant contrast for Text/TextDim/TextDisabled on backgrounds and for Success/Warning/Error.

### 4.3 Style (Already Good)

- FrameRounding = 8, GrabRounding = 6, FrameBorderSize = 1 are appropriate. No change required for this pass.

---

## 5. Out of Scope for This Pass

- Light themes.
- Theme-specific rounding (e.g. “crisp” Nord vs “soft” Gruvbox).
- New semantic tokens (e.g. TitleBg, Info) unless needed for the above mappings.
- Changing which widgets use Theme::Colors (that’s already correct where PushStyleColor is used).

---

## 6. Success Criteria

After implementation:

1. Changing theme in Settings makes **all** UI text, title bars, and scrollbars follow the selected palette.
2. Each theme has **clearer visual hierarchy** (borders, hover steps).
3. Each theme feels **more recognizable** (slightly stronger accent and palette-specific tweaks) while remaining readable and accessible.
