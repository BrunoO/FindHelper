/**
 * @file AppBootstrap_linux.cpp
 * @brief Implementation of Linux-specific application initialization and cleanup
 *
 * This module implements the complete Linux application initialization sequence:
 * - GLFW window creation with OpenGL context
 * - OpenGL context initialization
 * - ImGui context and backend initialization
 * - Settings loading with command-line overrides
 * - Index loading from file (if requested)
 *
 * All initialization is wrapped in exception handling to ensure proper cleanup
 * on failure. The window resize callback is configured to update OpenGL viewport
 * and track window dimensions.
 */

#include "AppBootstrap_linux.h"

// GLFW defines must be set before including AppBootstrapCommon.h (which includes glfw3.h)
#define GLFW_INCLUDE_NONE

#include <atomic>
#include <cassert>
#include <exception>
#include <stdexcept>
#include <system_error>
#include <thread>

// GLFW includes
// Note: GLFW_INCLUDE_NONE is set before AppBootstrapCommon.h include above
#include <GLFW/glfw3.h>

#include "core/AppBootstrapCommon.h"
#include "core/CommandLineArgs.h"
#include "core/Settings.h"
#include "crawler/FolderCrawler.h"
#include "gui/RendererInterface.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "index/FileIndex.h"
#include "index/IndexFromFilePopulator.h"
#include "platform/FileOperations.h"  // Common interface header
#include "platform/linux/FontUtils_linux.h"
#include "platform/linux/OpenGLManager.h"
#include "ui/Theme.h"
#include "utils/CpuFeatures.h"
#include "utils/Logger.h"

namespace {
  // Helper functions to reduce cognitive complexity of Initialize()



  void ConfigureOpenGLHints() {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    #ifdef __APPLE__
      glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    #endif  // __APPLE__
  }


  bool InitializeOpenGLAndWindow(GLFWwindow* window, OpenGLManager& opengl_manager,
                                  int& last_window_width, int& last_window_height,
                                  const AppSettings& app_settings) {
    last_window_width = app_settings.windowWidth;
    last_window_height = app_settings.windowHeight;

    AppBootstrapCommon::SetupWindowResizeCallback(window, &opengl_manager, &last_window_width, &last_window_height);

    if (!opengl_manager.Initialize(window)) {
      LOG_ERROR("Failed to initialize OpenGL");
      glfwDestroyWindow(window);
      glfwTerminate();
      return false;
    }

    int width = 0;  // Output parameter for glfwGetFramebufferSize
    int height = 0;  // Output parameter for glfwGetFramebufferSize

    glfwGetFramebufferSize(window, &width, &height);
    opengl_manager.HandleResize(width, height);

    return true;
  }

  bool InitializeImGuiBackend(GLFWwindow* window, OpenGLManager& opengl_manager) {
    AppBootstrapCommon::InitializeImGuiContext();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    if (!opengl_manager.InitializeImGui()) {
      LOG_ERROR("Failed to initialize ImGui OpenGL3 backend");
      return false;
    }

    return true;
  }

  void CleanupOnException(AppBootstrapResultLinux& result) {
    // Let CleanupImGuiAndGlfw shut down renderer (OpenGL3) and GLFW; do not
    // call ImGui_ImplOpenGL3_Shutdown() here to avoid double shutdown (segfault on exit).
    AppBootstrapCommon::CleanupImGuiAndGlfw(result);
  }

  // Helper function to handle initialization exceptions consistently
  void HandleInitializationException(
      std::string_view error_message,
      AppBootstrapResultLinux& result) {
    LOG_ERROR_BUILD(error_message);
    CleanupOnException(result);
  }

  // FolderCrawler is now driven asynchronously via IndexBuilder infrastructure.
  // This helper is kept only for documentation; synchronous crawling has been
  // moved out of the bootstrap path to avoid freezing the UI on startup.
} // anonymous namespace

