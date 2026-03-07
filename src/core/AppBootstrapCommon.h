#pragma once

/**
 * @file AppBootstrapCommon.h
 * @brief Common bootstrap utilities shared across platforms
 *
 * This header contains common bootstrap functions that are identical across
 * Windows, Linux, and macOS platforms. These functions handle:
 * - CPU information logging
 * - Command-line argument overrides
 * - GLFW error callback setup
 * - Index file loading
 *
 * Platform-specific bootstrap code should use these utilities to avoid duplication.
 */

#include <string>
#include <string_view>
#include <thread>

// NOTE: GLFW and ImGui must be included with proper defines BEFORE including this header
// Platform files must set GLFW_INCLUDE_NONE and platform-specific defines (GLFW_EXPOSE_NATIVE_*)
// before including AppBootstrapCommon.h
#include <GLFW/glfw3.h>

#include "core/AppBootstrapResultBase.h"
#include "core/CommandLineArgs.h"
#include "core/Settings.h"
#include "core/Version.h"
#include "gui/RendererInterface.h"
#include "imgui.h"
#include "index/FileIndex.h"
#include "index/IndexFromFilePopulator.h"
#include "ui/Theme.h"
#include "utils/CpuFeatures.h"
#include "utils/Logger.h"

// Include ImGui GLFW backend header if not already included
// This is needed for CleanupImGuiAndGlfw template function
#ifndef IMGUI_IMPL_GLFW_H
#include "imgui_impl_glfw.h"
#endif  // IMGUI_IMPL_GLFW_H

