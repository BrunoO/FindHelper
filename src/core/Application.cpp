/**
 * @file Application.cpp
 * @brief Implementation of the central Application class
 */

#include "core/Application.h"

#include <cassert>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

// GLFW / platform system includes (must be before GLFW native/ImGui backends)
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef _WIN32
// Windows headers must be included before glfw3native.h
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include; path case already matches filesystem, non-portable path warning not applicable
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#else
// macOS includes
#ifdef __APPLE__
#import <Metal/Metal.h>  // NOLINT(llvm-include-order) - Objective-C import, platform-specific
#import <QuartzCore/QuartzCore.h>  // NOLINT(llvm-include-order) - Objective-C import, platform-specific
#endif                             // __APPLE__
#endif                             // _WIN32

#include "api/GeminiApiUtils.h"
#include "core/AppBootstrapResultBase.h"
#include "core/ApplicationLogic.h"
#include "core/CommandLineArgs.h"
#include "core/IndexBuilder.h"
#include "core/Settings.h"
#include "filters/TimeFilterUtils.h"
#include "gui/GuiState.h"
#include "gui/ImGuiUtils.h"
#include "gui/RendererInterface.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "index/FileIndex.h"
#include "path/PathUtils.h"
#include "platform/FileOperations.h"
#include "search/ExportSearchResultsService.h"
#include "search/SearchController.h"
#include "search/SearchResultsService.h"
#include "search/SearchThreadPool.h"
#include "search/SearchWorker.h"
#include "ui/EmptyState.h"
#include "ui/FilterPanel.h"
#include "ui/IconsFontAwesome.h"
#include "ui/MetricsWindow.h"
#include "ui/Popups.h"
#include "ui/ResultsTable.h"
#include "ui/SearchControls.h"
#include "ui/SearchHelpWindow.h"
#include "ui/SearchInputs.h"
#include "ui/SettingsWindow.h"
#include "ui/StatusBar.h"
#include "ui/UIRenderer.h"
#include "usn/UsnMonitor.h"
#include "utils/AsyncUtils.h"
#include "utils/ClipboardUtils.h"
#include "utils/LoadBalancingStrategy.h"  // ValidateAndNormalizeStrategyName (strategy validation and default)
#include "utils/Logger.h"
#include "utils/PlatformTypes.h"  // For NativeWindowHandle definition
#include "utils/ThreadPool.h"
#include "utils/ThreadUtils.h"

// Test engine: include header first so one definition (no ODR risk from inline/macros), then
// forward-declare only what is not in this header. imgui_te_engine.h declares
// ImGuiTestEngine_PostSwap, ImGuiTestEngine_QueueTests, ImGuiTestEngine_IsTestQueueEmpty (used in
// Run/ProcessFrame). Test engine: include header first so one definition (no ODR risk from
// inline/macros), then forward-declare only what is not in this header. imgui_te_engine.h declares
// ImGuiTestEngine_PostSwap, ImGuiTestEngine_QueueTests, ImGuiTestEngine_IsTestQueueEmpty (used in
// Run/ProcessFrame).
#ifdef ENABLE_IMGUI_TEST_ENGINE
#include "imgui_te_engine.h"
void ImGuiTestEngine_ShowTestEngineWindows(ImGuiTestEngine* engine, bool* p_open);  // NOLINT(readability-identifier-naming,google-objc-function-naming) - External ImGui Test Engine API
void RegisterFindHelperTests(ImGuiTestEngine* engine, IRegressionTestHook* hook);  // NOLINT(readability-identifier-naming,google-objc-function-naming) - Test registration; implemented in ImGuiTestEngineTests.cpp

