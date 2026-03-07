#include "platform/windows/ShellContextUtils.h"

#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

#ifdef _WIN32
#include <objbase.h>       // CoInitializeEx, CoUninitialize
#include <shellapi.h>
#include <shlobj.h>        // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <shlobj_core.h>   // SHParseDisplayName, PIDLIST_ABSOLUTE; NOSONAR(cpp:S3806) - lowercase per project standard
#include <shobjidl_core.h> // CMINVOKECOMMANDINFOEX, CMIC_MASK_UNICODE; NOSONAR(cpp:S3806) - lowercase per project standard
#include <shlwapi.h>       // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <winnt.h>         // For TOKEN_ELEVATION, OpenProcessToken, GetTokenInformation

#include "utils/Logger.h"
#include "utils/LoggingUtils.h"
#include "utils/ScopedHandle.h"
#include "utils/StringUtils.h"

// ARRAYSIZE macro if not defined (available in newer Windows SDK)
#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif  // ARRAYSIZE

// Link with shell32.lib and shlwapi.lib
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

// RAII for PIDLIST_ABSOLUTE: ensures CoTaskMemFree on all code paths.
struct PidlDeleter {
  void operator()(PIDLIST_ABSOLUTE p) const noexcept {
    if (p != nullptr) {
      CoTaskMemFree(p);
    }
  }
};
using PidlAbsolutePtr =
  std::unique_ptr<std::remove_pointer_t<PIDLIST_ABSOLUTE>, PidlDeleter>;

// RAII for CoTaskMem-allocated wide strings (e.g. SHGetKnownFolderPath).
struct CoTaskMemWStrDeleter {
  void operator()(WCHAR* p) const noexcept {
    if (p != nullptr) {
      CoTaskMemFree(p);
    }
  }
};

// Normalize path for SHParseDisplayName: replace forward slashes with backslashes (Windows expects backslashes).
static void NormalizePathToBackslashes(std::wstring& w_path) {
  for (wchar_t& c : w_path) {
    if (c == L'/') {
      c = L'\\';
    }
  }
}

// Try to invoke the "pintohome" verb by scanning the context menu entries and invoking
// the matching command by its ordinal (menu ID). This mirrors what happens when the user
// selects "Pin to Quick access" from the Explorer context menu and is more robust for
// shell extensions (e.g., OneDrive) that reject direct verb-by-name invocation.
//
// Returns true if "pintohome" was found and invoked successfully, false otherwise.
static bool TryInvokePintohomeViaMenu(HWND hwnd,
                                      IContextMenu* p_context_menu,
                                      std::string_view path_str) {
  if (p_context_menu == nullptr) {
    return false;
  }

  if (HMENU h_menu = CreatePopupMenu(); h_menu) {
    const HRESULT hr_menu = p_context_menu->QueryContextMenu(
        h_menu, 0, 1, 0x7FFF, CMF_NORMAL);
    if (FAILED(hr_menu)) {
      logging_utils::LogHResultError(
          "QueryContextMenu",
          "PinToQuickAccess: verb scan, Path: " + std::string(path_str),
          hr_menu);
      DestroyMenu(h_menu);
      return false;
    }

    const int menu_count = GetMenuItemCount(h_menu);
    for (int index = 0; index < menu_count; ++index) {
      const UINT item_id = GetMenuItemID(h_menu, index);
      if (item_id == static_cast<UINT>(-1) || item_id < 1U || item_id > 0x7FFFU) {
        continue;
      }

      const auto cmd = static_cast<UINT_PTR>(item_id - 1U);
      char verb_buffer[256] = {0};  // NOSONAR(cpp:S5945) - IContextMenu::GetCommandString expects a C-style char buffer
      if (FAILED(p_context_menu->GetCommandString(
              cmd, GCS_VERBA, nullptr, verb_buffer, ARRAYSIZE(verb_buffer)))) {
        continue;
      }

      if (_stricmp(verb_buffer, "pintohome") != 0) {
        continue;
      }

      CMINVOKECOMMANDINFOEX ici_ex = {};
      ici_ex.cbSize = sizeof(CMINVOKECOMMANDINFOEX);
      ici_ex.fMask = CMIC_MASK_UNICODE | CMIC_MASK_ASYNCOK;
      ici_ex.hwnd = hwnd;
      ici_ex.lpVerb = MAKEINTRESOURCEA(static_cast<UINT>(cmd));
      ici_ex.nShow = SW_SHOWNORMAL;

      const HRESULT hr_invoke = p_context_menu->InvokeCommand(
          reinterpret_cast<LPCMINVOKECOMMANDINFO>(&ici_ex));  // NOSONAR(cpp:S3630) - Win32 InvokeCommand expects CMINVOKECOMMANDINFO*; reinterpret_cast from CMINVOKECOMMANDINFOEX* follows MSDN pattern
      DestroyMenu(h_menu);

      if (FAILED(hr_invoke)) {
        logging_utils::LogHResultError(
            "InvokeCommand",
            "PinToQuickAccess: pintohome (via menu), Path: " + std::string(path_str),
            hr_invoke);
        return false;
      }

      LOG_INFO_BUILD("PinToQuickAccess: pinned to Quick Access via menu: " << path_str);
      return true;
    }

    DestroyMenu(h_menu);
  } else {
    DWORD err = GetLastError();
    logging_utils::LogWindowsApiError("CreatePopupMenu",
                                      "PinToQuickAccess: verb scan, Path: " + std::string(path_str),
                                      err);
  }

  return false;
}

