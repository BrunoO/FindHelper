/**
 * @file Settings.cpp
 * @brief Implementation of application settings persistence and management
 *
 * This file implements functions for loading and saving application settings
 * to/from a JSON file. Settings include UI preferences, search configuration,
 * and thread pool sizing.
 *
 * STORAGE:
 * - Primary: {USER_HOME}/.FindHelper/findhelper_settings.json
 *   (Windows: %USERPROFILE%\.FindHelper\findhelper_settings.json;
 *    macOS/Linux: $HOME/.FindHelper/findhelper_settings.json)
 * - Search history: {same directory}/search_history.json
 * - Failsafe: if primary is unavailable (HOME unset, directory not writable, etc.),
 *   falls back to legacy: Windows = next to executable or CWD; macOS/Linux = CWD.
 * - Migration: on first run with new logic, if no file at primary but one at legacy,
 *   load from legacy and save to primary (legacy file is not deleted).
 *
 * SETTINGS INCLUDED:
 * - Window size and position
 * - Font family and size
 * - Search thread pool size
 * - UI preferences (instant search, auto-refresh, etc.)
 *
 * ERROR HANDLING:
 * - Graceful fallback to defaults if file is missing or corrupted
 * - Validation of numeric values (thread pool size, window dimensions)
 * - Logging of errors for debugging
 *
 * TEST SUPPORT:
 * - In-memory settings mode for unit tests (test_settings namespace)
 * - Allows tests to override settings without file I/O
 *
 * @see Settings.h for AppSettings struct definition
 * @see Application.cpp for settings usage
 */

#include "core/Settings.h"

#include "search/SearchHistory.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#endif  // _WIN32

#include <filesystem>
#include <nlohmann/json.hpp>

#include "path/PathUtils.h"
#include "search/SearchTypes.h"
#include "utils/Logger.h"

using json = nlohmann::json;  // NOLINT(readability-identifier-naming,readability-redundant-qualified-alias) - nlohmann convention; lowercase matches library style

// Validation bounds for settings (file-local where not in settings_defaults)
namespace {
constexpr float kMinFontSize = 4.0F;
constexpr float kMaxFontSize = 64.0F;
constexpr float kMinFontScale = 0.2F;
constexpr float kMaxFontScale = 4.0F;

// Settings file location (DRY: single definition per spec)
constexpr std::string_view kSettingsSubfolder = ".FindHelper";
constexpr std::string_view kSettingsFileName = "findhelper_settings.json";
constexpr std::string_view kSearchHistoryFileName = "search_history.json";

// Appearance fields that users notice when "settings reverted to defaults".
// Ignores uiMode / crawlFolder / search tuning so Minimalistic + factory look still flags.
[[nodiscard]] bool LooksLikeFactoryAppearanceDefaults(const AppSettings& settings) {
  const AppSettings defaults;
  return settings.themeId == defaults.themeId && settings.fontFamily.empty() &&
         settings.fontSize == defaults.fontSize && settings.fontScale == defaults.fontScale &&
         settings.windowWidth == defaults.windowWidth &&
         settings.windowHeight == defaults.windowHeight;
}

// One Release-visible summary per process so FileIndex LoadSettings fallbacks do not spam.
void LogSettingsLoadResultOnce(std::string_view path, const AppSettings& loaded) {
  static std::once_flag once;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables) - process-lifetime diagnostic gate
  std::call_once(once, [path, &loaded] {
    const char* const appearance = LooksLikeFactoryAppearanceDefaults(loaded)
                                       ? "factory-default appearance"
                                       : "customized appearance";
    LOG_IMPORTANT_BUILD("Settings: loaded from '"
                        << path << "' (" << appearance << "; theme=" << loaded.themeId
                        << ", fontFamily="
                        << (loaded.fontFamily.empty() ? "(default)" : loaded.fontFamily)
                        << ", window=" << loaded.windowWidth << "x" << loaded.windowHeight
                        << ", uiMode=" << static_cast<int>(loaded.uiMode) << ")");
  });
}
}  // namespace

// Test-only: In-memory settings support
namespace test_settings {
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - Test infra; must be mutable; suffix matches project convention
  static AppSettings* in_memory_settings_ = nullptr;  // NOSONAR(cpp:S5421) - Must be modifiable for test infrastructure
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - Test infra; must be mutable
  static bool in_memory_mode_ = false;  // NOSONAR(cpp:S5421) - Must be modifiable for test infrastructure

  void SetInMemorySettings(const AppSettings& settings) {
    static AppSettings storage;
    storage = settings;
    in_memory_settings_ = &storage;
    in_memory_mode_ = true;
  }

  AppSettings GetInMemorySettings() {
    if (in_memory_settings_ != nullptr) {
      return *in_memory_settings_;
    }
    AppSettings defaults;
    return defaults;
  }

  void ClearInMemorySettings() {
    in_memory_settings_ = nullptr;
    in_memory_mode_ = false;
  }

  bool IsInMemoryMode() {
    return in_memory_mode_;
  }
}

