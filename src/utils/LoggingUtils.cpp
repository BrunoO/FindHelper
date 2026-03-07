#include "LoggingUtils.h"

#ifdef _WIN32

#include "utils/Logger.h"
#include <sstream>
#include <iomanip>
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <winerror.h>

namespace logging_utils {

std::string GetWindowsErrorString(DWORD error_code) {
  LPSTR message_buffer = nullptr;
  if (size_t size = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      (LPSTR)&message_buffer, 0, nullptr); size == 0) {
    return "Unknown error " + std::to_string(error_code);
  }
  
  std::string message(message_buffer);
  LocalFree(message_buffer);
  
  // Remove trailing newline/carriage return
  while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
    message.pop_back();
  }
  
  return message;
}

void LogWindowsApiError(std::string_view operation,
                        std::string_view context,
                        DWORD error_code) {
  std::string error_msg = GetWindowsErrorString(error_code);
  LOG_ERROR_BUILD(operation << " failed: " << error_msg 
                  << ". Context: " << context << ", Error code: " << error_code);
}

void LogException(std::string_view operation,
                 std::string_view context,
                 const std::exception& e) {
  LOG_ERROR_BUILD(operation << " failed: Exception: " << e.what() 
                  << ". Context: " << context);
}

void LogUnknownException(std::string_view operation,
                        std::string_view context) {
  LOG_ERROR_BUILD(operation << " failed: Unknown exception. Context: " << context);
}

void LogHResultError(std::string_view operation,
                    std::string_view context,
                    HRESULT hr) {
  // Convert HRESULT to DWORD error code (extract the error code portion)
  DWORD error_code = HRESULT_CODE(hr);
  std::string error_msg = GetWindowsErrorString(error_code);
  
  // Log with both readable error message and HRESULT in hex
  LOG_ERROR_BUILD(operation << " failed: " << error_msg 
                  << " (HRESULT: 0x" << std::hex << hr << std::dec << ")"
                  << ". Context: " << context);
}

} // namespace logging_utils

#endif  // _WIN32

