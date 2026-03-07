# Project Reorganization File Mapping

**Date Created:** January 4, 2026  
**Purpose:** Reference document mapping all files from their current locations to their new locations in the reorganized structure.

This document serves as a reference during the migration process. Files are organized by phase and category.

---

## Phase 2: Platform-Specific Code

### Windows Files → `src/platform/windows/`

| Current Location | New Location | Notes |
|-----------------|--------------|-------|
| `AppBootstrap_win.cpp` | `src/platform/windows/AppBootstrap_win.cpp` | Windows-specific implementation |
| `AppBootstrap.h` | `src/platform/windows/AppBootstrap.h` | Windows-specific header (includes Windows.h) |
| `main_win.cpp` | `src/platform/windows/main_win.cpp` | Windows main entry point |
| `FileOperations_win.cpp` | `src/platform/windows/FileOperations_win.cpp` | Windows file operations |
| `FileOperations.h` | `src/platform/windows/FileOperations.h` | Check if Windows-specific or common |
| `FontUtils_win.cpp` | `src/platform/windows/FontUtils_win.cpp` | Windows font utilities |
| `FontUtils_win.h` | `src/platform/windows/FontUtils_win.h` | Windows font utilities header |
| `ThreadUtils_win.cpp` | `src/platform/windows/ThreadUtils_win.cpp` | If exists, otherwise use common |
| `StringUtils_win.cpp` | `src/platform/windows/StringUtils_win.cpp` | Windows string utilities |
| `DirectXManager.cpp` | `src/platform/windows/DirectXManager.cpp` | DirectX manager |
| `DirectXManager.h` | `src/platform/windows/DirectXManager.h` | DirectX manager header |
| `ShellContextUtils.cpp` | `src/platform/windows/ShellContextUtils.cpp` | Windows shell context |
| `ShellContextUtils.h` | `src/platform/windows/ShellContextUtils.h` | Windows shell context header |
| `DragDropUtils.cpp` | `src/platform/windows/DragDropUtils.cpp` | Windows drag-and-drop |
| `DragDropUtils.h` | `src/platform/windows/DragDropUtils.h` | Windows drag-and-drop header |
| `PrivilegeUtils.cpp` | `src/platform/windows/PrivilegeUtils.cpp` | Windows privilege management |
| `PrivilegeUtils.h` | `src/platform/windows/PrivilegeUtils.h` | Windows privilege management header |
| `resource.h` | `src/platform/windows/resource.h` | Windows resource header |
| `resource.rc` | `src/platform/windows/resource.rc` | Windows resource file |

**Total: 18 files (13 unique base names)**

### macOS Files → `src/platform/macos/`

| Current Location | New Location | Notes |
|-----------------|--------------|-------|
| `AppBootstrap_mac.mm` | `src/platform/macos/AppBootstrap_mac.mm` | macOS-specific implementation |
| `AppBootstrap_mac.h` | `src/platform/macos/AppBootstrap_mac.h` | macOS-specific header |
| `main_mac.mm` | `src/platform/macos/main_mac.mm` | macOS main entry point |
| `FileOperations_mac.mm` | `src/platform/macos/FileOperations_mac.mm` | macOS file operations |
| `FileOperations_mac.h` | `src/platform/macos/FileOperations_mac.h` | macOS file operations header |
| `FontUtils_mac.mm` | `src/platform/macos/FontUtils_mac.mm` | macOS font utilities |
| `FontUtils_mac.h` | `src/platform/macos/FontUtils_mac.h` | macOS font utilities header |
| `ThreadUtils_mac.cpp` | `src/platform/macos/ThreadUtils_mac.cpp` | macOS thread utilities |
| `ThreadUtils_mac.h` | `src/platform/macos/ThreadUtils_mac.h` | macOS thread utilities header |
| `StringUtils_mac.cpp` | `src/platform/macos/StringUtils_mac.cpp` | macOS string utilities |
| `StringUtils_mac.h` | `src/platform/macos/StringUtils_mac.h` | macOS string utilities header |
| `MetalManager.mm` | `src/platform/macos/MetalManager.mm` | Metal manager |
| `MetalManager.h` | `src/platform/macos/MetalManager.h` | Metal manager header |

**Total: 13 files (7 unique base names)**

### Linux Files → `src/platform/linux/`

