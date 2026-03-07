# Auto-Crawl on Startup Feature Plan

## Overview

When the application starts with no `--crawl-folder` argument and no USN monitoring, the UI should be shown first, then the app will automatically start crawling:
1. First, try the folder from settings (`settings->crawlFolder`) if available
2. If no folder in settings, crawl the user HOME folder

## Current Behavior

1. **Command-line parsing**: `CommandLineArgs` has a `crawl_folder` field that can be set via `--crawl-folder`
2. **USN monitoring**: On Windows, USN monitoring is started if running with elevation and no `--index-from-file`
3. **Settings**: `AppSettings` has a `crawlFolder` field that can be loaded from settings
4. **Index building**: In `ExecuteApplicationMain` (`src/core/main_common.h`), the logic is:
   - Priority: command-line `crawl_folder` > settings `crawlFolder` > default
   - On Windows: if USN monitor exists, use it; otherwise use folder crawler
   - On macOS/Linux: if `crawl_folder` is provided, use folder crawler
   - Index building starts **immediately** when an index builder is created

## Required Behavior

1. **Condition check**: When app starts with:
   - No `--crawl-folder` argument (`cmd_args.crawl_folder.empty()`)
   - No USN monitoring (`bootstrap.monitor == nullptr` or `index_builder_config.use_usn_monitor == false`)
   - No `--index-from-file` argument (`cmd_args.index_from_file.empty()`)

