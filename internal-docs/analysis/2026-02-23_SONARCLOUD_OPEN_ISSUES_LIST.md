# 2026-02-23 SonarCloud open issues – USN_WINDOWS

**Project:** BrunoO_USN_WINDOWS  
**Fetched:** 2026-02-23 (via `scripts/fetch_sonar_results.sh --open-only`)  
**Open issues:** 57

---

## Summary by file

| File | Count | Rules |
|------|-------|--------|
| `src/ui/ImGuiTestEngineTests.cpp` | 7 | S1188 (4× lambda length), S5945, S7127, S107 |
| `src/platform/windows/DragDropUtils.cpp` | 14 | S3806, S3656, S5025 (3×), S3230, S5008 (3×), S3624, S3630 |
| `src/platform/windows/ShellContextUtils.cpp` | 10 | S3806 (2×), S6020, S3630 (6×), S6004 |
| `src/core/Application.cpp` | 10 | S954, S3806, S1181 (2×), S2738 (2×), S6004 (3×) |
| `src/ui/HelpWindow.cpp` | 0 | S125 fixed: NOSONAR on #endif  // __APPLE__ (directive comment, not commented-out code) |
| `src/ui/StatusBar.cpp` | 5 | S6004, S3806, S1905 (3×) |
| `src/ui/ResultsTable.cpp` | 2 | S1066, S1905 |
| `src/ui/Popups.cpp` | 1 | S1144 (unused function) |
| `src/core/AppBootstrapCommon.h` | 1 | S995 |
| `src/platform/linux/AppBootstrap_linux.h` | 1 | S125 |
| `src/platform/windows/AppBootstrap_win.cpp` | 3 | S3806, S5350, S2738 |
| `src/platform/windows/AppBootstrap_win.h` | 1 | S125 |
| `src/platform/windows/FontUtils_win.cpp` | 1 | S6018 |
| `src/crawler/FolderCrawler.cpp` | 1 | S1905 |
| `src/search/SearchResultUtils.cpp` | 1 | S1905 |
| `src/usn/UsnMonitor.cpp` | 1 | S5827 |
| `src/utils/FileSystemUtils.h` | 1 | S6004 |

---

## Full list (open only)

