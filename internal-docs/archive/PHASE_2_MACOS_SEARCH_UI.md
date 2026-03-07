# Phase 2: macOS Search UI Implementation

## Overview

**Goal**: Achieve **feature parity** with the Windows version for UI and search capabilities. The macOS application will have the same search functionality, UI components, and user experience as Windows, with only two platform-specific differences:
1. **Index Population**: macOS uses `--index-from-file` command-line argument (no real-time USN monitoring)
2. **Real-time Monitoring**: macOS does not support real-time file system monitoring (USN is Windows-specific)

**Status**: Phase 1 complete - macOS has basic stub UI with index statistics display.  
**Next**: Phase 2 - Add full search UI and result display to achieve feature parity.

---

## Phase 2 Objectives

### Primary Goals: Feature Parity with Windows

The macOS application will have **complete feature parity** with Windows for all search and UI capabilities:

1. **Full Search UI Components** (identical to Windows):
   - Search input fields (path, filename, extensions)
   - Search controls (Search button, checkboxes: Folders Only, Case-Insensitive, Auto-refresh)
   - Clear All button
   - Help button with search tips
   - Time filter dropdown (Today, This Week, This Month, This Year, Older)
   - Search results table with full sorting (Filename, Extension, Size, Last Modified, Full Path)
   - Status bar showing search progress, result count, and index statistics
   - Keyboard shortcuts (Ctrl+F, F5, Escape)

2. **Complete Search Infrastructure** (identical to Windows):
   - `SearchController` integration (debounced auto-search, auto-refresh, polling)
   - `SearchWorker` integration (async search execution)
   - Full `FileIndex::Search` capabilities (path patterns, filename patterns, extension filters)
   - Search state management via `GuiState`
   - Saved searches (save, load, delete presets)
   - Search result filtering and sorting

3. **Settings and Metrics Windows** (identical to Windows):
   - Settings window (font family, font size, font scale, window size persistence)
   - Metrics window (index statistics, search performance metrics)
   - Settings persistence (JSON file)

4. **Platform-Specific Differences** (only these differ from Windows):
   - **Index Population**: macOS loads index from file via `--index-from-file` command-line argument
   - **Real-time Monitoring**: macOS does not support USN monitoring (Windows-only feature)
   - **File Operations**: macOS uses native file operations (open in Finder, reveal in Finder) instead of Windows shell integration

5. **Cross-Platform UIRenderer** (already completed in Step 1):
   - All UI components work on both platforms
   - Platform-specific features are optional (drag-and-drop on Windows, Finder operations on macOS)

---

## Implementation Steps

### Step 1: Make UIRenderer Cross-Platform

**File**: `UIRenderer.h`, `UIRenderer.cpp`

**Changes needed**:
1. **Remove Windows-specific includes**:
   - Replace `#include <windows.h>` with conditional include
   - Use forward declaration for `HWND` on non-Windows platforms

2. **Make HWND parameter optional**:
   - Change `RenderSearchResultsTable(GuiState &state, HWND hwnd, ...)` 
   - To: `RenderSearchResultsTable(GuiState &state, void* native_window = nullptr, ...)`
   - Or use conditional compilation: `#ifdef _WIN32` for Windows-specific features

3. **Platform-specific features**:
   - **Windows**: Drag-and-drop, shell context menu, file operations
   - **macOS**: Basic file operations (open in Finder, reveal in Finder)
   - Make these optional/conditional

**Example**:
```cpp
// In UIRenderer.h
#ifdef _WIN32
#include <windows.h>
using NativeWindowHandle = HWND;
#else
using NativeWindowHandle = void*;  // GLFWwindow* or nullptr
#endif

static void RenderSearchResultsTable(GuiState &state, 
                                     NativeWindowHandle native_window,
                                     SearchWorker &search_worker,
                                     FileIndex &file_index);
```

---

### Step 1.5: Extract Inline UI Code to UIRenderer (Prevent Duplication)

**⚠️ IMPORTANT**: Before Step 2, we must extract inline UI code from `main_gui.cpp` to prevent duplication when adding macOS UI.

**File**: `UIRenderer.h`, `UIRenderer.cpp`, `main_gui.cpp`

**Problem**: Currently, ~173 lines of UI code are inline in `main_gui.cpp`:
- Search controls row (Search button, checkboxes, Clear All, Help) - lines 747-827
- Saved search popups (SaveSearchPopup, DeleteSavedSearchPopup) - lines 856-947

