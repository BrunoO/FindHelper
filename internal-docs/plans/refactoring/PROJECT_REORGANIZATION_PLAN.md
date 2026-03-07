# Project Reorganization Plan

**Date:** January 2, 2025  
**Last Updated:** January 4, 2026  
**Status:** Proposal  
**Goal:** Restructure project folder to follow C++ best practices as the codebase grows

---

## Executive Summary

This document outlines a step-by-step plan to reorganize the project structure from a flat root directory to a well-organized, modular structure that follows C++ project best practices. The reorganization will improve maintainability, discoverability, and scalability while maintaining cross-platform compatibility.

---

## Current State Analysis

### Root Directory Issues
- **100+ files** in root directory (source, headers, docs, configs, resources)
- Mixed concerns: application code, platform code, utilities, documentation
- Difficult to navigate and understand project structure
- Platform-specific files scattered (e.g., `AppBootstrap_*.cpp`, `FileOperations_*.cpp`)

### Existing Good Structure
- ✅ `ui/` - UI components already organized
- ✅ `tests/` - Test files already organized  
- ✅ `docs/` - Documentation already organized
- ✅ `external/` - Git submodules properly organized
- ✅ `scripts/` - Build scripts organized

---

## Proposed Directory Structure

```
USN_WINDOWS/
├── CMakeLists.txt              # Root CMake configuration
├── README.md                    # Main project documentation
├── .gitignore
│
├── src/                        # All application source code
│   ├── core/                   # Core application components
│   │   ├── Application.cpp/h
│   │   ├── ApplicationLogic.cpp/h
│   │   ├── CommandLineArgs.cpp/h
│   │   ├── Settings.cpp/h
│   │   ├── Version.h
│   │   ├── AppBootstrapResultBase.h
│   │   └── main_common.h
│   │
│   ├── index/                  # File indexing system
│   │   ├── FileIndex.cpp/h
│   │   ├── FileIndexStorage.cpp/h
│   │   ├── FileIndexMaintenance.cpp/h
│   │   ├── IndexFromFilePopulator.cpp/h
│   │   ├── InitialIndexPopulator.cpp/h
│   │   ├── ISearchableIndex.h
│   │   └── LazyAttributeLoader.cpp/h
│   │
│   ├── search/                 # Search functionality
│   │   ├── SearchController.cpp/h
│   │   ├── SearchWorker.cpp/h
│   │   ├── ParallelSearchEngine.cpp/h
│   │   ├── SearchThreadPool.cpp/h
│   │   ├── SearchStatisticsCollector.cpp/h
│   │   ├── SearchResultUtils.cpp/h
│   │   ├── ISearchExecutor.h
│   │   ├── SearchContext.h
│   │   └── SearchInputField.h
│   │
│   ├── path/                   # Path operations and utilities
│   │   ├── PathOperations.cpp/h
│   │   ├── PathUtils.cpp/h
│   │   ├── PathBuilder.cpp/h
│   │   ├── PathStorage.cpp/h
│   │   ├── PathPatternMatcher.cpp/h
│   │   ├── PathConstants.h
│   │   └── DirectoryResolver.cpp/h
│   │
│   ├── usn/                    # USN journal monitoring
│   │   ├── UsnMonitor.cpp/h
│   │   └── UsnRecordUtils.h
│   │
│   ├── crawler/                # File system crawling
│   │   ├── FolderCrawler.cpp/h
│   │   └── IndexOperations.cpp/h
│   │
│   ├── ui/                     # UI components (keep existing)
│   │   ├── FilterPanel.cpp/h
│   │   ├── MetricsWindow.cpp/h
│   │   ├── Popups.cpp/h
│   │   ├── ResultsTable.cpp/h
│   │   ├── SearchControls.cpp/h
│   │   ├── SearchInputs.cpp/h
│   │   ├── SettingsWindow.cpp/h
│   │   └── StatusBar.cpp/h
│   │
│   ├── platform/               # Platform-specific code
│   │   ├── windows/
│   │   │   ├── AppBootstrap_win.cpp
│   │   │   ├── AppBootstrap.h
│   │   │   ├── main_win.cpp
│   │   │   ├── FileOperations_win.cpp
│   │   │   ├── FileOperations.h
│   │   │   ├── FontUtils_win.cpp/h
│   │   │   ├── ThreadUtils.cpp/h
│   │   │   ├── StringUtils.cpp/h
│   │   │   ├── DirectXManager.cpp/h
│   │   │   ├── ShellContextUtils.cpp/h
│   │   │   ├── DragDropUtils.cpp/h
│   │   │   ├── PrivilegeUtils.cpp/h
│   │   │   └── resource.h / resource.rc
│   │   │
│   │   ├── macos/
│   │   │   ├── AppBootstrap_mac.mm/h
│   │   │   ├── main_mac.mm
│   │   │   ├── FileOperations_mac.mm/h
│   │   │   ├── FontUtils_mac.mm/h
│   │   │   ├── ThreadUtils_mac.cpp/h
│   │   │   ├── StringUtils_mac.cpp/h
│   │   │   └── MetalManager.mm/h
│   │   │
│   │   └── linux/
│   │       ├── AppBootstrap_linux.cpp/h
│   │       ├── main_linux.cpp
│   │       ├── FileOperations_linux.cpp/h
│   │       ├── FontUtils_linux.cpp/h
│   │       └── OpenGLManager.cpp/h
│   │
│   ├── utils/                  # Cross-platform utilities
│   │   ├── StringUtils.cpp/h
│   │   ├── ThreadUtils.cpp/h
│   │   ├── StringSearch.cpp/h
│   │   ├── StringSearchAVX2.cpp/h
│   │   ├── SimpleRegex.h
│   │   ├── StdRegexUtils.h
│   │   ├── StringSearch.h
│   │   ├── CpuFeatures.cpp/h
│   │   ├── Logger.h
│   │   ├── LoggingUtils.cpp/h
│   │   ├── LoadBalancingStrategy.cpp/h
│   │   ├── ThreadPool.cpp/h
│   │   ├── ClipboardUtils.cpp/h
│   │   ├── HashMapAliases.h
│   │   ├── LightweightCallable.h
│   │   ├── PlatformTypes.h
│   │   ├── FileSystemUtils.h
│   │   ├── FileTimeTypes.h
│   │   └── ScopedHandle.h
│   │
│   ├── filters/                # Search filters
│   │   ├── TimeFilter.h
│   │   ├── TimeFilterUtils.cpp/h
│   │   ├── SizeFilter.h
│   │   └── SizeFilterUtils.cpp/h
│   │
│   ├── gui/                    # GUI state and utilities
│   │   ├── GuiState.cpp/h
│   │   ├── ImGuiUtils.h
│   │   └── RendererInterface.h
│   │
│   └── api/                    # External API integrations
│       ├── GeminiApiUtils.cpp/h
│       ├── GeminiApiHttp.h
│       ├── GeminiApiHttp_win.cpp
│       ├── GeminiApiHttp_mac.mm
│       └── GeminiApiHttp_linux.cpp
│
├── include/                    # Public headers (if needed for library)
│   └── (empty for now, or public API headers)
│
├── tests/                      # Test files (keep existing structure)
│   ├── CoverageAllSourcesMain.cpp
│   ├── *Tests.cpp
│   └── ...
│
├── external/                   # Git submodules (keep existing)
│   ├── imgui/
│   ├── doctest/
│   └── ...
│
├── scripts/                    # Build scripts (keep existing)
│   └── ...
│
├── docs/                       # Documentation (keep existing)
│   └── ...
│
├── resources/                  # Resource files
│   ├── icons/
│   │   ├── app_icon.icns
│   │   └── app_icon.ico
│   ├── windows/
│   │   ├── app.manifest
│   │   └── resource.rc
│   └── ...
│
├── build/                      # Build artifacts (gitignored)
├── build_tests/                # Test build artifacts (gitignored)
└── coverage/                   # Coverage reports (gitignored)
```

