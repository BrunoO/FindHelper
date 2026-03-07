# Configurable Recrawl Idle Requirement - Implementation Plan

## Overview

Make the idle requirement for periodic recrawl configurable, allowing users to:
1. **Enable/disable** the idle requirement (recrawl immediately when interval elapses, or wait for idle)
2. **Configure** the idle threshold duration (how long system/process must be idle)

This gives users control over when recrawls happen - some may want immediate recrawls regardless of activity, while others prefer to wait for idle periods.

**Current Behavior:**
- Recrawl only happens when system AND process have been idle for 5 minutes
- This is hard-coded and cannot be changed

**Proposed Behavior:**
- User can enable/disable idle requirement via Settings UI
- If enabled, user can configure idle threshold (1-30 minutes)
- If disabled, recrawl happens immediately when interval elapses (if no crawl in progress)

---

## Requirements

### Functional Requirements

1. **Idle Requirement Toggle**
   - Add checkbox in Settings UI: "Only recrawl when idle"
   - Default: `true` (current behavior)
   - When `false`: Recrawl immediately when interval elapses (no idle check)

2. **Idle Threshold Configuration**
   - Add input field in Settings UI: "Idle threshold (minutes)"
   - Only visible when "Only recrawl when idle" is enabled
   - Range: 1-30 minutes
   - Default: 5 minutes (current value)
   - Tooltip: "How long system and application must be idle before recrawl"

3. **Behavior Changes**
   - **Idle requirement enabled**: Current behavior (check system/process idle)
   - **Idle requirement disabled**: Skip idle checks, recrawl immediately when interval elapses

### Non-Functional Requirements

1. **Backward Compatibility**
   - Existing settings files without new fields should default to current behavior (idle required, 5 minutes)
   - Migration not needed (defaults handle it)

2. **Performance**
   - No performance impact when idle requirement is disabled (skips idle detection calls)
   - Idle detection calls are already lightweight

3. **User Experience**
   - Clear UI labels and tooltips
   - Settings saved immediately when changed
   - Visual feedback when settings are applied

---

## Current State Analysis

### Existing Components

1. **Settings Storage**:
   - `AppSettings` struct in `src/core/Settings.h`
   - JSON persistence in `src/core/Settings.cpp`
   - Settings UI in `src/ui/SettingsWindow.cpp`

2. **Recrawl Logic**:
   - `CheckPeriodicRecrawl()` in `src/core/ApplicationLogic.cpp`
   - Currently hard-codes `kIdleThresholdSeconds = 5.0 * 60.0` (5 minutes)
   - Checks both system and process idle

3. **Idle Detection**:
   - `SystemIdleDetector` utility in `src/utils/SystemIdleDetector.h/cpp`
   - `IsSystemIdleFor(seconds)` - checks system idle
   - `IsProcessIdleFor(last_interaction_time, seconds)` - checks process idle

### Key Files to Modify

- `src/core/Settings.h` - Add new settings fields
- `src/core/Settings.cpp` - Add JSON load/save for new fields
- `src/ui/SettingsWindow.cpp` - Add UI controls for new settings
- `src/core/ApplicationLogic.cpp` - Use settings instead of hard-coded values

---

## Design Decisions

### 1. Settings Structure

**Decision**: Add two new fields to `AppSettings`:
- `bool recrawlRequireIdle = true;` - Enable/disable idle requirement
- `int recrawlIdleThresholdMinutes = 5;` - Idle threshold in minutes

**Rationale**:
- Simple boolean for enable/disable
- Integer for threshold (consistent with `recrawlIntervalMinutes`)
- Defaults match current behavior (backward compatible)

**Alternative Considered**: Single enum with "immediate", "idle-1min", "idle-5min", etc.
- **Rejected**: Less flexible, harder to extend

### 2. UI Layout

**Decision**: Add controls in "Periodic Recrawl" section:
```
Periodic Recrawl:
[Recrawl Interval (minutes)] [3]
[✓] Only recrawl when idle
[Idle threshold (minutes)] [5]  (only visible when checkbox checked)
```

**Rationale**:
- Groups related settings together
- Conditional visibility keeps UI clean
- Follows existing UI patterns

### 3. Idle Threshold Range

**Decision**: 1-30 minutes

**Rationale**:
- 1 minute: Minimum useful value (system idle detection may be unreliable for shorter periods)
- 30 minutes: Maximum reasonable value (longer would be impractical)
- 5 minutes: Default (current behavior)

