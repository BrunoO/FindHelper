# Linux Build Fix Rationale - 2026-02-01

## Issue: Missing `wayland-scanner` dependency

### Description
The Linux build failed during the CMake configuration step for the GLFW dependency because it could not find `wayland-scanner`.

### Root Cause Analysis
The project uses GLFW 3.4, which is built from source via `FetchContent` if the system version is insufficient. On Linux, GLFW requires `wayland-scanner` to generate Wayland protocol code. This tool is typically provided by the `libwayland-bin` package on Ubuntu/Debian, which was missing from the documented prerequisites.

### Solution Implemented
1.  Manually installed `libwayland-bin` (along with other missing dev dependencies like `libcurl4-openssl-dev`, although that was already documented).
2.  Updated `docs/guides/building/BUILDING_ON_LINUX.md` to include `libwayland-bin` in the `apt-get` installation command.

### Verification Steps Taken
1.  Cleaned the build directory: `rm -rf build`
2.  Re-ran CMake configuration: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
3.  Verified configuration succeeded.
4.  Executed build: `cmake --build build --config Release -j$(nproc)`
5.  Verified build completed without errors.
6.  Ran all tests: `cd build && ctest --output-on-failure`
7.  Verified 100% test pass rate (25/25 tests passed).