---

## Step-by-Step Migration Plan

### Phase 1: Preparation (Low Risk)

**Goal:** Set up new directory structure without moving files yet.

#### Step 1.1: Create New Directory Structure
- Create all new directories (`src/`, `src/core/`, `src/index/`, etc.)
- Verify directory structure matches plan
- **No code changes yet**

#### Step 1.2: Update `.gitignore`
- Ensure build directories are properly ignored
- Add any new temporary directories if needed

#### Step 1.3: Document Current File Locations
- Create a mapping file: `docs/REORGANIZATION_FILE_MAPPING.md`
- List all files and their current → new locations
- This serves as a reference during migration

**Risk Level:** ⚠️ Low  
**Estimated Time:** 30 minutes  
**Validation:** Verify directories exist, no build impact

---

### Phase 2: Move Platform-Specific Code (Medium Risk)

**Goal:** Consolidate platform-specific files into `src/platform/`.

#### Step 2.1: Create Platform Directories
```bash
mkdir -p src/platform/windows
mkdir -p src/platform/macos
mkdir -p src/platform/linux
```

#### Step 2.2: Move Windows Files
Move from root to `src/platform/windows/`:
- `AppBootstrap_win.cpp` → `src/platform/windows/AppBootstrap_win.cpp`
- `AppBootstrap.h` → `src/platform/windows/AppBootstrap.h` (Windows-specific header)
- `main_win.cpp` → `src/platform/windows/main_win.cpp`
- `FileOperations_win.cpp` → `src/platform/windows/FileOperations_win.cpp`
- `FileOperations.h` → `src/platform/windows/FileOperations.h` (if Windows-specific, otherwise keep common)
- `FontUtils_win.cpp/h` → `src/platform/windows/FontUtils_win.cpp/h`
- `ThreadUtils_win.cpp` → `src/platform/windows/ThreadUtils_win.cpp` (if exists, otherwise use common)
- `StringUtils_win.cpp` → `src/platform/windows/StringUtils_win.cpp`
- `DirectXManager.cpp/h` → `src/platform/windows/DirectXManager.cpp/h`
- `ShellContextUtils.cpp/h` → `src/platform/windows/ShellContextUtils.cpp/h`
- `DragDropUtils.cpp/h` → `src/platform/windows/DragDropUtils.cpp/h`
- `PrivilegeUtils.cpp/h` → `src/platform/windows/PrivilegeUtils.cpp/h`
- `resource.h` → `src/platform/windows/resource.h`
- `resource.rc` → `src/platform/windows/resource.rc`