2. **UI first**: Show the UI (window is already shown in bootstrap, but we need to ensure it's visible)

3. **Delayed auto-crawl**: After UI is shown, automatically start crawling:
   - **Priority 1**: Use `settings->crawlFolder` if it's not empty
   - **Priority 2**: If `settings->crawlFolder` is empty, use `path_utils::GetUserHomePath()`

## Implementation Plan

### Step 1: Modify `ExecuteApplicationMain` to Delay Auto-Crawl

**File**: `src/core/main_common.h`

**Changes**:
1. Detect when auto-crawl should be triggered (no `--crawl-folder`, no USN monitoring, no `--index-from-file`)
2. Don't create index builder immediately in this case
3. Pass a flag to `Application` indicating that auto-crawl should be triggered after UI is shown

**Logic**:
```cpp
// In ExecuteApplicationMain, after determining index_builder_config:
bool should_auto_crawl = false;
std::string auto_crawl_folder;

if (cmd_args.crawl_folder.empty() && 
    !index_builder_config.use_usn_monitor && 
    cmd_args.index_from_file.empty()) {
  // Determine which folder to crawl
  if (bootstrap.settings && !bootstrap.settings->crawlFolder.empty()) {
    auto_crawl_folder = bootstrap.settings->crawlFolder;
    should_auto_crawl = true;
  } else {
    // Fallback to HOME folder
    auto_crawl_folder = path_utils::GetUserHomePath();
    should_auto_crawl = true;
  }
}

// Don't create index builder if we're doing auto-crawl
if (should_auto_crawl) {
  index_builder = nullptr;  // Will be created later in Application
} else {
  // Existing logic to create index builder...
}
```

### Step 2: Add Auto-Crawl Support to `Application`

**File**: `src/core/Application.h` and `src/core/Application.cpp`

**Changes**:
1. Add member variable to track auto-crawl state:
   ```cpp
   bool should_auto_crawl_ = false;
   std::string auto_crawl_folder_;
   bool auto_crawl_triggered_ = false;
   ```

2. Modify constructor to accept auto-crawl parameters:
   ```cpp
   Application(AppBootstrapResultBase& bootstrap,
               const CommandLineArgs& cmd_args,
               IndexBuildState& index_build_state,
               std::unique_ptr<IIndexBuilder> index_builder,
               bool should_auto_crawl = false,
               std::string auto_crawl_folder = "");
   ```

3. Add method to trigger auto-crawl:
   ```cpp
   void TriggerAutoCrawl();
   ```

4. In `Run()` method, trigger auto-crawl after first frame (to ensure UI is shown):
   ```cpp
   // In Application::Run(), after first frame is processed:
   if (should_auto_crawl_ && !auto_crawl_triggered_) {
     TriggerAutoCrawl();
     auto_crawl_triggered_ = true;
   }
   ```

### Step 3: Implement `TriggerAutoCrawl` Method

**File**: `src/core/Application.cpp`

**Implementation**:
```cpp
void Application::TriggerAutoCrawl() {
  if (auto_crawl_folder_.empty()) {
    LOG_ERROR("TriggerAutoCrawl called with empty folder path");
    return;
  }

  LOG_INFO_BUILD("Auto-starting crawl for folder: " << auto_crawl_folder_);
  
  if (StartIndexBuilding(auto_crawl_folder_)) {
    LOG_INFO_BUILD("Auto-crawl started successfully");
  } else {
    LOG_ERROR_BUILD("Failed to start auto-crawl for folder: " << auto_crawl_folder_);
  }
}
```

### Step 4: Update `ExecuteApplicationMain` to Pass Auto-Crawl Parameters

**File**: `src/core/main_common.h`

**Changes**:
1. Calculate auto-crawl folder before creating `Application`
2. Pass auto-crawl parameters to `Application` constructor

**Updated logic**:
```cpp
// Determine if we should auto-crawl
bool should_auto_crawl = false;
std::string auto_crawl_folder;

if (cmd_args.crawl_folder.empty() && 
    !index_builder_config.use_usn_monitor && 
    cmd_args.index_from_file.empty()) {
  if (bootstrap.settings && !bootstrap.settings->crawlFolder.empty()) {
    auto_crawl_folder = bootstrap.settings->crawlFolder;
    should_auto_crawl = true;
  } else {
    auto_crawl_folder = path_utils::GetUserHomePath();
    should_auto_crawl = true;
  }
}

// Create Application with auto-crawl parameters
Application app(bootstrap, cmd_args, index_build_state, std::move(index_builder),
                should_auto_crawl, auto_crawl_folder);
```

### Step 5: Include Required Headers

**File**: `src/core/main_common.h`

**Add include**:
```cpp
#include "path/PathUtils.h"
```

## Testing Considerations

1. **Test case 1**: App starts with no arguments, no USN monitoring, no settings folder
   - Expected: UI shows, then auto-crawls HOME folder

2. **Test case 2**: App starts with no arguments, no USN monitoring, but settings has `crawlFolder`
   - Expected: UI shows, then auto-crawls settings folder

3. **Test case 3**: App starts with `--crawl-folder` argument
   - Expected: Normal behavior (crawls immediately, no auto-crawl)

4. **Test case 4**: App starts with USN monitoring (Windows with elevation)
   - Expected: Normal behavior (USN monitoring starts, no auto-crawl)

5. **Test case 5**: App starts with `--index-from-file`
   - Expected: Normal behavior (loads from file, no auto-crawl)

## Edge Cases

1. **HOME folder doesn't exist**: `path_utils::GetUserHomePath()` should handle this gracefully, but we should verify the folder exists before starting crawl
2. **Settings folder doesn't exist**: `StartIndexBuilding` should handle invalid paths gracefully
3. **User cancels crawl**: Normal behavior, user can stop crawl from UI
4. **Multiple starts**: `auto_crawl_triggered_` flag prevents multiple auto-crawls

## Implementation Order

1. ✅ Add auto-crawl member variables to `Application`
2. ✅ Modify `Application` constructor to accept auto-crawl parameters
3. ✅ Implement `TriggerAutoCrawl` method
4. ✅ Modify `Application::Run()` to trigger auto-crawl after first frame
5. ✅ Modify `ExecuteApplicationMain` to detect auto-crawl condition and pass parameters
6. ✅ Add include for `PathUtils.h` in `main_common.h`
7. ✅ Test on all platforms (Windows, macOS, Linux)

## Code Quality Considerations

- Follow SOLID principles (Single Responsibility: auto-crawl logic is separate from normal index building)
- Use DRY (reuse existing `StartIndexBuilding` method)
- Keep it simple (KISS: straightforward flag-based approach)
- Add appropriate logging for debugging
- Handle errors gracefully (invalid folders, etc.)

## Related Files

- `src/core/main_common.h` - Main application entry point
- `src/core/Application.h` - Application class definition
- `src/core/Application.cpp` - Application implementation
- `src/path/PathUtils.h` - Home directory utility
- `src/core/Settings.h` - Settings structure
- `src/core/CommandLineArgs.h` - Command line arguments