**Alternative Considered**: 1-60 minutes (match recrawl interval range)
- **Rejected**: 30 minutes is more than enough for idle threshold

### 4. Behavior When Disabled

**Decision**: When idle requirement is disabled, recrawl immediately when:
- Recrawl interval has elapsed
- No crawl is currently in progress
- Recrawl is enabled
- Crawl folder is set

**Rationale**:
- Simplest behavior (just skip idle checks)
- Users who disable idle requirement want immediate recrawls
- No additional conditions needed

---

## Implementation Plan

### Phase 1: Settings Storage

#### Step 1.1: Add Settings Fields

**File**: `src/core/Settings.h`

**Changes**:
```cpp
struct AppSettings {
  // ... existing fields ...
  
  // Periodic recrawl idle requirement
  // If true, recrawl only happens when system and process are idle
  // If false, recrawl happens immediately when interval elapses
  // Default: true (current behavior)
  bool recrawlRequireIdle = true;
  
  // Idle threshold in minutes (only used when recrawlRequireIdle is true)
  // How long system and process must be idle before recrawl
  // Default: 5 minutes (current behavior)
  // Range: 1-30 minutes
  int recrawlIdleThresholdMinutes = 5;
};
```

#### Step 1.2: Add JSON Load/Save

**File**: `src/core/Settings.cpp`

**Changes**:
1. **LoadSettings()**: Add parsing for new fields
   ```cpp
   // recrawlRequireIdle (boolean, optional, default: true)
   if (j.contains("recrawlRequireIdle") && j["recrawlRequireIdle"].is_boolean()) {
     out.recrawlRequireIdle = j["recrawlRequireIdle"].get<bool>();
   }
   
   // recrawlIdleThresholdMinutes (integer, optional, range: 1-30, default: 5)
   if (j.contains("recrawlIdleThresholdMinutes") && j["recrawlIdleThresholdMinutes"].is_number_integer()) {
     int threshold = j["recrawlIdleThresholdMinutes"].get<int>();
     if (threshold >= 1 && threshold <= 30) {
       out.recrawlIdleThresholdMinutes = threshold;
     }
   }
   ```

2. **SaveSettings()**: Add serialization for new fields
   ```cpp
   j["recrawlRequireIdle"] = settings.recrawlRequireIdle;
   j["recrawlIdleThresholdMinutes"] = settings.recrawlIdleThresholdMinutes;
   ```

**Validation**:
- `recrawlIdleThresholdMinutes` clamped to 1-30 in load
- Defaults applied if fields missing (backward compatible)

---

### Phase 2: UI Controls

#### Step 2.1: Add Settings UI Controls

**File**: `src/ui/SettingsWindow.cpp`

**Location**: In "Periodic Recrawl" section (after recrawl interval input)

**Changes**:
```cpp
// Recrawl interval input (existing)
// ... existing code ...

// Idle requirement checkbox
bool require_idle = settings.recrawlRequireIdle;
if (ImGui::Checkbox("Only recrawl when idle", &require_idle)) {
  if (require_idle != settings.recrawlRequireIdle) {
    settings.recrawlRequireIdle = require_idle;
    SaveSettings(settings);
    LOG_INFO_BUILD("Updated recrawl idle requirement to: " << (require_idle ? "enabled" : "disabled"));
  }
}

if (ImGui::IsItemHovered()) {
  ImGui::SetTooltip("If enabled, recrawl only occurs when system and application are idle.\n"
                    "If disabled, recrawl happens immediately when interval elapses.");
}

// Idle threshold input (only visible when require_idle is true)
if (require_idle) {
  ImGui::Indent();
  int idle_threshold = settings.recrawlIdleThresholdMinutes;
  if (ImGui::InputInt("Idle threshold (minutes)", &idle_threshold, 1, 5)) {
    // Clamp to valid range: 1-30 minutes
    if (idle_threshold < 1) {
      idle_threshold = 1;
    } else if (idle_threshold > 30) {
      idle_threshold = 30;
    }
    
    // Update setting if value changed (after clamping)
    if (idle_threshold != settings.recrawlIdleThresholdMinutes) {
      settings.recrawlIdleThresholdMinutes = idle_threshold;
      SaveSettings(settings);
      LOG_INFO_BUILD("Updated recrawl idle threshold to " << idle_threshold << " minutes");
    }
  }
  
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("How long system and application must be idle before recrawl (1-30 minutes).");
  }
  
  ImGui::TextDisabled("(Current: %d minutes)", settings.recrawlIdleThresholdMinutes);
  ImGui::Unindent();
}
```