**Note:** `AppBootstrapResultBase.h` and `main_common.h` are shared/common files and should be moved to `src/core/` in Phase 3, not here.

#### Step 2.3: Move macOS Files
Move from root to `src/platform/macos/`:
- `AppBootstrap_mac.mm/h` → `src/platform/macos/AppBootstrap_mac.mm/h`
- `main_mac.mm` → `src/platform/macos/main_mac.mm`
- `FileOperations_mac.mm/h` → `src/platform/macos/FileOperations_mac.mm/h`
- `FontUtils_mac.mm/h` → `src/platform/macos/FontUtils_mac.mm/h`
- `ThreadUtils_mac.cpp/h` → `src/platform/macos/ThreadUtils_mac.cpp/h`
- `StringUtils_mac.cpp/h` → `src/platform/macos/StringUtils_mac.cpp/h`
- `MetalManager.mm/h` → `src/platform/macos/MetalManager.mm/h`

**Note:** `AppBootstrapResultBase.h` and `main_common.h` are shared/common files and should be moved to `src/core/` in Phase 3, not here.

#### Step 2.4: Move Linux Files
Move from root to `src/platform/linux/`:
- `AppBootstrap_linux.cpp/h` → `src/platform/linux/AppBootstrap_linux.cpp/h`
- `main_linux.cpp` → `src/platform/linux/main_linux.cpp`
- `FileOperations_linux.cpp/h` → `src/platform/linux/FileOperations_linux.cpp/h`
- `FontUtils_linux.cpp/h` → `src/platform/linux/FontUtils_linux.cpp/h`
- `OpenGLManager.cpp/h` → `src/platform/linux/OpenGLManager.cpp/h`

**Note:** `AppBootstrapResultBase.h` and `main_common.h` are shared/common files and should be moved to `src/core/` in Phase 3, not here.

#### Step 2.5: Update CMakeLists.txt
- Update all file paths in `CMakeLists.txt` to reflect new locations
- Test build on all three platforms (Windows, macOS, Linux)
- **Critical:** Verify platform-specific conditional compilation still works

**Risk Level:** ⚠️ Medium  
**Estimated Time:** 2-3 hours  
**Validation:** 
- ✅ All platforms build successfully
- ✅ No broken includes
- ✅ Platform-specific code compiles correctly

---

### Phase 3: Move Core Application Code (Medium Risk)

**Goal:** Move core application components to `src/core/`.

#### Step 3.1: Create Core Directory
```bash
mkdir -p src/core
```

#### Step 3.2: Move Core Files
Move from root to `src/core/`:
- `Application.cpp/h` → `src/core/Application.cpp/h`
- `ApplicationLogic.cpp/h` → `src/core/ApplicationLogic.cpp/h`
- `CommandLineArgs.cpp/h` → `src/core/CommandLineArgs.cpp/h`
- `Settings.cpp/h` → `src/core/Settings.cpp/h`
- `Version.h` → `src/core/Version.h`
- `AppBootstrapResultBase.h` → `src/core/AppBootstrapResultBase.h` (shared across platforms)
- `main_common.h` → `src/core/main_common.h` (shared across platforms)