**Solution**: Extract to `UIRenderer` methods before Step 2.

**Changes needed**:

1. **Add `RenderSearchControls()` method**:
   ```cpp
   // In UIRenderer.h
   static void RenderSearchControls(GuiState &state,
                                    SearchController *search_controller,
                                    SearchWorker *search_worker,
                                    bool is_index_building);
   ```
   - Move search button, checkboxes (Folders Only, Case-Insensitive, Auto-refresh), Clear All, Help button
   - Handle `is_index_building` state and keyboard shortcuts

2. **Add `RenderSavedSearchPopups()` method**:
   ```cpp
   // In UIRenderer.h
   static void RenderSavedSearchPopups(GuiState &state,
                                       AppSettings &settings);
   ```
   - Move SaveSearchPopup and DeleteSavedSearchPopup modals
   - Manage static state internally

3. **Update `main_gui.cpp`**:
   - Replace inline code with `UIRenderer::RenderSearchControls()`
   - Replace inline code with `UIRenderer::RenderSavedSearchPopups()`

**Benefits**:
- ✅ DRY principle: Single source of truth for UI layout
- ✅ Step 2 becomes simpler: Just call existing methods
- ✅ Consistent UI across platforms
- ✅ Easier maintenance

**See**: `docs/PHASE_2_STEP2_DRY_ANALYSIS.md` for detailed analysis.

---

### Step 1.6: Extract Main Window Rendering and Application Logic (Option 3: Hybrid Approach)

**⚠️ IMPORTANT**: This step further reduces duplication by extracting main window setup and application logic.

**Files**: `UIRenderer.h`, `UIRenderer.cpp`, `ApplicationLogic.h`, `ApplicationLogic.cpp`, `TimeFilterUtils.h`, `TimeFilterUtils.cpp`, `main_gui.cpp`

**Problem**: Even after Step 1.5, ~300 lines remain that would be duplicated:
- Main window setup/teardown (~15 lines of ImGui calls)
- Keyboard shortcuts (~20 lines)
- Application logic (~50 lines: search controller updates, maintenance, index building checks)
- Helper functions (~200 lines: some cross-platform, some Windows-specific)

**Solution**: Extract main window rendering and application logic using Option 3 (Hybrid Approach).

**Changes needed**:

1. **Add `UIRenderer::RenderMainWindow()` method**:
   ```cpp
   // In UIRenderer.h
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
   - Calls all UI rendering methods (already in UIRenderer)
   - Handles window positioning and sizing

2. **Add `ApplicationLogic::Update()` namespace**:
   ```cpp
   // New file: ApplicationLogic.h
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
   - Handles search controller updates
   - Handles maintenance tasks
   - Handles keyboard shortcuts
   - Handles index building checks

3. **Create `TimeFilterUtils.h/cpp`**:
   - Move `TimeFilterToString()` (cross-platform)
   - Move `TimeFilterFromString()` (cross-platform)
   - Move `ApplySavedSearchToGuiState()` (cross-platform)

4. **Update platform files**:
   - `main_gui.cpp`: Thin main loop with platform-specific rendering
   - `main_mac.mm`: Thin main loop with platform-specific rendering

**Benefits**:
- ✅ Better organization: Application logic separated from UI
- ✅ Testable: Application logic can be unit tested
- ✅ Maintainable: Clear separation of concerns
- ✅ Eliminates ~150 lines of duplication
- ✅ Platform files become thin (~200 lines each)

**See**: `docs/PHASE_2_MAIN_LOOP_ABSTRACTION_OPTIONS.md` for detailed analysis of all options.

---

### Step 2: Add Search Infrastructure to macOS (Simplified with Option 3)

**File**: `main_mac.mm`, `CMakeLists.txt`

**Changes needed**:

1. **Add search state management** (before main loop):
   ```cpp
   static GuiState state;
   static SearchWorker search_worker(file_index);
   static SearchController search_controller;
   static AppSettings settings;
   bool show_settings = false;
   bool show_metrics = false;
   ```

2. **Load settings** (after index loading):
   ```cpp
   LoadSettings(settings);
   ```

