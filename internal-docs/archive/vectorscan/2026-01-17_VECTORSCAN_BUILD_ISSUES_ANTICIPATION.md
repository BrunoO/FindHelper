# VectorScan Integration - Anticipated Build Issues (Linux & Windows)

**Date:** 2026-01-17  
**Scope:** VectorScan integration build issues on Linux and Windows  
**Reference:** `docs/guides/building/BUILD_ISSUES_ANTICIPATION.md`

---

## Executive Summary

This document anticipates potential build issues when building the VectorScan integration on Linux and Windows platforms. The integration is designed with graceful fallback mechanisms, but several platform-specific issues may occur during the build process.

**Overall Risk:** ⚠️ **MEDIUM** - Most issues are non-blocking (fallback to std::regex), but some may cause build failures if VectorScan is explicitly required.

---

## Windows-Specific Build Issues

### 1. ⚠️ vcpkg Integration Issues

**Location:** `CMakeLists.txt` lines 805-810

**Potential Issues:**

1. **vcpkg not configured:**
   - `find_package(vectorscan)` may fail if vcpkg is not integrated with CMake
   - CMake may not find vcpkg toolchain file

2. **vcpkg toolchain not set:**
   - User may not have set `CMAKE_TOOLCHAIN_FILE` pointing to vcpkg
   - Build will fail if `USE_VECTORSCAN=ON` and vcpkg is not configured

**Symptoms:**
```
CMake Error: Could not find a package configuration file provided by "vectorscan"
```

**Solutions:**
1. **Configure vcpkg toolchain:**
   ```bash
   cmake -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ..
   ```

2. **Install VectorScan via vcpkg:**
   ```bash
   vcpkg install vectorscan
   ```

3. **Use AUTO mode (recommended):**
   ```bash
   cmake -DUSE_VECTORSCAN=AUTO ..
   ```
   This will gracefully fallback to std::regex if VectorScan is not found.

**Prevention:** Document vcpkg setup in build instructions.

---

### 2. ⚠️ Library Naming Mismatch (Windows)

**Location:** `CMakeLists.txt` lines 828-838

**Potential Issue:**
- Windows libraries may be named `vectorscan.lib` or `hs.lib`
- vcpkg may provide `vectorscan::vectorscan` target, but manual detection looks for `hs` or `vectorscan`
- Library file extension may be `.lib` (MSVC) or `.a` (MinGW)

**Symptoms:**
```
LINK : fatal error LNK1104: cannot open file 'hs.lib'
```

**Current Detection Logic:**
```cmake
find_library(VECTORSCAN_LIB
    NAMES hs vectorscan
    PATHS
        ENV VECTORSCAN_ROOT/lib
        ENV VECTORSCAN_ROOT
)
```

**Solutions:**
1. **Set VECTORSCAN_ROOT environment variable:**
   ```cmd
   set VECTORSCAN_ROOT=C:\path\to\vectorscan
   ```

2. **Use vcpkg (recommended):**
   - vcpkg provides proper CMake targets
   - No manual path configuration needed

3. **Verify library name:**
   - Check actual library name in vcpkg installation
   - May need to adjust `NAMES` in `find_library`

**Prevention:** Test with both vcpkg and manual installation methods.

---

### 3. ⚠️ Include Path Issues (Windows)

**Location:** `CMakeLists.txt` lines 841-849

**Potential Issues:**

1. **Header location mismatch:**
   - VectorScan headers may be in `include/hs/hs.h` or `include/vectorscan/hs.h`
   - vcpkg may use different include structure

2. **Include path not set:**
   - Manual detection may find library but not headers
   - `VECTORSCAN_INCLUDE_DIR` may be empty

**Symptoms:**
```
fatal error C1083: Cannot open include file: 'hs/hs.h'
```

**Current Detection Logic:**
```cmake
find_path(VECTORSCAN_INCLUDE_DIR
    NAMES hs/hs.h
    PATHS
        ENV VECTORSCAN_ROOT/include
        ENV VECTORSCAN_ROOT
)
```

**Solutions:**
1. **Verify header location:**
   - Check actual header path in vcpkg installation
   - May need to adjust `NAMES` in `find_path`

2. **Set VECTORSCAN_ROOT:**
   ```cmd
   set VECTORSCAN_ROOT=C:\path\to\vectorscan
   ```

3. **Use vcpkg (recommended):**
   - vcpkg handles include paths automatically

**Prevention:** Test with both vcpkg and manual installation.

---

### 4. ⚠️ Linker Errors (Windows)

**Location:** `CMakeLists.txt` lines 864-883

**Potential Issues:**

1. **Library not linked:**
   - `VECTORSCAN_LIBRARIES` from pkg-config may be empty
   - Manual detection may find library but linking fails