#### Step 3.3: Update Includes
- Update all `#include "Application.h"` to `#include "core/Application.h"` (or use relative paths)
- Update CMakeLists.txt paths
- Test build

**Risk Level:** ⚠️ Medium  
**Estimated Time:** 1-2 hours  
**Validation:** 
- ✅ Application builds and runs
- ✅ All includes resolve correctly

---

### Phase 4: Move Index System (Low Risk)

**Goal:** Move file indexing components to `src/index/`.

#### Step 4.1: Create Index Directory
```bash
mkdir -p src/index
```

#### Step 4.2: Move Index Files
Move from root to `src/index/`:
- `FileIndex.cpp/h` → `src/index/FileIndex.cpp/h`
- `FileIndexStorage.cpp/h` → `src/index/FileIndexStorage.cpp/h`
- `FileIndexMaintenance.cpp/h` → `src/index/FileIndexMaintenance.cpp/h`
- `IndexFromFilePopulator.cpp/h` → `src/index/IndexFromFilePopulator.cpp/h`
- `InitialIndexPopulator.cpp/h` → `src/index/InitialIndexPopulator.cpp/h`
- `ISearchableIndex.h` → `src/index/ISearchableIndex.h`
- `LazyAttributeLoader.cpp/h` → `src/index/LazyAttributeLoader.cpp/h`
- `LazyValue.h` → `src/index/LazyValue.h`

#### Step 4.3: Update Includes and CMakeLists.txt
- Update includes
- Update CMakeLists.txt
- Test build

**Risk Level:** ⚠️ Low  
**Estimated Time:** 1 hour  
**Validation:** 
- ✅ Index system builds
- ✅ Tests pass

---

### Phase 5: Move Search Components (Low Risk)

**Goal:** Move search-related files to `src/search/`.

#### Step 5.1: Create Search Directory
```bash
mkdir -p src/search
```

#### Step 5.2: Move Search Files
Move from root to `src/search/`:
- `SearchController.cpp/h` → `src/search/SearchController.cpp/h`
- `SearchWorker.cpp/h` → `src/search/SearchWorker.cpp/h`
- `ParallelSearchEngine.cpp/h` → `src/search/ParallelSearchEngine.cpp/h`
- `SearchThreadPool.cpp/h` → `src/search/SearchThreadPool.cpp/h`
- `SearchStatisticsCollector.cpp/h` → `src/search/SearchStatisticsCollector.cpp/h`
- `SearchResultUtils.cpp/h` → `src/search/SearchResultUtils.cpp/h`
- `ISearchExecutor.h` → `src/search/ISearchExecutor.h`
- `SearchContext.h` → `src/search/SearchContext.h`
- `SearchInputField.h` → `src/search/SearchInputField.h`
- `SearchPatternUtils.h` → `src/search/SearchPatternUtils.h`

#### Step 5.3: Update Includes and CMakeLists.txt
- Update includes
- Update CMakeLists.txt
- Test build

**Risk Level:** ⚠️ Low  
**Estimated Time:** 1 hour  
**Validation:** 
- ✅ Search functionality works
- ✅ Tests pass

---

### Phase 6: Move Path Operations (Low Risk)

**Goal:** Move path-related files to `src/path/`.

#### Step 6.1: Create Path Directory
```bash
mkdir -p src/path
```

#### Step 6.2: Move Path Files
Move from root to `src/path/`:
- `PathOperations.cpp/h` → `src/path/PathOperations.cpp/h`
- `PathUtils.cpp/h` → `src/path/PathUtils.cpp/h`
- `PathBuilder.cpp/h` → `src/path/PathBuilder.cpp/h`
- `PathStorage.cpp/h` → `src/path/PathStorage.cpp/h`
- `PathPatternMatcher.cpp/h` → `src/path/PathPatternMatcher.cpp/h`
- `PathConstants.h` → `src/path/PathConstants.h`
- `DirectoryResolver.cpp/h` → `src/path/DirectoryResolver.cpp/h`

#### Step 6.3: Update Includes and CMakeLists.txt
- Update includes
- Update CMakeLists.txt
- Test build

**Risk Level:** ⚠️ Low  
**Estimated Time:** 1 hour  
**Validation:** 
- ✅ Path operations work
- ✅ Tests pass

---

### Phase 7: Move Remaining Components (Low Risk)

**Goal:** Move remaining organized components.

