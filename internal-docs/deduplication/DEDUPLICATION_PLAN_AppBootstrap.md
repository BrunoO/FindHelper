# Deduplication Plan: AppBootstrap (Windows & Linux)

**Target Files:**
- `src/platform/windows/AppBootstrap_win.cpp` - 9 duplicate blocks
- `src/platform/linux/AppBootstrap_linux.cpp` - 9 duplicate blocks

**Goal:** Extract common bootstrap patterns into shared utilities while maintaining platform-specific code separation.

---

## Analysis Summary

### Identical Code Blocks (100% Duplicate)
1. **`LogCpuInformation()`** - Identical in both files
   - Windows: lines 79-101
   - Linux: lines 50-72
   - **Action:** Extract to common utility

2. **`ApplyIntOverride()` template function** - Identical
   - Windows: lines 167-173
   - Linux: lines 88-94
   - **Action:** Extract to common header

3. **`ApplyStringOverride()`** - Identical
   - Windows: lines 176-181
   - Linux: lines 97-102
   - **Action:** Extract to common header

4. **`ApplyCommandLineOverrides()`** - Identical
   - Windows: lines 183-195
   - Linux: lines 104-116
   - **Action:** Extract to common header

### Very Similar Code Blocks (Minor Differences)
5. **`LoadIndexFromFile()`** - Nearly identical
   - Windows: lines 154-164
   - Linux: lines 74-85
   - **Difference:** Linux has extra logging (`LOG_INFO_BUILD` on success, `LOG_ERROR_BUILD` on failure)
   - **Action:** Extract to common utility with optional logging parameter

6. **`CreateGlfwWindow()`** - Very similar
   - Windows: lines 197-208
   - Linux: lines 127-137
   - **Differences:** 
     - Window title: "Find Helper" (Windows) vs "FindHelper" (Linux)
     - Result type: `AppBootstrapResult&` vs `AppBootstrapResultLinux&`
   - **Action:** Extract to template function or use base class reference

7. **Window resize callback setup** - Similar pattern
   - Windows: lines 241-252 (inside `InitializeDirectXAndWindow`)
   - Linux: lines 155-166 (inside `InitializeOpenGLAndWindow`)
   - **Differences:** Different renderer types (DirectXManager vs OpenGLManager)
   - **Action:** Extract callback setup to template function using RendererInterface

8. **GLFW error callback setup** - Identical lambda
   - Windows: lines 375-377
   - Linux: lines 256-258
   - **Action:** Extract to common utility function

### Similar Structure (Different Implementation Details)
9. **`InitializeImGuiBackend()`** - Similar structure, different details
   - Windows: lines 264-301
   - Linux: lines 184-207
   - **Differences:**
     - Style constants (Windows has more detailed constants)
     - Backend initialization (`ImGui_ImplGlfw_InitForOther` vs `ImGui_ImplGlfw_InitForOpenGL`)
     - Cleanup on failure (Windows has more comprehensive cleanup)
   - **Action:** Extract common parts (ImGui context creation, style setup) to utility, keep backend-specific parts platform-specific

10. **Exception handling patterns** - Similar structure
    - Windows: lines 429-443 (catches `bad_alloc`, `runtime_error`, `...`)
    - Linux: lines 322-359 (catches more specific exceptions: `system_error`, `bad_alloc`, `invalid_argument`, `logic_error`, `runtime_error`, `exception`, `...`)
    - **Action:** Extract common exception handling wrapper, allow platform-specific exception types

---

## Implementation Plan

### Phase 1: Extract Identical Functions (High Priority)

**Create:** `src/core/AppBootstrapCommon.h` (header-only utilities)

**Extract:**
1. `LogCpuInformation()` - Move to common header
2. `ApplyIntOverride()` template - Move to common header
3. `ApplyStringOverride()` - Move to common header
4. `ApplyCommandLineOverrides()` - Move to common header
5. `SetupGlfwErrorCallback()` - Extract error callback lambda to function

