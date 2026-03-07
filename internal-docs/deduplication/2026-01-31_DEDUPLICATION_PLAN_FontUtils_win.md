# Deduplication Plan: `src/platform/windows/FontUtils_win.cpp`

**File:** `src/platform/windows/FontUtils_win.cpp`  
**Date:** 2026-01-31  
**Target:** Reduce repetitive branches and string building; optionally table-drive font mapping

---

## Analysis Summary

The file implements Windows-specific ImGui font configuration with:

- **Embedded font handling:** Three nearly identical branches for Roboto Medium, Cousine, Karla (each calls `LoadEmbeddedFont` and returns).
- **System font path mapping:** A long if-else chain (10 families + default) that only assigns three path pointers (`primary`, `fallback`, `secondary`).
- **Font load fallback chain:** Try primary → fallback → secondary → `AddFontDefault` (repeated pattern for system fonts).
- **String building:** `std::string(font_name) + "..."` used twice in `LoadEmbeddedFont` for log messages.

Deduplication focuses on **within-file** repetition. **Cross-platform synergies** with `FontUtils_linux.cpp` and `FontUtils_mac.mm` (MergeFontAwesomeIcons, LoadEmbeddedFont, FreeType constants, ImFontConfig setup, TryLoadFontChain) are covered in `2026-01-31_FONTUTILS_CROSS_PLATFORM_SYNERGIES.md`. Implementing shared `FontUtilsCommon` first maximizes impact; then apply the within-file phases below (tables + use of shared helpers).

---

## Identified Duplication Patterns

### 1. **Embedded font branches** (HIGH PRIORITY)

**Location:** Lines 134–149

**Duplicate pattern:** For each of "Roboto Medium", "Cousine", "Karla":

```cpp
} else if (family == "Family Name") {
  LoadEmbeddedFont(io, font_config, font_size, "Display-Name",
                   Xxx_compressed_data,
                   Xxx_compressed_size);
  return;
}
```

**Impact:** 3 branches × ~5 lines ≈ 15 lines of repetition.

**Solution:** Drive from a small table (array of `{ family_name, display_name, data, size }`) and loop; on match call `LoadEmbeddedFont` and return. Keeps a single `LoadEmbeddedFont` call site and makes adding embedded fonts a one-line table entry.

**Suggested structure:**

```cpp
struct EmbeddedFontEntry {
  const char* family_name;
  const char* display_name;
  const unsigned char* data;
  int size;
};
static const EmbeddedFontEntry kEmbeddedFonts[] = {
  {"Roboto Medium", "Roboto-Medium", RobotoMedium_compressed_data, RobotoMedium_compressed_size},
  {"Cousine", "Cousine", Cousine_compressed_data, Cousine_compressed_size},
  {"Karla", "Karla", Karla_compressed_data, Karla_compressed_size},
};
// In ConfigureFontsFromSettings: loop over kEmbeddedFonts; if family matches, LoadEmbeddedFont and return
```

---

### 2. **System font path mapping** (HIGH PRIORITY)

**Location:** Lines 154–196

**Duplicate pattern:** Many branches of the form:

```cpp
} else if (family == "Family") {
  primary_font_path = "C:\\Windows\\Fonts\\primary.ttf";
  fallback_font_path = "C:\\Windows\\Fonts\\fallback.ttf";
  secondary_fallback_font_path = "C:\\Windows\\Fonts\\secondary.ttf";
}
```

**Impact:** ~10 families × ~4 lines ≈ 40 lines of repetitive assignments.

**Solution:** Table-driven mapping: a constant array of `{ family_name, primary, fallback, secondary }` (or a small struct). Look up by `family`; if found, set the three path pointers and break; else use default triple. Removes the long if-else chain and makes adding/changing families a single table row.

**Suggested structure:**

```cpp
struct SystemFontPaths {
  const char* family_name;
  const char* primary;
  const char* fallback;
  const char* secondary;
};
static const SystemFontPaths kSystemFontMap[] = {
  {"Consolas", "C:\\Windows\\Fonts\\consola.ttf", "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\arial.ttf"},
  {"Segoe UI", "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\consola.ttf", "C:\\Windows\\Fonts\\arial.ttf"},
  // ... one row per family ...
};
// Default row for unknown family
// Loop: find matching family, set primary_font_path etc., break
```

---

### 3. **Font load fallback chain** (MEDIUM PRIORITY)

**Location:** Lines 198–221

**Pattern:** Try primary path → if null try fallback → if null try secondary → if still null call `AddFontDefault()`. Then set `io.FontDefault` and log.

**Impact:** ~25 lines of sequential `AddFontFromFileTTF` and null checks.

**Solution:** Extract a helper that takes the three path pointers (or a small struct) and returns the loaded `ImFont*` (or null). Caller then does: `ImFont* font = TryLoadSystemFontChain(primary, fallback, secondary, font_size, &font_config);` and then the single if (font == nullptr) { AddFontDefault(); } else { io.FontDefault = font; } plus logging. Reduces nesting and makes the “try chain then default” logic reusable and testable.

**Suggested signature:**

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

---

### 4. **String building in LoadEmbeddedFont** (LOW PRIORITY)

**Location:** Lines 88, 91

**Duplicate pattern:**

- Success: `std::string(font_name) + " font loaded successfully from embedded data"`
- Failure: `std::string("Failed to load embedded ") + font_name + " font, falling back to default"`

**Impact:** Minor (two log lines), but uses temporary `std::string` where `std::string_view` or a small helper could avoid allocations if desired.

