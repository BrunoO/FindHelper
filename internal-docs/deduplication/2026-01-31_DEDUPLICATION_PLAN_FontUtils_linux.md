# Deduplication Plan: `src/platform/linux/FontUtils_linux.cpp`

**File:** `src/platform/linux/FontUtils_linux.cpp`  
**Date:** 2026-01-31  
**Target:** Within-file cleanup where useful; maximize cross-platform synergies with FontUtils_win and FontUtils_mac

---

## Analysis Summary

**Within-file status:** Linux is already in good shape. It uses:

- **Table-driven embedded fonts:** `kEmbeddedFonts` (unordered_map) + single lookup and `LoadEmbeddedFont` call (lines 165–176, 237–243). No if-else chain.
- **Table-driven system fonts:** `kSystemFontConfigs` (unordered_map) + lookup and path resolution via `FindFontPath` / `FindFontPathWithFallback` (lines 179–201, 251–264). No long if-else chain.

**Remaining within-file opportunities:**

1. **Font load chain** – The “try primary → fallback → secondary → AddFontDefault” block (lines 267–296) is inline. Extracting it to a helper would align with Windows/macOS and enable **cross-platform shared code** (see Synergies).
2. **Log string building** – Same as Windows: `LoadEmbeddedFont` uses `std::string(font_name) + "..."` twice (lines 154, 156). Optional cleanup; better addressed in shared code.

**Cross-platform:** Large overlap with `FontUtils_win.cpp` and `FontUtils_mac.mm`. Shared extraction (constants, ImFontConfig setup, `MergeFontAwesomeIcons`, `LoadEmbeddedFont`, `TryLoadFontChain`) is the main deduplication lever. See **Cross-Platform Synergies** below and `2026-01-31_FONTUTILS_CROSS_PLATFORM_SYNERGIES.md`.

---

## Within-File Deduplication

### 1. **Font load chain** (MEDIUM – best done via shared code)

**Location:** Lines 267–296

**Current pattern:** Inline try primary → fallback → secondary → if null then `AddFontDefault()` and log; else set `io.FontDefault` and log.

**Impact:** ~30 lines; same logic as Windows and macOS.

**Solution:** Prefer **shared** `TryLoadFontChain(ImFontAtlas*, const char* primary, const char* fallback, const char* secondary, float size, const ImFontConfig*)` in `FontUtilsCommon` (see Synergies). Linux then: resolve three `std::string` paths, call `TryLoadFontChain(io.Fonts, p1.empty() ? nullptr : p1.c_str(), ...)`, then handle null vs non-null (AddFontDefault + log vs io.FontDefault = font + log). No need for a Linux-only helper if the shared one is implemented.

---

### 2. **LoadEmbeddedFont log strings** (LOW)

**Location:** Lines 154, 156

Same as Windows: two log lines using `std::string(font_name) + "..."`. Optional; can be cleaned up in shared `LoadEmbeddedFont` if that is extracted (see Synergies).

---

## Cross-Platform Synergies

The following are **identical or nearly identical** across `FontUtils_win.cpp`, `FontUtils_linux.cpp`, and `FontUtils_mac.mm`:

| Item | Win | Linux | Mac | Shared? |
|------|-----|-------|-----|--------|
| FreeType constants (OversampleH/V, RasterizerDensity) | ✓ | ✓ | ✓ | **Yes** – `FontUtilsCommon.h` |
| ImFontConfig default setup block | ✓ | ✓ | ✓ | **Yes** – `SetupDefaultFontConfig()` |
| `MergeFontAwesomeIcons` | ✓ | ✓ | ✓ | **Yes** – `FontUtilsCommon.cpp` |
| `LoadEmbeddedFont` | ✓ | ✓ | ✓ | **Yes** – `FontUtilsCommon.cpp` |
| Font load chain (try primary → fallback → secondary) | ✓ | ✓ | ✓ | **Yes** – `TryLoadFontChain()` |
| Embedded font table | Win: if-else; Linux: table; Mac: if-else | - | - | Win/Mac adopt table; data stays per-platform or in common header |
| System font resolution | Win: path table; Linux: Fontconfig; Mac: Core Text | - | - | **No** – platform-specific |

**Recommended shared layout:**

- **`platform/FontUtilsCommon.h`**  
  - FreeType constants.  
  - `void SetupDefaultFontConfig(ImFontConfig&);`  
  - `bool LoadEmbeddedFont(ImGuiIO&, ImFontConfig&, float font_size, const char* font_name, const unsigned char* data, int size);`  
  - `ImFont* TryLoadFontChain(ImFontAtlas*, const char* primary, const char* fallback, const char* secondary, float size, const ImFontConfig*);`  
  - `void MergeFontAwesomeIcons(ImGuiIO&, float font_size);`  
  Declarations only; implementations in `FontUtilsCommon.cpp`.

- **`platform/FontUtilsCommon.cpp`**  
  - Built for all three platforms (no `#ifdef` for platform).  
  - Includes: `imgui.h`, embedded font headers (Roboto, Cousine, Karla, FontAwesome), `FontUtilsCommon.h`, `Logger.h`, `<cfloat>`.  
  - Implements: `SetupDefaultFontConfig`, `MergeFontAwesomeIcons`, `LoadEmbeddedFont`, `TryLoadFontChain`.  
  - `TryLoadFontChain`: for each of primary/fallback/secondary, if non-null and non-empty, call `AddFontFromFileTTF`; return first non-null font or nullptr.  
  - NOSONAR for the ImGui icon_ranges C-style array stays in the single shared `MergeFontAwesomeIcons`.