namespace AppBootstrapCommon {  // NOLINT(readability-identifier-naming) - PascalCase matches header name
// imgui_style_constants removed - replaced by ui::Theme

/**
 * @brief Log CPU information (hardware concurrency, hyper-threading, AVX2 support)
 *
 * This function logs detailed CPU information that is useful for debugging
 * and understanding the runtime environment. Called during application startup.
 */
inline void LogCpuInformation() {
  LOG_IMPORTANT_BUILD("=== CPU Information ===");
  LOG_IMPORTANT_BUILD("hardware_concurrency(): " << std::thread::hardware_concurrency());

  const bool ht_enabled = cpu_features::IsHyperThreadingEnabled();
  auto [physical_cores, logical_cores] = cpu_features::GetCoreCounts();

  LOG_IMPORTANT_BUILD("Hyper-Threading enabled: " << (ht_enabled ? "Yes" : "No"));
  if (physical_cores > 0 && logical_cores > 0) {
    LOG_IMPORTANT_BUILD("Physical cores: " << physical_cores);
    LOG_IMPORTANT_BUILD("Logical cores: " << logical_cores);
    if (logical_cores > physical_cores) {
      LOG_IMPORTANT_BUILD("HT ratio: " << (logical_cores / physical_cores) << "x");
    }
  } else {
    LOG_IMPORTANT_BUILD("Core count detection: Failed or unavailable");
  }

  const bool avx2_supported = cpu_features::SupportsAVX2();
  LOG_IMPORTANT_BUILD("AVX2 support: " << (avx2_supported ? "Yes" : "No"));
  LOG_IMPORTANT_BUILD("Build features: " << cpu_features::GetBuildFeatureString());
  LOG_IMPORTANT_BUILD("======================");
}

/**
 * @brief Setup GLFW error callback to log errors
 *
 * Configures GLFW to log all errors via the application's logging system.
 * Should be called before glfwInit().
 */
inline void SetupGlfwErrorCallback() {
  glfwSetErrorCallback([](int error, const char* description) {
    LOG_ERROR_BUILD("GLFW Error " << error << ": " << description);
  });
}

/**
 * @brief Apply integer command-line override to a setting
 *
 * @tparam T Integer type (int, size_t, etc.)
 * @param override_value Value from command line (or invalid_value if not set)
 * @param setting_value Reference to setting to override
 * @param setting_name Name of setting for logging
 * @param invalid_value Value that indicates override is not set (default: -1)
 */
template <typename T>
void ApplyIntOverride(T override_value, T& setting_value, [[maybe_unused]] const char* setting_name,
                      T invalid_value = static_cast<T>(-1)) {
  if (override_value >= 0 || override_value != invalid_value) {
    setting_value = override_value;
    LOG_INFO_BUILD("Command line override: " << setting_name << " = " << setting_value);
  }
}

/**
 * @brief Apply string command-line override to a setting
 *
 * @param override_value String value from command line
 * @param setting_value Reference to setting to override
 * @param setting_name Name of setting for logging
 */
inline void ApplyStringOverride(std::string_view override_value, std::string& setting_value,
                                [[maybe_unused]] const char* setting_name) {
  if (!override_value.empty()) {
    setting_value = override_value;
    LOG_INFO_BUILD("Command line override: " << setting_name << " = " << setting_value);
  }
}

/**
 * @brief Apply all command-line overrides to application settings
 *
 * Applies thread pool size, load balancing strategy, and window dimension
 * overrides from command-line arguments to the application settings.
 *
 * @param cmd_args Command-line arguments
 * @param app_settings Application settings to modify
 */
inline void ApplyCommandLineOverrides(const CommandLineArgs& cmd_args, AppSettings& app_settings) {
  ApplyIntOverride(cmd_args.thread_pool_size_override, app_settings.searchThreadPoolSize,
                   "thread pool size");
  ApplyStringOverride(cmd_args.load_balancing_override, app_settings.loadBalancingStrategy,
                      "load balancing strategy");
  ApplyIntOverride(cmd_args.window_width_override, app_settings.windowWidth, "window width");
  ApplyIntOverride(cmd_args.window_height_override, app_settings.windowHeight, "window height");
  ApplyStringOverride(cmd_args.win_volume_override, app_settings.monitoredVolume, "Windows volume");

#ifdef _WIN32
  if (!app_settings.monitoredVolume.empty()) {
    std::string root = app_settings.monitoredVolume;
    // Defensive check: ensure root is not empty before accessing root.back()
    // (SonarQube cpp:S6641 - even though we check monitoredVolume.empty() above,
    // static analysis may not track the relationship between the check and the copy)
    if (!root.empty() && root.back() != '\\' && root.back() != '/') {
      root += '\\';
    }
    path_utils::SetDefaultVolumeRootPath(root);
  }
#endif  // _WIN32

  const std::string pool_msg =  // NOLINT(bugprone-unused-local-non-trivial-variable) - used in LOG_INFO_BUILD below
    (app_settings.searchThreadPoolSize == 0)
      ? "Thread pool size: auto-detect (will use hardware concurrency on first search)"
      : "Thread pool size: " + std::to_string(app_settings.searchThreadPoolSize) +
          " threads (will be used when thread pool is created on first search)";
  LOG_INFO_BUILD(pool_msg);
}

/**
 * @brief Load index from file if specified
 *
 * Loads the file index from a previously saved index file. If the file path
 * is empty, returns true (no file to load). If loading fails, logs an error
 * and returns false.
 *
 * @param file_path Path to index file (empty string if not specified)
 * @param file_index File index to populate
 * @param verbose_logging If true, log success/failure with file path (default: false)
 * @return true on success or if no file to load, false on failure
 */
[[nodiscard]] inline bool LoadIndexFromFile(std::string_view file_path, FileIndex& file_index,
                                            bool verbose_logging = false) {
  if (file_path.empty()) {
    return true;  // No file to load
  }
  if (populate_index_from_file(file_path, file_index)) {
    file_index.RecomputeAllPaths();
    if (verbose_logging) {
      LOG_INFO_BUILD("Index loaded successfully from: " << file_path);
    }
    return true;
  }
  if (verbose_logging) {
    LOG_ERROR_BUILD("Failed to load index from: " << file_path);
  } else {
    LOG_ERROR("Failed to populate index from file. Exiting.");
  }
  return false;
}

/**
 * @brief Create GLFW window with specified settings
 *
 * Creates a GLFW window with the specified dimensions and title. The window
 * is stored in the bootstrap result. Platform-specific window hints should
 * be set before calling this function (e.g., GLFW_CLIENT_API for Windows,
 * GLFW_CONTEXT_VERSION for OpenGL).
 *
 * @tparam BootstrapResult Platform-specific bootstrap result type (must inherit from
 * AppBootstrapResultBase)
 * @param app_settings Application settings containing window dimensions
 * @param result Bootstrap result to store window in
 * @param window_title Window title (default: APP_DISPLAY_NAME)
 * @return GLFW window pointer on success, nullptr on failure
 */
template <typename BootstrapResult>
GLFWwindow* CreateGlfwWindow(const AppSettings& app_settings, BootstrapResult& result,
                             const char* window_title = APP_DISPLAY_NAME) {
  GLFWwindow* window = glfwCreateWindow(app_settings.windowWidth, app_settings.windowHeight,
                                        window_title, nullptr, nullptr);
  if (window == nullptr) {
    LOG_ERROR("Failed to create GLFW window");
    glfwTerminate();
    return nullptr;
  }
  result.window = window;
  return window;
}

/**
 * @brief Setup window resize callback using RendererInterface
 *
 * Configures GLFW window resize callback to update the renderer and track
 * window dimensions. Uses RendererInterface for platform-agnostic resize handling.
 *
 * @tparam RendererType Renderer type (must implement RendererInterface)
 * @param window GLFW window handle
 * @param renderer Renderer instance to handle resize events
 * @param last_width Pointer to store last known window width
 * @param last_height Pointer to store last known window height
 */
template <typename RendererType>
void SetupWindowResizeCallback(
  GLFWwindow* window, RendererType* renderer,
  int* last_width,  // NOLINT(readability-non-const-parameter) NOSONAR(cpp:S995) - output parameter,
                    // written in callback
  int* last_height) {  // NOLINT(readability-non-const-parameter) NOSONAR(cpp:S995) - output parameter
  // WindowResizeData is stored per-window in GLFW user pointer
  // Static is safe here because Initialize() is only called once per application lifecycle
  // The data persists for the window's lifetime and is cleaned up in Cleanup()
  struct WindowResizeData {
    RendererInterface* renderer;
    int* last_window_width;
    int* last_window_height;
  };

  static WindowResizeData resize_data;
  resize_data.renderer = renderer;  // RendererType* converts to RendererInterface*
  resize_data.last_window_width = last_width;
  resize_data.last_window_height = last_height;

  glfwSetWindowSizeCallback(window, [](GLFWwindow* w, int width, int height) {
    auto data = static_cast<WindowResizeData*>(glfwGetWindowUserPointer(w));
    if (data != nullptr && data->renderer != nullptr) {
      data->renderer->HandleResize(width, height);
    }
    if (data != nullptr) {
      if (data->last_window_width != nullptr) {
        *data->last_window_width = width;
      }
      if (data->last_window_height != nullptr) {
        *data->last_window_height = height;
      }
    }
  });

  glfwSetWindowUserPointer(window, &resize_data);
}

/**
 * @brief Initialize ImGui context with common configuration
 *
 * Creates ImGui context and sets up common configuration flags:
 * - NavEnableKeyboard: Enable keyboard navigation
 * - DockingEnable: Enable docking windows
 * - ViewportsEnable: Enable multi-viewport support
 *
 * This function handles the common parts of ImGui initialization that are
 * identical across all platforms. Platform-specific backend initialization
 * should be done separately.
 */
inline void InitializeImGuiContext() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  auto flags = static_cast<unsigned int>(io.ConfigFlags);
  flags |= static_cast<unsigned int>(ImGuiConfigFlags_NavEnableKeyboard);
  flags |= static_cast<unsigned int>(ImGuiConfigFlags_DockingEnable);
  flags |= static_cast<unsigned int>(ImGuiConfigFlags_ViewportsEnable);
  io.ConfigFlags = static_cast<ImGuiConfigFlags>(flags);
  ImGui::StyleColorsDark();
}