namespace {
class ApplicationRegressionTestHook : public IRegressionTestHook {
 public:
  explicit ApplicationRegressionTestHook(Application* app) : app_(app) {}
  [[nodiscard]] bool IsIndexReady() const override {
    return app_->IsIndexReadyForRegressionTest();
  }
  [[nodiscard]] size_t GetIndexSize() const override {
    return app_->GetIndexSizeForRegressionTest();
  }
  void SetSearchParams(std::string_view filename, std::string_view path,
                       std::string_view extensions_semicolon, bool folders_only) override {
    app_->SetSearchParamsForRegressionTest(filename, path, extensions_semicolon, folders_only);
  }
  [[nodiscard]] std::string_view GetSearchParamFilename() const noexcept override {
    return app_->GetSearchParamFilenameForRegressionTest();
  }
  [[nodiscard]] std::string_view GetSearchParamPath() const noexcept override {
    return app_->GetSearchParamPathForRegressionTest();
  }
  [[nodiscard]] std::string_view GetSearchParamExtensions() const noexcept override {
    return app_->GetSearchParamExtensionsForRegressionTest();
  }
  [[nodiscard]] bool GetSearchParamFoldersOnly() const noexcept override {
    return app_->GetSearchParamFoldersOnlyForRegressionTest();
  }
  void SetLoadBalancingStrategy(std::string_view strategy) override {
    app_->SetLoadBalancingStrategyForRegressionTest(strategy);
  }
  [[nodiscard]] std::string GetLoadBalancingStrategy() const override {
    return app_->GetLoadBalancingStrategyForRegressionTest();
  }
  void SetStreamPartialResults(bool stream) override {
    app_->SetStreamPartialResultsForRegressionTest(stream);
  }
  [[nodiscard]] std::optional<bool> GetStreamPartialResults() const noexcept override {
    return app_->GetStreamPartialResultsForRegressionTest();
  }
  void TriggerManualSearch() override {
    app_->TriggerManualSearchForRegressionTest();
  }
  [[nodiscard]] bool IsSearchComplete() const override {
    return app_->IsSearchCompleteForRegressionTest();
  }
  [[nodiscard]] size_t GetSearchResultCount() const override {
    return app_->GetSearchResultCountForRegressionTest();
  }
  [[nodiscard]] std::string GetClipboardText() const override {
    return app_->GetClipboardTextForRegressionTest();
  }
  void RequestSetSelectionAndMarkFirstResult() override {
    selection_mark_pending_ = true;
  }
  void RequestCopyMarkedPathsToClipboard() override {
    copy_marked_pending_ = true;
  }
  void RequestCopyMarkedFilenamesToClipboard() override {
    copy_marked_filenames_pending_ = true;
  }
  void RequestCopySelectedPathToClipboard() override {
    copy_selected_path_pending_ = true;
  }
  [[nodiscard]] bool GetAndClearRequestSetSelectionAndMarkFirstResult() override {
    const bool v = selection_mark_pending_;
    selection_mark_pending_ = false;
    return v;
  }
  [[nodiscard]] bool GetAndClearRequestCopyMarkedPathsToClipboard() override {
    const bool v = copy_marked_pending_;
    copy_marked_pending_ = false;
    return v;
  }
  [[nodiscard]] bool GetAndClearRequestCopyMarkedFilenamesToClipboard() override {
    const bool v = copy_marked_filenames_pending_;
    copy_marked_filenames_pending_ = false;
    return v;
  }
  [[nodiscard]] bool GetAndClearRequestCopySelectedPathToClipboard() override {
    const bool v = copy_selected_path_pending_;
    copy_selected_path_pending_ = false;
    return v;
  }

 private:
  Application*
    app_;  // NOLINT(readability-identifier-naming) - Project convention: snake_case_ for members
  bool selection_mark_pending_ = false;  // NOLINT(readability-identifier-naming) - Project convention snake_case_ for members
  bool copy_marked_pending_ = false;  // NOLINT(readability-identifier-naming) - Project convention snake_case_ for members
  bool copy_marked_filenames_pending_ = false;  // NOLINT(readability-identifier-naming) - Project convention snake_case_ for members
  bool copy_selected_path_pending_ = false;  // NOLINT(readability-identifier-naming) - Project convention snake_case_ for members
};

// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Cohesive shortcut-test hook handling;
// splitting would fragment
void ProcessShortcutTestHookRequests(IRegressionTestHook* hook, GuiState& state, GLFWwindow* window,
                                     const FileIndex& file_index) {
  if (hook == nullptr) {
    return;
  }
  if (hook->GetAndClearRequestSetSelectionAndMarkFirstResult()) {
    const auto* dr = search::SearchResultsService::GetDisplayResults(state);
    if (dr != nullptr && !dr->empty()) {
      state.selectedRow = 0;
      state.markedFileIds.clear();
      state.markedFileIds.insert((*dr)[0].fileId);
    }
  }
  if (hook->GetAndClearRequestCopyMarkedPathsToClipboard()) {
    ui::CopyMarkedPathsToClipboard(state, window, file_index);
  }
  if (hook->GetAndClearRequestCopyMarkedFilenamesToClipboard()) {
    ui::CopyMarkedFilenamesToClipboard(state, window, file_index);
  }
  if (hook->GetAndClearRequestCopySelectedPathToClipboard()) {
    const auto* dr = search::SearchResultsService::GetDisplayResults(state);
    if (dr != nullptr && !dr->empty()) {
      state.selectedRow = 0;
      file_operations::CopyPathToClipboard(window, (*dr)[0].fullPath);
    }
  }
}
}  // namespace
#endif  // ENABLE_IMGUI_TEST_ENGINE

// NativeWindowHandle is defined in UIRenderer.h (already included above)

/**
 * @brief Extracts platform-specific native window handle from GLFW window
 *
 * This helper function abstracts the platform-specific extraction of native
 * window handles, eliminating the need for #ifdef blocks in ProcessFrame().
 *
 * @param window GLFW window pointer
 * @return Platform-specific native window handle (HWND on Windows, nullptr on macOS)
 */
static NativeWindowHandle GetNativeWindowHandle([[maybe_unused]] GLFWwindow* window) {
#ifdef _WIN32
  return glfwGetWin32Window(window);
#else
  // macOS doesn't need native window handle for current UI operations
  // (context menu and clipboard operations are handled differently)
  return nullptr;
#endif  // _WIN32
}

