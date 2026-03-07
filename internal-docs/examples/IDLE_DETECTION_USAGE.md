# Idle Detection Usage Examples

This document shows how to use the `SystemIdleDetector` utility to detect both process-level and system-level idle time.

## Overview

The `SystemIdleDetector` provides two types of idle detection:

1. **Process-level idle**: Time since last user interaction within this application
2. **System-level idle**: Time since last system-wide user input (mouse/keyboard) across the entire computer

## Basic Usage

### Check if System Has Been Idle for 5 Minutes

```cpp
#include "utils/SystemIdleDetector.h"

// Check if the entire computer has been idle for 5 minutes
constexpr double kFiveMinutes = 5.0 * 60.0;  // 300 seconds
if (system_idle_detector::IsSystemIdleFor(kFiveMinutes)) {
    // System has been idle for at least 5 minutes
    // Perform background tasks, power-saving operations, etc.
    LOG_INFO("System has been idle for 5+ minutes");
}
```

### Check if Process Has Been Idle for 5 Minutes

```cpp
#include "utils/SystemIdleDetector.h"
#include <chrono>

// Track last interaction time (typically in your Application class)
std::chrono::steady_clock::time_point last_interaction_time = 
    std::chrono::steady_clock::now();

// Later, check if process has been idle for 5 minutes
constexpr double kFiveMinutes = 5.0 * 60.0;  // 300 seconds
if (system_idle_detector::IsProcessIdleFor(last_interaction_time, kFiveMinutes)) {
    // This application has been idle for at least 5 minutes
    LOG_INFO("Process has been idle for 5+ minutes");
}
```

### Get Exact Idle Time

```cpp
// Get system idle time in seconds
double system_idle_seconds = system_idle_detector::GetSystemIdleTimeSeconds();
if (system_idle_seconds >= 0.0) {
    LOG_INFO("System idle time: " << system_idle_seconds << " seconds");
} else {
    // System idle detection not available (e.g., Linux without X11)
    LOG_WARNING("System idle detection not available");
}

// Get process idle time in seconds
double process_idle_seconds = 
    system_idle_detector::GetProcessIdleTimeSeconds(last_interaction_time);
LOG_INFO("Process idle time: " << process_idle_seconds << " seconds");
```

## Integration with Application Class

Here's how you could integrate system-level idle detection into the existing `Application` class:

```cpp
// In Application::Run() or similar method
constexpr double kSystemIdleThresholdSeconds = 5.0 * 60.0;  // 5 minutes
constexpr double kProcessIdleThresholdSeconds = 5.0 * 60.0;  // 5 minutes

// Check both system and process idle
bool system_idle = system_idle_detector::IsSystemIdleFor(kSystemIdleThresholdSeconds);
bool process_idle = system_idle_detector::IsProcessIdleFor(
    last_interaction_time, kProcessIdleThresholdSeconds);

if (system_idle && process_idle) {
    // Both system and process have been idle for 5+ minutes
    // Perform aggressive power-saving, background maintenance, etc.
    LOG_INFO_BUILD("System and process idle for 5+ minutes - entering deep idle mode");
}
```

## Platform-Specific Notes

### Windows
- Uses `GetLastInputInfo()` from `user32.dll`
- Returns accurate system-wide idle time
- Always available on Windows

### macOS
- Uses `CGEventSourceSecondsSinceLastEventType()` from CoreGraphics
- Returns accurate system-wide idle time
- Always available on macOS

### Linux
- Uses X11 XScreenSaver extension
- Returns `-1.0` if X11 is not available (e.g., Wayland, headless server)
- Requires X11 and XScrnSaver libraries
- **Fallback**: Use process-level idle detection if system idle detection is unavailable

## Example: Background Maintenance During Idle

```cpp
void Application::PerformIdleMaintenance() {
    constexpr double kIdleThresholdSeconds = 5.0 * 60.0;  // 5 minutes
    
    // Only perform maintenance if both system and process are idle
    bool system_idle = system_idle_detector::IsSystemIdleFor(kIdleThresholdSeconds);
    bool process_idle = system_idle_detector::IsProcessIdleFor(
        last_interaction_time_, kIdleThresholdSeconds);
    
    if (system_idle && process_idle) {
        // Perform background maintenance tasks
        file_index_.PerformMaintenance();
        // ... other maintenance tasks
    }
}
```

## Error Handling

The functions return `-1.0` on error or unsupported platforms. Always check for this:

```cpp
double idle_seconds = system_idle_detector::GetSystemIdleTimeSeconds();
if (idle_seconds < 0.0) {
    // System idle detection not available
    // Fall back to process-level idle detection
    idle_seconds = system_idle_detector::GetProcessIdleTimeSeconds(last_interaction_time);
}
```

## Performance Considerations

- **System idle detection**: Platform API calls (typically very fast, < 1ms)
- **Process idle detection**: Simple time calculation (negligible overhead)
- Both functions are safe to call every frame if needed
- Consider caching results if checking very frequently (e.g., every frame)
