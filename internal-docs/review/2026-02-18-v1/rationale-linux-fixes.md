# Linux Build Rationale - 2026-02-18

## Issues Found

### 1. Missing GUI Dependencies in Build Environment
- **Issue**: Attempting to build the full application (`-DBUILD_APPLICATION=ON`) failed because the build environment lacks Wayland and X11 development headers (e.g., `wayland-scanner`, `libxrandr`).
- **Root Cause**: The current Linux environment is a restricted/headless container that does not have all the desktop-related dependencies listed in `BUILDING_ON_LINUX.md` installed.
- **Solution**: Followed the established workaround for restricted environments by building only the unit tests and benchmarks using `-DBUILD_APPLICATION=OFF -DGLFW_BUILD_WAYLAND=OFF`.
- **Verification**: CMake configuration succeeded with these flags.

## Build and Test Results

- **Configuration**: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_APPLICATION=OFF -DGLFW_BUILD_WAYLAND=OFF`
- **Build**: `cmake --build build --config Release` succeeded for all test targets.
- **Tests**: `ctest` passed 100% (25/25 tests).

## Conclusion
The Linux build process is robust when following the documentation. The dependencies listed in `BUILDING_ON_LINUX.md` are accurately identified as required for the full GUI application. In restricted environments, the build system correctly supports a tests-only mode which remains fully functional and verified.
