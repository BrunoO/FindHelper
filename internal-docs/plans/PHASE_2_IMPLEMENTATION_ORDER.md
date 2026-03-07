# Phase 2: Implementation Order (Option 3: Hybrid Approach)

## Overview

This document outlines the implementation order for Phase 2 using **Option 3: Hybrid Approach**, which provides maximum code reuse while maintaining clear separation of concerns.

---

## Implementation Steps

### ✅ Step 1: Make UIRenderer Cross-Platform (COMPLETED)
- Remove `HWND` dependencies
- Add `NativeWindowHandle` type alias
- Wrap Windows-specific calls in `#ifdef _WIN32`

**Status**: ✅ Complete

---

### ✅ Step 1.5: Extract Inline UI Code (COMPLETED)
- Extract `RenderSearchControls()` to `UIRenderer`
- Extract `RenderSavedSearchPopups()` to `UIRenderer`
- Update `main_gui.cpp` to use new methods

**Status**: ✅ Complete

---

### 🔄 Step 1.6: Extract Main Window Rendering and Application Logic (NEXT)

**Goal**: Extract main window setup and application logic to eliminate remaining duplication.

**Estimated Time**: 5-6 hours

#### Task 1: Create ApplicationLogic Namespace

**New Files**: `ApplicationLogic.h`, `ApplicationLogic.cpp`

**Functions to Extract from `main_gui.cpp`**:
1. **Keyboard shortcuts** (lines 695-714):
   - Ctrl+F: Focus filename input
   - F5: Refresh search
   - Escape: Clear filters

2. **Search controller updates** (lines 728-745):
   - `search_controller.Update()`
   - Maintenance tasks
   - Index building checks

**Signature**:
```cpp
namespace ApplicationLogic {
  void Update(GuiState &state,
              SearchController &search_controller,
              SearchWorker &search_worker,
              FileIndex &file_index,
              UsnMonitor *monitor,
              bool is_index_building);
  
  void HandleKeyboardShortcuts(GuiState &state,
                               SearchController &search_controller,
                               SearchWorker &search_worker,
                               bool is_index_building);
}
```

#### Task 2: Add RenderMainWindow to UIRenderer

**File**: `UIRenderer.h`, `UIRenderer.cpp`

**Function to Extract from `main_gui.cpp`**:
- Main window setup (lines 657-667, 787)
- All UI rendering calls (already in UIRenderer)

**Signature**:
```cpp
static void RenderMainWindow(GuiState &state,
                             SearchController &search_controller,
                             SearchWorker &search_worker,
                             FileIndex &file_index,
                             UsnMonitor *monitor,
                             AppSettings &settings,
                             bool &show_settings,
                             bool &show_metrics,
                             NativeWindowHandle native_window,
                             bool is_index_building);
```

#### Task 3: Create TimeFilterUtils

**New Files**: `TimeFilterUtils.h`, `TimeFilterUtils.cpp`

**Functions to Move from `main_gui.cpp`**:
- `TimeFilterToString()` (lines 977-993)
- `TimeFilterFromString()` (lines 997-1014)
- `ApplySavedSearchToGuiState()` (lines 1018-1029)

**Keep in `main_gui.cpp`** (Windows-specific):
- `ConfigureFontsFromSettings()` (Windows font paths)
- `ApplyFontSettings()` (Windows-specific implementation)
- `CalculateCutoffTime()` (Windows APIs)
- `GetUserProfilePath()` (Windows-specific)

#### Task 4: Update Platform Files

**`main_gui.cpp`**:
```cpp
// Main loop
while (!glfwWindowShouldClose(window)) {
  glfwPollEvents();
  
  // Platform-specific frame setup
  ImGui_ImplDX11_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  
  // Application logic
  bool is_index_building = g_monitor && g_monitor->IsPopulatingIndex();
  ApplicationLogic::Update(state, search_controller, search_worker,
                          g_file_index, g_monitor.get(), is_index_building);
  
  // UI rendering
  UIRenderer::RenderMainWindow(state, search_controller, search_worker,
                               g_file_index, g_monitor.get(), settings_state,
                               g_show_settings, g_show_metrics,
                               glfwGetWin32Window(window), is_index_building);
  
  // Platform-specific rendering
  ImGui::Render();
  direct_x_manager.RenderImGui();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
  }
  direct_x_manager.Present();
}
```

**Checklist**:
- [ ] Create `ApplicationLogic.h` and `ApplicationLogic.cpp`
- [ ] Move keyboard shortcuts to `ApplicationLogic::HandleKeyboardShortcuts()`
- [ ] Move search controller updates to `ApplicationLogic::Update()`
- [ ] Move maintenance tasks to `ApplicationLogic::Update()`
- [ ] Add `RenderMainWindow()` to `UIRenderer.h` and `UIRenderer.cpp`
- [ ] Move main window setup to `RenderMainWindow()`
- [ ] Create `TimeFilterUtils.h` and `TimeFilterUtils.cpp`
- [ ] Move cross-platform helpers to `TimeFilterUtils.cpp`
- [ ] Update `main_gui.cpp` to use new abstractions
- [ ] Add `ApplicationLogic.cpp` to Windows build in `CMakeLists.txt`
- [ ] Add `TimeFilterUtils.cpp` to Windows build in `CMakeLists.txt`
- [ ] Test Windows build
- [ ] Verify Windows functionality

---

### 📋 Step 2: Add Search Infrastructure to macOS (Feature Parity)

**Goal**: Achieve **complete feature parity** with Windows for search and UI capabilities.