#### Step 7.1: Move USN Components
- Create `src/usn/`
- Move `UsnMonitor.cpp/h` → `src/usn/UsnMonitor.cpp/h`
- Move `UsnRecordUtils.h` → `src/usn/UsnRecordUtils.h`

#### Step 7.2: Move Crawler Components
- Create `src/crawler/`
- Move `FolderCrawler.cpp/h` → `src/crawler/FolderCrawler.cpp/h`
- Move `IndexOperations.cpp/h` → `src/crawler/IndexOperations.cpp/h`

#### Step 7.3: Move Utilities
- Create `src/utils/`
- Move cross-platform utilities:
  - `StringUtils.h` → `src/utils/StringUtils.h` (common header, platform impls in platform/)
  - `ThreadUtils.h` → `src/utils/ThreadUtils.h` (common header, platform impls in platform/)
  - `StringSearch.cpp/h` → `src/utils/StringSearch.cpp/h` (if exists)
  - `StringSearchAVX2.cpp/h` → `src/utils/StringSearchAVX2.cpp/h`
  - `SimpleRegex.h` → `src/utils/SimpleRegex.h`
  - `StdRegexUtils.h` → `src/utils/StdRegexUtils.h`
  - `StringSearch.h` → `src/utils/StringSearch.h`
  - `CpuFeatures.cpp/h` → `src/utils/CpuFeatures.cpp/h`
  - `Logger.h` → `src/utils/Logger.h`
  - `LoggingUtils.cpp/h` → `src/utils/LoggingUtils.cpp/h`
  - `LoadBalancingStrategy.cpp/h` → `src/utils/LoadBalancingStrategy.cpp/h`
  - `ThreadPool.cpp/h` → `src/utils/ThreadPool.cpp/h`
  - `ClipboardUtils.cpp/h` → `src/utils/ClipboardUtils.cpp/h`
  - `HashMapAliases.h` → `src/utils/HashMapAliases.h`
  - `LightweightCallable.h` → `src/utils/LightweightCallable.h`
  - `PlatformTypes.h` → `src/utils/PlatformTypes.h`
  - `FileSystemUtils.h` → `src/utils/FileSystemUtils.h`
  - `FileTimeTypes.h` → `src/utils/FileTimeTypes.h`
  - `ScopedHandle.h` → `src/utils/ScopedHandle.h`

#### Step 7.4: Move Filters
- Create `src/filters/`
- Move `TimeFilter.h` → `src/filters/TimeFilter.h`
- Move `TimeFilterUtils.cpp/h` → `src/filters/TimeFilterUtils.cpp/h`
- Move `SizeFilter.h` → `src/filters/SizeFilter.h`
- Move `SizeFilterUtils.cpp/h` → `src/filters/SizeFilterUtils.cpp/h`

#### Step 7.5: Move GUI Components
- Create `src/gui/`
- Move `GuiState.cpp/h` → `src/gui/GuiState.cpp/h`
- Move `ImGuiUtils.h` → `src/gui/ImGuiUtils.h`
- Move `RendererInterface.h` → `src/gui/RendererInterface.h`

#### Step 7.6: Move API Components
- Create `src/api/`
- Move `GeminiApiUtils.cpp/h` → `src/api/GeminiApiUtils.cpp/h`
- Move `GeminiApiHttp.h` → `src/api/GeminiApiHttp.h`
- Move `GeminiApiHttp_win.cpp` → `src/api/GeminiApiHttp_win.cpp`
- Move `GeminiApiHttp_mac.mm` → `src/api/GeminiApiHttp_mac.mm`
- Move `GeminiApiHttp_linux.cpp` → `src/api/GeminiApiHttp_linux.cpp`

#### Step 7.7: Move UI Components
- Move `ui/` → `src/ui/` (already organized, just relocate)

#### Step 7.8: Update All Includes and CMakeLists.txt
- Update includes throughout codebase
- Update CMakeLists.txt
- Test build on all platforms

**Risk Level:** ⚠️ Low-Medium  
**Estimated Time:** 2-3 hours  
**Validation:** 
- ✅ All components build
- ✅ All tests pass
- ✅ Application runs correctly

---

### Phase 8: Move Resources (Low Risk)

**Goal:** Organize resource files.

#### Step 8.1: Create Resources Directory
```bash
mkdir -p resources/icons
mkdir -p resources/windows
```

#### Step 8.2: Move Resource Files
- `app_icon.icns` → `resources/icons/app_icon.icns`
- `app_icon.ico` → `resources/icons/app_icon.ico`
- `app.manifest` → `resources/windows/app.manifest`
- (resource.rc already moved in Phase 2)

