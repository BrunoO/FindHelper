# Release Notes

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
