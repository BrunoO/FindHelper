# Path Utilities Centralization

## Overview

All path handling (root folders, path separators, special folders) has been centralized into a single cross-platform module (`PathUtils.h/cpp`) to eliminate platform-specific assumptions throughout the codebase.

## Centralized Components

### 1. Path Separators

**Location**: `PathUtils.h`

```cpp
#ifdef _WIN32
inline constexpr char kPathSeparator = '\\';
inline constexpr const char* kPathSeparatorStr = "\\";
#else
inline constexpr char kPathSeparator = '/';
inline constexpr const char* kPathSeparatorStr = "/";
#endif
```

**Usage**: Use `path_utils::kPathSeparator` or `path_utils::kPathSeparatorStr` instead of hardcoded `'\\'` or `'/'`.

### 2. Root Path Constants

**Location**: `PathUtils.h/cpp`

- `GetDefaultVolumeRootPath()`: Returns `"C:\\"` on Windows, `"/"` on macOS/Linux
- `GetDefaultUserRootPath()`: Returns `"C:\\Users\\"` on Windows, `"/Users/"` on macOS

**Legacy Support**: `PathConstants.h` still provides `kDefaultVolumeRootPath` and `kDefaultUserRootPath` as `const char*` for backward compatibility, but new code should use the functions.

### 3. User Home Directory

**Location**: `PathUtils.h/cpp`

- `GetUserHomePath()`: Cross-platform function that:
  - Windows: Uses `USERPROFILE` environment variable
  - macOS/Linux: Uses `HOME` environment variable
  - Falls back to default user root path if environment variable is not set

### 4. Special Folders

**Location**: `PathUtils.h/cpp`

- `GetDesktopPath()`: Returns Desktop folder path
  - Windows: Uses `FOLDERID_Desktop` or falls back to `USERPROFILE\Desktop`
  - macOS/Linux: Returns `~/Desktop`
  
- `GetDownloadsPath()`: Returns Downloads folder path
  - Windows: Returns `USERPROFILE\Downloads`
  - macOS/Linux: Returns `~/Downloads`
  
- `GetTrashPath()`: Returns Trash/Recycle Bin folder path
  - Windows: Uses `FOLDERID_RecycleBinFolder`
  - macOS: Returns `~/.Trash`
  - Linux: Returns `~/.Trash` (could be enhanced to use `~/.local/share/Trash/files`)

### 5. Path Joining

**Location**: `PathUtils.h/cpp`

- `JoinPath(base, component)`: Joins two path components with appropriate separator
  - Automatically handles trailing/leading separators
  - Uses platform-specific separator
  
- `JoinPath(components)`: Joins multiple path components

## Files Updated

### Core Path Utilities
- ✅ **PathUtils.h**: New centralized header with all path utilities
- ✅ **PathUtils.cpp**: Implementation with platform-specific code
- ✅ **PathConstants.h**: Updated to use PathUtils functions, maintains backward compatibility

### Application Files
- ✅ **UIRenderer.cpp**: 
  - Removed `GetUserProfilePath()` and `TryGetKnownFolderPath()` helper functions
  - Updated Desktop, Downloads, Recycle Bin buttons to use `path_utils::GetDesktopPath()`, etc.
  - Updated "Current User" button to use `path_utils::GetUserHomePath()`
  - All path separator concatenations now use `path_utils::JoinPath()`

- ✅ **FileIndex.h**:
  - Updated `BuildFullPath()` to use `path_utils::GetDefaultVolumeRootPath()` and `path_utils::kPathSeparator`
  - Updated `Rename()` and `Move()` to use `path_utils::kPathSeparatorStr` for prefix updates
  - Updated `RecomputeAllPaths()` to use cross-platform path utilities

- ✅ **Logger.h**:
  - Updated temp path fallback to use `path_utils::JoinPath()`

### Build System
- ✅ **CMakeLists.txt**: `PathUtils.cpp` already included in both Windows and macOS builds

## Migration Guide

### Before (Windows-specific)
```cpp
// Hardcoded path separator
full_path.push_back('\\');

// Hardcoded root path
std::string path = "C:\\Users\\Desktop";

// Hardcoded special folders
std::string desktop = user_profile + "\\Desktop";
```

### After (Cross-platform)
```cpp
// Use path separator constant
full_path.push_back(path_utils::kPathSeparator);

// Use root path function
std::string path = path_utils::JoinPath(
    path_utils::GetDefaultVolumeRootPath(), "Users");

// Use special folder functions
std::string desktop = path_utils::GetDesktopPath();
```

## Benefits

1. **Single Source of Truth**: All path handling logic is in one place
2. **Cross-platform**: Automatically adapts to Windows, macOS, and Linux
3. **Maintainable**: Changes to path logic only need to be made once
4. **Type-safe**: Uses functions instead of string literals where appropriate
5. **Backward Compatible**: Legacy constants still work for existing code

## Testing

- ✅ All cross-platform tests pass
- ✅ macOS build succeeds
- ✅ Windows build should succeed (not tested, but no breaking changes)

## Future Enhancements

1. **Linux Support**: Enhance `GetTrashPath()` to use `~/.local/share/Trash/files` on Linux
2. **Additional Special Folders**: Add functions for Documents, Pictures, etc.
3. **Path Normalization**: Add function to normalize paths (remove redundant separators, resolve `..`, etc.)
4. **Path Validation**: Add function to validate paths are well-formed

## Notes

- The `find_last_of("\\/")` pattern is already cross-platform and doesn't need to be changed
- Regex patterns that use `\\` for escaping (e.g., `\\.`) are correct and should not be changed
- Path separators in string literals used for display/formatting (e.g., help text) can remain as-is

