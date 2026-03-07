# Linux Build and Test Fixes - 2026-01-31

This document describes the fixes applied during Linux build verification to ensure compatibility and eliminate compiler warnings.

## Issue 1: invalid-offsetof Warning in Popups.cpp

### Description
The Linux build using GCC 13.3.0 reported a warning:
```
/app/src/ui/Popups.cpp:409:20: warning: ‘offsetof’ within non-standard-layout type ‘GuiState’ is conditionally-supported [-Winvalid-offsetof]
  409 |           offsetof(GuiState, pathInput));
```

### Root Cause Analysis
The code used `offsetof` to compute the pointer to a `GuiState` object from a pointer to one of its members (`pathInput.Data()`). While this works for standard-layout types, `GuiState` is a complex class with many fields and dependencies, making it a non-standard-layout type. The C++ standard treats `offsetof` on such types as conditionally-supported, and modern compilers like GCC issue warnings to prevent potential undefined behavior.

### Solution Implemented
Refactored the `RenderRegexGeneratorPopupContent` and `RenderActionButtons` functions to take an optional `GuiState*` parameter. Instead of attempting to "back-calculate" the `GuiState` address from a member buffer, the pointer is now passed directly from the caller who already has access to it.

**Changes:**
- **src/ui/Popups.h**: Added `GuiState* gui_state = nullptr` parameter to `RenderRegexGeneratorPopupContent`.
- **src/ui/Popups.cpp**:
    - Updated `RenderActionButtons` to accept `GuiState*`.
    - Removed `offsetof` and `reinterpret_cast` logic.
    - Updated `RenderRegexGeneratorPopup` and `RenderRegexGeneratorPopupFilename` to pass the `&state` pointer.

### Verification Steps Taken
1. Reconfigured the build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
2. Verified that the build completes without the `-Winvalid-offsetof` warning.
3. Ran the unit test suite: `cd build && ctest --output-on-failure`. All 23 tests passed.