Application::Application(AppBootstrapResultBase& bootstrap, const CommandLineArgs& cmd_args,
                         IndexBuildState& index_build_state,
                         std::unique_ptr<IIndexBuilder> index_builder, bool should_auto_crawl,
                         std::string_view auto_crawl_folder,
                         bool start_index_build_after_first_frame)
    : file_index_(bootstrap.file_index),
      monitor_(std::move(bootstrap.monitor))  // nullptr on macOS/Linux, moved on Windows
      ,
      index_builder_(std::move(index_builder))  // Take ownership of index builder
      ,
      search_worker_(*file_index_), settings_(bootstrap.settings), window_(bootstrap.window),
      renderer_(
        bootstrap.renderer)  // Unified renderer interface (DirectXManager, MetalManager, etc.)
      ,
      last_window_width_(bootstrap.last_window_width),
      last_window_height_(bootstrap.last_window_height),
      platform_exit_hook_(bootstrap.platform_exit_hook), cmd_args_(cmd_args),
      metrics_available_(cmd_args.show_metrics), should_auto_crawl_(should_auto_crawl),
      auto_crawl_folder_(auto_crawl_folder),
      deferred_index_build_start_(start_index_build_after_first_frame),
      index_build_state_(index_build_state) {
  // Create ThreadPool (owned by Application) for general-purpose async operations
  // Use settings thread pool size if available, otherwise logical processor count (Linux: sysconf fallback when hardware_concurrency() is 0)
  size_t thread_count = GetLogicalProcessorCount();
  if (settings_ != nullptr && settings_->searchThreadPoolSize > 0) {
    thread_count = static_cast<size_t>(settings_->searchThreadPoolSize);
  }
  thread_pool_ = std::make_unique<ThreadPool>(thread_count);

  // Create SearchThreadPool and inject it into FileIndex
  // This replaces the static thread pool with dependency injection
  // Use same thread count as general ThreadPool for consistency
  auto search_thread_pool = std::make_shared<SearchThreadPool>(thread_count);
  if (file_index_ != nullptr) {
    file_index_->SetThreadPool(search_thread_pool);
  }

  // Debug-only assertions (disabled in Release builds via NDEBUG)
  // These are validation checks, not runtime checks - if they fail, there's a programming error
  // In Debug builds, failed assertions call abort() which is appropriate for development
  // In Release builds, these are compiled out (zero overhead)
  assert(file_index_ != nullptr);  // NOSONAR(cpp:S2583) - Assertion is intentional for Debug
                                   // builds, compiled out in Release
  assert(settings_ != nullptr);  // NOSONAR(cpp:S2583) - Assertion is intentional for Debug builds,
                                 // compiled out in Release
  assert(window_ != nullptr);    // NOSONAR(cpp:S2583) - Assertion is intentional for Debug builds,
                                 // compiled out in Release
  assert(thread_pool_ != nullptr);  // NOSONAR(cpp:S2583) - Assertion is intentional for Debug
                                    // builds, compiled out in Release
  assert(renderer_ != nullptr);  // NOSONAR(cpp:S2583) - Assertion is intentional for Debug builds,
                                 // compiled out in Release

  // Initialize recrawl tracking
  last_crawl_completion_time_ = std::chrono::steady_clock::now();
  recrawl_enabled_ = false;
  last_interaction_time_ = std::chrono::steady_clock::now();

#ifdef ENABLE_IMGUI_TEST_ENGINE
  imgui_test_engine_ = bootstrap.imgui_test_engine;
  if (imgui_test_engine_ != nullptr) {
    regression_hook_ = std::make_unique<ApplicationRegressionTestHook>(this);
    RegisterFindHelperTests(imgui_test_engine_, regression_hook_.get());
  }
#endif  // ENABLE_IMGUI_TEST_ENGINE
}

Application::~Application() {
  // Stop index builder before cleanup to ensure all background threads have joined
  if (index_builder_ != nullptr) {
    index_builder_->Stop();
  }

  // CRITICAL: Drain ThreadPool tasks that capture references into state_ (results,
  // file_index, sort_cancellation_token_) before state_ is destroyed. Otherwise
  // a worker can run a StartAttributeLoadingAsync task after state_ is destroyed
  // and dereference dangling references → use-after-free / SIGSEGV on quit.
  state_.sort_cancellation_token_.Cancel();
  for (auto& f : state_.attributeLoadingFutures) {
    async_utils::SafeWaitFuture(f);
  }
  state_.attributeLoadingFutures.clear();
  for (auto& f : state_.cloudFileLoadingFutures) {
    async_utils::SafeWaitFuture(f);
  }
  state_.cloudFileLoadingFutures.clear();
}

bool Application::StartIndexBuilding(
  std::string_view folder_path) {  // NOSONAR(cpp:S107) - 12 parameters: UI rendering coordinator
                                   // needs many dependencies
  if (folder_path.empty()) {
    LOG_ERROR("StartIndexBuilding called with empty folder path");
    return false;
  }

  // Stop existing builder if running
  if (index_builder_ != nullptr) {
    index_builder_->Stop();
    index_builder_.reset();
  }

  // Clear the existing index before starting a new crawl
  // This ensures the new crawl replaces the old index instead of adding to it
  file_index_->Clear();

  // Create new builder with selected folder
  IndexBuilderConfig config;
  config.crawl_folder = std::string(folder_path);
  config.index_file_path.clear();
  config.use_usn_monitor = false;  // Folder crawler, not USN

  index_builder_ = CreateFolderCrawlerIndexBuilder(*file_index_, config);
  if (index_builder_ != nullptr) {
    index_build_state_.Reset();
    index_build_state_.source_description = "Folder: " + std::string(folder_path);
    index_builder_->Start(index_build_state_);
    LOG_INFO_BUILD("Started folder crawl for: " << folder_path);
    return true;
  }

  LOG_ERROR("Failed to create folder crawler index builder");
  return false;
}

