#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Default values for AppSettings (named constants to avoid magic numbers).
// Named namespace (not anonymous) to satisfy cert-dcl59-cpp / google-build-namespaces in headers.
namespace settings_defaults {
inline constexpr float kDefaultFontSize = 14.0F;
inline constexpr int kDefaultWindowWidth = 1280;
inline constexpr int kDefaultWindowHeight = 800;
inline constexpr int kMinWindowWidth = 640;
inline constexpr int kMaxWindowWidth = 4096;
inline constexpr int kMinWindowHeight = 480;
inline constexpr int kMaxWindowHeight = 2160;
inline constexpr int kDefaultDynamicChunkSize = 1000;
inline constexpr int kDefaultHybridInitialWorkPercent = 80;
inline constexpr int kDefaultGuidedSchedulingDivisor = 2;
inline constexpr int kMinGuidedSchedulingDivisor = 1;
inline constexpr int kMaxGuidedSchedulingDivisor = 8;
inline constexpr int kMinRecrawlIntervalMinutes = 1;
inline constexpr int kMaxRecrawlIntervalMinutes = 60;
/** Maximum number of recent searches to store and display. */
inline constexpr size_t kMaxRecentSearches = 50;
/** Thread pool size: 0 = auto-detect, range for explicit count. */
inline constexpr int kMinThreadPoolSize = 0;
inline constexpr int kMaxThreadPoolSize = 64;
}  // namespace settings_defaults

// Recrawl timing settings (grouped to keep AppSettings under 20 fields for S1820).
struct RecrawlSettings {
  int intervalMinutes = 3;  // NOLINT(readability-identifier-naming) - camelCase for JSON API
};

// Persisted search parameter payload for unified search history.
// Distinct from the runtime SearchParams in search/SearchTypes.h (which is the
// search-engine operational type and lacks time/size/AI fields).
struct SearchHistoryParams {
  std::string filename;  // NOLINT(readability-identifier-naming) - POD history payload; snake_case without trailing _
  std::string path;  // NOLINT(readability-identifier-naming) - POD history payload; snake_case without trailing _
  std::string extensions;  // NOLINT(readability-identifier-naming) - POD history payload; snake_case without trailing _
  bool folders_only = false;  // NOLINT(readability-identifier-naming) - POD history payload; snake_case without trailing _
  bool case_sensitive = false;  // NOLINT(readability-identifier-naming) - POD history payload; snake_case without trailing _
  int time_filter = 0;  // NOLINT(readability-identifier-naming) - POD history payload; snake_case without trailing _
  std::uint64_t size_filter = 0;  // NOLINT(readability-identifier-naming) - POD history payload; snake_case without trailing _
  std::string ai_search_description;  // NOLINT(readability-identifier-naming) - POD history payload; snake_case without trailing _
};

// A single entry in the unified search history (recent or pinned).
struct SearchHistoryEntry {
  std::string id;  // NOLINT(readability-identifier-naming) - POD history entry; snake_case without trailing _
  SearchHistoryParams params;  // NOLINT(readability-identifier-naming) - POD history entry; snake_case without trailing _
  std::string custom_name;  // NOLINT(readability-identifier-naming) - POD history entry; snake_case without trailing _
  bool is_pinned = false;  // NOLINT(readability-identifier-naming) - POD history entry; snake_case without trailing _
  std::int64_t created_at_unix_ms = 0;  // NOLINT(readability-identifier-naming) - POD history entry; snake_case without trailing _
  std::int64_t last_used_at_unix_ms = 0;  // NOLINT(readability-identifier-naming) - POD history entry; snake_case without trailing _
  std::int64_t pinned_at_unix_ms = 0;  // NOLINT(readability-identifier-naming) - POD history entry; snake_case without trailing _
};

// Simple application-wide settings structure.
// Currently focused on appearance/font parameters, but designed to be
// extended later with more settings (themes, layout, etc.).
struct AppSettings {
  // Logical font family name (not a path). We map these to concrete
  // Windows font files when initializing ImGui.
  std::string fontFamily =  // NOLINT(readability-identifier-naming,readability-redundant-string-init) - camelCase for JSON/settings API; "" satisfies member-init
    "";

  // Base font size in pixels used when loading the ImGui font.
  float fontSize = settings_defaults::kDefaultFontSize;  // NOLINT(readability-identifier-naming) - camelCase for JSON API

  // Global UI scale factor applied via ImGuiIO::FontGlobalScale.
  float fontScale =  // NOLINT(readability-identifier-naming) - camelCase for JSON/settings API
    1.0F;

  // UI theme ID. One of: "default_dark", "dracula", "nord", "one_dark", "gruvbox", "everforest".
  std::string themeId =  // NOLINT(readability-identifier-naming) - camelCase for JSON/settings API
    "default_dark";

  // Preferred main window size. Used on startup; kept current via PersistSettings.
  int windowWidth = settings_defaults::kDefaultWindowWidth;  // NOLINT(readability-identifier-naming) - camelCase for JSON API
  int windowHeight = settings_defaults::kDefaultWindowHeight;  // NOLINT(readability-identifier-naming) - camelCase for JSON API

  // Thread pool size for parallel search operations
  // 0 = auto-detect (use hardware concurrency)
  // 1-64 = explicit thread count
  // Allows users to tune performance for their hardware
  int searchThreadPoolSize =  // NOLINT(readability-identifier-naming) - camelCase for JSON/settings API
    0;

  // Dynamic chunk size for the hybrid schedule's second phase
  // Used for small chunks assigned dynamically to balance load
  // Range: 100-100000 items per chunk (100K max for testing)
  // Larger chunks = less overhead but less fine-grained balancing
  // Smaller chunks = more overhead but better load balancing
  // Recommended: 1000-10000 for most cases, up to 100000 for large datasets
  int dynamicChunkSize =  // NOLINT(readability-identifier-naming) - camelCase for JSON/settings API
    settings_defaults::kDefaultDynamicChunkSize;

