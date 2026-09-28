#include "api/GeminiApiHttp.h"
#include "utils/Logger.h"

#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>

#include <sstream>
#include <string>

namespace gemini_api_utils {

std::pair<bool, std::string> CallGeminiApiHttpPlatform(
    std::string_view prompt,
    std::string_view api_key,
    int timeout_seconds) {
  
  @autoreleasepool {
    // Build JSON request body
    std::string body_str = BuildGeminiRequestBody(prompt);
    
    // Convert to NSData
    NSData *body_data = [NSData dataWithBytes:body_str.c_str() length:body_str.size()];
    
    // Build URL
    NSString *url_string = @"https://generativelanguage.googleapis.com/v1beta/models/gemini-flash-latest:generateContent";
    NSURL *url = [NSURL URLWithString:url_string];
    
    if (!url) {
      LOG_ERROR_BUILD("Gemini API HTTP call failed: invalid API URL");
      return {false, "Invalid API URL"};
    }
    
    // Create request
    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
    [request setHTTPMethod:@"POST"];
    [request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
    
    // Set API key header
    std::string api_key_str(api_key);
    NSString *api_key_ns = [NSString stringWithUTF8String:api_key_str.c_str()];
    [request setValue:api_key_ns forHTTPHeaderField:@"x-goog-api-key"];
    
    [request setHTTPBody:body_data];
    [request setTimeoutInterval:timeout_seconds];
    
    // Use semaphore to make synchronous request
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    __block NSData *response_data = nil;
    __block NSHTTPURLResponse *http_response = nil;
    __block NSError *request_error = nil;
    
    NSURLSessionDataTask *task = [[NSURLSession sharedSession] dataTaskWithRequest:request
                                                                  completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
      response_data = data;
      http_response = (NSHTTPURLResponse *)response;
      request_error = error;
      dispatch_semaphore_signal(semaphore);
    }];
    
    [task resume];
    
    // Wait for completion with timeout
    dispatch_time_t timeout_time = dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout_seconds * NSEC_PER_SEC));
    long wait_result = dispatch_semaphore_wait(semaphore, timeout_time);
    
    if (wait_result != 0) {
      LOG_ERROR_BUILD("Gemini API HTTP call failed: request timeout");
      return {false, "Request timeout"};
    }

    // Check for network error
    if (request_error) {
      NSString *error_desc = [request_error localizedDescription];
      std::string error_msg = "Network error: ";
      if (error_desc) {
        error_msg += [error_desc UTF8String];
      } else {
        error_msg += "Unknown error";
      }
      LOG_ERROR_BUILD("Gemini API HTTP call failed: " << error_msg);
      return {false, error_msg};
    }

    // Check HTTP status
    if (!http_response || http_response.statusCode != kHttpStatusOk) {
      std::string error_msg = "HTTP error: ";
      if (http_response) {
        error_msg += std::to_string(http_response.statusCode);
      } else {
        error_msg += "No response";
      }
      if (response_data) {
        NSString *error_body = [[NSString alloc] initWithData:response_data encoding:NSUTF8StringEncoding];
        if (error_body) {
          error_msg += " - ";
          error_msg += [error_body UTF8String];
        }
      }
      LOG_ERROR_BUILD("Gemini API HTTP call failed: " << error_msg);
      return {false, error_msg};
    }
    
    // Return raw JSON response
    if (!response_data) {
      LOG_ERROR_BUILD("Gemini API HTTP call failed: empty response from API");
      return {false, "Empty response from API"};
    }
    
    // Check response size limit
    if ([response_data length] > kMaxResponseSize) {
      LOG_ERROR_BUILD("Gemini API HTTP call failed: response too large (" << [response_data length] << " bytes, max " << kMaxResponseSize << ")");
      return {false, "Response too large (max 1MB)"};
    }
    
    NSString *response_string = [[NSString alloc] initWithData:response_data encoding:NSUTF8StringEncoding];
    if (!response_string) {
      LOG_ERROR_BUILD("Gemini API HTTP call failed: failed to decode response as UTF-8");
      return {false, "Failed to decode response as UTF-8"};
    }
    
    std::string response_body = [response_string UTF8String];
    return {true, response_body};
  }
}

} // namespace gemini_api_utils