/**
 * @brief Apply common ImGui color settings shared across platforms
 *
 * Configures color distinctions between input fields and headers/buttons
 * to improve visual clarity, and adds borders to input fields.
 * This is shared between Windows and Linux style configurations.
 *
 * @param style ImGui style to modify
 */
// Obsolete style functions removed - replaced by ui::Theme

/**
 * @brief Build error message for exception with prefix
 *
 * Helper function to build error messages with a consistent format.
 *
 * @param prefix Error type prefix (e.g., "Memory allocation failure", "Runtime error")
 * @param what Exception message from e.what()
 * @param suffix Optional suffix to append (e.g., error code)
 * @return Formatted error message string
 */
inline std::string BuildErrorMessage(const char* prefix, const char* what,
                                     std::string_view suffix = std::string_view()) {
  std::string message = std::string(prefix) + " during initialization: " + what;
  if (!suffix.empty()) {
    message += suffix;
  }
  return message;
}

/**
 * @brief Build error message for bad_alloc exception
 *
 * @param e The bad_alloc exception
 * @return Formatted error message string
 */
inline std::string BuildBadAllocErrorMessage(const std::bad_alloc& e) {
  (void)e;  // Suppress unused variable warning in Release mode
  return BuildErrorMessage("Memory allocation failure", e.what());
}

