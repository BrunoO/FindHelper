# DRY constants analysis

**Date:** 2026-02-20

**Guiding principle:** One constant per purpose; avoid duplicating the same value or semantic in multiple places.

## Consolidations done

### 1. Max recent searches
- **Before:** `k_max_recent_to_show` in EmptyState.cpp, `max_recent_searches` in TimeFilterUtils.cpp.
- **After:** Single `settings_defaults::kMaxRecentSearches` in Settings.h, used for both storage limit and display limit.

### 2. Window size bounds (min/max width and height)
- **Before:** Same values (640, 4096, 480, 2160) in Application.cpp (`application_constants`), Settings.cpp (anonymous namespace), CommandLineArgs.cpp (local constants and help text).
- **After:** `settings_defaults::kMinWindowWidth`, `kMaxWindowWidth`, `kMinWindowHeight`, `kMaxWindowHeight` in Settings.h. Used in Settings.cpp, Application.cpp, CommandLineArgs.cpp; help text in CommandLineArgs uses these constants so it stays in sync.

### 3. Default window size
- **Before:** `kDefaultWindowWidth` / `kDefaultWindowHeight` in both main_common.h and Settings.h.
- **After:** Only in `settings_defaults` (Settings.h). main_common.h includes Settings.h and uses `settings_defaults::kDefaultWindowWidth` / `kDefaultWindowHeight`.

### 4. Recrawl interval and idle threshold bounds
- **Before:** Min/max (1, 60 and 1, 30) in Settings.cpp (anonymous namespace), SettingsWindow.cpp (`settings_ui_constants`), ApplicationLogic.cpp (local constexpr).
- **After:** `settings_defaults::kMinRecrawlIntervalMinutes`, `kMaxRecrawlIntervalMinutes`, `kMinRecrawlIdleThresholdMinutes`, `kMaxRecrawlIdleThresholdMinutes` in Settings.h. Used in Settings.cpp, SettingsWindow.cpp, ApplicationLogic.cpp.

### 5. Windows/Unix epoch difference (FILETIME)
- **Before:** Value `11644473600LL` in FileSystemUtils.h (two local constants in different functions) and in TimeFilterUtils.cpp.
- **After:** Single `file_time_constants::kEpochDiffSeconds` in FileTimeTypes.h. FileSystemUtils.h and TimeFilterUtils.cpp use it.

### 6. Context menu debounce (ResultsTable.cpp)
- **Before:** `kContextMenuDebounceMs = 300` defined twice (in two functions).
- **After:** Single constant `kContextMenuDebounceMs = 300` in the anonymous namespace; both call sites use it.

### 7. Thread pool size bounds
- **Before:** CommandLineArgs.cpp had `kThreadPoolSizeMax = 64`; SettingsWindow.cpp had `kMaxThreadPoolSize = 64`.
- **After:** `settings_defaults::kMinThreadPoolSize` and `kMaxThreadPoolSize` in Settings.h. CommandLineArgs.cpp and SettingsWindow.cpp use them; help text and tooltip use the constant so they stay in sync.

## Remaining opportunities (not changed here)

- **Font size / UI scale ranges:** Settings.cpp uses one set of validation bounds (e.g. kMaxFontSize 64); SettingsWindow.cpp uses different slider bounds (e.g. kMaxFontSize 24). These are intentionally different (validation allows wider range than the UI slider). No DRY issue.
- **Clear color (0.45, 0.55, 0.60):** OpenGLManager.cpp and DirectXManager.cpp each define the same clear color. Could be shared for consistency; left as-is because they are per-backend and may diverge.

## Where to add shared constants

- **App/settings bounds and defaults:** `settings_defaults` in `core/Settings.h`. Use for anything that is a default or a min/max for a setting (window, recrawl, recent list size, etc.).
- **FILETIME / time conversion:** `file_time_constants` in `utils/FileTimeTypes.h`. Use for Windows epoch vs Unix epoch and related FILETIME conversion values.
- **File-system / attribute constants:** `file_system_utils_constants` in `utils/FileSystemUtils.h` for things like filetime intervals per second, etc.

When adding a new constant, search the repo for the same numeric value or concept; if it already exists elsewhere, reuse or move to the appropriate shared location (DRY).
