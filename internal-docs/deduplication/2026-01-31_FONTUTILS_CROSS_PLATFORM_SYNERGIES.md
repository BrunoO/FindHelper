# FontUtils Cross-Platform Synergies (Win / Linux / macOS)

**Date:** 2026-01-31  
**Files:** `FontUtils_win.cpp`, `FontUtils_linux.cpp`, `FontUtils_mac.mm`  
**Goal:** Single shared implementation for identical logic; platform files keep only path resolution and backend invalidation.

---

## Executive Summary

All three FontUtils implementations share the same structure and large blocks of identical code:

- FreeType constants and ImFontConfig default setup
- `MergeFontAwesomeIcons`
- `LoadEmbeddedFont`
- “Try primary → fallback → secondary → AddFontDefault” chain
- High-level flow: Clear → setup config → embedded check → system paths → load chain → default/log → MergeFontAwesomeIcons

**Synergy:** Extract the identical parts into a common module (`FontUtilsCommon.h` / `FontUtilsCommon.cpp`). Each platform file then: (1) resolves three paths (or uses embedded), (2) calls shared helpers, (3) invalidates the platform backend. This removes roughly 80–100 lines of duplication per file and keeps font behavior consistent.

---

## Identical or Near-Identical Code

### 1. FreeType constants (all three files)

```cpp
constexpr int kFreeTypeOversampleH = 3;
constexpr int kFreeTypeOversampleV = 1;
constexpr float kFreeTypeRasterizerDensity = 1.0f;
```

**Action:** Move to `platform/FontUtilsCommon.h` (or a single shared constant block).

---

### 2. ImFontConfig default setup (all three files)

Same block in Win (108–127), Linux (212–231), Mac (165–184):

- `FontDataOwnedByAtlas = false`
- `PixelSnapH = true`
- `OversampleH` / `OversampleV` from constants
- `RasterizerDensity` from constant
- `GlyphMinAdvanceX = 0.0f`, `GlyphMaxAdvanceX = FLT_MAX`

**Action:** Extract to `void SetupDefaultFontConfig(ImFontConfig& config)` in `FontUtilsCommon.cpp`, declared in `FontUtilsCommon.h`. Platform code calls it once at the start of `ConfigureFontsFromSettings`.

---

### 3. MergeFontAwesomeIcons (all three files)

Identical logic: build `ImFontConfig` (MergeMode, FontDataOwnedByAtlas, GlyphMinAdvanceX, PixelSnapH), C-style `icon_ranges[]`, `AddFontFromMemoryCompressedTTF` with FontAwesome data, LOG_WARNING if null.

- Win/Linux: NOSONAR for icon_ranges (ImGui API requires C-style array).
- Mac: same code, NOSONAR can be added in shared version.

**Action:** Single implementation in `FontUtilsCommon.cpp`, declared in `FontUtilsCommon.h`. One NOSONAR in shared code. Platform files remove their local implementation and call the common one.

---

### 4. LoadEmbeddedFont (all three files)

Same signature and body:

- `FontDataOwnedByAtlas = false`
- `AddFontFromMemoryCompressedTTF(compressed_data, compressed_size, font_size, &font_config, nullptr)`
- If non-null: `io.FontDefault = font`, `MergeFontAwesomeIcons(io, font_size)`, LOG_INFO, return true
- Else: LOG_WARNING, `font = AddFontDefault()`, `io.FontDefault = font`, return false

**Action:** Single implementation in `FontUtilsCommon.cpp`, declared in `FontUtilsCommon.h`. Platform code passes `io`, `font_config`, `font_size`, `font_name`, and the platform’s embedded font data/size (or use a small platform-agnostic table of font name → data/size if we want to centralize embedded font list). For minimal change, keep embedded font **data** in platform files (or in existing EmbeddedFont_*.h) and pass pointers into shared `LoadEmbeddedFont`.

---

### 5. Font load chain (all three files)

Same pattern:

- Try primary path; if null, try fallback; if null, try secondary.
- If still null: `font = AddFontDefault()`, log “Using default ImGui font”.
- Else: `io.FontDefault = font`, log “Unicode font loaded successfully...”.
- Then call `MergeFontAwesomeIcons(io, font_size)`.

Windows: `const char*` (nullptr = skip). Linux/mac: `std::string` and `.c_str()` (empty = skip). So a shared helper can take `const char* primary, fallback, secondary` (nullptr or empty string = skip).

**Action:** Extract to:

```cpp
// Returns first successfully loaded font, or nullptr if all fail.
ImFont* TryLoadFontChain(
    ImFontAtlas* atlas,
    const char* primary_path,
    const char* fallback_path,
    const char* secondary_fallback_path,
    float font_size,
    const ImFontConfig* font_config);
```

in `FontUtilsCommon.cpp`. Each platform: resolve three paths (or get three `const char*`), call `TryLoadFontChain(io.Fonts, ...)`, then if null call `AddFontDefault()` and set `io.FontDefault`, else set `io.FontDefault = font`; log; then `MergeFontAwesomeIcons(io, font_size)`.

