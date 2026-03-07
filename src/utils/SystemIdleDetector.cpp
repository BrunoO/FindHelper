#include "SystemIdleDetector.h"

#include "utils/Logger.h"

#include <chrono>
#include <cmath>
#include <memory>

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include "utils/LoggingUtils.h"
// Link with user32.lib for GetLastInputInfo
#pragma comment(lib, "user32.lib")
#elif defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#elif defined(__linux__)
#include <X11/extensions/scrnsaver.h>
#include <X11/Xlib.h>
#include <cstdlib>
#endif  // _WIN32

namespace system_idle_detector {

double GetSystemIdleTimeSeconds() {
#ifdef _WIN32
  LASTINPUTINFO lii;
  lii.cbSize = sizeof(LASTINPUTINFO);
  
  if (!GetLastInputInfo(&lii)) {
    DWORD err = GetLastError();
    logging_utils::LogWindowsApiError("GetLastInputInfo",
                                      "Getting system idle time",
                                      err);
    return -1.0;  // Error getting system idle time
  }
  
  // GetLastInputInfo returns time in milliseconds since system boot
  // GetTickCount() can overflow after ~49 days, but subtraction handles it correctly
  // Convert to seconds
  DWORD current_tick = GetTickCount();
  DWORD idle_ms = current_tick - lii.dwTime;
  return static_cast<double>(idle_ms) / 1000.0;
  
#elif defined(__APPLE__)
  // CGEventSourceSecondsSinceLastEventType returns time since last event
  // kCGEventSourceStateHIDSystemState checks system-wide input (not just this app)
  // NOTE: This API requires Accessibility permissions on macOS. Without permissions,
  // it may return 0.0 or incorrect values. Check System Settings > Privacy & Security > Accessibility
  const double idle_seconds = CGEventSourceSecondsSinceLastEventType(
      kCGEventSourceStateHIDSystemState,
      kCGAnyInputEventType);
  
  // Return -1.0 if the function fails (shouldn't happen, but handle gracefully)
  if (std::isnan(idle_seconds) || std::isinf(idle_seconds) || idle_seconds < 0.0) {
    return -1.0;
  }
  
  // Log warning if idle time is suspiciously low (might indicate missing permissions)
  // Only log once per session to avoid spam
  if (static bool logged_permission_warning = false; !logged_permission_warning && idle_seconds == 0.0) {
    // This might indicate the API is not working due to missing permissions
    // However, 0.0 is also valid if user just interacted, so we can't be certain
    // We'll log a debug message but not treat it as an error
    LOG_INFO_BUILD("SystemIdleDetector: macOS system idle time is 0.0 seconds. "
                   "If idle detection doesn't work, check Accessibility permissions in System Settings.");
    logged_permission_warning = true;
  }
  
  return idle_seconds;
  
#elif defined(__linux__)
  // RAII wrapper for X11 Display - automatically closes on scope exit
  struct DisplayDeleter {
    void operator()(Display* d) const noexcept {
      if (d != nullptr) {
        XCloseDisplay(d);
      }
    }
  };
  using DisplayPtr = std::unique_ptr<Display, DisplayDeleter>;
  
  // Try X11 first (most common on Linux desktop)
  DisplayPtr display(XOpenDisplay(nullptr));
  if (display == nullptr) {
    // X11 not available (e.g., headless server, Wayland)
    // Fallback: return -1.0 to indicate system idle detection is not available
    return -1.0;
  }
  
  // Use XScreenSaver extension to get idle time
  int event_base = 0;
  if (int error_base = 0; !XScreenSaverQueryExtension(display.get(), &event_base, &error_base)) {
    return -1.0;  // XScreenSaver extension not available
  }
  
  // RAII wrapper for XScreenSaverInfo - automatically frees on scope exit
  struct XScreenSaverInfoDeleter {
    void operator()(XScreenSaverInfo* info) const noexcept {
      if (info != nullptr) {
        XFree(info);
      }
    }
  };
  using XScreenSaverInfoPtr = std::unique_ptr<XScreenSaverInfo, XScreenSaverInfoDeleter>;
  
  XScreenSaverInfoPtr info(XScreenSaverAllocInfo());
  if (info == nullptr) {
    return -1.0;  // Failed to allocate info structure
  }
  
  if (!XScreenSaverQueryInfo(display.get(), DefaultRootWindow(display.get()), info.get())) {
    return -1.0;  // Failed to query idle time
  }
  
  // XScreenSaverInfo.idle is in milliseconds
  double idle_seconds = static_cast<double>(info->idle) / 1000.0;
  
  return idle_seconds;
  
#else
  // Unsupported platform
  return -1.0;
#endif  // _WIN32
}

bool IsSystemIdleFor(double idle_threshold_seconds) {
  const double idle_seconds = GetSystemIdleTimeSeconds();
  
  // If system idle detection is not available, return false (assume not idle)
  if (idle_seconds < 0.0) {
    return false;
  }
  
  return idle_seconds >= idle_threshold_seconds;
}

double GetProcessIdleTimeSeconds(const std::chrono::steady_clock::time_point& last_interaction_time) {
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(now - last_interaction_time).count();
}

bool IsProcessIdleFor(const std::chrono::steady_clock::time_point& last_interaction_time,
                      double idle_threshold_seconds) {
  // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - initialized by GetProcessIdleTimeSeconds() below
  const double idle_seconds = GetProcessIdleTimeSeconds(last_interaction_time);
  return idle_seconds >= idle_threshold_seconds;
}

} // namespace system_idle_detector
