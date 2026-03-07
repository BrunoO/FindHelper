#pragma once

/**
 * @file AppBootstrap_mac.h
 * @brief macOS-specific application initialization and cleanup
 *
 * This module centralizes all macOS-specific initialization logic:
 * - GLFW window management
 * - Metal device and command queue creation
 * - ImGui context and backend initialization
 * - Metal layer setup on NSWindow
 * - Settings loading and command-line overrides
 * - Index loading from file (if requested)
 * - Window size management
 *
 * All initialization is wrapped in exception handling to ensure proper cleanup
 * on failure. The module returns an AppBootstrapResultMac struct containing all
 * initialized resources, which can be checked with IsValid().
 */

#include "core/AppBootstrapResultBase.h"

#include <memory>
#include <string>

// Forward declarations
struct CommandLineArgs;
struct AppSettings;
class FileIndex;
struct GLFWwindow;

// Include UsnMonitor.h for unique_ptr destructor (even though monitor is nullptr on macOS)
// This ensures the unique_ptr in AppBootstrapResultBase can be properly destroyed
#include "usn/UsnMonitor.h"

#ifdef __APPLE__
/**
 * @struct AppBootstrapResultMac
 * @brief macOS-specific bootstrap result, inherits common fields from base
 *
 * All common fields (window, renderer, settings, etc.) are inherited from
 * AppBootstrapResultBase. macOS has no additional platform-specific fields.
 */
struct AppBootstrapResultMac : public AppBootstrapResultBase {
  // No macOS-specific fields needed - all fields are in base class
};

/**
 * @namespace AppBootstrapMac
 * @brief Handles macOS-specific application initialization and cleanup
 */
// NOLINTNEXTLINE(readability-identifier-naming) - platform namespace name
namespace AppBootstrapMac {
  /**
   * @brief Initialize all macOS-specific subsystems
   *
   * Performs complete application initialization:
   * - GLFW (window management)
   * - Metal device and command queue creation
   * - ImGui context and backend initialization
   * - Metal layer setup on NSWindow
   * - Settings loading and command-line overrides
   * - Index loading from file (if requested via command line)
   *
   * All initialization is wrapped in exception handling. On failure, partially
   * initialized resources are cleaned up and a result with IsValid() == false
   * is returned.
   *
   * @param cmd_args Command line arguments (for overrides and index-from-file)
   * @param file_index File index to populate (if indexing from file)
   * @param last_window_width Reference to store last known window width (updated on resize)
   * @param last_window_height Reference to store last known window height (updated on resize)
   * @return AppBootstrapResultMac with all initialized resources, or invalid result on failure
   */
  AppBootstrapResultMac Initialize(const CommandLineArgs &cmd_args,
                                   FileIndex &file_index,
                                   int &last_window_width,
                                   int &last_window_height);
  
  /**
   * @brief Cleanup all initialized resources in reverse order
   *
   * Properly shuts down all subsystems in reverse order of initialization:
   * - Shutdown ImGui backends and context
   * - Release Metal resources
   * - Destroy GLFW window and terminate GLFW
   *
   * @param result AppBootstrapResultMac containing resources to cleanup (modified in place)
   */
  void Cleanup(AppBootstrapResultMac &result);
}

#endif  // __APPLE__