**UI Layout**:
```
Periodic Recrawl:
[Recrawl Interval (minutes)] [3]
(Current: 3 minutes)

[✓] Only recrawl when idle
  [Idle threshold (minutes)] [5]
  (Current: 5 minutes)
```

**Notes**:
- Checkbox uses ImGui pattern (variable modified by reference, then used after if)
- InputInt uses same pattern (NOSONAR comments needed)
- Indent/Unindent for visual hierarchy
- Conditional visibility keeps UI clean

---

### Phase 3: Logic Implementation

#### Step 3.1: Update CheckPeriodicRecrawl()

**File**: `src/core/ApplicationLogic.cpp`

**Changes**:
```cpp
void CheckPeriodicRecrawl(Application &application,
                            const AppSettings &settings,
                            const std::chrono::steady_clock::time_point &last_interaction_time,
                            const std::chrono::steady_clock::time_point &last_crawl_completion_time,
                            bool recrawl_enabled) {
  // ... existing early returns (recrawl disabled, no folder, crawl in progress) ...
  
  // Check if recrawl interval has elapsed
  // ... existing interval check code ...
  
  // Check idle requirement (if enabled)
  if (settings.recrawlRequireIdle) {
    // Validate and clamp idle threshold
    int threshold_minutes = settings.recrawlIdleThresholdMinutes;
    if (threshold_minutes < 1) {
      threshold_minutes = 1;
      LOG_WARNING_BUILD("CheckPeriodicRecrawl: Idle threshold was < 1 minute, clamped to 1 minute");
    } else if (threshold_minutes > 30) {
      threshold_minutes = 30;
      LOG_WARNING_BUILD("CheckPeriodicRecrawl: Idle threshold was > 30 minutes, clamped to 30 minutes");
    }
    
    const double idle_threshold_seconds = static_cast<double>(threshold_minutes) * 60.0;
    
    // Check if system and process are idle
    const bool system_idle = system_idle_detector::IsSystemIdleFor(idle_threshold_seconds);
    const bool process_idle = system_idle_detector::IsProcessIdleFor(
        last_interaction_time, idle_threshold_seconds);
    
    if (!system_idle) {
      LOG_INFO_BUILD("CheckPeriodicRecrawl: System is not idle (threshold: " << idle_threshold_seconds << " seconds)");
      return;
    }
    
    if (!process_idle) {
      LOG_INFO_BUILD("CheckPeriodicRecrawl: Process is not idle - last interaction: " 
                     << std::chrono::duration_cast<std::chrono::seconds>(now - last_interaction_time).count()
                     << " seconds ago (threshold: " << idle_threshold_seconds << " seconds)");
      return;
    }
  } else {
    // Idle requirement disabled - skip idle checks
    LOG_INFO_BUILD("CheckPeriodicRecrawl: Idle requirement disabled - recrawling immediately");
  }
  
  // All conditions met - trigger recrawl
  LOG_INFO_BUILD("CheckPeriodicRecrawl: All conditions met - triggering recrawl (interval: " 
                 << interval_minutes << " minutes, elapsed: " 
                 << std::chrono::duration_cast<std::chrono::minutes>(elapsed).count() << " minutes"
                 << (settings.recrawlRequireIdle ? ", idle threshold: " + std::to_string(threshold_minutes) + " minutes" : ", idle requirement disabled")
                 << ")");
  application.TriggerRecrawl();
}
```

**Key Changes**:
1. Wrap idle checks in `if (settings.recrawlRequireIdle)` block
2. Use `settings.recrawlIdleThresholdMinutes` instead of hard-coded constant
3. Add logging when idle requirement is disabled
4. Update log messages to include threshold value

#### Step 3.2: Update Documentation Comments

**File**: `src/core/ApplicationLogic.h`

**Changes**:
```cpp
/**
 * @brief Check and trigger periodic recrawl if conditions are met
 *
 * Checks if periodic recrawl should be triggered based on:
 * - Recrawl is enabled
 * - Crawl folder is set
 * - Recrawl interval has elapsed (configurable, 1-60 minutes)
 * - System and process are idle (if idle requirement enabled, configurable 1-30 minutes)
 * - No crawl is currently in progress
 *
 * @param application Application instance for triggering recrawl
 * @param settings Application settings (for crawl folder, recrawl interval, and idle config)
 * @param last_interaction_time Time of last user interaction (for idle detection)
 * @param last_crawl_completion_time Time when last crawl completed
 * @param recrawl_enabled Whether periodic recrawl is enabled
 */
```