// Internal helper: populate and invoke the context menu for a given item.
// Returns true on successful command execution, false otherwise.
static bool InvokeContextMenuCommand(
    HWND hwnd,
    IContextMenu* p_context_menu,
    HMENU h_menu,
    POINT pt,
    std::string_view path) {
  const HRESULT hr_menu = p_context_menu->QueryContextMenu(
      h_menu, 0, 1, 0x7FFF, CMF_NORMAL);
  if (FAILED(hr_menu)) {
    logging_utils::LogHResultError("QueryContextMenu",
                                   "Path: " + std::string(path),
                                   hr_menu);
    return false;
  }

  const int command = TrackPopupMenu(
      h_menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
      pt.x, pt.y, 0, hwnd, nullptr);
  if (command <= 0) {
    return false;
  }

  // Use CMINVOKECOMMANDINFOEX for consistency with PinToQuickAccess; verb is ordinal (menu index).
  CMINVOKECOMMANDINFOEX ici_ex = {};
  ici_ex.cbSize = sizeof(CMINVOKECOMMANDINFOEX);
  ici_ex.fMask = 0;
  ici_ex.hwnd = hwnd;
  ici_ex.lpVerb = MAKEINTRESOURCEA(command - 1);
  ici_ex.nShow = SW_SHOWNORMAL;

  const HRESULT hr_invoke = p_context_menu->InvokeCommand(
      reinterpret_cast<LPCMINVOKECOMMANDINFO>(&ici_ex));  // NOSONAR(cpp:S3630) - Win32 InvokeCommand expects CMINVOKECOMMANDINFO*; reinterpret_cast from CMINVOKECOMMANDINFOEX* follows MSDN pattern
  if (SUCCEEDED(hr_invoke)) {
    return true;
  }

  logging_utils::LogHResultError(
      "InvokeCommand",
      "Path: " + std::string(path) + ", Command: " + std::to_string(command - 1),
      hr_invoke);
  return false;
}

bool ShowContextMenu(HWND hwnd, std::string_view path, POINT pt) {
  const std::string path_str(path);
  std::wstring w_path = Utf8ToWide(path_str);
  NormalizePathToBackslashes(w_path);

  // COM is required for IShellFolder/IContextMenu; initialize explicitly and uninitialize on success.
  // Uninitialize when we initialized (S_OK) or incremented ref (S_FALSE); never when RPC_E_CHANGED_MODE.
  const HRESULT hr_com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool need_uninit =
      (SUCCEEDED(hr_com) && hr_com != RPC_E_CHANGED_MODE);
  if (FAILED(hr_com) && hr_com != RPC_E_CHANGED_MODE) {
    logging_utils::LogHResultError("CoInitializeEx",
                                    "ShowContextMenu: COM initialization",
                                    hr_com);
    return false;
  }

  HRESULT hr;
  IShellFolder* p_parent_folder = nullptr;
  PIDLIST_ABSOLUTE raw_pidl = nullptr;
  PCUITEMID_CHILD pidl_child = nullptr;
  IContextMenu* p_context_menu = nullptr;
  bool result = false;  // NOLINT(misc-const-correctness) - result is modified by InvokeContextMenuCommand

  hr = SHParseDisplayName(w_path.c_str(), nullptr, &raw_pidl, 0, nullptr);
  if (FAILED(hr)) {
    logging_utils::LogHResultError("SHParseDisplayName",
                                    "Path: " + path_str,
                                    hr);
    if (need_uninit) {
      CoUninitialize();
    }
    return false;
  }
  PidlAbsolutePtr pidl_abs(raw_pidl);

  hr = SHBindToParent(pidl_abs.get(), IID_IShellFolder,
                      reinterpret_cast<void**>(&p_parent_folder),  // NOSONAR(cpp:S3630) - SHBindToParent uses void** out param; reinterpret_cast required from typed interface pointer
                      &pidl_child);
  if (FAILED(hr)) {
    logging_utils::LogHResultError("SHBindToParent",
                                    "Path: " + path_str,
                                    hr);
    if (need_uninit) {
      CoUninitialize();
    }
    return false;
  }

  hr = p_parent_folder->GetUIObjectOf(hwnd, 1, &pidl_child,
                                      IID_IContextMenu, nullptr,
                                      reinterpret_cast<void**>(&p_context_menu));  // NOSONAR(cpp:S3630) - GetUIObjectOf uses void** out param; reinterpret_cast required from typed interface pointer
  if (FAILED(hr)) {
    logging_utils::LogHResultError("GetUIObjectOf",
                                    "Path: " + path_str + ", Interface: IContextMenu",
                                    hr);
    p_parent_folder->Release();
    if (need_uninit) {
      CoUninitialize();
    }
    return false;
  }

  if (HMENU h_menu = CreatePopupMenu(); h_menu) {
    result = InvokeContextMenuCommand(hwnd, p_context_menu, h_menu, pt, path_str);
    DestroyMenu(h_menu);
  } else {
    DWORD err = GetLastError();
    logging_utils::LogWindowsApiError("CreatePopupMenu",
                                      "Path: " + path_str,
                                      err);
  }

  p_context_menu->Release();
  p_parent_folder->Release();
  if (need_uninit) {
    CoUninitialize();
  }
  return result;
}