2. **Wrong library architecture:**
   - x86 library linked to x64 build (or vice versa)
   - MSVC library linked to MinGW build (or vice versa)

3. **Missing dependencies:**
   - VectorScan may depend on other libraries (e.g., pthreads on MinGW)

**Symptoms:**
```
LINK : fatal error LNK1104: cannot open file 'vectorscan.lib'
error LNK2019: unresolved external symbol hs_compile
```

**Solutions:**
1. **Verify library architecture:**
   - Ensure library matches build architecture (x86 vs x64)
   - Ensure library matches compiler (MSVC vs MinGW)

2. **Check library dependencies:**
   - VectorScan may require additional libraries
   - Check vcpkg or VectorScan documentation

3. **Use vcpkg (recommended):**
   - vcpkg handles dependencies automatically

**Prevention:** Test with both MSVC and MinGW builds.

---

### 5. ⚠️ PGO Compatibility (Windows)

**Location:** `CMakeLists.txt` - VectorScan linking section

**Potential Issue:**
- If VectorScan is linked as a static library, PGO flags may need to match
- VectorScan object files compiled without PGO flags may cause LNK1269 errors

**Symptoms:**
```
error LNK1269: multiple .pgd files found
```

**Current Status:**
- VectorScan is linked as external library (not compiled with project)
- PGO flags should not affect external libraries
- **Risk:** Low - external libraries are pre-compiled

**Solutions:**
1. **Use vcpkg (recommended):**
   - vcpkg libraries are pre-compiled, no PGO issues

2. **If building VectorScan from source:**
   - Ensure VectorScan is built with matching compiler flags
   - Or disable PGO for VectorScan build

**Prevention:** Test with PGO enabled builds.

---

## Linux-Specific Build Issues

### 6. ⚠️ pkg-config Not Available (Linux)

**Location:** `CMakeLists.txt` lines 813-822

**Potential Issue:**
- `pkg-config` may not be installed on all Linux systems
- `find_package(PkgConfig)` may fail silently
- Build will fallback to manual detection, but may miss system-installed VectorScan

**Symptoms:**
```
VectorScan: Not found (vs: prefix will fallback to std::regex)
```

**Current Detection Logic:**
```cmake
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(VECTORSCAN QUIET vectorscan)
    ...
endif()
```

**Solutions:**
1. **Install pkg-config:**
   ```bash
   sudo apt-get install pkg-config  # Ubuntu/Debian
   sudo dnf install pkgconfig      # Fedora
   ```

2. **Use manual detection:**
   - Set `VECTORSCAN_ROOT` environment variable
   - Or install VectorScan in standard locations (`/usr/lib`, `/usr/include`)

3. **Use AUTO mode (recommended):**
   - Build will succeed without VectorScan (falls back to std::regex)

**Prevention:** Document pkg-config requirement in build instructions.

---

### 7. ⚠️ Library Naming Mismatch (Linux)

**Location:** `CMakeLists.txt` lines 828-838

**Potential Issues:**

1. **Library name variations:**
   - System packages may name library `libhs.so` or `libvectorscan.so`
   - Package manager may install with different naming

2. **Architecture-specific paths:**
   - x86_64: `/usr/lib/x86_64-linux-gnu/`
   - ARM64: `/usr/lib/aarch64-linux-gnu/`
   - Current detection may miss architecture-specific paths

**Current Detection Logic:**
```cmake
find_library(VECTORSCAN_LIB
    NAMES hs vectorscan
    PATHS
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        /usr/lib64
        ...
)
```

**Solutions:**
1. **Add architecture-specific paths:**
   ```cmake
   if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
       list(APPEND SEARCH_PATHS /usr/lib/x86_64-linux-gnu)
   elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
       list(APPEND SEARCH_PATHS /usr/lib/aarch64-linux-gnu)
   endif()
   ```

2. **Use pkg-config (recommended):**
   - pkg-config handles architecture-specific paths automatically

3. **Set VECTORSCAN_ROOT:**
   ```bash
   export VECTORSCAN_ROOT=/usr
   ```

**Prevention:** Test on multiple Linux distributions and architectures.

---

### 8. ⚠️ Include Path Issues (Linux)

**Location:** `CMakeLists.txt` lines 841-849

**Potential Issues:**

1. **Header location variations:**
   - System packages may install headers in `/usr/include/hs/` or `/usr/include/vectorscan/`
   - Architecture-specific include paths may be used

2. **Include path not found:**
   - Manual detection may find library but not headers
   - Headers may be in non-standard location

**Current Detection Logic:**
```cmake
find_path(VECTORSCAN_INCLUDE_DIR
    NAMES hs/hs.h
    PATHS
        /usr/include
        /usr/local/include
        ...
)
```