| Current Location | New Location | Notes |
|-----------------|--------------|-------|
| `AppBootstrap_linux.cpp` | `src/platform/linux/AppBootstrap_linux.cpp` | Linux-specific implementation |
| `AppBootstrap_linux.h` | `src/platform/linux/AppBootstrap_linux.h` | Linux-specific header |
| `main_linux.cpp` | `src/platform/linux/main_linux.cpp` | Linux main entry point |
| `FileOperations_linux.cpp` | `src/platform/linux/FileOperations_linux.cpp` | Linux file operations |
| `FileOperations_linux.h` | `src/platform/linux/FileOperations_linux.h` | Linux file operations header |
| `FontUtils_linux.cpp` | `src/platform/linux/FontUtils_linux.cpp` | Linux font utilities |
| `FontUtils_linux.h` | `src/platform/linux/FontUtils_linux.h` | Linux font utilities header |
| `OpenGLManager.cpp` | `src/platform/linux/OpenGLManager.cpp` | OpenGL manager |
| `OpenGLManager.h` | `src/platform/linux/OpenGLManager.h` | OpenGL manager header |

**Total: 9 files (5 unique base names)**

---

## Phase 3: Core Application → `src/core/`

| Current Location | New Location | Notes |
|-----------------|--------------|-------|
| `Application.cpp` | `src/core/Application.cpp` | Main application class |
| `Application.h` | `src/core/Application.h` | Main application header |
| `ApplicationLogic.cpp` | `src/core/ApplicationLogic.cpp` | Application logic |
| `ApplicationLogic.h` | `src/core/ApplicationLogic.h` | Application logic header |
| `CommandLineArgs.cpp` | `src/core/CommandLineArgs.cpp` | Command-line argument parsing |
| `CommandLineArgs.h` | `src/core/CommandLineArgs.h` | Command-line argument header |
| `Settings.cpp` | `src/core/Settings.cpp` | Application settings |
| `Settings.h` | `src/core/Settings.h` | Application settings header |
| `Version.h` | `src/core/Version.h` | Version information |
| `AppBootstrapResultBase.h` | `src/core/AppBootstrapResultBase.h` | **Shared** across all platforms |
| `main_common.h` | `src/core/main_common.h` | **Shared** common main template |

**Total: 11 files (7 unique base names)**

---

## Phase 4: Index System → `src/index/`

| Current Location | New Location | Notes |
|-----------------|--------------|-------|
| `FileIndex.cpp` | `src/index/FileIndex.cpp` | File index implementation |
| `FileIndex.h` | `src/index/FileIndex.h` | File index header |
| `FileIndexStorage.cpp` | `src/index/FileIndexStorage.cpp` | Index storage |
| `FileIndexStorage.h` | `src/index/FileIndexStorage.h` | Index storage header |
| `FileIndexMaintenance.cpp` | `src/index/FileIndexMaintenance.cpp` | Index maintenance |
| `FileIndexMaintenance.h` | `src/index/FileIndexMaintenance.h` | Index maintenance header |
| `IndexFromFilePopulator.cpp` | `src/index/IndexFromFilePopulator.cpp` | Index populator from file |
| `IndexFromFilePopulator.h` | `src/index/IndexFromFilePopulator.h` | Index populator header |
| `InitialIndexPopulator.cpp` | `src/index/InitialIndexPopulator.cpp` | Initial index populator |
| `InitialIndexPopulator.h` | `src/index/InitialIndexPopulator.h` | Initial index populator header |
| `ISearchableIndex.h` | `src/index/ISearchableIndex.h` | Searchable index interface |
| `LazyAttributeLoader.cpp` | `src/index/LazyAttributeLoader.cpp` | Lazy attribute loader |
| `LazyAttributeLoader.h` | `src/index/LazyAttributeLoader.h` | Lazy attribute loader header |
| `LazyValue.h` | `src/index/LazyValue.h` | Lazy value template |

**Total: 14 files (8 unique base names)**

---

## Phase 5: Search Components → `src/search/`

