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
inline constexpr int kDefaultHybridInitialWorkPercent = 75;
inline constexpr int kDefaultRecrawlIdleThresholdMinutes = 5;
inline constexpr int kMinRecrawlIntervalMinutes = 1;
inline constexpr int kMaxRecrawlIntervalMinutes = 60;
inline constexpr int kMinRecrawlIdleThresholdMinutes = 1;
inline constexpr int kMaxRecrawlIdleThresholdMinutes = 30;
/** Maximum number of recent searches to store and display. */
inline constexpr size_t kMaxRecentSearches = 50;
/** Thread pool size: 0 = auto-detect, range for explicit count. */
inline constexpr int kMinThreadPoolSize = 0;
inline constexpr int kMaxThreadPoolSize = 64;
}  // namespace settings_defaults

// Saved search preset (subset of GuiState fields).
struct SavedSearch {
  std::string name{};   // NOLINT(readability-identifier-naming,readability-redundant-member-init) - struct; camelCase for JSON API; {} satisfies member-init
  std::string path{};   // NOLINT(readability-identifier-naming,readability-redundant-member-init) - struct; camelCase for JSON API
  std::string extensions{};   // NOLINT(readability-identifier-naming,readability-redundant-member-init) - struct; camelCase for JSON API
  std::string filename{};   // NOLINT(readability-identifier-naming,readability-redundant-member-init) - struct; camelCase for JSON API
  bool foldersOnly = false;   // NOLINT(readability-identifier-naming) - struct; camelCase for JSON API
  bool caseSensitive = false;   // NOLINT(readability-identifier-naming) - struct; camelCase for JSON API
  std::string timeFilter{};   // NOLINT(readability-identifier-naming,readability-redundant-member-init) - struct; camelCase for JSON API
  std::string sizeFilter{};   // NOLINT(readability-identifier-naming,readability-redundant-member-init) - struct; camelCase for JSON API
  std::string aiSearchDescription{};   // NOLINT(readability-identifier-naming,readability-redundant-member-init) - struct; camelCase for JSON API
};

// Recrawl timing settings (grouped to keep AppSettings under 20 fields for S1820).
struct RecrawlSettings {
  int intervalMinutes = 3;  // NOLINT(readability-identifier-naming) - camelCase for JSON API
  bool requireIdle = true;  // NOLINT(readability-identifier-naming) - camelCase for JSON API
  int idleThresholdMinutes =  // NOLINT(readability-identifier-naming) - camelCase for JSON API
    settings_defaults::kDefaultRecrawlIdleThresholdMinutes;
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

  // Preferred main window size. Used on startup and updated on shutdown.
  int windowWidth = settings_defaults::kDefaultWindowWidth;  // NOLINT(readability-identifier-naming) - camelCase for JSON API
  int windowHeight = settings_defaults::kDefaultWindowHeight;  // NOLINT(readability-identifier-naming) - camelCase for JSON API

  // Saved searches (optional; may be empty).
  std::vector<SavedSearch> savedSearches{};  // NOLINT(readability-identifier-naming,readability-redundant-member-init) - camelCase for JSON/settings API; {} satisfies member-init

  // Recent searches (automatically tracked, up to kMaxRecentSearches, names optional).
  // Unlike savedSearches, these are automatically recorded and don't require names.
  std::vector<SavedSearch> recentSearches{};  // NOLINT(readability-identifier-naming,readability-redundant-member-init) - camelCase for JSON/settings API

  // Load balancing strategy for parallel search
  // Options: "static", "hybrid", "dynamic", "interleaved"
  // - "static": Fixed chunks assigned upfront (original)
  // - "hybrid": Initial large chunks + dynamic small chunks (recommended)
  // - "dynamic": Small chunks assigned dynamically (no initial chunks)
  // - "interleaved": Threads process items in an interleaved manner
#if defined(FAST_LIBS_BOOST)
  // - "work_stealing": Per-thread queues with stealing (requires FAST_LIBS_BOOST)
#endif  // FAST_LIBS_BOOST
  std::string loadBalancingStrategy =  // NOLINT(readability-identifier-naming) - camelCase for JSON/settings API
    "hybrid";

  // Thread pool size for parallel search operations
  // 0 = auto-detect (use hardware concurrency)
  // 1-64 = explicit thread count
  // Allows users to tune performance for their hardware
  int searchThreadPoolSize =  // NOLINT(readability-identifier-naming) - camelCase for JSON/settings API
    0;

  // Dynamic chunk size for Hybrid and Dynamic load balancing strategies
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
  // Recommended: 70-80 for balanced approach
  int hybridInitialWorkPercent =  // NOLINT(readability-identifier-naming) - camelCase for JSON/settings API
    settings_defaults::kDefaultHybridInitialWorkPercent;

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

  // Enable streaming search results (show results incrementally)
  // Default: true (for better perceived performance)
  bool streamPartialResults =  // NOLINT(readability-identifier-naming) - camelCase for JSON/settings API
    true;

  // Indent results table filenames by folder depth (path hierarchy visualization)
  // Default: false (flat results on first run; users can enable hierarchy)
  bool showPathHierarchyIndentation =  // NOLINT(readability-identifier-naming) - camelCase for JSON/settings API
    false;

  // Folder to crawl and index (alternative to USN Journal)
  // Empty = use command-line argument or default behavior
  // Format: Platform-specific path (e.g., "C:\\Users\\Documents" or "/home/user/documents")
  // Note: Command-line --crawl-folder takes precedence over this setting
  // On Windows: Only used when no admin rights or USN Journal is not active
  // On macOS/Linux: Used when no index file is provided
  std::string crawlFolder{};  // NOLINT(readability-identifier-naming,readability-redundant-member-init) - camelCase for JSON/settings API; {} satisfies member-init

  // Periodic recrawl settings (interval, idle requirement, idle threshold)
  RecrawlSettings recrawl{};
};

// Load settings from a JSON file located next to the executable.
// On any error (file missing, malformed JSON, etc.), this function
// leaves `out` at its defaults and returns false.
bool LoadSettings(AppSettings& out);

// Save settings to the JSON file located next to the executable.
// Returns true on success, false on failure.
bool SaveSettings(const AppSettings& settings);

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