**Linux after shared extraction:**

- Remove: FreeType constants, full ImFontConfig setup block, `MergeFontAwesomeIcons`, `LoadEmbeddedFont`, and the inline font load chain.
- Keep: `FindFontPath`, `FindFontPathWithFallback`, `kEmbeddedFonts`, `kSystemFontConfigs`, and the logic that (1) looks up embedded font and calls shared `LoadEmbeddedFont` then returns, (2) looks up system config and resolves three paths, (3) calls `TryLoadFontChain(io.Fonts, ...)`, (4) handles null (AddFontDefault + log) vs non-null (io.FontDefault + log), (5) calls `MergeFontAwesomeIcons(io, font_size)`.
- Use: `SetupDefaultFontConfig(font_config)` at start of `ConfigureFontsFromSettings`.

**Synergy with Windows:** Windows plan (table-driven embedded + table-driven system paths + `TryLoadFontChain`) aligns with this: Windows gets tables and uses the same shared `LoadEmbeddedFont`, `TryLoadFontChain`, `MergeFontAwesomeIcons`, and `SetupDefaultFontConfig`. Linux already has tables; it only needs to switch to the shared implementations.

**Synergy with macOS:** macOS still has if-else for embedded and for system paths. It can (1) adopt shared code (constants, SetupDefaultFontConfig, MergeFontAwesomeIcons, LoadEmbeddedFont, TryLoadFontChain) and (2) optionally move to table-driven embedded + a table or structured config for system paths (mac-specific content), matching Linux/Win structure.

---

## Deduplication Phases (Linux)

### Phase 1: Introduce shared FontUtilsCommon (cross-platform)

**Goal:** Add `FontUtilsCommon.h` / `FontUtilsCommon.cpp` and use them from Linux (and then Win/mac).

**Steps:**

1. Add `platform/FontUtilsCommon.h` with constants, `SetupDefaultFontConfig`, `LoadEmbeddedFont`, `TryLoadFontChain`, `MergeFontAwesomeIcons` declarations.
2. Add `platform/FontUtilsCommon.cpp` with implementations (platform-agnostic; only ImGui + embedded font data).
3. In CMakeLists, compile `FontUtilsCommon.cpp` for Linux (and Windows/macOS when they are migrated).
4. In `FontUtils_linux.cpp`: remove local FreeType constants, ImFontConfig setup block, `MergeFontAwesomeIcons`, `LoadEmbeddedFont`, and the inline font load chain; include `FontUtilsCommon.h`; call `SetupDefaultFontConfig(font_config)`; use shared `LoadEmbeddedFont` for embedded path; resolve three paths and call `TryLoadFontChain(io.Fonts, ...)`; handle null/non-null and call `MergeFontAwesomeIcons(io, font_size)`.

**Expected reduction (Linux):** ~80–100 lines (constants, setup block, MergeFontAwesomeIcons, LoadEmbeddedFont, load chain). Same logic, single shared implementation.

**Risk:** Low if `FontUtilsCommon.cpp` is built and tested on Linux first, then Win/mac adopt it.

---

### Phase 2: Optional log message cleanup in shared LoadEmbeddedFont

If shared `LoadEmbeddedFont` is introduced, use a small helper or consistent formatting for the two log messages (success/failure) to avoid ad-hoc `std::string` concatenation. Low priority.

---

## Implementation Order (with Windows/macOS)

1. **Implement FontUtilsCommon (Phase 1)** – Add shared code and migrate **Linux first** (already table-driven; minimal logic change).
2. **Migrate Windows** – Apply Windows deduplication plan (table-driven embedded + table-driven system paths) and switch to FontUtilsCommon for constants, SetupDefaultFontConfig, LoadEmbeddedFont, TryLoadFontChain, MergeFontAwesomeIcons.
3. **Migrate macOS** – Switch to FontUtilsCommon; optionally table-drive embedded and system font config (mac-specific tables).

---

## Testing Strategy (Linux)

After Phase 1:

1. Build on Linux and run the app.
2. In Settings, switch between embedded fonts (Roboto Medium, Cousine, Karla) and system fonts (e.g. Consolas, Ubuntu, DejaVu Sans Mono).
3. Confirm font changes apply, FontAwesome icons render, and no regressions.
4. Run project tests (e.g. `scripts/build_tests_macos.sh` on macOS; on Linux run the test suite if available).

---

## Related Files

- `src/platform/linux/FontUtils_linux.h` – Declarations.
- `docs/deduplication/2026-01-31_DEDUPLICATION_PLAN_FontUtils_win.md` – Windows plan (table-driven + shared code).
- `docs/deduplication/2026-01-31_FONTUTILS_CROSS_PLATFORM_SYNERGIES.md` – Shared extraction strategy for Win/Linux/mac.
- `docs/deduplication/DEDUPLICATION_RESEARCH_CrossPlatform.md` – General cross-platform dedup approach (FileOperations, AppBootstrap).

---

## Notes

- Linux already uses tables; no need to “deduplicate” embedded/system branches inside this file. The gain is from **shared** code and from using `TryLoadFontChain` instead of inline chain.
- `FindFontPath` (fc-list) and `FindFontPathWithFallback` stay Linux-specific. Only the “load from three paths + default” logic is shared.
- NOSONAR for the icon_ranges C-style array in shared `MergeFontAwesomeIcons` should be documented (ImGui API requirement).
