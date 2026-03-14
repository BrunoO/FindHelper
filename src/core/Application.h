#pragma once

/**
 * @file Application.h
 * @brief Central Application class that owns and manages all application components
 *
 * This class serves as the core of the application, encapsulating the main loop
 * and taking ownership of all major components. It eliminates global state by
 * using dependency injection to pass dependencies to components that need them.
 *
 * DESIGN:
 * - Owns: FileIndex, ThreadPool, UsnMonitor, SearchWorker, GuiState, AppSettings, SearchController
 * - Coordinates: ApplicationLogic, UIRenderer (these take dependencies as parameters)
 * - Platform-agnostic: Core logic is shared between Windows and macOS
 * - Lifecycle: Constructor initializes, Run() executes main loop, Destructor cleans up
 */

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// Full definitions needed for value members and unique_ptr members
// Note: unique_ptr<T> needs full definition of T for destructor
#include "core/CommandLineArgs.h"
#include "core/IndexBuildState.h"
#include "gui/GuiState.h"
#include "gui/UIActions.h"
#include "search/SearchController.h"
#include "search/SearchWorker.h"
#include "usn/UsnMonitor.h"
#include "utils/ThreadPool.h"
#ifdef ENABLE_IMGUI_TEST_ENGINE
// Single include for test engine hook (avoids ODR; no duplicate include in other headers).
#include "ui/ImGuiTestEngineRegressionHook.h"
#endif  // ENABLE_IMGUI_TEST_ENGINE

// Forward declarations
struct AppBootstrapResultBase;
class FileIndex;
struct AppSettings;
class RendererInterface;
struct GLFWwindow;
class IIndexBuilder;
struct SavedSearch;
#ifdef ENABLE_IMGUI_TEST_ENGINE
struct ImGuiTestEngine;  // Pointer member only; full type in .cpp when ENABLE_IMGUI_TEST_ENGINE.
#endif  // ENABLE_IMGUI_TEST_ENGINE

// NativeWindowHandle forward declaration (platform-specific)
// Full definition available via PlatformTypes.h in .cpp files
// On Windows: HWND (opaque pointer)
// On non-Windows: void* (always nullptr, marked with NOSONAR in PlatformTypes.h)
#ifdef _WIN32
struct HWND__;
using NativeWindowHandle = HWND__*; // HWND on Windows (opaque pointer)
#else
using NativeWindowHandle = void*; // NOSONAR(cpp:S5008) - Cross-platform abstraction: always nullptr on non-Windows, type doesn't matter
#endif  // _WIN32

/**
 * @class Application
 * @brief Central application class that owns all components and manages the main loop
 */
class Application : public ui::UIActions {  // NOSONAR(cpp:S1820) - Application class has 23 fields: Central application class aggregates all components (monitors, workers, controllers, state, settings, windows). Splitting would require passing multiple objects, reducing encapsulation and increasing complexity
public:
  /**
   * @brief Construct Application from bootstrap result
   *
   * Takes ownership of components from AppBootstrapResultBase and initializes
   * application-specific components (SearchWorker, GuiState, SearchController).
   *
   * Unified constructor for all platforms - uses base class to eliminate duplication.
   *
   * @param bootstrap Bootstrap result containing initialized platform resources
   * @param cmd_args Command line arguments (for index dumping, etc.)
   * @param index_build_state Shared index build state for progress reporting
   * @param index_builder Index builder instance (may be nullptr, takes ownership)
   * @param should_auto_crawl If true, automatically start crawling after UI is shown
   * @param auto_crawl_folder Folder path to crawl when auto-crawl is enabled
   * @param start_index_build_after_first_frame If true, start index builder after first frame is rendered (ensures UI visible before crawl)
   */
  Application(AppBootstrapResultBase& bootstrap,
              const CommandLineArgs& cmd_args,
              IndexBuildState& index_build_state,
              std::unique_ptr<IIndexBuilder> index_builder,
              bool should_auto_crawl = false,
              std::string_view auto_crawl_folder = "",
              bool start_index_build_after_first_frame = false);

  /**
   * @brief Destructor - cleans up owned resources
   *
   * All resources are cleaned up via unique_ptr destructors or AppBootstrap:
   * - ThreadPool, UsnMonitor, and IIndexBuilder are cleaned up automatically via unique_ptr
   * - Renderer is cleaned up by AppBootstrap
   */
  ~Application() override;

  // Delete copy constructor and assignment (Application is not copyable)
  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  // Delete move constructor and assignment (Application is not movable - owns resources for lifetime)
  Application(Application&&) = delete;
  Application& operator=(Application&&) = delete;

  /**
   * @brief Run the main application loop
   *
   * This method contains the main loop logic that was previously in main().
   * It handles:
   * - Frame rendering (ImGui frame setup)
   * - Index dumping (if requested)
   * - Application logic updates (search controller, maintenance, keyboard shortcuts)
   * - UI rendering
   * - Platform-specific rendering (DirectX/Metal)
   * - Settings persistence on shutdown
   *
   * @return Exit code (0 for success, non-zero for error)
   */
  int Run();


