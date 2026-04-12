# Release Notes

## April 11, 2026

### Changed

- **Search results:** Match lists are applied when a search **completes** (single handoff to the UI). The previous **partial-result streaming** path has been removed so behavior is simpler and easier to reason about.
- **Parallel search:** The app always uses the **hybrid** parallel schedule. Alternate load-balancing modes and their settings / UI controls are **removed** so Windows, macOS, and Linux behave the same.
- **Settings window:** Closing the window with the **frame close** control no longer triggers an extra settings **save** from the render path (saves still happen from explicit actions such as **Save**).

### Fixed

- **Auto-refresh / results table:** Less flicker when the index changes often, including when sorting by **Size** or **Last modified** during auto-refresh.
- **Settings save:** Failures while writing settings are handled more safely instead of failing silently.
- **Thread pool sizing:** Comparison logic now uses **live** settings so overrides behave consistently.
- **USN / index maintenance:** Follow-up reliability work around journal **queue** handling and **integrity** signaling (including a fix so routine size updates do not latch “integrity” errors).

### Added

- **USN journal queue:** Standalone module with **unit tests** for queue behavior.

### Documentation

- Public design notes updated for **hybrid-only** parallel search; former **interleaved** strategy material is kept as **historical / archived** context only.

### Code quality

- Regression and unit-test cleanups (including ImGui Test Engine cases) aligned with the streamlined search pipeline.

---

## April 9, 2026

### Fixed

- **Windows indexing reliability:** Improved handling for USN journal buffer-loss scenarios so index integrity issues are surfaced more consistently.
- **Settings window stability:** Minor cleanup in window-open handling to reduce edge-case risk.

### Added

- **USN coverage:** Added focused unit tests around USN queue behavior.

### Code quality

- Reduced complexity in regex helper code to make future maintenance safer and easier.
- Simplified parts of async-sort state handling for better readability.
- Updated lint configuration to better match project naming conventions and reduce false positives.

---

## April 7, 2026

### Performance

- **Regex search pre-filter:** `rs:` patterns now extract the longest required literal substring at matcher-creation time and reject non-matching filenames with a fast SIMD substring check before invoking the regex engine. Common patterns such as `rs:\.pdf$` (literal `.pdf`) and `rs:^report` (literal `report`) skip `std::regex_search` for the vast majority of files, reducing per-file cost from ~1 µs to ~20 ns on mismatches. Patterns with top-level alternation (`|`) or pure character-class patterns (`\d{4}`) correctly bypass the pre-filter.

### Tests

- **Regex pre-filter regression suite** (`RegexSearchPrefilterTests`): 30 test cases / 70 assertions covering `MatchPattern`, `CreateFilenameMatcher`, and end-to-end `SearchAsyncWithData` for extension anchors, prefix anchors, alternation, character-class patterns, case-sensitivity boundaries, and the critical case-insensitive uppercase-extension scenario (`ANNUAL_REPORT.PDF` must match `rs:\.pdf$`).
- **`ExtractRequiredLiteral` unit tests** added to `StdRegexUtilsTests`: one `TEST_CASE` per distinct branch — anchors, escaped literals, character-class escapes, `[...]` character classes, dot quantifiers, top-level alternation, groups, optional/required plain chars, optional/required escaped chars, and longest-run selection.

### Code quality

- Sonar-driven cleanups: `UIThreadOwned<T>` enforces UI-thread write confinement; `AsyncSortState` merges dual-bool with a typed `SortReadyState` enum; `ResultPoolOwner` enforces pool-lifecycle ordering; `RenderApplicationControls` relocated with documented contracts.
- Thread pool worker lambda extracted into a named loop helper (Sonar S1188 / S107).
- UI-thread marking and safety assertions added across platforms.

### Changed

- **Settings window:** reopens re-centered on the main window.
- **Settings:** mode-specific helper text added to the UI Mode selector.
- **Theme:** accent tones and table-header contrast rebalanced.
- **Search history rows:** two-line layout with right-aligned hover actions and relative timestamps.
- **Welcome panel:** replaces the older `EmptyState` component; history row layout and ImGui cursor bounds corrected.
- **Windows:** application keeps the system display awake while running.
- **UI mode load:** legacy `simplifiedUI` key no longer overrides `uiMode` when both are present in settings.
- **Index path access:** `PathAccessor` view methods replaced with `GetPathCopy()` to eliminate dangling-view risk.

### Fixed

- **Search:** setup exceptions in `StaticChunkingStrategy` and `HybridStrategy` task lambdas are now caught; prevents silent thread-pool task failure on bad index state.
- **USN monitoring:** index-integrity latch with status-bar notice when journal wrap or queue drop is detected.

---

## April 3, 2026

### Added

- **Unified Search History:** Empty-state **Search History** panel with pinned and recent queries, persisted to `search_history.json`. **Pin / unpin** the last completed search (**Ctrl/Cmd+P**), **rename** and **delete** entries (with confirmation), and per-row actions for discoverability. **Zero-result** manual searches are recorded so they can be pinned or reopened. Replaces the older saved/recent combo and footer flows; **Help** shortcuts were updated to match (e.g. **Pin search** instead of legacy save wording).
- **Windows diagnostics:** Optional **minidump** generation on unhandled exceptions, with safer crash-dump path handling.
- **Empty state:** **Show all indexed items** is aligned with the other example chips for consistent styling.

### Changed

- **Metrics window:** Toggled from **Settings**; the dedicated CLI flag for opening metrics was removed.
- **Results table:** Folder file-count column renamed to **Descendant Files*** with a header tooltip explaining the value is **cached** and may lag the live file system.
- **Logging:** Log file name is derived from the **executable** name.
- **USN monitoring:** FSCTL **995** is treated as **graceful cancellation** instead of a fatal control error.

### Fixed

- **Windows / search stability:** Hardening against crashes during **auto-refresh** and in the **streaming** / **incremental search** path: consistent **results batch** invalidation, **ClearResultPool**-style pool lifecycle, **UI-thread-only** commits for async sort staging, **sort cancellation** ordering before waiting on attribute futures, **streaming collector** lifetime across UI polling, **thread-safe error strings** (no cross-thread dangling `string_view`), and safer **path-pool remap** / pending-batch bounds.
- **Results handoff:** **Folder-size** background work for the previous result set is cancelled before applying a new one.
- **Settings:** Load/save merge behaves correctly when **sidecar JSON** omits expected array keys.
- **Build / CI:** MSVC linking (**BuildHistoryParams**), **Application** constructor signature consistency, and **GitHub Actions** version pin fixes for workflows.

### Code quality

- Sonar-driven cleanups (search history, sort UI, crash handler); Cursor rules for **async ownership / lifetime** and **crash triage** discipline.

---

## March 30, 2026

### Added

- **Help → About:** Startup command-line arguments (excluding the program path) are now displayed under **About**, making it easy to verify the runtime launch context from within the UI.

### Changed

- **Saved searches:** Extracted to a dedicated `saved_searches.json` file alongside `settings.json`, keeping `settings.json` focused on scalar preferences. Legacy `savedSearches` embedded in `settings.json` is automatically migrated on first load.
- **Concurrent-instance safety:** On every settings save, both saved-search and recent-search collections are merged from disk (not just when the in-memory list is empty). Disk-only entries are appended; name-matched saved searches keep the in-memory (local) value; recent searches are deduplicated by full equality and capped at the configured maximum.

### Fixed

- **Windows CI:** `run_tests` CMake target now passes `-C <config>` to `ctest`, fixing test runs from the Developer Command Prompt.

---

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
