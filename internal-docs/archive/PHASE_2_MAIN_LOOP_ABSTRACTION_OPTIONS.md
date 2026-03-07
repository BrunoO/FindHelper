# Phase 2: Main Loop Abstraction Options

## Problem Statement

Even after Step 1.5, `main_gui.cpp` still contains significant code that would be duplicated in `main_mac.mm`:

1. **Main window setup/teardown** (~15 lines of ImGui calls)
2. **Main loop structure** (~20 lines: frame setup, rendering, present)
3. **Keyboard shortcuts** (~20 lines of ImGui key handling)
4. **Application logic** (~50 lines: search controller updates, maintenance, index building checks)
5. **Helper functions** (~200 lines: some Windows-specific, some cross-platform)

**Total duplication risk**: ~300+ lines

---

## Current State Analysis

### Code in `main_gui.cpp` Main Loop (Lines 648-811)

#### ✅ Platform-Specific (Should Stay in Platform Files)
- GLFW window creation/cleanup
- DirectX/Metal initialization
- Platform-specific rendering calls (`direct_x_manager.RenderImGui()` vs `ImGui_ImplMetal_RenderDrawData()`)
- Platform-specific present calls (`direct_x_manager.Present()` vs Metal present)

#### ⚠️ Duplication Risk (Could Be Abstracted)

**1. Main Window Setup** (Lines 657-667, 787):
```cpp
ImGuiViewport *main_viewport = ImGui::GetMainViewport();
ImGui::SetNextWindowPos(main_viewport->WorkPos);
ImGui::SetNextWindowSize(main_viewport->WorkSize);
ImGui::SetNextWindowViewport(main_viewport->ID);
ImGui::Begin("Find Helper", nullptr, ...);
// ... UI content ...
ImGui::End();
```

**2. Keyboard Shortcuts** (Lines 695-714):
```cpp
if (!ImGui::GetIO().WantTextInput) {
  if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_F)) {
    state.focusFilenameInput = true;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F5) && !is_index_building) {
    search_controller.TriggerManualSearch(state, search_worker);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    state.ClearInputs();
    state.timeFilter = TimeFilter::None;
  }
}
```

**3. Application Logic** (Lines 672-745):
```cpp
bool is_index_building = g_monitor && g_monitor->IsPopulatingIndex();
// Index dump logic
// Search controller updates
// Maintenance tasks
```

**4. Main Loop Structure** (Lines 649-655, 795-810):
```cpp
glfwPollEvents();
ImGui_ImplDX11_NewFrame();  // vs ImGui_ImplMetal_NewFrame()
ImGui_ImplGlfw_NewFrame();
ImGui::NewFrame();
// ... UI rendering ...
ImGui::Render();
direct_x_manager.RenderImGui();  // vs ImGui_ImplMetal_RenderDrawData()
// Multi-viewport handling
direct_x_manager.Present();  // vs Metal present
```

**5. Helper Functions** (Lines 848-1029):
- `ConfigureFontsFromSettings()` - Windows font paths (platform-specific)
- `ApplyFontSettings()` - Cross-platform but Windows-specific implementation
- `CalculateCutoffTime()` - Uses Windows APIs (platform-specific)
- `GetUserProfilePath()` - Windows-specific
- `TimeFilterToString()` - Cross-platform ✅
- `TimeFilterFromString()` - Cross-platform ✅
- `ApplySavedSearchToGuiState()` - Cross-platform ✅

---

## Abstraction Options

### Option 1: Minimal Abstraction (Recommended for Phase 2)

**Approach**: Extract only the most obvious duplication - main window setup and keyboard shortcuts.

**Pros**:
- ✅ Minimal changes
- ✅ Low risk
- ✅ Quick to implement
- ✅ Clear separation of concerns

**Cons**:
- ⚠️ Still some duplication in main loop structure
- ⚠️ Application logic remains duplicated

**Changes**:

