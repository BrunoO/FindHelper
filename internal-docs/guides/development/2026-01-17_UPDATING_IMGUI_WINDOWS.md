# Updating ImGui on Windows

This guide explains how to update the ImGui submodule on Windows.

## Prerequisites

- **Git** installed and in PATH
- **Developer Command Prompt** or **Developer PowerShell** (for Visual Studio)
- Project repository cloned with submodules initialized

## Quick Update (Recommended)

### Using PowerShell

```powershell
# Navigate to project root
cd C:\path\to\USN_WINDOWS

# Update ImGui submodule to latest docking branch
cd external\imgui
git fetch origin
git checkout docking
git pull origin docking
cd ..\..

# Stage and commit the update
git add external\imgui
git commit -m "Update ImGui to latest docking branch"
```

### Using Command Prompt (CMD)

```cmd
REM Navigate to project root
cd C:\path\to\USN_WINDOWS

REM Update ImGui submodule to latest docking branch
cd external\imgui
git fetch origin
git checkout docking
git pull origin docking
cd ..\..

REM Stage and commit the update
git add external\imgui
git commit -m "Update ImGui to latest docking branch"
```

## Step-by-Step Process

### 1. Navigate to Project Root

```powershell
cd C:\path\to\USN_WINDOWS
```

### 2. Check Current ImGui Version

```powershell
cd external\imgui
git log --oneline -1
git status
```

This shows:
- Current commit hash
- Whether you're on the correct branch (should be `docking`)
- If there are any uncommitted changes

### 3. Fetch Latest Changes

```powershell
git fetch origin
```

This downloads the latest commits from the remote repository without modifying your local files.

### 4. Update to Latest Docking Branch

```powershell
git checkout docking
git pull origin docking
```

This:
- Ensures you're on the `docking` branch
- Pulls the latest commits from `origin/docking`

### 5. Verify the Update

```powershell
git log --oneline -1
```

You should see the latest commit (e.g., `f89ef40cb Backends: Win32: fixed an issue...`).

Check the version:
```powershell
Select-String -Path imgui.h -Pattern "IMGUI_VERSION" | Select-Object -First 1
```

Should show: `#define IMGUI_VERSION       "1.92.6 WIP"` (or newer)

### 6. Return to Project Root

```powershell
cd ..\..
```

### 7. Stage the Submodule Update

```powershell
git add external\imgui
```

### 8. Verify What Will Be Committed

```powershell
git status
```

You should see:
```
Changes to be committed:
  modified:   external/imgui
```

### 9. Commit the Update

```powershell
git commit -m "Update ImGui to latest docking branch

- Updated from [old commit] to [new commit]
- Includes critical fixes: [list key fixes]
- Version: [new version]"
```

## Updating to a Specific Version

If you want to update to a specific tagged release instead of the latest:

```powershell
cd external\imgui
git fetch origin --tags
git checkout v1.92.5-docking  # or any other tag
cd ..\..
git add external\imgui
git commit -m "Update ImGui to v1.92.5-docking"
```

## Troubleshooting

### Issue: "Your branch is behind 'origin/docking'"

**Solution**: This is normal. Just run:
```powershell
git pull origin docking
```

### Issue: "You have uncommitted changes"

**Solution**: Either commit or stash your changes first:
```powershell
# Option 1: Commit your changes
git add .
git commit -m "Your changes"

# Option 2: Stash your changes (temporary)
git stash
# ... do the update ...
git stash pop  # Restore your changes
```

### Issue: "Submodule is dirty"

**Solution**: The submodule has uncommitted changes. Clean them:
```powershell
cd external\imgui
git status  # See what's changed
git restore .  # Discard changes
# OR
git add . && git commit -m "Submodule changes"  # Commit changes
```

### Issue: Submodule Not Initialized

**Solution**: Initialize submodules:
```powershell
git submodule update --init --recursive
```

## Verification After Update

### 1. Check Version in Code

After updating, verify the version in `external/imgui/imgui.h`:
```cpp
#define IMGUI_VERSION       "1.92.6 WIP"  // Should match latest
#define IMGUI_VERSION_NUM   19259         // Should match latest
```

### 2. Verify Required Files Exist

```powershell
Test-Path external\imgui\backends\imgui_impl_glfw.cpp
Test-Path external\imgui\backends\imgui_impl_dx11.cpp
Test-Path external\imgui\backends\imgui_impl_opengl3.cpp
Test-Path external\imgui\misc\freetype\imgui_freetype.cpp
```

All should return `True`.

### 3. Build the Project

```powershell
# Configure (if needed)
cmake -S . -B build -A x64

# Build
cmake --build build --config Release
```

If the build succeeds, the update is compatible.

## Checking What Changed

To see what commits were added:

```powershell
cd external\imgui
git log HEAD~109..HEAD --oneline  # Shows last 109 commits
```

To see detailed changes in a specific commit:
```powershell
git show <commit-hash>
```

## Rolling Back (If Needed)

If the update causes issues, you can rollback:

```powershell
# Find the previous commit hash
cd external\imgui
git log --oneline | Select-Object -Skip 109 -First 1

# Checkout the previous version (replace with actual hash)
git checkout 3912b3d9a9c1b3f17431aebafd86d2f40ee6e59c

# Return to project root and commit
cd ..\..
git add external\imgui
git commit -m "Rollback ImGui to v1.92.5-docking"
```

## Best Practices

1. **Always update from the project root** - Don't update submodules from within the submodule directory in a separate terminal
2. **Check for breaking changes** - Review the changelog before updating
3. **Test after updating** - Build and test the application after updating
4. **Commit the update** - Always commit submodule updates so the team stays in sync
5. **Use descriptive commit messages** - Include version numbers and key changes

## Related Documentation

- [ImGui Update Analysis](../analysis/2026-01-17_IMGUI_UPDATE_ANALYSIS.md) - Detailed analysis of what changed
- [Building on Windows](../../platform/windows/BUILDING_ON_WINDOWS.md) - Build instructions
- [ImGui Immediate Mode Paradigm](../../design/IMGUI_IMMEDIATE_MODE_PARADIGM.md) - Understanding ImGui

## Current Status

As of January 17, 2026:
- **Current Version**: Latest docking branch (f89ef40cb)
- **Version String**: 1.92.6 WIP (19259)
- **Last Updated**: January 17, 2026
