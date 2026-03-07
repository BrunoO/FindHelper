# Drag-and-Drop Implementation Plan

## Feasibility Assessment

**Status: ✅ FEASIBLE**

Drag-and-drop from ImGui to external Windows applications (desktop, Explorer, other apps) is **fully feasible** using Windows OLE (Object Linking and Embedding) drag-and-drop APIs. This requires implementing COM interfaces that bridge ImGui's internal drag detection with Windows' system-wide drag-and-drop mechanism.

### Key Requirements

1. **Windows OLE Drag-and-Drop APIs**: Implement `IDropSource` and `IDataObject` COM interfaces
2. **ImGui Integration**: Detect drag start in ImGui and initiate OLE drag operation
3. **File Path Format**: Provide file paths in `CF_HDROP` format (standard Windows file drag format)
4. **COM Initialization**: Application already initializes COM (`CoInitialize` in `main_gui.cpp`)

### Technical Approach

- **ImGui's `BeginDragDropSource`**: Use to detect when user starts dragging a file item
- **Windows `DoDragDrop`**: Call when ImGui detects drag start to initiate system-wide drag operation
- **`IDataObject`**: Provide file paths in `CF_HDROP` format for Windows Explorer and other applications
- **`IDropSource`**: Handle drag operation lifecycle (continue/cancel, visual feedback)

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    ImGui UI Layer                            │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  RenderSearchResultsTable()                         │   │
│  │  - Detects mouse drag on file row                   │   │
│  │  - Calls BeginDragDropSource()                      │   │
│  └──────────────────────────────────────────────────────┘   │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│              DragDropManager (New Module)                   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  StartDragOperation()                               │   │
│  │  - Creates FileDropSource (IDropSource)              │   │
│  │  - Creates FileDataObject (IDataObject)             │   │
│  │  - Calls DoDragDrop()                               │   │
│  └──────────────────────────────────────────────────────┘   │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│              Windows OLE APIs                                │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  DoDragDrop()                                        │   │
│  │  - Manages drag operation                           │   │
│  │  - Communicates with drop targets                   │   │
│  │  - Returns drop result                             │   │
│  └──────────────────────────────────────────────────────┘   │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│         External Applications (Explorer, Desktop, etc.)     │
│  - Receives CF_HDROP data                                   │
│  - Performs file operation (copy, move, etc.)                │
└─────────────────────────────────────────────────────────────┘
```

---

## Implementation Plan

### Phase 1: Create Drag-and-Drop Infrastructure

#### 1.1 Create `DragDropManager.h` and `DragDropManager.cpp`

**Purpose**: Main interface for initiating drag-and-drop operations from ImGui.

**Key Functions**:
- `StartDragOperation(const std::vector<std::string>& filePaths, HWND hwnd) -> HRESULT`
  - Initiates OLE drag operation with given file paths
  - Returns `S_OK` on success, error code on failure

**Location**: New files in project root (similar to `FileOperations.h/cpp`)

**Dependencies**:
- Windows OLE headers (`oleidl.h`, `objidl.h`)
- COM initialization (already done in `main_gui.cpp`)

---

#### 1.2 Create `FileDropSource.h` and `FileDropSource.cpp`

**Purpose**: Implements `IDropSource` COM interface to manage drag operation lifecycle.

**Interface Methods** (required by `IDropSource`):
- `QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) -> HRESULT`
  - Determines if drag should continue, cancel, or complete
  - Returns `S_OK` (continue), `DRAGDROP_S_CANCEL`, or `DRAGDROP_S_DROP`
  
- `GiveFeedback(DWORD dwEffect) -> HRESULT`
  - Provides visual feedback during drag
  - Returns `DRAGDROP_S_USEDEFAULTCURSORS` to use default Windows cursors

**Implementation Notes**:
- Use `IUnknown` base class for reference counting
- Handle escape key and right-click to cancel drag
- Support left-click drag (copy) and Ctrl+left-click (move)

**Location**: New files in project root

---

#### 1.3 Create `FileDataObject.h` and `FileDataObject.cpp`

**Purpose**: Implements `IDataObject` COM interface to provide file data in `CF_HDROP` format.

**Interface Methods** (required by `IDataObject`):
- `GetData(FORMATETC* pFormatetc, STGMEDIUM* pmedium) -> HRESULT`
  - Provides data in requested format (CF_HDROP for files)
  
- `QueryGetData(FORMATETC* pFormatetc) -> HRESULT`
  - Checks if requested format is supported
  
- `GetCanonicalFormatEtc(FORMATETC* pFormatetcIn, FORMATETC* pFormatetcOut) -> HRESULT`
  - Returns canonical format equivalent
  
- `SetData(FORMATETC* pFormatetc, STGMEDIUM* pmedium, BOOL fRelease) -> HRESULT`
  - Not needed for drag source (read-only)
  
- `EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppenumFormatetc) -> HRESULT`
  - Enumerates supported formats (CF_HDROP)
  
- `DAdvise(FORMATETC* pFormatetc, DWORD advf, IAdviseSink* pAdvSink, DWORD* pdwConnection) -> HRESULT`
  - Not needed for drag source
  
- `DUnadvise(DWORD dwConnection) -> HRESULT`
  - Not needed for drag source
  
- `EnumDAdvise(IEnumSTATDATA** ppenumAdvise) -> HRESULT`
  - Not needed for drag source

**Key Implementation Details**:
- **CF_HDROP Format**: Use `DragQueryFile` structure format
  - Allocate `DROPFILES` structure + file path strings
  - File paths must be double-null-terminated wide strings
  - First path offset in `DROPFILES.pFiles` structure
  
- **Memory Management**: 
  - Allocate `HGLOBAL` memory for `STGMEDIUM.hGlobal`
  - Use `GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size)`
  - Caller (Windows) will free memory via `ReleaseStgMedium`

**Location**: New files in project root

---

### Phase 2: Integrate with ImGui

#### 2.1 Modify `RenderSearchResultsTable()` in `main_gui.cpp`

**Changes Required**:

1. **Detect Drag Start**: 
   - When user clicks and drags on a file row, call `ImGui::BeginDragDropSource()`
   - Set payload with file path (or row index to look up path)

2. **Initiate OLE Drag Operation**:
   - When `BeginDragDropSource()` returns true, call `DragDropManager::StartDragOperation()`
   - Pass file path(s) and window handle

3. **Visual Feedback**:
   - Show drag preview text in ImGui (e.g., "Dragging file...")
   - Windows will provide cursor feedback via `IDropSource::GiveFeedback()`

**Code Pattern**:
```cpp
// In RenderSearchResultsTable(), for each file row:
if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
    // Set ImGui payload (for internal use, if needed)
    ImGui::SetDragDropPayload("FILE_PATH", result.fullPath.c_str(), 
                              result.fullPath.length() + 1);
    
    // Show drag preview
    ImGui::Text("Dragging: %s", result.filename.c_str());
    
    // Start Windows OLE drag operation
    std::vector<std::string> paths = {result.fullPath};
    HRESULT hr = DragDropManager::StartDragOperation(paths, hwnd);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to start drag operation");
    }
    
    ImGui::EndDragDropSource();
}
```

**Considerations**:
- **Single vs. Multiple Files**: Start with single file drag, extend to multi-select later
- **Drag Threshold**: ImGui handles drag threshold automatically
- **Error Handling**: Log errors but don't block UI (drag may fail silently)

---

#### 2.2 Support Multi-File Drag (Optional Enhancement)

**Future Enhancement**: Allow dragging multiple selected files at once.

**Requirements**:
- Track selected rows in `GuiState` (already has `selectedRow`, could extend to `selectedRows`)
- When dragging, collect all selected file paths
- Pass vector to `StartDragOperation()`

**Implementation**:
- Modify `GuiState` to support multi-select (Ctrl+click, Shift+click)
- Update `RenderSearchResultsTable()` to handle multi-select
- Pass all selected paths to drag operation

---

### Phase 3: Testing and Refinement

#### 3.1 Test Scenarios

1. **Basic Drag to Desktop**:
   - Drag single file from results table to desktop
   - Verify file is copied/moved correctly

2. **Drag to Explorer Window**:
   - Drag file to open Explorer window
   - Verify file operation completes

3. **Drag to Other Applications**:
   - Test with applications that accept file drops (e.g., Notepad++, VS Code)
   - Verify file path is provided correctly

4. **Error Cases**:
   - Drag file that no longer exists (deleted during drag)
   - Drag to invalid location
   - Cancel drag (Escape key, right-click)

5. **Performance**:
   - Test with long file paths (>260 characters)
   - Test with many files (if multi-file drag implemented)

---

#### 3.2 Edge Cases to Handle

1. **File Deleted During Drag**:
   - Windows will handle this (returns error from drop target)
   - No special handling needed in source

2. **Invalid Paths**:
   - Validate paths before starting drag operation
   - Log warning and skip invalid paths

3. **COM Threading**:
   - Ensure drag operation runs on main thread (UI thread)
   - COM is already initialized in main thread

4. **Memory Leaks**:
   - Ensure proper COM reference counting
   - Use `AddRef()`/`Release()` correctly
   - Windows will free `STGMEDIUM` memory, but verify no leaks

---

## File Structure

```
USN_WINDOWS/
├── DragDropManager.h          (New - Main drag-and-drop interface)
├── DragDropManager.cpp        (New - Implementation)
├── FileDropSource.h           (New - IDropSource implementation)
├── FileDropSource.cpp         (New - Implementation)
├── FileDataObject.h           (New - IDataObject implementation)
├── FileDataObject.cpp         (New - Implementation)
├── main_gui.cpp               (Modified - Add drag detection)
└── docs/
    └── DRAG_AND_DROP_IMPLEMENTATION_PLAN.md  (This file)
