# Next Steps: Phase 2 Implementation

## Current Status

### ✅ Completed
- **Phase 1**: macOS stub UI with index statistics display
- **Step 1**: Made `UIRenderer` cross-platform with `NativeWindowHandle`
- **Step 1.5**: Extracted inline UI code to `UIRenderer` methods
- **Step 1.6**: Extracted main window rendering and application logic
- **Refactoring**: All code is ready for cross-platform use

### 📋 Next: Step 2 - Add Full Search Infrastructure to macOS

**Goal**: Replace the stub UI in `main_mac.mm` with the full search UI to achieve feature parity with Windows.

---

## Step 2: Implementation Tasks

### Task 1: Add Required Includes and State Management

**File**: `main_mac.mm`

**Add includes**:
```cpp
#include "ApplicationLogic.h"
#include "UIRenderer.h"
#include "SearchResultUtils.h"
#include "GuiState.h"
#include "SearchController.h"
#include "SearchWorker.h"
#include "Settings.h"
```

**Add state management** (after index loading, before main loop):
```cpp
// GUI state
static GuiState state;
static SearchWorker search_worker(file_index);
static SearchController search_controller;

// Settings state
static AppSettings settings;
LoadSettings(settings);

// UI toggles
static bool show_settings = false;
static bool show_metrics = false;
```

### Task 2: Replace Stub UI with Full UI

**File**: `main_mac.mm`

**Replace the stub UI section** (lines 142-189) with:
```cpp
// Application logic (handles search controller updates, maintenance, keyboard shortcuts)
bool is_index_building = false;  // macOS: no monitoring
ApplicationLogic::Update(state, search_controller, search_worker,
                        file_index, nullptr, is_index_building);

// UI rendering (handles main window setup and all UI components)
UIRenderer::RenderMainWindow(state, search_controller, search_worker,
                            file_index, nullptr, settings,
                            show_settings, show_metrics,
                            nullptr, is_index_building);
```

### Task 3: Add Required Source Files to CMakeLists.txt

**File**: `CMakeLists.txt`

**Check if these files are in macOS `APP_SOURCES`**:
- [x] `ApplicationLogic.cpp` ✅ (already added)
- [x] `TimeFilterUtils.cpp` ✅ (already added)
- [ ] `UIRenderer.cpp` ❌ (needs to be added)
- [ ] `SearchResultUtils.cpp` ❌ (needs to be added)
- [x] `Settings.cpp` ✅ (already added)
- [x] `GuiState.cpp` ✅ (already added)
- [x] `SearchController.cpp` ✅ (already added)
- [x] `SearchWorker.cpp` ✅ (already added)

**Add missing files**:
```cmake
set(APP_SOURCES
    main_mac.mm
    ApplicationLogic.cpp
    TimeFilterUtils.cpp
    SearchResultUtils.cpp      # ADD THIS
    UIRenderer.cpp             # ADD THIS
    CommandLineArgs.cpp
    Settings.cpp
    FileIndex.cpp
    # ... rest of existing files
)
```

### Task 4: Add macOS Font Configuration (Optional but Recommended)

**File**: `main_mac.mm` or create `FontUtils_mac.cpp`

**For now**: macOS can use default ImGui fonts, but for feature parity, we should add font configuration similar to Windows.

**Option 1**: Use default fonts (quickest)
- ImGui default font is fine for initial implementation
- Can be enhanced later

**Option 2**: Create `FontUtils_mac.cpp` (for full parity)
- Map font families to macOS system fonts
- Configure ImGui fonts similar to Windows
- Apply font settings from `AppSettings`

**Recommendation**: Start with Option 1, add Option 2 later if needed.

### Task 5: Handle Window Size Persistence

**File**: `main_mac.mm`