| Current Location | New Location | Notes |
|-----------------|--------------|-------|
| `SearchController.cpp` | `src/search/SearchController.cpp` | Search controller |
| `SearchController.h` | `src/search/SearchController.h` | Search controller header |
| `SearchWorker.cpp` | `src/search/SearchWorker.cpp` | Search worker |
| `SearchWorker.h` | `src/search/SearchWorker.h` | Search worker header |
| `ParallelSearchEngine.cpp` | `src/search/ParallelSearchEngine.cpp` | Parallel search engine |
| `ParallelSearchEngine.h` | `src/search/ParallelSearchEngine.h` | Parallel search engine header |
| `SearchThreadPool.cpp` | `src/search/SearchThreadPool.cpp` | Search thread pool |
| `SearchThreadPool.h` | `src/search/SearchThreadPool.h` | Search thread pool header |
| `SearchStatisticsCollector.cpp` | `src/search/SearchStatisticsCollector.cpp` | Search statistics |
| `SearchStatisticsCollector.h` | `src/search/SearchStatisticsCollector.h` | Search statistics header |
| `SearchResultUtils.cpp` | `src/search/SearchResultUtils.cpp` | Search result utilities |
| `SearchResultUtils.h` | `src/search/SearchResultUtils.h` | Search result utilities header |
| `ISearchExecutor.h` | `src/search/ISearchExecutor.h` | Search executor interface |
| `SearchContext.h` | `src/search/SearchContext.h` | Search context |
| `SearchInputField.h` | `src/search/SearchInputField.h` | Search input field |
| `SearchPatternUtils.h` | `src/search/SearchPatternUtils.h` | Search pattern utilities |

**Total: 16 files (10 unique base names)**

---

## Phase 6: Path Operations → `src/path/`

| Current Location | New Location | Notes |
|-----------------|--------------|-------|
| `PathOperations.cpp` | `src/path/PathOperations.cpp` | Path operations |
| `PathOperations.h` | `src/path/PathOperations.h` | Path operations header |
| `PathUtils.cpp` | `src/path/PathUtils.cpp` | Path utilities |
| `PathUtils.h` | `src/path/PathUtils.h` | Path utilities header |
| `PathBuilder.cpp` | `src/path/PathBuilder.cpp` | Path builder |
| `PathBuilder.h` | `src/path/PathBuilder.h` | Path builder header |
| `PathStorage.cpp` | `src/path/PathStorage.cpp` | Path storage |
| `PathStorage.h` | `src/path/PathStorage.h` | Path storage header |
| `PathPatternMatcher.cpp` | `src/path/PathPatternMatcher.cpp` | Path pattern matcher |
| `PathPatternMatcher.h` | `src/path/PathPatternMatcher.h` | Path pattern matcher header |
| `PathConstants.h` | `src/path/PathConstants.h` | Path constants |
| `DirectoryResolver.cpp` | `src/path/DirectoryResolver.cpp` | Directory resolver |
| `DirectoryResolver.h` | `src/path/DirectoryResolver.h` | Directory resolver header |

**Total: 13 files (7 unique base names)**

---

## Phase 7: Remaining Components

### USN Components → `src/usn/`

| Current Location | New Location | Notes |
|-----------------|--------------|-------|
| `UsnMonitor.cpp` | `src/usn/UsnMonitor.cpp` | USN journal monitor |
| `UsnMonitor.h` | `src/usn/UsnMonitor.h` | USN journal monitor header |
| `UsnRecordUtils.h` | `src/usn/UsnRecordUtils.h` | USN record utilities |

**Total: 3 files (2 unique base names)**

### Crawler Components → `src/crawler/`

| Current Location | New Location | Notes |
|-----------------|--------------|-------|
| `FolderCrawler.cpp` | `src/crawler/FolderCrawler.cpp` | Folder crawler |
| `FolderCrawler.h` | `src/crawler/FolderCrawler.h` | Folder crawler header |
| `IndexOperations.cpp` | `src/crawler/IndexOperations.cpp` | Index operations |
| `IndexOperations.h` | `src/crawler/IndexOperations.h` | Index operations header |

**Total: 4 files (2 unique base names)**

### Utilities → `src/utils/`