1. **Add `UIRenderer::RenderMainWindow()`**:
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
   - Handles main window setup (`ImGui::Begin/End`)
   - Calls all UI rendering methods
   - Handles keyboard shortcuts
   - Handles application logic (search controller updates, maintenance)

2. **Move helper functions**:
   - `TimeFilterToString()` → `TimeFilterUtils.h/cpp` (already cross-platform)
   - `TimeFilterFromString()` → `TimeFilterUtils.h/cpp`
   - `ApplySavedSearchToGuiState()` → `TimeFilterUtils.h/cpp` or `SettingsUtils.h/cpp`
   - Keep Windows-specific helpers in `main_gui.cpp`

**Result**: `main_gui.cpp` and `main_mac.mm` become:
```cpp
// Main loop
while (!glfwWindowShouldClose(window)) {
  glfwPollEvents();
  
  // Platform-specific frame setup
  ImGui_ImplDX11_NewFrame();  // or ImGui_ImplMetal_NewFrame()
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  
  // Single call to render everything
  UIRenderer::RenderMainWindow(state, search_controller, search_worker, 
                               file_index, monitor, settings, 
                               show_settings, show_metrics,
                               native_window, is_index_building);
  
  // Platform-specific rendering
  ImGui::Render();
  direct_x_manager.RenderImGui();  // or Metal rendering
  direct_x_manager.Present();  // or Metal present
}
```

**Estimated Effort**: 3-4 hours

---

### Option 2: Full Main Loop Abstraction

**Approach**: Create a `MainLoop` class or function that handles the entire render loop.

**Pros**:
- ✅ Maximum code reuse
- ✅ Single source of truth for main loop
- ✅ Easier to test
- ✅ Platform files become very thin

**Cons**:
- ⚠️ More complex abstraction
- ⚠️ Platform-specific rendering needs callbacks or virtual functions
- ⚠️ Higher risk of bugs
- ⚠️ More refactoring effort

**Changes**:

1. **Create `MainLoop` class**:
   ```cpp
   class MainLoop {
   public:
     struct PlatformCallbacks {
       std::function<void()> newFrame;
       std::function<void()> render;
       std::function<void()> present;
     };
     
     void Run(GLFWwindow *window,
              GuiState &state,
              SearchController &search_controller,
              SearchWorker &search_worker,
              FileIndex &file_index,
              UsnMonitor *monitor,
              AppSettings &settings,
              NativeWindowHandle native_window,
              const PlatformCallbacks &callbacks);
   };
   ```

2. **Platform files become**:
   ```cpp
   // Windows
   MainLoop::PlatformCallbacks callbacks;
   callbacks.newFrame = []() {
     ImGui_ImplDX11_NewFrame();
     ImGui_ImplGlfw_NewFrame();
     ImGui::NewFrame();
   };
   callbacks.render = [&]() {
     ImGui::Render();
     direct_x_manager.RenderImGui();
   };
   callbacks.present = [&]() {
     direct_x_manager.Present();
   };
   
   MainLoop loop;
   loop.Run(window, state, search_controller, search_worker, 
            file_index, monitor, settings, native_window, callbacks);
   ```

**Estimated Effort**: 8-10 hours

---

### Option 3: Hybrid Approach (Recommended for Future)

**Approach**: Extract main window rendering and application logic, but keep platform-specific rendering in platform files.

**Pros**:
- ✅ Good balance of abstraction and clarity
- ✅ Platform-specific code stays visible
- ✅ Easier to debug
- ✅ Moderate refactoring effort

**Cons**:
- ⚠️ Still some duplication in main loop structure
- ⚠️ More complex than Option 1

**Changes**:

1. **Add `UIRenderer::RenderMainWindow()`** (same as Option 1)
2. **Add `ApplicationLogic::Update()`**:
   ```cpp
   namespace ApplicationLogic {
     void Update(GuiState &state,
                 SearchController &search_controller,
                 SearchWorker &search_worker,
                 FileIndex &file_index,
                 UsnMonitor *monitor,
                 bool is_index_building);
   }
   ```
   - Handles search controller updates
   - Handles maintenance tasks
   - Handles keyboard shortcuts
   - Handles index building checks