void Application::StopIndexBuilding() {  // NOSONAR(cpp:S107) - 12 parameters: UI rendering
                                         // coordinator needs many dependencies
  if (index_builder_ != nullptr) {
    index_builder_->Stop();
    LOG_INFO_BUILD("Stopped index building");
  }
}

void Application::TriggerRecrawl() {
  // Don't recrawl if already building
  if (IsIndexBuilding()) {
    return;
  }

  // Don't recrawl if no folder is set
  if (settings_ == nullptr || settings_->crawlFolder.empty()) {
    return;
  }

  // Use existing StartIndexBuilding method which handles stopping old builder
  if (StartIndexBuilding(settings_->crawlFolder)) {
    LOG_INFO_BUILD("Periodic recrawl started for folder: " << settings_->crawlFolder);
  } else {
    LOG_ERROR_BUILD("Failed to start periodic recrawl for folder: " << settings_->crawlFolder);
  }
}

std::chrono::steady_clock::time_point Application::GetLastCrawlCompletionTime() const {
  return last_crawl_completion_time_;
}

bool Application::IsRecrawlEnabled() const {
  return recrawl_enabled_;
}

bool Application::CheckAndClearCrawlCompletion() {
  if (crawl_just_completed_) {
    crawl_just_completed_ = false;
    return true;
  }
  return false;
}

void Application::TriggerAutoCrawl() {
  if (auto_crawl_folder_.empty()) {
    LOG_ERROR("TriggerAutoCrawl called with empty folder path");
    return;
  }

  LOG_INFO_BUILD("Auto-starting crawl for folder: " << auto_crawl_folder_);

  if (StartIndexBuilding(auto_crawl_folder_)) {
    LOG_INFO_BUILD("Auto-crawl started successfully");
  } else {
    LOG_ERROR_BUILD("Failed to start auto-crawl for folder: " << auto_crawl_folder_);
  }
}

bool Application::HandleMinimizedWindow() {
  if (glfwGetWindowAttrib(window_, GLFW_ICONIFIED) == 0) {
    return false;
  }
  // Window minimized - sleep until event arrives
  // This prevents burning CPU cycles rendering to an invisible window.
  // See: https://github.com/ocornut/imgui/issues/440
  glfwWaitEvents();  // Blocks until an event arrives (e.g., window restore)
  // Note: Do NOT reset last_interaction_time_ here - window restore is not a user interaction
  // The idle timer should continue from when the user last actually interacted with the app
  return true;
}

bool Application::HandleUnfocusedWindow() {
  constexpr double kUnfocusedWaitTimeout = 0.1;  // 100ms when unfocused (~10 FPS)
  if (const bool is_focused = glfwGetWindowAttrib(window_, GLFW_FOCUSED) != 0; is_focused) {
    return false;
  }
  // Window unfocused - reduce frame rate significantly
  // User is working in another app, no need for high FPS
  glfwWaitEventsTimeout(kUnfocusedWaitTimeout);  // ~10 FPS when unfocused
  ProcessFrame();
  // Note: Do NOT reset last_interaction_time_ here - regaining focus is not a user interaction
  // The idle timer should continue from when the user last actually interacted with the app
  return true;
}

bool Application::HandleFocusedWindow(double idle_timeout_seconds, double idle_wait_timeout) {
  // Calculate time since last interaction (using previous frame's state)
  auto now = std::chrono::steady_clock::now();

  const bool is_idle =
    std::chrono::duration<double>(now - last_interaction_time_).count() > idle_timeout_seconds;

  // Choose event handling strategy

  if (const bool has_background_work{search_worker_.IsBusy() || IsIndexBuilding() ||
                                     !state_.attributeLoadingFutures.empty()};
      is_idle && !has_background_work) {
    glfwWaitEventsTimeout(idle_wait_timeout);  // ~10 FPS when idle
  } else {
    glfwPollEvents();  // Full speed when active
  }

  ProcessFrame();
  return is_idle;
}

bool Application::DetectUserInteraction(ImVec2& last_mouse_pos) const {
  const ImGuiIO& io = ImGui::GetIO();

  // Check for mouse movement
  if (last_mouse_pos.x != io.MousePos.x || last_mouse_pos.y != io.MousePos.y) {
    last_mouse_pos = io.MousePos;
    return true;
  }

  // Check for mouse button activity
  if (io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2] || io.MouseWheel != 0.0F ||
      io.MouseWheelH != 0.0F) {
    return true;
  }

  // Check for keyboard activity (any key down)
  if (io.WantCaptureKeyboard) {
    return true;
  }

  // Check for any input event this frame
  if (io.WantCaptureMouse || io.WantTextInput) {
    return true;
  }

  return false;
}

void Application::UpdateIdleStateLogging(bool is_idle, bool& was_idle) const {
  if (is_idle != was_idle) {
    LOG_INFO_BUILD("Power mode: " << (is_idle ? "Idle (~10 FPS)" : "Active (VSync)"));
    was_idle = is_idle;
  }
}

