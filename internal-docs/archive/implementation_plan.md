# Implementation Plan - Replace Legacy ANSI APIs

## Goal
Replace usage of legacy ANSI Windows APIs (ending in 'A') with modern Unicode APIs (ending in 'W') to ensure correct handling of international characters and special symbols in paths and environment variables.

## User Review Required
> [!IMPORTANT]
> This change affects how the application retrieves environment variables like `USERPROFILE`. If you have a custom setup relying on ANSI behavior (unlikely), this might change behavior.

## Proposed Changes

### GUI (`main_gui.cpp`)
#### [MODIFY] [main_gui.cpp](file:///Users/brunoorsier/dev/USN_WINDOWS/main_gui.cpp)
- Replace `GetEnvironmentVariableA` with `GetEnvironmentVariableW`.
- Use `WideToUtf8` to convert the result back to `std::string` (or `char` buffer) for internal use.
- This ensures that if `USERPROFILE` contains Unicode characters (e.g., `C:\Users\José`), it is correctly retrieved.

### Monitor (`UsnMonitor.cpp`)
#### [MODIFY] [UsnMonitor.cpp](file:///Users/brunoorsier/dev/USN_WINDOWS/UsnMonitor.cpp)
- Replace `CreateFileA` with `CreateFileW`.
- Update the volume path string to a wide string literal `L"\\\\.\\C:"`.

## Verification Plan

### Automated Tests
- None (requires Windows environment).

### Manual Verification
1.  **Build the application**.
2.  **Test "Current User" Button**: Click the button and verify it populates the path correctly, even if your username has special characters.
3.  **Test "Desktop" / "Downloads" Buttons**: Verify they populate the correct paths.
4.  **Monitor Startup**: Verify the application still successfully opens the C: volume and starts monitoring (check logs for "USN Journal queried successfully").
