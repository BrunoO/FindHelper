# Gemini API Remaining Work Plan

## Overview

This document outlines the remaining work to complete the Gemini API integration. The core API utilities are implemented, but several important features are missing, including Linux support, UI integration for the new search config feature, and platform-specific code refactoring.

## Current Status Summary

### ✅ Completed
- Core API utilities (`GeminiApiUtils.h/cpp`)
  - Path pattern generation (legacy API)
  - Search config generation (new JSON-based API)
  - Cross-platform HTTP client (Windows ✅, macOS ✅, Linux ❌)
  - JSON parsing for both approaches
  - Async wrappers for both APIs
  - Environment variable support
  - Input validation and error handling
  - Unit tests
- `GuiState::ApplySearchConfig()` method
- Basic UI integration (legacy path pattern only)

### ❌ Missing / Incomplete
1. **Linux HTTP client implementation** (critical)
2. **UI integration for search config feature** (high priority)
3. **Platform-specific code refactoring** (medium priority)
4. **Clipboard workflow** (low priority)
5. **Settings UI** (low priority)
6. **Performance features** (caching, rate limiting) (low priority)

---

## Phase 1: Linux Support & Code Refactoring (Priority: HIGH)

### 1.1 Extract Platform-Specific HTTP Code

**Problem:** 
- HTTP client code is duplicated between `CallGeminiApi()` and `CallGeminiApiRaw()`
- Platform-specific code is mixed in `GeminiApiUtils.cpp` (Windows, macOS, Linux)
- Linux implementation is missing for both functions
- Code duplication makes maintenance difficult

**Solution:** Extract platform-specific HTTP implementations into separate files following the existing pattern (`FileOperations_linux.cpp`, `FileOperations_mac.mm`, etc.)

**Files to Create:**
- `GeminiApiHttp_win.cpp` - Windows WinHTTP implementation
- `GeminiApiHttp_mac.mm` - macOS NSURLSession implementation  
- `GeminiApiHttp_linux.cpp` - Linux libcurl implementation (NEW)

**Files to Modify:**
- `GeminiApiUtils.cpp` - Remove platform-specific code, call platform functions
- `GeminiApiUtils.h` - Add internal platform function declarations (if needed)

**Implementation Steps:**

1. **Create `GeminiApiHttp_win.cpp`**
   - Extract Windows WinHTTP code from `CallGeminiApi()` and `CallGeminiApiRaw()`
   - Create internal function: `std::pair<bool, std::string> CallGeminiApiHttp_Platform(...)`
   - Handle both success (return JSON) and error (return error message) cases
   - Signature: `(prompt, api_key, timeout_seconds) -> (success, response_or_error)`

2. **Create `GeminiApiHttp_mac.mm`**
   - Extract macOS NSURLSession code from `CallGeminiApi()` and `CallGeminiApiRaw()`
   - Same signature as Windows version
   - Use `@autoreleasepool` for memory management

3. **Create `GeminiApiHttp_linux.cpp`** (NEW)
   - Implement using libcurl (already a dependency)
   - Use synchronous libcurl API with timeout
   - Handle HTTPS, headers, JSON body, response reading
   - Same signature as other platforms
   - Error handling for network errors, timeouts, HTTP errors

4. **Refactor `GeminiApiUtils.cpp`**
   - Remove all `#ifdef _WIN32`, `#elif defined(__APPLE__)`, `#else` blocks
   - Replace with calls to platform-specific functions
   - Both `CallGeminiApi()` and `CallGeminiApiRaw()` call the same platform function
   - `CallGeminiApi()` parses response, `CallGeminiApiRaw()` returns raw JSON

**Linux libcurl Implementation Details:**

```cpp
// GeminiApiHttp_linux.cpp structure
#include <curl/curl.h>
#include <sstream>
#include <string>

namespace {
  // Write callback for libcurl
  size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* data) {
    size_t total_size = size * nmemb;
    data->append((char*)contents, total_size);
    return total_size;
  }
}

std::pair<bool, std::string> CallGeminiApiHttp_Platform(
    std::string_view prompt,
    std::string_view api_key,
    int timeout_seconds) {
  
  // Build JSON request body
  std::string escaped_prompt = EscapeJsonString(prompt);
  std::ostringstream json_body;
  json_body << R"({"contents":[{"parts":[{"text":")"
            << escaped_prompt
            << R"("}]}]})";
  std::string body_str = json_body.str();
  
  // Initialize libcurl
  CURL* curl = curl_easy_init();
  if (!curl) {
    return {false, "Failed to initialize libcurl"};
  }
  
  std::string response_data;
  std::string error_buffer;
  
  // Set URL
  curl_easy_setopt(curl, CURLOPT_URL, 
                   "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent");
  
  // Set POST data
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body_str.size());
  
  // Set headers
  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  std::string api_key_header = "x-goog-api-key: " + std::string(api_key);
  headers = curl_slist_append(headers, api_key_header.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  
  // Set write callback
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
  
  // Set error buffer
  error_buffer.resize(CURL_ERROR_SIZE);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer.data());
  
  // Set timeout
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout_seconds);
  
  // Enable SSL/TLS
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  
  // Perform request
  CURLcode res = curl_easy_perform(curl);
  
  long http_code = 0;
  if (res == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  }
  
  // Cleanup
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  
  // Handle errors
  if (res != CURLE_OK) {
    std::string error_msg = "Network error: ";
    if (!error_buffer.empty() && error_buffer[0] != '\0') {
      error_msg += error_buffer;
    } else {
      error_msg += curl_easy_strerror(res);
    }
    return {false, error_msg};
  }
  
  if (http_code != 200) {
    return {false, "HTTP error " + std::to_string(http_code) + ": " + response_data};
  }
  
  // Check response size limit
  if (response_data.size() > kMaxResponseSize) {
    return {false, "Response too large (max 1MB)"};
  }
  
  return {true, response_data};
}
```