#### Step 8.3: Update CMakeLists.txt
- Update resource file paths in CMakeLists.txt
- Test build

**Risk Level:** ⚠️ Low  
**Estimated Time:** 30 minutes  
**Validation:** 
- ✅ Resources are included in builds
- ✅ Icons display correctly

---

### Phase 9: Move Documentation and Cleanup (Low Risk)

**Goal:** Organize root-level documentation files.

#### Step 9.1: Move Documentation Files
Move root-level `.md` files to `docs/`:
- `CXX17_NAMING_CONVENTIONS.md` → `docs/CXX17_NAMING_CONVENTIONS.md`
- `BUILDING_ON_LINUX.md` → `docs/BUILDING_ON_LINUX.md`
- `FIXES_BENEFITS_EXPLAINED.md` → `docs/FIXES_BENEFITS_EXPLAINED.md`
- `Fixes_Rationale.md` → `docs/Fixes_Rationale.md`
- `ideas for new features.md` → `docs/ideas_for_new_features.md`
- `MEMORY_ANALYSIS_REPORT.md` → `docs/MEMORY_ANALYSIS_REPORT.md`
- `QUICK_WINS_FIXED.md` → `docs/QUICK_WINS_FIXED.md`
- `TEST_IMPROVEMENTS_VERIFICATION_REPORT.md` → `docs/TEST_IMPROVEMENTS_VERIFICATION_REPORT.md`
- `TestImprovements_Rationale.md` → `docs/TestImprovements_Rationale.md`
- `TIMELINE.md` → `docs/TIMELINE.md`
- `VERIFICATION_REPORT.md` → `docs/VERIFICATION_REPORT.md`

#### Step 9.2: Move Utility Scripts
- `count_lines.py` → `scripts/count_lines.py`
- `duplo` → `scripts/duplo` (or remove if not needed)
- `duplo_report.txt` → `docs/duplo_report.txt` (or remove)
- `duplo-macos.zip` → Remove or move to `build/` if needed

#### Step 9.3: Move Generated/Config Files
- `imgui.ini` → Keep in root (ImGui config, user-specific)
- `settings.json` → Keep in root (user settings) or move to `resources/`
- `GitVersion.h` → Keep in root (generated, may be gitignored)
- `Version.h` → Move to `src/core/Version.h` or keep in root

#### Step 9.4: Clean Up Root Directory
- Verify only essential files remain in root:
  - `CMakeLists.txt`
  - `README.md`
  - `.gitignore`
  - `AGENTS.md` (if needed)
  - Generated files (`GitVersion.h`, `imgui.ini`, `settings.json`)

**Risk Level:** ⚠️ Low  
**Estimated Time:** 1 hour  
**Validation:** 
- ✅ Documentation is accessible
- ✅ No broken links in documentation

---

### Phase 10: Update Include Paths Strategy (Critical)

**Goal:** Establish consistent include path strategy.

#### Option A: Relative Includes (Recommended for this project)
- Use relative paths: `#include "../core/Application.h"`
- Pros: Explicit, no path configuration needed
- Cons: More verbose, requires updates when moving files

#### Option B: Source Root Includes
- Add `src/` to include path in CMakeLists.txt
- Use: `#include "core/Application.h"`
- Pros: Cleaner includes, easier refactoring
- Cons: Requires CMake configuration

**Recommendation:** Use **Option B** (source root includes) for cleaner code.

#### Step 10.1: Update CMakeLists.txt Include Directories
```cmake
# Add src/ to include path
target_include_directories(USN_Monitor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # ... other includes
)
```

#### Step 10.2: Update All Include Statements
- Change `#include "Application.h"` → `#include "core/Application.h"`
- Change `#include "FileIndex.h"` → `#include "index/FileIndex.h"`
- Change `#include "SearchController.h"` → `#include "search/SearchController.h"`
- etc.

#### Step 10.3: Update Platform-Specific Includes
- Platform files may need special handling
- Consider: `#include "platform/windows/FileOperations.h"`

**Risk Level:** ⚠️ High (many files to update)  
**Estimated Time:** 3-4 hours  
**Validation:** 
- ✅ All includes resolve correctly
- ✅ All platforms build
- ✅ No include path conflicts

---

### Phase 11: Update Tests (Medium Risk)

**Goal:** Update test files to use new include paths.

#### Step 11.1: Update Test Includes
- Update all `#include` statements in test files
- Update test CMakeLists.txt if needed