---

## Testing Plan

### Unit Tests

1. **Settings Load/Save**
   - Test loading settings with new fields
   - Test loading settings without new fields (backward compatibility)
   - Test saving settings with new fields
   - Test validation (threshold clamping)

2. **Recrawl Logic**
   - Test recrawl when idle requirement disabled (should skip idle checks)
   - Test recrawl when idle requirement enabled (should check idle)
   - Test with different threshold values (1, 5, 30 minutes)
   - Test threshold clamping (0 → 1, 31 → 30)

### Integration Tests

1. **UI Interaction**
   - Test checkbox toggles setting
   - Test threshold input updates setting
   - Test conditional visibility (threshold only visible when checkbox checked)
   - Test settings persistence (restart app, verify settings saved)

2. **Recrawl Behavior**
   - Test immediate recrawl when idle requirement disabled
   - Test idle-based recrawl when idle requirement enabled
   - Test with different threshold values

### Manual Testing

1. **Enable idle requirement, set threshold to 1 minute**
   - Verify recrawl waits for 1 minute of idle time
   - Verify recrawl happens after 1 minute idle + interval elapsed

2. **Disable idle requirement**
   - Verify recrawl happens immediately when interval elapses
   - Verify no idle checks are performed

3. **Change threshold while app running**
   - Verify new threshold is used for next recrawl check

---

## Migration & Backward Compatibility

### Backward Compatibility

**Status**: ✅ **FULLY COMPATIBLE**

- Existing settings files without new fields will use defaults:
  - `recrawlRequireIdle = true` (current behavior)
  - `recrawlIdleThresholdMinutes = 5` (current behavior)
- No migration needed
- No breaking changes

### Settings File Format

**Before**:
```json
{
  "recrawlIntervalMinutes": 3
}
```

**After** (with defaults):
```json
{
  "recrawlIntervalMinutes": 3,
  "recrawlRequireIdle": true,
  "recrawlIdleThresholdMinutes": 5
}
```

**After** (idle requirement disabled):
```json
{
  "recrawlIntervalMinutes": 3,
  "recrawlRequireIdle": false,
  "recrawlIdleThresholdMinutes": 5
}
```

---

## Impact Analysis

### Files Modified

1. `src/core/Settings.h` - Add 2 new fields (~10 lines)
2. `src/core/Settings.cpp` - Add load/save logic (~20 lines)
3. `src/ui/SettingsWindow.cpp` - Add UI controls (~40 lines)
4. `src/core/ApplicationLogic.cpp` - Update recrawl logic (~30 lines)
5. `src/core/ApplicationLogic.h` - Update documentation (~5 lines)

**Total**: ~105 lines added/modified

### Dependencies

- No new dependencies
- Uses existing `SystemIdleDetector` utility
- Uses existing settings infrastructure

### Risk Assessment

**Low Risk**:
- Changes are additive (new settings, new UI controls)
- Backward compatible (defaults match current behavior)
- Isolated to recrawl logic (no impact on other features)
- Easy to test (clear behavior changes)

---

## Future Enhancements

### Potential Improvements

1. **Separate System/Process Idle Thresholds**
   - Allow different thresholds for system vs process idle
   - More granular control

2. **Idle Detection Method Selection**
   - Allow choosing between different idle detection methods
   - Platform-specific optimizations

3. **Recrawl Scheduling**
   - Schedule recrawls at specific times (e.g., "every hour at :00")
   - More predictable than interval-based

4. **Activity-Based Recrawl**
   - Recrawl when file system activity detected (via USN on Windows)
   - More responsive than time-based

---

## Summary

This feature adds user control over when periodic recrawls happen by making the idle requirement configurable. Users can:
- **Enable idle requirement**: Recrawl only when system/process idle (current behavior, configurable threshold)
- **Disable idle requirement**: Recrawl immediately when interval elapses

**Implementation**: 3 phases (Settings → UI → Logic)
**Risk**: Low (additive changes, backward compatible)
**Impact**: ~105 lines across 5 files
**Testing**: Unit tests, integration tests, manual testing

**Status**: Ready for implementation
