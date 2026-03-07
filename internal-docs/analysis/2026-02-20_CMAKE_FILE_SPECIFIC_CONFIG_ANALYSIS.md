# CMakeLists.txt File-Specific Configuration Analysis

**Date:** 2026-02-20

## Summary

All file-specific configurations in `CMakeLists.txt` were reviewed. **Necessary** ones were kept; **redundant** duplicate property sets for `StringSearchAVX2.cpp` were removed and standardized to a single place.

---

## 1. Configurations That Are Necessary (Kept)

### 1.1 `src/utils/StringSearchAVX2.cpp` — AVX2 only for this TU

- **What:** `set_source_files_properties(..., COMPILE_FLAGS "/arch:AVX2")` (MSVC) or `-mavx2` (GCC/Clang).
- **Why:** AVX2 must be enabled only for this translation unit so the rest of the app stays baseline and runs on non-AVX2 CPUs (runtime dispatch).
- **Standardizing to “all files”:** Not possible; enabling AVX2 globally would break compatibility.
- **Standardization done:** The MSVC property was set in **one place** (main Windows block). Duplicate `set_source_files_properties` in test targets (test_utils_obj, path_pattern_integration_tests, parallel_search_engine_tests, file_index_search_strategy_tests, settings_tests) were **removed**, since source file properties apply to every target that compiles that file.

### 1.2 `src/core/Application.cpp` — LANGUAGE OBJCXX (macOS)

- **What:** `set_source_files_properties(src/core/Application.cpp PROPERTIES LANGUAGE OBJCXX)`.
- **Why:** That file uses Objective-C headers on macOS; compiling it as ObjC++ is required.
- **Standardizing:** Would require splitting into e.g. `Application_mac.mm` and platform-specific source lists. Current approach (one file, LANGUAGE override) is valid and kept.

### 1.3 `GeminiApiUtils.cpp` in `coverage_all_sources` — LANGUAGE OBJCXX (Apple)

- **What:** `set_source_files_properties(GeminiApiUtils.cpp PROPERTIES LANGUAGE OBJCXX)` when `APPLE`.
- **Why:** That TU uses NSURLSession (ObjC) on macOS for the coverage executable.
- **Standardizing:** Same as above; necessary unless we introduce a separate `.mm` for macOS.

### 1.4 `MACOS_ICON_FILE` (e.g. `app_icon.icns`)

- **What:** `set_source_files_properties(..., MACOSX_PACKAGE_LOCATION "Resources")`.
- **Why:** Puts the icon in the app bundle’s Resources; required for the bundle layout.
- **Standardizing:** N/A (resource placement, not compile config).

---

## 2. Platform / Target Selection (Not “Unique File Config”)

- **GEMINI_API_HTTP_SRC:** Platform-specific source file (`GeminiApiHttp_win.cpp`, `_mac.mm`, etc.). Structural, not per-file flags.
- **ThreadUtils_win/mac/linux** in some tests: Platform-specific sources. Structural.
- **MftMetadataReader.cpp** (optional): Optional feature source. Structural.

These are standard CMake patterns, not special per-file configuration.

---

## 3. Changes Made (Standardization)

- **StringSearchAVX2.cpp (MSVC):** One `set_source_files_properties(src/utils/StringSearchAVX2.cpp PROPERTIES COMPILE_FLAGS "/arch:AVX2")` in the main Windows block; removed five duplicate blocks from test targets. Comment updated to state it applies to all targets that use this file.

---

## 4. Checklist

| File / config                         | Necessary? | Action                          |
|--------------------------------------|------------|----------------------------------|
| StringSearchAVX2.cpp AVX2            | Yes        | Kept; consolidated to one set   |
| Application.cpp LANGUAGE OBJCXX      | Yes        | Kept                            |
| GeminiApiUtils.cpp LANGUAGE OBJCXX   | Yes        | Kept                            |
| Icon MACOSX_PACKAGE_LOCATION         | Yes        | Kept                            |
| Duplicate AVX2 in test targets       | No         | Removed (rely on single set)     |
