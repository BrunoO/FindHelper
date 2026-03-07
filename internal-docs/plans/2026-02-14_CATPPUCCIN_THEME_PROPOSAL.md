# Catppuccin Mocha Theme Proposal

**Date:** 2026-02-14  
**Status:** Proposal  
**License:** MIT (Catppuccin)

---

## Summary

Add **Catppuccin Mocha** as a seventh UI theme. It would be the first **pastel** theme in the set, with a soft, warm-but-cool aesthetic that is distinctly different from all existing themes.

---

## Current Theme Coverage

| Theme        | Dominant Character | Accent   | Aesthetic          |
|-------------|-------------------|----------|--------------------|
| Default Dark| Neutral gray      | Blue     | Professional, flat |
| Dracula     | Purple-gray       | Purple   | Saturated, bold    |
| Nord        | Cool frost gray   | Cyan     | Arctic, crisp      |
| One Dark    | Blue-gray         | Blue     | Atom-style         |
| Gruvbox     | Warm brown        | Orange   | Retro, earthy      |
| Everforest  | Green-tinted dark  | Green    | Natural, calm      |

**Gap:** No pastel or soft aesthetic. All current themes use saturated accents on neutral/cool/warm bases.

---

## Why Catppuccin Mocha?

1. **Distinct aesthetic** – Pastel palette (mauve, lavender, peach) instead of saturated colors. "Soothing" and "cozy" by design.
2. **Popular** – 18,000+ GitHub stars, widely used in VS Code, Neovim, terminals, etc.
3. **MIT licensed** – Same as other theme references.
4. **Well-documented** – Official palette at [catppuccin.com/palette](https://catppuccin.com/palette).
5. **No overlap** – Does not duplicate any existing theme’s character.

---

## Palette Reference (Catppuccin Mocha)

Values from the official palette; ImVec4 (0–1) would be derived for implementation.

| Role        | Hex       | Notes                          |
|-------------|-----------|--------------------------------|
| Base (WindowBg) | #1e1e2e | Dark lavender-gray            |
| Mantle      | #181825   | Slightly darker than Base     |
| Crust       | #11111b   | Deepest (optional for accents) |
| Surface 0   | #313244   | Surface / FrameBg             |
| Surface 1   | #45475a   | SurfaceHover                  |
| Surface 2   | #585b70   | SurfaceActive                 |
| Text        | #cdd6f4   | Light lavender                |
| Subtext 1    | #bac2de   | TextDim                       |
| Subtext 0    | #a6adc8   | TextDisabled                  |
| Blue (Accent) | #89b4fa | Primary accent (or Mauve)     |
| Mauve       | #cba6f7   | Alternative accent            |
| Pink        | #f5c2e7   | AccentHover option            |
| Red         | #f38ba8   | Error                         |
| Yellow      | #f9e2af   | Warning                       |
| Green       | #a6e3a1   | Success                       |

---

## Implementation Notes

- **Accent choice:** Mauve (#cba6f7) is Catppuccin’s signature color; Blue (#89b4fa) is a safe alternative if Mauve feels too close to Dracula.
- **TextOnAccent:** Use Base (#1e1e2e) or Mantle (#181825) for dark text on light accent.
- **Border:** Overlay 0 (#6c7086) or Surface 2 (#585b70) for clear edges.
- **Deep black:** Use Crust (#11111b) for WindowBg if a deeper base is preferred (similar to Everforest/Dracula).

---

## Alternative Considered: Rose Pine

**Rose Pine** (MIT) offers a muted rose/pine/iris palette. It is also distinct but less widely known than Catppuccin. Could be a future addition if a second “soft” theme is desired.

---

## Effort Estimate

- Add `kCatppuccin` palette in `Theme.cpp`
- Add `"catppuccin"` to `ResolveThemeId()` and Settings combo
- Add entry to `CREDITS.md`
- ~30 minutes

---

## Recommendation

Proceed with **Catppuccin Mocha** as the next theme. It fills the pastel/soft gap and aligns with the project’s existing theme strategy (popular, open source, MIT).
