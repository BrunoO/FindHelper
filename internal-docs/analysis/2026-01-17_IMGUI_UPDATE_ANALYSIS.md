# ImGui Update Analysis - January 17, 2026

## Current Status

- **Current Version**: `v1.92.5-docking` (commit `3912b3d9a9c1b3f17431aebafd86d2f40ee6e59c`)
- **Latest Available**: `v1.92.5-docking` (commit `f89ef40cb` - 109 commits ahead)
- **Release Date**: v1.92.5 was released November 20, 2025
- **Branch**: Using `docking` branch (required for multi-viewport and docking features)

## Commits Ahead: 109

The project is 109 commits behind the latest `docking` branch. These are post-release fixes and improvements that have not yet been tagged as a new release.

## Key Changes Since v1.92.5

### Critical Fixes (Recommended to Update)

1. **Win32 Task Bar Icon Fix** (`f89ef40cb`)
   - **Issue**: Newly appearing windows not parented to main viewport don't show task bar icon until explicitly refocused
   - **Impact**: Windows-specific UI issue affecting multi-viewport scenarios
   - **Relevance**: HIGH - This project uses Windows as primary target with docking/viewports

2. **Font Crash Fix** (`f64c7c37e`)
   - **Issue**: Crash when using `AddFont()` with `MergeMode=true` on a font that has already been rendered
   - **Impact**: Potential crash in font loading code
   - **Relevance**: MEDIUM - Project uses custom fonts (FontAwesome, Roboto, etc.)

3. **Texture Crash Fix** (`25158fe33`)
   - **Issue**: Assert/crash when a destroyed texture is recreated without pixel data available
   - **Impact**: Potential crash in texture management
   - **Relevance**: MEDIUM - Project uses textures for icons and fonts

