#pragma once

/**
 * @file AppBootstrap_win.h
 * @brief Windows-specific application initialization and cleanup
 *
 * This module centralizes all Windows-specific initialization logic:
 * - GLFW window management
 * - COM/OLE initialization (for Shell APIs and drag-and-drop)
 * - Administrator privilege checking and elevation
 * - Index loading from file (if requested)
 * - Settings loading and command-line overrides
 * - DirectX 11 and ImGui initialization
 * - UsnMonitor startup (if not indexing from file)
 *
 * All initialization is wrapped in exception handling to ensure proper cleanup
 * on failure. The module returns an AppBootstrapResult struct containing all
 * initialized resources, which can be checked with IsValid().
 */

#ifdef _WIN32
#include <windows.h>
#endif  // _WIN32

#include "core/AppBootstrapResultBase.h"

#include <memory>
#include <string>

// Forward declarations
struct CommandLineArgs;
struct AppSettings;
class FileIndex;
struct GLFWwindow;

/**
 * @struct AppBootstrapResult
 * @brief Windows-specific bootstrap result, inherits common fields from base
 *
 * Adds Windows-specific fields (COM initialization) to the common base structure.
 * All common fields (window, renderer, settings, etc.) are inherited from
 * AppBootstrapResultBase.
 */
struct AppBootstrapResult : public AppBootstrapResultBase {
  bool com_initialized = false;  // Windows-specific: COM/OLE initialization status
  bool security_fatal_error = false;  // Security: fatal error that requires application exit
  bool running_without_elevation = false;  // Graceful degradation: running without admin rights
};

/**
 * @namespace AppBootstrap
 * @brief Handles Windows-specific application initialization and cleanup
 */
namespace AppBootstrap {  // NOLINT(readability-identifier-naming) - platform namespace name
  /**
   * @brief Initialize all Windows-specific subsystems
   *
   * Performs complete application initialization:
   * - GLFW (window management with DPI awareness)
   * - COM/OLE (for Shell APIs and drag-and-drop)
   * - Administrator privilege check (restarts as admin if needed)
   * - Index loading from file (if requested via command line)
   * - Settings loading and command-line overrides
   * - DirectX 11 and ImGui initialization
   * - UsnMonitor startup (if not indexing from file)
   *
   * All initialization is wrapped in exception handling. On failure, partially
   * initialized resources are cleaned up and a result with IsValid() == false
   * is returned.
   *
   * @param cmd_args Command line arguments (for overrides and index-from-file)
   * @param file_index File index to populate (if indexing from file)
   * @param last_window_width Reference to store last known window width (updated on resize)
   * @param last_window_height Reference to store last known window height (updated on resize)
   * @return AppBootstrapResult with all initialized resources, or invalid result on failure
   */
  AppBootstrapResult Initialize(const CommandLineArgs &cmd_args,
                                FileIndex &file_index,
                                int &last_window_width,
                                int &last_window_height);

  /**
   * @brief Cleanup all initialized resources in reverse order
   *
   * Properly shuts down all subsystems in reverse order of initialization:
   * - Stop UsnMonitor
   * - Shutdown ImGui backends and context
   * - Cleanup DirectX resources
   * - Destroy GLFW window and terminate GLFW
   * - Uninitialize COM/OLE
   *
   * @param result AppBootstrapResult containing resources to cleanup (modified in place)
   */
  void Cleanup(AppBootstrapResult &result);
}
