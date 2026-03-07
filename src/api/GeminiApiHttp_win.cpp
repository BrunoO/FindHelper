#include "api/GeminiApiHttp.h"

#include <sstream>
#include <string>
#include <vector>

#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <winhttp.h>

#include "utils/Logger.h"
#pragma comment(lib, "winhttp.lib")

namespace gemini_api_utils {

namespace {
  // Maximum response size limit
  constexpr size_t kMaxResponseSize = size_t{1024} * 1024;  // 1MB

  // HTTP status codes
  constexpr int kHttpStatusOk = 200;

  // Helper: Escape JSON string (basic escaping for prompt text)
  std::string EscapeJsonString(std::string_view str) {
    std::ostringstream oss;
    for (const char c : str) {
      switch (c) {
      case '"':
        oss << "\\\"";  // NOSONAR(cpp:S3628) - Raw string literal would be less readable for JSON escaping
        break;
      case '\\':
        oss << "\\\\";  // NOSONAR(cpp:S3628) - Raw string literal would be less readable for JSON escaping
        break;
      case '\n':
        oss << "\\n";
        break;
      case '\r':
        oss << "\\r";
        break;
      case '\t':
        oss << "\\t";
        break;
      default:
        oss << c;
        break;
      }
    }
    return oss.str();
  }

  // Helper: Cleanup WinHTTP handles
  void CleanupWinHttpHandles(HINTERNET h_request, HINTERNET h_connect, HINTERNET h_session) {
    if (h_request) {
      WinHttpCloseHandle(h_request);
    }
    if (h_connect) {
      WinHttpCloseHandle(h_connect);
    }
    if (h_session) {
      WinHttpCloseHandle(h_session);
    }
  }

  // Helper: Convert API key to wide string and build headers
  std::wstring BuildApiHeaders(std::string_view api_key) {
    int api_key_len = MultiByteToWideChar(CP_UTF8, 0, api_key.data(), 
                                          static_cast<int>(api_key.size()), nullptr, 0);
    std::vector<wchar_t> api_key_wide(api_key_len + 1);
    MultiByteToWideChar(CP_UTF8, 0, api_key.data(), 
                        static_cast<int>(api_key.size()), 
                        api_key_wide.data(), api_key_len);
    api_key_wide[api_key_len] = L'\0';

    std::wstring headers = L"Content-Type: application/json\r\n";
    headers += L"x-goog-api-key: ";
    headers += api_key_wide.data();
    headers += L"\r\n";
    return headers;
  }

  // Helper: Read error response body
  std::string ReadErrorResponseBody(HINTERNET h_request) {
    std::string error_body;
    DWORD bytes_available = 0;
    DWORD bytes_read = 0;
    std::vector<char> buffer(4096);
    
    while (WinHttpQueryDataAvailable(h_request, &bytes_available) && bytes_available > 0) {
      if (bytes_available > buffer.size()) {
        bytes_available = static_cast<DWORD>(buffer.size());  // NOSONAR(cpp:S1905) - Cast needed for WinHttp API (DWORD is 32-bit, size_t may be 64-bit)
      }
      if (WinHttpReadData(h_request, buffer.data(), bytes_available, &bytes_read) && bytes_read > 0) {
        error_body.append(buffer.data(), bytes_read);
      }
    }
    return error_body;
  }