**CMakeLists.txt Updates:**
- Add `GeminiApiHttp_linux.cpp` to Linux build
- Ensure libcurl is linked (already done for tests, need to add for main executable)
- Add `#include <curl/curl.h>` handling

**Testing:**
- Unit tests should work on Linux after implementation
- Test with `GEMINI_API_SKIP` set (should skip API calls)
- Test with valid API key (should make real API calls)
- Test timeout scenarios
- Test error scenarios (invalid API key, network failure)

---

## Phase 2: UI Integration for Search Config Feature (Priority: HIGH)

### 2.1 Update UI to Use Search Config API

**Current State:**
- UI only uses `GeneratePathPatternAsync()` (legacy, only populates path field)
- `GenerateSearchConfigAsync()` exists but is not called
- `ApplySearchConfig()` exists but is not used

**Goal:** 
- Add option to use full search config API
- Allow users to choose between "Generate Pattern" (legacy) and "Generate Search Config" (new)

**Files to Modify:**
- `ui/SearchInputs.cpp` - Add new button/option for search config
- `GuiState.h` - Add state for search config mode (optional)

**Implementation Options:**

**Option A: Two Separate Buttons (Recommended)**
- Keep existing "Generate Pattern" button (legacy behavior)
- Add new "Generate Search Config" button next to it
- User chooses which one to use
- Both use the same input field

**Option B: Toggle/Mode Switch**
- Add a toggle/radio button: "Pattern Only" vs "Full Config"
- Single "Generate" button that calls appropriate API based on mode
- More compact UI

**Option C: Smart Detection**
- Single button that tries search config first, falls back to pattern if needed
- More complex, less user control

**Recommendation: Option A** - Clear separation, backward compatible, user has control

**Implementation Steps:**

1. **Add new button in `ui/SearchInputs.cpp`**
   ```cpp
   // After existing "Generate Pattern" button
   ImGui::SameLine();
   if (ImGui::Button("Generate Search Config")) {
     if (!is_api_call_in_progress &&
         std::strlen(state.gemini_description_input_) > 0) {
       StartGeminiSearchConfigCall(); // New lambda
     }
   }
   ```

2. **Add new lambda function `StartGeminiSearchConfigCall()`**
   ```cpp
   auto StartGeminiSearchConfigCall = [&state]() {
     // Similar cleanup logic as StartGeminiApiCall
     state.gemini_api_call_in_progress_ = true;
     state.gemini_error_message_ = "";
     state.gemini_api_future_ = gemini_api_utils::GenerateSearchConfigAsync(
         state.gemini_description_input_, "", 30);
   };
   ```

3. **Update result handling to detect search config vs path pattern**
   ```cpp
   if (result.success) {
     if (!result.search_config.filename.empty() || 
         !result.search_config.extensions.empty() ||
         !result.search_config.path.empty() ||
         result.search_config.folders_only ||
         result.search_config.case_sensitive ||
         result.search_config.time_filter != "None" ||
         result.search_config.size_filter != "None") {
       // Full search config - apply it
       state.ApplySearchConfig(result.search_config);
     } else if (!result.path_pattern.empty()) {
       // Legacy path pattern only
       state.pathInput.SetValue(result.path_pattern);
     }
     state.gemini_description_input_[0] = '\0';
   }
   ```

4. **Update button labels/tooltips for clarity**
   - "Generate Pattern" → "Generate Pattern (Path only)"
   - "Generate Search Config" → "Generate Full Config (All filters)"

**Testing:**
- Test with descriptions that should generate full configs
- Test with simple descriptions (should still work)
- Verify all search parameters are applied correctly
- Test error handling

---

## Phase 3: Clipboard Workflow (Priority: LOW)

### 3.1 Implement Clipboard-Based Workflow

**Goal:** When `GEMINI_API_KEY` is not set, allow users to use external AI assistants via clipboard.

**Files to Create:**
- `ClipboardUtils.h` / `ClipboardUtils.cpp` - GLFW-based clipboard functions (if not exists)

**Files to Modify:**
- `ui/SearchInputs.cpp` - Add clipboard workflow UI
- `Application.cpp` - Pass GLFW window handle to UI (if needed)

