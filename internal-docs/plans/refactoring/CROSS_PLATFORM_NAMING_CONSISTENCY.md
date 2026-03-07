# Cross-Platform File Naming Consistency Analysis

## Summary

This document identifies cross-platform files that should be renamed with the `_win` postfix to maintain consistency with the existing naming convention used for platform-specific implementations.

## Current Naming Pattern

The project follows a consistent pattern for platform-specific files:
- **Windows**: `*_win.cpp/h` (e.g., `FontUtils_win.cpp`, `main_win.cpp`)
- **macOS**: `*_mac.mm/h` (e.g., `FontUtils_mac.mm`, `AppBootstrap_mac.mm`)
- **Linux**: `*_linux.cpp/h` (e.g., `FontUtils_linux.cpp`, `AppBootstrap_linux.cpp`)

## Files Requiring Rename

The following files have Windows-specific implementations but are missing the `_win` suffix:

### 1. `FileOperations.cpp` → `FileOperations_win.cpp`
**Evidence:**
- Contains Windows-specific code (`#ifdef _WIN32` blocks, Windows Shell APIs)
- Has corresponding platform-specific files:
  - `FileOperations_linux.cpp` (exists)
  - `FileOperations_mac.mm` (exists)
- Header file `FileOperations.h` is cross-platform (shared interface)

**Windows-specific code:**
- Uses `ShellExecuteW`, `SHFileOperationW`, `ILCreateFromPathW`, `SHOpenFolderAndSelectItems`
- Uses `IFileDialog` COM interface
- Includes `<windows.h>`, `<shellapi.h>`, `<shlobj_core.h>`, `<shobjidl.h>`

### 2. `AppBootstrap.cpp` → `AppBootstrap_win.cpp`
**Evidence:**
- Contains Windows-specific code (DirectX, COM initialization, Windows APIs)
- Has corresponding platform-specific files:
  - `AppBootstrap_linux.cpp` (exists)
  - `AppBootstrap_mac.mm` (exists)
- Header file `AppBootstrap.h` contains Windows-specific types (`HWND`, `AppBootstrapResult` with `com_initialized`)

**Windows-specific code:**
- Uses DirectX 11 (`DirectXManager`)
- COM/OLE initialization (`CoInitializeEx`, `CoUninitialize`)
- Administrator privilege checking
- Includes `<windows.h>`, `<ole2.h>`

### 3. `StringUtils.cpp` → `StringUtils_win.cpp`
**Evidence:**
- Contains Windows-specific code (Windows string conversion APIs)
- Has corresponding platform-specific files:
  - `StringUtils_linux.cpp` (exists)
  - `StringUtils_mac.cpp` (exists)
- Header file `StringUtils.h` is cross-platform (shared interface)

**Windows-specific code:**
- Uses `WideCharToMultiByte` and `MultiByteToWideChar` Windows APIs
- Includes `<windows.h>`

### 4. `ThreadUtils.cpp` → `ThreadUtils_win.cpp`
**Evidence:**
- Contains Windows-specific code (Windows thread naming APIs)
- Has corresponding platform-specific files:
  - `ThreadUtils_linux.cpp` (exists)
  - `ThreadUtils_mac.cpp` (exists)
- Header file `ThreadUtils.h` is cross-platform (shared interface)

**Windows-specific code:**
- Uses `SetThreadDescription` Windows API
- Uses `GetModuleHandleA`, `GetProcAddress` for dynamic loading
- Includes `<windows.h>`

## Files That Are Correctly Named

### Platform-Specific Files (Already Following Convention)
These files already follow the naming convention:
- ✅ `FontUtils_win.cpp/h` - Windows-specific font utilities
- ✅ `main_win.cpp` - Windows main entry point
- ✅ `GeminiApiHttp_win.cpp` - Windows-specific HTTP implementation
- ✅ `FileOperations_linux.cpp` - Linux-specific file operations
- ✅ `FileOperations_mac.mm` - macOS-specific file operations
- ✅ `AppBootstrap_linux.cpp/h` - Linux-specific bootstrap
- ✅ `AppBootstrap_mac.mm/h` - macOS-specific bootstrap
- ✅ `StringUtils_linux.cpp` - Linux-specific string utilities
- ✅ `StringUtils_mac.cpp` - macOS-specific string utilities
- ✅ `ThreadUtils_linux.cpp` - Linux-specific thread utilities
- ✅ `ThreadUtils_mac.cpp` - macOS-specific thread utilities

