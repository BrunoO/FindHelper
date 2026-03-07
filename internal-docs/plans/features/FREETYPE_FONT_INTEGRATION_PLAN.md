# FreeType Font Integration Plan

## Overview

This document outlines the plan to integrate FreeType for font rendering across all platforms (Windows, macOS, Linux) and include a bundled font in the repository to simplify installation and ensure consistent typography.

## Goals

1. **Replace stb_truetype with FreeType** for better font rendering quality
2. **Bundle a free, open-source font** in the repository
3. **Simplify installation** by removing dependency on system fonts
4. **Maintain cross-platform compatibility** (Windows, macOS, Linux)

## Recommended Font: Inter

**Inter** is recommended as the bundled font for the following reasons:

- **License**: SIL Open Font License (OFL) - allows embedding and redistribution
- **Quality**: Designed specifically for screen readability with excellent hinting
- **Coverage**: Supports 140+ languages with extensive Unicode coverage
- **Versions**: Available in multiple weights (Regular, Medium, Bold, etc.)
- **Modern**: Neo-grotesque design optimized for UI applications
- **Cross-platform**: Works identically on all platforms
- **Download**: Available from [GitHub - rsms/inter](https://github.com/rsms/inter)

**Alternative fonts** (if Inter doesn't meet requirements):
- **Roboto** (already in ImGui misc/fonts, Apache 2.0 license)
- **Open Sans** (SIL OFL, very popular)
- **Source Sans Pro** (SIL OFL, Adobe)

## Implementation Plan

### Phase 1: Add FreeType Dependency

#### 1.1 Windows - Add FreeType via vcpkg or FetchContent

**Option A: vcpkg (Recommended for Windows)**
```cmake
# In CMakeLists.txt, add vcpkg integration
if(WIN32)
    find_package(freetype CONFIG QUIET)
    if(NOT freetype_FOUND)
        # Fallback to FetchContent if vcpkg not available
        include(FetchContent)
        FetchContent_Declare(freetype
            GIT_REPOSITORY https://github.com/freetype/freetype.git
            GIT_TAG VER-2-13-2
        )
        FetchContent_MakeAvailable(freetype)
    endif()
endif()
```

**Option B: FetchContent (Cross-platform)**
```cmake
# Use FetchContent for all platforms
include(FetchContent)
FetchContent_Declare(freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG VER-2-13-2
)
set(FREETYPE_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(FREETYPE_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(freetype)
```

#### 1.2 macOS - Use Homebrew or FetchContent

**Option A: System package (if available)**
```cmake
if(APPLE)
    find_package(freetype QUIET)
    if(NOT freetype_FOUND)
        # Fallback to FetchContent
    endif()
endif()
```

**Option B: FetchContent (Recommended)**
- Use FetchContent for consistent builds across all platforms

#### 1.3 Linux - Use system package or FetchContent

**Option A: System package (preferred for Linux)**
```cmake
if(UNIX AND NOT APPLE)
    find_package(Freetype REQUIRED)
    # Update README.md to add libfreetype6-dev to apt-get install list
endif()
```

**Option B: FetchContent (fallback)**
- Use FetchContent if system package not found

### Phase 2: Add Font Files to Repository

#### 2.1 Create Font Directory Structure

```
resources/
  fonts/
    Inter/
      Inter-Regular.ttf
      Inter-Medium.ttf
      Inter-Bold.ttf
      OFL.txt (license file)
    README.md (font attribution)
```

#### 2.2 Download and Add Font Files

1. Download Inter font family from [GitHub releases](https://github.com/rsms/inter/releases)
2. Extract and copy required font files to `resources/fonts/Inter/`
3. Include `OFL.txt` license file
4. Add `README.md` with font attribution

#### 2.3 Update .gitignore (if needed)

Ensure font files are tracked:
```
# Don't ignore font files
!resources/fonts/**/*.ttf
!resources/fonts/**/*.otf
!resources/fonts/**/*.txt
```

### Phase 3: Update CMakeLists.txt

#### 3.1 Add FreeType Dependency

Add to CMakeLists.txt after existing dependencies:

```cmake
# FreeType for improved font rendering
include(FetchContent)
FetchContent_Declare(freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG VER-2-13-2
)
set(FREETYPE_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(FREETYPE_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(freetype)

# For Linux, also try system package first
if(UNIX AND NOT APPLE)
    find_package(Freetype QUIET)
    if(freetype_FOUND)
        # Use system FreeType
        set(FREETYPE_LIBRARIES ${FREETYPE_LIBRARIES})
    else()
        # Use FetchContent FreeType
        set(FREETYPE_LIBRARIES freetype)
    endif()
else()
    set(FREETYPE_LIBRARIES freetype)
endif()
```

#### 3.2 Add ImGui FreeType Source

Add to IMGUI_SOURCES (all platforms):

```cmake
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/misc/freetype/imgui_freetype.cpp  # Add this
    # ... existing backend files ...
)
```

#### 3.3 Link FreeType Library

Add to target_link_libraries for all platforms:

```cmake
target_link_libraries(find_helper PRIVATE
    # ... existing libraries ...
    ${FREETYPE_LIBRARIES}
)
```

#### 3.4 Add Font Resource Path

Add font directory to include paths or create a resource path variable:

```cmake
# Font resources path
set(FONT_RESOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts")
```

### Phase 4: Update ImGui Configuration

#### 4.1 Enable FreeType in imconfig.h

Create or update `imconfig.h` (or use `IMGUI_USER_CONFIG`):

```cpp
// Enable FreeType for better font rendering
#define IMGUI_ENABLE_FREETYPE
```

**Note**: If using a custom config file, add:
```cmake
target_compile_definitions(find_helper PRIVATE
    IMGUI_USER_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/src/imgui_config.h"
)
```

#### 4.2 Include FreeType Headers

Ensure FreeType headers are available:

```cmake
target_include_directories(find_helper PRIVATE
    # ... existing includes ...
    ${freetype_SOURCE_DIR}/include  # If using FetchContent
    # OR
    ${FREETYPE_INCLUDE_DIRS}        # If using find_package
)
```

### Phase 5: Refactor Font Loading Code

#### 5.1 Create Unified FontUtils

Create a new unified font utility that works across all platforms:

**File**: `src/platform/FontUtils.cpp` (platform-agnostic)
**Header**: `src/platform/FontUtils.h`

```cpp
// FontUtils.h
#pragma once

#include "core/Settings.h"
#include <string>

namespace FontUtils {
    // Get path to bundled font file
    std::string GetBundledFontPath(const std::string& font_family, const std::string& weight = "Regular");
    
    // Configure fonts using bundled font files
    void ConfigureFontsFromSettings(const AppSettings& settings);
    
    // Apply font settings (rebuild atlas)
    void ApplyFontSettings(const AppSettings& settings);
}
```

#### 5.2 Update Font Loading Logic

Replace platform-specific font path resolution with bundled font loading:

**Before** (Windows example):
```cpp
primary_font_path = "C:\\Windows\\Fonts\\consola.ttf";
```

**After** (all platforms):
```cpp
std::string font_path = FontUtils::GetBundledFontPath("Inter", "Regular");
if (font_path.empty()) {
    // Fallback to default
    font = io.Fonts->AddFontDefault();
} else {
    font = io.Fonts->AddFontFromFileTTF(font_path.c_str(), font_size, &font_config, nullptr);
}
```

#### 5.3 Update Font Configuration

Modify `ImFontConfig` settings for FreeType:

```cpp
ImFontConfig font_config;
font_config.FontDataOwnedByAtlas = false; // Using file-based fonts

// FreeType-specific settings (optional, FreeType handles hinting automatically)
// Note: Oversampling settings are less critical with FreeType
font_config.OversampleH = 3;  // Reduced from 5 (FreeType handles hinting better)
font_config.OversampleV = 1;  // Reduced from 3
font_config.RasterizerDensity = 1.0f;  // Standard density for FreeType
font_config.PixelSnapH = true;  // Still useful for crisp rendering
```

#### 5.4 Remove Platform-Specific Font Utilities

After unified implementation:
- Keep `FontUtils_win.cpp`, `FontUtils_mac.mm`, `FontUtils_linux.cpp` for backward compatibility or remove them
- Update all call sites to use unified `FontUtils::ConfigureFontsFromSettings()`

### Phase 6: Update Settings

#### 6.1 Update Default Font Family

In `Settings.cpp` or `Settings.h`, update default font:

```cpp
// Change default from "Consolas" or system-specific to "Inter"
constexpr const char* kDefaultFontFamily = "Inter";
```

#### 6.2 Update Font Family Options

Update font family dropdown in settings UI to use bundled fonts:

```cpp
// In SettingsWindow.cpp
const char* font_families[] = {
    "Inter",
    "Inter Medium",
    "Inter Bold",
    // Keep system fonts as fallback options if desired
};
```

### Phase 7: Testing

#### 7.1 Build Verification

Test builds on all platforms:
- **Windows**: Verify FreeType links correctly (vcpkg or FetchContent)
- **macOS**: Verify FreeType builds via FetchContent
- **Linux**: Verify system FreeType or FetchContent works

#### 7.2 Runtime Testing

1. **Font Loading**: Verify Inter font loads correctly
2. **Font Quality**: Compare rendering quality (FreeType vs stb_truetype)
3. **Font Scaling**: Test different font sizes
4. **Font Switching**: Test changing fonts in settings
5. **Fallback**: Test behavior when font file missing

#### 7.3 Cross-Platform Consistency

Verify font rendering is consistent across:
- Windows (DirectX 11)
- macOS (Metal)
- Linux (OpenGL 3)

### Phase 8: Documentation Updates

#### 8.1 Update README.md

Add FreeType to prerequisites:
- **Windows**: Note vcpkg option or automatic FetchContent
- **macOS**: Note automatic FetchContent
- **Linux**: Add `libfreetype6-dev` to apt-get install list

#### 8.2 Update BUILDING_ON_LINUX.md

Add FreeType to Linux build instructions:
```bash
sudo apt-get install -y libfreetype6-dev
```

#### 8.3 Add Font Attribution

Create `resources/fonts/README.md`:
```markdown
# Fonts

This directory contains bundled fonts for the application.

## Inter

- **Source**: [GitHub - rsms/inter](https://github.com/rsms/inter)
- **License**: SIL Open Font License (OFL)
- **License File**: `Inter/OFL.txt`

Inter is a typeface designed specifically for computer screens, with excellent readability and extensive language support.
```

## Implementation Order

1. **Phase 1**: Add FreeType dependency to CMakeLists.txt
2. **Phase 2**: Download and add Inter font files to repository
3. **Phase 3**: Update CMakeLists.txt with FreeType linking
4. **Phase 4**: Enable FreeType in imconfig.h
5. **Phase 5**: Create unified FontUtils and refactor font loading
6. **Phase 6**: Update settings defaults
7. **Phase 7**: Test on all platforms
8. **Phase 8**: Update documentation

## Benefits

1. **Better Font Quality**: FreeType provides superior hinting and rendering
2. **Consistent Typography**: Same font on all platforms
3. **Simplified Installation**: No dependency on system fonts
4. **Better Unicode Support**: FreeType handles complex scripts better
5. **Future-Proof**: Easy to add more fonts or support font features

## Potential Issues and Solutions

### Issue 1: FreeType Build Time
**Problem**: FetchContent may slow down initial builds
**Solution**: Use system packages where available (Linux), cache builds

### Issue 2: Font File Size
**Problem**: Font files add to repository size
**Solution**: Include only necessary weights (Regular, Medium, Bold), use Git LFS if needed

### Issue 3: License Compliance
**Problem**: Must include font license
**Solution**: Include `OFL.txt` and attribution in README

### Issue 4: Backward Compatibility
**Problem**: Existing users may have custom font settings
**Solution**: Keep system font fallback, migrate settings gracefully

## Migration Strategy

1. **Phase 1**: Add FreeType alongside stb_truetype (both available)
2. **Phase 2**: Default to FreeType + bundled font, keep system font fallback
3. **Phase 3**: Remove system font dependencies (optional, future)

## Files to Modify

### New Files
- `src/platform/FontUtils.cpp` (unified font loading)
- `src/platform/FontUtils.h`
- `resources/fonts/Inter/Inter-Regular.ttf`
- `resources/fonts/Inter/Inter-Medium.ttf`
- `resources/fonts/Inter/Inter-Bold.ttf`
- `resources/fonts/Inter/OFL.txt`
- `resources/fonts/README.md`
- `src/imgui_config.h` (if using custom config)

### Modified Files
- `CMakeLists.txt` (FreeType dependency, ImGui FreeType source)
- `external/imgui/imconfig.h` (or create `src/imgui_config.h`)
- `src/platform/windows/FontUtils_win.cpp` (refactor to use unified utils)
- `src/platform/macos/FontUtils_mac.mm` (refactor to use unified utils)
- `src/platform/linux/FontUtils_linux.cpp` (refactor to use unified utils)
- `src/core/Settings.cpp` (update default font)
- `src/ui/SettingsWindow.cpp` (update font options)
- `README.md` (add FreeType to prerequisites)
- `docs/BUILDING_ON_LINUX.md` (add libfreetype6-dev)

### Files to Remove (Optional)
- `src/platform/windows/FontUtils_win.cpp` (after migration)
- `src/platform/macos/FontUtils_mac.mm` (after migration)
- `src/platform/linux/FontUtils_linux.cpp` (after migration)

## Estimated Effort

- **Phase 1-3** (CMake/FreeType setup): 2-3 hours
- **Phase 4** (ImGui config): 30 minutes
- **Phase 5** (Code refactoring): 3-4 hours
- **Phase 6** (Settings update): 1 hour
- **Phase 7** (Testing): 2-3 hours
- **Phase 8** (Documentation): 1 hour

**Total**: ~10-12 hours

## Next Steps

1. Review and approve this plan
2. Download Inter font files
3. Start with Phase 1 (CMake integration)
4. Test incrementally on each platform
5. Update documentation as changes are made

