#pragma once

#include <cstddef>
#include <string>

namespace ui {

/**
 * @file ui/AboutSectionHelpers.h
 * @brief Helpers for the Help window "About" section (version, build, platform, system info).
 *
 * Encapsulates low-level dependencies (Logger, ThreadUtils, Windows PGO, etc.)
 * so HelpWindow stays free of platform and util includes.
 */

/** Application display name (e.g. "The FindHelper Experiment"). */
[[nodiscard]] const char* GetAboutAppDisplayName();

/** Version string for display (e.g. "abc1234", without "v-" prefix). */
[[nodiscard]] const char* GetAboutAppVersion();

/** Help window title (e.g. "The FindHelper Experiment – Help"). */
[[nodiscard]] const char* GetAboutHelpWindowTitle();

/** Build type label: "(Release)", "(Debug)", "(Release, Boost)", "(Debug, Boost)". */
[[nodiscard]] const char* GetAboutBuildTypeLabel();

/** PGO mode: 'G' (GENPROFILE), 'U' (USEPROFILE), or '\0' if none. */
[[nodiscard]] char GetAboutPgoMode();

/** Tooltip text for PGO mode; returns nullptr if mode is '\0'. */
[[nodiscard]] const char* GetAboutPgoTooltip(char pgo_mode);

/** Platform/monitoring label: "Windows (No Monitoring)", "macOS (No Monitoring)", "Linux (No Monitoring)". */
[[nodiscard]] const char* GetAboutPlatformMonitoringLabel();

/** Number of logical processors (for thread pool sizing). */
[[nodiscard]] size_t GetAboutLogicalProcessorCount();

/**
 * Current process memory as display string (e.g. "15.3 MB") or "N/A" if unavailable.
 * Uses a live read every call; prefer GetAboutProcessMemoryDisplayFromBytes when the
 * value is already throttled (e.g. from GuiState, same as status bar).
 */
[[nodiscard]] std::string GetAboutProcessMemoryDisplay();

/**
 * Format precomputed memory bytes for About display (e.g. "15.3 MB", or "N/A" when 0).
 * Use when the caller supplies a throttled value (e.g. state.memory_bytes_) so the Help
 * window does not refresh more often than the status bar.
 */
[[nodiscard]] std::string GetAboutProcessMemoryDisplayFromBytes(size_t memory_bytes);

/**
 * Regex engine(s) used for pattern matching (literal/simple/complex paths).
 * E.g. "String search, SimpleRegex, std::regex" or "String search, SimpleRegex, Boost.Regex".
 * Updated when RE2 is integrated (RE2, then Boost, then std::regex).
 */
[[nodiscard]] std::string GetAboutRegexEnginesLabel();

}  // namespace ui