3. **Replace stub UI with full UI** (in main loop):
   - Remove the stub "Index Statistics" window
   - Replace with calls to `ApplicationLogic::Update()` and `UIRenderer::RenderMainWindow()`

4. **Update main loop**:
   ```cpp
   while (!glfwWindowShouldClose(window)) {
     @autoreleasepool {
       glfwPollEvents();
       
       // Metal setup...
       
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
       
       ImGui::Render();
       ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, renderEncoder);
       
       // Metal present...
     }
   }
   ```

5. **Add required source files to CMakeLists.txt**:
   - `UIRenderer.cpp` (already added)
   - `ApplicationLogic.cpp` (new file from Step 1.6)
   - `TimeFilterUtils.cpp` (new file from Step 1.6)
   - `Settings.cpp` (already added)
   - `GuiState.cpp` (already added)
   - `SearchController.cpp` (already added)
   - `SearchWorker.cpp` (already added)

**Benefits of Option 3**:
- ✅ **Minimal code duplication**: Only ~50 lines of platform-specific rendering code
- ✅ **Single source of truth**: All UI logic in `UIRenderer`, all application logic in `ApplicationLogic`
- ✅ **Easy to maintain**: Changes to UI or application logic only need to be made once
- ✅ **Testable**: Application logic can be unit tested independently
- ✅ **Clear separation**: Platform files are thin and focused on platform-specific setup

---

### Step 3: Add Required Source Files to macOS Build

**File**: `CMakeLists.txt`

**Changes needed**:
Add to `APP_SOURCES` for macOS:
- `UIRenderer.cpp` (after making it cross-platform)
- `FileOperations.cpp` (if needed for macOS file operations)
- Or create `FileOperations_mac.cpp` for macOS-specific file operations

**Note**: Some files may need platform-specific implementations:
- `FileOperations.cpp` - Windows shell integration
- `DragDropUtils.cpp` - Windows drag-and-drop
- `ShellContextUtils.cpp` - Windows shell context menu

**Options**:
1. Create macOS stubs for these files
2. Make them conditional in CMakeLists.txt
3. Create platform-specific implementations

---

### Step 4: Platform-Specific File Operations

**Windows** (existing):
- Drag-and-drop files
- Shell context menu (open, explore, properties)
- File operations via Windows API

**macOS** (to implement):
- Open file in default application
- Reveal in Finder
- Copy file path to clipboard
- Basic file operations via macOS APIs

**Approach**:
- Create `FileOperations_mac.cpp` with macOS implementations
- Or make `FileOperations.cpp` conditional with platform-specific code
- Use `#ifdef _WIN32` / `#else` blocks

---

### Step 5: Testing and Refinement

**Testing checklist**:
- [ ] Search input fields work correctly
- [ ] Search executes and displays results
- [ ] Results table is sortable
- [ ] Status bar shows correct information
- [ ] Keyboard shortcuts work
- [ ] File operations work (open, reveal in Finder)
- [ ] UI is responsive and matches Windows functionality (where applicable)

---

## Detailed Implementation Plan

### 1. UIRenderer Cross-Platform Migration

**Priority**: High (blocks other work)

**Tasks**:
1. Replace `HWND` with platform-agnostic type
2. Make Windows-specific features conditional
3. Test on both platforms

**Estimated effort**: 2-3 hours

---

### 2. macOS Search UI Integration

**Priority**: High

**Tasks**:
1. Add `GuiState`, `SearchWorker`, `SearchController` to `main_mac.mm`
2. Integrate UI rendering calls
3. Add search controller update loop
4. Test search functionality

**Estimated effort**: 2-3 hours

---

### 3. macOS File Operations

**Priority**: Medium

**Tasks**:
1. Implement macOS file operations (open, reveal in Finder)
2. Add to results table context menu or buttons
3. Test file operations

**Estimated effort**: 2-3 hours

---

### 4. UI Polish and Feature Parity

**Priority**: Low

**Tasks**:
1. Ensure UI looks good on macOS
2. Add macOS-specific UI improvements (if any)
3. Test edge cases
4. Performance optimization

**Estimated effort**: 2-4 hours

---

## Code Structure After Phase 2

