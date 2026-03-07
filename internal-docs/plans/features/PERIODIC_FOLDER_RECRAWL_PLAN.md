# Periodic Folder Recrawl - Implementation Plan

## Overview

Implement automatic periodic recrawling of the user-specified folder (via `--crawl-folder` or Settings window). This ensures the index stays up-to-date with file system changes.

**Phase 1**: Hard-coded 3-minute interval  
**Phase 2**: Configurable interval via Settings window

---

## Current State Analysis

### Existing Components

1. **Folder Crawling**:
   - `FolderCrawler`: Performs directory traversal
   - `FolderCrawlerIndexBuilder`: Wraps crawler in `IIndexBuilder` interface
   - `IIndexBuilder::Start()`: Starts background crawl
   - `IIndexBuilder::Stop()`: Stops current crawl

2. **Settings**:
   - `AppSettings::crawlFolder`: Already stores folder path
   - Settings loaded from JSON at startup
   - Settings saved when user changes folder in UI

3. **Application Architecture**:
   - `Application` owns `index_builder_` (unique_ptr<IIndexBuilder>)
   - `Application::IsIndexBuilding()`: Checks if crawl is in progress
   - `ApplicationLogic::Update()`: Called every frame, handles periodic tasks
   - `IndexBuildState`: Tracks crawl progress

4. **Idle Detection**:
   - `SystemIdleDetector`: New utility for system/process idle detection
   - Can check if system/process has been idle for specified duration

### Key Files

- `src/core/Application.h/cpp`: Owns index_builder_, manages lifecycle
- `src/core/ApplicationLogic.h/cpp`: Periodic update logic (called every frame)
- `src/core/Settings.h/cpp`: Settings storage (crawlFolder already exists)
- `src/core/IndexBuilder.h`: IIndexBuilder interface
- `src/crawler/FolderCrawlerIndexBuilder.cpp`: Implementation
- `src/utils/SystemIdleDetector.h/cpp`: Idle detection utility

---

## Requirements

### Phase 1 (Hard-coded 3 minutes)

1. **Automatic Recrawl**: Recrawl folder every 3 minutes (hard-coded)
2. **Idle-Only**: Only recrawl when system/process has been idle for at least 5 minutes
3. **No Interruption**: Don't start recrawl if:
   - Current crawl is in progress
   - User is actively using the application
   - System is not idle
4. **Seamless**: Recrawl should happen in background without disrupting user experience

### Phase 2 (Configurable)

1. **Settings UI**: Add recrawl interval setting to Settings window
2. **Persistence**: Save interval to `AppSettings` (JSON)
3. **Validation**: Validate interval (minimum 1 minute, maximum 60 minutes)
4. **Default**: Use 3 minutes as default if not set

---

## Design Decisions

### 1. Recrawl Trigger Logic

**Decision**: Check recrawl condition in `ApplicationLogic::Update()` (called every frame)

**Rationale**:
- Already handles periodic tasks (memory updates, maintenance)
- Runs every frame, so can check time-based conditions frequently
- Centralized location for all periodic operations
- Can use existing idle detection integration

**Implementation**:
```cpp
// In ApplicationLogic::Update()
void CheckPeriodicRecrawl(Application& app, const AppSettings& settings) {
  // Only check if crawl folder is set
  if (settings.crawlFolder.empty()) {
    return;
  }
  
  // Check if recrawl is needed (3 minutes elapsed)
  // Check if system/process is idle (5 minutes)
  // Check if no crawl is in progress
  // If all conditions met, trigger recrawl
}
```

### 2. Idle Detection Strategy

**Decision**: Require both system AND process to be idle for 5 minutes

**Rationale**:
- System idle: Computer is not being used (user stepped away)
- Process idle: Application is not being actively used
- Combined: Ensures user is truly away before recrawling
- Prevents interrupting user work

**Implementation**:
```cpp
constexpr double kIdleThresholdSeconds = 5.0 * 60.0;  // 5 minutes
bool system_idle = system_idle_detector::IsSystemIdleFor(kIdleThresholdSeconds);
bool process_idle = system_idle_detector::IsProcessIdleFor(
    last_interaction_time, kIdleThresholdSeconds);

if (system_idle && process_idle) {
  // Safe to recrawl
}
```

### 3. Recrawl Interval Tracking

**Decision**: Track last crawl completion time, not start time