**Solutions:**
1. **Add architecture-specific paths:**
   ```cmake
   if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
       list(APPEND INCLUDE_PATHS /usr/include/x86_64-linux-gnu)
   endif()
   ```

2. **Use pkg-config (recommended):**
   - pkg-config handles include paths automatically

3. **Set VECTORSCAN_ROOT:**
   ```bash
   export VECTORSCAN_ROOT=/usr
   ```

**Prevention:** Test on multiple Linux distributions.

---

### 9. ⚠️ System Package vs Manual Installation (Linux)

**Location:** `CMakeLists.txt` - Detection logic

**Potential Issues:**

1. **Multiple installations:**
   - System package installed in `/usr/lib`
   - Manual installation in `/usr/local/lib`
   - CMake may find wrong installation

2. **Version conflicts:**
   - System package may be older version
   - Manual installation may be newer version
   - Build may link against wrong version

**Solutions:**
1. **Use pkg-config (recommended):**
   - pkg-config typically finds system packages first

2. **Set VECTORSCAN_ROOT:**
   - Explicitly point to desired installation
   ```bash
   export VECTORSCAN_ROOT=/usr/local
   ```

3. **Use AUTO mode:**
   - Build will use whatever is found first
   - Falls back to std::regex if not found

**Prevention:** Document installation method preference.

---

### 10. ⚠️ Architecture-Specific Issues (Linux)

**Location:** `CMakeLists.txt` - Manual detection paths

**Potential Issues:**

1. **ARM64 support:**
   - VectorScan may not be available for ARM64
   - Build will fallback to std::regex (acceptable)

2. **Architecture-specific library paths:**
   - x86_64: `/usr/lib/x86_64-linux-gnu/`
   - ARM64: `/usr/lib/aarch64-linux-gnu/`
   - Current detection may miss ARM64 paths

**Current Detection Logic:**
```cmake
PATHS
    /usr/lib/x86_64-linux-gnu  # Only x86_64
    /usr/lib64
    ...
```

**Solutions:**
1. **Add ARM64 paths:**
   ```cmake
   if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
       list(APPEND SEARCH_PATHS /usr/lib/aarch64-linux-gnu)
   endif()
   ```

2. **Use pkg-config (recommended):**
   - pkg-config handles architecture-specific paths automatically

3. **Use AUTO mode:**
   - Build will succeed without VectorScan (falls back to std::regex)

**Prevention:** Test on ARM64 systems if targeting ARM.

---

## Common Issues (Both Platforms)

### 11. ⚠️ Conditional Compilation Issues

**Location:** `src/utils/VectorScanUtils.h` and `src/utils/VectorScanUtils.cpp`

**Potential Issues:**

1. **USE_VECTORSCAN not defined:**
   - If VectorScan is found but `USE_VECTORSCAN` is not defined, code will not compile
   - Stub implementations will be used (acceptable)

2. **Include path not set:**
   - `#include <hs/hs.h>` may fail if include path is not set
   - Even if `USE_VECTORSCAN` is defined

**Symptoms:**
```
fatal error: 'hs/hs.h' file not found
```

**Solutions:**
1. **Verify CMake configuration:**
   - Check that `target_include_directories` is called
   - Check that `USE_VECTORSCAN` is defined

2. **Use vcpkg or pkg-config (recommended):**
   - These handle include paths automatically

**Prevention:** Test with both detection methods.

---

### 12. ⚠️ Runtime Library Availability

**Location:** `src/utils/VectorScanUtils.cpp` - `IsRuntimeAvailable()`

**Potential Issue:**
- VectorScan may be found at compile time but not available at runtime
- Library may be missing from system PATH (Windows) or LD_LIBRARY_PATH (Linux)

**Symptoms:**
- Build succeeds
- Application crashes at runtime with "library not found" error

**Current Implementation:**
```cpp
bool IsRuntimeAvailable() {
    return IsAvailable();  // Only checks compile-time availability
}
```

**Solutions:**
1. **Windows:**
   - Ensure VectorScan DLL is in PATH or same directory as executable
   - Or link statically

2. **Linux:**
   - Ensure library is in LD_LIBRARY_PATH
   - Or install system package

3. **Graceful fallback:**
   - Current implementation falls back to std::regex if VectorScan fails
   - Runtime errors are caught and handled

**Prevention:** Document runtime library requirements.

---

### 13. ⚠️ Error Messages Not Clear

**Location:** `CMakeLists.txt` - Error messages

**Potential Issue:**
- Error messages may not clearly indicate how to fix issues
- Users may not know about AUTO mode or fallback behavior

