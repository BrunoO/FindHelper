# Rationale for Linux Build Fixes - 2026-02-14

## Issue 1: Missing Gemini API Stub for Linux

### Description
In restricted Linux environments (like CI/CD or headless servers) where `libcurl` is not available, the build for several targets (`find_helper`, `search_benchmark`, `gemini_api_utils_tests`) failed because they explicitly required `src/api/GeminiApiHttp_linux.cpp`, which depends on `curl/curl.h`.

### Root Cause
The `CMakeLists.txt` always included `src/api/GeminiApiHttp_linux.cpp` for Linux builds, but only conditionally linked `libcurl`. When `libcurl` was missing, compilation failed during the header inclusion of `<curl/curl.h>`.

### Solution implemented
1. Created `src/api/GeminiApiHttp_stub.cpp`, a minimal implementation of `CallGeminiApiHttpPlatform` that returns a failure message.
2. Modified `CMakeLists.txt` to:
   - Perform early detection of `libcurl` on Linux.
   - Set a variable `GEMINI_API_HTTP_SRC` to either the full Linux implementation or the stub based on `libcurl` availability.
   - Use `${GEMINI_API_HTTP_SRC}` in all relevant targets.
   - Make `libcurl` optional for the main application on Linux (it will now build with the stub if missing).

### Verification steps taken
1. Run CMake configuration in an environment without `libcurl-dev`.
2. Verified that CMake reports: `Linux: libcurl NOT found - Gemini API will use stub (no network support)`.
3. Successfully compiled `search_benchmark` and other tests.
4. Verified that `search_benchmark` links and runs without undefined references.

## Issue 2: Improper CMake structuring for BUILD_APPLICATION=OFF

### Description
Platform-specific variables (like `GEMINI_API_HTTP_SRC`) were defined inside an `if(BUILD_APPLICATION)` block, making them unavailable when building only tests (`-DBUILD_APPLICATION=OFF`).

### Root Cause
Tight coupling of platform detection and variable setting with the main application target definition.

### Solution implemented
Moved common platform-specific source selection (like `GEMINI_API_HTTP_SRC`) to a more global scope in `CMakeLists.txt`, before the `if(BUILD_APPLICATION)` block.

### Verification steps taken
1. Configured with `-DBUILD_APPLICATION=OFF -DBUILD_TESTS=ON`.
2. Verified that all test targets correctly identified their platform-specific sources.
3. Successfully built all tests.