**Rationale**:
- Ensures full crawl completes before next recrawl starts
- Prevents overlapping crawls
- More predictable behavior

**Implementation**:
```cpp
// In Application class
std::chrono::steady_clock::time_point last_crawl_completion_time_;

// After crawl completes
last_crawl_completion_time_ = std::chrono::steady_clock::now();

// Check if recrawl needed
auto elapsed = std::chrono::steady_clock::now() - last_crawl_completion_time_;
if (elapsed >= kRecrawlInterval) {
  // Trigger recrawl
}
```

### 4. Index Builder Reuse

**Decision**: Reuse existing `index_builder_` if it's a `FolderCrawlerIndexBuilder`, otherwise create new one

**Rationale**:
- `index_builder_` may be `WindowsIndexBuilder` (USN-based) on Windows
- Need to create `FolderCrawlerIndexBuilder` for folder recrawl
- Can replace builder if type doesn't match

**Implementation**:
```cpp
// Check if current builder is FolderCrawlerIndexBuilder
// If not, create new FolderCrawlerIndexBuilder
// Stop old builder, start new one
```

### 5. Settings Storage (Phase 2)

**Decision**: Add `recrawlIntervalMinutes` to `AppSettings`

**Rationale**:
- Consistent with other settings
- Persisted to JSON automatically
- Easy to access from UI

**Implementation**:
```cpp
struct AppSettings {
  // ... existing fields ...
  
  // Periodic recrawl interval in minutes (Phase 2)
  // Default: 3 minutes
  // Range: 1-60 minutes
  int recrawlIntervalMinutes = 3;
};
```

---

## Implementation Plan

### Phase 1: Hard-coded 3 Minutes

#### Step 1: Add Recrawl Tracking to Application

**File**: `src/core/Application.h`

**Changes**:
```cpp
class Application {
private:
  // ... existing members ...
  
  // Periodic recrawl tracking
  std::chrono::steady_clock::time_point last_crawl_completion_time_;
  bool recrawl_enabled_ = false;  // Set to true when crawl folder is available
};
```

**File**: `src/core/Application.cpp`

**Changes**:
1. Initialize `last_crawl_completion_time_` in constructor
2. Update `last_crawl_completion_time_` when crawl completes
3. Add method to check if recrawl is needed:
   ```cpp
   bool ShouldRecrawl() const;
   ```
4. Add method to trigger recrawl:
   ```cpp
   void TriggerRecrawl();
   ```

**Implementation Details**:
- Track completion time: Update when `IndexBuildState` indicates crawl is complete
- Check completion: Monitor `IndexBuildState::is_complete` or `IsIndexBuilding() == false`
- Recrawl trigger: Create new `FolderCrawlerIndexBuilder` and call `Start()`

#### Step 2: Integrate Recrawl Check in ApplicationLogic

**File**: `src/core/ApplicationLogic.h`

**Changes**:
```cpp
namespace application_logic {
  // ... existing functions ...
  
  // Check and trigger periodic recrawl if needed
  void CheckPeriodicRecrawl(Application& app, const AppSettings& settings,
                            const std::chrono::steady_clock::time_point& last_interaction_time);
}
```

**File**: `src/core/ApplicationLogic.cpp`

**Changes**:
1. Add `CheckPeriodicRecrawl()` implementation:
   - Check if `settings.crawlFolder` is set
   - Check if 3 minutes have elapsed since last crawl
   - Check if system/process is idle (5 minutes)
   - Check if no crawl is in progress
   - If all conditions met, call `app.TriggerRecrawl()`

2. Call from `Update()`:
   ```cpp
   void Update(...) {
     // ... existing code ...
     
     // Check periodic recrawl (only if index is ready)
     if (!is_index_building) {
       CheckPeriodicRecrawl(*application, *settings, last_interaction_time);
     }
   }
   ```

**Dependencies**:
- Include `SystemIdleDetector.h`
- Include `Application.h` (forward declaration may not be enough)

#### Step 3: Handle Crawl Completion

**File**: `src/core/Application.cpp`

**Changes**:
- In `ProcessFrame()` or similar, check if crawl just completed:
  ```cpp
  bool was_building = IsIndexBuilding();
  // ... frame processing ...
  bool is_building = IsIndexBuilding();
  
  if (was_building && !is_building) {
    // Crawl just completed
    last_crawl_completion_time_ = std::chrono::steady_clock::now();
    recrawl_enabled_ = true;  // Enable periodic recrawl
  }
  ```