3. **Platform files**:
   ```cpp
   while (!glfwWindowShouldClose(window)) {
     glfwPollEvents();
     
     // Platform-specific frame setup
     ImGui_ImplDX11_NewFrame();
     ImGui_ImplGlfw_NewFrame();
     ImGui::NewFrame();
     
     // Application logic
     ApplicationLogic::Update(state, search_controller, search_worker,
                              file_index, monitor, is_index_building);
     
     // UI rendering
     UIRenderer::RenderMainWindow(...);
     
     // Platform-specific rendering
     ImGui::Render();
     direct_x_manager.RenderImGui();
     direct_x_manager.Present();
   }
   ```

**Estimated Effort**: 5-6 hours

---

## Recommendation

### For Phase 2: **Option 3 (Hybrid Approach)** ✅ SELECTED

**Rationale**:
1. **Better Organization**: Application logic separated from UI
2. **Testable**: Application logic can be unit tested
3. **Maintainable**: Clear separation of concerns
4. **Flexible**: Easy to add new platforms
5. **Maximum Code Reuse**: Eliminates ~150 lines of duplication
6. **Platform Files Become Thin**: ~200 lines each, focused on platform-specific setup

---

## Implementation Plan for Option 3 (Selected)

### Step 1: Extract Main Window Rendering

**File**: `UIRenderer.h`, `UIRenderer.cpp`

**New Method**:
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

**Responsibilities**:
- Main window setup (`ImGui::Begin/End`)
- Window positioning and sizing
- All UI rendering calls (already in UIRenderer)

**Note**: Does NOT handle keyboard shortcuts or application logic (moved to `ApplicationLogic`).

### Step 2: Create ApplicationLogic Namespace

**New Files**: `ApplicationLogic.h`, `ApplicationLogic.cpp`

**New Namespace**:
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

**Responsibilities**:
- Search controller updates
- Maintenance tasks
- Keyboard shortcuts handling
- Index building checks

### Step 3: Move Cross-Platform Helpers

**New File**: `TimeFilterUtils.h`, `TimeFilterUtils.cpp`

**Functions to Move**:
- `TimeFilterToString()`
- `TimeFilterFromString()`
- `ApplySavedSearchToGuiState()`

**Keep in `main_gui.cpp`**:
- `ConfigureFontsFromSettings()` (Windows font paths)
- `ApplyFontSettings()` (Windows-specific implementation)
- `CalculateCutoffTime()` (Windows APIs)
- `GetUserProfilePath()` (Windows-specific)

### Step 4: Update Platform Files

