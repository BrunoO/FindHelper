# Phase 2 Step 1: Architecture Options for Making UIRenderer Cross-Platform

## Problem Statement

`UIRenderer` currently has Windows-specific dependencies:
- **HWND parameter** in `RenderSearchResultsTable()` and `RenderPathColumnWithEllipsis()`
- **Windows-specific function calls**:
  - `ShowContextMenu(hwnd, path, pt)` - Windows shell context menu
  - `file_operations::CopyPathToClipboard(hwnd, path)` - Windows clipboard API
  - `GetCursorPos(&pt)` - Windows API
  - `drag_drop::StartFileDragDrop(path)` - Windows drag-and-drop

**Goal**: Make `UIRenderer` compile and work on both Windows and macOS without duplicating code.

---

## Architecture Options

### Option 1: Type Alias with Conditional Compilation (Recommended)

**Approach**: Use a type alias for the native window handle, make Windows-specific features conditional.

**Pros**:
- ✅ Clean API - single type alias hides platform differences
- ✅ Minimal code changes - just replace `HWND` with `NativeWindowHandle`
- ✅ Type-safe - compiler enforces correct usage
- ✅ Easy to extend to other platforms (Linux, etc.)

**Cons**:
- ⚠️ Requires conditional compilation for platform-specific features
- ⚠️ Some functions need `#ifdef _WIN32` blocks

**Implementation**:

```cpp
// In UIRenderer.h
#ifdef _WIN32
#include <windows.h>
using NativeWindowHandle = HWND;
#else
// On macOS/GLFW, we can use GLFWwindow* or void*
using NativeWindowHandle = void*;  // nullptr on macOS (not needed for basic operations)
#endif

class UIRenderer {
public:
  static void RenderSearchResultsTable(GuiState &state, 
                                       NativeWindowHandle native_window,
                                       SearchWorker &search_worker,
                                       FileIndex &file_index);
  
  static bool RenderPathColumnWithEllipsis(const SearchResult &result,
                                           bool is_selected, int row,
                                           GuiState &state, 
                                           NativeWindowHandle native_window);
};
```

```cpp
// In UIRenderer.cpp
void UIRenderer::RenderSearchResultsTable(GuiState &state, 
                                           NativeWindowHandle native_window,
                                           SearchWorker &search_worker,
                                           FileIndex &file_index) {
  // ... common code ...
  
  // Right-click context menu (Windows only)
  if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
#ifdef _WIN32
    LOG_INFO("Opening context menu for: " + result.fullPath);
    POINT pt;
    GetCursorPos(&pt);
    ShowContextMenu(static_cast<HWND>(native_window), result.fullPath, pt);
#else
    // macOS: Could implement native context menu here, or just skip
    // For Phase 2, we can skip context menu on macOS
#endif
  }
  
  // Ctrl+C to copy path (Windows only for now)
  if (ImGui::IsItemHovered() && ImGui::IsKeyDown(ImGuiKey_LeftCtrl) &&
      ImGui::IsKeyPressed(ImGuiKey_C)) {
#ifdef _WIN32
    file_operations::CopyPathToClipboard(static_cast<HWND>(native_window), result.fullPath);
#else
    // macOS: Use cross-platform clipboard API (to be implemented)
    // For Phase 2, we can use a simple cross-platform clipboard function
#endif
  }
}
```

**Usage**:
```cpp
// Windows (main_gui.cpp)
HWND hwnd = glfwGetWin32Window(window);
UIRenderer::RenderSearchResultsTable(state, hwnd, search_worker, file_index);

// macOS (main_mac.mm)
UIRenderer::RenderSearchResultsTable(state, nullptr, search_worker, file_index);
// Or: UIRenderer::RenderSearchResultsTable(state, window, search_worker, file_index);
```

**Estimated Effort**: 2-3 hours
- Replace `HWND` with `NativeWindowHandle` in headers and implementations
- Add `#ifdef _WIN32` blocks around Windows-specific calls
- Update call sites in `main_gui.cpp` and `main_mac.mm`

---

### Option 2: Abstract Platform Interface

**Approach**: Create a platform abstraction layer with virtual functions for platform-specific operations.

**Pros**:
- ✅ Clean separation of platform-specific code
- ✅ Easy to test (mock platform interface)
- ✅ Extensible - easy to add new platforms
- ✅ No conditional compilation in UI code

**Cons**:
- ⚠️ More complex - requires interface design
- ⚠️ Overhead of virtual function calls
- ⚠️ More files to maintain
- ⚠️ Overkill for just 2-3 platform-specific operations

**Implementation**:

```cpp
// PlatformInterface.h
class PlatformInterface {
public:
  virtual ~PlatformInterface() = default;
  virtual void ShowContextMenu(const std::string& path, int x, int y) = 0;
  virtual void CopyPathToClipboard(const std::string& path) = 0;
  virtual void StartFileDragDrop(const std::string& path) = 0;
};

// WindowsPlatform.cpp
class WindowsPlatform : public PlatformInterface {
  HWND hwnd_;
public:
  WindowsPlatform(HWND hwnd) : hwnd_(hwnd) {}
  void ShowContextMenu(const std::string& path, int x, int y) override {
    POINT pt = {x, y};
    ShowContextMenu(hwnd_, path, pt);
  }
  // ... other methods
};

// MacPlatform.cpp
class MacPlatform : public PlatformInterface {
public:
  void ShowContextMenu(const std::string& path, int x, int y) override {
    // macOS implementation or no-op
  }
  // ... other methods
};
```

**Estimated Effort**: 6-8 hours
- Design and implement platform interface
- Create Windows and macOS implementations
- Refactor UIRenderer to use interface
- Update call sites

---

### Option 3: Function Pointers / Callbacks