**Alternative**: Monitor `IndexBuildState` directly if accessible

#### Step 4: Implement Recrawl Trigger

**File**: `src/core/Application.cpp`

**Changes**:
```cpp
void Application::TriggerRecrawl() {
  // Don't recrawl if already building
  if (IsIndexBuilding()) {
    return;
  }
  
  // Don't recrawl if no folder is set
  if (settings_->crawlFolder.empty()) {
    return;
  }
  
  // Stop current builder if it exists and is running
  if (index_builder_) {
    index_builder_->Stop();
  }
  
  // Create new FolderCrawlerIndexBuilder
  IndexBuilderConfig config;
  config.crawl_folder = settings_->crawlFolder;
  index_builder_ = CreateFolderCrawlerIndexBuilder(*file_index_, config);
  
  // Start new crawl
  index_builder_->Start(index_build_state_);
  
  LOG_INFO_BUILD("Periodic recrawl started for folder: " << settings_->crawlFolder);
}
```

#### Step 5: Update ApplicationLogic::Update() Signature

**File**: `src/core/ApplicationLogic.h/cpp`

**Changes**:
- Add `Application&` parameter to `Update()`:
  ```cpp
  void Update(GuiState& state,
              SearchController& search_controller,
              SearchWorker& search_worker,
              FileIndex& file_index,
              const UsnMonitor* monitor,
              bool is_index_building,
              Application& application,  // NEW
              const AppSettings& settings,  // NEW
              const std::chrono::steady_clock::time_point& last_interaction_time);  // NEW
  ```

**File**: `src/core/Application.cpp`

**Changes**:
- Update call to `ApplicationLogic::Update()`:
  ```cpp
  application_logic::Update(state_, search_controller_, search_worker_,
                          *file_index_, monitor_.get(), IsIndexBuilding(),
                          *this, *settings_, last_interaction_time);
  ```

**Note**: Need to track `last_interaction_time` in `Application` class (already exists in `Run()` method)

---

### Phase 2: Configurable Interval

#### Step 1: Add Setting to AppSettings

**File**: `src/core/Settings.h`

**Changes**:
```cpp
struct AppSettings {
  // ... existing fields ...
  
  // Periodic recrawl interval in minutes
  // Default: 3 minutes
  // Range: 1-60 minutes
  // Only used when crawlFolder is set
  int recrawlIntervalMinutes = 3;
};
```

**File**: `src/core/Settings.cpp`

**Changes**:
- Update JSON serialization/deserialization to include `recrawlIntervalMinutes`
- Add validation: clamp to 1-60 range

#### Step 2: Add UI Control to Settings Window

**File**: `src/ui/SettingsWindow.cpp`

**Changes**:
- Add input field for recrawl interval:
  ```cpp
  // In folder selection section
  ImGui::Text("Recrawl Interval (minutes):");
  int interval = settings.recrawlIntervalMinutes;
  if (ImGui::InputInt("##recrawl_interval", &interval, 1, 5,
                      ImGuiInputTextFlags_CharsDecimal)) {
    // Clamp to valid range
    interval = (std::max)(1, (std::min)(60, interval));
    settings.recrawlIntervalMinutes = interval;
    SaveSettings(settings);
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(1-60 minutes)");
  ```

#### Step 3: Use Setting in Recrawl Logic

**File**: `src/core/ApplicationLogic.cpp`

**Changes**:
- Replace hard-coded 3 minutes with `settings.recrawlIntervalMinutes`:
  ```cpp
  const int recrawl_interval_minutes = settings.recrawlIntervalMinutes;
  const auto recrawl_interval = std::chrono::minutes(recrawl_interval_minutes);
  
  auto elapsed = std::chrono::steady_clock::now() - last_crawl_completion_time_;
  if (elapsed >= recrawl_interval) {
    // Trigger recrawl
  }
  ```

---

## Testing Plan

### Phase 1 Testing

1. **Basic Functionality**:
   - Set crawl folder via Settings
   - Wait 3 minutes (with system/process idle)
   - Verify recrawl starts automatically
   - Verify index is updated

2. **Idle Detection**:
   - Set crawl folder
   - Keep system/process active (not idle)
   - Verify recrawl does NOT start
   - Make system/process idle for 5+ minutes
   - Verify recrawl starts