```

---

## Dependencies

### Windows Libraries
- `ole32.lib` - OLE drag-and-drop APIs (already linked via `ShellContextUtils.cpp`)
- `shell32.lib` - Shell APIs (already linked)
- `uuid.lib` - COM interface IDs (may need to add)

### Headers
- `<oleidl.h>` - `IDropSource`, `IDataObject` interfaces
- `<objidl.h>` - `FORMATETC`, `STGMEDIUM` structures
- `<shlobj.h>` - Shell utilities (already included)

### COM Initialization
- Already initialized in `main_gui.cpp` via `CoInitialize(NULL)`
- No additional COM setup needed

---

## Implementation Steps (Detailed)

### Step 1: Create FileDropSource Implementation

1. Create `FileDropSource.h`:
   - Declare `FileDropSource` class inheriting from `IDropSource`
   - Implement `IUnknown` methods (`QueryInterface`, `AddRef`, `Release`)
   - Declare `IDropSource` methods (`QueryContinueDrag`, `GiveFeedback`)

2. Create `FileDropSource.cpp`:
   - Implement reference counting
   - Implement `QueryContinueDrag`: 
     - Cancel on Escape key
     - Cancel on right mouse button
     - Complete on left button release
   - Implement `GiveFeedback`: Return `DRAGDROP_S_USEDEFAULTCURSORS`

**Estimated Complexity**: Low-Medium (standard COM implementation)

---

### Step 2: Create FileDataObject Implementation

1. Create `FileDataObject.h`:
   - Declare `FileDataObject` class inheriting from `IDataObject`
   - Store file paths (wide strings) internally
   - Implement `IUnknown` methods

2. Create `FileDataObject.cpp`:
   - **Constructor**: Accept vector of file paths, convert to wide strings
   - **GetData**: 
     - Check format is `CF_HDROP`
     - Allocate `DROPFILES` structure + file path data
     - Format: `[DROPFILES struct][path1\0path2\0\0]` (double-null-terminated)
     - Set `STGMEDIUM.hGlobal` to allocated memory
   - **QueryGetData**: Return `S_OK` for `CF_HDROP`, `DV_E_FORMATETC` otherwise
   - **GetCanonicalFormatEtc**: Return `DATA_S_SAMEFORMATETC` for `CF_HDROP`
   - **EnumFormatEtc**: Create enumerator for `CF_HDROP` format
   - **Other methods**: Return `E_NOTIMPL` (not needed for drag source)

**Key Challenge**: Correctly format `DROPFILES` structure with wide string paths.

**Reference**: 
- [DROPFILES structure](https://docs.microsoft.com/en-us/windows/win32/api/shlobj_core/ns-shlobj_core-dropfiles)
- [CF_HDROP format](https://docs.microsoft.com/en-us/windows/win32/shell/clipboard#cf_hdrop)

**Estimated Complexity**: Medium (requires careful memory layout)

---

### Step 3: Create DragDropManager

1. Create `DragDropManager.h`:
   - Declare `StartDragOperation()` static function
   - Accept vector of file paths and window handle

2. Create `DragDropManager.cpp`:
   - Create `FileDropSource` instance
   - Create `FileDataObject` instance with file paths
   - Call `DoDragDrop(pDataObject, pDropSource, dwEffect, &dwResult)`
   - Handle return value and log result
   - Clean up COM objects

**Estimated Complexity**: Low (wrapper around Windows API)

---

### Step 4: Integrate with ImGui

1. Modify `RenderSearchResultsTable()` in `main_gui.cpp`:
   - Add drag detection for each file row
   - Call `DragDropManager::StartDragOperation()` when drag starts
   - Handle errors gracefully

2. Test with single file drag

**Estimated Complexity**: Low (straightforward integration)

---

## Code Examples

### Example: FileDropSource::QueryContinueDrag

```cpp
HRESULT STDMETHODCALLTYPE FileDropSource::QueryContinueDrag(
    BOOL fEscapePressed, 
    DWORD grfKeyState) 
{
    // Cancel on Escape key
    if (fEscapePressed) {
        return DRAGDROP_S_CANCEL;
    }
    
    // Cancel on right mouse button
    if (grfKeyState & MK_RBUTTON) {
        return DRAGDROP_S_CANCEL;
    }
    
    // Complete drag if left button released
    if (!(grfKeyState & MK_LBUTTON)) {
        return DRAGDROP_S_DROP;
    }
    
    // Continue drag
    return S_OK;
}
```

### Example: FileDataObject::GetData (CF_HDROP)

```cpp
HRESULT STDMETHODCALLTYPE FileDataObject::GetData(
    FORMATETC* pFormatetc, 
    STGMEDIUM* pmedium) 
{
    if (pFormatetc->cfFormat != CF_HDROP) {
        return DV_E_FORMATETC;
    }
    
    // Calculate total size needed
    size_t totalSize = sizeof(DROPFILES);
    for (const auto& path : filePaths_) {
        totalSize += (path.length() + 1) * sizeof(wchar_t);
    }
    totalSize += sizeof(wchar_t); // Final null terminator
    
    // Allocate memory
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, totalSize);
    if (!hGlobal) {
        return E_OUTOFMEMORY;
    }
    
    // Fill DROPFILES structure
    DROPFILES* pDropFiles = static_cast<DROPFILES*>(GlobalLock(hGlobal));
    pDropFiles->pFiles = sizeof(DROPFILES); // Offset to file list
    pDropFiles->fWide = TRUE; // Wide character strings
    
    // Copy file paths (double-null-terminated)
    wchar_t* pFiles = reinterpret_cast<wchar_t*>(
        reinterpret_cast<BYTE*>(pDropFiles) + sizeof(DROPFILES));
    
    for (const auto& path : filePaths_) {
        std::wstring wPath = Utf8ToWide(path);
        wcscpy_s(pFiles, (totalSize - sizeof(DROPFILES)) / sizeof(wchar_t), wPath.c_str());
        pFiles += wPath.length() + 1;
    }
    *pFiles = L'\0'; // Final null terminator
    
    GlobalUnlock(hGlobal);
    
    // Set up STGMEDIUM
    pmedium->tymed = TYMED_HGLOBAL;
    pmedium->hGlobal = hGlobal;
    pmedium->pUnkForRelease = nullptr;
    
    return S_OK;
}
```

---

## Testing Checklist

- [ ] Single file drag to desktop (copy operation)
- [ ] Single file drag to desktop (move operation with Ctrl+Shift)
- [ ] Single file drag to Explorer window
- [ ] Single file drag to text editor (VS Code, Notepad++)
- [ ] Cancel drag with Escape key
- [ ] Cancel drag with right mouse button
- [ ] Drag file with long path (>260 characters)
- [ ] Drag file with special characters in path
- [ ] Drag file that is deleted during drag (error handling)
- [ ] Verify no memory leaks (use Application Verifier or similar)
- [ ] Verify COM reference counting (all AddRef/Release balanced)

---

## Estimated Effort

- **FileDropSource**: 2-3 hours
- **FileDataObject**: 4-6 hours (most complex, requires careful memory management)
- **DragDropManager**: 1-2 hours
- **ImGui Integration**: 1-2 hours
- **Testing and Debugging**: 3-4 hours

**Total**: ~12-17 hours

---

## Risks and Mitigations

### Risk 1: COM Threading Issues
**Mitigation**: Ensure all drag operations run on main UI thread (already the case)

### Risk 2: Memory Leaks in DROPFILES Structure
**Mitigation**: 
- Use `GlobalAlloc` correctly
- Windows will free via `ReleaseStgMedium`, but verify with memory profiler
- Test with Application Verifier

### Risk 3: Path Encoding Issues
**Mitigation**: 
- Use wide strings (`std::wstring`) for file paths in `DROPFILES`
- Ensure proper UTF-8 to wide string conversion
- Test with non-ASCII characters

### Risk 4: Drag Operation Conflicts with ImGui
**Mitigation**: 
- Call `DoDragDrop` synchronously (blocks until drop completes)
- ImGui will handle mouse input correctly during drag
- Test drag from different parts of table (filename, path, etc.)

---

## Future Enhancements

1. **Multi-File Drag**: Support dragging multiple selected files
2. **Drag Preview Icon**: Custom drag image (advanced, requires `IDragSourceHelper`)
3. **Drag from Other Sources**: Accept files dragged into application (implement `IDropTarget`)
4. **Progress Feedback**: Show progress for large file operations (if move/copy takes time)

---

## References

- [IDropSource Interface](https://docs.microsoft.com/en-us/windows/win32/api/oleidl/nn-oleidl-idropsource)
- [IDataObject Interface](https://docs.microsoft.com/en-us/windows/win32/api/objidl/nn-objidl-idataobject)
- [DoDragDrop Function](https://docs.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-dodragdrop)
- [DROPFILES Structure](https://docs.microsoft.com/en-us/windows/win32/api/shlobj_core/ns-shlobj_core-dropfiles)
- [CF_HDROP Clipboard Format](https://docs.microsoft.com/en-us/windows/win32/shell/clipboard#cf_hdrop)
- [ImGui Drag and Drop](https://github.com/ocornut/imgui/wiki/Docking#drag-and-drop) (internal only)

---

## Conclusion

This implementation is **feasible and well-supported** by Windows APIs. The main complexity lies in correctly implementing the `IDataObject::GetData` method to format file paths in `CF_HDROP` format. The rest is standard COM interface implementation.

The implementation follows the existing codebase patterns:
- Separate utility modules (`FileOperations`, `ShellContextUtils`)
- COM usage (already used in `ShellContextUtils.cpp`)
- Windows Shell API integration
- Error logging via `Logger`

**Recommendation**: Proceed with implementation, starting with `FileDataObject` (most complex) and working outward to integration.