**Add window size persistence** (similar to Windows):
```cpp
// After settings load
int window_width = settings.windowWidth > 0 ? settings.windowWidth : 1280;
int window_height = settings.windowHeight > 0 ? settings.windowHeight : 800;

// Update window creation
GLFWwindow *window = glfwCreateWindow(window_width, window_height, "FindHelper", NULL, NULL);

// Before cleanup, save window size
int current_width, current_height;
glfwGetWindowSize(window, &current_width, &current_height);
if (current_width >= 640 && current_width <= 4096) {
    settings.windowWidth = current_width;
}
if (current_height >= 480 && current_height <= 2160) {
    settings.windowHeight = current_height;
}
SaveSettings(settings);
```

---

## Step 3: Verify All Source Files Are Included

**File**: `CMakeLists.txt`

**Checklist**:
- [ ] Verify `UIRenderer.cpp` is in macOS `APP_SOURCES`
- [ ] Verify `SearchResultUtils.cpp` is in macOS `APP_SOURCES`
- [ ] Verify all other required files are present
- [ ] Test macOS build

---

## Step 4: Platform-Specific File Operations

**Goal**: Implement macOS file operations (open in Finder, reveal in Finder, copy path).

**Files to create/modify**:
- `FileOperations_mac.cpp` (new file) or modify `FileOperations.cpp` with `#ifdef _WIN32`

**Functions to implement**:
- `OpenFileDefault(const std::string &path)` - Open file in default application
- `OpenParentFolder(const std::string &path)` - Reveal in Finder
- `CopyPathToClipboard(const std::string &path)` - Copy path to clipboard

**macOS APIs to use**:
- `NSWorkspace` for opening files
- `NSWorkspace` for revealing files in Finder
- `NSPasteboard` for clipboard operations

**Integration**: Update `UIRenderer.cpp` to call macOS file operations when `native_window` is `nullptr` (macOS).

---

## Testing Checklist

After Step 2, verify:

### Search Functionality
- [ ] Path pattern search works
- [ ] Filename pattern search works
- [ ] Extension filter works
- [ ] Folders-only filter works
- [ ] Case-sensitive/insensitive toggle works
- [ ] Time filter works (Today, This Week, This Month, This Year, Older)
- [ ] Auto-search (debounced) works
- [ ] Auto-refresh works
- [ ] Manual search trigger (F5) works
- [ ] Search result sorting works (all 5 columns)
- [ ] Saved searches work (save, load, delete)

### UI Components
- [ ] Search input fields display correctly
- [ ] Search controls display correctly
- [ ] Search results table displays correctly
- [ ] Status bar displays correctly
- [ ] Settings window opens and works
- [ ] Metrics window opens and works
- [ ] Help popups work
- [ ] Saved search popups work

### Keyboard Shortcuts
- [ ] Ctrl+F focuses filename input
- [ ] F5 refreshes search
- [ ] Escape clears all filters

### Settings & Persistence
- [ ] Settings window works
- [ ] Window size persists across sessions
- [ ] Settings are saved to JSON file

---

## Estimated Time

- **Step 2**: 3-4 hours
  - Task 1: 15 minutes
  - Task 2: 30 minutes
  - Task 3: 15 minutes
  - Task 4: 30 minutes (optional)
  - Task 5: 15 minutes
  - Testing: 2-3 hours

- **Step 3**: 30 minutes (verification)

- **Step 4**: 2-3 hours (file operations implementation)

**Total**: ~6-8 hours to complete Phase 2

---

## Quick Start

1. **Update `main_mac.mm`**:
   - Add includes
   - Add state management
   - Replace stub UI with `ApplicationLogic::Update()` and `UIRenderer::RenderMainWindow()`

2. **Update `CMakeLists.txt`**:
   - Add `UIRenderer.cpp` to macOS `APP_SOURCES`
   - Add `SearchResultUtils.cpp` to macOS `APP_SOURCES`

3. **Build and test**:
   ```bash
   cmake -S . -B build
   cmake --build build
   ./build/FindHelper --index-from-file /path/to/index.txt
   ```

4. **Verify feature parity** with Windows

---

## Notes

- **macOS font configuration**: Can be deferred to later if default fonts are acceptable
- **File operations**: Can be implemented incrementally (start with copy path, then add open/reveal)
- **Testing**: Focus on verifying feature parity with Windows, especially search functionality