| Current Location | New Location | Notes |
|-----------------|--------------|-------|
| `StringUtils.h` | `src/utils/StringUtils.h` | Common string utilities header (platform impls in platform/) |
| `ThreadUtils.h` | `src/utils/ThreadUtils.h` | Common thread utilities header (platform impls in platform/) |
| `StringSearchAVX2.cpp` | `src/utils/StringSearchAVX2.cpp` | AVX2 string search |
| `StringSearchAVX2.h` | `src/utils/StringSearchAVX2.h` | AVX2 string search header |
| `SimpleRegex.h` | `src/utils/SimpleRegex.h` | Simple regex utilities |
| `StdRegexUtils.h` | `src/utils/StdRegexUtils.h` | Standard regex utilities |
| `StringSearch.h` | `src/utils/StringSearch.h` | String search header |
| `CpuFeatures.cpp` | `src/utils/CpuFeatures.cpp` | CPU feature detection |
| `CpuFeatures.h` | `src/utils/CpuFeatures.h` | CPU feature detection header |
| `Logger.h` | `src/utils/Logger.h` | Logger |
| `LoggingUtils.cpp` | `src/utils/LoggingUtils.cpp` | Logging utilities |
| `LoggingUtils.h` | `src/utils/LoggingUtils.h` | Logging utilities header |
| `LoadBalancingStrategy.cpp` | `src/utils/LoadBalancingStrategy.cpp` | Load balancing strategy |
| `LoadBalancingStrategy.h` | `src/utils/LoadBalancingStrategy.h` | Load balancing strategy header |
| `ThreadPool.cpp` | `src/utils/ThreadPool.cpp` | Thread pool |
| `ThreadPool.h` | `src/utils/ThreadPool.h` | Thread pool header |
| `ClipboardUtils.cpp` | `src/utils/ClipboardUtils.cpp` | Clipboard utilities |
| `ClipboardUtils.h` | `src/utils/ClipboardUtils.h` | Clipboard utilities header |
| `HashMapAliases.h` | `src/utils/HashMapAliases.h` | HashMap type aliases |
| `LightweightCallable.h` | `src/utils/LightweightCallable.h` | Lightweight callable |
| `PlatformTypes.h` | `src/utils/PlatformTypes.h` | Platform-specific types |
| `FileSystemUtils.h` | `src/utils/FileSystemUtils.h` | File system utilities |
| `FileTimeTypes.h` | `src/utils/FileTimeTypes.h` | File time types |
| `ScopedHandle.h` | `src/utils/ScopedHandle.h` | RAII handle wrapper |

**Total: 24 files (20 unique base names)**

### Filters → `src/filters/`

| Current Location | New Location | Notes |
|-----------------|--------------|-------|
| `TimeFilter.h` | `src/filters/TimeFilter.h` | Time filter |
| `TimeFilterUtils.cpp` | `src/filters/TimeFilterUtils.cpp` | Time filter utilities |
| `TimeFilterUtils.h` | `src/filters/TimeFilterUtils.h` | Time filter utilities header |
| `SizeFilter.h` | `src/filters/SizeFilter.h` | Size filter |
| `SizeFilterUtils.cpp` | `src/filters/SizeFilterUtils.cpp` | Size filter utilities |
| `SizeFilterUtils.h` | `src/filters/SizeFilterUtils.h` | Size filter utilities header |

**Total: 6 files (4 unique base names)**

### GUI Components → `src/gui/`

| Current Location | New Location | Notes |
|-----------------|--------------|-------|
| `GuiState.cpp` | `src/gui/GuiState.cpp` | GUI state |
| `GuiState.h` | `src/gui/GuiState.h` | GUI state header |
| `ImGuiUtils.h` | `src/gui/ImGuiUtils.h` | ImGui utilities |
| `RendererInterface.h` | `src/gui/RendererInterface.h` | Renderer interface |

**Total: 4 files (3 unique base names)**

### API Components → `src/api/`

| Current Location | New Location | Notes |
|-----------------|--------------|-------|
| `GeminiApiUtils.cpp` | `src/api/GeminiApiUtils.cpp` | Gemini API utilities |
| `GeminiApiUtils.h` | `src/api/GeminiApiUtils.h` | Gemini API utilities header |
| `GeminiApiHttp.h` | `src/api/GeminiApiHttp.h` | Gemini API HTTP header |
| `GeminiApiHttp_win.cpp` | `src/api/GeminiApiHttp_win.cpp` | Windows HTTP implementation |
| `GeminiApiHttp_mac.mm` | `src/api/GeminiApiHttp_mac.mm` | macOS HTTP implementation |
| `GeminiApiHttp_linux.cpp` | `src/api/GeminiApiHttp_linux.cpp` | Linux HTTP implementation |

**Total: 6 files (2 unique base names, 3 platform implementations)**