### Windows-Only Files (No Cross-Platform Versions)
These files are Windows-only features and don't need the `_win` suffix since they have no cross-platform counterparts:
- ✅ `DragDropUtils.cpp/h` - Windows drag-and-drop (Windows-only feature)
- ✅ `ShellContextUtils.cpp/h` - Windows shell context menu (Windows-only feature)
- ✅ `DirectXManager.cpp/h` - DirectX 11 rendering (Windows-only)
- ✅ `UsnMonitor.cpp/h` - USN Journal monitoring (Windows-only)

**Rationale:** These are Windows-only features that don't have macOS/Linux equivalents, so they don't need platform-specific naming. The `_win` suffix is reserved for files that are part of a cross-platform set.

### Cross-Platform Files with Conditional Compilation
These files are cross-platform but use conditional compilation (`#ifdef _WIN32`) in a single file rather than separate platform files:
- ✅ `PathUtils.cpp` - Cross-platform path utilities (single file with `#ifdef` blocks)
- ✅ `SearchResultUtils.cpp` - Cross-platform search result utilities (single file with `#ifdef` blocks)
- ✅ `TimeFilterUtils.cpp` - Cross-platform time filter utilities (single file with `#ifdef` blocks)
- ✅ `CpuFeatures.cpp` - Cross-platform CPU feature detection (single file with `#ifdef` blocks)

**Rationale:** These files use conditional compilation to support multiple platforms in a single file. They don't need the `_win` suffix because they're not part of a set of separate platform-specific files. The naming convention applies to files that have separate implementations per platform.

## Impact Assessment

### CMakeLists.txt Updates Required
All four files are referenced in `CMakeLists.txt` and will need to be updated:
- `FileOperations.cpp` → `FileOperations_win.cpp`
- `AppBootstrap.cpp` → `AppBootstrap_win.cpp`
- `StringUtils.cpp` → `StringUtils_win.cpp`
- `ThreadUtils.cpp` → `ThreadUtils_win.cpp`

### Include Statement Updates
Any files that include these headers may need updates if they reference the `.cpp` files directly (unlikely, but should be checked):
- `#include "FileOperations.h"` - No change needed (header is cross-platform)
- `#include "AppBootstrap.h"` - No change needed (header is cross-platform)
- `#include "StringUtils.h"` - No change needed (header is cross-platform)
- `#include "ThreadUtils.h"` - No change needed (header is cross-platform)

## Recommendation

Rename all four files to add the `_win` suffix for consistency. This will:
1. ✅ Make the platform-specific nature of these files immediately clear
2. ✅ Match the existing naming convention used for other platform-specific files
3. ✅ Improve code organization and maintainability
4. ✅ Make it easier to identify platform-specific implementations

## Files Checked But Don't Require Renaming

The following files were examined but determined to **not** require the `_win` suffix:

### Windows-Only Features
- `DragDropUtils.cpp` - Windows drag-and-drop feature (no macOS/Linux equivalent)
- `ShellContextUtils.cpp` - Windows shell context menu (no macOS/Linux equivalent)

### Cross-Platform Files with Conditional Compilation
- `PathUtils.cpp` - Single file with `#ifdef _WIN32` blocks (not separate platform files)
- `SearchResultUtils.cpp` - Single file with `#ifdef _WIN32` blocks (not separate platform files)
- `TimeFilterUtils.cpp` - Single file with `#ifdef _WIN32` blocks (not separate platform files)
- `CpuFeatures.cpp` - Single file with `#ifdef _WIN32` blocks (not separate platform files)

### Already Correctly Named
- `GeminiApiHttp_win.cpp` - Already has `_win` suffix ✅

## Implementation Order

1. **FileOperations.cpp** - Most visible, used by UI layer
2. **StringUtils.cpp** - Widely used utility
3. **ThreadUtils.cpp** - Less frequently used
4. **AppBootstrap.cpp** - Application entry point (may have more dependencies)