  /**
   * @brief Get the native window handle (for platform-specific dialogs)
   *
   * @return Native window handle (HWND on Windows, nullptr on other platforms)
   */
  [[nodiscard]] NativeWindowHandle GetNativeWindowHandle() const;

  /**
   * @brief Get the time when last crawl completed
   *
   * @return Time point of last crawl completion
   */
  [[nodiscard]] std::chrono::steady_clock::time_point GetLastCrawlCompletionTime() const;

  /**
   * @brief Check if periodic recrawl is enabled
   *
   * @return true if periodic recrawl is enabled, false otherwise
   */
  [[nodiscard]] bool IsRecrawlEnabled() const;

  /**
   * @brief Trigger periodic recrawl if conditions are met
   *
   * This method is called by ApplicationLogic::CheckPeriodicRecrawl() when
   * all conditions for periodic recrawl are satisfied. It reuses the existing
   * StartIndexBuilding() infrastructure to start a new crawl.
   */
  void TriggerRecrawl();

  /**
   * @brief Check if crawl just completed and clear the flag
   *
   * @return true if crawl just completed (flag is then cleared), false otherwise
   */
  [[nodiscard]] bool CheckAndClearCrawlCompletion();

  // UIActions interface implementation
  /**
   * @brief Trigger a manual search with current state
   * @param state Current GUI state (read-only)
   */
  void TriggerManualSearch(const GuiState& state) override;

  /**
   * @brief Apply a saved search configuration to state
   * @param state GUI state to modify
   * @param saved_search Saved search configuration to apply
   */
  void ApplySavedSearch(GuiState& state, const SavedSearch& saved_search) override;

#ifdef ENABLE_IMGUI_TEST_ENGINE
  /** @brief For regression test hook: true when index is ready for search. */
  [[nodiscard]] bool IsIndexReadyForRegressionTest() const;
  /** @brief For regression test hook: index size (for dataset verification). */
  [[nodiscard]] size_t GetIndexSizeForRegressionTest() const;
  /** @brief For regression test hook: set search params then trigger manual search. */
  void SetSearchParamsForRegressionTest(std::string_view filename,
                                        std::string_view path,
                                        std::string_view extensions_semicolon,
                                        bool folders_only);
  /** @brief For regression test hook: current search params as views into state_ buffers (no copy; lifetime: *this). */
  [[nodiscard]] std::string_view GetSearchParamFilenameForRegressionTest() const noexcept;
  [[nodiscard]] std::string_view GetSearchParamPathForRegressionTest() const noexcept;
  [[nodiscard]] std::string_view GetSearchParamExtensionsForRegressionTest() const noexcept;
  [[nodiscard]] bool GetSearchParamFoldersOnlyForRegressionTest() const noexcept;
  /**
   * @brief For regression test hook: set load balancing strategy for the next search.
   * Accepted values: "static", "hybrid", "dynamic" (lowercase). Invalid names are normalized to "hybrid" and logged.
   */
  void SetLoadBalancingStrategyForRegressionTest(std::string_view strategy);
  /** @brief For regression test hook: current strategy (empty if settings_ is null). Enables tests to verify strategy was applied. */
  [[nodiscard]] std::string GetLoadBalancingStrategyForRegressionTest() const;
  /** @brief For regression test hook: set stream partial results for the next search (true = stream, false = all at once). */
  void SetStreamPartialResultsForRegressionTest(bool stream);
  /** @brief For regression test hook: std::nullopt = setting unavailable (e.g. settings_ null), false/true = explicitly off/on. Enables tests to assert availability vs value. */
  [[nodiscard]] std::optional<bool> GetStreamPartialResultsForRegressionTest() const noexcept;
  /** @brief For regression test hook: trigger manual search with current state_. */
  void TriggerManualSearchForRegressionTest();
  /** @brief For regression test hook: true when last search completed. */
  [[nodiscard]] bool IsSearchCompleteForRegressionTest() const;
  /** @brief For regression test hook: result count from last search. */
  [[nodiscard]] size_t GetSearchResultCountForRegressionTest() const;
  /** @brief For regression test hook: clipboard text from app's GLFW window (same as copy path uses). */
  [[nodiscard]] std::string GetClipboardTextForRegressionTest() const;
#endif  // ENABLE_IMGUI_TEST_ENGINE

  /**
   * @brief Check if index building is currently in progress
   *
   * @return true if index building is active, false otherwise
   */
  [[nodiscard]] bool IsIndexBuilding() const override;

  /**
   * @brief Get indexed file count
   * @return Number of files in index
   */
  [[nodiscard]] size_t GetIndexedFileCount() const override;

  /**
   * @brief Start indexing a folder (runtime index building)
   * @param folder_path Path to folder to crawl and index
   * @return true if indexing started successfully, false otherwise
   */
  [[nodiscard]] bool StartIndexBuilding(std::string_view folder_path) override;