---

## Platform-Specific Parts (do not share)

| Responsibility | Win | Linux | Mac |
|----------------|-----|-------|-----|
| Embedded font table / if-else | Path table (or if-else) | `kEmbeddedFonts` map | if-else (can adopt table) |
| System font paths | Path strings in table | Fontconfig (`FindFontPath`, `FindFontPathWithFallback`) + `kSystemFontConfigs` | Core Text `FindFontPath` + if-else or table |
| Backend invalidation | `ImGui_ImplDX11_InvalidateDeviceObjects()` | `ImGui_ImplOpenGL3_DestroyDeviceObjects()` | `ImGui_ImplMetal_DestroyDeviceObjects()` |

Path **resolution** (how we get primary/fallback/secondary) stays per platform; the **loading** of those paths (TryLoadFontChain) is shared.

---

## Proposed Layout

### `platform/FontUtilsCommon.h`

- FreeType constants (`kFreeTypeOversampleH`, etc.).
- `void SetupDefaultFontConfig(ImFontConfig& config);`
- `void MergeFontAwesomeIcons(ImGuiIO& io, float font_size);`
- `bool LoadEmbeddedFont(ImGuiIO& io, ImFontConfig& font_config, float font_size, const char* font_name, const unsigned char* compressed_data, int compressed_size);`
- `ImFont* TryLoadFontChain(ImFontAtlas* atlas, const char* primary_path, const char* fallback_path, const char* secondary_fallback_path, float font_size, const ImFontConfig* font_config);`

### `platform/FontUtilsCommon.cpp`

- Include: `imgui.h`, `platform/EmbeddedFont_FontAwesome.h`, `utils/Logger.h`, `<cfloat>`, and (if LoadEmbeddedFont is shared) the other embedded font headers or accept data/size from caller only.
- Implements all four functions above.
- No `#ifdef _WIN32` / `__linux__` / `__APPLE__`; code is platform-agnostic (ImGui + font data only).
- NOSONAR for icon_ranges in `MergeFontAwesomeIcons` (ImGui API).

### CMakeLists.txt

- Add `FontUtilsCommon.cpp` to the same target(s) that build `FontUtils_win.cpp`, `FontUtils_linux.cpp`, and `FontUtils_mac.mm` (so it is compiled for Win, Linux, and macOS when building the app).

### Platform files after migration

- **Win:** Include `FontUtilsCommon.h`. Remove local constants, ImFontConfig block, `MergeFontAwesomeIcons`, `LoadEmbeddedFont`, inline load chain. Keep: path table (or adopt table from Windows plan), logic that picks three paths and calls `TryLoadFontChain`, null/non-null handling, `MergeFontAwesomeIcons(io, font_size)`, `ImGui_ImplDX11_InvalidateDeviceObjects()`.
- **Linux:** Same idea; keep `FindFontPath`, `FindFontPathWithFallback`, `kEmbeddedFonts`, `kSystemFontConfigs`; use shared helpers and `TryLoadFontChain`; keep `ImGui_ImplOpenGL3_DestroyDeviceObjects()`.
- **Mac:** Same idea; keep `FindFontPath`, `FindFontPathWithFallback` and system path logic; use shared helpers and `TryLoadFontChain`; keep `ImGui_ImplMetal_DestroyDeviceObjects()`.

---

## Implementation Order

1. **Add FontUtilsCommon** – Create `FontUtilsCommon.h` and `FontUtilsCommon.cpp` with constants, `SetupDefaultFontConfig`, `MergeFontAwesomeIcons`, `LoadEmbeddedFont`, `TryLoadFontChain`. Wire into build for all three platforms.
2. **Migrate Linux first** – Linux already uses tables; replace local implementations with calls to FontUtilsCommon. Validate on Linux.
3. **Migrate Windows** – Apply Windows deduplication plan (table-driven embedded + system paths) and switch to FontUtilsCommon. Validate on Windows.
4. **Migrate macOS** – Replace local implementations with FontUtilsCommon; optionally table-drive embedded and system fonts. Validate on macOS.

---

## Expected Impact

- **Lines removed per platform:** ~80–100 (constants, setup block, MergeFontAwesomeIcons, LoadEmbeddedFont, load chain).
- **New shared code:** ~120–150 lines in one place instead of ~250–300 duplicated across three files.
- **Consistency:** One place to fix bugs or change FreeType/ImGui font behavior.
- **Testing:** Same behavior; validate on each platform after migration.

---

## Related Docs

- `docs/deduplication/2026-01-31_DEDUPLICATION_PLAN_FontUtils_win.md` – Windows table-driven + shared code.
- `docs/deduplication/2026-01-31_DEDUPLICATION_PLAN_FontUtils_linux.md` – Linux plan + use of shared code.
- `docs/deduplication/DEDUPLICATION_RESEARCH_CrossPlatform.md` – General cross-platform approach (AppBootstrap, FileOperations).
