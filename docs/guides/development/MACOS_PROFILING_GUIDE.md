# macOS Profiling Guide

This guide explains how to profile the USN_WINDOWS application on macOS using various profiling tools.

## Table of Contents

1. [Overview](#overview)
2. [Prerequisites](#prerequisites)
3. [Method 1: Instruments (Recommended)](#method-1-instruments-recommended)
4. [Method 2: Command-Line Tools](#method-2-command-line-tools)
5. [Method 3: Xcode Profiler](#method-3-xcode-profiler)
6. [Common Profiling Scenarios](#common-profiling-scenarios)
7. [Interpreting Results](#interpreting-results)
8. [Tips and Best Practices](#tips-and-best-practices)

---

## Overview

Profiling helps identify performance bottlenecks, memory leaks, and optimization opportunities. On macOS, you have several options:

- **Instruments** (recommended): Comprehensive GUI tool with multiple profiling templates
- **sample**: Command-line tool for quick CPU profiling
- **Xcode Profiler**: Integrated profiling within Xcode
- **dtrace**: Advanced dynamic tracing (for experts)

---

## Prerequisites

1. **Build the application in Release mode** (for realistic performance):
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ```

2. **Locate the executable**:
   - **App Bundle**: `build/FindHelper.app/Contents/MacOS/FindHelper`
   - **Direct executable**: `build/find_helper` (if not bundled)

3. **Install Xcode Command Line Tools** (if not already installed):
   ```bash
   xcode-select --install
   ```

---

## Method 1: Instruments (Recommended)

**Instruments** is Apple's comprehensive profiling tool, part of Xcode. It provides multiple profiling templates for different analysis types.

### Step 1: Launch Instruments

```bash
# Option 1: From Terminal
open -a Instruments

# Option 2: From Xcode
# Xcode → Open Developer Tool → Instruments
```

### Step 2: Choose a Profiling Template

Instruments offers several templates. For this application, the most useful are:

#### A. Time Profiler (CPU Performance)

**Use case**: Identify CPU bottlenecks, hot functions, slow code paths.

**Steps**:
1. Select **"Time Profiler"** template
2. Click the **"Choose Target"** dropdown → **"Choose Target..."**
3. Navigate to and select: `build/FindHelper.app` or `build/find_helper`
4. Click **Record** (red circle button) or press `Cmd+R`
5. **Use the application** (perform searches, interact with UI)
6. Click **Stop** when done

**What to look for**:
- **Heaviest Stack Trace**: Functions taking the most CPU time
- **Call Tree**: Expand to see which functions call which
- **Invert Call Tree**: Shows who calls a function (useful for finding callers)
- **Hide System Libraries**: Focus on your code

**Tips**:
- Enable **"Invert Call Tree"** to see who calls expensive functions
- Enable **"Hide System Libraries"** to focus on application code
- Use **"Call Tree"** → **"Separate by Thread"** to see per-thread performance

#### B. Allocations (Memory Usage)

**Use case**: Track memory allocations, find memory leaks, identify memory-heavy operations.

**Steps**:
1. Select **"Allocations"** template
2. Choose your application target
3. Click **Record**
4. Use the application
5. Click **Stop**

**What to look for**:
- **Allocations**: Total memory allocated over time
- **Persistent Bytes**: Memory that's still allocated (potential leaks)
- **Statistics**: Objects by type, allocation size distribution
- **Call Trees**: Where allocations occur

**Tips**:
- Look for **persistent allocations** that grow over time (potential leaks)
- Check **"Statistics"** view to see which object types use the most memory
- Use **"Mark Generation"** button to track allocations between specific points

#### C. Leaks (Memory Leaks)

**Use case**: Automatically detect memory leaks.

**Steps**:
1. Select **"Leaks"** template
2. Choose your application target
3. Click **Record**
4. Use the application for several minutes
5. Click **Stop**

**What to look for**:
- **Red bars**: Detected memory leaks
- **Leak Summary**: List of leaked objects
- **Stack Trace**: Where the leak occurred

**Note**: This works best with Objective-C/C++ code. For pure C++ code, use **Allocations** and look for persistent allocations.

#### D. System Trace (Comprehensive)

**Use case**: Full system-level profiling (CPU, memory, I/O, GPU, etc.).

**Steps**:
1. Select **"System Trace"** template
2. Choose your application target
3. Click **Record**
4. Use the application
5. Click **Stop**

**What to look for**:
- **CPU Usage**: Per-core CPU utilization
- **Thread States**: Which threads are running/blocked
- **Memory**: Allocation and deallocation events
- **I/O**: File system operations
- **GPU**: Metal rendering performance (relevant for this app)

**Tips**:
- Use **"Track"** view to see timeline of events
- Filter by process/thread to focus on your application
- Check **"GPU"** track for Metal rendering performance

#### E. Metal System Trace (GPU Performance)

**Use case**: Profile Metal rendering performance (GPU usage, draw calls, shader performance).

**Steps**:
1. Select **"Metal System Trace"** template
2. Choose your application target
3. Click **Record**
4. Use the application (interact with UI)
5. Click **Stop**

**What to look for**:
- **GPU Utilization**: How much the GPU is being used
- **Draw Calls**: Number of draw calls per frame
- **Frame Time**: Time per frame (target: 16.67ms for 60 FPS)
- **Shader Performance**: Time spent in shaders

**Note**: This is especially useful for this application since it uses Metal for rendering.

### Step 3: Analyze Results

1. **Navigate the timeline**: Drag to select time ranges
2. **Inspect call trees**: Expand to see function hierarchies
3. **Filter data**: Use search/filter options
4. **Export data**: File → Export to save results

---

## Method 2: Command-Line Tools

### A. sample (Quick CPU Profiling)

**Use case**: Quick CPU profiling without GUI.

**Steps**:
```bash
# Start the application
./build/FindHelper.app/Contents/MacOS/FindHelper &

# Get the process ID
PID=$(pgrep -f FindHelper)

# Sample for 10 seconds (adjust as needed)
sample $PID 10 -f profile.txt

# View results
cat profile.txt
```

**Options**:
- `-f <file>`: Save output to file
- `-mayDie`: Sample processes that may exit
- `-i <interval>`: Sampling interval in milliseconds (default: 1ms)

**Output**: Shows call stacks and time spent in each function.

### B. dtrace (Advanced Dynamic Tracing)

**Use case**: Custom profiling scripts, advanced analysis.

**Example: Profile function calls**
```bash
# Create a D script to trace function calls
cat > trace_functions.d << 'EOF'
pid$target:find_helper::*entry {
    @[probefunc] = count();
}
EOF

# Run with your application
sudo dtrace -s trace_functions.d -c './build/FindHelper.app/Contents/MacOS/FindHelper'
```

**Note**: `dtrace` requires root access and is more complex. Use Instruments for most use cases.

### C. leaks (Detect Memory Leaks)

**Use case**: Quick leak detection from command line.

**Steps**:
```bash
# Run the application
./build/FindHelper.app/Contents/MacOS/FindHelper &

# Get PID
PID=$(pgrep -f FindHelper)

# Check for leaks
leaks $PID

# Or run with leaks detection
MallocStackLogging=1 ./build/FindHelper.app/Contents/MacOS/FindHelper
```

---

## Method 3: Xcode Profiler

If you're using Xcode for development:

### Steps

1. **Open the project in Xcode** (if you have an Xcode project):
   ```bash
   # Generate Xcode project (if needed)
   cmake -B build -G Xcode
   open build/USN_Monitor.xcodeproj
   ```

2. **Set the scheme** to your executable target

3. **Profile**: Product → Profile (or `Cmd+I`)

4. **Choose template**: Same templates as Instruments

5. **Analyze**: Same workflow as Instruments

**Note**: This method requires an Xcode project. If you're using CMake directly, use Instruments standalone.

---

## Common Profiling Scenarios

### Scenario 1: Slow Search Performance

**Goal**: Find bottlenecks in search operations.

**Steps**:
1. Use **Time Profiler**
2. Start recording
3. Perform several searches (various query lengths, patterns)
4. Stop recording
5. **Look for**:
   - Functions in `SearchWorker`, `StringSearch`, `FileIndex`
   - High CPU usage during search operations
   - Functions called frequently (invert call tree)

**Optimization targets**:
- String matching algorithms
- File index traversal
- Thread pool overhead

### Scenario 2: High Memory Usage

**Goal**: Identify memory-heavy operations.

**Steps**:
1. Use **Allocations**
2. Start recording
3. Load a large index, perform searches
4. Stop recording
5. **Look for**:
   - Large allocations in `FileIndex`
   - Persistent allocations that don't free
   - Allocation spikes during specific operations

**Optimization targets**:
- Index data structures
- Search result storage
- String pooling

### Scenario 3: UI Lag / Low Frame Rate

**Goal**: Find rendering bottlenecks.

**Steps**:
1. Use **Metal System Trace** or **Time Profiler**
2. Start recording
3. Interact with UI (resize, scroll, type)
4. Stop recording
5. **Look for**:
   - Frame time > 16.67ms (60 FPS target)
   - High GPU utilization
   - Expensive ImGui operations
   - Blocking operations on main thread

**Optimization targets**:
- ImGui rendering
- Metal draw calls
- Main thread blocking (move work to background threads)

### Scenario 4: Memory Leaks

**Goal**: Find memory leaks.

**Steps**:
1. Use **Leaks** or **Allocations**
2. Start recording
3. Use application for 5-10 minutes (perform various operations)
4. Stop recording
5. **Look for**:
   - Red bars in Leaks template
   - Persistent allocations that grow over time
   - Objects that should be freed but aren't

**Common leak sources**:
- File handles not closed
- Thread objects not joined
- Callback objects not released

---

## Interpreting Results

### Time Profiler

- **Weight**: Time spent in function (including children)
- **Self Weight**: Time spent in function (excluding children)
- **Symbol Name**: Function name (may be mangled for C++)
- **Call Tree**: Hierarchical view of function calls

**Key metrics**:
- **% of Total Time**: Percentage of total CPU time
- **Count**: Number of times function was called
- **Total Time**: Absolute time spent

### Allocations

- **Allocations**: Number of allocations
- **Size**: Total size of allocations
- **Persistent Bytes**: Memory still allocated
- **Transient Bytes**: Memory allocated then freed

**Key metrics**:
- **Growth Rate**: How fast memory grows
- **Peak Memory**: Maximum memory usage
- **Allocation Size Distribution**: Small vs. large allocations

### Metal System Trace

- **Frame Time**: Time to render one frame
- **GPU Utilization**: Percentage of GPU used
- **Draw Calls**: Number of draw calls per frame
- **Vertex/Fragment Shader Time**: Time in shaders

**Key metrics**:
- **Target Frame Time**: 16.67ms for 60 FPS
- **GPU Bound**: GPU is the bottleneck
- **CPU Bound**: CPU is the bottleneck

---

## Tips and Best Practices

### 1. Profile Release Builds

Always profile **Release** builds for realistic performance:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Debug builds have:
- No optimizations (`-O0`)
- Debug symbols (slower)
- Assertions enabled
- Different performance characteristics

### 2. Use Representative Workloads

Profile with **realistic usage patterns**:
- Typical search queries
- Normal file index sizes
- Common UI interactions
- Extended usage (for leak detection)

### 3. Profile Multiple Times

Run profiling **multiple times** to account for:
- System load variations
- Cache warm-up effects
- JIT compilation (if applicable)

### 4. Focus on Hot Paths

After initial profiling, **focus optimization on hot paths**:
- Functions taking >5% of total time
- Frequently called functions
- Operations in critical paths

### 5. Compare Before/After

When optimizing:
1. Profile **before** changes (baseline)
2. Make optimizations
3. Profile **after** changes
4. Compare results

### 6. Use Symbolication

For readable function names:
- Build with **debug symbols** (`-g` flag)
- Ensure symbols are available
- Instruments will show demangled C++ names

**Note**: Release builds can include debug symbols:
```cmake
# In CMakeLists.txt, add to Release config:
target_compile_options(find_helper PRIVATE
    $<$<CONFIG:Release>:-g>  # Debug symbols
)
```

See [Symbolication for Xcode Profiling](#symbolication-for-xcode-profiling) for detailed instructions.

### 7. Profile on Target Hardware

Profile on the **actual hardware** users will use:
- Apple Silicon (M1/M2/M3) vs. Intel Mac
- Different macOS versions
- Different system loads

### 8. Use Sampling vs. Instrumentation

- **Sampling** (Time Profiler): Low overhead, statistical
- **Instrumentation** (Allocations): Higher overhead, precise

Choose based on what you need:
- **Sampling**: For CPU profiling (low overhead)
- **Instrumentation**: For memory profiling (precise)

### 9. Filter System Libraries

In Instruments, **hide system libraries** to focus on your code:
- Call Tree → **"Hide System Libraries"**
- Focus on application code first
- Then investigate system library calls if needed

### 10. Export and Share Results

Export profiling results for:
- Team review
- Before/after comparisons
- Documentation

**File → Export** in Instruments.

---

## Example Workflow

### Complete Profiling Session

```bash
# 1. Build Release version
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# 2. Launch Instruments
open -a Instruments

# 3. In Instruments:
#    - Select "Time Profiler" template
#    - Choose target: build/FindHelper.app
#    - Click Record

# 4. Use the application:
#    - Load a file index
#    - Perform several searches
#    - Interact with UI

# 5. Stop recording

# 6. Analyze:
#    - Enable "Invert Call Tree"
#    - Enable "Hide System Libraries"
#    - Look for hot functions
#    - Identify optimization targets

# 7. Export results (optional)
#    - File → Export
```

---

---

## Symbolication for Xcode Profiling

**Symbolication** is the process of converting memory addresses and mangled function names into readable function names, file names, and line numbers. Without proper symbolication, profiling results show cryptic names like `_ZN9FileIndex12SearchFilesERKSt6vector...` instead of `FileIndex::SearchFiles()`.

### Understanding macOS Symbols

macOS uses **dSYM bundles** (Debug Symbol bundles) to store debug information separately from the executable. This allows:
- Smaller executables (symbols stored separately)
- Better performance (symbols not loaded unless needed)
- Symbol stripping for distribution (keep symbols for debugging, strip for release)

### Method 1: Enable Symbols in CMake (Recommended)

#### Step 1: Add Debug Symbols to Release Build

Update `CMakeLists.txt` to include debug symbols in Release builds:

```cmake
# Compiler options for macOS
target_compile_options(find_helper PRIVATE
    -std=c++17
    -fobjc-arc
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:
        -g                      # ✅ Add debug symbols
        -O3                    # Maximum optimization
        -flto                  # Link-Time Optimization
        # ... other flags ...
    >
)
```

**Note**: The `-g` flag generates debug symbols. On macOS, this typically creates a `.dSYM` bundle automatically.

#### Step 2: Enable dSYM Generation

CMake can generate dSYM bundles automatically. Add this to `CMakeLists.txt`:

```cmake
# Generate dSYM bundles for macOS
if(APPLE)
    # Enable dSYM generation for Release builds
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    
    # Generate dSYM (debug symbol bundle)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -g")
    set(CMAKE_BUILD_TYPE_RELEASE_DEBUG_SYMBOLS ON)
endif()
```

**Alternative**: Use Xcode generator which handles dSYM automatically:
```bash
cmake -B build -G Xcode -DCMAKE_BUILD_TYPE=Release
```

#### Step 3: Verify Symbols Are Generated

After building, check for dSYM:

```bash
# Check if dSYM was created
ls -la build/FindHelper.app.dSYM/Contents/Resources/DWARF/

# Or for direct executable
ls -la build/find_helper.dSYM/Contents/Resources/DWARF/
```

**Expected output**: Should show a binary file (usually named after your executable).

#### Step 4: Verify Symbols in Executable

```bash
# Check if symbols are embedded (or in dSYM)
nm build/FindHelper.app/Contents/MacOS/FindHelper | grep "T " | head -20

# Or use dwarfdump to verify dSYM
dwarfdump build/FindHelper.app.dSYM | head -50
```

### Method 2: Xcode Project (Automatic Symbolication)

If using Xcode project generator, symbols are handled automatically:

#### Step 1: Generate Xcode Project

```bash
cmake -B build -G Xcode -DCMAKE_BUILD_TYPE=Release
open build/USN_Monitor.xcodeproj
```

#### Step 2: Configure Build Settings

In Xcode:

1. Select your target (`find_helper`)
2. Go to **Build Settings**
3. Search for **"Debug Information Format"**
4. Set to **"DWARF with dSYM File"** (for Release)
5. Search for **"Generate Debug Symbols"**
6. Ensure it's set to **"Yes"**

**Or** set via CMake (recommended):
```cmake
# In CMakeLists.txt, for Xcode generator
if(CMAKE_GENERATOR STREQUAL "Xcode")
    set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym")
    set(CMAKE_XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS "YES")
endif()
```

#### Step 3: Build in Xcode

1. **Product → Build** (or `Cmd+B`)
2. Xcode automatically generates `find_helper.dSYM` in the build directory
3. Location: `build/Debug/` or `build/Release/` (depending on scheme)

#### Step 4: Profile with Symbols

1. **Product → Profile** (or `Cmd+I`)
2. Xcode automatically loads symbols from the dSYM
3. Profiling results show readable function names

### Method 3: Manual Symbol Loading in Instruments

If symbols aren't automatically loaded:

#### Step 1: Locate dSYM

```bash
# Find dSYM bundle
find build -name "*.dSYM" -type d
```

#### Step 2: Load Symbols in Instruments

1. In Instruments, go to **File → Symbols...**
2. Click **"+"** to add symbol location
3. Navigate to your `.dSYM` bundle
4. Click **"Add"**
5. Instruments will use these symbols for symbolication

**Or** set symbol search paths:
1. **Instruments → Preferences → Symbols**
2. Add path to your build directory
3. Instruments will search for dSYM files automatically

### Method 4: Command-Line Symbol Verification

#### Check if Symbols Are Available

```bash
# Use atos to symbolicate addresses (if you have crash addresses)
atos -o build/FindHelper.app/Contents/MacOS/FindHelper -arch arm64 0x12345678

# Or use dSYM
atos -o build/FindHelper.app.dSYM/Contents/Resources/DWARF/FindHelper -arch arm64 0x12345678

# Use nm to list symbols
nm -g build/FindHelper.app/Contents/MacOS/FindHelper | grep "T " | head -20

# Use dwarfdump to inspect dSYM
dwarfdump build/FindHelper.app.dSYM | grep "FileIndex::" | head -10
```

### Ensuring C++ Name Demangling

C++ function names are **mangled** (e.g., `_ZN9FileIndex12SearchFilesEv`). Instruments/Xcode should automatically demangle them, but if you see mangled names:

#### Verify Demangling Works

In Instruments:
1. Look at the **Call Tree** view
2. Function names should show as `FileIndex::SearchFiles()` not `_ZN9FileIndex12SearchFilesEv`
3. If mangled, symbols may not be loaded correctly

#### Manual Demangling (if needed)

```bash
# Use c++filt to demangle names
echo "_ZN9FileIndex12SearchFilesEv" | c++filt
# Output: FileIndex::SearchFiles()
```

### Complete CMake Configuration Example

Here's a complete example for `CMakeLists.txt`:

```cmake
if(APPLE)
    # Compiler options for macOS
    target_compile_options(find_helper PRIVATE
        -std=c++17
        -fobjc-arc
        $<$<CONFIG:Debug>:-g -O0>
        $<$<CONFIG:Release>:
            -g                      # Debug symbols for profiling
            -O3                    # Maximum optimization
            -flto                  # Link-Time Optimization
            -fdata-sections
            -ffunction-sections
            -funroll-loops
            -finline-functions
            -ffast-math
            -fomit-frame-pointer
        >
    )
    
    # Enable dSYM generation (automatic with -g on macOS, but explicit is better)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -g")
    
    # For Xcode generator, ensure dSYM is generated
    if(CMAKE_GENERATOR STREQUAL "Xcode")
        set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym")
        set(CMAKE_XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS "YES")
    endif()
endif()
```

### Troubleshooting Symbol Issues

#### Issue: No symbols in Xcode Profiler

**Symptoms**: Function names show as memory addresses or mangled names.

**Solutions**:

1. **Verify symbols are generated**:
   ```bash
   ls -la build/*.dSYM
   ```

2. **Check build settings**:
   - Ensure `-g` flag is in Release build
   - Verify dSYM is generated

3. **Clean and rebuild**:
   ```bash
   rm -rf build
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ```

4. **Manually load symbols in Instruments**:
   - File → Symbols → Add dSYM location

#### Issue: Symbols work in Debug but not Release

**Cause**: Release builds may strip symbols or not generate dSYM.

**Solution**: Ensure `-g` is in Release config (see CMake example above).

#### Issue: Partial symbolication (some functions missing)

**Cause**: Inlining or LTO may remove some symbols.

**Solution**: 
- Use `-fno-inline-functions` temporarily for profiling (slower but more symbols)
- Or accept that inlined functions won't show up (they're optimized away)

#### Issue: dSYM not found by Xcode

**Solution**: 
1. Check dSYM location matches executable
2. Use **File → Symbols** in Instruments to manually add
3. Ensure dSYM UUID matches executable UUID:
   ```bash
   # Get executable UUID
   dwarfdump -u build/FindHelper.app/Contents/MacOS/FindHelper
   
   # Get dSYM UUID
   dwarfdump -u build/FindHelper.app.dSYM
   
   # UUIDs must match!
   ```

### Best Practices

1. **Always build with symbols for profiling**: Add `-g` to Release builds when profiling
2. **Keep dSYM files**: Don't delete `.dSYM` bundles (they're needed for symbolication)
3. **Match UUIDs**: Ensure dSYM UUID matches executable UUID
4. **Use Xcode generator for convenience**: Xcode handles symbols automatically
5. **Profile Release builds**: Use Release builds with symbols for realistic performance data

---

## Troubleshooting

### Issue: No symbols in Instruments/Xcode

**Solution**: Build with debug symbols and ensure dSYM is generated:
```cmake
# In CMakeLists.txt
target_compile_options(find_helper PRIVATE
    $<$<CONFIG:Release>:-g>  # Debug symbols
)
```

**For Xcode projects**, also ensure:
- Build Settings → Debug Information Format → "DWARF with dSYM File"
- Build Settings → Generate Debug Symbols → "Yes"

**Verify symbols**:
```bash
# Check dSYM exists
ls -la build/*.dSYM

# Verify UUIDs match
dwarfdump -u build/FindHelper.app/Contents/MacOS/FindHelper
dwarfdump -u build/FindHelper.app.dSYM
```

**If symbols still don't load**:
1. In Instruments: File → Symbols → Add dSYM location manually
2. Clean and rebuild: `rm -rf build && cmake -B build ...`

### Issue: Application crashes during profiling

**Solution**: 
- Profile Debug build first to catch crashes
- Check for memory corruption
- Use Leaks template to find leaks

### Issue: High overhead from profiling

**Solution**:
- Use sampling (Time Profiler) instead of instrumentation
- Reduce sampling frequency
- Profile shorter time periods

### Issue: Can't attach to process

**Solution**:
- Ensure application is running
- Check process ID is correct
- Use Instruments GUI instead of command-line tools

---

## Additional Resources

- [Apple Instruments User Guide](https://developer.apple.com/documentation/instruments)
- [Instruments Help](https://help.apple.com/instruments/mac/current/)
- [Performance Best Practices](https://developer.apple.com/library/archive/documentation/Performance/Conceptual/PerformanceOverview/)

---

*Last Updated: 2025-12-25*


