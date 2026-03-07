# Phase 2: Feature Parity Goal

## Overview

**Goal**: Achieve **complete feature parity** between macOS and Windows for all UI and search capabilities.

The macOS application will have **identical functionality** to Windows, with only two platform-specific differences:
1. **Index Population**: macOS uses `--index-from-file` command-line argument (no real-time USN monitoring)
2. **Real-time Monitoring**: macOS does not support real-time file system monitoring (USN is Windows-specific)

---

## Feature Parity Checklist

### ✅ Search Functionality (100% Parity)

| Feature | Windows | macOS | Status |
|---------|---------|-------|--------|
| Path pattern search | ✅ | ✅ | Identical |
| Filename pattern search | ✅ | ✅ | Identical |
| Extension filter | ✅ | ✅ | Identical |
| Folders-only filter | ✅ | ✅ | Identical |
| Case-sensitive/insensitive toggle | ✅ | ✅ | Identical |
| Time filter (Today, This Week, This Month, This Year, Older) | ✅ | ✅ | Identical |
| Auto-search (debounced) | ✅ | ✅ | Identical |
| Auto-refresh | ✅ | ✅ | Identical |
| Manual search trigger (F5) | ✅ | ✅ | Identical |
| Search result sorting (all 5 columns) | ✅ | ✅ | Identical |
| Saved searches (save, load, delete) | ✅ | ✅ | Identical |

### ✅ UI Components (100% Parity)

| Component | Windows | macOS | Status |
|-----------|---------|-------|--------|
| Search input fields (path, filename, extensions) | ✅ | ✅ | Identical |
| Search controls (Search button, checkboxes, Clear All, Help) | ✅ | ✅ | Identical |
| Search results table | ✅ | ✅ | Identical |
| Status bar (progress, result count, index stats) | ✅ | ✅ | Identical |
| Settings window | ✅ | ✅ | Identical |
| Metrics window | ✅ | ✅ | Identical |
| Help popups (search tips, keyboard shortcuts, regex generator) | ✅ | ✅ | Identical |
| Saved search popups | ✅ | ✅ | Identical |

### ✅ Keyboard Shortcuts (100% Parity)

| Shortcut | Windows | macOS | Status |
|----------|---------|-------|--------|
| Ctrl+F (focus filename input) | ✅ | ✅ | Identical |
| F5 (refresh search) | ✅ | ✅ | Identical |
| Escape (clear all filters) | ✅ | ✅ | Identical |

### ✅ Settings & Persistence (100% Parity)

| Feature | Windows | macOS | Status |
|---------|---------|-------|--------|
| Font family selection | ✅ | ✅ | Identical |
| Font size configuration | ✅ | ✅ | Identical |
| Font scale (global UI scale) | ✅ | ✅ | Identical |
| Window size persistence | ✅ | ✅ | Identical |
| Settings JSON file | ✅ | ✅ | Identical |
| Saved searches persistence | ✅ | ✅ | Identical |

### ⚠️ Platform-Specific Differences (By Design)

| Feature | Windows | macOS | Notes |
|---------|---------|-------|-------|
| Index population | USN Journal (real-time) | File-based (static) | macOS uses `--index-from-file` |
| Real-time monitoring | ✅ USN Journal | ❌ Not supported | USN is Windows-specific |
| File operations | Windows shell (context menu, drag-drop) | macOS Finder (open, reveal) | Different UI, same functionality |
| Rendering backend | DirectX 11 | Metal | Platform-specific graphics API |

---

## Implementation Status

### ✅ Completed (Phase 1 & Step 1.6)

- [x] Cross-platform `UIRenderer` with `NativeWindowHandle`
- [x] Extracted inline UI code to `UIRenderer` methods
- [x] Extracted main window rendering to `UIRenderer::RenderMainWindow()`
- [x] Extracted application logic to `ApplicationLogic` namespace
- [x] Extracted time filter utilities to `TimeFilterUtils`
- [x] Extracted search result utilities to `SearchResultUtils`
- [x] Extracted Windows bootstrap to `AppBootstrap`
- [x] Extracted Windows font utilities to `FontUtils_win`
- [x] macOS build system with GLFW + Metal + ImGui
- [x] macOS stub UI with index statistics display

### 📋 Remaining (Step 2)

- [ ] Add search state management to `main_mac.mm`
- [ ] Load settings in `main_mac.mm`
- [ ] Replace stub UI with full UI using `ApplicationLogic::Update()` and `UIRenderer::RenderMainWindow()`
- [ ] Verify all UI components work (search inputs, controls, results table, status bar)
- [ ] Verify settings window works
- [ ] Verify metrics window works
- [ ] Verify saved searches work
- [ ] Verify keyboard shortcuts work
- [ ] Update `CMakeLists.txt` to include all required source files
- [ ] Test macOS build and verify feature parity

### 📋 Remaining (Step 4)

- [ ] Implement macOS file operations (open in Finder, reveal in Finder, copy path)
- [ ] Test macOS file operations

---

## Success Criteria

Phase 2 is complete when:

1. ✅ **All search features work identically** on macOS and Windows
2. ✅ **All UI components are identical** in appearance and behavior
3. ✅ **All keyboard shortcuts work identically** on both platforms
4. ✅ **Settings and metrics windows work identically** on both platforms
5. ✅ **Saved searches work identically** on both platforms
6. ✅ **File operations work** (platform-specific implementations, but same core functionality)
7. ✅ **Code compiles and runs** on both Windows and macOS
8. ✅ **No code duplication** (all UI logic in `UIRenderer`, all application logic in `ApplicationLogic`)

---

## Key Differences (Platform-Specific by Design)

### Index Population

**Windows**:
- Real-time USN Journal monitoring
- Automatic index updates as files change
- No manual intervention required

**macOS**:
- Static index loaded from file via `--index-from-file` command-line argument
- Index must be manually reloaded when files change
- Example: `./FindHelper --index-from-file /path/to/index.json`

**Impact**: macOS users must regenerate the index file on Windows and reload it, but search functionality is identical once the index is loaded.

### Real-time Monitoring

**Windows**:
- USN Journal provides real-time file system change notifications
- Index automatically updates as files are created, modified, or deleted

**macOS**:
- No real-time monitoring (USN is Windows-specific)
- Index is static until manually reloaded

**Future**: Phase 3 could add FSEvents-based monitoring for macOS (optional enhancement).

### File Operations

**Windows**:
- Shell integration (context menu, drag-and-drop, explore folder)
- Uses Windows Shell APIs

**macOS**:
- Native macOS operations (open in Finder, reveal in Finder, copy path)
- Uses macOS Cocoa APIs

**Impact**: Different user interactions, but same core functionality (opening files, navigating folders).

---

## Testing Strategy

### Feature Parity Testing

For each feature in the checklist above:
1. Test on Windows
2. Test on macOS
3. Verify identical behavior (where applicable)
4. Document any platform-specific differences

### Platform-Specific Testing

1. **Windows**: Verify USN monitoring works, shell integration works
2. **macOS**: Verify index loading from file works, Finder operations work

---

## Notes

- **Feature parity does not mean identical code**: Platform-specific implementations are expected (e.g., DirectX vs Metal, Windows shell vs macOS Finder).
- **Feature parity means identical functionality**: Users should have the same search and UI experience on both platforms.
- **Platform-specific differences are by design**: Index population and real-time monitoring differ due to platform capabilities, not by choice.