4. **Viewport Focus Fixes** (`c389a9528`, `9eebd37b5`)
   - **Issue**: Multiple viewport focus-related issues (#8948, #9172, #9131, #9128)
   - **Impact**: Window focus and viewport behavior improvements
   - **Relevance**: HIGH - Project uses docking and multi-viewport features

### Windows-Specific Improvements

1. **Unicode Input Support** (`d27dce58c`, `962cc2381`)
   - **Issue**: Support for Unicode inputs on MBCS Windows via WM_IME_CHAR/WM_IME_COMPOSITION
   - **Impact**: Better internationalization support
   - **Relevance**: MEDIUM - May be useful for international users

2. **DirectX12 Warning Fix** (`f0699effe`)
   - **Issue**: Disable breaking on D3D12_MESSAGE_ID_FENCE_ZERO_WAIT warning
   - **Impact**: Cleaner debug output (project uses DirectX11, not DX12)
   - **Relevance**: LOW - Project uses DirectX11, not DirectX12

### Other Notable Fixes

1. **InputText Navigation Fix** (`1566c96cc`)
   - **Issue**: Fixed remote/shortcut InputText() not teleporting mouse cursor when nav cursor is active
   - **Impact**: Better keyboard navigation experience
   - **Relevance**: MEDIUM - Project uses keyboard navigation

2. **Table Display Order Fix** (`cf64b7fa7`)
   - **Issue**: Fixed losing stored display order when reducing column count
   - **Impact**: Table state preservation
   - **Relevance**: LOW - Project may use tables in ResultsTable

3. **Font FreeType Fix** (`9055c9ed2`)
   - **Issue**: Fixed overwriting `ImFontConfig::PixelSnapH` when hinting is enabled
   - **Impact**: Font rendering improvements
   - **Relevance**: MEDIUM - Project uses FreeType for font rendering

4. **Debug Window Fix** (`00dfb3c89`)
   - **Issue**: Fixed implicit/fallback "Debug" window staying visible if once docked
   - **Impact**: UI cleanup
   - **Relevance**: LOW - Minor UI issue

5. **OpenGL3 Backend Fix** (`bd6f48fe2`)
   - **Issue**: Fixed embedded loader multiple init/shutdown cycles broken on some platforms
   - **Impact**: Linux/macOS stability (project uses OpenGL3 on Linux)
   - **Relevance**: MEDIUM - Project supports Linux with OpenGL3

## Breaking Changes

Based on the v1.92.5 release notes, the following breaking changes were introduced:

1. **Legacy Key/Flag Names Commented Out** (since v1.89.0)
   - `ImGuiKey_ModCtrl` → `ImGuiMod_Ctrl`
   - `ImGuiKey_ModShift` → `ImGuiMod_Shift`
   - `ImGuiKey_ModAlt` → `ImGuiMod_Alt`
   - `ImGuiKey_ModSuper` → `ImGuiMod_Super`

2. **Legacy API Commented Out** (since v1.89.8)
   - `io.ClearInputCharacters()` → Use `io.ClearInputKeys()` instead

3. **Legacy BeginChild Flags** (moved to `ImGuiChildFlags`)
   - `ImGuiWindowFlags_NavFlattened` → `ImGuiChildFlags_NavFlattened`

4. **Deprecated Functions** (since v1.89.7)
   - `SetItemAllowOverlap()` → Use `SetNextItemAllowOverlap()` instead

**Action Required**: Search codebase for these deprecated APIs and update if found.

## Compatibility Check

✅ **COMPATIBILITY VERIFIED**: The codebase has been checked and does **NOT** use any deprecated ImGui APIs:
- ❌ No usage of `ImGuiKey_Mod*` (legacy key names)
- ❌ No usage of `ClearInputCharacters()` (legacy API)
- ❌ No usage of `SetItemAllowOverlap()` (deprecated function)
- ❌ No usage of `ImGuiWindowFlags_NavFlattened` (moved to `ImGuiChildFlags`)
- ❌ No usage of `BeginChildFrame()`, `EndChildFrame()`, `ShowStackToolWindow()`, `IM_OFFSETOF()`, `IM_FLOOR()` (commented out in v1.92.5)

**Result**: No code changes required for compatibility. The update is safe to proceed.

## Recommendation

### ✅ **RECOMMENDED: Update to Latest Docking Branch**

**Rationale:**
1. **Critical Windows Fix**: The Win32 task bar icon fix is directly relevant to this Windows-primary project
2. **Crash Fixes**: Multiple crash fixes (fonts, textures) improve stability
3. **Viewport Fixes**: Important for docking/multi-viewport features used by the project
4. **No Breaking Changes in Post-Release Commits**: The 109 commits are bug fixes and improvements, not breaking changes
5. **Low Risk**: The docking branch is actively maintained and regularly synced with master

**Update Process:**
```bash
cd external/imgui
git fetch origin
git checkout docking
git pull origin docking
cd ../..
git add external/imgui
git commit -m "Update ImGui to latest docking branch (109 commits ahead of v1.92.5)"
```

**Testing Checklist:**
- [ ] Verify Windows build compiles successfully
- [ ] Test window docking and multi-viewport behavior
- [ ] Verify font loading (FontAwesome, Roboto, etc.)
- [ ] Test keyboard navigation
- [ ] Verify table functionality (if used)
- [ ] Test on Linux (OpenGL3 backend fix)
- [ ] Check for any deprecated API usage (see Breaking Changes section)

## Alternative: Wait for Next Release

If you prefer to wait for a tagged release:
- **Pros**: Tagged releases are more stable, have release notes
- **Cons**: Missing 109 commits of bug fixes and improvements
- **Timeline**: Unknown - v1.92.5 was released Nov 20, 2025, next release date unknown

## Risk Assessment

- **Risk Level**: **LOW**
  - Post-release commits are typically bug fixes
  - Docking branch is actively maintained
  - No breaking changes in the 109 commits
  - Easy to rollback if issues occur (git submodule)

- **Impact of Not Updating**: 
  - Missing Windows task bar icon fix (UI issue)
  - Missing crash fixes (stability)
  - Missing viewport focus fixes (UX)

## Conclusion

**Recommendation: Update to latest docking branch**

The benefits (critical Windows fix, crash fixes, viewport improvements) outweigh the minimal risk. The 109 commits are primarily bug fixes and improvements with no breaking changes. The update process is straightforward and easily reversible.

---

## Update Status: ✅ COMPLETED

**Date**: January 17, 2026  
**Commit**: `4cb582f`  
**Updated From**: v1.92.5-docking (3912b3d9a)  
**Updated To**: Latest docking branch (f89ef40cb)  
**Version**: 1.92.6 WIP (19259)  
**Commits Applied**: 109 commits

**Files Changed**:
- `external/imgui` submodule updated

**Next Steps**:
1. ✅ Update completed and committed
2. ⏳ Test Windows build (when on Windows)
3. ⏳ Test window docking and multi-viewport behavior
4. ⏳ Verify font loading (FontAwesome, Roboto, etc.)
5. ⏳ Test keyboard navigation
6. ⏳ Test on Linux (OpenGL3 backend fix)
