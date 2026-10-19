# Linux File Operations Implementation

## Summary

✅ **Linux file operations implementation completed**

All core file operations have been implemented for Linux, providing feature parity with Windows and macOS.

---

## Implementation Details

### File: `FileOperations_linux.cpp`

**Location:** Root directory (same level as `FileOperations.cpp` and `FileOperations_mac.mm`)

**Functions Implemented:**

1. ✅ **`OpenFileDefault()`** - Opens files with default application
2. ✅ **`OpenParentFolder()`** - Opens parent directory in file manager
3. ✅ **`CopyPathToClipboard()`** - Copies path to clipboard (X11/Wayland)
4. ✅ **`DeleteFileToRecycleBin()`** - Moves files to Trash
5. ✅ **`ShowFolderPickerDialog()`** - Shows folder picker dialog

---

## Function Implementations

### 1. OpenFileDefault

**Implementation:**
- Uses `xdg-open` (primary method, works on most distributions)
- Fallbacks: `gio open` (GNOME), `kde-open` (KDE), `exo-open` (XFCE)
- Validates file existence before attempting to open
- Escapes shell special characters for security

**Code Pattern:**
```cpp
void OpenFileDefault(const std::string &full_path) {
  // Validate input
  // Check file exists
  // Try xdg-open, then fallbacks
  // Log errors on failure
}
```

**Dependencies:** `xdg-utils` package (usually pre-installed)

---

### 2. OpenParentFolder

**Implementation:**
- Extracts parent directory from file path
- Uses `xdg-open` to open parent directory in file manager
- Fallback to `gio open` if `xdg-open` fails
- Note: Linux file managers don't support selecting a specific file when opening a directory (unlike Windows/macOS), so we open the parent directory

**Code Pattern:**
```cpp
void OpenParentFolder(const std::string &full_path) {
  // Validate input
  // Get parent directory using std::filesystem
  // Use xdg-open on parent directory
  // Log errors on failure
}
```

**Dependencies:** `xdg-utils` package

---

### 3. CopyPathToClipboard

**Implementation:**
- Detects display server (X11 vs Wayland)
- **Wayland:** Uses `wl-clipboard` (`wl-copy` command)
- **X11:** Tries `xclip` first, falls back to `xsel`
- Escapes path for shell safety
- Logs appropriate warnings if clipboard tools are unavailable

**Code Pattern:**
```cpp
void CopyPathToClipboard(const std::string &full_path) {
  // Detect display server (WAYLAND_DISPLAY or DISPLAY env vars)
  // Wayland: Use wl-copy
  // X11: Try xclip, then xsel
  // Log errors/warnings
}
```

**Dependencies:**
- Wayland: `wl-clipboard` package
- X11: `xclip` or `xsel` package

**Note:** These are common utilities but may need to be installed on some distributions.

---

### 4. DeleteFileToRecycleBin

**Implementation:**
- Primary method: Uses `gio trash` (GNOME, most common)
- Fallback: Manual implementation following FreeDesktop.org Trash specification
- Creates trash directories if they don't exist (`$XDG_DATA_HOME/Trash/files` and `$XDG_DATA_HOME/Trash/info`)
- Creates `.trashinfo` file with metadata (path, deletion date)
- Handles filename conflicts (adds number suffix if file already exists in trash)
- Uses `std::filesystem::rename` to move file to trash

**Code Pattern:**
```cpp
bool DeleteFileToRecycleBin(const std::string &full_path) {
  // Validate input
  // Try gio trash first
  // Fallback to manual implementation:
  //   - Get XDG_DATA_HOME or default to ~/.local/share
  //   - Create Trash/files and Trash/info directories
  //   - Create .trashinfo file with metadata
  //   - Move file to trash (handle conflicts)
  // Return success/failure
}
```

**Dependencies:**
- Primary: `glib2` (for `gio` command, usually pre-installed on GNOME)
- Fallback: None (uses standard C++ filesystem library)