**Estimated Time**: 3-4 hours (simplified with Option 3, but includes all features)

**Changes**:

1. **Add search state management** to `main_mac.mm`:
   ```cpp
   static GuiState state;
   static SearchWorker search_worker(file_index);
   static SearchController search_controller;
   static AppSettings settings;
   bool show_settings = false;
   bool show_metrics = false;
   ```

2. **Load settings**:
   ```cpp
   LoadSettings(settings);
   ```

3. **Replace stub UI with full UI** in main loop:
   ```cpp
   while (!glfwWindowShouldClose(window)) {
     @autoreleasepool {
       glfwPollEvents();
       
       // Metal setup...
       
       ImGui_ImplMetal_NewFrame(renderPassDescriptor);
       ImGui_ImplGlfw_NewFrame();
       ImGui::NewFrame();
       
       // Application logic
       bool is_index_building = false;  // macOS: no monitoring
       ApplicationLogic::Update(state, search_controller, search_worker,
                               file_index, nullptr, is_index_building);
       
       // UI rendering
       UIRenderer::RenderMainWindow(state, search_controller, search_worker,
                                    file_index, nullptr, settings,
                                    show_settings, show_metrics,
                                    nullptr, is_index_building);
       
       // Metal rendering...
     }
   }
   ```

4. **Update `CMakeLists.txt`**:
   - Add `ApplicationLogic.cpp` to macOS `APP_SOURCES`
   - Add `TimeFilterUtils.cpp` to macOS `APP_SOURCES`
   - Add `UIRenderer.cpp` to macOS `APP_SOURCES` (if not already added)

**Checklist**:
- [ ] Add search state management to `main_mac.mm` (GuiState, SearchWorker, SearchController, AppSettings)
- [ ] Load settings in `main_mac.mm` (LoadSettings, ApplyFontSettings for macOS)
- [ ] Replace stub UI with `ApplicationLogic::Update()` and `UIRenderer::RenderMainWindow()`
- [ ] Verify all UI components work (search inputs, controls, results table, status bar)
- [ ] Verify settings window works (show_settings toggle, font configuration)
- [ ] Verify metrics window works (show_metrics toggle, index statistics)
- [ ] Verify saved searches work (save, load, delete)
- [ ] Verify keyboard shortcuts work (Ctrl+F, F5, Escape)
- [ ] Update `CMakeLists.txt` to include all required source files
- [ ] Test macOS build
- [ ] Verify macOS functionality matches Windows (feature parity)

---

### 📋 Step 3: Add Required Source Files to macOS Build

**Goal**: Ensure all required source files are included in macOS build.

**File**: `CMakeLists.txt`

**Checklist**:
- [ ] Verify `UIRenderer.cpp` is in macOS `APP_SOURCES`
- [ ] Verify `ApplicationLogic.cpp` is in macOS `APP_SOURCES`
- [ ] Verify `TimeFilterUtils.cpp` is in macOS `APP_SOURCES`
- [ ] Verify `Settings.cpp` is in macOS `APP_SOURCES`
- [ ] Verify `GuiState.cpp` is in macOS `APP_SOURCES`
- [ ] Verify `SearchController.cpp` is in macOS `APP_SOURCES`
- [ ] Verify `SearchWorker.cpp` is in macOS `APP_SOURCES`
- [ ] Test macOS build

---

### 📋 Step 4: Platform-Specific File Operations (macOS Equivalents)

**Goal**: Implement macOS file operations to match Windows functionality (open, reveal in Finder, copy path).

**Estimated Time**: 2-3 hours

**Note**: This step ensures macOS has equivalent file operation capabilities to Windows, even though the implementation differs (macOS uses Finder operations instead of Windows shell integration).

**Approach**:
- Create `FileOperations_mac.cpp` with macOS implementations
- Or make `FileOperations.cpp` conditional with `#ifdef _WIN32` / `#else` blocks

**Checklist**:
- [ ] Implement `OpenFileDefault()` for macOS
- [ ] Implement `OpenParentFolder()` for macOS (reveal in Finder)
- [ ] Implement `CopyPathToClipboard()` for macOS
- [ ] Test macOS file operations

---

## Summary

### Code Reduction

**Before**:
- `main_gui.cpp`: ~1033 lines
- `main_mac.mm`: ~216 lines (stub)
- **Duplication risk**: ~300 lines

**After Option 3**:
- `main_gui.cpp`: ~600 lines (platform setup + Windows helpers)
- `main_mac.mm`: ~200 lines (platform setup)
- `UIRenderer.cpp`: +150 lines (RenderMainWindow)
- `ApplicationLogic.cpp`: +100 lines (new file)
- `TimeFilterUtils.cpp`: +50 lines (new file)
- **Duplication eliminated**: ~150 lines
- **Remaining duplication**: ~50 lines (acceptable, platform-specific rendering)

### Benefits

- ✅ **Maximum code reuse**: ~150 lines of duplication eliminated
- ✅ **Better organization**: Application logic separated from UI
- ✅ **Testable**: Application logic can be unit tested
- ✅ **Maintainable**: Clear separation of concerns
- ✅ **Platform files are thin**: ~200 lines each, focused on platform-specific setup
- ✅ **Easy to extend**: Adding new platforms requires minimal code

---

## Next Steps

1. **Complete Step 1.6**: Extract main window rendering and application logic
2. **Complete Step 2**: Add search infrastructure to macOS
3. **Complete Step 3**: Verify all source files are in macOS build
4. **Complete Step 4**: Implement macOS file operations

**Total Estimated Time**: 9-12 hours

