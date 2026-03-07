# UI Folder Crawl Selection - Impact Summary

## Quick Overview

**Goal**: Allow users to select a folder to crawl from the UI (Settings window) instead of requiring the `--crawl-folder` command-line argument.

**Status**: Planning phase - Implementation plan created

## Key Architectural Changes

### 1. Settings Storage
- **Add**: `std::string crawlFolder` field to `AppSettings` struct
- **Impact**: Minimal - extends existing JSON settings file
- **Files**: `src/core/Settings.h`, `src/core/Settings.cpp`

### 2. Index Builder Ownership
- **Current**: `IIndexBuilder` created in `main_common.h` as local variable
- **Change**: Move ownership to `Application` class
- **Impact**: **Moderate** - requires refactoring constructor signatures
- **Files**: `src/core/Application.h`, `src/core/Application.cpp`, `src/core/main_common.h`
- **Rationale**: Enables runtime control (start/stop indexing from UI)

### 3. UI Controls
- **Add**: "Index Configuration" section in Settings window
- **Features**: Folder picker button, Start/Stop indexing buttons
- **Impact**: Low - adds new UI section, reuses existing folder picker
- **Files**: `src/ui/SettingsWindow.h`, `src/ui/SettingsWindow.cpp`

### 4. Runtime Index Building
- **Add**: `StartIndexBuilding()`, `StopIndexBuilding()` methods to `Application`
- **Impact**: **Moderate** - new runtime behavior (previously only at startup)
- **Files**: `src/core/Application.h`, `src/core/Application.cpp`

## Impact Analysis by Category

### ✅ Positive Impacts

1. **User Experience**
   - No restart required to change indexed folder
   - Native folder picker on all platforms
   - Settings persist across sessions
   - Visual progress feedback (already implemented)

2. **Flexibility**
   - Runtime folder selection
   - Can re-index without restart
   - Command-line still works (backward compatible)

3. **Code Quality**
   - Reuses existing infrastructure (`IIndexBuilder`, folder picker)
   - Follows existing patterns (settings persistence)
   - No breaking changes

### ⚠️ Potential Issues

1. **Index Replacement**
   - **Issue**: New crawl replaces entire index (not incremental)
   - **Mitigation**: Show confirmation dialog before starting new crawl
   - **Future**: Consider incremental indexing feature

2. **State Management**
   - **Issue**: Complex state transitions (idle → indexing → complete/failed)
   - **Mitigation**: Use existing `IndexBuildState` (thread-safe, already handles state)
   - **Status**: Already handled by existing code

3. **Threading**
   - **Issue**: Starting new crawl while one is in progress
   - **Mitigation**: `StartIndexBuilding()` stops existing builder first
   - **Status**: `IIndexBuilder::Stop()` is already thread-safe

4. **Architecture**
   - **Issue**: Moving `IIndexBuilder` ownership requires constructor changes
   - **Mitigation**: Pass via `std::move()` from `main_common.h` to `Application`
   - **Complexity**: Moderate - affects initialization flow

### 🔒 Breaking Changes

**None** - This is a purely additive feature:
- Command-line `--crawl-folder` still works (takes precedence)
- Existing startup behavior unchanged
- Settings file format extended (backward compatible)

## Dependencies

### Existing Components (Reused)
- ✅ `ShowFolderPickerDialog()` - Already implemented for all platforms
- ✅ `IIndexBuilder` interface - Already supports Start/Stop
- ✅ `IndexBuildState` - Already thread-safe and UI-integrated
- ✅ `AppSettings` JSON persistence - Already working
- ✅ `FolderCrawlerIndexBuilder` - Already implements folder crawling

### New Dependencies
- None - all required functionality already exists

## Testing Requirements

### Unit Tests
- Settings save/load with `crawlFolder` field
- Empty string handling
- Path format preservation (Windows/Unix)

### Integration Tests
- Folder picker on Windows, macOS, Linux
- Start/stop index building from UI
- Command-line precedence over UI setting
- Settings persistence across sessions

### Manual Testing
- Select folder via UI
- Start indexing
- Stop indexing mid-way
- Change folder and re-index
- Restart application (verify settings persistence)
- Command-line `--crawl-folder` still works

## Implementation Complexity

| Phase | Complexity | Risk Level |
|-------|-----------|------------|
| Phase 1: Settings Storage | Low | Low |
| Phase 2: UI Controls | Low | Low |
| Phase 3: Runtime Index Building | **Moderate** | **Medium** |
| Phase 4: Command-Line Integration | Low | Low |
| Phase 5: Validation | Low | Low |

**Overall**: Moderate complexity due to ownership refactoring, but low risk due to existing infrastructure.

## Files Changed Summary

### New Files
- None

### Modified Files (Estimated)
- `src/core/Settings.h` - Add `crawlFolder` field
- `src/core/Settings.cpp` - Update JSON serialization
- `src/ui/SettingsWindow.h` - Document new section
- `src/ui/SettingsWindow.cpp` - Implement folder selection UI
- `src/core/Application.h` - Add `index_builder_` member, runtime methods
- `src/core/Application.cpp` - Implement runtime index building
- `src/core/main_common.h` - Move `index_builder` ownership to `Application`

**Total**: ~7 files modified, ~200-300 lines of code added

## Success Criteria

1. ✅ User can select folder via UI (folder picker dialog)
2. ✅ Selected folder saved to settings (persists across sessions)
3. ✅ User can start indexing from UI (not just at startup)
4. ✅ User can stop indexing from UI
5. ✅ Command-line `--crawl-folder` still works (takes precedence)
6. ✅ Progress displayed in status bar (already implemented)
7. ✅ Works on Windows, macOS, and Linux
8. ✅ No breaking changes

## Questions Resolved

1. **Index Builder Ownership**: Move to `Application` class ✅
2. **UI Location**: Settings window ✅
3. **Command-Line Precedence**: Command-line takes precedence ✅
4. **Index Replacement**: Show confirmation dialog ✅
5. **Settings Window Parameters**: Add `IndexBuildState&` and `IIndexBuilder*` parameters ✅

## Next Steps

1. Review and approve implementation plan
2. Implement Phase 1 (Settings Storage)
3. Implement Phase 2 (UI Controls)
4. Implement Phase 3 (Runtime Index Building) - **Most complex**
5. Implement Phase 4 (Command-Line Integration)
6. Implement Phase 5 (Validation)
7. Testing and validation
8. Documentation updates

## Related Documentation

- Full implementation plan: `docs/plans/features/UI_FOLDER_CRAWL_SELECTION_PLAN.md`
- Index building architecture: `src/core/IndexBuilder.h`
- Settings structure: `src/core/Settings.h`
- Folder picker implementation: `src/platform/*/FileOperations_*.cpp`
