# UI Folder Crawl Selection - Implementation Plan

## Overview

This plan outlines the implementation of a UI-based folder selection feature that allows users to choose a folder to crawl and index from within the application, eliminating the need for the `--crawl-folder` command-line argument for interactive use.

## Current State Analysis

### Current Implementation

1. **Command-Line Parsing**: `--crawl-folder` is parsed in `CommandLineArgs.cpp` and stored in `CommandLineArgs::crawl_folder`
2. **Initialization Flow**: 
   - `main_common.h` reads `cmd_args.crawl_folder` and passes it to `IndexBuilderConfig`
   - `CreateFolderCrawlerIndexBuilder()` creates a builder if `crawl_folder` is not empty
   - Index building starts automatically during application initialization
3. **Index Building**: 
   - `FolderCrawlerIndexBuilder` runs in a background thread
   - Progress is reported via `IndexBuildState`
   - UI displays progress in status bar
4. **Folder Picker**: Already implemented for all platforms:
   - Windows: `ShowFolderPickerDialog()` in `FileOperations_win.cpp` (uses IFileDialog)
   - macOS: `ShowFolderPickerDialog()` in `FileOperations_mac.mm` (uses NSOpenPanel)
   - Linux: `ShowFolderPickerDialog()` in `FileOperations_linux.cpp` (uses zenity/kdialog/yad)

### Key Components

- **`CommandLineArgs`**: Contains `crawl_folder` string
- **`IndexBuilderConfig`**: Contains `crawl_folder` and other index building config
- **`IIndexBuilder`**: Abstract interface for index building (Start/Stop methods)
- **`IndexBuildState`**: Shared state for progress reporting
- **`AppSettings`**: Persisted settings structure (JSON)
- **`Application`**: Owns `IIndexBuilder` and manages lifecycle
- **`SettingsWindow`**: UI component for settings (ImGui)

## Requirements

1. **UI Control**: Add a folder selection control in the Settings window
2. **Persistence**: Save selected folder path in `AppSettings` (JSON)
3. **Runtime Crawl**: Allow starting a new crawl from UI (not just at startup)
4. **State Management**: Handle stopping current crawl and starting a new one
5. **Validation**: Validate folder path exists and is accessible
6. **Platform Support**: Works on Windows, macOS, and Linux
7. **Backward Compatibility**: `--crawl-folder` command-line argument still works

## Design Decisions

### 1. Settings Storage

**Decision**: Add `crawlFolder` field to `AppSettings` struct

**Rationale**:
- Settings are already persisted to JSON
- Consistent with other settings (e.g., `monitoredVolume`)
- Allows remembering user's last selection
- Can be overridden by command-line argument (command-line takes precedence)

**Location**: `src/core/Settings.h`

```cpp
struct AppSettings {
  // ... existing fields ...
  
  // Folder to crawl and index (alternative to USN Journal)
  // Empty = use command-line argument or default behavior
  // Format: Platform-specific path (e.g., "C:\\Users\\Documents" or "/home/user/documents")
  // Note: Command-line --crawl-folder takes precedence over this setting
  std::string crawlFolder;
};
```

### 2. UI Location

**Decision**: Add folder selection section to Settings window

**Rationale**:
- Settings window is the natural place for configuration
- Users already know where to find settings
- Keeps all index-related configuration in one place
- Can be grouped with other index-related settings (future: index file selection)

**UI Layout**:
```
Settings Window
├── Appearance
├── Performance
└── Index Configuration (NEW)
    ├── Folder to Index: [Browse...] [Current: /path/to/folder]
    └── [Start Indexing] / [Stop Indexing] (contextual button)
```

### 3. Index Builder Lifecycle

**Decision**: Support runtime index building (not just startup)

**Rationale**:
- Users may want to change the indexed folder without restarting
- Allows re-indexing if index becomes stale
- More flexible than startup-only approach

**Implementation**:
- **Current Architecture**: `IIndexBuilder` is created in `main_common.h` as a local variable, not owned by `Application`
- **Required Change**: Move `IIndexBuilder` ownership to `Application` class for runtime control
- Need to add method to `Application` to start/stop index building at runtime
- Must handle stopping current crawl before starting new one
- **Migration**: Pass `index_builder` from `main_common.h` to `Application` constructor, or create it in `Application` if needed

### 4. Command-Line Precedence

**Decision**: Command-line `--crawl-folder` takes precedence over UI setting

**Rationale**:
- Maintains backward compatibility
- Allows automation/scripting use cases
- Command-line is explicit user intent

**Flow**:
1. If `--crawl-folder` is provided, use it (ignore UI setting)
2. If no command-line argument, use `AppSettings::crawlFolder` if set
3. If neither is set, use platform-specific default behavior

### 5. Threading Model

**Decision**: Use existing `IIndexBuilder` interface (already thread-safe)

