/**
 * @file AppBootstrap_mac.mm
 * @brief Implementation of macOS-specific application initialization and
 * cleanup
 *
 * This module implements the complete macOS application initialization
 * sequence:
 * - GLFW window creation
 * - Metal device and command queue creation
 * - ImGui context and backend initialization
 * - Metal layer setup on NSWindow
 * - Settings loading with command-line overrides
 * - Index loading from file (if requested)
 *
 * All initialization is wrapped in exception handling to ensure proper cleanup
 * on failure. The window resize callback is configured to update Metal layer
 * drawable size and track window dimensions.
 */

#include "AppBootstrap_mac.h"

// GLFW defines must be set before including AppBootstrapCommon.h (which
// includes glfw3.h)
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA

#include "FontUtils_mac.h"
#include "MetalManager.h"
#include "core/AppBootstrapCommon.h"
#include "core/CommandLineArgs.h"
#include "core/Settings.h"
#include "crawler/FolderCrawler.h"
#include "index/FileIndex.h"
#include "index/IndexFromFilePopulator.h"
#include "platform/FileOperations.h" // Common interface header
#include "utils/CpuFeatures.h"
#include "utils/Logger.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <thread>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_metal.h"

// ImGui Test Engine: all includes and usage must stay inside ENABLE_IMGUI_TEST_ENGINE to avoid
// ambiguous declarations when the flag is off (guarded per TU).
#ifdef ENABLE_IMGUI_TEST_ENGINE
#include "imgui_te_engine.h"
#include "imgui_te_ui.h"
#endif  // ENABLE_IMGUI_TEST_ENGINE

// GLFW includes (native Cocoa access for Metal layer)
// Note: GLFW defines are set before AppBootstrapCommon.h include above
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <cassert>

#ifdef __APPLE__