**Benefits:**
- Eliminates ~80-100 lines of duplicate code
- Single source of truth for command-line override logic
- Easier to maintain and test

**Files to Create:**
- `src/core/AppBootstrapCommon.h` - Common bootstrap utilities

**Files to Modify:**
- `src/platform/windows/AppBootstrap_win.cpp` - Remove duplicates, include common header
- `src/platform/linux/AppBootstrap_linux.cpp` - Remove duplicates, include common header

---

### Phase 2: Extract Similar Functions with Platform Abstraction (Medium Priority)

**Extract to `src/core/AppBootstrapCommon.h`:**

1. **`LoadIndexFromFile()`** - Make logging optional or consistent
   ```cpp
   bool LoadIndexFromFile(const std::string& file_path, FileIndex& file_index, bool verbose_logging = true);
   ```

2. **`CreateGlfwWindow()`** - Use base class reference
   ```cpp
   template<typename BootstrapResult>
   GLFWwindow* CreateGlfwWindow(const AppSettings& app_settings, BootstrapResult& result, const char* window_title = "FindHelper");
   ```

3. **Window resize callback setup** - Extract to template using RendererInterface
   ```cpp
   template<typename RendererType>
   void SetupWindowResizeCallback(GLFWwindow* window, RendererType* renderer, int* last_width, int* last_height);
   ```

**Benefits:**
- Eliminates ~50-70 lines of duplicate code
- Maintains type safety with templates
- Platform-specific differences handled via parameters

---

### Phase 3: Extract ImGui Initialization Common Parts (Lower Priority)

**Extract to `src/core/AppBootstrapCommon.h`:**

1. **Common ImGui context creation and style setup**
   ```cpp
   void InitializeImGuiContext();
   void ConfigureImGuiStyle(bool viewports_enabled);
   ```

2. **Keep platform-specific parts separate:**
   - Backend initialization (DirectX vs OpenGL)
   - Platform-specific cleanup on failure

**Benefits:**
- Eliminates ~30-40 lines of duplicate code
- Maintains platform-specific backend initialization
- Common style configuration in one place

---

### Phase 4: Extract Exception Handling Wrapper (Lower Priority)

**Create:** `src/core/AppBootstrapCommon.h`

**Extract:**
```cpp
template<typename BootstrapResult>
BootstrapResult HandleBootstrapException(
    const std::exception& e,
    const std::string& error_prefix,
    BootstrapResult& result,
    std::function<void(BootstrapResult&)> cleanup_fn);
```

**Benefits:**
- Consistent exception handling across platforms
- Reduces duplicate exception handling code
- Allows platform-specific cleanup functions

**Note:** This may be lower priority as exception handling differences may be intentional (Linux catches more specific exceptions).

---

## File Structure

### New File: `src/core/AppBootstrapCommon.h`

```cpp
#pragma once

#include "core/CommandLineArgs.h"
#include "core/Settings.h"
#include "index/FileIndex.h"
#include "utils/Logger.h"
#include "utils/CpuFeatures.h"

#include <string>
#include <string_view>
#include <GLFW/glfw3.h>

namespace AppBootstrapCommon {
  // Phase 1: Identical functions
  void LogCpuInformation();
  void SetupGlfwErrorCallback();
  
  template<typename T>
  void ApplyIntOverride(T override_value, T& setting_value, const char* setting_name, T invalid_value = static_cast<T>(-1));
  
  void ApplyStringOverride(std::string_view override_value, std::string& setting_value, const char* setting_name);
  void ApplyCommandLineOverrides(const CommandLineArgs& cmd_args, AppSettings& app_settings);
  
  // Phase 2: Similar functions with abstraction
  bool LoadIndexFromFile(const std::string& file_path, FileIndex& file_index, bool verbose_logging = true);
  
  template<typename BootstrapResult>
  GLFWwindow* CreateGlfwWindow(const AppSettings& app_settings, BootstrapResult& result, const char* window_title = "FindHelper");
  
  template<typename RendererType>
  void SetupWindowResizeCallback(GLFWwindow* window, RendererType* renderer, int* last_width, int* last_height);
  
  // Phase 3: ImGui common parts
  void InitializeImGuiContext();
  void ConfigureImGuiStyle(bool viewports_enabled);
}
```

