# FontUtils Deduplication Summary

## Related Files to Deduplicate

After deduplicating `FontUtils_linux.cpp`, we identified that the same duplication patterns exist in the Windows and macOS versions:

### 1. FontUtils_win.cpp (Windows)
**Status:** ❌ Not yet deduplicated

**Duplication Patterns:**
- **Embedded font loading** (3 occurrences): Same pattern as Linux - Roboto Medium, Cousine, Karla
  - Lines 94-157: 3 identical ~20-line blocks
  - Difference: Also calls `MergeFontAwesomeIcons(io, font_size)` after loading
  - ~60 lines of duplicate code

**Solution:**
- Extract `LoadEmbeddedFont()` helper function (similar to Linux)
- Include `MergeFontAwesomeIcons()` call in the helper or after it

### 2. FontUtils_mac.mm (macOS)
**Status:** ❌ Not yet deduplicated

**Duplication Patterns:**
- **Embedded font loading** (3 occurrences): Same pattern as Linux - Roboto Medium, Cousine, Karla
  - Lines 105-168: 3 identical ~20-line blocks
  - Difference: Also calls `MergeFontAwesomeIcons(io, font_size)` after loading
  - ~60 lines of duplicate code
- **Font path finding with fallback chain** (Multiple occurrences): Similar to Linux
  - Lines 173-254: Repetitive `if (primary_font_path.empty())` chains
  - Could use `FindFontPathWithFallback()` helper (similar to Linux)

**Solution:**
- Extract `LoadEmbeddedFont()` helper function (similar to Linux)
- Include `MergeFontAwesomeIcons()` call in the helper or after it
- Extract `FindFontPathWithFallback()` helper function (similar to Linux)

## Comparison with Linux Version

| File | Embedded Font Duplication | Fallback Chain Duplication | Status |
|------|---------------------------|----------------------------|--------|
| FontUtils_linux.cpp | ✅ Deduplicated | ✅ Deduplicated | ✅ Complete |
| FontUtils_win.cpp | ❌ Needs deduplication | N/A (hardcoded paths) | ❌ Pending |
| FontUtils_mac.mm | ❌ Needs deduplication | ❌ Needs deduplication | ❌ Pending |

## Expected Benefits

- **Consistency:** All three platform-specific font utilities will use the same helper patterns
- **Maintainability:** Changes to embedded font loading logic only need to be made in one place per platform
- **Code reduction:** ~60 lines of duplicate code eliminated per file
- **Cross-platform alignment:** Windows, macOS, and Linux versions will have similar structure

## Next Steps

1. Deduplicate `FontUtils_win.cpp` - Extract `LoadEmbeddedFont()` helper
2. Deduplicate `FontUtils_mac.mm` - Extract `LoadEmbeddedFont()` and `FindFontPathWithFallback()` helpers