3. **Edge Cases**:
   - Recrawl while crawl is in progress (should not start)
   - No crawl folder set (should not attempt recrawl)
   - Crawl folder changed (should use new folder)

### Phase 2 Testing

1. **Settings Persistence**:
   - Set interval to 5 minutes
   - Restart application
   - Verify interval is preserved

2. **UI Validation**:
   - Enter invalid values (0, negative, > 60)
   - Verify values are clamped to valid range

3. **Interval Changes**:
   - Change interval while recrawl is scheduled
   - Verify new interval is used for next recrawl

---

## File Changes Summary

### Phase 1

**Modified Files**:
- `src/core/Application.h`: Add recrawl tracking members
- `src/core/Application.cpp`: Add recrawl logic, track completion time
- `src/core/ApplicationLogic.h`: Add `CheckPeriodicRecrawl()` declaration
- `src/core/ApplicationLogic.cpp`: Implement recrawl check, update `Update()` signature
- `src/core/Application.cpp`: Update `Update()` call site

**New Dependencies**:
- `src/utils/SystemIdleDetector.h` (already exists)

### Phase 2

**Modified Files**:
- `src/core/Settings.h`: Add `recrawlIntervalMinutes` field
- `src/core/Settings.cpp`: Update JSON serialization
- `src/ui/SettingsWindow.cpp`: Add UI control for interval
- `src/core/ApplicationLogic.cpp`: Use setting instead of hard-coded value

---

## Risks and Mitigations

### Risk 1: Recrawl Interrupts User

**Mitigation**: 
- Require both system AND process idle (5 minutes)
- Only recrawl when no active crawl is in progress
- Recrawl happens in background thread (non-blocking)

### Risk 2: Too Frequent Recrawls

**Mitigation**:
- Minimum interval of 1 minute (Phase 2)
- Only recrawl when idle (reduces frequency naturally)
- Track completion time, not start time (prevents overlapping)

### Risk 3: Index Builder Type Mismatch

**Mitigation**:
- Check builder type before reusing
- Create new `FolderCrawlerIndexBuilder` if needed
- Stop old builder before starting new one

### Risk 4: Settings Not Persisted

**Mitigation**:
- Use existing `SaveSettings()` mechanism
- Save immediately when interval changes in UI
- Validate on load (clamp to valid range)

---

## Future Enhancements

1. **Smart Recrawl**: Only recrawl if files have changed (check modification times)
2. **Incremental Recrawl**: Only crawl changed directories
3. **User Notification**: Show notification when recrawl starts/completes
4. **Recrawl Statistics**: Track recrawl frequency, duration, files changed
5. **Pause/Resume**: Allow user to pause periodic recrawl temporarily

---

## Implementation Order

### Phase 1 (Hard-coded 3 minutes)

1. ✅ Add recrawl tracking to `Application` class
2. ✅ Implement `TriggerRecrawl()` method
3. ✅ Track crawl completion time
4. ✅ Add `CheckPeriodicRecrawl()` to `ApplicationLogic`
5. ✅ Integrate idle detection
6. ✅ Update `Update()` signature and call site
7. ✅ Test basic functionality

### Phase 2 (Configurable)

1. ✅ Add `recrawlIntervalMinutes` to `AppSettings`
2. ✅ Update JSON serialization
3. ✅ Add UI control to Settings window
4. ✅ Use setting in recrawl logic
5. ✅ Test settings persistence

---

## Success Criteria

### Phase 1

- [x] Recrawl starts automatically after 3 minutes (when idle)
- [x] Recrawl does NOT start if system/process is not idle
- [x] Recrawl does NOT start if crawl is already in progress
- [x] Index is updated after recrawl completes
- [x] No user-visible disruption during recrawl

### Phase 2

- [x] Recrawl interval can be configured in Settings window
- [x] Interval is persisted to JSON
- [x] Interval validation works (1-60 minutes)
- [x] Configured interval is used for recrawl timing
- [x] Default value (3 minutes) is used if not set

---

## Notes

- **Idle Threshold**: 5 minutes (harder than recrawl interval) ensures user is truly away
- **Recrawl Interval**: 3 minutes (Phase 1), configurable (Phase 2)
- **Thread Safety**: All operations are thread-safe (existing `IIndexBuilder` interface)
- **Performance**: Recrawl happens in background, minimal main thread impact
- **Compatibility**: Works with existing folder crawl feature (no breaking changes)