// Helper: Load and validate theme ID (allowlist of seven known IDs)
static void LoadThemeId(const json& j, AppSettings& out) {
  if (!j.contains("themeId") || !j["themeId"].is_string()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    return;
  }
  const std::string value = j["themeId"].get<std::string>();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  static constexpr std::array<std::string_view, 7> kAllowedThemeIds = {
      "default_dark", "dracula", "nord", "one_dark", "gruvbox", "everforest", "catppuccin",};
  if (std::find(kAllowedThemeIds.begin(), kAllowedThemeIds.end(), value) !=  //  - C++17; std::ranges requires C++20
      kAllowedThemeIds.end()) {
    out.themeId = value;
  }
}

// Helper: Load basic UI settings (font, window size)
static void LoadBasicSettings(const json& j, AppSettings& out) {
  // Font family (string)
  if (j.contains("fontFamily") && j["fontFamily"].is_string()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    if (const auto family = j["fontFamily"].get<std::string>(); !family.empty()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
      out.fontFamily = family;
    }
  }

  // Font size (number)
  if (j.contains("fontSize") && j["fontSize"].is_number()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    if (const float size = j["fontSize"]; size > kMinFontSize && size < kMaxFontSize) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
      out.fontSize = size;
    }
  }

  // Font scale (number)
  if (j.contains("fontScale") && j["fontScale"].is_number()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    if (const float scale = j["fontScale"]; scale > kMinFontScale && scale < kMaxFontScale) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
      out.fontScale = scale;
    }
  }

  // Window size (optional, stored as integers)
  if (j.contains("windowWidth") && j["windowWidth"].is_number_integer()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    if (const int width = j["windowWidth"];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
        width >= settings_defaults::kMinWindowWidth && width <= settings_defaults::kMaxWindowWidth) {
      out.windowWidth = width;
    }
  }

  if (j.contains("windowHeight") && j["windowHeight"].is_number_integer()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    if (const int height = j["windowHeight"];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
        height >= settings_defaults::kMinWindowHeight && height <= settings_defaults::kMaxWindowHeight) {
      out.windowHeight = height;
    }
  }

  LoadThemeId(j, out);
}

// Helper: Load and validate thread pool size (0 = auto, 1-64 = explicit)
static void LoadThreadPoolSize(const json& j, AppSettings& out) {
  if (!j.contains("searchThreadPoolSize") || !j["searchThreadPoolSize"].is_number_integer()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    return;
  }
  if (const int pool_size = j["searchThreadPoolSize"];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
      pool_size == 0 || (pool_size >= 1 && pool_size <= 64)) {
    out.searchThreadPoolSize = pool_size;
  } else {
    LOG_WARNING_BUILD("Invalid searchThreadPoolSize value: " << pool_size
        << ". Valid range: 0 (auto-detect) or 1-64. Using default (0 = auto-detect).");
  }
}

// Helper: Load and validate dynamic chunk size
static void LoadDynamicChunkSize(const json& j, AppSettings& out) {
  if (!j.contains("dynamicChunkSize") || !j["dynamicChunkSize"].is_number_integer()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    return;
  }
  if (const int chunk_size = j["dynamicChunkSize"];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
      chunk_size >= settings_defaults::kMinDynamicChunkSize &&
      chunk_size <= settings_defaults::kMaxDynamicChunkSize) {
    out.dynamicChunkSize = chunk_size;
  } else {
    LOG_WARNING_BUILD("Invalid dynamicChunkSize value: " << chunk_size
        << ". Valid range: " << settings_defaults::kMinDynamicChunkSize << "-"
        << settings_defaults::kMaxDynamicChunkSize << ". Using default ("
        << settings_defaults::kDefaultDynamicChunkSize << ").");
  }
}

// Helper: Load and validate hybrid initial work percent
static void LoadHybridInitialWorkPercent(const json& j, AppSettings& out) {
  if (!j.contains("hybridInitialWorkPercent") || !j["hybridInitialWorkPercent"].is_number_integer()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    return;
  }
  if (const int percent = j["hybridInitialWorkPercent"];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
      percent >= settings_defaults::kMinHybridInitialWorkPercent &&
      percent <= settings_defaults::kMaxHybridInitialWorkPercent) {
    out.hybridInitialWorkPercent = percent;
  } else {
    LOG_WARNING_BUILD("Invalid hybridInitialWorkPercent value: " << percent
        << ". Valid range: " << settings_defaults::kMinHybridInitialWorkPercent << "-"
        << settings_defaults::kMaxHybridInitialWorkPercent << ". Using default ("
        << settings_defaults::kDefaultHybridInitialWorkPercent << ").");
  }
}

// Helper: Load and validate guided scheduling divisor
static void LoadGuidedSchedulingDivisor(const json& j, AppSettings& out) {
  if (!j.contains("guidedSchedulingDivisor") || !j["guidedSchedulingDivisor"].is_number_integer()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    return;
  }
  if (const int divisor = j["guidedSchedulingDivisor"];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
      divisor >= settings_defaults::kMinGuidedSchedulingDivisor &&
      divisor <= settings_defaults::kMaxGuidedSchedulingDivisor) {
    out.guidedSchedulingDivisor = divisor;
  } else {
    LOG_WARNING_BUILD("Invalid guidedSchedulingDivisor value: " << divisor
        << ". Valid range: 1-8. Using default (2).");
  }
}