/**
 * @brief Build error message for runtime_error exception
 *
 * @param e The runtime_error exception
 * @return Formatted error message string
 */
inline std::string BuildRuntimeErrorMessage(const std::runtime_error& e) {
  (void)e;  // Suppress unused variable warning in Release mode
  return BuildErrorMessage("Runtime error", e.what());
}

/**
 * @brief Build error message for system_error exception
 *
 * @param e The system_error exception
 * @return Formatted error message string
 */
inline std::string BuildSystemErrorMessage(const std::system_error& e) {
  const std::string suffix = " (code: " + std::to_string(e.code().value()) + ")";
  return BuildErrorMessage("System error", e.what(), suffix);
}

/**
 * @brief Build error message for invalid_argument exception
 *
 * @param e The invalid_argument exception
 * @return Formatted error message string
 */
inline std::string BuildInvalidArgumentErrorMessage(const std::invalid_argument& e) {
  return BuildErrorMessage("Invalid argument", e.what());
}

/**
 * @brief Build error message for logic_error exception
 *
 * @param e The logic_error exception
 * @return Formatted error message string
 */
inline std::string BuildLogicErrorMessage(const std::logic_error& e) {
  return BuildErrorMessage("Logic error", e.what());
}

/**
 * @brief Build error message for generic exception
 *
 * @param e The exception
 * @return Formatted error message string
 */
inline std::string BuildExceptionErrorMessage(const std::exception& e) {
  return BuildErrorMessage("Exception", e.what());
}

/**
 * @brief Get error message for unknown exception (catch-all)
 *
 * @return Error message string for unknown exceptions
 */
inline std::string GetUnknownExceptionMessage() {
  return "Unknown exception during initialization - possible memory corruption or non-standard "
         "exception";
}

/**
 * @brief Common ImGui and GLFW cleanup sequence
 *
 * Performs the common cleanup steps shared across all platforms:
 * - Shutdown ImGui renderer backend (if renderer exists)
 * - Shutdown ImGui GLFW backend
 * - Destroy ImGui context
 * - Cleanup renderer resources (if renderer exists)
 * - Destroy GLFW window (if window exists)
 * - Terminate GLFW
 *
 * Platform-specific cleanup (e.g., UsnMonitor, COM, OpenGL3, field nullification)
 * should be done before or after calling this function.
 *
 * @tparam BootstrapResult Platform-specific bootstrap result type (must inherit from
 * AppBootstrapResultBase)
 * @param result Bootstrap result containing resources to cleanup
 */
template <typename BootstrapResult>
void CleanupImGuiAndGlfw(BootstrapResult& result) {
  // Shutdown ImGui renderer backend (if renderer exists)
  if (result.renderer) {
    result.renderer->ShutdownImGui();
  }

  // Shutdown ImGui GLFW backend (common across all platforms)
  ImGui_ImplGlfw_Shutdown();

  // Destroy ImGui context
  ImGui::DestroyContext();

  // Cleanup renderer resources (if renderer exists)
  if (result.renderer) {
    result.renderer->Cleanup();
  }

  // Destroy GLFW window (if window exists)
  if (result.window) {
    glfwDestroyWindow(result.window);
    result.window = nullptr;
  }

  // Terminate GLFW (common across all platforms)
  glfwTerminate();
}

}  // namespace AppBootstrapCommon
