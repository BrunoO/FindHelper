# 2026-02-20 Files still with clang-tidy warnings

**Source:** Snapshot from `clang-tidy-report-2026-02-20.txt`, with files already fixed in follow-up commits excluded.

**Fixed in this round (excluded from list below):**
- `src/crawler/IndexOperations.cpp`
- `src/core/CommandLineArgs.h`
- `src/api/GeminiApiUtils.h`
- `src/path/DirectoryResolver.cpp`
- `src/path/PathOperations.cpp`
- `src/search/SearchContextBuilder.cpp`

**Note:** Re-run `scripts/run_clang_tidy.sh` to regenerate a full report; this list is derived from the original report minus the six files above.

---

## Project files still with warnings or errors

| File | Warnings | Errors | Total |
|------|----------|--------|-------|
| src/path/PathPatternMatcher.cpp | 53 | 0 | 53 |
| src/utils/FileSystemUtils.h | 31 | 0 | 31 |
| src/platform/linux/FileOperations_linux.cpp | 28 | 0 | 28 |
| src/crawler/FolderCrawler.cpp | 27 | 0 | 27 |
| src/search/ParallelSearchEngine.cpp | 25 | 0 | 25 |
| src/api/GeminiApiHttp_linux.cpp | 22 | 0 | 22 |
| src/utils/LoadBalancingStrategy.cpp | 22 | 0 | 22 |
| src/index/LazyAttributeLoader.cpp | 19 | 0 | 19 |
| src/platform/windows/FileOperations_win.cpp | 0 | 19 | 19 |
| src/ui/UIRenderer.cpp | 19 | 0 | 19 |
| src/utils/LightweightCallable.h | 15 | 0 | 15 |
| src/utils/Logger.h | 15 | 0 | 15 |
| src/ui/SearchInputsGeminiHelpers.cpp | 11 | 0 | 11 |
| src/path/PathBuilder.cpp | 8 | 0 | 8 |
| src/search/StreamingResultsCollector.cpp | 8 | 0 | 8 |
| src/filters/TimeFilterUtils.cpp | 7 | 0 | 7 |
| src/utils/SimpleRegex.h | 7 | 0 | 7 |
| src/api/GeminiApiUtils.cpp | 6 | 0 | 6 |
| src/ui/ResultsTable.cpp | 6 | 0 | 6 |
| src/api/GeminiApiHttp_win.cpp | 4 | 1 | 5 |
| src/index/mft/MftMetadataReader.h | 3 | 2 | 5 |
| src/search/SearchInputField.h | 5 | 0 | 5 |
| src/search/SearchResultUtils.cpp | 5 | 0 | 5 |
| src/utils/HashMapAliases.h | 5 | 0 | 5 |
| src/utils/ThreadPool.cpp | 5 | 0 | 5 |
| src/utils/StringSearch.h | 4 | 0 | 4 |
| src/index/mft/MftMetadataReader.cpp | 3 | 0 | 3 |
| src/search/SearchThreadPool.h | 3 | 0 | 3 |
| src/usn/UsnRecordUtils.h | 0 | 3 | 3 |
| src/utils/ThreadPool.h | 3 | 0 | 3 |
| src/gui/GuiState.cpp | 2 | 0 | 2 |
| src/index/InitialIndexPopulator.h | 0 | 2 | 2 |
| src/platform/linux/StringUtils_linux.cpp | 2 | 0 | 2 |
| src/platform/macos/StringUtils_mac.cpp | 2 | 0 | 2 |
| src/platform/windows/DirectXManager.h | 0 | 2 | 2 |
| src/search/SearchThreadPool.cpp | 2 | 0 | 2 |
| src/search/SearchWorker.cpp | 2 | 0 | 2 |
| src/utils/LoadBalancingStrategy.h | 2 | 0 | 2 |
| src/core/AppBootstrapCommon.h | 1 | 0 | 1 |
| src/core/Application.cpp | 0 | 1 | 1 |
| src/crawler/FolderCrawlerIndexBuilder.cpp | 1 | 0 | 1 |
| src/platform/linux/AppBootstrap_linux.cpp | 0 | 1 | 1 |
| src/platform/linux/OpenGLManager.cpp | 0 | 1 | 1 |
| src/platform/macos/MetalManager.h | 0 | 1 | 1 |
| src/platform/macos/ThreadUtils_mac.cpp | 1 | 0 | 1 |
| src/platform/windows/DragDropUtils.cpp | 0 | 1 | 1 |
| src/platform/windows/ShellContextUtils.cpp | 1 | 0 | 1 |
| src/platform/windows/StringUtils_win.cpp | 0 | 1 | 1 |
| src/platform/windows/ThreadUtils_win.cpp | 0 | 1 | 1 |
| src/search/StreamingResultsCollector.h | 1 | 0 | 1 |
| src/ui/StatusBar.cpp | 1 | 0 | 1 |
| src/utils/ClipboardUtils.cpp | 1 | 0 | 1 |
| src/utils/SystemIdleDetector.cpp | 0 | 1 | 1 |

**Errors** in the report are mostly environment-related on macOS (e.g. `windows.h` not found, `Metal/Metal.h` not found) and are expected when running clang-tidy outside the target platform.

**Total:** 54 project files still with at least one warning or error in the snapshot.