**Trash Specification:** Follows [FreeDesktop.org Trash Specification 1.0](https://specifications.freedesktop.org/trash-spec/trashspec-1.0.html)

---

### 5. ShowFolderPickerDialog

**Implementation:**
- Uses dialog utilities available on Linux
- Primary: `zenity` (GNOME, works on most distributions)
- Fallbacks: `kdialog` (KDE), `yad` (Yet Another Dialog)
- Reads selected path from command output
- Returns false if no dialog utility is available

**Code Pattern:**
```cpp
bool ShowFolderPickerDialog(std::string& out_path) {
  // Try zenity first
  // Fallback to kdialog
  // Fallback to yad
  // Read path from command output
  // Return success/failure
}
```

**Dependencies:**
- `zenity` (GNOME, usually pre-installed)
- `kdialog` (KDE, optional fallback)
- `yad` (optional fallback)

**Note:** A full GTK implementation would require linking against GTK libraries. The current implementation uses command-line dialog utilities which are more portable and don't require additional dependencies.

---

## Helper Functions

### Internal Namespace

All helper functions are in the `file_operations::internal` namespace:

1. **`ValidatePath()`** - Validates path input (non-empty, no null characters)
2. **`FileExists()`** - Checks if file exists using `stat()`
3. **`EscapeShellPath()`** - Escapes shell special characters (wraps in single quotes, escapes embedded quotes)
4. **`ExecuteCommand()`** - Executes shell command and returns exit code

---

## Security Considerations

1. **Path Escaping:** All paths are escaped before being passed to shell commands to prevent command injection
2. **Input Validation:** All functions validate input paths (non-empty, no null characters)
3. **File Existence:** Functions check file existence before operations where appropriate
4. **Error Handling:** Comprehensive error logging for debugging

---

## Dependencies Summary

| Function | Required Dependencies | Optional Dependencies |
|----------|----------------------|----------------------|
| `OpenFileDefault` | `xdg-utils` | `gio`, `kde-open`, `exo-open` |
| `OpenParentFolder` | `xdg-utils` | `gio` |
| `CopyPathToClipboard` | `xclip` or `xsel` (X11) or `wl-clipboard` (Wayland) | - |
| `DeleteFileToRecycleBin` | None (fallback works) | `glib2` (for `gio trash`) |
| `ShowFolderPickerDialog` | `zenity` | `kdialog`, `yad` |

**Note:** Most of these dependencies are commonly pre-installed on Linux distributions. Users may need to install clipboard utilities (`xclip`, `xsel`, or `wl-clipboard`) if not already present.

---

## CMake Integration

**File:** `CMakeLists.txt`

**Changes:**
- Added `FileOperations_linux.cpp` to Linux build section (`APPLE` section)
- Added UI components to Linux build (they were missing)
- Linux build now includes all necessary sources

**Location:** Lines 347-373 (Linux build section)

---

## Header Documentation Updates

**File:** `FileOperations.h`

**Changes:**
- Updated module description to mention Linux
- Added Linux API references section
- Updated function comments to mention Linux behavior
- Updated clipboard function comment to include Linux

---

## Testing Recommendations

### Manual Testing Checklist

- [ ] **OpenFileDefault:**
  - [ ] Test with various file types (text, image, PDF, etc.)
  - [ ] Test with files that have spaces in path
  - [ ] Test with files that have special characters in path
  - [ ] Verify fallbacks work if `xdg-open` is unavailable

- [ ] **OpenParentFolder:**
  - [ ] Test with files in various directory depths
  - [ ] Verify parent directory opens correctly
  - [ ] Test with files that have spaces/special characters

- [ ] **CopyPathToClipboard:**
  - [ ] Test on X11 (verify `xclip` or `xsel` works)
  - [ ] Test on Wayland (verify `wl-clipboard` works)
  - [ ] Verify path can be pasted in other applications
  - [ ] Test with paths containing spaces/special characters

- [ ] **DeleteFileToRecycleBin:**
  - [ ] Test with `gio trash` available (GNOME)
  - [ ] Test with `gio trash` unavailable (fallback)
  - [ ] Verify file appears in Trash
  - [ ] Verify `.trashinfo` file is created correctly
  - [ ] Test filename conflict handling
  - [ ] Test with files that have spaces/special characters

- [ ] **ShowFolderPickerDialog:**
  - [ ] Test with `zenity` available
  - [ ] Test with `kdialog` available (KDE)
  - [ ] Test with `yad` available
  - [ ] Verify selected path is returned correctly
  - [ ] Test cancellation (should return false)

---

## Known Limitations

1. **OpenParentFolder:** Linux file managers don't support selecting a specific file when opening a directory (unlike Windows/macOS). We open the parent directory, but the file won't be selected.

2. **ShowFolderPickerDialog:** Uses command-line dialog utilities instead of native GTK/Qt dialogs. A full native implementation would require linking against GTK or Qt libraries, which adds dependencies.

3. **Clipboard Dependencies:** Requires external utilities (`xclip`, `xsel`, or `wl-clipboard`). These are common but may need to be installed.

4. **Desktop Environment Variations:** Different desktop environments may have different default behaviors. The implementation tries to be desktop-agnostic by using standard tools (`xdg-open`, `gio`, etc.).

---

## Future Enhancements

1. **Native GTK Dialog:** Replace `zenity`/`kdialog` with native GTK file chooser (requires GTK dependency)

2. **Native Clipboard API:** Use X11/Wayland clipboard APIs directly instead of external utilities (requires X11/Wayland development libraries)

3. **File Selection in OpenParentFolder:** Investigate desktop-specific APIs for selecting files when opening directories (e.g., Nautilus, Dolphin APIs)

4. **Better Error Messages:** Provide user-facing error messages when dependencies are missing

---

## Code Quality

✅ **Follows Project Conventions:**
- Same structure and patterns as Windows/macOS implementations
- Input validation consistent with other platforms
- Error logging consistent with other platforms
- Thread safety: All functions designed for UI thread (same as Windows/macOS)

✅ **Security:**
- Path escaping to prevent command injection
- Input validation
- File existence checks

✅ **Portability:**
- Uses standard C++17 features (`std::filesystem`)
- Uses POSIX APIs where needed
- Desktop-agnostic where possible

---

## Conclusion

✅ **Linux file operations implementation is complete and ready for testing.**

All core file operations are implemented with appropriate fallbacks and error handling. The implementation follows the same patterns as Windows and macOS, ensuring consistency across platforms.

**Next Steps:**
1. Test on a Linux system
2. Install any missing dependencies if needed
3. Verify all operations work correctly
4. Consider future enhancements (native dialogs, clipboard APIs)

---

**Document Version:** 1.0  
**Last Updated:** 2025-12-28  
**Related Documents:**
- `docs/FILE_OPERATIONS_PARITY_ANALYSIS.md` - Windows/macOS parity analysis
- `docs/FILE_OPERATIONS_UI_USAGE_GAP_ANALYSIS.md` - UI usage analysis
- `docs/LINUX_PREPARATION_REFACTORINGS.md` - Linux preparation plan