**Solution (optional):** Use a tiny helper that takes `font_name` and a success flag and returns the message string (or format via a single helper). Alternatively keep as-is and only refactor if log formatting is centralized later.

---

## Deduplication Phases

### Phase 1: Table-driven embedded fonts

**Goal:** Replace the three embedded-font if-else branches with a table and loop.

**Changes:**

- Add `EmbeddedFontEntry` (or similar) and `kEmbeddedFonts` table in the anonymous namespace.
- In `ConfigureFontsFromSettings`, loop over the table; when `family == entry.family_name`, call `LoadEmbeddedFont(io, font_config, font_size, entry.display_name, entry.data, entry.size)` and return.
- Remove the three `if (family == "Roboto Medium")` … `else if (family == "Karla")` blocks.

**Expected reduction:** ~10–15 lines; single place to add new embedded fonts.

**Risk:** Low; behavior unchanged.

---

### Phase 2: Table-driven system font paths

**Goal:** Replace the long if-else chain for system font path assignment with a table and lookup.

**Changes:**

- Add `SystemFontPaths` (or similar) and `kSystemFontMap` table plus a default row for unknown family.
- In `ConfigureFontsFromSettings`, loop (or use a small lookup helper) to find the row matching `family`; set `primary_font_path`, `fallback_font_path`, `secondary_fallback_font_path` from the row.
- Remove the 10+ if-else branches.

**Expected reduction:** ~35–40 lines; single place to add/change system font mappings.

**Risk:** Low; paths and fallbacks unchanged.

---

### Phase 3: Use shared font load chain (or extract local helper)

**Goal:** Isolate “try primary → fallback → secondary” into one function.

**Preferred (synergy):** Use shared `TryLoadFontChain()` from `FontUtilsCommon` (see `2026-01-31_FONTUTILS_CROSS_PLATFORM_SYNERGIES.md`). Windows then: resolve three paths from table, call `TryLoadFontChain(io.Fonts, ...)`, handle null (AddFontDefault + log) vs non-null (io.FontDefault = font + log), then `MergeFontAwesomeIcons(io, font_size)`.

**Alternative (Windows-only):** If shared code is not yet in place, add `TryLoadFontChain` in the anonymous namespace with the same signature; migrate to shared implementation later.

**Expected reduction:** Fewer lines in `ConfigureFontsFromSettings`, clearer control flow.

**Risk:** Low; logic unchanged.

---

### Phase 4: Optional log string cleanup (optional)

**Goal:** Avoid ad-hoc `std::string` concatenation in `LoadEmbeddedFont` if we want consistent, allocation-friendly logging.

**Changes:**

- Add a small helper (e.g. `FormatEmbeddedFontLogMessage(font_name, success)`) or use a single formatting path.
- Replace the two log lines in `LoadEmbeddedFont` with calls to it.

**Expected reduction:** Minimal line count; mainly clarity and consistency.

**Risk:** Very low.

---

## Implementation Order

**With synergies:** Implement `FontUtilsCommon` first (see `2026-01-31_FONTUTILS_CROSS_PLATFORM_SYNERGIES.md`); then apply Phases 1–2 (tables) and Phase 3 (use shared `TryLoadFontChain`, `LoadEmbeddedFont`, `MergeFontAwesomeIcons`, `SetupDefaultFontConfig`).

**Windows-only (if shared code is deferred):**

1. **Phase 1** – Table-driven embedded fonts (quick win, clear win).
2. **Phase 2** – Table-driven system font paths (largest reduction).
3. **Phase 3** – Font chain helper (local or shared).
4. **Phase 4** – Optional log string cleanup (only if desired).

---

## Testing Strategy

After each phase:

1. Build on Windows (primary target) and run the app.
2. In Settings, switch between:
   - Embedded: Roboto Medium, Cousine, Karla.
   - System: Consolas, Segoe UI, Arial, Calibri, Verdana, Tahoma, Cascadia Mono, Lucida Console, Courier New, and “unknown” (default).
3. Confirm font changes apply and no regressions (size, scale, FontAwesome merge).
4. Run project tests (e.g. `scripts/build_tests_macos.sh` on macOS if FontUtils is not Windows-only in the test build; otherwise run Windows test build).

---

## Related Files

- `src/platform/windows/FontUtils_win.h` – Declarations for `ConfigureFontsFromSettings`, `ApplyFontSettings`.
- `docs/deduplication/2026-01-31_DEDUPLICATION_PLAN_FontUtils_linux.md` – Linux plan; Linux already table-driven; synergies with shared FontUtilsCommon.
- `docs/deduplication/2026-01-31_FONTUTILS_CROSS_PLATFORM_SYNERGIES.md` – Shared extraction strategy (Win/Linux/mac): constants, SetupDefaultFontConfig, MergeFontAwesomeIcons, LoadEmbeddedFont, TryLoadFontChain.
- `docs/deduplication/DEDUPLICATION_RESEARCH_CrossPlatform.md` – General cross-platform approach (AppBootstrap, FileOperations).

---

## Notes

- All changes must remain inside `#ifdef _WIN32` and preserve Windows font paths (`C:\Windows\Fonts\...`).
- NOSONAR on the ImGui icon_ranges C-style array (line 51) should remain; table-driven refactors do not affect that.
- Using `std::string_view` for family comparison is acceptable (e.g. table holding `std::string_view` or comparing `family` string_view to `entry.family_name`); ensure C++17-only and project conventions (e.g. AGENTS document).