**Current Error Messages:**
```cmake
if(USE_VECTORSCAN STREQUAL "ON")
    message(FATAL_ERROR "USE_VECTORSCAN=ON but VectorScan was not found.")
    message(FATAL_ERROR "  Installation options:")
    ...
endif()
```

**Solutions:**
1. **Improve error messages:**
   - Add more specific guidance
   - Suggest AUTO mode as alternative

2. **Document fallback behavior:**
   - Explain that AUTO mode is recommended
   - Explain that std::regex fallback is acceptable

**Prevention:** Test error messages with users.

---

## Build Issue Prevention Checklist

### Before Building on Windows:

- [ ] **vcpkg configured:**
  - [ ] vcpkg installed
  - [ ] `CMAKE_TOOLCHAIN_FILE` set (if using vcpkg)
  - [ ] VectorScan installed via vcpkg: `vcpkg install vectorscan`

- [ ] **Or manual installation:**
  - [ ] VectorScan built from source
  - [ ] `VECTORSCAN_ROOT` environment variable set
  - [ ] Library and headers in expected locations

- [ ] **Build configuration:**
  - [ ] Use `-DUSE_VECTORSCAN=AUTO` (recommended) or `-DUSE_VECTORSCAN=ON`
  - [ ] Verify architecture matches (x86 vs x64)
  - [ ] Verify compiler matches (MSVC vs MinGW)

### Before Building on Linux:

- [ ] **System packages:**
  - [ ] pkg-config installed: `sudo apt-get install pkg-config`
  - [ ] VectorScan installed via package manager (if available)

- [ ] **Or manual installation:**
  - [ ] VectorScan built from source
  - [ ] `VECTORSCAN_ROOT` environment variable set (if needed)
  - [ ] Library and headers in standard locations (`/usr/lib`, `/usr/include`)

- [ ] **Build configuration:**
  - [ ] Use `-DUSE_VECTORSCAN=AUTO` (recommended)
  - [ ] Verify architecture matches (x86_64 vs ARM64)
  - [ ] Verify library paths match architecture

---

## Recommended Solutions

### For Windows:

1. **Use vcpkg (recommended):**
   ```bash
   vcpkg install vectorscan
   cmake -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DUSE_VECTORSCAN=AUTO ..
   ```

2. **Or use AUTO mode without VectorScan:**
   ```bash
   cmake -DUSE_VECTORSCAN=AUTO ..
   ```
   Build will succeed, falls back to std::regex.

### For Linux:

1. **Use pkg-config (recommended):**
   ```bash
   sudo apt-get install pkg-config
   # Install VectorScan via package manager or build from source
   cmake -DUSE_VECTORSCAN=AUTO ..
   ```

2. **Or use AUTO mode without VectorScan:**
   ```bash
   cmake -DUSE_VECTORSCAN=AUTO ..
   ```
   Build will succeed, falls back to std::regex.

---

## Testing Recommendations

### Windows Testing:

1. **Test with vcpkg:**
   - Install VectorScan via vcpkg
   - Configure with vcpkg toolchain
   - Verify build succeeds

2. **Test without VectorScan:**
   - Build with `USE_VECTORSCAN=AUTO`
   - Verify build succeeds (falls back to std::regex)

3. **Test manual installation:**
   - Build VectorScan from source
   - Set `VECTORSCAN_ROOT`
   - Verify build succeeds

### Linux Testing:

1. **Test with pkg-config:**
   - Install VectorScan via package manager
   - Verify pkg-config finds it
   - Verify build succeeds

2. **Test without VectorScan:**
   - Build with `USE_VECTORSCAN=AUTO`
   - Verify build succeeds (falls back to std::regex)

3. **Test manual installation:**
   - Build VectorScan from source
   - Install to `/usr/local`
   - Verify build succeeds

4. **Test on multiple distributions:**
   - Ubuntu/Debian
   - Fedora
   - Arch Linux

5. **Test on multiple architectures:**
   - x86_64
   - ARM64 (if available)

---

## Summary

**Overall Risk Assessment:**

- **Windows:** ⚠️ **MEDIUM** - vcpkg integration may require setup, but AUTO mode provides graceful fallback
- **Linux:** ⚠️ **LOW** - pkg-config handles most cases, AUTO mode provides graceful fallback

**Key Recommendations:**

1. **Always use AUTO mode** (default) - provides graceful fallback
2. **Use vcpkg on Windows** - handles dependencies automatically
3. **Use pkg-config on Linux** - handles paths automatically
4. **Document installation methods** - help users avoid issues
5. **Test fallback behavior** - ensure std::regex works when VectorScan unavailable

**Blocking Issues:** None - all issues have workarounds or graceful fallbacks.

---

**Review Complete:** VectorScan integration is designed with robust fallback mechanisms. Most build issues are non-blocking and can be resolved by using AUTO mode or proper installation methods.
