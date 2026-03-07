/**
 * @file ThreadUtils.cpp
 * @brief Windows implementation of thread utility functions
 */

#include "utils/ThreadUtils.h"

#include <vector>

#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem

#include "utils/Logger.h"

void SetThreadName(const char* thread_name) {  // NOLINT(readability-identifier-naming,readability-function-cognitive-complexity) - snake_case param; complexity from platform branches
  if (thread_name == nullptr || *thread_name == '\0') {
    LOG_WARNING_BUILD("SetThreadName: called with null or empty thread_name");
    return;
  }

  // SetThreadDescription is available on Windows 10 version 1607 and later
  // It's the recommended way to name threads and works with modern debuggers
  using SetThreadDescriptionProc = HRESULT(WINAPI*)(HANDLE, PCWSTR);  // NOLINT(readability-identifier-naming) - Windows API type name
  static SetThreadDescriptionProc setThreadDescription = nullptr;  // NOLINT(readability-identifier-naming)
  static bool initialized = false;
  
  if (!initialized) {  // NOSONAR(cpp:S6004) - Static variable used after if block (line 25)
    if (HMODULE kernel32 = GetModuleHandleA("kernel32.dll"); kernel32 != nullptr) {  // NOLINT(readability-implicit-bool-conversion) - explicit for pointer
      // Note: reinterpret_cast is necessary here because GetProcAddress() returns
      // FARPROC (void*), which must be cast to the specific function pointer type.
      // This is a Windows API limitation. The cast is safe because:
      // 1. We know the function signature matches SetThreadDescription
      // 2. GetProcAddress() returns a valid function pointer if found
      // 3. We check for nullptr before using the pointer
      setThreadDescription = reinterpret_cast<SetThreadDescriptionProc>(  // NOSONAR(cpp:S3630) - Windows API GetProcAddress requires reinterpret_cast to convert FARPROC to function pointer type
          GetProcAddress(kernel32, "SetThreadDescription"));
      if (setThreadDescription == nullptr) {  // NOLINT(bugprone-branch-clone) - then/else intentionally different log levels (WARNING vs DEBUG)
        LOG_WARNING_BUILD("SetThreadName: SetThreadDescription not available on this system; "
                          "thread_name=\"" << (thread_name ? thread_name : "<null>") << "\"");
      } else {
        LOG_DEBUG_BUILD("SetThreadName: SetThreadDescription resolved successfully");
      }
    } else {
      LOG_WARNING_BUILD("SetThreadName: GetModuleHandleA(\"kernel32.dll\") failed; "
                        "thread_name=\"" << (thread_name ? thread_name : "<null>") << "\"");
    }
    initialized = true;
  }
  
  if (setThreadDescription != nullptr) {  // NOLINT(readability-implicit-bool-conversion) - explicit pointer check
    // Convert UTF-8 to wide string
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, thread_name, -1, nullptr, 0);  // NOLINT(readability-identifier-naming)
    if (wideLen <= 0) {
      DWORD lastError = GetLastError();  // NOLINT(readability-identifier-naming)
      LOG_ERROR_BUILD("SetThreadName: MultiByteToWideChar size query failed; "
                      "lastError=" << lastError << " thread_name=\"" << thread_name << "\"");
      return;
    }

    std::vector<wchar_t> wideName(static_cast<size_t>(wideLen));  // NOLINT(readability-identifier-naming) - wide string buffer
    if (int convertedLen = MultiByteToWideChar(CP_UTF8, 0, thread_name, -1, wideName.data(), wideLen);  // NOLINT(readability-identifier-naming)
        convertedLen <= 0) {
      DWORD lastError = GetLastError();  // NOLINT(readability-identifier-naming)
      LOG_ERROR_BUILD("SetThreadName: MultiByteToWideChar conversion failed; "
                      "lastError=" << lastError << " thread_name=\"" << thread_name << "\"");
      return;
    }

    HRESULT hr = setThreadDescription(GetCurrentThread(), wideName.data());  // NOLINT(readability-identifier-naming) - Windows HRESULT convention
    if (FAILED(hr)) {
      LOG_ERROR_BUILD("SetThreadName: SetThreadDescription failed; "
                      "hr=0x" << std::hex << hr << std::dec
                              << " thread_name=\"" << thread_name << "\"");
    } else {
      LOG_DEBUG_BUILD("SetThreadName: successfully set thread name to \"" << thread_name << "\"");
    }
  } else {
    LOG_DEBUG_BUILD("SetThreadName: SetThreadDescription pointer is null; "
                    "thread_name=\"" << thread_name << "\"");
  }
}