int Application::Run() {  // NOLINT(readability-function-cognitive-complexity) - Main loop: event
                          // handling, idle detection, and exception paths
  try {
    // Power-saving mode configuration
    // - Minimized: Sleep until event (0% CPU when not visible)
    // - Unfocused: ~10 FPS (user is in another app)
    // - Idle: ~10 FPS after 2 seconds of no interaction
    // - Active: Full speed (VSync-limited, typically 60 FPS)
    constexpr double kIdleTimeoutSeconds = 2.0;
    constexpr double kIdleWaitTimeout = 0.1;  // 100ms when idle (~10 FPS)

    // Idle detection state (tracked across frames)
    // Use member variable for last_interaction_time so it can be accessed from ProcessFrame
    last_interaction_time_ = std::chrono::steady_clock::now();
    auto last_mouse_pos = ImVec2(-1, -1);  // Invalid position to force first-frame detection
    bool was_idle = false;

    // Render first frame before starting deferred folder crawl so UI is visible (fixes blank UI on
    // Linux with --crawl-folder)
    if (deferred_index_build_start_ && index_builder_ != nullptr) {
      glfwPollEvents();
      ProcessFrame();
      index_builder_->Start(index_build_state_);
      deferred_index_build_start_ = false;
    }

#ifdef ENABLE_IMGUI_TEST_ENGINE
    // Queue only when compile-time test engine is enabled and runtime engine/context are available
    if (cmd_args_.run_imgui_tests_and_exit && imgui_test_engine_ != nullptr &&
        ImGui::GetCurrentContext() != nullptr) {
      // Keep GUI visible on error so failing state can be inspected (same as ticking "KeepGui" in
      // test window).
      ImGuiTestEngine_GetIO(imgui_test_engine_).ConfigKeepGuiFunc = true;
      ImGuiTestEngine_QueueTests(imgui_test_engine_, ImGuiTestGroup_Unknown, "all",
                                 ImGuiTestRunFlags_RunFromCommandLine);
    }
#endif  // ENABLE_IMGUI_TEST_ENGINE

    // Main loop: run while the window should NOT close (0 = don't close, non-zero = should close)
    while (glfwWindowShouldClose(window_) == 0) {
      // Priority 1: Window minimized - sleep until event arrives
      if (HandleMinimizedWindow()) {
        continue;  // Re-check window state before processing frame
      }

      // Priority 2: Window unfocused - reduce frame rate significantly
      if (HandleUnfocusedWindow()) {
        continue;
      }

      // Priority 3: Window focused - check if we should use idle mode
      const bool is_idle = HandleFocusedWindow(kIdleTimeoutSeconds, kIdleWaitTimeout);

      // After frame: detect interaction for NEXT iteration's idle decision
      if (DetectUserInteraction(last_mouse_pos)) {
        last_interaction_time_ = std::chrono::steady_clock::now();
      }

      // Log idle state transitions (debug builds only)
      UpdateIdleStateLogging(is_idle, was_idle);
    }

    // Platform exit hook (e.g. Windows PGO flush). No-op on platforms that do not set it.
    if (platform_exit_hook_) {
      platform_exit_hook_("shutdown");
    }

    // Save settings before shutdown
    SaveSettingsOnShutdown();

    return 0;
  } catch (const std::bad_alloc& e) {
    (void)e;  // Suppress unused variable warning in Release mode
    LOG_ERROR_BUILD("Memory allocation failure in Application::Run(): " << e.what());
    if (platform_exit_hook_) {
      platform_exit_hook_("exception_exit");
    }
    return 1;
  } catch (const std::runtime_error& e) {  // NOSONAR(cpp:S1181) - Top-level handler for std::runtime_error; more granular
                                           // exception types do not change handling
    (void)e;       // Suppress unused variable warning in Release mode
    LOG_ERROR_BUILD("Runtime error in Application::Run(): " << e.what());
    if (platform_exit_hook_) {
      platform_exit_hook_("exception_exit");
    }
    return 1;
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Final standard exception handler;
                                       // more specific granularity not useful at this top level
    (void)e;                           // Suppress unused variable warning in Release mode
    LOG_ERROR_BUILD("Standard exception in Application::Run(): " << e.what());
    if (platform_exit_hook_) {
      platform_exit_hook_("exception_exit");
    }
    return 1;
  } catch (...) {  // NOSONAR(cpp:S2738) - Required top-level catch-all to handle non-standard
                   // exceptions and ensure clean shutdown
    LOG_ERROR("Unknown exception in Application::Run() - possible memory corruption or "
              "non-standard exception");
    if (platform_exit_hook_) {
      platform_exit_hook_("exception_exit");
    }
    return 1;
  }
}

