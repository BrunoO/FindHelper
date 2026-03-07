# File Operations Parity Analysis: Windows vs macOS

## Executive Summary

**Status:** ✅ **macOS has feature parity with Windows for all core file operations**

All essential file operations are implemented on both platforms. The only differences are Windows-specific features (drag-and-drop, shell context menu) which are expected to be Windows-only and don't affect core functionality.

---

## Core File Operations Comparison

### ✅ 1. OpenFileDefault

**Windows Implementation:**
- Uses `ShellExecuteW` with "open" verb
- Falls back to "edit" verb if "open" fails
- Handles file associations automatically

**macOS Implementation:**
- Uses `NSWorkspace::openURL`
- Opens file with default application
- Handles file associations automatically

**Parity Status:** ✅ **PARITY** (macOS doesn't need "edit" fallback - different OS behavior)

**Notes:**
- Windows fallback to "edit" verb is Windows-specific behavior
- macOS `NSWorkspace::openURL` handles all file types correctly
- Both implementations validate paths and handle errors

---

### ✅ 2. OpenParentFolder

**Windows Implementation:**
- Uses `ILCreateFromPathW` + `SHOpenFolderAndSelectItems`
- Opens Explorer and selects the file

**macOS Implementation:**
- Uses `NSWorkspace::activateFileViewerSelectingURLs`
- Reveals file in Finder and selects it

**Parity Status:** ✅ **PARITY**

**Notes:**
- Both implementations work identically from user perspective
- Both validate paths and handle errors

---

### ✅ 3. CopyPathToClipboard

**Windows Implementation:**
- Uses `OpenClipboard`, `SetClipboardData` with `CF_UNICODETEXT`
- Requires `HWND` parameter (Windows API requirement)
- Must be called from UI thread

**macOS Implementation:**
- Uses `NSPasteboard::generalPasteboard`
- No window handle needed
- Must be called from main thread

**Parity Status:** ✅ **PARITY**

**Notes:**
- API signature differs (Windows needs HWND, macOS doesn't)
- Handled with `#ifdef _WIN32` in header
- Both implementations work identically from user perspective

---

### ✅ 4. DeleteFileToRecycleBin

**Windows Implementation:**
- Uses `SHFileOperationW` with `FO_DELETE` and `FOF_ALLOWUNDO`
- Moves file to Recycle Bin
- Returns detailed error codes

**macOS Implementation:**
- Uses `NSFileManager::trashItemAtURL`
- Moves file to Trash
- Returns NSError with localized descriptions

**Parity Status:** ✅ **PARITY**

**Notes:**
- Both implementations move files to platform-specific trash locations
- Both validate paths and handle errors
- Both return boolean success/failure

---

### ✅ 5. ShowFolderPickerDialog

**Windows Implementation:**
- Uses `IFileDialog` with `FOS_PICKFOLDERS` option
- Requires `HWND` parameter for parent window
- Returns selected folder path

**macOS Implementation:**
- Uses `NSOpenPanel` with directory selection
- No window handle needed (uses NSApp)
- Returns selected folder path
- Includes app activation logic for proper modal dialog display

**Parity Status:** ✅ **PARITY**

**Notes:**
- API signature differs (Windows needs HWND, macOS doesn't)
- Handled with `#ifdef _WIN32` in header
- macOS implementation includes additional app activation logic for better UX
- Both implementations work identically from user perspective

---

## Windows-Specific Features (Expected to be Windows-Only)

### ⚠️ 1. Drag-and-Drop (Windows Only)

**Windows Implementation:**
- `drag_drop::StartFileDragDrop()` in `DragDropUtils.cpp`
- Uses COM/OLE drag-and-drop APIs
- Enables dragging files from results table to Explorer/Desktop/etc.

**macOS Status:**
- ❌ **Not implemented** (expected - would require different API)

**Parity Status:** ⚠️ **NOT NEEDED** (Windows-specific feature)

**Notes:**
- Drag-and-drop is a Windows shell integration feature
- macOS would require different implementation (Cocoa drag-and-drop)
- Not essential for core functionality
- Can be added later if needed

**Usage:**
- Used in `ui/ResultsTable.cpp` for dragging files from results table
- Wrapped in `#ifdef _WIN32` - gracefully skipped on macOS

---

### ⚠️ 2. Shell Context Menu (Windows Only)

**Windows Implementation:**
- `ShowContextMenu()` in `ShellContextUtils.cpp`
- Shows Windows Explorer context menu (right-click menu)
- Uses Windows Shell API (`IContextMenu`)

**macOS Status:**
- ❌ **Not implemented** (expected - macOS doesn't have Windows shell)

**Parity Status:** ⚠️ **NOT NEEDED** (Windows-specific feature)

**Notes:**
- Context menu is Windows shell integration
- macOS has different context menu system (Cocoa)
- Not essential for core functionality
- Can be added later if needed

**Usage:**
- Used in `ui/ResultsTable.cpp` for right-click context menu
- Wrapped in `#ifdef _WIN32` - gracefully skipped on macOS

---

## Implementation Quality Comparison

### Code Quality

| Aspect | Windows | macOS | Notes |
|--------|---------|-------|-------|
| **Input Validation** | ✅ Yes | ✅ Yes | Both validate paths, check for nulls |
| **Error Handling** | ✅ Yes | ✅ Yes | Both log errors and return appropriate values |
| **Thread Safety** | ✅ Yes | ✅ Yes | Both require UI thread |
| **String Encoding** | ✅ UTF-8→UTF-16 | ✅ UTF-8→NSString | Both handle encoding correctly |
| **Documentation** | ✅ Yes | ✅ Yes | Both well-documented |

### Error Handling

**Windows:**
- Detailed error codes from `SHFileOperationW`
- Human-readable error messages via `GetFileOperationErrorString()`
- `ShellExecuteW` error codes documented

**macOS:**
- `NSError` objects with localized descriptions
- File existence checks before operations
- Clear error logging

**Parity Status:** ✅ **PARITY** (both handle errors appropriately)

---

## API Signature Differences

### CopyPathToClipboard

**Windows:**
```cpp
void CopyPathToClipboard(HWND hwnd, const std::string &full_path);
```

**macOS:**
```cpp
void CopyPathToClipboard(const std::string &full_path);
```

**Handled:** ✅ Yes - `#ifdef _WIN32` in header file

### ShowFolderPickerDialog

**Windows:**
```cpp
bool ShowFolderPickerDialog(HWND hwnd, std::string& out_path);
```

**macOS:**
```cpp
bool ShowFolderPickerDialog(std::string& out_path);
```

**Handled:** ✅ Yes - `#ifdef _WIN32` in header file

---

## Usage in UI Code

### ResultsTable.cpp

All file operations are used with proper platform guards:

```cpp
// Open parent folder (both platforms)
file_operations::OpenParentFolder(result.fullPath);

// Copy to clipboard (platform-specific signature)
#ifdef _WIN32
  file_operations::CopyPathToClipboard(static_cast<HWND>(native_window),
                                         result.fullPath);
#else
  file_operations::CopyPathToClipboard(result.fullPath);
#endif

// Context menu (Windows only)
#ifdef _WIN32
  ShowContextMenu(static_cast<HWND>(native_window), result.fullPath, pt);
#endif

// Drag-and-drop (Windows only)
#ifdef _WIN32
  drag_drop::StartFileDragDrop(drag_result.fullPath);
#endif
```

**Status:** ✅ **All operations properly guarded**

---

## Missing Features Analysis

### Features That Could Be Added (Optional)

1. **macOS Drag-and-Drop**
   - Would require Cocoa drag-and-drop implementation
   - Not essential for core functionality
   - Can be added if needed

2. **macOS Context Menu**
   - Would require Cocoa context menu implementation
   - Not essential for core functionality
   - Can be added if needed

3. **Windows "Edit" Fallback for macOS**
   - macOS doesn't need this - `NSWorkspace::openURL` handles all file types
   - Not needed

**Conclusion:** No essential features are missing. All core file operations have parity.

---

## Recommendations

### ✅ Current Status: Ready for Linux Implementation

All core file operations are implemented on both Windows and macOS with proper parity. The codebase is ready for Linux implementation following the same patterns.

### For Linux Implementation:

1. **Follow macOS Pattern:**
   - Use POSIX/standard APIs (like macOS does)
   - No window handle needed for clipboard/folder picker
   - Use `xdg-open` for file operations

2. **Implement All Core Operations:**
   - ✅ `OpenFileDefault` - Use `xdg-open`
   - ✅ `OpenParentFolder` - Use `xdg-open` with parent directory
   - ✅ `CopyPathToClipboard` - Use X11/Wayland clipboard
   - ✅ `DeleteFileToRecycleBin` - Use `gio trash` or manual implementation
   - ✅ `ShowFolderPickerDialog` - Use GTK file chooser

3. **Skip Windows-Specific Features:**
   - ⚠️ Drag-and-drop (can add later if needed)
   - ⚠️ Context menu (can add later if needed)

---

## Summary Table

| Operation | Windows | macOS | Linux (Future) | Parity |
|-----------|---------|-------|----------------|--------|
| **OpenFileDefault** | ✅ | ✅ | ⚠️ TODO | ✅ **PARITY** |
| **OpenParentFolder** | ✅ | ✅ | ⚠️ TODO | ✅ **PARITY** |
| **CopyPathToClipboard** | ✅ | ✅ | ⚠️ TODO | ✅ **PARITY** |
| **DeleteFileToRecycleBin** | ✅ | ✅ | ⚠️ TODO | ✅ **PARITY** |
| **ShowFolderPickerDialog** | ✅ | ✅ | ⚠️ TODO | ✅ **PARITY** |
| **Drag-and-Drop** | ✅ | ❌ | ❌ | ⚠️ **Windows-only** |
| **Context Menu** | ✅ | ❌ | ❌ | ⚠️ **Windows-only** |

**Overall Status:** ✅ **macOS has full parity with Windows for all core file operations**

---

## Conclusion

✅ **macOS file operations implementation is complete and has feature parity with Windows.**

All essential file operations are implemented on both platforms:
- Open file with default application
- Open parent folder and select file
- Copy path to clipboard
- Delete file to trash/recycle bin
- Show folder picker dialog

The only differences are Windows-specific features (drag-and-drop, shell context menu) which are expected to be Windows-only and don't affect core functionality.

**The codebase is ready for Linux implementation following the same patterns used for macOS.**

---

**Document Version:** 1.0  
**Last Updated:** 2025-12-28  
**Related Documents:**
- `docs/LINUX_PREPARATION_REFACTORINGS.md` - Linux implementation plan
- `FileOperations.h` - API declarations
- `FileOperations.cpp` - Windows implementation
- `FileOperations_mac.mm` - macOS implementation

