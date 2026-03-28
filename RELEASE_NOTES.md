# Release Notes

## March 28, 2026

### Added

- **Natural-language / Gemini:** Recent searches persist the NL field and show it in empty-state **Recent searches** tooltips; **Saved searches** combo shows the same text in tooltips when present.
- **ImGui Test Engine:** Regression coverage for the **# Files** results column (fixture counts, data-driven polling).

### Changed

- **Status bar:** Version removed from the left cluster (still in **Help → About**); indeterminate **busy** bar renders more smoothly and covers **Gemini** calls, cloud attribute loading, and progressive total-size work.
- **Windows indexing:** MFT metadata reading is toggled with **`--mft-metadata-reading=true|false`** (default true) instead of a CMake option.
- **Documentation:** Stable paths and **DRY** guide under `docs/` for published snapshots; outdated internal task markdown removed.

### Fixed

- **Windows:** Results-table shell drag-and-drop advertises **copy** only so Explorer does not default to **move** on same-volume drops.
- **Path patterns:** Normalisation when collapsing `**/` sequences.

### Code quality

- Sonar-driven cleanups (CLI, MFT/index logging, Help, StatusBar, search-strategy tests); deduplicated helpers in load balancing, Help rendering, ImGui shortcut/clipboard tests, and folder-size tests; `constexpr` for eligible static UI/test tables.

---

## March 27, 2026

### Added

- **Results table:** **# Files** column — for each **folder** row, shows the recursive count of non-directory files under that directory (enable it from the column picker; hidden by default). Shows **…** while the count is still computing and **–** for file rows. **Export CSV** and **Ctrl+Shift+X** (Windows/Linux) / **Cmd+Shift+X** (macOS) clipboard CSV include a **# Files** field aligned with the table.

---

## March 24, 2026

### Performance

- **Folder-crawl indexing:** After a recursive folder crawl finishes, the index now runs a lightweight **finalize** step (`FinalizeFolderCrawlIndexing`) that releases the temporary name cache instead of rebuilding all paths (`RecomputeAllPaths`) on **macOS and Linux**. **Windows** still performs OneDrive lazy-load sentinel updates in finalize; USN/MFT code paths are unchanged and continue to use full path recomputation where required.

### Changed

- **Indexing UI:** `IsIndexBuilding()` is sampled once per frame for index-build state; `SearchController` and `GuiState` stay consistent with `Application`’s notion of “index building.”
- **Windows status bar:** Index elapsed time and final duration use the same `IsIndexBuilding()` signal as the empty/indexing UI, so timing matches what the user sees during USN and monitor phases.

### Fixed

- **Windows folder crawler:** Enumeration no longer treats every reparse point as a symlink. **OneDrive** and other cloud placeholders use non-symlink reparse tags; only **NTFS symlink** reparse points (`IO_REPARSE_TAG_SYMLINK`) are skipped, and **junctions** are followed again.

### Code quality

- **Sonar:** `FileIndex::RebuildPathToIdMapLocked` uses a C++17 structured binding (cpp:S6005).

---

## March 22, 2026

### Performance

- **Attribute loading:** Replaced the two-task-per-file approach (separate size task + mtime task) with a single combined task that loads both in one `stat()` syscall, eliminating the race where both tasks would call `stat()` on the same file before either wrote to the cache. `FolderSizeAggregator` now reads already-cached file sizes from `FileEntry` during its index scan, skipping `stat()` for files pre-loaded by sort attribute tasks.

---

## March 21, 2026

### Added

- **Multi-file drag-and-drop:** Drag selected rows from the results table to any shell target (Explorer, Desktop, Finder, etc.). Dragging from an unselected row snaps selection to that row and performs a single-file drag. Dragging from a selected row drags all eligible selected files at once. Directories and files pending deletion are excluded. Available on Windows and macOS.
- **Results table:** ImGui multi-select drives row selection and range extension (Shift+arrows, Shift+N/P, Ctrl+A, Ctrl+click), with table-aligned focus and clipper handling for range anchors.
- **Benchmarks:** macOS test script can run an optional std-linux filename hot-path search benchmark after the sample run.

### Changed

- **Indexing:** Bump-pointer name arena replaces many small string allocations on large indexes; vestigial per-entry extension pooling removed from `FileEntry`.
- **Parallel search:** Filename filters use bounded `string_view` path slices.

### Fixed

- **Results table:** Ctrl+click merges into the existing selection; N/P moves one row per keypress; Shift+arrow / Shift+N/P ranges work with the scrollable table’s focus scope.

---

## March 15, 2026

### Added

- **Status bar progress bar:** When the app is busy (indexing, searching, loading attributes, or computing folder sizes), a themed indeterminate progress bar appears in the status bar. Track and fill use theme colors (Border, Accent). Footer layout reserves space so the status bar stays visible.
- **Folder attributes:** Folder rows in the results table now display last modified time and aggregate size instead of placeholder \"Folder\" values. **NEEDS MORE TESTING**
- **Linux:** Status bar now shows process memory (VmRSS from `/proc/self/status`), matching Windows and macOS. Thread count uses a cgroup-aware fallback when `std::thread::hardware_concurrency()` returns 0 (e.g. in containers).
- **Documentation:** Lock ordering and critical-sections design note (`docs/design/LOCK_ORDERING_AND_CRITICAL_SECTIONS.md`) — rules for no I/O under locks and per-component mutex usage.
- **Results shortcuts:** New Ctrl/Cmd+Shift+X shortcut copies selected or marked rows from the results table to the clipboard as CSV (header row plus one line per row, visible columns only).


### Changed

- **Thread pool sizing:** Application, search engine, and folder crawler use `GetLogicalProcessorCount()` (from ThreadUtils) instead of `std::thread::hardware_concurrency()` so Linux cgroup/container limits are respected when the runtime reports 0 cores.
- **About / Help:** About section helpers and Help window refinements (see `AboutSectionHelpers.cpp/.h`).

### Fixed

- **Linux:** Process memory in the status bar was not shown; it now displays "Memory: X MB" using `/proc/self/status` (VmRSS).

---

## March 14, 2026

### Added

- **Fuzzy search:** Use the `fz:` prefix in the search box for fuzzy matching (e.g. typo-tolerant and abbreviation-friendly matches). Documented in the in-app Help window.
- **Indexing UI:** Improved progress display and feedback while the initial index is being built.

### Changed

- **UI:** Close, Cancel, and Delete buttons now use a consistent width across dialogs.
- **Results panel:** Shortcuts header text improved for better readability on smaller or resized windows.
- **Dependencies:** FreeType 2.14.2, nlohmann/json v3.12.0, ImGui docking branch.

### Fixed

- **Windows indexing:** When USN initial population failed, the UI could stay stuck on “Indexing” with no way to recover. The app now correctly leaves the indexing state and allows choosing another folder or retrying.
- **Search:** Background search worker now catches unhandled exceptions so a bad query or index state is less likely to crash the application.
- **Windows / Gemini API:** WinHttp handles are managed with RAII, avoiding handle leaks and improving reliability when using the Gemini integration.
