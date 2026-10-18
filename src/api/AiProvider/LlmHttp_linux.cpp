#include "api/AiProvider/LlmHttp.h"

#include <cstdint>
#include <string>
#include <vector>

#include <curl/curl.h>

#include "api/GeminiApiHttp.h"
#include "utils/Logger.h"

namespace ai_provider::llm_http {

namespace {
constexpr size_t kCurlErrorBufferSize = CURL_ERROR_SIZE;

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* data) {  // NOSONAR(cpp:S5008) - void* required by libcurl C API
  const size_t total_size = size * nmemb;
  data->append(static_cast<const char*>(contents), total_size);  // NOSONAR(cpp:S5008) - C API cast
  return total_size;
}
}  // namespace

std::pair<bool, std::string> CallLlmHttpPost(
    std::string_view url,
    const std::vector<std::string>& headers,
    std::string_view body,
    int timeout_seconds) {
  const std::string url_str(url);
  const std::string body_str(body);

  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    LOG_ERROR_BUILD("LLM HTTP call failed: failed to initialize libcurl");
    return {false, "Failed to initialize libcurl"};
  }

  std::string response_data;
  std::vector error_buffer(kCurlErrorBufferSize, '\0');

  curl_easy_setopt(curl, CURLOPT_URL, url_str.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body_str.size());

  struct curl_slist* header_list = nullptr;
  for (const auto& header : headers) {
    header_list = curl_slist_append(header_list, header.c_str());
  }
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer.data());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<int64_t>(timeout_seconds));
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<int64_t>(timeout_seconds));
  curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);  // NOSONAR(cpp:S4423) - minimum TLS 1.2
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "FindHelper/1.0");

  const CURLcode res = curl_easy_perform(curl);
  int64_t http_code = 0;
  if (res == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  }

  curl_slist_free_all(header_list);

  if (res != CURLE_OK) {
    std::string error_msg = "Network error: ";
    if (error_buffer.front() != '\0') {
      error_msg += error_buffer.data();
    } else {
      error_msg += curl_easy_strerror(res);
    }
    curl_easy_cleanup(curl);
    LOG_ERROR_BUILD("LLM HTTP call failed: " << error_msg);
    return {false, error_msg};
  }

  if (http_code != gemini_api_utils::kHttpStatusOk) {
    std::string error_msg = "HTTP error " + std::to_string(http_code);
    if (!response_data.empty()) {
      error_msg += ": " + response_data;
    }
    curl_easy_cleanup(curl);
    LOG_ERROR_BUILD("LLM HTTP call failed: " << error_msg);
    return {false, error_msg};
  }

  if (response_data.size() > gemini_api_utils::kMaxResponseSize) {
    curl_easy_cleanup(curl);
    LOG_ERROR_BUILD("LLM HTTP call failed: response too large (" << response_data.size() << " bytes)");
    return {false, "Response too large (max 1MB)"};
  }

  curl_easy_cleanup(curl);
  return {true, response_data};
}

}  // namespace ai_provider::llm_http
