# Files with `readability-identifier-naming` Warnings to Address

**Date:** 2026-01-31  
**Check:** `readability-identifier-naming`  
**Purpose:** List of files where identifiers must be renamed to match project conventions (CXX17_NAMING_CONVENTIONS.md / .clang-tidy).

**Total:** 73 files (from script run; flat list below is authoritative)

To regenerate this list (from project root):

```bash
python3 scripts/list_files_with_naming_warnings.py 2>/dev/null | grep -E '^/|^src/' | sed 's|.*/USN_WINDOWS/||' | sort -u
```

---

## List by directory

### core (6)
- src/core/AppBootstrapCommon.h
- src/core/Application.cpp
- src/core/Application.h
- src/core/Settings.cpp
- src/core/Settings.h
- src/core/Version.h

### crawler (3)
- src/crawler/FolderCrawler.cpp
- src/crawler/FolderCrawler.h
- src/crawler/IndexOperations.h

### index (10)
- src/index/FileIndex.h
- src/index/FileIndexMaintenance.h
- src/index/FileIndexStorage.h
- src/index/IndexFromFilePopulator.cpp
- src/index/IndexFromFilePopulator.h
- src/index/LazyAttributeLoader.cpp
- src/index/LazyAttributeLoader.h
- src/index/mft/MftMetadataReader.cpp
- src/index/mft/MftMetadataReader.h
- src/index/PathRecomputer.h

### path (5)
- src/path/DirectoryResolver.h
- src/path/PathBuilder.h
- src/path/PathOperations.h
- src/path/PathPatternMatcher.h
- src/path/PathStorage.h

### platform (18)
- src/platform/EmbeddedFont_Cousine.cpp
- src/platform/EmbeddedFont_Cousine.h
- src/platform/EmbeddedFont_FontAwesome.cpp
- src/platform/EmbeddedFont_FontAwesome.h
- src/platform/EmbeddedFont_Karla.cpp
- src/platform/EmbeddedFont_Karla.h
- src/platform/EmbeddedFont_RobotoMedium.cpp
- src/platform/EmbeddedFont_RobotoMedium.h
- src/platform/linux/AppBootstrap_linux.h
- src/platform/linux/OpenGLManager.h
- src/platform/linux/ThreadUtils_linux.cpp
- src/platform/macos/AppBootstrap_mac.h
- src/platform/macos/MetalManager.h
- src/platform/macos/ThreadUtils_mac.cpp
- src/platform/windows/AppBootstrap.h
- src/platform/windows/DirectXManager.h
- src/platform/windows/DragDropUtils.cpp
- src/platform/windows/ThreadUtils_win.cpp

### search (14)
- src/search/ParallelSearchEngine.cpp
- src/search/ParallelSearchEngine.h
- src/search/SearchController.cpp
- src/search/SearchInputField.h
- src/search/SearchPatternUtils.h
- src/search/SearchResultUtils.h
- src/search/SearchStatisticsCollector.cpp
- src/search/SearchStatisticsCollector.h
- src/search/SearchThreadPool.h
- src/search/SearchThreadPoolManager.cpp
- src/search/SearchThreadPoolManager.h
- src/search/SearchTypes.h
- src/search/SearchWorker.cpp
- src/search/SearchWorker.h

### ui (4)
- src/ui/FolderBrowser.h
- src/ui/MetricsWindow.cpp
- src/ui/SearchInputs.cpp
- src/ui/StatusBar.cpp

### usn (2)
- src/usn/UsnMonitor.cpp
- src/usn/UsnMonitor.h

### utils (13)
- src/utils/CpuFeatures.cpp
- src/utils/FileSystemUtils.h
- src/utils/FileTimeTypes.h
- src/utils/LoadBalancingStrategy.cpp
- src/utils/Logger.h
- src/utils/RegexAliases.h
- src/utils/SimpleRegex.h
- src/utils/StdRegexUtils.h
- src/utils/StringUtils.h
- src/utils/ThreadPool.h
- src/utils/ThreadUtils.h

---

## Flat list (copy-paste)

```
src/core/AppBootstrapCommon.h
src/core/Application.cpp
src/core/Application.h
src/core/Settings.cpp
src/core/Settings.h
src/core/Version.h
src/crawler/FolderCrawler.cpp
src/crawler/FolderCrawler.h
src/crawler/IndexOperations.h
src/index/FileIndex.h
src/index/FileIndexMaintenance.h
src/index/FileIndexStorage.h
src/index/IndexFromFilePopulator.cpp
src/index/IndexFromFilePopulator.h
src/index/LazyAttributeLoader.cpp
src/index/LazyAttributeLoader.h
src/index/mft/MftMetadataReader.cpp
src/index/mft/MftMetadataReader.h
src/index/PathRecomputer.h
src/path/DirectoryResolver.h
src/path/PathBuilder.h
src/path/PathOperations.h
src/path/PathPatternMatcher.h
src/path/PathStorage.h
src/platform/EmbeddedFont_Cousine.cpp
src/platform/EmbeddedFont_Cousine.h
src/platform/EmbeddedFont_FontAwesome.cpp
src/platform/EmbeddedFont_FontAwesome.h
src/platform/EmbeddedFont_Karla.cpp
src/platform/EmbeddedFont_Karla.h
src/platform/EmbeddedFont_RobotoMedium.cpp
src/platform/EmbeddedFont_RobotoMedium.h
src/platform/linux/AppBootstrap_linux.h
src/platform/linux/OpenGLManager.h
src/platform/linux/ThreadUtils_linux.cpp
src/platform/macos/AppBootstrap_mac.h
src/platform/macos/MetalManager.h
src/platform/macos/ThreadUtils_mac.cpp
src/platform/windows/AppBootstrap.h
src/platform/windows/DirectXManager.h
src/platform/windows/DragDropUtils.cpp
src/platform/windows/ThreadUtils_win.cpp
src/search/ParallelSearchEngine.cpp
src/search/ParallelSearchEngine.h
src/search/SearchController.cpp
src/search/SearchInputField.h
src/search/SearchPatternUtils.h
src/search/SearchResultUtils.h
src/search/SearchStatisticsCollector.cpp
src/search/SearchStatisticsCollector.h
src/search/SearchThreadPool.h
src/search/SearchThreadPoolManager.cpp
src/search/SearchThreadPoolManager.h
src/search/SearchTypes.h
src/search/SearchWorker.cpp
src/search/SearchWorker.h
src/ui/FolderBrowser.h
src/ui/MetricsWindow.cpp
src/ui/SearchInputs.cpp
src/ui/StatusBar.cpp
src/usn/UsnMonitor.cpp
src/usn/UsnMonitor.h
src/utils/CpuFeatures.cpp
src/utils/FileSystemUtils.h
src/utils/FileTimeTypes.h
src/utils/LoadBalancingStrategy.cpp
src/utils/Logger.h
src/utils/RegexAliases.h
src/utils/SimpleRegex.h
src/utils/StdRegexUtils.h
src/utils/StringUtils.h
src/utils/ThreadPool.h
src/utils/ThreadUtils.h
```

---

## Related

- **Conventions:** `docs/standards/CXX17_NAMING_CONVENTIONS.md`
- **Config:** `.clang-tidy` (CheckOptions: readability-identifier-naming.*)
- **Script:** `scripts/list_files_with_naming_warnings.py`
- **Status:** `docs/analysis/2026-01-31_CLANG_TIDY_STATUS.md`