**Rationale**:
- `IIndexBuilder::Start()` and `Stop()` are already designed for background threads
- `IndexBuildState` is thread-safe (uses atomics)
- UI updates happen on main thread (ImGui requirement)
- No need to change existing threading model

## Implementation Plan

### Phase 1: Settings Storage

**Files to Modify**:
- `src/core/Settings.h`: Add `crawlFolder` field to `AppSettings`
- `src/core/Settings.cpp`: Update JSON serialization/deserialization

**Changes**:
1. Add `std::string crawlFolder;` to `AppSettings` struct
2. Update `LoadSettings()` to read `crawlFolder` from JSON
3. Update `SaveSettings()` to write `crawlFolder` to JSON
4. Default value: empty string (no folder selected)

**Validation**:
- JSON round-trip test (save/load)
- Empty string handling
- Path format preservation (Windows backslashes, Unix forward slashes)

### Phase 2: UI Controls

**Files to Modify**:
- `src/ui/SettingsWindow.h`: Add folder selection section
- `src/ui/SettingsWindow.cpp`: Implement folder picker UI

**Changes**:
1. Add new section "Index Configuration" to Settings window
2. Display current folder path (from `AppSettings::crawlFolder`)
3. Add "Browse..." button that calls `ShowFolderPickerDialog()`
4. Add "Start Indexing" button (enabled when folder is set and not currently indexing)
5. Add "Stop Indexing" button (enabled when indexing is in progress)
6. Show validation errors (folder doesn't exist, no permission, etc.)

**UI Flow**:
```
1. User clicks "Browse..." → ShowFolderPickerDialog() → Update AppSettings::crawlFolder
2. User clicks "Start Indexing" → Trigger index build with selected folder
3. User clicks "Stop Indexing" → Cancel current index build
```

**Dependencies**:
- Need access to `FileIndex` (already passed to `SettingsWindow::Render()`)
- Need access to `IndexBuildState` (to check if indexing is active)
- Need access to `IIndexBuilder` (to start/stop indexing)
- Need access to `NativeWindowHandle` (for folder picker dialog)

### Phase 3: Runtime Index Building

**Files to Modify**:
- `src/core/Application.h`: Add `IIndexBuilder` member and methods for runtime index building
- `src/core/Application.cpp`: Implement runtime index building methods
- `src/core/main_common.h`: Pass `index_builder` to `Application` constructor (or let `Application` create it)

**Changes**:
1. **Move `IIndexBuilder` ownership to `Application`**:
   - Add `std::unique_ptr<IIndexBuilder> index_builder_;` member to `Application`
   - Update `Application` constructor to accept `index_builder` (or create it if needed)
   - Update `main_common.h` to pass `index_builder` to `Application` constructor
   - Update `Application::Run()` to stop `index_builder_` in destructor (already handled by `main_common.h`, but move to `Application`)

2. Add `StartIndexBuilding(const std::string& folder_path)` method to `Application`
3. Add `StopIndexBuilding()` method to `Application`
4. Add `IsIndexBuilding()` method (already exists, verify it works correctly)
5. Handle stopping existing builder before starting new one
6. Update `IndexBuildState` with new source description

**Implementation Details**:
```cpp
// In Application.h - Add member
std::unique_ptr<IIndexBuilder> index_builder_;  // Owned by Application

// In Application.cpp - Constructor (accept index_builder from main_common.h)
Application::Application(AppBootstrapResultBase& bootstrap,
                       const CommandLineArgs& cmd_args,
                       IndexBuildState& index_build_state,
                       std::unique_ptr<IIndexBuilder> index_builder)
    : file_index_(bootstrap.file_index)
    , index_builder_(std::move(index_builder))  // Take ownership
    // ... rest of initialization ...

// In Application.cpp - Runtime methods
void Application::StartIndexBuilding(const std::string& folder_path) {
  // Stop existing builder if running
  if (index_builder_) {
    index_builder_->Stop();
  }
  
  // Create new builder with selected folder
  IndexBuilderConfig config;
  config.crawl_folder = folder_path;
  config.index_file_path.clear();
  config.use_usn_monitor = false; // Folder crawler, not USN
  
  index_builder_ = CreateFolderCrawlerIndexBuilder(*file_index_, config);
  if (index_builder_) {
    index_build_state_.Reset();
    index_build_state_.source_description = "Folder crawl: " + folder_path;
    index_builder_->Start(index_build_state_);
  }
}

void Application::StopIndexBuilding() {
  if (index_builder_) {
    index_builder_->Stop();
  }
}

// In Application destructor (or before shutdown)
~Application() {
  if (index_builder_) {
    index_builder_->Stop();  // Ensure cleanup
  }
}
```

**Alternative Approach** (if moving ownership is too complex):
- Keep `index_builder` in `main_common.h` but pass pointer to `Application`
- `Application` can create new builders at runtime (but won't own startup builder)
- **Recommendation**: Move ownership to `Application` for cleaner architecture

**Threading Considerations**:
- `StartIndexBuilding()` must be called from main thread (ImGui context)
- `StopIndexBuilding()` must be called from main thread
- Background threads are managed by `IIndexBuilder` implementations
- UI updates happen on main thread via `IndexBuildState` polling

### Phase 4: Command-Line Integration

**Files to Modify**:
- `src/core/main_common.h`: Update initialization logic

**Changes**:
1. Check command-line argument first: `if (!cmd_args.crawl_folder.empty())`
2. Fall back to settings: `else if (!settings.crawlFolder.empty())`
3. Otherwise use platform-specific default behavior

**Priority Order**:
1. Command-line `--crawl-folder` (highest priority)
2. `AppSettings::crawlFolder` (UI setting)
3. Platform-specific default (lowest priority)

### Phase 5: Validation and Error Handling

**Files to Modify**:
- `src/ui/SettingsWindow.cpp`: Add validation logic
- `src/platform/FileOperations.h`: Add folder validation helper (if needed)

**Validation Checks**:
1. Folder path is not empty
2. Folder exists (`std::filesystem::exists()`)
3. Folder is a directory (`std::filesystem::is_directory()`)
4. Folder is readable (try to open/read)

**Error Display**:
- Show error message in Settings window (red text below folder path)
- Disable "Start Indexing" button if validation fails
- Log errors to console for debugging

## Impact Analysis

### Positive Impacts

1. **User Experience**:
   - No need to restart application to change indexed folder
   - Visual feedback during indexing
   - Settings persistence across sessions
   - Cross-platform folder picker (native dialogs)

2. **Flexibility**:
   - Can re-index different folders without restart
   - Can stop/start indexing on demand
   - Command-line still works for automation

3. **Maintainability**:
   - Uses existing `IIndexBuilder` interface
   - Reuses existing folder picker implementation
   - Follows existing settings persistence pattern

### Potential Issues and Mitigations

1. **Index Replacement**:
   - **Issue**: Starting a new crawl replaces the entire index
   - **Mitigation**: Show confirmation dialog: "This will replace the current index. Continue?"
   - **Alternative**: Support incremental indexing (future enhancement)

2. **Performance**:
   - **Issue**: Large folders may take time to index
   - **Mitigation**: Progress is already shown in status bar
   - **Mitigation**: User can stop indexing if needed

3. **State Management**:
   - **Issue**: Complex state transitions (idle → indexing → complete/failed)
   - **Mitigation**: Use existing `IndexBuildState` (already handles state)
   - **Mitigation**: UI buttons are enabled/disabled based on state

4. **Threading**:
   - **Issue**: Starting new crawl while one is in progress
   - **Mitigation**: `StartIndexBuilding()` stops existing builder first
   - **Mitigation**: `IIndexBuilder::Stop()` is thread-safe

5. **Settings Sync**:
   - **Issue**: Settings saved to JSON, but index builder uses in-memory config
   - **Mitigation**: Save settings immediately when folder is selected
   - **Mitigation**: Load settings at startup (already implemented)

6. **Platform Differences**:
   - **Issue**: Folder picker implementations differ per platform
   - **Mitigation**: Already abstracted via `ShowFolderPickerDialog()` function
   - **Mitigation**: Path format differences handled by `std::filesystem`

### Breaking Changes

**None**: This is a purely additive feature. Existing functionality remains unchanged:
- Command-line `--crawl-folder` still works
- Existing index building at startup still works
- Settings file format is extended (backward compatible)

### Testing Considerations

1. **Unit Tests**:
   - Settings save/load with `crawlFolder` field
   - Empty string handling
   - Path format preservation

2. **Integration Tests**:
   - Folder picker on all platforms
   - Start/stop index building from UI
   - Command-line precedence over UI setting

3. **Manual Testing**:
   - Select folder via UI
   - Start indexing
   - Stop indexing mid-way
   - Change folder and re-index
   - Restart application (verify settings persistence)

## File Changes Summary

### New Files
- None (reuses existing infrastructure)

### Modified Files

1. **Settings**:
   - `src/core/Settings.h`: Add `crawlFolder` field
   - `src/core/Settings.cpp`: Update JSON serialization

2. **UI**:
   - `src/ui/SettingsWindow.h`: Add Index Configuration section (documentation)
   - `src/ui/SettingsWindow.cpp`: Implement folder selection UI
   - Update `SettingsWindow::Render()` signature to accept `IndexBuildState&` and `IIndexBuilder*` (or `Application*`)

3. **Application Logic**:
   - `src/core/Application.h`: Add `index_builder_` member, `StartIndexBuilding()`, `StopIndexBuilding()` methods
   - `src/core/Application.cpp`: Implement runtime index building, update constructor to accept `index_builder`
   - `src/core/main_common.h`: Move `index_builder` ownership to `Application` (pass via constructor)

4. **Initialization**:
   - `src/core/main_common.h`: Update to check settings if no command-line argument

5. **Dependencies** (if needed):
   - `src/ui/SettingsWindow.cpp`: Need access to `IndexBuildState` and `IIndexBuilder`
   - May need to pass additional parameters to `SettingsWindow::Render()`

## Implementation Order

1. **Phase 1**: Settings storage (foundation)
2. **Phase 2**: UI controls (user-facing)
3. **Phase 3**: Runtime index building (core functionality)
4. **Phase 4**: Command-line integration (compatibility)
5. **Phase 5**: Validation and error handling (polish)

## Future Enhancements

1. **Incremental Indexing**: Add new folder to existing index (don't replace)
2. **Multiple Folders**: Support indexing multiple folders simultaneously
3. **Index Management**: UI to view/manage multiple indexes
4. **Folder History**: Remember recently selected folders
5. **Index File Selection**: UI to select index file (similar to folder selection)

## Windows-Specific Considerations

**See detailed analysis**: `docs/plans/features/UI_FOLDER_CRAWL_SELECTION_WINDOWS_ANALYSIS.md`

### Key Windows Behavior

On Windows, the application uses a **privilege-based indexing strategy**:
- **With Admin Privileges**: Uses USN Journal monitoring (preferred, real-time, volume-wide)
- **Without Admin Privileges**: Uses Folder Crawling (fallback, one-time, folder-specific)

### Recommended Approach: Enhanced Approach 1

**Show UI folder selection only when**:
1. No admin rights at startup (primary fallback)
2. USN Journal is not active (runtime fallback)

**Rationale**:
- Maintains architectural clarity: USN Journal = primary, Folder Crawling = fallback
- Simple UI when everything works (most common case)
- Provides graceful fallback for edge cases (USN Journal fails)

**Implementation**:
```cpp
// In SettingsWindow::Render()
bool show_folder_selection = false;
#ifdef _WIN32
  bool has_admin = IsProcessElevated();
  bool usn_active = (monitor && monitor->IsActive());
  
  // Show folder selection if no admin OR USN Journal not active
  if (!has_admin || !usn_active) {
    show_folder_selection = true;
    // Show appropriate message based on state
  }
#else
  // Always show on macOS/Linux (no USN Journal alternative)
  show_folder_selection = true;
#endif
```

**Alternative Approaches Considered**:
- **Approach 2**: Always show UI (even with admin) - rejected: too complex, may confuse users
- **Approach 3**: Conditional with method selection - rejected: unnecessary complexity
- **Approach 4**: Based on runtime state only - rejected: doesn't account for startup privileges

## Questions to Resolve

1. **Index Replacement Confirmation**: Should we show a confirmation dialog when starting a new crawl that will replace the existing index?
   - **Recommendation**: Yes, show confirmation dialog with option to cancel

2. **Settings Window Parameters**: How to pass `IndexBuildState` and `IIndexBuilder` to `SettingsWindow::Render()`?
   - **Option A**: Add parameters to `Render()` method
   - **Option B**: Pass `Application*` pointer (gives access to everything)
   - **Option C**: Create a settings context struct
   - **Recommendation**: Option A (explicit parameters, follows existing pattern)

3. **Index Builder Ownership**: Should `Application` own `IIndexBuilder` or should it be managed elsewhere?
   - **Current**: `main_common.h` creates builder as local variable, `Application` doesn't own it
   - **Recommendation**: **Move ownership to `Application` class** for runtime control
   - **Implementation**: Pass `index_builder` from `main_common.h` to `Application` constructor via `std::move()`
   - **Benefit**: `Application` can start/stop indexing at runtime, cleaner ownership model

4. **Startup Behavior**: If `AppSettings::crawlFolder` is set but no command-line argument, should we auto-start indexing?
   - **Recommendation**: Yes, maintain current behavior (auto-start if folder is configured)

5. **Windows UI Visibility**: When should folder selection UI be visible on Windows?
   - **Recommendation**: Only when no admin rights OR USN Journal not active (Enhanced Approach 1)
   - **See**: `docs/plans/features/UI_FOLDER_CRAWL_SELECTION_WINDOWS_ANALYSIS.md` for detailed analysis

## Success Criteria

1. ✅ User can select a folder via UI (folder picker dialog)
2. ✅ Selected folder is saved to settings and persists across sessions
3. ✅ User can start indexing from UI (not just at startup)
4. ✅ User can stop indexing from UI
5. ✅ Command-line `--crawl-folder` still works and takes precedence
6. ✅ Progress is displayed in status bar (already implemented)
7. ✅ Works on Windows, macOS, and Linux
8. ✅ No breaking changes to existing functionality