  /**
   * @brief Stop current index building
   *
   * Requests cancellation and waits for background threads to complete.
   */
  void StopIndexBuilding() override;

  /**
   * @brief Export currently displayed search results to CSV
   * @param state GUI state containing search results
   */
  void ExportToCsv(GuiState& state) override;

private:
  // Core components (owned by Application). NOLINT: project convention snake_case with trailing underscore (AGENTS.md).
  // NOLINTBEGIN(readability-identifier-naming)
  FileIndex* file_index_;              // Owned by AppBootstrap (pointer to external)
  std::unique_ptr<ThreadPool> thread_pool_{nullptr}; // Owned by Application (initialized in constructor body)
  std::unique_ptr<UsnMonitor> monitor_{nullptr}; // Owned by Application (from bootstrap, moved in constructor)
  std::unique_ptr<IIndexBuilder> index_builder_{nullptr}; // Owned by Application (runtime index building, moved in constructor)
  SearchWorker search_worker_;          // Owned by Application
  GuiState state_;                      // Owned by Application
  SearchController search_controller_;  // Owned by Application
  AppSettings* settings_;               // Owned by AppBootstrap (pointer to external)

  // Platform resources (managed by AppBootstrap, but we need access)
  GLFWwindow* window_;                 // Managed by AppBootstrap
  RendererInterface* renderer_;        // Unified renderer interface (platform-specific implementation)
  int* last_window_width_;              // Pointer to window width (updated on resize)
  int* last_window_height_;            // Pointer to window height (updated on resize)

  /// Optional platform exit hook (e.g. Windows PGO flush). Set from bootstrap; empty on non-Windows.
  std::function<void(const char*)> platform_exit_hook_;

  // Command line arguments
  CommandLineArgs cmd_args_;

  // State tracking for recent searches
  bool last_search_active_ = false;
  SearchParams last_search_params_;

  // State tracking
  bool index_dumped_ = false;          // Track if index dump has been performed
  std::atomic<bool> show_settings_{false};         // Settings window visibility (thread-safe)
  std::atomic<bool> show_metrics_{false};           // Metrics window visibility (thread-safe)
  bool metrics_available_ = false;                  // Whether Metrics UI is available (from command line)

  // Periodic recrawl tracking
  std::chrono::steady_clock::time_point last_crawl_completion_time_{};  // NOLINT(readability-redundant-member-init) - set in constructor body
  bool recrawl_enabled_ = false;  // Set to true when crawl folder is available and first crawl completes
  std::chrono::steady_clock::time_point last_interaction_time_{};  // NOLINT(readability-redundant-member-init) - set in constructor body
  bool crawl_just_completed_ = false;  // Flag to trigger search refresh when crawl completes

  // Auto-crawl on startup tracking
  bool should_auto_crawl_ = false;  // If true, automatically start crawling after UI is shown
  std::string auto_crawl_folder_{};  // NOLINT(readability-redundant-member-init) - overwritten in constructor initializer list
  bool auto_crawl_triggered_ = false;  // Flag to ensure auto-crawl is only triggered once (in-class initialized)

  // Deferred index build start (folder crawler): start after first frame so UI is visible (Linux/macOS --crawl-folder)
  bool deferred_index_build_start_ = false;

#ifdef ENABLE_IMGUI_TEST_ENGINE
  // ImGui Test Engine: Application does not own it; created in bootstrap Initialize, destroyed in Cleanup.
  // Valid only between those phases; do not use after Cleanup or store elsewhere.
  ImGuiTestEngine* imgui_test_engine_ = nullptr;
  // Visibility toggle: when false we skip ShowTestEngineWindows to reduce per-frame cost; library sets false when user closes window
  bool show_test_engine_window_ = true;
  // Hook for regression tests (set search params, trigger search, read result count). Owned by Application.
  std::unique_ptr<IRegressionTestHook> regression_hook_;
#endif  // ENABLE_IMGUI_TEST_ENGINE

  // Shared index build state (owned by caller, referenced here)
  IndexBuildState& index_build_state_;
  // NOLINTEND(readability-identifier-naming)

  // Private helper methods
  void ProcessFrame();
  void HandleIndexDump();
  void SaveSettingsOnShutdown();
  void RenderFrame();

  // Helper methods for Run() to reduce cognitive complexity
  bool HandleMinimizedWindow();
  bool HandleUnfocusedWindow();
  bool HandleFocusedWindow(double idle_timeout_seconds, double idle_wait_timeout);
  bool DetectUserInteraction(ImVec2& last_mouse_pos) const;
  void UpdateIdleStateLogging(bool is_idle, bool& was_idle) const;

  // Helper methods for ProcessFrame() to reduce cognitive complexity
  void UpdateIndexBuildState();
  void UpdateSearchState(bool is_index_building);

  // Auto-crawl helper method
  void TriggerAutoCrawl();

  // Stopping overlay removed - now using StoppingState component instead
};

