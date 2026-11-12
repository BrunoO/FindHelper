# Configuring Instruments to Show Source Code

This guide explains how to configure Instruments (macOS profiling tool) to display source code alongside profiling data.

In the commands below, `$PROJECT_ROOT` is the path to your clone of the repository (the directory containing `CMakeLists.txt`). For example: `export PROJECT_ROOT=$(pwd)` when you are in the repo root.

## Prerequisites

Your project already has debug symbols enabled in `CMakeLists.txt`:
- `-g` flag in Release builds (line 274)
- dSYM generation configured (lines 286-307)

## Step 1: Verify dSYM Generation

After building, verify the dSYM bundle exists:

```bash
# For app bundle
ls -la build/FindHelper.app.dSYM/Contents/Resources/DWARF/

# For direct executable
ls -la build/find_helper.dSYM/Contents/Resources/DWARF/
```

**Expected**: Should show a binary file (usually `FindHelper` or `find_helper`).

## Step 2: Build with Source Path Information

The dSYM contains source file paths. Ensure these are absolute paths:

```bash
# Build from the project root (not a subdirectory)
cd $PROJECT_ROOT
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

**Important**: Build from the project root so source paths in dSYM are correct.

## Step 3: Configure Instruments Source Paths

### Method A: Automatic (Recommended)

Instruments should automatically find source code if:
1. The source code is in the same location as when you built
2. The dSYM contains correct source paths

**Verify source paths in dSYM**:
```bash
# Check source paths stored in dSYM
dwarfdump --debug-info build/FindHelper.app.dSYM | grep -A 5 "DW_AT_comp_dir\|DW_AT_name" | head -20
```

**Expected output**: Should show paths like:
```
DW_AT_comp_dir : <your-project-root-path>
DW_AT_name     : Application.cpp
```

### Method B: Manual Source Path Configuration

If Instruments can't find source files automatically:

1. **Launch Instruments**:
   ```bash
   open -a Instruments
   ```

2. **Start a profiling session**:
   - Choose a template (e.g., "Time Profiler")
   - Select your app: `build/FindHelper.app` or `build/find_helper`
   - Click "Record"

3. **Configure Source Paths**:
   - **File → Symbols → Add dSYM...**
   - Navigate to: `build/FindHelper.app.dSYM` (or `build/find_helper.dSYM`)
   - Click "Open"

4. **Set Source Code Location**:
   - **File → Symbols → Source Paths...**
   - Click the **"+"** button
   - Add your source directory (project root path, e.g. `$PROJECT_ROOT`)
   - Click "OK"

5. **Verify Source Code Display**:
   - In the profiling view, double-click a function name
   - You should see the source code in the detail pane
   - If not, check the source path mapping

## Step 4: Verify Source Code is Visible

### In Time Profiler:

1. **Record a profiling session**
2. **Stop recording**
3. **Double-click a function** in the call tree
4. **Check the detail pane** (bottom of Instruments window)
5. **Source code should appear** with line numbers and highlighting

### If Source Code Doesn't Appear:

1. **Check symbolication**:
   - Function names should be demangled (e.g., `FileIndex::SearchFiles()` not `_ZN9FileIndex12SearchFilesEv`)
   - If mangled, symbols aren't loaded correctly

2. **Verify dSYM location**:
   ```bash
   # Check if dSYM is next to executable
   ls -la build/FindHelper.app.dSYM
   ```

3. **Check source paths in dSYM**:
   ```bash
   dwarfdump build/FindHelper.app.dSYM | grep "DW_AT_comp_dir" | head -5
   ```
   Should show your project root path (the directory containing `CMakeLists.txt`).

4. **Rebuild if paths changed**:
   If you moved the project or built from a different location:
   ```bash
   rm -rf build
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ```

## Step 5: Advanced Configuration

### Persistent Source Paths

To avoid reconfiguring every time:

1. **File → Symbols → Source Paths...**
2. Add your source directory
3. Check **"Save for all projects"** (if available)
4. Click "OK"

### Multiple Source Locations

If your project uses external dependencies:

1. **File → Symbols → Source Paths...**
2. Add each source directory:
   - Your project root (main source)
   - `/path/to/external/lib` (if needed)
3. Click "OK"

### Symbol Server Configuration

For production builds with stripped symbols:

1. **File → Symbols → Symbol Server...**
2. Configure symbol server URL (if using one)
3. Or add local dSYM paths

## Troubleshooting

### Issue: "Source code not available"

**Symptoms**: Function names appear but no source code.

**Solutions**:
1. Verify dSYM exists: `ls -la build/*.dSYM`
2. Check source paths: `dwarfdump build/FindHelper.app.dSYM | grep "DW_AT_comp_dir"`
3. Rebuild from project root
4. Manually add source path in Instruments

### Issue: "Wrong source code shown"

**Symptoms**: Source code appears but is from wrong file/location.

**Cause**: Source paths in dSYM don't match current file locations.

**Solution**: Rebuild from the correct directory.

### Issue: "Partial source code" (some files missing)

**Symptoms**: Some files show source, others don't.

**Cause**: Mixed build paths or moved source files.

**Solution**: 
1. Clean build: `rm -rf build`
2. Rebuild from project root
3. Ensure all source files are in expected locations

### Issue: "Source code shows but line numbers are wrong"

**Symptoms**: Source appears but highlighting is incorrect.

**Cause**: Optimizations may inline code or reorder it.

**Solution**: 
- This is normal with `-O3` optimization
- Consider profiling with `-O2` for more accurate line numbers
- Or use Debug build for exact line mapping (slower but accurate)

## Quick Reference

```bash
# 1. Build with symbols
cd $PROJECT_ROOT
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# 2. Verify dSYM
ls -la build/FindHelper.app.dSYM

# 3. Check source paths
dwarfdump build/FindHelper.app.dSYM | grep "DW_AT_comp_dir"

# 4. Launch Instruments
open -a Instruments

# 5. In Instruments:
#    - File → Symbols → Add dSYM... → Select build/FindHelper.app.dSYM
#    - File → Symbols → Source Paths... → Add your project root path
#    - Record profiling session
#    - Double-click function to see source code
```

## Additional Resources

- [Apple's Instruments User Guide](https://developer.apple.com/documentation/instruments)
- [dSYM Documentation](https://developer.apple.com/documentation/xcode/building-your-app-to-include-debugging-information)
- Project's profiling guide: `docs/MACOS_PROFILING_GUIDE.md`