namespace AppBootstrapMac {

AppBootstrapResultMac Initialize(const CommandLineArgs &cmd_args,
                                 FileIndex &file_index, int &last_window_width,
                                 int &last_window_height) {
  // Implementation details:
  // 1. Initialize GLFW with error callback
  // 2. Log CPU information (Debug and Release builds)
  // 3. Load index from file (if requested)
  // 4. Load settings and apply command-line overrides
  // 5. Create GLFW window
  // 6. Initialize Metal device and command queue
  // 7. Setup Metal layer on NSWindow
  // 8. Initialize ImGui context and backends
  // 9. Configure ImGui style
  LOG_INFO("AppBootstrapMac::Initialize() entry");
  AppBootstrapResultMac result;
  result.file_index = &file_index;
  result.last_window_width = &last_window_width;
  result.last_window_height = &last_window_height;

  LOG_INFO("Application starting (macOS)");

  // Initialize GLFW
  AppBootstrapCommon::SetupGlfwErrorCallback();

  if (!glfwInit()) {
    LOG_ERROR("Failed to initialize GLFW");
    return std::move(result); // IsValid() will be false
  }

  // Log CPU information (Debug and Release builds)
  AppBootstrapCommon::LogCpuInformation();

  // Helper lambda for cleanup on exception (must be visible to catch blocks)
  auto cleanup_on_exception = [&result]() {
    // Cleanup any partially initialized resources in reverse order
    // Note: ImGui shutdown functions are safe to call even if initialization
    // didn't complete Common ImGui and GLFW cleanup (handles renderer shutdown
    // via MetalManager)
    AppBootstrapCommon::CleanupImGuiAndGlfw(result);
  };

  // Load application settings and initialize graphics subsystems.
  // Wrap in try-catch to handle exceptions gracefully.
  try {
    // Load application settings (window size, font options, saved searches).
    static AppSettings app_settings;
    LoadSettings(app_settings);

    // Apply command line overrides
    AppBootstrapCommon::ApplyCommandLineOverrides(cmd_args, app_settings);

    // If indexing from file, do that now (before window creation)
    if (!AppBootstrapCommon::LoadIndexFromFile(cmd_args.index_from_file,
                                               file_index, true)) {
      glfwTerminate();
      return std::move(result); // IsValid() will be false
    }

    // Create GLFW window with graphics context (use size from settings)
    // NOTE: Window must be created BEFORE showing folder picker dialog on macOS
    // because runModal requires a proper Cocoa application context
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window =
        AppBootstrapCommon::CreateGlfwWindow(app_settings, result);
    if (window == nullptr) {
      return std::move(result); // IsValid() will be false
    }

    // Initialize last-known window size (will be updated on window resize
    // callback)
    last_window_width = app_settings.windowWidth;
    last_window_height = app_settings.windowHeight;

    // Initialize Metal manager (must be declared before resize callback setup)
    static MetalManager metal_manager;

    // Set window resize callback to handle Metal layer drawable size updates
    // and track window dimensions for settings persistence
    struct WindowResizeData {
      MetalManager *metal_manager;
      int *last_window_width;
      int *last_window_height;
    };
    static WindowResizeData resize_data;
    resize_data.metal_manager = &metal_manager;
    resize_data.last_window_width = &last_window_width;
    resize_data.last_window_height = &last_window_height;

    glfwSetWindowSizeCallback(window, [](GLFWwindow *w, int width, int height) {
      WindowResizeData *data =
          static_cast<WindowResizeData *>(glfwGetWindowUserPointer(w));
      if (data != nullptr && data->metal_manager != nullptr) {
        // Use framebuffer size (pixels) for Metal layer, not window size
        // (points) This is critical on Retina displays where framebuffer size
        // is 2x window size
        int framebuffer_width, framebuffer_height;
        glfwGetFramebufferSize(w, &framebuffer_width, &framebuffer_height);
        data->metal_manager->HandleResize(framebuffer_width,
                                          framebuffer_height);
      }
      if (data != nullptr) {
        // Store window size (points) for settings persistence
        if (data->last_window_width != nullptr)
          *data->last_window_width = width;
        if (data->last_window_height != nullptr)
          *data->last_window_height = height;
      }
    });

    // Store resize data in window user pointer
    glfwSetWindowUserPointer(window, &resize_data);

    // Initialize Metal manager
    result.renderer = &metal_manager;

    if (!metal_manager.Initialize(window)) {
      LOG_ERROR("Failed to initialize Metal");
      glfwDestroyWindow(window);
      glfwTerminate();
      return std::move(result); // IsValid() will be false
    }

    // Set initial layer size (must be done after Initialize)
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    metal_manager.HandleResize(width, height);

    // Setup Dear ImGui context
    AppBootstrapCommon::InitializeImGuiContext();

    ImGuiIO &io = ImGui::GetIO();
    const bool viewports_enabled =
        (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
    ui::Theme::Apply(app_settings.themeId, viewports_enabled);

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOther(window, true); // true = install callbacks
    if (!metal_manager.InitializeImGui()) {
      LOG_ERROR("Failed to initialize ImGui renderer backend");
      ImGui_ImplGlfw_Shutdown();
      ImGui::DestroyContext();
      metal_manager.Cleanup();
      glfwDestroyWindow(window);
      glfwTerminate();
      return std::move(result); // IsValid() will be false
    }

    // Apply font settings (family, size, scale) and rebuild font atlas.
    ApplyFontSettings(app_settings);

    LOG_INFO("ImGui initialized successfully");

#ifdef ENABLE_IMGUI_TEST_ENGINE
    // Create and start ImGui Test Engine (optional; for in-process UI tests)
    ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
    if (engine != nullptr) {
      ImGuiTestEngineIO& test_io = ImGuiTestEngine_GetIO(engine);
      test_io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
      test_io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
      ImGuiTestEngine_Start(engine, ImGui::GetCurrentContext());
      // Crash handler: compile-time gated by IMGUI_TEST_ENGINE_INSTALL_CRASH_HANDLER (CMake option).
      // Enable: build with -DIMGUI_TEST_ENGINE_INSTALL_CRASH_HANDLER=ON (default when test engine is ON).
      // Disable: build with -DIMGUI_TEST_ENGINE_INSTALL_CRASH_HANDLER=OFF (e.g. for CI or custom handler).
      // No runtime flag or env var; installation is automatic when this block is compiled in.
#ifdef IMGUI_TEST_ENGINE_INSTALL_CRASH_HANDLER
      ImGuiTestEngine_InstallDefaultCrashHandler();
#endif  // IMGUI_TEST_ENGINE_INSTALL_CRASH_HANDLER
      result.imgui_test_engine = engine;
    } else {
      LOG_ERROR_BUILD("ImGui Test Engine: CreateContext failed; continuing without test engine.");
    }
#endif  // ENABLE_IMGUI_TEST_ENGINE

    // Make window visible and bring to front before showing dialog
    // Show the window
    glfwShowWindow(window);

    // If no index file was provided, check if --crawl-folder is set or if
    // auto-crawl will handle it. The actual crawling is performed
    // asynchronously via IndexBuilder infrastructure in RunApplication(), so we
    // no longer block here. Auto-crawl will use settings folder or HOME folder
    // if no --crawl-folder is provided.
    LOG_INFO_BUILD("Checking index options - index_from_file: '"
                   << cmd_args.index_from_file << "', crawl_folder: '"
                   << cmd_args.crawl_folder << "'");
    if (cmd_args.index_from_file.empty() && cmd_args.crawl_folder.empty()) {
      // Allow auto-crawl: will use settings->crawlFolder or HOME folder
      LOG_INFO_BUILD("No index file or --crawl-folder provided, auto-crawl "
                     "will use settings folder or HOME folder");
    } else if (!cmd_args.index_from_file.empty()) {
      LOG_INFO_BUILD("Index file provided, skipping FolderCrawler: "
                     << cmd_args.index_from_file);
    }

    // Store settings pointer
    result.settings = &app_settings;
  } catch (const std::bad_alloc &e) {
    LOG_ERROR_BUILD(AppBootstrapCommon::BuildBadAllocErrorMessage(e));
    cleanup_on_exception();
    return result; // IsValid() will be false
  } catch (const std::runtime_error &e) {
    LOG_ERROR_BUILD(AppBootstrapCommon::BuildRuntimeErrorMessage(e));
    cleanup_on_exception();
    return result; // IsValid() will be false
  } catch (const std::exception &e) {
    LOG_ERROR_BUILD(AppBootstrapCommon::BuildExceptionErrorMessage(e));
    LOG_ERROR_BUILD("Exception type: " << typeid(e).name());
    cleanup_on_exception();
    return result; // IsValid() will be false
  } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all needed for initialization: must handle all exceptions including non-standard ones
    LOG_ERROR(AppBootstrapCommon::GetUnknownExceptionMessage());
    cleanup_on_exception();
    return result; // IsValid() will be false
  }

  // Final validation before returning
  LOG_INFO("AppBootstrapMac::Initialize() about to return");
  if (!result.IsValid()) {
    LOG_ERROR("AppBootstrapResult is invalid after initialization");
    LOG_ERROR_BUILD(
        "window=" << (result.window ? "valid" : "null")
                  << ", renderer=" << (result.renderer ? "valid" : "null")
                  << ", settings=" << (result.settings ? "valid" : "null")
                  << ", file_index=" << (result.file_index ? "valid" : "null"));
    LOG_INFO("AppBootstrapMac::Initialize() returning with invalid result");
  } else {
    LOG_INFO("AppBootstrapResult is valid - initialization successful");
    LOG_INFO("AppBootstrapMac::Initialize() returning successfully");
  }

  return std::move(result);
}

void Cleanup(AppBootstrapResultMac &result) {
  // Cleanup order (reverse of initialization):
  // 1. Stop ImGui Test Engine (if enabled)
  // 2. Shutdown ImGui renderer backend, GLFW, destroy ImGui context (CleanupImGuiAndGlfw)
  // 3. Destroy Test Engine context (after ImGui context destroyed)
  // 4. Cleanup Metal manager, GLFW window, terminate GLFW
  // 5. Clear pointers

#ifdef ENABLE_IMGUI_TEST_ENGINE
  ImGuiTestEngine* engine = result.imgui_test_engine;
  if (engine != nullptr) {
    ImGuiTestEngine_Stop(engine);
  }
  AppBootstrapCommon::CleanupImGuiAndGlfw(result);
  if (engine != nullptr) {
    ImGuiTestEngine_DestroyContext(engine);
    result.imgui_test_engine = nullptr;
  }
#else
  AppBootstrapCommon::CleanupImGuiAndGlfw(result);
#endif  // ENABLE_IMGUI_TEST_ENGINE

  // Platform-specific: Clear pointers
  result.renderer = nullptr;
  result.settings = nullptr;
  result.file_index = nullptr;
  result.last_window_width = nullptr;
  result.last_window_height = nullptr;
}

} // namespace AppBootstrapMac

#endif  // __APPLE__