namespace AppBootstrapLinux {

AppBootstrapResultLinux Initialize(const CommandLineArgs &cmd_args,
                                  FileIndex &file_index,
                                  int &last_window_width,
                                  int &last_window_height) {
  // Implementation details:
  // 1. Initialize GLFW with error callback
  // 2. Log CPU information (Debug and Release builds)
  // 3. Load index from file (if requested)
  // 4. Load settings and apply command-line overrides
  // 5. Create GLFW window with OpenGL context
  // 6. Initialize OpenGL manager
  // 7. Setup window resize callback
  // 8. Initialize ImGui context and backends
  // 9. Configure ImGui style
  AppBootstrapResultLinux result;
  result.file_index = &file_index;
  result.last_window_width = &last_window_width;
  result.last_window_height = &last_window_height;

  LOG_INFO("Application starting (Linux)");

  AppBootstrapCommon::SetupGlfwErrorCallback();

  if (glfwInit() == 0) {
    LOG_ERROR("Failed to initialize GLFW");
    return result;  // NOSONAR(cpp:S5274) - RVO (Return Value Optimization) applies to plain return statements in C++17
  }

  AppBootstrapCommon::LogCpuInformation();

  // Load index from file if specified (before window creation)
  if (!AppBootstrapCommon::LoadIndexFromFile(cmd_args.index_from_file, file_index, true)) {
    glfwTerminate();
    return result;  // NOSONAR(cpp:S5274) - RVO (Return Value Optimization) applies to plain return statements in C++17
  }

  try {
    static AppSettings app_settings;
    LoadSettings(app_settings);
    AppBootstrapCommon::ApplyCommandLineOverrides(cmd_args, app_settings);

    ConfigureOpenGLHints();
    GLFWwindow* window = AppBootstrapCommon::CreateGlfwWindow(app_settings, result);
    if (window == nullptr) {
      return result;  // NOSONAR(cpp:S5274) - RVO (Return Value Optimization) applies to plain return statements in C++17
    }

    static OpenGLManager opengl_manager;
    result.renderer = &opengl_manager;

    if (!InitializeOpenGLAndWindow(window, opengl_manager, last_window_width,
                                    last_window_height, app_settings)) {
      return result;  // NOSONAR(cpp:S5274) - RVO (Return Value Optimization) applies to plain return statements in C++17
    }

    if (!InitializeImGuiBackend(window, opengl_manager)) {
      HandleInitializationException(
          "Failed to initialize ImGui backend",
          result);
      return result;
    }

    {
      const ImGuiIO& io = ImGui::GetIO();
      const bool viewports_enabled =
          (static_cast<unsigned>(io.ConfigFlags) & static_cast<unsigned>(ImGuiConfigFlags_ViewportsEnable)) != 0;
      ui::Theme::Apply(app_settings.themeId, viewports_enabled);
    }

    ApplyFontSettings(app_settings);
    result.settings = &app_settings;
    glfwShowWindow(window);

    LOG_INFO_BUILD("Checking index options - index_from_file: '" << cmd_args.index_from_file
                   << "', crawl_folder: '" << cmd_args.crawl_folder << "'");
    // Synchronous FolderCrawler call has been removed. Initial index population
    // is now handled by the shared IndexBuilder infrastructure in RunApplication().
    // Auto-crawl will use settings folder or HOME folder if no --crawl-folder is provided.
    if (cmd_args.index_from_file.empty() && cmd_args.crawl_folder.empty()) {
      // Allow auto-crawl: will use settings->crawlFolder or HOME folder
      LOG_INFO_BUILD("No index file or --crawl-folder provided, auto-crawl will use settings folder or HOME folder");
    }

    LOG_INFO("Linux application initialization completed successfully");
    return result;  // NOSONAR(cpp:S5274) - RVO (Return Value Optimization) applies to plain return statements in C++17
  } catch (const std::system_error& e) {
    HandleInitializationException(
        AppBootstrapCommon::BuildSystemErrorMessage(e),
        result);
    return result;
  } catch (const std::bad_alloc& e) {
    HandleInitializationException(
        AppBootstrapCommon::BuildBadAllocErrorMessage(e),
        result);
    return result;
  } catch (const std::invalid_argument& e) {
    HandleInitializationException(
        AppBootstrapCommon::BuildInvalidArgumentErrorMessage(e),
        result);
    return result;
  } catch (const std::logic_error& e) {  // NOSONAR(cpp:S1181) - Part of exception hierarchy, catches logic_error and derived types (invalid_argument already caught above)
    HandleInitializationException(
        AppBootstrapCommon::BuildLogicErrorMessage(e),
        result);
    return result;
  } catch (const std::runtime_error& e) {  // NOSONAR(cpp:S1181) - Part of exception hierarchy, catches runtime_error and derived types (system_error already caught above)
    HandleInitializationException(
        AppBootstrapCommon::BuildRuntimeErrorMessage(e),
        result);
    return result;
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all safety net after specific exception types, needed for initialization error handling
    HandleInitializationException(
        AppBootstrapCommon::BuildExceptionErrorMessage(e),
        result);
    return result;
  } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all needed for initialization: must handle all exceptions including non-standard ones
    HandleInitializationException(
        AppBootstrapCommon::GetUnknownExceptionMessage(),
        result);
    return result;
  }
}

void Cleanup(AppBootstrapResultLinux &result) {
  // Cleanup in reverse order of initialization.
  // Do NOT call ImGui_ImplOpenGL3_Shutdown() here: CleanupImGuiAndGlfw calls
  // result.renderer->ShutdownImGui() which shuts down the OpenGL3 backend.
  // Calling it twice causes use-after-free and segmentation fault on exit.
  AppBootstrapCommon::CleanupImGuiAndGlfw(result);

  // Platform-specific: Clear result fields
  result.renderer = nullptr;
  result.settings = nullptr;
  result.file_index = nullptr;
}

} // namespace AppBootstrapLinux

