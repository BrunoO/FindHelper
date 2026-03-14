/**
 * @file Settings.cpp
 * @brief Implementation of application settings persistence and management
 *
 * This file implements functions for loading and saving application settings
 * to/from a JSON file. Settings include UI preferences, search configuration,
 * and thread pool sizing.
 *
 * STORAGE:
 * - Primary: {USER_HOME}/.FindHelper/settings.json
 *   (Windows: %USERPROFILE%\.FindHelper\settings.json; macOS/Linux: $HOME/.FindHelper/settings.json)
 * - Failsafe: if primary is unavailable (HOME unset, directory not writable, etc.),
 *   falls back to legacy: Windows = next to executable or CWD; macOS/Linux = CWD.
 * - Migration: on first run with new logic, if no file at primary but one at legacy,
 *   load from legacy and save to primary (legacy file is not deleted).
 *
 * SETTINGS INCLUDED:
 * - Window size and position
 * - Font family and size
 * - Search thread pool size
 * - Load balancing strategy
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

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#endif  // _WIN32

#include <filesystem>
#include <nlohmann/json.hpp>

#include "path/PathUtils.h"
#include "utils/Logger.h"

using json = nlohmann::json;  // NOLINT(readability-identifier-naming) - nlohmann convention; lowercase matches library style

// Validation bounds for settings (file-local where not in settings_defaults)
namespace {
constexpr float kMinFontSize = 4.0F;
constexpr float kMaxFontSize = 64.0F;
constexpr float kMinFontScale = 0.2F;
constexpr float kMaxFontScale = 4.0F;
constexpr int kMinDynamicChunkSize = 100;
constexpr int kMaxDynamicChunkSize = 100000;
constexpr int kMinHybridInitialWorkPercent = 50;
constexpr int kMaxHybridInitialWorkPercent = 95;

// Settings file location (DRY: single definition per spec)
constexpr std::string_view kSettingsSubfolder = ".FindHelper";
constexpr std::string_view kSettingsFileName = "settings.json";
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

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - nlohmann JSON operator[] on object keys; bounds are validated by j.contains() before each access

// Helper: Load and validate theme ID (allowlist of seven known IDs)
static void LoadThemeId(const json& j, AppSettings& out) {
  if (!j.contains("themeId") || !j["themeId"].is_string()) {
    return;
  }
  const std::string value = j["themeId"].get<std::string>();
  static const std::array<std::string_view, 7> kAllowedThemeIds = {
      "default_dark", "dracula", "nord", "one_dark", "gruvbox", "everforest", "catppuccin"};
  if (std::find(kAllowedThemeIds.begin(), kAllowedThemeIds.end(), value) !=  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
      kAllowedThemeIds.end()) {
    out.themeId = value;
  }
}

// Helper: Load basic UI settings (font, window size)
static void LoadBasicSettings(const json& j, AppSettings& out) {
  // Font family (string)
  if (j.contains("fontFamily") && j["fontFamily"].is_string()) {
    const auto family = j["fontFamily"].get<std::string>();
    if (!family.empty()) {
      out.fontFamily = family;
    }
  }

  // Font size (number)
  if (j.contains("fontSize") && j["fontSize"].is_number()) {
    const float size = j["fontSize"];
    if (size > kMinFontSize && size < kMaxFontSize) {
      out.fontSize = size;
    }
  }

  // Font scale (number)
  if (j.contains("fontScale") && j["fontScale"].is_number()) {
    const float scale = j["fontScale"];
    if (scale > kMinFontScale && scale < kMaxFontScale) {
      out.fontScale = scale;
    }
  }

  // Window size (optional, stored as integers)
  if (j.contains("windowWidth") && j["windowWidth"].is_number_integer()) {
    const int width = j["windowWidth"];
    if (width >= settings_defaults::kMinWindowWidth && width <= settings_defaults::kMaxWindowWidth) {
      out.windowWidth = width;
    }
  }

  if (j.contains("windowHeight") && j["windowHeight"].is_number_integer()) {
    const int height = j["windowHeight"];
    if (height >= settings_defaults::kMinWindowHeight && height <= settings_defaults::kMaxWindowHeight) {
      out.windowHeight = height;
    }
  }

  LoadThemeId(j, out);
}

// Helper: Load and validate load balancing strategy
static void LoadLoadBalancingStrategy(const json& j, AppSettings& out) {
  if (!j.contains("loadBalancingStrategy") || !j["loadBalancingStrategy"].is_string()) {
    return;
  }
  const auto strategy = j["loadBalancingStrategy"].get<std::string>();
  if (strategy == "static" || strategy == "hybrid" || strategy == "dynamic" || strategy == "interleaved") {
    out.loadBalancingStrategy = strategy;
  } else {
    LOG_WARNING_BUILD("Invalid loadBalancingStrategy value: " << strategy
        << ". Valid options: static, hybrid, dynamic, interleaved. Using default (hybrid).");
  }
}

// Helper: Load and validate thread pool size (0 = auto, 1-64 = explicit)
static void LoadThreadPoolSize(const json& j, AppSettings& out) {
  if (!j.contains("searchThreadPoolSize") || !j["searchThreadPoolSize"].is_number_integer()) {
    return;
  }
  const int pool_size = j["searchThreadPoolSize"];
  if (pool_size == 0 || (pool_size >= 1 && pool_size <= 64)) {
    out.searchThreadPoolSize = pool_size;
  } else {
    LOG_WARNING_BUILD("Invalid searchThreadPoolSize value: " << pool_size
        << ". Valid range: 0 (auto-detect) or 1-64. Using default (0 = auto-detect).");
  }
}

// Helper: Load and validate dynamic chunk size
static void LoadDynamicChunkSize(const json& j, AppSettings& out) {
  if (!j.contains("dynamicChunkSize") || !j["dynamicChunkSize"].is_number_integer()) {
    return;
  }
  const int chunk_size = j["dynamicChunkSize"];
  if (chunk_size >= kMinDynamicChunkSize && chunk_size <= kMaxDynamicChunkSize) {
    out.dynamicChunkSize = chunk_size;
  } else {
    LOG_WARNING_BUILD("Invalid dynamicChunkSize value: " << chunk_size
        << ". Valid range: 100-100000. Using default (1000).");
  }
}

// Helper: Load and validate hybrid initial work percent
static void LoadHybridInitialWorkPercent(const json& j, AppSettings& out) {
  if (!j.contains("hybridInitialWorkPercent") || !j["hybridInitialWorkPercent"].is_number_integer()) {
    return;
  }
  const int percent = j["hybridInitialWorkPercent"];
  if (percent >= kMinHybridInitialWorkPercent && percent <= kMaxHybridInitialWorkPercent) {
    out.hybridInitialWorkPercent = percent;
  } else {
    LOG_WARNING_BUILD("Invalid hybridInitialWorkPercent value: " << percent
        << ". Valid range: 50-95. Using default (75).");
  }
}

// Helper: Load search configuration settings
static void LoadSearchConfigSettings(const json& j, AppSettings& out) {
  LoadLoadBalancingStrategy(j, out);
  LoadThreadPoolSize(j, out);
  LoadDynamicChunkSize(j, out);
  LoadHybridInitialWorkPercent(j, out);
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
  if (!j.contains("monitoredVolume") || !j["monitoredVolume"].is_string()) {
    return;
  }
  SetMonitoredVolumeIfValid(j["monitoredVolume"].get<std::string>(), out);
}

// Helper: Load UI mode from JSON (handles legacy simplifiedUI)
static void LoadUIMode(const json& j, AppSettings& out) {
  if (j.contains("uiMode") && j["uiMode"].is_number_integer()) {
    if (const int mode = j["uiMode"]; mode >= 0 && mode <= 2) {
      out.uiMode = static_cast<AppSettings::UIMode>(mode);
    }
  }
  if (j.contains("simplifiedUI") && j["simplifiedUI"].is_boolean()) {
    out.uiMode = j["simplifiedUI"].get<bool>() ? AppSettings::UIMode::Simplified : AppSettings::UIMode::Full;
  }
}

// Helper: Load recrawl interval and idle threshold
static void LoadRecrawlTimingSettings(const json& j, AppSettings& out) {
  if (j.contains("recrawlIntervalMinutes") && j["recrawlIntervalMinutes"].is_number_integer()) {
    int interval = j["recrawlIntervalMinutes"].get<int>();
    interval = (std::max)(settings_defaults::kMinRecrawlIntervalMinutes,
                          (std::min)(interval, settings_defaults::kMaxRecrawlIntervalMinutes));
    out.recrawl.intervalMinutes = interval;
  }
  if (j.contains("recrawlIdleThresholdMinutes") && j["recrawlIdleThresholdMinutes"].is_number_integer()) {
    int threshold = j["recrawlIdleThresholdMinutes"].get<int>();
    threshold = (std::max)(settings_defaults::kMinRecrawlIdleThresholdMinutes,
                           (std::min)(threshold, settings_defaults::kMaxRecrawlIdleThresholdMinutes));
    out.recrawl.idleThresholdMinutes = threshold;
  }
}

// Helper: Load crawl / recrawl related settings and UI preferences
static void LoadCrawlAndRecrawlSettings(const json& j, AppSettings& out) {
  if (j.contains("showWorkflowHint") && j["showWorkflowHint"].is_boolean()) {
    out.showWorkflowHint = j["showWorkflowHint"];
  }

  LoadUIMode(j, out);

  if (j.contains("streamPartialResults") && j["streamPartialResults"].is_boolean()) {
    out.streamPartialResults = j["streamPartialResults"];
  }

  if (j.contains("showPathHierarchyIndentation") &&
      j["showPathHierarchyIndentation"].is_boolean()) {
    out.showPathHierarchyIndentation = j["showPathHierarchyIndentation"];
  }

  if (j.contains("crawlFolder") && j["crawlFolder"].is_string()) {
    out.crawlFolder = j["crawlFolder"].get<std::string>();
  }

  LoadRecrawlTimingSettings(j, out);

  if (j.contains("recrawlRequireIdle") && j["recrawlRequireIdle"].is_boolean()) {
    out.recrawl.requireIdle = j["recrawlRequireIdle"].get<bool>();
  }
}

// Helper: Load a single SavedSearch from JSON
static SavedSearch LoadSavedSearchFromJson(const json& item) {
  SavedSearch saved;

  // name
  if (item.contains("name") && item["name"].is_string()) {
    saved.name = item["name"];
  }

  // path
  if (item.contains("path") && item["path"].is_string()) {
    saved.path = item["path"];
  }

  // extensions
  if (item.contains("extensions") && item["extensions"].is_string()) {
    saved.extensions = item["extensions"];
  }

  // filename
  if (item.contains("filename") && item["filename"].is_string()) {
    saved.filename = item["filename"];
  }

  // foldersOnly
  if (item.contains("foldersOnly") && item["foldersOnly"].is_boolean()) {
    saved.foldersOnly = item["foldersOnly"];
  }

  // caseSensitive
  if (item.contains("caseSensitive") && item["caseSensitive"].is_boolean()) {
    saved.caseSensitive = item["caseSensitive"];
  }

  // timeFilter (string)
  if (item.contains("timeFilter") && item["timeFilter"].is_string()) {
    saved.timeFilter = item["timeFilter"];
  }

  // sizeFilter (string)
  if (item.contains("sizeFilter") && item["sizeFilter"].is_string()) {
    saved.sizeFilter = item["sizeFilter"];
  }

  // aiSearchDescription (string) - optional, for backward compatibility
  if (item.contains("aiSearchDescription") && item["aiSearchDescription"].is_string()) {
    saved.aiSearchDescription = item["aiSearchDescription"];
  }

  return saved;
}

// Helper: Load saved searches array
static void LoadSavedSearches(const json& j, AppSettings& out) {
  if (!j.contains("savedSearches") || !j["savedSearches"].is_array()) {
    return;
  }

  out.savedSearches.clear();
  for (const auto &item : j["savedSearches"]) {
    if (!item.is_object()) {
      continue;
    }

    SavedSearch saved = LoadSavedSearchFromJson(item);

    // Only add if it has at least a name (avoid empty objects)
    if (!saved.name.empty()) {
      out.savedSearches.push_back(std::move(saved));
    }
  }
}

// Helper: Load recent searches array
static void LoadRecentSearches(const json& j, AppSettings& out) {
  if (!j.contains("recentSearches") || !j["recentSearches"].is_array()) {
    return;
  }

  out.recentSearches.clear();
  for (const auto &item : j["recentSearches"]) {
    if (!item.is_object()) {
      continue;
    }

    SavedSearch recent = LoadSavedSearchFromJson(item);

    // Add recent search even if name is empty (unlike savedSearches)
    // Only require at least one search field to be non-empty
    if (!recent.filename.empty() || !recent.extensions.empty() ||
        !recent.path.empty() || recent.foldersOnly) {
      out.recentSearches.push_back(std::move(recent));
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

// Helper: primary path {HOME}/.FindHelper/settings.json. Returns empty if HOME not available.
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
  const std::filesystem::path primary_dir(GetPrimarySettingsDir());
  if (primary_dir.empty()) {
    LOG_INFO_BUILD("Settings: primary path skipped (HOME/USERPROFILE unset or default)");
    return false;
  }
  try {
    if (for_save) {
      std::filesystem::create_directories(primary_dir);
    }
    if (!std::filesystem::exists(primary_dir) || !std::filesystem::is_directory(primary_dir)) {
      if (for_save) {
        LOG_INFO_BUILD("Settings: primary path skipped (.FindHelper not a directory or not writable)");
      }
      return false;
    }
    return true;
  } catch (const std::filesystem::filesystem_error& e) {
    (void)e;
    LOG_INFO_BUILD("Settings: primary path skipped (" << e.what() << ")");
    return false;
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all after filesystem_error
    (void)e;
    LOG_INFO_BUILD("Settings: primary path skipped (" << e.what() << ")");
    return false;
  }
}

// Resolve effective settings file path: primary when usable, else legacy. Log fallback.
static std::string ResolveSettingsFilePath(bool for_save) {
  if (CanUsePrimaryPath(for_save)) {
    return GetPrimarySettingsPath();
  }
  const std::string legacy = GetLegacySettingsFilePath();
  LOG_INFO_BUILD("Settings: using legacy path (failsafe): " << legacy);
  return legacy;
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

  // Load saved and recent searches using helper functions
  LoadSavedSearches(j, out);
  LoadRecentSearches(j, out);

  return true;
}

// Log a JSON-related error from a settings operation and return false (for use in catch blocks).
static bool LogJsonSettingsError(std::string_view prefix, const char* what) {
  LOG_ERROR(std::string(prefix) + std::string(what));
  return false;
}

// Load from an already-opened stream with exception handling. Returns true on success.
static bool TryLoadFromOpenFile(std::ifstream& file, AppSettings& out) {
  try {
    return LoadSettingsFromJsonStream(file, out);
  } catch (const json::parse_error& e) {
    return LogJsonSettingsError("JSON parse error in settings file: ", e.what());
  } catch (const json::exception& e) {
    return LogJsonSettingsError("JSON error in settings file: ", e.what());
  } catch (const std::ios_base::failure& e) {
    return LogJsonSettingsError("I/O error reading settings file: ", e.what());
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all after specific types
    return LogJsonSettingsError("Error parsing settings file: ", e.what());
  }
}

// Try loading from legacy path when primary was chosen but file missing (migration). Returns true if loaded.
static bool TryLoadFromLegacyAndMigrateToPrimary(std::string_view path, AppSettings& out) {
  if (const std::string primary = GetPrimarySettingsPath(); primary.empty() || path != primary) {
    return false;
  }
  const std::string legacy = GetLegacySettingsFilePath();
  std::ifstream legacy_file(legacy, std::ios::in | std::ios::binary);
  if (!legacy_file.is_open()) {
    return false;
  }
  try {
    const bool ok = LoadSettingsFromJsonStream(legacy_file, out);
    legacy_file.close();
    if (ok && CanUsePrimaryPath(true)) {
      SaveSettings(out);
      LOG_INFO_BUILD("Settings: migrated from legacy path to " << GetPrimarySettingsPath());
    }
    return ok;
  } catch (const json::parse_error& e) {
    return LogJsonSettingsError("JSON parse error in settings file: ", e.what());
  } catch (const json::exception& e) {
    return LogJsonSettingsError("JSON error in settings file: ", e.what());
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all after specific types
    return LogJsonSettingsError("Error parsing settings file: ", e.what());
  }
}

bool LoadSettings(AppSettings &out) {
  // Test mode: Use in-memory settings if active
  if (test_settings::IsInMemoryMode()) {
    out = test_settings::GetInMemorySettings();
    return true;
  }

  const AppSettings defaults;
  out = defaults;

  const std::string path = ResolveSettingsFilePath(false);
  std::ifstream file(path, std::ios::in | std::ios::binary);
  if (!file.is_open()) {
    if (TryLoadFromLegacyAndMigrateToPrimary(path, out)) {
      return true;
    }
    LOG_INFO("Settings file not found at: " + path + " (using defaults)");
    return false;
  }

  return TryLoadFromOpenFile(file, out);
}

// Write JSON to the given path. Returns true on success.
static bool WriteSettingsToPath(std::string_view path, const json& j,
                                const AppSettings& settings) {
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
                                            const AppSettings& settings) {
  if (WriteSettingsToPath(path, j, settings)) {
    return true;
  }
  if (const std::string primary = GetPrimarySettingsPath(); !primary.empty() && path == primary) {
    const std::string legacy = GetLegacySettingsFilePath();
    LOG_INFO_BUILD("Settings: save to primary failed, retrying legacy path: " << legacy);
    if (WriteSettingsToPath(legacy, j, settings)) {
      return true;
    }
  }
  return false;
}

// Serialize a single SavedSearch to a JSON object.
static json SerializeSavedSearch(const SavedSearch& s) {
  json search_obj;
  search_obj["name"] = s.name;
  search_obj["path"] = s.path;
  search_obj["extensions"] = s.extensions;
  search_obj["filename"] = s.filename;
  search_obj["foldersOnly"] = s.foldersOnly;
  search_obj["caseSensitive"] = s.caseSensitive;
  search_obj["timeFilter"] = s.timeFilter;
  search_obj["sizeFilter"] = s.sizeFilter;
  if (!s.aiSearchDescription.empty()) {
    search_obj["aiSearchDescription"] = s.aiSearchDescription;
  }
  return search_obj;
}

// Build json object from AppSettings for serialization.
static json SettingsToJson(const AppSettings& settings) {
  json j;
  j["fontFamily"] = settings.fontFamily;
  j["fontSize"] = settings.fontSize;
  j["fontScale"] = settings.fontScale;
  j["themeId"] = settings.themeId;
  j["windowWidth"] = settings.windowWidth;
  j["windowHeight"] = settings.windowHeight;
  j["loadBalancingStrategy"] = settings.loadBalancingStrategy;
  j["searchThreadPoolSize"] = settings.searchThreadPoolSize;
  j["dynamicChunkSize"] = settings.dynamicChunkSize;
  j["monitoredVolume"] = settings.monitoredVolume;
  j["hybridInitialWorkPercent"] = settings.hybridInitialWorkPercent;
  j["showWorkflowHint"] = settings.showWorkflowHint;
  j["uiMode"] = static_cast<uint8_t>(settings.uiMode);
  j["streamPartialResults"] = settings.streamPartialResults;
  j["showPathHierarchyIndentation"] = settings.showPathHierarchyIndentation;
  j["crawlFolder"] = settings.crawlFolder;
  j["recrawlIntervalMinutes"] = settings.recrawl.intervalMinutes;
  j["recrawlRequireIdle"] = settings.recrawl.requireIdle;
  j["recrawlIdleThresholdMinutes"] = settings.recrawl.idleThresholdMinutes;

  if (!settings.savedSearches.empty()) {
    json searches_array = json::array();
    for (const auto& s : settings.savedSearches) {
      searches_array.push_back(SerializeSavedSearch(s));
    }
    j["savedSearches"] = searches_array;
  }

  if (!settings.recentSearches.empty()) {
    json recent_array = json::array();
    for (const auto& s : settings.recentSearches) {
      recent_array.push_back(SerializeSavedSearch(s));
    }
    j["recentSearches"] = recent_array;
  }

  return j;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

bool SaveSettings(const AppSettings& settings) {
  if (test_settings::IsInMemoryMode()) {
    test_settings::SetInMemorySettings(settings);
    return true;
  }

  LOG_INFO_BUILD("Saving settings ...'");
  const std::string path = ResolveSettingsFilePath(true);
  LOG_INFO_BUILD("Settings file path: " + path);
  LOG_INFO_BUILD("Number of saved searches to save: " + std::to_string(settings.savedSearches.size()));

  try {
    if (const json j = SettingsToJson(settings); WriteSettingsWithLegacyFallback(path, j, settings)) {
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