---

## Implementation Steps

### Step 1: Create Common Header
1. Create `src/core/AppBootstrapCommon.h`
2. Move identical functions from both files
3. Add proper includes and namespace

### Step 2: Update Windows Implementation
1. Include `AppBootstrapCommon.h`
2. Remove duplicate functions
3. Replace calls with `AppBootstrapCommon::` namespace
4. Test compilation and functionality

### Step 3: Update Linux Implementation
1. Include `AppBootstrapCommon.h`
2. Remove duplicate functions
3. Replace calls with `AppBootstrapCommon::` namespace
4. Test compilation and functionality

### Step 4: Verify and Test
1. Run build tests on macOS (required)
2. Verify Windows build (if possible)
3. Verify Linux build (if possible)
4. Run application on all platforms to ensure functionality

---

## Expected Results

### Code Reduction
- **Phase 1:** ~80-100 lines eliminated
- **Phase 2:** ~50-70 lines eliminated
- **Phase 3:** ~30-40 lines eliminated
- **Total:** ~160-210 lines of duplicate code eliminated

### Duplo Impact
- **Before:** 9 duplicate blocks in each file (18 total)
- **After (Phase 1):** ~5-6 duplicate blocks remaining
- **After (Phase 1+2):** ~2-3 duplicate blocks remaining
- **After (All Phases):** 0-1 duplicate blocks remaining

### Maintainability
- Single source of truth for common bootstrap logic
- Easier to add new platforms (macOS can reuse common code)
- Consistent behavior across platforms
- Easier to test common utilities

---

## Considerations

### Platform-Specific Code Preservation
- **Windows-specific:** COM initialization, admin privilege checking, DirectX initialization, UsnMonitor startup
- **Linux-specific:** OpenGL initialization, different exception types
- **Common:** CPU logging, command-line overrides, GLFW window creation, ImGui context setup

### Template Usage
- Use templates for type-agnostic functions (window creation, resize callbacks)
- Maintain type safety while eliminating duplication
- Allow compiler to optimize per-platform

### Backward Compatibility
- No API changes to public interfaces
- Internal refactoring only
- All existing functionality preserved

### Testing Strategy
1. **Unit tests:** Test common utilities in isolation
2. **Integration tests:** Verify bootstrap on each platform
3. **Manual testing:** Run application on Windows and Linux

---

## Risk Assessment

### Low Risk
- Phase 1 (identical functions) - Straightforward extraction
- Phase 2 (similar functions) - Well-defined abstractions

### Medium Risk
- Phase 3 (ImGui initialization) - Need to ensure platform-specific parts work correctly
- Phase 4 (exception handling) - May need platform-specific exception types

### Mitigation
- Implement phases incrementally
- Test after each phase
- Keep platform-specific code clearly separated
- Maintain comprehensive error handling

---

## Success Criteria

1. ✅ All identical code blocks extracted to common header
2. ✅ Similar code blocks abstracted with templates/parameters
3. ✅ Platform-specific code remains clearly separated
4. ✅ No functionality changes (behavior preserved)
5. ✅ All platforms compile and run successfully
6. ✅ Duplo reports significant reduction in duplicate blocks
7. ✅ Code is easier to maintain and extend

---

## Notes

- This refactoring respects the cross-platform nature of the project
- Platform-specific code blocks (`#ifdef _WIN32`, etc.) are preserved
- The refactoring follows DRY principle while maintaining platform separation
- Common utilities can be reused for macOS bootstrap in the future