| # | Severity | Rule | File:Line | Message |
|---|----------|------|-----------|---------|
| 1 | MAJOR | cpp:S1188 | ImGuiTestEngineTests.cpp:284 | Lambda has 29 lines (>20). Split or make named function. |
| 2 | MAJOR | cpp:S5945 | ImGuiTestEngineTests.cpp:332 | Use std::array or std::vector instead of C-style array. |
| 3 | CRITICAL | cpp:S7127 | ImGuiTestEngineTests.cpp:341 | Use std::size for array size. |
| 4 | MAJOR | cpp:S107 | ImGuiTestEngineTests.cpp:74 | Function has 9 parameters (>7). |
| 5 | MAJOR | cpp:S1188 | ImGuiTestEngineTests.cpp:184 | Lambda has 26 lines (>20). |
| 6 | MAJOR | cpp:S1188 | ImGuiTestEngineTests.cpp:217 | Lambda has 26 lines (>20). |
| 7 | MAJOR | cpp:S1188 | ImGuiTestEngineTests.cpp:250 | Lambda has 26 lines (>20). |
| 8 | MINOR | cpp:S6004 | StatusBar.cpp:391 | Use init-statement for "is_loading_attributes". |
| 9 | MINOR | cpp:S995 | AppBootstrapCommon.h:238 | Make last_height pointer-to-const. |
| 10 | MAJOR | cpp:S954 | Application.cpp:118 | Move all #include to top of file. |
| 11 | MAJOR | cpp:S3806 | Application.cpp:126 | Use lowercase &lt;windows.h&gt; (non-portable path). |
| 12 | MAJOR | cpp:S1181 | Application.cpp:512 | Catch more specific exception. |
| 13 | MAJOR | cpp:S1181 | Application.cpp:519 | Catch more specific exception. |
| 14 | MINOR | cpp:S2738 | Application.cpp:526 | Catch specific exception type. |
| 15 | MINOR | cpp:S6004 | Application.cpp:612 | Use init-statement for "io". |
| 16 | MINOR | cpp:S6004 | Application.cpp:923 | Use init-statement for "default_user_root". |
| 17 | MINOR | cpp:S6004 | Application.cpp:949 | Use init-statement for "output_path". |
| 18 | MINOR | cpp:S1905 | FolderCrawler.cpp:665 | Remove redundant cast. |
| 19 | MAJOR | cpp:S125 | AppBootstrap_linux.h:49 | Remove commented-out code. |
| 20 | MAJOR | cpp:S3806 | AppBootstrap_win.cpp:41 | Use lowercase &lt;windows.h&gt;. |
| 21 | MINOR | cpp:S5350 | AppBootstrap_win.cpp:86 | Make pgort_handle pointer-to-const. |
| 22 | MINOR | cpp:S2738 | AppBootstrap_win.cpp:212 | Catch specific exception type. |
| 23 | MAJOR | cpp:S125 | AppBootstrap_win.h:54 | Remove commented-out code. |
| 24 | MAJOR | cpp:S3806 | DragDropUtils.cpp:6 | Use lowercase &lt;windows.h&gt;. |
| 25 | CRITICAL | cpp:S3656 | DragDropUtils.cpp:66 | Member variables should not be protected. |
| 26 | CRITICAL | cpp:S5025 | DragDropUtils.cpp:77 | Avoid raw delete; use RAII. |
| 27 | MAJOR | cpp:S3230 | DragDropUtils.cpp:90 | Remove redundant initializer list (use in-class init). |
| 28 | CRITICAL | cpp:S5008 | DragDropUtils.cpp:93 | Replace void* with meaningful type. |
| 29 | CRITICAL | cpp:S5025 | DragDropUtils.cpp:156 | Replace new with managed memory. |
| 30 | CRITICAL | cpp:S3624 | DragDropUtils.cpp:170 | Customize copy ctor/assign; consider move. |
| 31 | CRITICAL | cpp:S5008 | DragDropUtils.cpp:182 | Replace void* with meaningful type. |
| 32 | CRITICAL | cpp:S5025 | DragDropUtils.cpp:265 | Replace new with managed memory. |
| 33 | CRITICAL | cpp:S5008 | DragDropUtils.cpp:291 | Replace void* with meaningful type. |
| 34 | MAJOR | cpp:S3630 | DragDropUtils.cpp:354 | Replace reinterpret_cast with safer op. |
| 35 | MAJOR | cpp:S6018 | FontUtils_win.cpp:44 | Use inline variable for global. |
| 36 | MAJOR | cpp:S3806 | ShellContextUtils.cpp:12 | Use lowercase &lt;ShlObj_core.h&gt;. |
| 37 | MAJOR | cpp:S3806 | ShellContextUtils.cpp:13 | Use lowercase &lt;ShObjIdl_core.h&gt;. |
| 38 | MINOR | cpp:S6020 | ShellContextUtils.cpp:41 | Use std::remove_pointer_t. |
| 39 | MAJOR | cpp:S3630 | ShellContextUtils.cpp:94, 143, 157, 223, 237, 261 | Replace reinterpret_cast (6 occurrences). |
| 40 | MINOR | cpp:S6004 | ShellContextUtils.cpp:364 | Use init-statement for "converted". |
| 41 | MINOR | cpp:S1905 | SearchResultUtils.cpp:184 | Remove redundant cast. |
| 42 | MAJOR | cpp:S1144 | Popups.cpp:136 | Unused function RenderPopupCloseButton. |
| 43 | MAJOR | cpp:S1066 | ResultsTable.cpp:886 | Merge if with enclosing one. |
| 44 | MINOR | cpp:S1905 | ResultsTable.cpp:1075 | Remove redundant cast. |
| 45 | MAJOR | cpp:S3806 | StatusBar.cpp:27 | Use lowercase &lt;windows.h&gt;. |
| 46 | MINOR | cpp:S1905 | StatusBar.cpp:318, 327 | Remove redundant cast (2×). |
| 47 | MAJOR | cpp:S5827 | UsnMonitor.cpp:294 | Replace redundant type with auto. |
| 48 | MINOR | cpp:S6004 | FileSystemUtils.h:399 | Use init-statement for "n". |
| — | — | cpp:S125 | HelpWindow.cpp | Fixed: NOSONAR on same line (directive-matching comment). |

---

## By severity

| Severity | Count |
|----------|-------|
| CRITICAL | 9 |
| MAJOR | 28 |
| MINOR | 20 |

## By rule (top)

| Rule | Count | Description |
|------|-------|-------------|
| cpp:S3630 | 7 | Replace reinterpret_cast |
| cpp:S6004 | 7 | Use C++17 init-statement |
| cpp:S3806 | 6 | Use lowercase include path |
| cpp:S1905 | 6 | Remove redundant cast |
| cpp:S1188 | 4 | Lambda too long (>20 lines) |
| cpp:S5025 | 3 | Replace new/delete with RAII |
| cpp:S5008 | 3 | Replace void* |
| cpp:S125 | 6 | Remove commented-out code (3 in HelpWindow fixed with NOSONAR) |
| cpp:S1181 | 2 | Catch specific exception |
| cpp:S2738 | 2 | Catch specific exception type |

---

## How to refresh this list

```bash
export SONARQUBE_TOKEN="your_token"
./scripts/fetch_sonar_results.sh --open-only --format json
# Issues saved to ./sonar-results/sonarqube_issues.json
./scripts/fetch_sonar_results.sh --open-only --format summary
# Summary to ./sonar-results/sonarqube_issues_summary.txt
```

**SonarCloud UI:** https://sonarcloud.io/project/issues?id=BrunoO_USN_WINDOWS