**Approach**: Pass optional function pointers for platform-specific operations.

**Pros**:
- ✅ No conditional compilation in UIRenderer
- ✅ Flexible - caller provides implementations
- ✅ Easy to test with mock functions

**Cons**:
- ⚠️ More complex function signatures
- ⚠️ Caller must provide all callbacks (even if unused)
- ⚠️ Less type-safe than Option 1
- ⚠️ More verbose at call sites

**Implementation**:

```cpp
// In UIRenderer.h
struct PlatformCallbacks {
  std::function<void(const std::string&, int, int)> show_context_menu;
  std::function<void(const std::string&)> copy_to_clipboard;
  std::function<void(const std::string&)> start_drag_drop;
};

class UIRenderer {
public:
  static void RenderSearchResultsTable(GuiState &state, 
                                       const PlatformCallbacks& callbacks,
                                       SearchWorker &search_worker,
                                       FileIndex &file_index);
};
```

**Usage**:
```cpp
// Windows
PlatformCallbacks callbacks;
callbacks.show_context_menu = [hwnd](const std::string& path, int x, int y) {
  POINT pt = {x, y};
  ShowContextMenu(hwnd, path, pt);
};
UIRenderer::RenderSearchResultsTable(state, callbacks, search_worker, file_index);

// macOS
PlatformCallbacks callbacks;  // Empty callbacks = no-op
UIRenderer::RenderSearchResultsTable(state, callbacks, search_worker, file_index);
```

**Estimated Effort**: 4-5 hours
- Define callback structure
- Refactor UIRenderer to use callbacks
- Update call sites with callback implementations

---

### Option 4: Separate Platform-Specific Renderers

**Approach**: Create `UIRendererWindows` and `UIRendererMac` with shared base class.

**Pros**:
- ✅ Complete separation of platform code
- ✅ No conditional compilation
- ✅ Platform-specific optimizations possible

**Cons**:
- ⚠️ Code duplication - most UI code is identical
- ⚠️ Maintenance burden - changes must be made in two places
- ⚠️ More complex inheritance hierarchy
- ⚠️ Overkill for small platform differences

**Estimated Effort**: 8-10 hours
- Design base class with shared functionality
- Create platform-specific derived classes
- Refactor all UI code

---

## Recommendation: Option 1 (Type Alias with Conditional Compilation)

**Why Option 1 is best**:
1. **Minimal changes**: Just replace `HWND` with `NativeWindowHandle` and add a few `#ifdef` blocks
2. **Type safety**: Compiler enforces correct usage
3. **Maintainability**: Single codebase, easy to see what's platform-specific
4. **Performance**: No virtual function overhead
5. **Simplicity**: Easy to understand and review

**Implementation Plan**:

1. **Add type alias** in `UIRenderer.h`:
   ```cpp
   #ifdef _WIN32
   #include <windows.h>
   using NativeWindowHandle = HWND;
   #else
   using NativeWindowHandle = void*;
   #endif
   ```

2. **Update function signatures**:
   - `RenderSearchResultsTable(GuiState &state, NativeWindowHandle native_window, ...)`
   - `RenderPathColumnWithEllipsis(..., NativeWindowHandle native_window)`

3. **Wrap Windows-specific calls** in `#ifdef _WIN32`:
   - `ShowContextMenu()` calls
   - `CopyPathToClipboard()` calls
   - `GetCursorPos()` calls
   - `StartFileDragDrop()` calls

4. **Update call sites**:
   - Windows: `UIRenderer::RenderSearchResultsTable(state, glfwGetWin32Window(window), ...)`
   - macOS: `UIRenderer::RenderSearchResultsTable(state, nullptr, ...)`

5. **For macOS Phase 2**: Implement basic file operations (open, reveal) without needing window handle

---

## Platform-Specific Features to Handle

### Windows-Only (Phase 2 - Skip on macOS)
- ✅ Shell context menu (`ShowContextMenu`)
- ✅ Windows clipboard API (`CopyPathToClipboard`)
- ✅ Windows drag-and-drop (`StartFileDragDrop`)

### Cross-Platform (Phase 2 - Implement on macOS)
- ✅ Open file (`file_operations::OpenFileDefault`) - can use `open` command on macOS
- ✅ Open parent folder (`file_operations::OpenParentFolder`) - can use `open` command on macOS
- ✅ Delete file (`file_operations::DeleteFileToRecycleBin`) - can use `trash` command on macOS

### Future Enhancements (Post-Phase 2)
- macOS native context menu (NSMenu)
- macOS native drag-and-drop (NSDraggingSource)
- Cross-platform clipboard API

---

## Migration Checklist

- [ ] Add `NativeWindowHandle` type alias to `UIRenderer.h`
- [ ] Update `RenderSearchResultsTable()` signature
- [ ] Update `RenderPathColumnWithEllipsis()` signature
- [ ] Wrap `ShowContextMenu()` calls in `#ifdef _WIN32`
- [ ] Wrap `CopyPathToClipboard()` calls in `#ifdef _WIN32`
- [ ] Wrap `GetCursorPos()` calls in `#ifdef _WIN32`
- [ ] Wrap `StartFileDragDrop()` calls in `#ifdef _WIN32`
- [ ] Update `main_gui.cpp` to pass `glfwGetWin32Window(window)`
- [ ] Update `main_mac.mm` to pass `nullptr` (or `window` if needed later)
- [ ] Test Windows build
- [ ] Test macOS build
- [ ] Verify no compilation errors on either platform

---

## Estimated Time

**Total**: 2-3 hours
- Type alias and signature changes: 30 minutes
- Conditional compilation blocks: 1 hour
- Call site updates: 30 minutes
- Testing and verification: 1 hour