### UI Components → `src/ui/`

| Current Location | New Location | Notes |
|-----------------|--------------|-------|
| `ui/` (entire directory) | `src/ui/` | Move entire directory, keep structure |

**Note:** UI directory already exists and is organized. Just relocate to `src/ui/`.

---

## Phase 8: Resources → `resources/`

| Current Location | New Location | Notes |
|-----------------|--------------|-------|
| `app_icon.icns` | `resources/icons/app_icon.icns` | macOS icon |
| `app_icon.ico` | `resources/icons/app_icon.ico` | Windows icon |
| `app.manifest` | `resources/windows/app.manifest` | Windows manifest |
| `resource.rc` | `resources/windows/resource.rc` | (Already moved in Phase 2) |

**Total: 3 files (resource.rc already moved)**

---

## Phase 9: Documentation → `docs/`

| Current Location | New Location | Notes |
|-----------------|--------------|-------|
| `CXX17_NAMING_CONVENTIONS.md` | `docs/CXX17_NAMING_CONVENTIONS.md` | Naming conventions |
| `BUILDING_ON_LINUX.md` | `docs/BUILDING_ON_LINUX.md` | Linux build instructions |
| `FIXES_BENEFITS_EXPLAINED.md` | `docs/FIXES_BENEFITS_EXPLAINED.md` | Fixes documentation |
| `Fixes_Rationale.md` | `docs/Fixes_Rationale.md` | Fixes rationale |
| `ideas for new features.md` | `docs/ideas_for_new_features.md` | Feature ideas |
| `MEMORY_ANALYSIS_REPORT.md` | `docs/MEMORY_ANALYSIS_REPORT.md` | Memory analysis |
| `QUICK_WINS_FIXED.md` | `docs/QUICK_WINS_FIXED.md` | Quick wins documentation |
| `TEST_IMPROVEMENTS_VERIFICATION_REPORT.md` | `docs/TEST_IMPROVEMENTS_VERIFICATION_REPORT.md` | Test improvements |
| `TestImprovements_Rationale.md` | `docs/TestImprovements_Rationale.md` | Test improvements rationale |
| `TIMELINE.md` | `docs/TIMELINE.md` | Project timeline |
| `VERIFICATION_REPORT.md` | `docs/VERIFICATION_REPORT.md` | Verification report |

**Total: 11 files**

---

## Files to Keep in Root

These files should remain in the root directory:

- `CMakeLists.txt` - Root CMake configuration
- `README.md` - Main project documentation
- `.gitignore` - Git ignore rules
- `AGENTS.md` - Agent guidelines (if needed)
- `GitVersion.h` - Generated version file (gitignored)
- `imgui.ini` - ImGui user settings (gitignored)
- `settings.json` - User settings (may be gitignored)
- `compile_commands.json` - Clang tooling (gitignored)
- `sonar-project.properties` - SonarQube configuration

---

## Summary Statistics

| Category | Files |
|----------|-------|
| Platform (Windows) | 18 |
| Platform (macOS) | 13 |
| Platform (Linux) | 9 |
| Core | 11 |
| Index | 14 |
| Search | 16 |
| Path | 13 |
| USN | 3 |
| Crawler | 4 |
| Utils | 24 |
| Filters | 6 |
| GUI | 4 |
| API | 6 |
| UI (directory) | ~8 files (existing) |
| Resources | 3 |
| Documentation | 11 |
| **Total** | **~150 files** |

**Note:** Some files may have both `.cpp` and `.h` versions, so the actual file count is higher than unique base names.

---

## Migration Notes

1. **Include Path Updates:** After moving files, all `#include` statements will need to be updated to reflect new paths.

2. **CMakeLists.txt:** All file paths in `CMakeLists.txt` must be updated to match new locations.

3. **Platform-Specific Files:** Some files like `FileOperations.h` and `ThreadUtils.h` may be common headers with platform-specific implementations. Verify before moving.

4. **Shared Files:** `AppBootstrapResultBase.h` and `main_common.h` are shared across platforms and belong in `src/core/`, not platform directories.

5. **UI Directory:** The `ui/` directory is already organized. Just relocate it to `src/ui/` maintaining its internal structure.

---

**Last Updated:** January 4, 2026  
**Status:** Phase 1 Complete - Directory structure created, mapping documented