void Application::ProcessFrame() {
  // Note: Event polling (glfwPollEvents/glfwWaitEvents/glfwWaitEventsTimeout)
  // is handled in Run() before calling ProcessFrame() to enable power-saving modes.

  // Trigger auto-crawl after first frame is processed (UI is shown)
  if (should_auto_crawl_ && !auto_crawl_triggered_) {
    TriggerAutoCrawl();
    auto_crawl_triggered_ = true;
  }

  // Start the Dear ImGui frame
  // Platform-specific frame setup (DirectX or Metal)
  if (renderer_ != nullptr) {
    renderer_->BeginFrame();
  }

  // Common ImGui frame setup (GLFW + NewFrame)
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  // Handle index dumping if requested
  HandleIndexDump();

  // Application logic (handles search, maintenance, keyboard shortcuts)
  const bool is_index_building = IsIndexBuilding();

  // Update index build state and search state
  UpdateIndexBuildState();
  UpdateSearchState(is_index_building);

#ifdef ENABLE_IMGUI_TEST_ENGINE
  ProcessShortcutTestHookRequests(regression_hook_.get(), state_, window_, *file_index_);
#endif  // ENABLE_IMGUI_TEST_ENGINE

  // Render UI (delegated to UIRenderer)
  NativeWindowHandle native_window = GetNativeWindowHandle();
  ui::UIRenderer::RenderMainWindow(
    {state_, this, *settings_, *file_index_, *thread_pool_, monitor_.get(), search_worker_,
     native_window, window_, show_settings_, show_metrics_, is_index_building, metrics_available_,
#ifdef ENABLE_IMGUI_TEST_ENGINE
     (imgui_test_engine_ != nullptr ? &show_test_engine_window_ : nullptr)
#else
     nullptr
#endif  // ENABLE_IMGUI_TEST_ENGINE
    });
  ui::UIRenderer::RenderFloatingWindows({state_, this, *settings_, *file_index_, monitor_.get(),
                                         search_worker_, window_, show_settings_, show_metrics_,
                                         metrics_available_});

#ifdef ENABLE_IMGUI_TEST_ENGINE
  if (imgui_test_engine_ != nullptr && ImGui::GetCurrentContext() != nullptr &&
      show_test_engine_window_) {
    ImGuiTestEngine_ShowTestEngineWindows(imgui_test_engine_, &show_test_engine_window_);
  }
#endif  // ENABLE_IMGUI_TEST_ENGINE

  // Rendering (unguarded; not test-engine-dependent)
  RenderFrame();

  // Render additional platform windows when multi-viewport is enabled
  if (const ImGuiIO& io = ImGui::GetIO();
      (static_cast<unsigned int>(io.ConfigFlags) &
       static_cast<unsigned int>(ImGuiConfigFlags_ViewportsEnable)) != 0U) {
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
  }

#ifdef ENABLE_IMGUI_TEST_ENGINE
  if (imgui_test_engine_ != nullptr && ImGui::GetCurrentContext() != nullptr) {
    ImGuiTestEngine_PostSwap(imgui_test_engine_);
    if (cmd_args_.run_imgui_tests_and_exit &&
        ImGuiTestEngine_IsTestQueueEmpty(imgui_test_engine_)) {
      glfwSetWindowShouldClose(window_, 1);
    }
  }
#endif  // ENABLE_IMGUI_TEST_ENGINE
}

void Application::UpdateIndexBuildState() {
  // Track previous state to detect completion
  const bool was_building = state_.index_build_in_progress;
  const auto now = std::chrono::steady_clock::now();

  // Update GUI-facing view of index build state so UI components can display
  // progress without knowing the underlying implementation (USN vs crawler).
  state_.index_build_in_progress = index_build_state_.active.load(std::memory_order_relaxed);
  state_.index_build_failed = index_build_state_.failed.load(std::memory_order_relaxed);
  state_.index_entries_processed =
    index_build_state_.entries_processed.load(std::memory_order_relaxed);
  state_.index_files_processed = index_build_state_.files_processed.load(std::memory_order_relaxed);
  state_.index_dirs_processed = index_build_state_.dirs_processed.load(std::memory_order_relaxed);
  state_.index_error_count = index_build_state_.errors.load(std::memory_order_relaxed);

  // Detect start of a new index build (initial crawl, auto-crawl, or USN-based build).
  if (state_.index_build_in_progress && !was_building) {
    state_.index_build_start_time = now;
    state_.index_build_last_duration_ms = 0U;
    state_.index_build_has_timing = true;
  }

  // Detect crawl completion for periodic recrawl tracking
  if (was_building && !state_.index_build_in_progress && !state_.index_build_failed) {
    // Compute and store duration of the just-completed index build.
    const auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - state_.index_build_start_time);
    state_.index_build_last_duration_ms = static_cast<uint64_t>(duration.count());

    // Crawl just completed successfully
    last_crawl_completion_time_ = now;
    crawl_just_completed_ = true;  // Flag to trigger search refresh
    // Enable periodic recrawl if crawl folder is set
    if (settings_ != nullptr && !settings_->crawlFolder.empty()) {
      recrawl_enabled_ = true;
    }
  }

  // Build a concise status string for the status bar / empty state.
  state_.index_build_status_text.clear();
  if (state_.index_build_in_progress) {
    const std::string source = index_build_state_.source_description.empty()
                                 ? std::string("index")
                                 : index_build_state_.source_description;
    state_.index_build_status_text = "Indexing " + source;
  } else if (state_.index_build_failed) {
    std::string error = index_build_state_.GetLastErrorMessage();
    if (error.empty()) {
      error = "Index build failed";
    }
    state_.index_build_status_text = std::move(error);
  } else if (index_build_state_.completed.load(std::memory_order_relaxed) &&
             state_.index_entries_processed > 0U) {
    state_.index_build_status_text =
      "Index ready - " + std::to_string(state_.index_entries_processed) + " entries";
  }
}