bool PinToQuickAccess(HWND hwnd, std::string_view path) {
  const std::string path_str(path);
  std::wstring w_path = Utf8ToWide(path_str);
  NormalizePathToBackslashes(w_path);

  // COM is required for IShellFolder/IContextMenu; initialize explicitly and uninitialize on success.
  // Uninitialize when we initialized (S_OK) or incremented ref (S_FALSE); never when RPC_E_CHANGED_MODE.
  const HRESULT hr_com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool need_uninit =
      (SUCCEEDED(hr_com) && hr_com != RPC_E_CHANGED_MODE);
  if (FAILED(hr_com) && hr_com != RPC_E_CHANGED_MODE) {
    logging_utils::LogHResultError("CoInitializeEx",
                                    "PinToQuickAccess: COM initialization",
                                    hr_com);
    return false;
  }

  HRESULT hr;
  IShellFolder* p_parent_folder = nullptr;
  PIDLIST_ABSOLUTE raw_pidl = nullptr;
  PCUITEMID_CHILD pidl_child = nullptr;
  IContextMenu* p_context_menu = nullptr;

  hr = SHParseDisplayName(w_path.c_str(), nullptr, &raw_pidl, 0, nullptr);
  if (FAILED(hr)) {
    logging_utils::LogHResultError("SHParseDisplayName",
                                    "PinToQuickAccess: Path: " + path_str,
                                    hr);
    if (need_uninit) {
      CoUninitialize();
    }
    return false;
  }
  PidlAbsolutePtr pidl_abs(raw_pidl);

  hr = SHBindToParent(pidl_abs.get(), IID_IShellFolder,
                      reinterpret_cast<void**>(&p_parent_folder),  // NOSONAR(cpp:S3630) - SHBindToParent uses void** out param; reinterpret_cast required from typed interface pointer
                      &pidl_child);
  if (FAILED(hr)) {
    logging_utils::LogHResultError("SHBindToParent",
                                    "PinToQuickAccess: Path: " + path_str,
                                    hr);
    if (need_uninit) {
      CoUninitialize();
    }
    return false;
  }

  hr = p_parent_folder->GetUIObjectOf(hwnd, 1, &pidl_child,
                                      IID_IContextMenu, nullptr,
                                      reinterpret_cast<void**>(&p_context_menu));  // NOSONAR(cpp:S3630) - GetUIObjectOf uses void** out param; reinterpret_cast required from typed interface pointer
  if (FAILED(hr)) {
    logging_utils::LogHResultError("GetUIObjectOf",
                                    "PinToQuickAccess: Path: " + path_str,
                                    hr);
    p_parent_folder->Release();
    if (need_uninit) {
      CoUninitialize();
    }
    return false;
  }

  if (const bool invoked =
          TryInvokePintohomeViaMenu(hwnd, p_context_menu, path_str);
      !invoked) {
    // Fallback: attempt direct verb-by-name invocation for environments where the
    // menu-based path does not expose a canonical "pintohome" verb string.
    CMINVOKECOMMANDINFOEX ici_ex = {};
    ici_ex.cbSize = sizeof(CMINVOKECOMMANDINFOEX);
    ici_ex.fMask = CMIC_MASK_UNICODE | CMIC_MASK_ASYNCOK;
    ici_ex.hwnd = hwnd;
    ici_ex.lpVerbW = L"pintohome";
    ici_ex.nShow = SW_SHOWNORMAL;

    const HRESULT hr_invoke = p_context_menu->InvokeCommand(
        reinterpret_cast<LPCMINVOKECOMMANDINFO>(&ici_ex));  // NOSONAR(cpp:S3630) - Win32 InvokeCommand expects CMINVOKECOMMANDINFO*; reinterpret_cast from CMINVOKECOMMANDINFOEX* follows MSDN pattern

    if (FAILED(hr_invoke)) {
      logging_utils::LogHResultError("InvokeCommand",
                                      "PinToQuickAccess: pintohome, Path: " + path_str,
                                      hr_invoke);
      p_context_menu->Release();
      p_parent_folder->Release();
      if (need_uninit) {
        CoUninitialize();
      }
      return false;
    }

    LOG_INFO_BUILD("PinToQuickAccess: pinned to Quick Access via verb: " << path_str);
  }

  p_context_menu->Release();
  p_parent_folder->Release();
  if (need_uninit) {
    CoUninitialize();
  }

  return true;
}

