#include "api/GeminiApiHttp.h"
#include "utils/Logger.h"

#include <curl/curl.h>
#include <string>
#include <vector>

namespace gemini_api_utils {

namespace {
  constexpr size_t kCurlErrorBufferSize = CURL_ERROR_SIZE;

  // Write callback for libcurl
  // Note: void* parameter is required by libcurl C API callback signature (CURLOPT_WRITEFUNCTION).
  // This is a C API limitation and cannot be avoided. The callback is safe because:
  // 1. libcurl guarantees the pointer points to valid data of the specified size
  // 2. The callback signature is fixed by the C API and cannot be changed
  // 3. We immediately cast to char* and use it safely within the callback
  size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* data) {  // NOSONAR(cpp:S5008) - void* required by libcurl C API callback signature (CURLOPT_WRITEFUNCTION), cast to const char* for safe usage // NOLINT(readability-non-const-parameter) - Signature fixed by curl_write_callback C API, data must be non-const (modified by append)
    const size_t total_size = size * nmemb;
    data->append(static_cast<const char*>(contents), total_size);  // NOSONAR(cpp:S5008) - void* from C API, cast to const char* for safe usage
    return total_size;
  }
} // namespace

std::pair<bool, std::string> CallGeminiApiHttpPlatform(
    std::string_view prompt,
    std::string_view api_key,
    int timeout_seconds) {

  // Build JSON request body
  const std::string body_str = BuildGeminiRequestBody(prompt);

  // Initialize libcurl
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    LOG_ERROR_BUILD("Gemini API HTTP call failed: failed to initialize libcurl");
    return {false, "Failed to initialize libcurl"};
  }

  std::string response_data;
  std::vector<char> error_buffer(kCurlErrorBufferSize, '\0');

  // Set URL
  curl_easy_setopt(curl, CURLOPT_URL,
                   "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent");
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body_str.size());
  // Set headers
  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  const std::string api_key_header = "x-goog-api-key: " + std::string(api_key);
  headers = curl_slist_append(headers, api_key_header.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer.data());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<int64_t>(timeout_seconds));
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<int64_t>(timeout_seconds));
  // Enable SSL/TLS with minimum TLS 1.2 (allows TLS 1.2 and 1.3)
  curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);  // NOSONAR(cpp:S4423) - CURL_SSLVERSION_TLSv1_2 enforces minimum TLS 1.2
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "FindHelper/1.0");
  // Perform request
  const CURLcode res = curl_easy_perform(curl);
  int64_t http_code = 0;
  if (res == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  }

  // Cleanup headers before checking errors
  curl_slist_free_all(headers);

  // Handle errors
  if (res != CURLE_OK) {
    std::string error_msg = "Network error: ";
    if (error_buffer.front() != '\0') {
      error_msg += error_buffer.data();
    } else {
      error_msg += curl_easy_strerror(res);
    }
    curl_easy_cleanup(curl);
    LOG_ERROR_BUILD("Gemini API HTTP call failed: " << error_msg);
    return {false, error_msg};
  }

  // Check HTTP status code
  if (http_code != kHttpStatusOk) {
    std::string error_msg = "HTTP error " + std::to_string(http_code);
    if (!response_data.empty()) {
      error_msg += ": " + response_data;
    }
    curl_easy_cleanup(curl);
    LOG_ERROR_BUILD("Gemini API HTTP call failed: " << error_msg);
    return {false, error_msg};
  }

  // Check response size limit
  if (response_data.size() > kMaxResponseSize) {
    curl_easy_cleanup(curl);
    LOG_ERROR_BUILD("Gemini API HTTP call failed: response too large (" << response_data.size() << " bytes, max " << kMaxResponseSize << ")");
    return {false, "Response too large (max 1MB)"};
  }

  // Cleanup
  curl_easy_cleanup(curl);

  // Return raw JSON response
  return {true, response_data};
}

} // namespace gemini_api_utils