#### Step 11.2: Verify Test Build
- Build tests on all platforms
- Run test suite
- Verify all tests pass

**Risk Level:** ⚠️ Medium  
**Estimated Time:** 1-2 hours  
**Validation:** 
- ✅ All tests build
- ✅ All tests pass

---

### Phase 12: Final Validation and Documentation (Low Risk)

**Goal:** Verify everything works and update documentation.

#### Step 12.1: Full Build Test
- Clean build on Windows
- Clean build on macOS
- Clean build on Linux
- Verify all platforms produce working executables

#### Step 12.2: Update README.md
- Update project structure section
- Update build instructions if needed
- Update any path references

#### Step 12.3: Update CMakeLists.txt Comments
- Add comments explaining directory structure
- Document any special include path handling

#### Step 12.4: Create Migration Summary
- Document what was moved where
- Update `docs/REORGANIZATION_FILE_MAPPING.md` with final state

**Risk Level:** ⚠️ Low  
**Estimated Time:** 1-2 hours  
**Validation:** 
- ✅ All platforms build successfully
- ✅ Application runs correctly on all platforms
- ✅ Documentation is up to date

---

## Risk Mitigation Strategies

### 1. Incremental Migration
- **Strategy:** Move files in small, logical groups
- **Benefit:** Easier to identify and fix issues
- **Validation:** Test after each phase

### 2. Git Commits Per Phase
- **Strategy:** Commit after each successful phase
- **Benefit:** Easy rollback if issues arise
- **Commit Message Format:** `refactor: reorganize project structure - Phase X: [description]`

### 3. Branch Strategy
- **Strategy:** Create feature branch: `refactor/project-reorganization`
- **Benefit:** Isolate changes, easy to review
- **Merge:** Only after all phases complete and validated

### 4. Automated Testing
- **Strategy:** Run full test suite after each phase
- **Benefit:** Catch issues early
- **Command:** `scripts/build_tests_macos.sh` (macOS), equivalent for other platforms

### 5. Include Path Validation
- **Strategy:** Use IDE or tools to verify all includes resolve
- **Benefit:** Catch include errors before compilation
- **Tool:** IDE "Find All References" or `grep -r "#include"`

---

## Estimated Timeline

| Phase | Description | Time | Risk |
|-------|-------------|------|------|
| 1 | Preparation | 30 min | Low |
| 2 | Platform Code | 2-3 hrs | Medium |
| 3 | Core Application | 1-2 hrs | Medium |
| 4 | Index System | 1 hr | Low |
| 5 | Search Components | 1 hr | Low |
| 6 | Path Operations | 1 hr | Low |
| 7 | Remaining Components | 2-3 hrs | Low-Medium |
| 8 | Resources | 30 min | Low |
| 9 | Documentation | 1 hr | Low |
| 10 | Include Paths | 3-4 hrs | High |
| 11 | Update Tests | 1-2 hrs | Medium |
| 12 | Final Validation | 1-2 hrs | Low |
| **Total** | | **15-20 hrs** | |

**Note:** Times are estimates. Actual time may vary based on:
- Number of include statements to update
- CMake complexity
- Test coverage
- Platform-specific issues

---

## Success Criteria

### Must Have
- ✅ All source files organized in logical directories
- ✅ All platforms (Windows, macOS, Linux) build successfully
- ✅ All tests pass
- ✅ Application runs correctly on all platforms
- ✅ No broken includes
- ✅ CMakeLists.txt updated and working

### Nice to Have
- ✅ Clean root directory (only essential files)
- ✅ Consistent include path strategy
- ✅ Updated documentation
- ✅ Clear separation of concerns

---

## Post-Migration Benefits

### Immediate Benefits
1. **Discoverability:** Easier to find files by logical grouping
2. **Maintainability:** Clear module boundaries
3. **Scalability:** Easy to add new components in appropriate directories
4. **Onboarding:** New developers understand structure faster

### Long-Term Benefits
1. **Modularity:** Components can be extracted to libraries if needed
2. **Testing:** Easier to test modules in isolation
3. **Documentation:** Structure documents architecture
4. **Build System:** Cleaner CMakeLists.txt organization

---

## Alternative: Gradual Migration

If a big-bang migration is too risky, consider a **gradual migration**:

1. **Start with new code:** Put all new files in new structure
2. **Move during refactoring:** Move files when you touch them anyway
3. **Set deadline:** Complete migration within 2-3 months
4. **Document:** Keep track of what's moved vs. what remains