### macOS (`main_mac.mm`)
```cpp
// State management
static GuiState state;
static SearchWorker search_worker(file_index);
static SearchController search_controller;

// Main loop
while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    // ... Metal setup ...
    
    ImGui::NewFrame();
    
    // Main window
    ImGui::Begin("FindHelper", ...);
    
    // Index statistics (Phase 1)
    // ... existing code ...
    
    // Search UI (Phase 2)
    UIRenderer::RenderSearchInputs(state, &search_controller, 
                                    &search_worker, false);
    UIRenderer::RenderSearchResultsTable(state, nullptr, 
                                         search_worker, file_index);
    UIRenderer::RenderStatusBar(state, search_worker, nullptr, file_index);
    
    // Update search controller
    search_controller.Update(state, search_worker, nullptr);
    
    ImGui::End();
    
    // ... rendering ...
}
```

---

## Dependencies and Prerequisites

### Required (already available):
- ✅ `GuiState.h` - Cross-platform
- ✅ `SearchController.cpp` - Cross-platform
- ✅ `SearchWorker.cpp` - Cross-platform
- ✅ `FileIndex.cpp` - Cross-platform
- ✅ `UIRenderer.cpp` - Needs cross-platform migration

### Needs work:
- ⚠️ `UIRenderer.cpp` - Remove Windows dependencies
- ⚠️ `FileOperations.cpp` - Add macOS implementation or stub
- ⚠️ Platform-specific file operations

---

## Success Criteria

Phase 2 is complete when **macOS achieves feature parity** with Windows:

### Search Functionality (100% Parity)
1. ✅ All search input fields work (path, filename, extensions)
2. ✅ All search filters work (folders only, case sensitivity, time filters)
3. ✅ Search results are displayed in a fully sortable table (all 5 columns)
4. ✅ Search controller features work (auto-search, auto-refresh, manual refresh)
5. ✅ Saved searches work (save, load, delete presets)
6. ✅ Keyboard shortcuts work (Ctrl+F, F5, Escape)

### UI Components (100% Parity)
7. ✅ All UI components match Windows (search controls, results table, status bar)
8. ✅ Settings window works (font configuration, window size persistence)
9. ✅ Metrics window works (index statistics, search performance)
10. ✅ Help popups work (search tips, keyboard shortcuts, regex generator)

### Platform-Specific Features (macOS Equivalents)
11. ✅ File operations work on macOS (open in Finder, reveal in Finder, copy path)
12. ✅ Index loading from file works via command-line argument
13. ✅ Settings persistence works (JSON file)

### Code Quality
14. ✅ Code compiles and runs on both Windows and macOS
15. ✅ No code duplication (all UI logic in `UIRenderer`, all application logic in `ApplicationLogic`)
16. ✅ Cross-platform tests pass

---

## Future Phases (Post-Phase 2)

### Phase 3: macOS-Specific Enhancements
- Native macOS file system monitoring (FSEvents) as alternative to USN (optional)
- App bundle packaging and distribution
- macOS-specific UI polish (if needed)
- Additional macOS integrations (Spotlight, etc.)

### Phase 4: Advanced Features (Both Platforms)
- Keyboard shortcuts customization
- Advanced search options
- Performance optimizations
- Additional file operations

---

## Notes

### Platform-Specific Differences (By Design)

1. **Index Population**:
   - **Windows**: Real-time USN Journal monitoring (automatic index updates)
   - **macOS**: Static index loaded from file via `--index-from-file` command-line argument
   - **Impact**: macOS users must manually reload the index when files change, but search functionality is identical

2. **Real-time Monitoring**:
   - **Windows**: USN Journal provides real-time file system change notifications
   - **macOS**: No real-time monitoring (USN is Windows-specific)
   - **Future**: Phase 3 could add FSEvents-based monitoring for macOS (optional enhancement)

3. **File Operations**:
   - **Windows**: Shell integration (context menu, drag-and-drop, explore folder)
   - **macOS**: Native macOS operations (open in Finder, reveal in Finder, copy path)
   - **Impact**: Different user interactions, but same core functionality (opening files, navigating folders)

### Feature Parity Guarantee

**All search and UI features are identical** between Windows and macOS:
- ✅ Same search input fields and filters
- ✅ Same search results table and sorting
- ✅ Same settings and metrics windows
- ✅ Same keyboard shortcuts
- ✅ Same saved searches functionality
- ✅ Same UI layout and behavior

The only differences are in **how the index is populated** (USN vs file) and **how files are opened** (Windows shell vs macOS Finder), which are platform-specific by nature.

