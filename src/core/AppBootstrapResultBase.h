#pragma once

/**
 * @file AppBootstrapResultBase.h
 * @brief Base structure for platform-specific bootstrap results
 *
 * This base class contains all common fields shared across platforms.
 * Platform-specific bootstrap results inherit from this and add
 * platform-specific fields (e.g., COM initialization on Windows).
 *
 * DESIGN:
 * - Common fields: window, renderer, settings, file_index, monitor, window dimensions
 * - Platform-specific fields: Added in derived classes (e.g., com_initialized on Windows)
 * - Virtual destructor: Allows proper cleanup through base pointer
 * - IsValid(): Checks common fields for all platforms
 */

#include <functional>
#include <memory>

// Forward declarations
struct GLFWwindow;
struct AppSettings;
class FileIndex;
class RendererInterface;

// UsnMonitor forward declaration
// Note: unique_ptr requires full type for destructor, but since monitor is always
// nullptr on macOS/Linux, we can use forward declaration. On Windows, UsnMonitor.h
// is included in AppBootstrap_win.h before AppBootstrapResultBase.h is used.
class UsnMonitor;

#ifdef ENABLE_IMGUI_TEST_ENGINE
struct ImGuiTestEngine;
#endif  // ENABLE_IMGUI_TEST_ENGINE

/**
 * @struct AppBootstrapResultBase
 * @brief Base structure with common bootstrap fields across all platforms
 *
 * All platform-specific bootstrap results inherit from this base class.
 * This eliminates duplication and allows a single Application constructor.
 */
struct AppBootstrapResultBase {
  // Common fields (all platforms)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  GLFWwindow* window = nullptr;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  RendererInterface* renderer = nullptr;  // Platform-specific renderer (DirectXManager, MetalManager, etc.)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  AppSettings* settings = nullptr;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  FileIndex* file_index = nullptr;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  std::unique_ptr<UsnMonitor> monitor = nullptr;  // nullptr on macOS/Linux (Windows-only)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  int* last_window_width = nullptr;   ///< Pointer to last known window width (updated on resize)
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  int* last_window_height = nullptr;   ///< Pointer to last known window height (updated on resize)

  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  std::function<void(const char*)> platform_exit_hook;  ///< Optional platform exit hook (e.g. Windows PGO flush). "shutdown" / "exception_exit".

#ifdef ENABLE_IMGUI_TEST_ENGINE
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
  ImGuiTestEngine* imgui_test_engine = nullptr;  ///< Created in bootstrap, destroyed in platform Cleanup
#endif  // ENABLE_IMGUI_TEST_ENGINE

  /**
   * @brief Default constructor
   */
  AppBootstrapResultBase() = default;
  
  /**
   * @brief Virtual destructor for proper cleanup through base pointer
   */
  virtual ~AppBootstrapResultBase() = default;
  
  // Enable move semantics (copy is disabled due to unique_ptr)
  AppBootstrapResultBase(AppBootstrapResultBase&&) = default;
  AppBootstrapResultBase& operator=(AppBootstrapResultBase&&) = default;
  
  // Disable copy (unique_ptr is not copyable)
  AppBootstrapResultBase(const AppBootstrapResultBase&) = delete;
  AppBootstrapResultBase& operator=(const AppBootstrapResultBase&) = delete;
  
  /**
   * @brief Check if bootstrap was successful
   * @return True if all critical resources were initialized (window, renderer, settings, file_index)
   */
  [[nodiscard]] bool IsValid() const {
    return window != nullptr && renderer != nullptr && 
           settings != nullptr && file_index != nullptr;
  }
};