**Implementation:**
- Check if `GEMINI_API_KEY` is set
- If not set: Show "Generate Prompt" button (copies prompt to clipboard)
- Show "Paste from Clipboard" button (reads JSON, parses, applies config)
- Show status messages with instructions

**Details:** See `docs/GEMINI_CLIPBOARD_WORKFLOW_PLAN.md` for full implementation plan.

---

## Phase 4: Settings UI (Priority: LOW)

### 4.1 API Key Configuration UI

**Goal:** Allow users to configure API key in settings instead of environment variable.

**Files to Modify:**
- `ui/SettingsWindow.cpp` - Add API key input field
- `Settings.h/cpp` - Add API key storage
- `GeminiApiUtils.cpp` - Check settings in addition to environment variable

**Implementation:**
- Add masked input field in settings
- Store in settings file (encrypted or plain text - security consideration)
- Add "Test API Key" button
- Show status (valid/invalid/not set)

---

## Phase 5: Performance Features (Priority: LOW)

### 5.1 Caching

**Goal:** Cache API responses to avoid redundant calls.

**Files to Create:**
- `GeminiApiCache.h` / `GeminiApiCache.cpp`

**Implementation:**
- In-memory cache: `std::unordered_map<std::string, SearchConfig>`
- Key: user description (normalized)
- LRU eviction (max 100 entries)
- Optional: Persist to disk

### 5.2 Rate Limiting

**Goal:** Prevent API abuse.

**Implementation:**
- Track API call timestamps
- Limit: 10 calls/minute (configurable)
- Show message if rate limit exceeded

---

## Implementation Order

### Sprint 1: Critical Fixes (Week 1)
1. ✅ Extract platform-specific HTTP code to separate files
2. ✅ Implement Linux HTTP client using libcurl
3. ✅ Refactor `CallGeminiApi()` and `CallGeminiApiRaw()` to use platform functions
4. ✅ Update CMakeLists.txt for Linux build
5. ✅ Test on all three platforms

### Sprint 2: UI Integration (Week 2)
6. ✅ Add "Generate Search Config" button to UI
7. ✅ Implement `StartGeminiSearchConfigCall()` lambda
8. ✅ Update result handling to use `ApplySearchConfig()`
9. ✅ Test full workflow

### Sprint 3: Polish (Week 3, Optional)
10. ⚠️ Clipboard workflow (if needed)
11. ⚠️ Settings UI (if needed)
12. ⚠️ Caching (if needed)
13. ⚠️ Rate limiting (if needed)

---

## Technical Considerations

### Code Organization
- Follow existing pattern: `*_win.cpp`, `*_mac.mm`, `*_linux.cpp`
- Keep platform-specific code isolated
- Shared code stays in main file

### Thread Safety
- All HTTP calls are synchronous (blocking)
- Async wrappers use `std::async` (already implemented)
- UI updates happen on main thread (already handled)

### Error Handling
- Platform functions return `(success, response_or_error)`
- Consistent error messages across platforms
- User-friendly error display in UI (already implemented)

### Testing
- Unit tests should work on all platforms after Linux implementation
- Integration tests for full workflow
- Test error scenarios on all platforms

---

## Files Summary

### New Files to Create:
- `GeminiApiHttp_win.cpp` - Windows HTTP implementation
- `GeminiApiHttp_mac.mm` - macOS HTTP implementation
- `GeminiApiHttp_linux.cpp` - Linux HTTP implementation (NEW)
- `ClipboardUtils.h/cpp` - Clipboard utilities (if not exists, for Phase 3)

### Files to Modify:
- `GeminiApiUtils.cpp` - Remove platform code, call platform functions
- `GeminiApiUtils.h` - Add internal function declarations (if needed)
- `ui/SearchInputs.cpp` - Add search config button and handling
- `CMakeLists.txt` - Add Linux HTTP file, ensure libcurl linking
- `Settings.h/cpp` - Add API key storage (Phase 4)
- `ui/SettingsWindow.cpp` - Add API key UI (Phase 4)

### Files Already Complete:
- `GeminiApiUtils.h` - ✅ API declarations
- `GuiState.cpp` - ✅ `ApplySearchConfig()` method
- Core parsing and prompt building - ✅ Complete

---

## Success Criteria

### Phase 1 Complete When:
- ✅ Linux can make Gemini API calls
- ✅ All three platforms use same code structure
- ✅ No code duplication between `CallGeminiApi()` and `CallGeminiApiRaw()`
- ✅ Unit tests pass on all platforms

### Phase 2 Complete When:
- ✅ Users can generate full search configs from natural language
- ✅ All search parameters are applied correctly
- ✅ Both legacy and new APIs work in UI
- ✅ Error handling works for both APIs

### Phase 3-5 Complete When:
- ✅ (Optional features implemented and tested)

---

## Notes

- **Linux is a target platform** - Linux support is critical, not optional
- **Backward compatibility** - Keep legacy path pattern API working
- **Code quality** - Follow existing patterns and naming conventions
- **Testing** - Test on all three platforms before considering complete
- **Documentation** - Update user docs when UI changes are made