// Helper: Load search configuration settings
static void LoadSearchConfigSettings(const json& j, AppSettings& out) {
  LoadThreadPoolSize(j, out);
  LoadDynamicChunkSize(j, out);
  LoadHybridInitialWorkPercent(j, out);
  LoadGuidedSchedulingDivisor(j, out);
  if (j.contains("hideSystemFiles") && j["hideSystemFiles"].is_boolean()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    out.hideSystemFiles = j["hideSystemFiles"];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
}

// Helper: Validate Windows volume format (e.g., "C:", "D:") - single letter + colon
#ifdef _WIN32
static bool IsValidWindowsVolume(std::string_view volume) {
  if (volume.length() != 2) {
    return false;
  }
  const auto drive = static_cast<char>(std::toupper(static_cast<unsigned char>(volume[0])));
  return std::isalpha(static_cast<unsigned char>(drive)) != 0 && volume[1] == ':';
}
#endif  // _WIN32

// Helper: Set monitored volume with Windows-only validation (reduces cognitive complexity)
static void SetMonitoredVolumeIfValid(std::string_view volume, AppSettings& out) {
#ifdef _WIN32
  if (IsValidWindowsVolume(volume)) {
    out.monitoredVolume = volume;
  } else {
    LOG_WARNING_BUILD("Invalid monitoredVolume value: " << volume
                      << " (must be format \"C:\", \"D:\", etc., using default)");
  }
#else
  // On non-Windows platforms, store the value but it will be ignored
  out.monitoredVolume = volume;
#endif  // _WIN32
}

// Helper: Load monitored volume (Windows-only validation, cross-platform storage)
static void LoadMonitoredVolume(const json& j, AppSettings& out) {
  if (!j.contains("monitoredVolume") || !j["monitoredVolume"].is_string()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    return;
  }
  SetMonitoredVolumeIfValid(j["monitoredVolume"].get<std::string>(), out);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
}

// Helper: Load UI mode from JSON.
static void LoadUIMode(const json& j, AppSettings& out) {
  if (j.contains("uiMode")) {
    if (const auto& mode_val = j["uiMode"]; mode_val.is_number_integer()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
      if (const int mode = mode_val.get<int>(); mode >= 0 && mode <= 2) {
        out.uiMode = static_cast<AppSettings::UIMode>(mode);
      }
    } else if (mode_val.is_number_float()) {
      // Some editors/tools may write whole numbers as JSON floats (e.g. 2.0)
      if (const double mode_d = mode_val.get<double>();
          mode_d >= 0.0 && mode_d <= 2.0 && mode_d == std::floor(mode_d)) {
        out.uiMode = static_cast<AppSettings::UIMode>(static_cast<int>(mode_d));
      }
    }
  }
}

// Helper: Load recrawl interval
static void LoadRecrawlTimingSettings(const json& j, AppSettings& out) {
  if (j.contains("recrawlIntervalMinutes") && j["recrawlIntervalMinutes"].is_number_integer()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    int interval = j["recrawlIntervalMinutes"].get<int>();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    interval = (std::max)(settings_defaults::kMinRecrawlIntervalMinutes,
                          (std::min)(interval, settings_defaults::kMaxRecrawlIntervalMinutes));
    out.recrawl.intervalMinutes = interval;
  }
  // recrawlIdleThresholdMinutes and recrawlRequireIdle are ignored (removed feature)
}

// Helper: Load AI provider settings (provider id, base URL, model, key env).
// Secrets are never stored; only the env var name is persisted.
static void LoadAiProviderSettings(const json& j, AppSettings& out) {
  if (!j.contains("aiProvider") || !j["aiProvider"].is_object()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    return;
  }
  const auto& ai = j["aiProvider"];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  if (ai.contains("providerId") && ai["providerId"].is_string()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    if (const std::string provider_id = ai["providerId"].get<std::string>();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
        provider_id == "gemini" || provider_id == "openai-compatible") {
      out.aiProvider.providerId = provider_id;
    }
  }
  if (ai.contains("baseUrl") && ai["baseUrl"].is_string()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    out.aiProvider.baseUrl = ai["baseUrl"].get<std::string>();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  if (ai.contains("model") && ai["model"].is_string()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    out.aiProvider.model = ai["model"].get<std::string>();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  if (ai.contains("apiKeyEnv") && ai["apiKeyEnv"].is_string()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    out.aiProvider.apiKeyEnv = ai["apiKeyEnv"].get<std::string>();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  if (ai.contains("timeoutSeconds") && ai["timeoutSeconds"].is_number_integer()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    out.aiProvider.timeoutSeconds = ai["timeoutSeconds"].get<int>();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  // presetId only names the catalog entry; it is validated by the catalog
  // itself (an unknown id falls back to the custom entry), so the loader
  // stays free of any dependency on the provider layer.
  if (ai.contains("presetId") && ai["presetId"].is_string()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    out.aiProvider.presetId = ai["presetId"].get<std::string>();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
}

// Helper: Load crawl / recrawl related settings and UI preferences
static void LoadCrawlAndRecrawlSettings(const json& j, AppSettings& out) {
  if (j.contains("showWorkflowHint") && j["showWorkflowHint"].is_boolean()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    out.showWorkflowHint = j["showWorkflowHint"];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }

  if (j.contains("showMetricsButton") && j["showMetricsButton"].is_boolean()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    out.showMetricsButton = j["showMetricsButton"];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }

  LoadUIMode(j, out);

  if (j.contains("showPathHierarchyIndentation") &&
      j["showPathHierarchyIndentation"].is_boolean()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    out.showPathHierarchyIndentation = j["showPathHierarchyIndentation"];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }

  if (j.contains("crawlFolder") && j["crawlFolder"].is_string()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    out.crawlFolder = j["crawlFolder"].get<std::string>();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }

  LoadRecrawlTimingSettings(j, out);
}

// Helper: Load a single SearchHistoryParams from a JSON object.
static SearchHistoryParams LoadHistoryParamsFromJson(const json& item) {
  SearchHistoryParams params;
  if (item.contains("filename") && item["filename"].is_string()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    params.filename = item["filename"];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  if (item.contains("path") && item["path"].is_string()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    params.path = item["path"];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  if (item.contains("extensions") && item["extensions"].is_string()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    params.extensions = item["extensions"];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  if (item.contains("itemType") && item["itemType"].is_number_integer()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    const int raw_item_type = item["itemType"].get<int>();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    params.item_type = IsValidItemTypeFilterValue(raw_item_type) ? raw_item_type : 0;
  } else if (item.contains("foldersOnly") && item["foldersOnly"].is_boolean()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    params.item_type = item["foldersOnly"].get<bool>() ? 2 : 0;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  params.folders_only = (params.item_type == 2);
  if (item.contains("caseSensitive") && item["caseSensitive"].is_boolean()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    params.case_sensitive = item["caseSensitive"];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  if (item.contains("timeFilter") && item["timeFilter"].is_number_integer()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    params.time_filter = item["timeFilter"].get<int>();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  if (item.contains("sizeFilter") && item["sizeFilter"].is_number_unsigned()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    params.size_filter = item["sizeFilter"].get<std::uint64_t>();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  if (item.contains("aiSearchDescription") && item["aiSearchDescription"].is_string()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    params.ai_search_description = item["aiSearchDescription"];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  return params;
}

// Helper: Load a single SearchHistoryEntry from a JSON object.
static SearchHistoryEntry LoadHistoryEntryFromJson(const json& item) {
  SearchHistoryEntry entry;
  if (item.contains("id") && item["id"].is_string()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    entry.id = item["id"];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  if (item.contains("customName") && item["customName"].is_string()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    entry.custom_name = item["customName"];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  if (item.contains("isPinned") && item["isPinned"].is_boolean()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    entry.is_pinned = item["isPinned"];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  if (item.contains("createdAt") && item["createdAt"].is_number_integer()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    entry.created_at_unix_ms = item["createdAt"].get<std::int64_t>();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  if (item.contains("lastUsedAt") && item["lastUsedAt"].is_number_integer()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    entry.last_used_at_unix_ms = item["lastUsedAt"].get<std::int64_t>();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  if (item.contains("pinnedAt") && item["pinnedAt"].is_number_integer()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    entry.pinned_at_unix_ms = item["pinnedAt"].get<std::int64_t>();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  if (item.contains("lastResultCount") && item["lastResultCount"].is_number_integer()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    // Any negative stored value other than -1 is treated as unknown (-1);
    // 0 is a meaningful recorded zero-result run.
    const std::int64_t stored = item["lastResultCount"].get<std::int64_t>();  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    entry.last_run_result_count = (stored >= 0) ? stored : -1;
  }
  if (item.contains("params") && item["params"].is_object()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    entry.params = LoadHistoryParamsFromJson(item["params"]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  return entry;
}

// Helper: Load searchHistory array from a JSON object.
static void LoadHistoryEntries(const json& j, AppSettings& out) {
  if (!j.contains("searchHistory") || !j["searchHistory"].is_array()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    return;
  }
  out.searchHistory.clear();
  for (const auto& item : j["searchHistory"]) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
    if (!item.is_object()) {
      continue;
    }
    if (SearchHistoryEntry entry = LoadHistoryEntryFromJson(item); !entry.id.empty()) {
      out.searchHistory.push_back(std::move(entry));
    }
  }
}

// Helper: legacy path (next to executable on Windows, CWD on macOS/Linux).
static std::string GetLegacySettingsFilePath() {
#ifdef _WIN32
  std::string module_path(MAX_PATH, '\0');
  DWORD length = GetModuleFileNameA(nullptr, module_path.data(), MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return std::string(kSettingsFileName);
  }
  module_path.resize(length);

  std::string path = module_path;
  if (std::size_t pos = path.find_last_of("\\/"); pos != std::string::npos) {
    path.resize(pos + 1);
  } else {
    path.clear();
  }
  path += kSettingsFileName;
  return path;
#else
  return std::string(kSettingsFileName);
#endif  // _WIN32
}

// Helper: primary path {HOME}/.FindHelper/findhelper_settings.json. Returns empty if HOME not available.
static std::string GetPrimarySettingsPath() {
  const std::string home = path_utils::GetUserHomePath();
  if (home.empty()) {
    return {};
  }
  if (home == path_utils::GetDefaultUserRootPath()) {
    return {};
  }
  const std::string dir = path_utils::JoinPath(home, kSettingsSubfolder);
  return path_utils::JoinPath(dir, kSettingsFileName);
}

// Helper: primary directory {HOME}/.FindHelper. Returns empty if HOME not available.
static std::string GetPrimarySettingsDir() {
  const std::string home = path_utils::GetUserHomePath();
  if (home.empty() || home == path_utils::GetDefaultUserRootPath()) {
    return {};
  }
  return path_utils::JoinPath(home, kSettingsSubfolder);
}

// Helper: true if primary path can be used (HOME set, dir exists or can be created, writable).
static bool CanUsePrimaryPath(bool for_save) {
  const std::string home = path_utils::GetUserHomePath();
  const std::filesystem::path primary_dir(GetPrimarySettingsDir());
  if (primary_dir.empty()) {
    LOG_IMPORTANT_BUILD("Settings: primary path skipped — HOME/USERPROFILE unset or default (home='"
                        << home << "')");
    return false;
  }
  try {
    if (for_save) {
      std::filesystem::create_directories(primary_dir);
    }
    if (!std::filesystem::exists(primary_dir) || !std::filesystem::is_directory(primary_dir)) {
      LOG_IMPORTANT_BUILD("Settings: primary path skipped — .FindHelper missing or not a directory ('"
                          << primary_dir.string() << "', for_save=" << (for_save ? "true" : "false")
                          << ")");
      return false;
    }
    return true;
  } catch (const std::filesystem::filesystem_error& e) {
    (void)e;
    LOG_IMPORTANT_BUILD("Settings: primary path skipped — filesystem error (" << e.what() << ")");
    return false;
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all after filesystem_error
    (void)e;
    LOG_IMPORTANT_BUILD("Settings: primary path skipped — exception (" << e.what() << ")");
    return false;
  }
}

// Resolve effective settings file path: primary when usable, else legacy. Log fallback.
static std::string ResolveSettingsFilePath(bool for_save) {
  if (CanUsePrimaryPath(for_save)) {
    return GetPrimarySettingsPath();
  }
  std::string legacy = GetLegacySettingsFilePath();
  LOG_IMPORTANT_BUILD("Settings: using legacy path (failsafe): '" << legacy << "'");
  return legacy;
}

// Path to search_history.json in the same directory as the resolved settings file.
static std::string ResolveHistoryFilePath(bool for_save) {
  const std::string settings_path = ResolveSettingsFilePath(for_save);
  const std::filesystem::path p(settings_path);
  const std::filesystem::path parent = p.parent_path();
  if (parent.empty()) {
    return std::string(kSearchHistoryFileName);
  }
  return (parent / std::string(kSearchHistoryFileName)).string();
}

static std::string GetHistoryPathAdjacentToSettingsFile(std::string_view settings_file_path) {
  const std::filesystem::path p(settings_file_path);
  return (p.parent_path() / std::string(kSearchHistoryFileName)).string();
}

// Read search_history.json from path into out; returns false on missing file, parse failure,
// or JSON that does not contain a well-formed "searchHistory" array.
static bool ReadHistoryJsonFromFile(const std::string& path, AppSettings& out) {
  std::ifstream f(path, std::ios::in | std::ios::binary);
  if (!f.is_open()) {
    return false;
  }
  try {
    json j;
    f >> j;
    if (!j.contains("searchHistory") || !j["searchHistory"].is_array()) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
      return false;
    }
    LoadHistoryEntries(j, out);
    return true;
  } catch (const json::parse_error& e) {
    (void)e;
    return false;
  } catch (const json::exception& e) {
    (void)e;
    return false;
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - After json::*; catch allocator/stream failures
    (void)e;
    return false;
  }
}

// Parse JSON from an open stream into out; resets out to {} on failure.
static bool ParseJsonFromOpenStream(std::ifstream& file, json& out) {
  if (!file.is_open()) {
    return false;
  }
  try {
    file >> out;
    return true;
  } catch (const json::parse_error& e) {
    (void)e;
    out = json::object();
    return false;
  } catch (const json::exception& e) {
    (void)e;
    out = json::object();
    return false;
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - After json::*; catch allocator/stream failures
    (void)e;
    out = json::object();
    return false;
  }
}

static bool TryLoadHistoryFromDedicatedFile(AppSettings& out) {
  return ReadHistoryJsonFromFile(ResolveHistoryFilePath(false), out);
}

static void FinishLoadHistory(AppSettings& out) {
  // No migration: search_history.json is a dedicated file; there is no legacy key in settings.
  TryLoadHistoryFromDedicatedFile(out);
  SeedDefaultPinnedSearchesIfNeeded(out);
}

static void MergeHistoryFromDisk(AppSettings& to_write) {
  // Merge policy: disk entries absent from memory are appended; memory wins on id conflict.
  // Dedup key is entry.id (stable FNV-1a hash of normalized params).
  json existing_j;
  {
    const std::string path = ResolveHistoryFilePath(false);
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!ParseJsonFromOpenStream(f, existing_j)) {
      return;
    }
  }
  AppSettings from_disk;
  LoadHistoryEntries(existing_j, from_disk);
  if (from_disk.searchHistory.empty()) {
    return;
  }

  size_t merged_count = 0;
  for (const auto& disk_entry : from_disk.searchHistory) {
    if (disk_entry.id.empty()) {
      continue;
    }
    const auto* const already_present =
        FindHistoryEntryById(to_write.searchHistory, disk_entry.id);
    if (already_present == nullptr) {
      to_write.searchHistory.push_back(disk_entry);
      ++merged_count;
    }
  }
  if (merged_count > 0U) {
    LOG_INFO_BUILD("Settings: merged " << merged_count << " search history entr(ies) from disk");
  }
}

// Helper: Load settings content from an already opened stream.
// Separated from LoadSettings() to reduce cognitive complexity in the public API.
static bool LoadSettingsFromJsonStream(std::ifstream& file, AppSettings& out) {
  json j;
  file >> j;

  // Load settings using helper functions to reduce cognitive complexity
  LoadBasicSettings(j, out);
  LoadSearchConfigSettings(j, out);
  LoadMonitoredVolume(j, out);
  LoadCrawlAndRecrawlSettings(j, out);
  LoadAiProviderSettings(j, out);

  FinishLoadHistory(out);

  return true;
}

// Log a JSON-related error from a settings operation and return false (for use in catch blocks).
static bool LogJsonSettingsError(std::string_view path, std::string_view prefix, const char* what) {
  LOG_ERROR(std::string("Settings: ") + std::string(prefix) + " '" + std::string(path) + "': " +
            std::string(what));
  LOG_IMPORTANT_BUILD("Settings: load failed — " << prefix << " '" << path << "': " << what);
  return false;
}

// Load from an already-opened stream with exception handling. Returns true on success.
static bool TryLoadFromOpenFile(std::ifstream& file, AppSettings& out, std::string_view path) {
  try {
    return LoadSettingsFromJsonStream(file, out);
  } catch (const json::parse_error& e) {
    return LogJsonSettingsError(path, "JSON parse error", e.what());
  } catch (const json::exception& e) {
    return LogJsonSettingsError(path, "JSON error", e.what());
  } catch (const std::ios_base::failure& e) {
    return LogJsonSettingsError(path, "I/O error reading", e.what());
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all after specific types
    return LogJsonSettingsError(path, "error parsing", e.what());
  }
}

// Try loading from legacy path when primary was chosen but file missing (migration). Returns true if loaded.
static bool TryLoadFromLegacyAndMigrateToPrimary(std::string_view path, AppSettings& out) {
  if (const std::string primary = GetPrimarySettingsPath(); primary.empty() || path != primary) {
    return false;
  }
  const std::string legacy = GetLegacySettingsFilePath();
  LOG_IMPORTANT_BUILD("Settings: primary file missing at '" << path
                      << "'; trying legacy migration from '" << legacy << "'");
  std::ifstream legacy_file(legacy, std::ios::in | std::ios::binary);
  if (!legacy_file.is_open()) {
    LOG_IMPORTANT_BUILD("Settings: legacy settings file also missing at '" << legacy << "'");
    return false;
  }
  try {
    LoadSettingsFromJsonStream(legacy_file, out);
    legacy_file.close();
    if (CanUsePrimaryPath(true)) {
      if (SaveSettings(out)) {
        LOG_IMPORTANT_BUILD("Settings: migrated from legacy '" << legacy << "' to '"
                            << GetPrimarySettingsPath() << "'");
      } else {
        LOG_ERROR("Settings: migration write to primary path failed; legacy file preserved");
        LOG_IMPORTANT_BUILD("Settings: migration write failed — primary '"
                            << GetPrimarySettingsPath() << "', legacy preserved at '" << legacy
                            << "'");
      }
    }
    return true;
  } catch (const json::parse_error& e) {
    return LogJsonSettingsError(legacy, "JSON parse error (legacy)", e.what());
  } catch (const json::exception& e) {
    return LogJsonSettingsError(legacy, "JSON error (legacy)", e.what());
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all after specific types
    return LogJsonSettingsError(legacy, "error parsing (legacy)", e.what());
  }
}

std::optional<AppSettings> LoadSettings() {
  // Test mode: Use in-memory settings if active
  if (test_settings::IsInMemoryMode()) {
    return test_settings::GetInMemorySettings();
  }

  const AppSettings defaults;
  AppSettings out = defaults;

  const std::string path = ResolveSettingsFilePath(false);
  std::ifstream file(path, std::ios::in | std::ios::binary);
  if (!file.is_open()) {
    if (TryLoadFromLegacyAndMigrateToPrimary(path, out)) {
      // After migration the live file is primary; summarize what is now in memory.
      LogSettingsLoadResultOnce(path, out);
      return out;
    }
    LOG_IMPORTANT_BUILD("Settings: file not found at '" << path
                        << "' — using defaults (theme=" << defaults.themeId << ", window="
                        << defaults.windowWidth << "x" << defaults.windowHeight << ")");
    return std::nullopt;
  }

  if (!TryLoadFromOpenFile(file, out, path)) {
    LOG_IMPORTANT_BUILD("Settings: parse failed for '" << path << "' — using defaults");
    return std::nullopt;
  }

  LogSettingsLoadResultOnce(path, out);
  return out;
}

bool LoadSettings(AppSettings &out) {
  if (const auto loaded = LoadSettings(); loaded.has_value()) {
    out = *loaded;
    return true;
  }
  out = AppSettings{};
  return false;
}

// Write JSON to the given path. Returns true on success.
static bool WriteSettingsToPath(std::string_view path, const json& j,
                                [[maybe_unused]] const AppSettings& settings) {
  std::ofstream file(std::string(path), std::ios::out | std::ios::trunc | std::ios::binary);
  if (!file.is_open()) {
    return false;
  }
  file << j.dump(2) << "\n";
  file.flush();
  const bool ok = file.good();
  file.close();
  if (ok) {
    LOG_INFO_BUILD("Saved settings to '" << path << "' (window="
                  << settings.windowWidth << "x" << settings.windowHeight << ")");
  }
  return ok;
}

// Write to path; if path was primary and write failed, retry legacy. Returns true if written.
static bool WriteSettingsWithLegacyFallback(std::string_view path, const json& j,
                                            const AppSettings& settings,
                                            std::string* out_written_settings_path) {
  if (WriteSettingsToPath(path, j, settings)) {
    if (out_written_settings_path != nullptr) {
      *out_written_settings_path = std::string(path);
    }
    return true;
  }
  if (const std::string primary = GetPrimarySettingsPath(); !primary.empty() && path == primary) {
    const std::string legacy = GetLegacySettingsFilePath();
    LOG_INFO_BUILD("Settings: save to primary failed, retrying legacy path: " << legacy);
    if (WriteSettingsToPath(legacy, j, settings)) {
      if (out_written_settings_path != nullptr) {
        *out_written_settings_path = legacy;
      }
      return true;
    }
  }
  return false;
}

static json SerializeHistoryParams(const SearchHistoryParams& params) {
  json j;
  j["filename"] = params.filename;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  j["path"] = params.path;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  j["extensions"] = params.extensions;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  j["itemType"] = params.item_type;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  j["foldersOnly"] = (params.item_type == 2 || params.folders_only);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  j["caseSensitive"] = params.case_sensitive;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  j["timeFilter"] = params.time_filter;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  j["sizeFilter"] = params.size_filter;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  if (!params.ai_search_description.empty()) {
    j["aiSearchDescription"] = params.ai_search_description;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  return j;
}

static json SerializeHistoryEntry(const SearchHistoryEntry& entry) {
  json j;
  j["id"] = entry.id;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  j["params"] = SerializeHistoryParams(entry.params);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  if (!entry.custom_name.empty()) {
    j["customName"] = entry.custom_name;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  j["isPinned"] = entry.is_pinned;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  j["createdAt"] = entry.created_at_unix_ms;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  j["lastUsedAt"] = entry.last_used_at_unix_ms;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  // Last-run result count: only written when known (>= 0). Absent key on load
  // means "never run / unknown" (-1), keeping old files backward-compatible.
  if (entry.last_run_result_count >= 0) {
    j["lastResultCount"] = entry.last_run_result_count;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  if (entry.is_pinned) {
    j["pinnedAt"] = entry.pinned_at_unix_ms;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  }
  return j;
}

static bool WriteHistoryToPath(std::string_view path, const AppSettings& settings) {
  json j;
  json arr = json::array();
  for (const auto& entry : settings.searchHistory) {
    arr.push_back(SerializeHistoryEntry(entry));
  }
  j["searchHistory"] = std::move(arr);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON access by known schema key
  std::ofstream file(std::string(path), std::ios::out | std::ios::trunc | std::ios::binary);
  if (!file.is_open()) {
    return false;
  }
  file << j.dump(2) << "\n";
  file.flush();
  const bool ok = file.good();
  file.close();
  if (ok) {
    LOG_INFO_BUILD("Saved search history to '" << path << "' ("
                  << settings.searchHistory.size() << " entries)");
  }
  return ok;
}

// Build json object from AppSettings for serialization.
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

static json SettingsToJson(const AppSettings& settings) {
  json j;
  j["fontFamily"] = settings.fontFamily;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON object access by known key
  j["fontSize"] = settings.fontSize;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON object access by known key
  j["fontScale"] = settings.fontScale;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON object access by known key
  j["themeId"] = settings.themeId;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON object access by known key
  j["windowWidth"] = settings.windowWidth;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON object access by known key
  j["windowHeight"] = settings.windowHeight;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON object access by known key
  j["searchThreadPoolSize"] = settings.searchThreadPoolSize;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON object access by known key
  j["dynamicChunkSize"] = settings.dynamicChunkSize;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON object access by known key
  j["monitoredVolume"] = settings.monitoredVolume;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON object access by known key
  j["hybridInitialWorkPercent"] = settings.hybridInitialWorkPercent;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON object access by known key
  j["guidedSchedulingDivisor"] = settings.guidedSchedulingDivisor;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON object access by known key
  j["showWorkflowHint"] = settings.showWorkflowHint;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON object access by known key
  j["showMetricsButton"] = settings.showMetricsButton;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON object access by known key
  j["uiMode"] = static_cast<uint8_t>(settings.uiMode);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON object access by known key
  j["showPathHierarchyIndentation"] = settings.showPathHierarchyIndentation;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON object access by known key
  j["hideSystemFiles"] = settings.hideSystemFiles;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON object access by known key
  j["crawlFolder"] = settings.crawlFolder;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON object access by known key
  j["recrawlIntervalMinutes"] = settings.recrawl.intervalMinutes;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON object access by known key
  j["aiProvider"] = {{"providerId", settings.aiProvider.providerId},  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - JSON object access by known key
                     {"baseUrl", settings.aiProvider.baseUrl},
                     {"model", settings.aiProvider.model},
                     {"apiKeyEnv", settings.aiProvider.apiKeyEnv},
                     {"timeoutSeconds", settings.aiProvider.timeoutSeconds},
                     {"presetId", settings.aiProvider.presetId},
                    };

  return j;
}

void SyncWindowSizeIntoSettings(AppSettings& settings, int width, int height) {
  if (width >= settings_defaults::kMinWindowWidth &&
      width <= settings_defaults::kMaxWindowWidth) {
    settings.windowWidth = width;
  }
  if (height >= settings_defaults::kMinWindowHeight &&
      height <= settings_defaults::kMaxWindowHeight) {
    settings.windowHeight = height;
  }
}

void ApplyWorkingSettingsPreservingRuntime(AppSettings& live, const AppSettings& working) {
  const int window_width = live.windowWidth;
  const int window_height = live.windowHeight;
  auto search_history = std::move(live.searchHistory);
  live = working;
  live.windowWidth = window_width;
  live.windowHeight = window_height;
  live.searchHistory = std::move(search_history);
}

bool SaveSettings(const AppSettings& settings) {
  if (test_settings::IsInMemoryMode()) {
    test_settings::SetInMemorySettings(settings);
    return true;
  }

  AppSettings to_write = settings;
  MergeHistoryFromDisk(to_write);

  LOG_INFO_BUILD("Saving settings ...'");
  const std::string path = ResolveSettingsFilePath(true);
  LOG_INFO_BUILD("Settings file path: " + path);

  try {
    std::string written_settings_path;
    if (const json j = SettingsToJson(to_write);
        WriteSettingsWithLegacyFallback(path, j, to_write, &written_settings_path)) {
      if (LooksLikeFactoryAppearanceDefaults(to_write)) {
        LOG_IMPORTANT_BUILD(
            "Settings: wrote factory-default-looking preferences to '"
            << written_settings_path << "' (theme=" << to_write.themeId << ", fontFamily="
            << (to_write.fontFamily.empty() ? "(default)" : to_write.fontFamily) << ", window="
            << to_write.windowWidth << "x" << to_write.windowHeight
            << ") — unexpected if you had customized appearance; check prior load/reset");
      }
      if (const std::string history_path = GetHistoryPathAdjacentToSettingsFile(written_settings_path);
          !WriteHistoryToPath(history_path, to_write)) {
        LOG_ERROR("Failed to write search history file: " + history_path);
      }
      // Limitation (deemed acceptable): returns true based solely on the main settings file
      // write succeeding. Failure in WriteHistoryToPath is logged as an error but does not
      // propagate to the caller, so the UI gives no feedback if history fails to persist.
      return true;
    }
    LOG_ERROR("Failed to open or write settings file: " + path);
    return false;
  } catch (const json::exception& e) {
    LOG_ERROR("JSON error saving settings: " + std::string(e.what()));
    return false;
  } catch (const std::ios_base::failure& e) {
    LOG_ERROR("I/O error saving settings file: " + std::string(e.what()));
    return false;
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all after specific types
    LOG_ERROR("Error saving settings file: " + std::string(e.what()));
    return false;
  }
}
