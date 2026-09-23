#pragma once

#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Must be first: provides types for Shell headers
#include <shlobj.h>   // NOSONAR(cpp:S3806) - After windows.h; lowercase for portability

// Shows the Windows Explorer context menu for the specified file path
// at the given screen coordinates.
// Returns true if a command was executed, false otherwise.
bool ShowContextMenu(HWND hwnd, std::string_view path, POINT pt);

// Pins the specified file or folder to Windows Quick Access (File Explorer).
// Works for both files and folders on Windows 11. Uses the Unicode verb L"pintohome"
// (CMINVOKECOMMANDINFOEX) for robustness across locales.
// Returns true if the verb was invoked successfully, false otherwise.
bool PinToQuickAccess(HWND hwnd, std::string_view path);

// Checks if the current process is running with administrative privileges.
bool IsProcessElevated();

// Restarts the current application with administrative privileges,
// forwarding the given command-line arguments (excluding argv[0]) so the
// elevated process starts with the same options (and the same unknowns).
// Returns true if the restart was initiated successfully.
bool RestartAsAdmin(HWND hwnd, int argc, char* const* argv);

// Prompts the user to restart the application as administrator for USN-based
// real-time indexing, forwarding argc/argv (see RestartAsAdmin).
// Returns true if the restart was initiated successfully.
bool PromptForElevationAndRestart(HWND hwnd, int argc, char* const* argv);

// Helper to resolve a known folder (e.g. FOLDERID_Desktop, FOLDERID_RecycleBinFolder)
// to a UTF-8 path string. Returns true on success.
bool TryGetKnownFolderPath(REFKNOWNFOLDERID folder_id, std::string &out_path_utf8);

#endif  // _WIN32