  // Helper: Read response body (avoids nested breaks)
  std::pair<bool, std::string> ReadResponseBody(HINTERNET h_request) {
    std::string response_body;
    DWORD bytes_available = 0;
    DWORD bytes_read = 0;
    std::vector<char> buffer(4096);

    while (WinHttpQueryDataAvailable(h_request, &bytes_available) && bytes_available > 0) {
      // Check response size limit
      if (response_body.size() + bytes_available > kMaxResponseSize) {
        LOG_ERROR_BUILD("Gemini API HTTP call failed: response too large (" << (response_body.size() + bytes_available) << " bytes, max " << kMaxResponseSize << ")");
        return {false, "Response too large (max 1MB)"};
      }

      if (!WinHttpReadData(h_request, buffer.data(), bytes_available, &bytes_read) || bytes_read == 0) {
        break;
      }

      response_body.append(buffer.data(), bytes_read);
    }

    return {true, response_body};
  }
} // namespace

std::pair<bool, std::string> CallGeminiApiHttpPlatform(
    std::string_view prompt,
    std::string_view api_key,
    int timeout_seconds) {
  
  // Build JSON request body
  const std::string escaped_prompt = EscapeJsonString(prompt);
  std::ostringstream json_body;
  json_body << R"({"contents":[{"parts":[{"text":")"
            << escaped_prompt
            << R"("}]}]})";
  std::string body_str = json_body.str();
  
  // Initialize WinHTTP
  HINTERNET h_session = WinHttpOpen(
      L"FindHelper/1.0",
      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
      WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS,
      0);

  if (!h_session) {
    LOG_ERROR_BUILD("Gemini API HTTP call failed: failed to initialize WinHTTP session");
    return {false, "Failed to initialize WinHTTP session"};
  }

  // Set timeout (convert seconds to milliseconds)
  constexpr int milliseconds_per_second = 1000;
  auto timeout_ms = static_cast<DWORD>(timeout_seconds * milliseconds_per_second);
  WinHttpSetTimeouts(h_session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

  // Connect to host
  HINTERNET h_connect = WinHttpConnect(
      h_session,
      L"generativelanguage.googleapis.com",
      INTERNET_DEFAULT_HTTPS_PORT,
      0);

  if (!h_connect) {
    CleanupWinHttpHandles(nullptr, nullptr, h_session);
    LOG_ERROR_BUILD("Gemini API HTTP call failed: failed to connect to API host");
    return {false, "Failed to connect to API host"};
  }

  // Open request
  HINTERNET h_request = WinHttpOpenRequest(
      h_connect,
      L"POST",
      L"/v1beta/models/gemini-1.5-flash:generateContent",
      nullptr,
      WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES,
      WINHTTP_FLAG_SECURE);

  if (!h_request) {
    CleanupWinHttpHandles(nullptr, h_connect, h_session);
    LOG_ERROR_BUILD("Gemini API HTTP call failed: failed to open HTTP request");
    return {false, "Failed to open HTTP request"};
  }

  // Build headers
  const std::wstring headers = BuildApiHeaders(api_key);

  // Send request
  // Note: body_str.data() returns char* for non-const string (C++17+), avoiding const_cast
  // WinHttpSendRequest takes non-const pointer but only reads the data, so this is safe
  if (BOOL send_result = WinHttpSendRequest(
      h_request,
      headers.c_str(),
      static_cast<DWORD>(-1), // Auto-calculate headers length
      body_str.data(), // Use data() instead of c_str() to get non-const pointer
      static_cast<DWORD>(body_str.size()),  // NOSONAR(cpp:S1905) - Cast needed for WinHttp API (DWORD is 32-bit, size_t may be 64-bit)
      static_cast<DWORD>(body_str.size()),  // NOSONAR(cpp:S1905) - Cast needed for WinHttp API (DWORD is 32-bit, size_t may be 64-bit)
      0); !send_result) { // Context parameter (DWORD_PTR)
    CleanupWinHttpHandles(h_request, h_connect, h_session);
    LOG_ERROR_BUILD("Gemini API HTTP call failed: failed to send HTTP request");
    return {false, "Failed to send HTTP request"};
  }

  // Receive response
  if (!WinHttpReceiveResponse(h_request, nullptr)) {
    CleanupWinHttpHandles(h_request, h_connect, h_session);
    LOG_ERROR_BUILD("Gemini API HTTP call failed: failed to receive HTTP response");
    return {false, "Failed to receive HTTP response"};
  }

  // Check HTTP status
  DWORD status_code = 0;
  if (DWORD status_code_size = sizeof(status_code); !WinHttpQueryHeaders(h_request,
                           WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           nullptr,
                           &status_code,
                           &status_code_size,
                           nullptr)) {
    CleanupWinHttpHandles(h_request, h_connect, h_session);
    return {false, "Failed to query HTTP status"};
  }

  if (status_code != kHttpStatusOk) {
    // Read error response body
    std::string error_body = ReadErrorResponseBody(h_request);
    CleanupWinHttpHandles(h_request, h_connect, h_session);
    
    std::string error_msg = "HTTP error " + std::to_string(status_code);
    if (!error_body.empty()) {
      error_msg += ": " + error_body;
    }
    return {false, error_msg};
  }

  // Read response body
  auto [success, response_body] = ReadResponseBody(h_request);
  CleanupWinHttpHandles(h_request, h_connect, h_session);

  if (!success) {
    return {false, response_body};  // response_body contains error message
  }

  // Return raw JSON response
  return {true, response_body};
}

} // namespace gemini_api_utils

