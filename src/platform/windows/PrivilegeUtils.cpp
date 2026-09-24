#include "PrivilegeUtils.h"

#ifdef _WIN32

#include "utils/Logger.h"
#include "utils/LoggingUtils.h"
#include "utils/ScopedHandle.h"
#include <winnt.h>  // For TOKEN_PRIVILEGES, SE_* constants
#include <sstream>

namespace privilege_utils {

bool DropUnnecessaryPrivileges() {
  ScopedHandle hToken;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, hToken.GetAddressOf())) {
    DWORD err = GetLastError();
    LOG_WARNING_BUILD("Failed to open process token for privilege adjustment: " << err);
    return false;
  }

  // List of privileges to disable
  // These are privileges that are not needed for USN Journal operations
  struct PrivilegeToDisable {
    LPCWSTR name;  // Windows privilege names are wide strings
    const char* description;
  };

  const PrivilegeToDisable privileges_to_disable[] = {  // NOSONAR(cpp:S5945) - C-style array required for aggregate initialization with Windows API constants
    {SE_DEBUG_NAME, "Debug programs"},
    {SE_TAKE_OWNERSHIP_NAME, "Take ownership of files"},
    {SE_SECURITY_NAME, "Manage auditing and security log"},
    // SE_BACKUP_NAME is intentionally not dropped. It is required for the
    // USN Journal auto-refresh feature to function correctly, as it allows
    // bypassing ACLs to read file and directory metadata across the volume.
    {SE_RESTORE_NAME, "Restore files and directories"},
    // Note: We keep privileges that might be needed for volume access
    // SE_MANAGE_VOLUME_NAME is not typically enabled by default, so we don't disable it
  };

  bool all_succeeded = true;
  int disabled_count = 0;
  int not_enabled_count = 0;
  int failed_count = 0;

  for (const auto& priv : privileges_to_disable) {
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = 0; // Disable (SE_PRIVILEGE_ENABLED = 0 means disabled)

    if (!LookupPrivilegeValueW(nullptr, priv.name, &tp.Privileges[0].Luid)) {
      DWORD err = GetLastError();
      LOG_WARNING_BUILD("Failed to lookup privilege " << priv.description << ": " << err);
      all_succeeded = false;
      failed_count++;
      continue;
    }

    // Disable the privilege
    // Note: We don't check if it's enabled first - AdjustTokenPrivileges will
    // return ERROR_NOT_ALL_ASSIGNED if the privilege wasn't enabled, which we handle below
    if (!AdjustTokenPrivileges(hToken.Get(), FALSE, &tp, 0, nullptr, nullptr)) {
      DWORD err = GetLastError();
      LOG_WARNING_BUILD("Failed to disable privilege " << priv.description << ": " << err);
      all_succeeded = false;
      failed_count++;
      continue;
    }

    DWORD last_err = GetLastError();
    if (last_err == ERROR_NOT_ALL_ASSIGNED) {
      // Privilege wasn't enabled, that's fine - nothing to disable
      not_enabled_count++;
      continue;
    } else if (last_err != ERROR_SUCCESS) {
      LOG_WARNING_BUILD("Unexpected error when disabling privilege " << priv.description << ": " << last_err);
      all_succeeded = false;
      failed_count++;
      continue;
    }

    // Successfully disabled
    LOG_INFO_BUILD("Disabled privilege: " << priv.description);
    disabled_count++;
  }

  if (disabled_count > 0) {
    LOG_INFO_BUILD("Dropped " << disabled_count << " unnecessary privilege(s) - reduced attack surface");
  } else if (not_enabled_count > 0) {
    LOG_INFO_BUILD("No unnecessary privileges were enabled (already minimal privilege set)");
  }

  if (failed_count > 0) {
    LOG_WARNING_BUILD("Failed to drop " << failed_count << " privilege(s) - continuing with elevated privileges");
  }

  // Return true if we succeeded in disabling at least some privileges, or if none were enabled
  // Return false only if we tried to disable privileges but all attempts failed
  return (disabled_count > 0) || (failed_count == 0);
}

std::vector<std::string> GetCurrentPrivileges() {
  std::vector<std::string> privileges;

  ScopedHandle hToken;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, hToken.GetAddressOf())) {
    return privileges; // Return empty on error
  }

  // Get required buffer size
  DWORD cb_size = 0;
  if (!GetTokenInformation(hToken.Get(), TokenPrivileges, nullptr, 0, &cb_size)) {
    if (DWORD err = GetLastError(); err != ERROR_INSUFFICIENT_BUFFER) {
      // Only log if it's not the expected "insufficient buffer" error
      logging_utils::LogWindowsApiError("GetTokenInformation (size query)",
                                        "Getting required buffer size for token privileges",
                                        err);
    }
    return privileges;
  }

  if (cb_size == 0) {
    return privileges;
  }

  // Allocate buffer with proper alignment for TOKEN_PRIVILEGES structure
  // Note: reinterpret_cast is necessary here because GetTokenInformation() requires
  // a BYTE buffer that it will fill with TOKEN_PRIVILEGES data. This is a standard
  // Windows API pattern. The cast is safe because:
  // 1. The buffer is properly sized (cb_size bytes)
  // 2. std::vector<BYTE> provides proper alignment for any type
  // 3. GetTokenInformation() will write a valid TOKEN_PRIVILEGES structure
  std::vector<BYTE> buffer(cb_size);
  auto tp = reinterpret_cast<TOKEN_PRIVILEGES*>(buffer.data());  // NOSONAR(cpp:S3630) - Windows API GetTokenInformation requires reinterpret_cast to convert BYTE buffer to TOKEN_PRIVILEGES*

  if (!GetTokenInformation(hToken.Get(), TokenPrivileges, tp, cb_size, &cb_size)) {
    DWORD err = GetLastError();
    logging_utils::LogWindowsApiError("GetTokenInformation",
                                      "Getting token privileges",
                                      err);
    return privileges;
  }

  // Iterate through privileges
  for (DWORD i = 0; i < tp->PrivilegeCount; ++i) {
    if (tp->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED) {
      // Get privilege name
      char name[256];  // NOSONAR(cpp:S5945) - Windows API LookupPrivilegeNameA requires C-style array
      DWORD name_size = sizeof(name);
      if (LookupPrivilegeNameA(nullptr, &tp->Privileges[i].Luid, name, &name_size)) {
        privileges.emplace_back(name);
      }
    }
  }

  return privileges;
}

} // namespace privilege_utils

#endif  // _WIN32

