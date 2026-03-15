# Release Notes

## March 15, 2026

### Added

- **Linux:** Status bar now shows process memory (VmRSS from `/proc/self/status`), matching Windows and macOS. Thread count uses a cgroup-aware fallback when `std::thread::hardware_concurrency()` returns 0 (e.g. in containers).
- **Documentation:** Lock ordering and critical-sections design note (`docs/design/2026-03-15_LOCK_ORDERING_AND_CRITICAL_SECTIONS.md`) — rules for no I/O under locks and per-component mutex usage.


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