**Pros:** Lower risk, less disruption  
**Cons:** Temporary inconsistency, longer timeline

---

## Questions to Resolve Before Starting

1. **Include Path Strategy:** Option A (relative) or Option B (source root)?
   - **Recommendation:** Option B (source root includes)

2. **UI Directory:** Keep `ui/` or move to `src/ui/`?
   - **Recommendation:** Move to `src/ui/` for consistency

3. **Public Headers:** Do we need `include/` directory for public API?
   - **Recommendation:** Not needed now, can add later if creating library

4. **Resource Paths:** How to handle resource paths in code?
   - **Recommendation:** Use CMake to set resource paths, or relative to executable

5. **Generated Files:** Where to put generated files (GitVersion.h)?
   - **Recommendation:** Keep in root or move to `build/` (gitignored)

---

## Next Steps

1. **Review this plan** with team/stakeholders
2. **Resolve questions** above
3. **Create feature branch:** `refactor/project-reorganization`
4. **Start with Phase 1** (low risk, sets foundation)
5. **Proceed incrementally** through phases
6. **Validate after each phase** before proceeding
7. **Merge after all phases complete** and validated

---

## References

- [Google C++ Style Guide - Directory Structure](https://google.github.io/styleguide/cppguide.html#Directory_Structure)
- [CMake Best Practices](https://gist.github.com/mbinna/c61dbb39bca0e4fb7d1f73b0d66a4fd1)
- Project's own `CXX17_NAMING_CONVENTIONS.md`
- Project's own `docs/DESIGN_REVIEW 18 DEC 2025.md`

---

**Document Status:** Proposal - Awaiting Review  
**Last Updated:** January 4, 2026  
**Author:** AI Assistant (Auto)

---

## Changes Since Original Plan (January 4, 2026)

### New Files Added to Plan

The following files have been added to the project since the original plan and are now included:

**Core:**
- `Version.h` → `src/core/Version.h`

**Core (Shared):**
- `AppBootstrapResultBase.h` → `src/core/AppBootstrapResultBase.h` (shared across all platforms)
- `main_common.h` → `src/core/main_common.h` (shared across all platforms)

**Platform-Specific:**
- `PrivilegeUtils.cpp/h` → `src/platform/windows/PrivilegeUtils.cpp/h` (Windows-only)

**Utilities:**
- `LoggingUtils.cpp/h` → `src/utils/LoggingUtils.cpp/h`
- `ThreadPool.cpp/h` → `src/utils/ThreadPool.cpp/h`
- `ClipboardUtils.cpp/h` → `src/utils/ClipboardUtils.cpp/h`
- `HashMapAliases.h` → `src/utils/HashMapAliases.h`
- `LightweightCallable.h` → `src/utils/LightweightCallable.h`
- `PlatformTypes.h` → `src/utils/PlatformTypes.h`
- `FileSystemUtils.h` → `src/utils/FileSystemUtils.h`
- `FileTimeTypes.h` → `src/utils/FileTimeTypes.h`
- `ScopedHandle.h` → `src/utils/ScopedHandle.h`

**API:**
- `GeminiApiHttp.h` → `src/api/GeminiApiHttp.h`
- `GeminiApiHttp_win.cpp` → `src/api/GeminiApiHttp_win.cpp`
- `GeminiApiHttp_mac.mm` → `src/api/GeminiApiHttp_mac.mm`
- `GeminiApiHttp_linux.cpp` → `src/api/GeminiApiHttp_linux.cpp`

**Documentation:**
- `QUICK_WINS_FIXED.md` → `docs/QUICK_WINS_FIXED.md`

### Updated File Counts

- **Core:** 4 → 7 files (added `Version.h`, `AppBootstrapResultBase.h`, `main_common.h`)
- **Platform (Windows):** 11 → 13 files (added `PrivilegeUtils.cpp/h`)
- **Platform (macOS):** 7 files (unchanged)
- **Platform (Linux):** 5 files (unchanged)
- **Utils:** ~10 → ~20 files (added 10 new utility files)
- **API:** 1-2 → 5 files (added platform-specific HTTP implementations)
- **Total:** ~100 → ~120 files

### Notes

- `AppBootstrapResultBase.h` and `main_common.h` are shared/common files used by all platforms and belong in `src/core/`, not in platform-specific directories.
- `PrivilegeUtils` is Windows-specific and should go in `src/platform/windows/`.
- All new utility files are cross-platform and belong in `src/utils/`.
- `AppBootstrap.h` is Windows-specific (includes Windows.h), while `AppBootstrapResultBase.h` is shared.


