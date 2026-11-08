#include "api/AiProvider/LlmHttp.h"

#include <string>
#include <vector>

#include "api/GeminiApiHttp.h"
#include "utils/Logger.h"

#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>

namespace ai_provider {
namespace llm_http {

std::pair<bool, std::string> CallLlmHttpPost(
    std::string_view url,
    const std::vector<std::string>& headers,
    std::string_view body,
    int timeout_seconds) {
  @autoreleasepool {
    const std::string url_str(url);
    const std::string body_str(body);

    NSData* body_data = [NSData dataWithBytes:body_str.c_str() length:body_str.size()];
    NSString* url_ns = [NSString stringWithUTF8String:url_str.c_str()];
    NSURL* ns_url = [NSURL URLWithString:url_ns];
    if (!ns_url) {
      LOG_ERROR_BUILD("LLM HTTP call failed: invalid API URL");
      return {false, "Invalid API URL"};
    }

    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:ns_url];
    [request setHTTPMethod:@"POST"];
    for (const auto& header : headers) {
      const std::string::size_type colon = header.find(':');
      if (colon == std::string::npos) {
        continue;
      }
      std::string name = header.substr(0, colon);
      std::string value = header.substr(colon + 1);
      while (!value.empty() && value.front() == ' ') {
        value.erase(0, 1);
      }
      NSString* name_ns = [NSString stringWithUTF8String:name.c_str()];
      NSString* value_ns = [NSString stringWithUTF8String:value.c_str()];
      if (name_ns != nil && value_ns != nil) {
        [request setValue:value_ns forHTTPHeaderField:name_ns];
      }
    }
    [request setHTTPBody:body_data];
    [request setTimeoutInterval:timeout_seconds];

    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    __block NSData* response_data = nil;
    __block NSHTTPURLResponse* http_response = nil;
    __block NSError* request_error = nil;

    NSURLSessionDataTask* task = [[NSURLSession sharedSession] dataTaskWithRequest:request
                                                                 completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
      response_data = data;
      http_response = (NSHTTPURLResponse*)response;
      request_error = error;
      dispatch_semaphore_signal(semaphore);
    }];
    [task resume];

    dispatch_time_t timeout_time = dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout_seconds * NSEC_PER_SEC));
    long wait_result = dispatch_semaphore_wait(semaphore, timeout_time);
    if (wait_result != 0) {
      LOG_ERROR_BUILD("LLM HTTP call failed: request timeout");
      return {false, "Request timeout"};
    }
    if (request_error) {
      NSString* error_desc = [request_error localizedDescription];
      std::string error_msg = "Network error: ";
      if (error_desc) {
        error_msg += [error_desc UTF8String];
      } else {
        error_msg += "Unknown error";
      }
      LOG_ERROR_BUILD("LLM HTTP call failed: " << error_msg);
      return {false, error_msg};
    }
    if (!http_response || http_response.statusCode != gemini_api_utils::kHttpStatusOk) {
      std::string error_msg = "HTTP error ";
      if (http_response) {
        error_msg += std::to_string(http_response.statusCode);
      } else {
        error_msg += "No response";
      }
      if (response_data) {
        NSString* error_body = [[NSString alloc] initWithData:response_data encoding:NSUTF8StringEncoding];
        if (error_body) {
          error_msg += ": ";
          error_msg += [error_body UTF8String];
        }
      }
      LOG_ERROR_BUILD("LLM HTTP call failed: " << error_msg);
      return {false, error_msg};
    }
    if (!response_data) {
      return {false, "Empty response from API"};
    }
    if ([response_data length] > gemini_api_utils::kMaxResponseSize) {
      LOG_ERROR_BUILD("LLM HTTP call failed: response too large");
      return {false, "Response too large (max 1MB)"};
    }
    NSString* response_string = [[NSString alloc] initWithData:response_data encoding:NSUTF8StringEncoding];
    if (!response_string) {
      return {false, "Failed to decode response as UTF-8"};
    }
    std::string response_body = [response_string UTF8String];
    return {true, response_body};
  }
}

}  // namespace llm_http
}  // namespace ai_provider
