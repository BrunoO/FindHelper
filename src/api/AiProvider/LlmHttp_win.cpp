#include "api/AiProvider/LlmHttp.h"

#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>

#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include
#include <winhttp.h>

#include "api/GeminiApiHttp.h"
#include "utils/Logger.h"
#pragma comment(lib, "winhttp.lib")

namespace ai_provider::llm_http {

namespace {

struct ParsedUrl {
  std::wstring host;
  INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
  std::wstring path;
  bool secure = true;
};

bool IsLoopbackHost(std::string_view host) {
  return host == "localhost" || host == "127.0.0.1" || host == "::1" || host == "[::1]";
}

bool ParseUrl(std::string_view url, ParsedUrl& out) {
  std::string url_str(url);
  bool secure = true;
  const std::string https_prefix = "https://";
  const std::string http_prefix = "http://";  // NOSONAR(cpp:S5332) - http allowed only for loopback hosts (Ollama local dev); rejected below
  if (url_str.compare(0, https_prefix.size(), https_prefix) == 0) {
    url_str = url_str.substr(https_prefix.size());
    secure = true;
  } else if (url_str.compare(0, http_prefix.size(), http_prefix) == 0) {
    url_str = url_str.substr(http_prefix.size());
    secure = false;
  } else {
    return false;
  }
  std::string host_port;
  std::string path_str = "/";
  if (const size_t slash = url_str.find('/'); slash != std::string::npos) {
    host_port = url_str.substr(0, slash);
    path_str = url_str.substr(slash);
  } else {
    host_port = url_str;
  }
  std::string host_str = host_port;
  INTERNET_PORT port = secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
  if (const size_t colon = host_port.rfind(':'); colon != std::string::npos) {
    host_str = host_port.substr(0, colon);
    try {
      port = static_cast<INTERNET_PORT>(std::stoi(host_port.substr(colon + 1)));
    } catch (const std::invalid_argument&) {
      return false;
    } catch (const std::out_of_range&) {
      return false;
    }
  }
  if (host_str.empty() || path_str.empty()) {
    return false;
  }
  // Security: plain http would send Bearer keys in cleartext. Allow it only
  // for loopback hosts (local Ollama dev); reject remote http URLs.
  if (!secure && !IsLoopbackHost(host_str)) {
    return false;
  }
  const int host_len = MultiByteToWideChar(CP_UTF8, 0, host_str.c_str(), -1, nullptr, 0);
  const int path_len = MultiByteToWideChar(CP_UTF8, 0, path_str.c_str(), -1, nullptr, 0);
  if (host_len <= 1 || path_len <= 1) {
    return false;
  }
  std::vector<wchar_t> host_wide(static_cast<size_t>(host_len));
  std::vector<wchar_t> path_wide(static_cast<size_t>(path_len));
  MultiByteToWideChar(CP_UTF8, 0, host_str.c_str(), -1, host_wide.data(), host_len);
  MultiByteToWideChar(CP_UTF8, 0, path_str.c_str(), -1, path_wide.data(), path_len);
  out.host = host_wide.data();
  out.path = path_wide.data();
  out.port = port;
  out.secure = secure;
  return true;
}

std::wstring BuildHeaderBlock(const std::vector<std::string>& headers) {
  std::wstring block;
  for (const auto& header : headers) {
    const int len = MultiByteToWideChar(CP_UTF8, 0, header.c_str(), -1, nullptr, 0);
    if (len <= 1) {
      continue;
    }
    std::vector<wchar_t> wide(static_cast<size_t>(len));
    MultiByteToWideChar(CP_UTF8, 0, header.c_str(), -1, wide.data(), len);
    block += wide.data();
    block += L"\r\n";
  }
  return block;
}

void CleanupHandles(HINTERNET req, HINTERNET conn, HINTERNET sess) {
  if (req != nullptr) {
    WinHttpCloseHandle(req);
  }
  if (conn != nullptr) {
    WinHttpCloseHandle(conn);
  }
  if (sess != nullptr) {
    WinHttpCloseHandle(sess);
  }
}

}  // namespace

std::pair<bool, std::string> CallLlmHttpPost(
    std::string_view url,
    const std::vector<std::string>& headers,
    std::string_view body,
    int timeout_seconds) {
  ParsedUrl parsed;
  if (!ParseUrl(url, parsed)) {
    return {false, "Invalid API URL"};
  }
  std::string body_str(body);

  HINTERNET h_session = nullptr;
  HINTERNET h_connect = nullptr;
  HINTERNET h_request = nullptr;

  h_session = WinHttpOpen(L"FindHelper/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (h_session == nullptr) {
    LOG_ERROR_BUILD("LLM HTTP call failed: failed to initialize WinHTTP session");
    return {false, "Failed to initialize WinHTTP session"};
  }

  constexpr int milliseconds_per_second = 1000;
  const auto timeout_ms = static_cast<DWORD>(timeout_seconds * milliseconds_per_second);
  WinHttpSetTimeouts(h_session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

  h_connect = WinHttpConnect(h_session, parsed.host.c_str(), parsed.port, 0);
  if (h_connect == nullptr) {
    CleanupHandles(nullptr, nullptr, h_session);
    LOG_ERROR_BUILD("LLM HTTP call failed: failed to connect to API host");
    return {false, "Failed to connect to API host"};
  }

  const DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
  h_request = WinHttpOpenRequest(h_connect, L"POST", parsed.path.c_str(), nullptr,
                                 WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (h_request == nullptr) {
    CleanupHandles(nullptr, h_connect, h_session);
    LOG_ERROR_BUILD("LLM HTTP call failed: failed to open HTTP request");
    return {false, "Failed to open HTTP request"};
  }

  if (const std::wstring header_block = BuildHeaderBlock(headers);
      !WinHttpSendRequest(h_request, header_block.c_str(),
                          static_cast<DWORD>(-1), body_str.data(),
                          static_cast<DWORD>(body_str.size()),  // NOSONAR(cpp:S1905) - WinHttp DWORD API
                          static_cast<DWORD>(body_str.size()),  // NOSONAR(cpp:S1905) - WinHttp DWORD API
                          0)) {
    CleanupHandles(h_request, h_connect, h_session);
    LOG_ERROR_BUILD("LLM HTTP call failed: failed to send HTTP request");
    return {false, "Failed to send HTTP request"};
  }

  if (!WinHttpReceiveResponse(h_request, nullptr)) {
    CleanupHandles(h_request, h_connect, h_session);
    LOG_ERROR_BUILD("LLM HTTP call failed: failed to receive HTTP response");
    return {false, "Failed to receive HTTP response"};
  }

  DWORD status_code = 0;
  if (DWORD status_size = sizeof(status_code);
      !WinHttpQueryHeaders(h_request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           nullptr, &status_code, &status_size, nullptr)) {
    CleanupHandles(h_request, h_connect, h_session);
    return {false, "Failed to query HTTP status"};
  }

  std::string response_body;
  DWORD bytes_available = 0;
  DWORD bytes_read = 0;
  std::vector<char> buffer(4096);
  while (WinHttpQueryDataAvailable(h_request, &bytes_available) && bytes_available > 0) {
    if (response_body.size() + bytes_available > gemini_api_utils::kMaxResponseSize) {
      CleanupHandles(h_request, h_connect, h_session);
      LOG_ERROR_BUILD("LLM HTTP call failed: response too large");
      return {false, "Response too large (max 1MB)"};
    }
    if (bytes_available > buffer.size()) {
      bytes_available = static_cast<DWORD>(buffer.size());  // NOSONAR(cpp:S1905) - WinHttp DWORD API
    }
    if (!WinHttpReadData(h_request, buffer.data(), bytes_available, &bytes_read) || bytes_read == 0) {
      break;
    }
    response_body.append(buffer.data(), bytes_read);
  }

  CleanupHandles(h_request, h_connect, h_session);

  if (status_code != gemini_api_utils::kHttpStatusOk) {
    std::string error_msg = "HTTP error " + std::to_string(status_code);
    if (!response_body.empty()) {
      error_msg += ": " + response_body;
    }
    return {false, error_msg};
  }

  return {true, response_body};
}

}  // namespace ai_provider::llm_http