void Application::UpdateSearchState(bool is_index_building) {
  // Track search completion for recent searches.
  // We need both the previous frame's searchActive (last_search_active_)
  // and the value after application_logic::Update() runs.
  const bool was_search_active = last_search_active_;

  // Store current search params if a search is active before the update.

  if (const bool is_search_active_before{state_.searchActive}; is_search_active_before) {
    last_search_params_ = state_.BuildCurrentSearchParams();
  }

  application_logic::Update(state_, search_controller_, search_worker_, *file_index_,
                            monitor_.get(), is_index_building, *this, *settings_,
                            last_interaction_time_);

  const bool is_search_active_after{state_.searchActive};

  // Check if search just completed (was active, now not active, and has results)
  // Only record recent searches for manually triggered searches (not instant/auto-refresh)
  if (was_search_active && !is_search_active_after && !state_.searchResults.empty() &&
      settings_ != nullptr && state_.searchWasManual) {
    // Record recent search (only for manual searches)
    RecordRecentSearch(last_search_params_, state_, *settings_);
    // Save settings to persist recent searches
    SaveSettings(*settings_);
  }

  // Reset manual flag after checking (for next search)
  if (!is_search_active_after) {
    state_.searchWasManual = false;
  }

  last_search_active_ = is_search_active_after;
}

void Application::HandleIndexDump() {
  if (cmd_args_.dump_index_to.empty() || index_dumped_ || !monitor_) {
    return;
  }

  if (const bool is_index_building = IsIndexBuilding(); is_index_building) {
    return;
  }

  // Wait a brief moment to ensure index is fully finalized.
  const size_t indexed_count{
    monitor_->GetIndexedFileCount()};
                                       // false positive; indexed count is stable here
  if (indexed_count > 0) {
    if (DumpIndexToFile(*file_index_, cmd_args_.dump_index_to)) {
      index_dumped_ = true;
      LOG_INFO("Index dump completed successfully");
    } else {
      LOG_ERROR("Failed to dump index to file");
      // Don't retry - log error and continue
      index_dumped_ = true;  // Mark as attempted to avoid retry loops
    }
  }
}

void Application::SaveSettingsOnShutdown() {
  if (settings_ == nullptr || last_window_width_ == nullptr || last_window_height_ == nullptr) {
    return;
  }

  // Persist window size on shutdown so next run can start with the same size
  if (*last_window_width_ >= settings_defaults::kMinWindowWidth &&
      *last_window_width_ <= settings_defaults::kMaxWindowWidth) {
    settings_->windowWidth = *last_window_width_;
  }
  if (*last_window_height_ >= settings_defaults::kMinWindowHeight &&
      *last_window_height_ <= settings_defaults::kMaxWindowHeight) {
    settings_->windowHeight = *last_window_height_;
  }

  // Always attempt to save settings so preferences persist
  SaveSettings(*settings_);
}

NativeWindowHandle Application::GetNativeWindowHandle() const {
  return ::GetNativeWindowHandle(window_);
}

bool Application::IsIndexBuilding() const {
  // Prefer shared IndexBuildState when available so that all platforms
  // (Windows USN, macOS/Linux FolderCrawler) report index build status
  // through a single abstraction.
  if (index_build_state_.active.load(std::memory_order_relaxed)) {
    return true;
  }

  // Check if finalization is in progress (prevents search race condition)
  if (index_build_state_.finalizing_population.load(std::memory_order_relaxed)) {
    return true;
  }

  // Backward-compatible fallback for Windows USN monitor metrics.
  return monitor_ && monitor_->IsPopulatingIndex();
}

void Application::RenderFrame() {
  // Render ImGui draw data
  ImGui::Render();

  // Platform-specific rendering and presentation
  if (renderer_ != nullptr) {
    renderer_->RenderImGui();
    renderer_->EndFrame();
  }
}

// UIActions interface implementation
void Application::TriggerManualSearch(const GuiState& state) {
  // Block manual search while index is building (prevents race with path_storage_
  // and avoids starting search from recent searches, example searches, or saved search)
  if (IsIndexBuilding()) {
    return;
  }
  // Sync caller's search params into state_ so the controller uses them (required when
  // caller is e.g. EmptyState after applying an example search; state and state_ are
  // usually the same reference, but syncing guarantees correct params).
  state_.filenameInput.SetValue(state.filenameInput.AsView());
  state_.extensionInput.SetValue(state.extensionInput.AsView());
  state_.pathInput.SetValue(state.pathInput.AsView());
  state_.foldersOnly = state.foldersOnly;
  state_.caseSensitive = state.caseSensitive;
  state_.timeFilter = state.timeFilter;
  state_.sizeFilter = state.sizeFilter;

  if (settings_ != nullptr) {
    search_controller_.TriggerManualSearch(state_, search_worker_, *settings_);
  }
}

#ifdef ENABLE_IMGUI_TEST_ENGINE
bool Application::IsIndexReadyForRegressionTest() const {
  if (IsIndexBuilding()) {
    return false;
  }
  return state_.index_entries_processed > 0U ||
         (file_index_ != nullptr && file_index_->Size() > 0U);
}

size_t Application::GetIndexSizeForRegressionTest() const {
  // Guard: file_index_ may be null before bootstrap or after teardown; do not dereference without
  // check.
  if (file_index_ == nullptr) {
    return 0U;
  }
  return file_index_->Size();
}

void Application::SetSearchParamsForRegressionTest(std::string_view filename, std::string_view path,
                                                   std::string_view extensions_semicolon,
                                                   bool folders_only) {
  state_.filenameInput.SetValue(filename);
  state_.pathInput.SetValue(path);
  state_.extensionInput.SetValue(extensions_semicolon);
  state_.foldersOnly = folders_only;
}

