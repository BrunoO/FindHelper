# FontUtils_linux.cpp Deduplication Analysis

## Summary

Identified significant code duplication in `FontUtils_linux.cpp` that can be refactored using helper functions.

## Duplication Patterns Identified

### 1. Embedded Font Loading Pattern (3 occurrences)

**Before Refactoring:**
Three identical blocks for "Roboto Medium", "Cousine", and "Karla" (lines 110-169):
```cpp
if (family == "Roboto Medium") {
  font_config.FontDataOwnedByAtlas = false;
  ImFont *font = io.Fonts->AddFontFromMemoryCompressedTTF(
      RobotoMedium_compressed_data,
      RobotoMedium_compressed_size,
      font_size,
      &font_config,
      nullptr
  );

  if (font != nullptr) {
    io.FontDefault = font;
    LOG_INFO("Roboto-Medium font loaded successfully from embedded data");
    return;
  } else {
    LOG_WARNING("Failed to load embedded Roboto-Medium font, falling back to default");
    font = io.Fonts->AddFontDefault();
    io.FontDefault = font;
    return;
  }
}
// ... same pattern for Cousine and Karla
```

**Duplication Statistics:**
- **3 occurrences** of this 20-line pattern
- **~60 lines of repetitive code** that could be extracted

**Solution:**
Create a helper function `LoadEmbeddedFont()` that takes:
- Font name (for logging)
- Compressed data pointer
- Compressed size
- Font config reference
- Font size
- ImGuiIO reference

### 2. Font Path Finding with Fallback Chain Pattern (Multiple occurrences)

**Before Refactoring:**
Many font families use the same pattern of trying multiple fonts in sequence:
```cpp
if (family == "Consolas") {
  primary_font_path = FindFontPath("DejaVu Sans Mono");
  if (primary_font_path.empty()) {
    primary_font_path = FindFontPath("Liberation Mono");
  }
  if (primary_font_path.empty()) {
    primary_font_path = FindFontPath("Courier New");
  }
  fallback_font_path = FindFontPath("Ubuntu Mono");
  secondary_fallback_font_path = FindFontPath("Monospace");
}
```

**Duplication Statistics:**
- **Multiple occurrences** of the pattern: try primary, if empty try another, if empty try another
- **~10-15 lines per font family** with similar structure
- **Pattern repeated for:** Consolas, Segoe UI, Arial, Calibri, Verdana, Tahoma, Cascadia Mono, Lucida Console, Courier New

**Solution:**
Create a helper function `FindFontPathWithFallback()` that takes:
- Vector of font names to try in order
- Returns the first non-empty path found

Or create a struct to represent font family configuration with primary/fallback/secondary fallback lists.

### 3. Font Loading with Fallback Chain (Already reasonably clean)

The font loading pattern (lines 288-316) is already reasonably clean and doesn't need significant refactoring.

## Proposed Solution

### Helper Function 1: LoadEmbeddedFont

```cpp
static bool LoadEmbeddedFont(
    ImGuiIO& io,
    ImFontConfig& font_config,
    float font_size,
    const char* font_name,
    const unsigned char* compressed_data,
    int compressed_size) {
  font_config.FontDataOwnedByAtlas = false;
  ImFont* font = io.Fonts->AddFontFromMemoryCompressedTTF(
      compressed_data,
      compressed_size,
      font_size,
      &font_config,
      nullptr
  );

  if (font != nullptr) {
    io.FontDefault = font;
    LOG_INFO(std::string(font_name) + " font loaded successfully from embedded data");
    return true;
  } else {
    LOG_WARNING(std::string("Failed to load embedded ") + font_name + " font, falling back to default");
    font = io.Fonts->AddFontDefault();
    io.FontDefault = font;
    return false;
  }
}
```

### Helper Function 2: FindFontPathWithFallback

```cpp
static std::string FindFontPathWithFallback(const std::vector<std::string>& font_names) {
  for (const auto& font_name : font_names) {
    std::string path = FindFontPath(font_name);
    if (!path.empty()) {
      return path;
    }
  }
  return "";
}
```

## Expected Results

- **Code reduction:** ~60-80 lines of duplicate code eliminated
- **Maintainability:** Changes to embedded font loading logic only need to be made in one place
- **Readability:** Clearer intent with named helper functions
- **Consistency:** Same pattern used consistently across all embedded fonts

## Implementation Plan

1. Add `LoadEmbeddedFont()` helper function
2. Replace three embedded font blocks with calls to `LoadEmbeddedFont()`
3. Add `FindFontPathWithFallback()` helper function
4. Refactor font path finding to use the helper where appropriate
5. Test to ensure all fonts still load correctly
6. Verify no compilation warnings or errors
