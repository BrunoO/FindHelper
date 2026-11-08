#pragma once

// Static application identity strings (display name, window titles).
// Intentionally independent of GitVersion.h so UI/bootstrap TUs do not
// recompile when APP_VERSION changes on every commit.

// Centralized application display name for UI and window titles.
// Keep this in sync with app.manifest and documentation.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage) - Macro simplifies string literal reuse and concatenation in other macros; kept for compatibility with existing UI title definitions
#define APP_DISPLAY_NAME_STR "The FindHelper Experiment"  // NOSONAR(cpp:S5028) - Macro needed for compile-time string literal concatenation in HELP/METRICS title macros
#define HELP_WINDOW_TITLE_STR APP_DISPLAY_NAME_STR " – Help"
#define METRICS_WINDOW_TITLE_STR APP_DISPLAY_NAME_STR " Metrics"

// NOLINTNEXTLINE(readability-identifier-naming) - UPPER_SNAKE_CASE for app display name (sync with app.manifest/docs)
inline constexpr const char* APP_DISPLAY_NAME = APP_DISPLAY_NAME_STR;