  // Initial work percentage for Hybrid strategy
  // Percentage of total work assigned as initial chunks (rest is for dynamic balancing)
  // Range: 50-95 (50% = more dynamic work, 95% = mostly initial chunks)
  // Lower values = more dynamic chunks = better load balancing but less cache locality
  // Higher values = more initial chunks = better cache locality but less load balancing
  // Recommended: 75-85 for balanced approach
  int hybridInitialWorkPercent =  // NOLINT(readability-identifier-naming) - camelCase for JSON/settings API
    settings_defaults::kDefaultHybridInitialWorkPercent;

  // Guided scheduling divisor multiplier (hybrid schedule dynamic phase)
  // Chunk size formula: remaining / (guidedSchedulingDivisor * thread_count)
  // Range: 1-8 (smaller = larger initial dynamic chunks, fewer atomics; larger = smaller chunks, better balance)
  // Recommended: 2 (default, standard OpenMP guided formula)
  int guidedSchedulingDivisor =  // NOLINT(readability-identifier-naming) - camelCase for JSON/settings API
    settings_defaults::kDefaultGuidedSchedulingDivisor;

  // Volume to monitor for USN Journal (Windows only, ignored on other platforms)
  // Format: "C:", "D:", etc. (without backslashes or "\\\\.\\" prefix)
  // The "\\\\.\\" prefix is added automatically by UsnMonitor on Windows
  // Default: "C:" (system drive)
  // Note: This setting is only used on Windows. On macOS/Linux, it is stored
  // in the JSON file but ignored during application execution.
  std::string monitoredVolume =  // NOLINT(readability-identifier-naming) - camelCase for JSON/settings API
    "C:";

  // UI preferences
  // Whether to show the workflow hint at the top of AI search section
  // Default: true (show hint for new users)
  // Set to false when user clicks "Don't show again"
  bool showWorkflowHint =  // NOLINT(readability-identifier-naming) - camelCase for JSON/settings API
    true;

  // UI Mode (defines the complexity level of the interface)
  enum class UIMode : std::uint8_t {
    Full = 0,         // Show all controls and panels
    Simplified = 1,   // Hide advanced search options (Extensions, Filename, Search Options)
    Minimalistic = 2  // Show only essential path search input
  };

  // Current UI mode
  // Simplified and Minimalistic modes automatically enable "Search as you type"
  // and "Auto-refresh" for a streamlined experience.
  // Default: Full (0)
  UIMode uiMode =  // NOLINT(readability-identifier-naming) - camelCase for JSON/settings API
    UIMode::Full;

  // Indent results table filenames by folder depth (path hierarchy visualization)
  // Default: false (flat results on first run; users can enable hierarchy)
  bool showPathHierarchyIndentation =  // NOLINT(readability-identifier-naming) - camelCase for JSON/settings API
    false;

  // Whether to show the Metrics button in the main UI toolbar.
  // Default: false (hide Metrics UI for regular users; can be enabled from Settings for diagnostics).
  bool showMetricsButton =  // NOLINT(readability-identifier-naming) - camelCase for JSON/settings API
    false;

  // Folder to crawl and index (alternative to USN Journal)
  // Empty = use command-line argument or default behavior
  // Format: Platform-specific path (e.g., "C:\\Users\\Documents" or "/home/user/documents")
  // Note: Command-line --crawl-folder takes precedence over this setting
  // On Windows: Only used when no admin rights or USN Journal is not active
  // On macOS/Linux: Used when no index file is provided
  std::string crawlFolder{};  // NOLINT(readability-identifier-naming,readability-redundant-member-init) - camelCase for JSON/settings API; {} satisfies member-init

  // Periodic recrawl settings (interval, idle requirement, idle threshold)
  RecrawlSettings recrawl{};  // NOLINT(readability-identifier-naming,readability-redundant-member-init) - camelCase for JSON/settings API; {} satisfies member-init

  // Unified search history (recent + pinned entries).
  // Persisted to search_history.json alongside settings.json.
  // searchHistory is absent from settings.json itself; it lives in its own file.
  std::vector<SearchHistoryEntry> searchHistory{};  // NOLINT(readability-identifier-naming,readability-redundant-member-init) - camelCase for settings API; {} satisfies member-init
};

// Load settings from a JSON file located next to the executable.
// On any error (file missing, malformed JSON, etc.), this function
// leaves `out` at its defaults and returns false.
bool LoadSettings(AppSettings& out);

// Save settings to the JSON file located next to the executable.
// Returns true on success, false on failure.
// Prefer Application::PersistSettings / UIActions::PersistSettings in production so
// runtime geometry is synced before write; this is the low-level I/O primitive.
[[nodiscard]] bool SaveSettings(const AppSettings& settings);

// Copy validated window dimensions into settings (used before mid-session Persist).
void SyncWindowSizeIntoSettings(AppSettings& settings, int width, int height);

// Apply Settings-window working copy onto live settings without clobbering runtime
// fields the Settings UI does not edit (window size, search history).
void ApplyWorkingSettingsPreservingRuntime(AppSettings& live, const AppSettings& working);

// Test-only: In-memory settings for testing (bypasses file I/O)
namespace test_settings {
// Set in-memory settings (for testing)
void SetInMemorySettings(const AppSettings& settings);

// Get current in-memory settings (for testing)
AppSettings GetInMemorySettings();

// Clear in-memory settings (restore file-based behavior)
void ClearInMemorySettings();

// Check if in-memory mode is active
bool IsInMemoryMode();
}  // namespace test_settings