bool IsProcessElevated() {
  bool is_elevated = false;
  ScopedHandle h_token;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, h_token.GetAddressOf()) != 0 && h_token.Get()) {  // NOSONAR(cpp:S6004) - Variable used after if block (line 135)
    TOKEN_ELEVATION elevation;
    DWORD cb_size = sizeof(TOKEN_ELEVATION);
    if (GetTokenInformation(h_token.Get(), TokenElevation, &elevation,
                            sizeof(elevation), &cb_size)) {
      is_elevated = elevation.TokenIsElevated;
    } else {
      // Silent failure is acceptable here - function returns false if unable to determine
      // This is a query function, not a critical operation
      DWORD err = GetLastError();
      LOG_WARNING_BUILD("IsProcessElevated: GetTokenInformation failed, error: " << err);
    }
  } else {
    // Silent failure is acceptable here - function returns false if unable to determine
    // This is a query function, not a critical operation
    DWORD err = GetLastError();
    LOG_WARNING_BUILD("IsProcessElevated: OpenProcessToken failed, error: " << err);
  }
  return is_elevated;
}

bool RestartAsAdmin(HWND hwnd) {
  WCHAR sz_path[MAX_PATH];  // NOSONAR(cpp:S5945) - Windows API GetModuleFileNameW requires C-style array
  if (GetModuleFileNameW(nullptr, sz_path, ARRAYSIZE(sz_path))) {
    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    sei.lpVerb = L"runas";
    sei.lpFile = sz_path;
    sei.hwnd = hwnd;
    sei.nShow = SW_NORMAL;

    if (!ShellExecuteExW(&sei)) {
      // Note: ERROR_CANCELLED means the user refused the elevation prompt
      // Other errors indicate system-level failures
      if (DWORD dw_error = GetLastError(); dw_error != ERROR_CANCELLED) {
        logging_utils::LogWindowsApiError("ShellExecuteExW",
                                          "Restarting as administrator",
                                          dw_error);
      }
      return false;
    }
    return true;
  } else {
    // GetModuleFileNameW failed
    DWORD err = GetLastError();
    logging_utils::LogWindowsApiError("GetModuleFileNameW",
                                      "Getting executable path for admin restart",
                                      err);
    return false;
  }
}

bool PromptForElevationAndRestart(HWND hwnd) {
  // Ask the user if they want to restart with administrator privileges for
  // USN Journal-based real-time indexing. If they decline or the message box
  // fails, continue running without elevation (folder-based indexing only).
  if (const int result = MessageBoxW(
          hwnd,
          L"Real-time indexing using the Windows USN Journal requires administrator privileges.\n\n"
          L"Restart The FindHelper Experiment as administrator now?\n\n"
          L"If you choose No, the application will continue with folder-based indexing only.",
          L"Administrator Privileges Required",
          MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2);
      result != IDYES) {
    return false;
  }

  return RestartAsAdmin(hwnd);
}

bool TryGetKnownFolderPath(REFKNOWNFOLDERID folder_id, std::string &out_path_utf8) {
  PWSTR raw_path = nullptr;
  if (HRESULT hr = SHGetKnownFolderPath(folder_id, 0, nullptr, &raw_path);
      FAILED(hr) || raw_path == nullptr) {
    return false;
  }
  std::unique_ptr<WCHAR, CoTaskMemWStrDeleter> folder_path_w(raw_path);

  char buffer[32767] = {0};  // NOSONAR(cpp:S5945) - Windows API WideCharToMultiByte requires C-style array
  if (const int converted =
          WideCharToMultiByte(CP_UTF8, 0, folder_path_w.get(), -1, buffer,
                              static_cast<int>(sizeof(buffer)), nullptr, nullptr);
      converted <= 0) {
    return false;
  }

  out_path_utf8.assign(buffer);
  return true;
}
#endif  // _WIN32