// Returned views refer to state_.*Input.buffer_ (persistent storage); valid for lifetime of *this.
// SearchInputField::AsView() returns a view of its member std::array, not a temporary.
std::string_view Application::GetSearchParamFilenameForRegressionTest() const noexcept {
  return state_.filenameInput.AsView();
}

std::string_view Application::GetSearchParamPathForRegressionTest() const noexcept {
  return state_.pathInput.AsView();
}

std::string_view Application::GetSearchParamExtensionsForRegressionTest() const noexcept {
  return state_.extensionInput.AsView();
}

bool Application::GetSearchParamFoldersOnlyForRegressionTest() const noexcept {
  return state_.foldersOnly;
}

void Application::SetLoadBalancingStrategyForRegressionTest(std::string_view strategy) {
  if (settings_ == nullptr) {
    return;
  }
  // ValidateAndNormalizeStrategyName from utils/LoadBalancingStrategy.h; invalid → "hybrid" and log
  settings_->loadBalancingStrategy = ValidateAndNormalizeStrategyName(
    strategy);  // NOLINT(readability-identifier-naming) - camelCase for JSON/settings API
}

std::string Application::GetLoadBalancingStrategyForRegressionTest() const {
  if (settings_ == nullptr) {
    return {};
  }
  return settings_->loadBalancingStrategy;  // NOLINT(readability-identifier-naming) - camelCase for
                                            // JSON/settings API
}

void Application::SetStreamPartialResultsForRegressionTest(bool stream) {
  if (settings_ == nullptr) {
    return;
  }
  settings_->streamPartialResults =
    stream;  // NOLINT(readability-identifier-naming) - camelCase for JSON/settings API
}

std::optional<bool> Application::GetStreamPartialResultsForRegressionTest() const noexcept {
  if (settings_ == nullptr) {
    return std::nullopt;  // Unavailable (distinct from explicitly false)
  }
  return settings_->streamPartialResults;  // NOLINT(readability-identifier-naming) - camelCase for
                                           // JSON/settings API
}

void Application::TriggerManualSearchForRegressionTest() {
  if (IsIndexBuilding() || settings_ == nullptr) {
    return;
  }
  state_.searchWasManual = true;
  search_controller_.TriggerManualSearch(state_, search_worker_, *settings_);
}

bool Application::IsSearchCompleteForRegressionTest() const {
  return !state_.searchActive;
}

size_t Application::GetSearchResultCountForRegressionTest() const {
  return state_.searchResults.size();
}

std::string Application::GetClipboardTextForRegressionTest() const {
  return clipboard_utils::GetClipboardText(window_);
}
#endif  // ENABLE_IMGUI_TEST_ENGINE

void Application::ApplySavedSearch(GuiState& state, const SavedSearch& saved_search) {
  ApplySavedSearchToGuiState(saved_search, state);
}

size_t Application::GetIndexedFileCount() const {
  return monitor_ ? monitor_->GetIndexedFileCount() : file_index_->Size();
}

void Application::ExportToCsv(GuiState& state) {
  const auto* results = search::SearchResultsService::GetDisplayResults(state);
  if (results == nullptr || results->empty()) {
    state.exportErrorMessage = "No results to export";
    state.exportNotificationTime = std::chrono::steady_clock::now();
    return;
  }

  // Guard: ExportToCsv depends on file_index_. If not yet initialized, skip and set error.
  if (file_index_ == nullptr) {
    state.exportErrorMessage = "Export not available (index not ready)";
    state.exportNotificationTime = std::chrono::steady_clock::now();
    return;
  }

  // Determine output path: prefer Downloads, fall back to Desktop
  std::string export_folder = path_utils::GetDownloadsPath();
  if (const std::string default_user_root = path_utils::GetDefaultUserRootPath();
      export_folder.empty() || export_folder == default_user_root) {
    export_folder = path_utils::GetDesktopPath();
  }

  // Create timestamped filename: search_results_YYYY-MM-DD_HHMMSS.csv
  auto now_time_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  struct tm time_struct{};
#ifdef _WIN32
  if (localtime_s(&time_struct, &now_time_t) != 0) {
    state.exportErrorMessage = "Failed to get local time for filename";
    state.exportNotificationTime = std::chrono::steady_clock::now();
    return;
  }
#else
  if (localtime_r(&now_time_t, &time_struct) == nullptr) {
    state.exportErrorMessage = "Failed to get local time for filename";
    state.exportNotificationTime = std::chrono::steady_clock::now();
    return;
  }
#endif  // _WIN32

  std::stringstream ss;
  ss << "search_results_" << std::put_time(&time_struct, "%Y-%m-%d_%H%M%S") << ".csv";

  if (const std::string output_path = path_utils::JoinPath(export_folder, ss.str());
      search::ExportSearchResultsService::ExportToCsv(*results, *file_index_, output_path)) {
    const bool used_downloads = (export_folder != path_utils::GetDesktopPath());
    state.exportNotification = "Exported " + std::to_string(results->size()) + " results to " +
                               (used_downloads ? "Downloads" : "Desktop");
    state.exportErrorMessage = "";
  } else {
    state.exportErrorMessage = "Failed to export CSV";
    state.exportNotification = "";
  }
  state.exportNotificationTime = std::chrono::steady_clock::now();
}