**`main_gui.cpp`**:
```cpp
// Main loop
while (!glfwWindowShouldClose(window)) {
  glfwPollEvents();
  
  // Platform-specific frame setup
  ImGui_ImplDX11_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  
  // Application logic (handles search controller updates, maintenance, keyboard shortcuts)
  bool is_index_building = g_monitor && g_monitor->IsPopulatingIndex();
  ApplicationLogic::Update(state, search_controller, search_worker,
                           g_file_index, g_monitor.get(), is_index_building);
  
  // UI rendering (handles main window setup and all UI components)
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

**`main_mac.mm`**:
```cpp
// Main loop
while (!glfwWindowShouldClose(window)) {
  @autoreleasepool {
    glfwPollEvents();
    
    // Metal setup...
    
    // Platform-specific frame setup
    ImGui_ImplMetal_NewFrame(renderPassDescriptor);
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    
    // Application logic (handles search controller updates, maintenance, keyboard shortcuts)
    bool is_index_building = false;  // macOS: no monitoring
    ApplicationLogic::Update(state, search_controller, search_worker,
                            file_index, nullptr, is_index_building);
    
    // UI rendering (handles main window setup and all UI components)
    UIRenderer::RenderMainWindow(state, search_controller, search_worker,
                                 file_index, nullptr, settings,
                                 show_settings, show_metrics,
                                 nullptr, is_index_building);
    
    // Platform-specific rendering
    ImGui::Render();
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, renderEncoder);
    
    // Metal present...
  }
}
```

---

## Code Reduction Estimate

### Before (Current State)
- `main_gui.cpp`: ~1033 lines
- `main_mac.mm`: ~216 lines (stub)
- **Duplication risk**: ~300 lines

### After Option 3
- `main_gui.cpp`: ~600 lines (platform setup + Windows helpers)
- `main_mac.mm`: ~200 lines (platform setup)
- `UIRenderer.cpp`: +150 lines (RenderMainWindow)
- `ApplicationLogic.cpp`: +100 lines (new file)
- `TimeFilterUtils.cpp`: +50 lines (new file)
- **Duplication eliminated**: ~150 lines
- **Remaining duplication**: ~50 lines (main loop structure - acceptable, platform-specific)

---

## Migration Checklist

### Step 1: Extract Main Window Rendering
- [ ] Add `RenderMainWindow()` to `UIRenderer.h`
- [ ] Implement `RenderMainWindow()` in `UIRenderer.cpp`
- [ ] Move main window setup (`ImGui::Begin/End`) to `RenderMainWindow()`
- [ ] Call all existing UI rendering methods from `RenderMainWindow()`
- [ ] Update `main_gui.cpp` to use `RenderMainWindow()`
- [ ] Test Windows build

### Step 2: Create ApplicationLogic Namespace
- [ ] Create `ApplicationLogic.h`
- [ ] Create `ApplicationLogic.cpp`
- [ ] Move keyboard shortcuts to `ApplicationLogic::HandleKeyboardShortcuts()`
- [ ] Move search controller updates to `ApplicationLogic::Update()`
- [ ] Move maintenance tasks to `ApplicationLogic::Update()`
- [ ] Move index building checks to `ApplicationLogic::Update()`
- [ ] Update `main_gui.cpp` to use `ApplicationLogic::Update()`
- [ ] Test Windows build

### Step 3: Move Cross-Platform Helpers
- [ ] Create `TimeFilterUtils.h`
- [ ] Create `TimeFilterUtils.cpp`
- [ ] Move `TimeFilterToString()` to `TimeFilterUtils.cpp`
- [ ] Move `TimeFilterFromString()` to `TimeFilterUtils.cpp`
- [ ] Move `ApplySavedSearchToGuiState()` to `TimeFilterUtils.cpp`
- [ ] Update includes in `main_gui.cpp`, `UIRenderer.cpp`, and `ApplicationLogic.cpp`
- [ ] Test Windows build

### Step 4: Update macOS
- [ ] Update `main_mac.mm` to use `ApplicationLogic::Update()` and `UIRenderer::RenderMainWindow()`
- [ ] Add `ApplicationLogic.cpp` to macOS build in `CMakeLists.txt`
- [ ] Add `TimeFilterUtils.cpp` to macOS build in `CMakeLists.txt`
- [ ] Test macOS build

---

## Conclusion

**Option 3 (Hybrid Approach)** is selected for Phase 2:
- ✅ Eliminates ~150 lines of duplication
- ✅ Better organization: Application logic separated from UI
- ✅ Testable: Application logic can be unit tested
- ✅ Maintainable: Clear separation of concerns
- ✅ Platform files become thin (~200 lines each)
- ✅ Flexible: Easy to add new platforms

**Remaining duplication** (~50 lines of main loop structure) is acceptable because:
- It's platform-specific (DirectX vs Metal rendering)
- It's clearly visible and easy to maintain
- The duplication is minimal and well-contained
- Platform-specific rendering calls must remain in platform files

**Estimated Total Effort**: 5-6 hours

