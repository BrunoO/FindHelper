# Gemini API Integration - Next Steps Plan

## Overview

This document outlines the recommended next steps for integrating the Gemini API helper functions into the main application. The helper functions are complete and tested, but they are not yet called from the application UI.

## Current Status

✅ **Completed:**
- Helper functions (`GeminiApiUtils.h` / `GeminiApiUtils.cpp`)
- Cross-platform HTTP client (WinHTTP on Windows, NSURLSession on macOS)
- JSON response parsing with robust pattern extraction
- Environment variable support (`GEMINI_API_KEY`, `GEMINI_API_SKIP`)
- Comprehensive unit tests
- Production readiness review

❌ **Not Yet Implemented:**
- UI integration (no way for users to input natural language descriptions)
- Async API calls (API calls are synchronous and would block UI)
- Error handling and user feedback in the UI
- Integration with existing search flow
- Caching/rate limiting
- Settings UI for API key configuration

---

## Phase 1: Basic UI Integration (Priority: High)

### 1.1 Add Natural Language Input Field

**Location:** `UIRenderer.cpp` → `RenderSearchInputs()`

**Changes:**
- Add a new input field for natural language descriptions (e.g., "all text files in documents folder")
- Place it near the existing search inputs (Path Search, Extensions, Filename)
- Add a button or icon to trigger the Gemini API call (e.g., "✨ Generate Pattern" or "🤖 AI Search")

**UI Design Options:**
1. **Option A: Separate input field with button**
   - New text input: "Describe files to search..."
   - Button: "Generate Pattern" → calls API and populates Path Search field
   
2. **Option B: Magic button on Path Search field**
   - Add a button/icon next to Path Search input
   - Clicking opens a popup/modal with description input
   - On success, populates Path Search field with generated pattern

3. **Option C: Smart detection**
   - If Path Search input doesn't start with "pp:", "rs:", "re:", or contain "*" or "?", treat as natural language
   - Auto-detect and call API when user presses Enter

**Recommendation:** Start with **Option A** (separate field) for clarity, then consider Option C as an enhancement.

**Files to Modify:**
- `GuiState.h`: Add `char geminiDescriptionInput[512] = "";` and `bool geminiApiCallInProgress = false;`
- `UIRenderer.cpp`: Add UI rendering in `RenderSearchInputs()`

### 1.2 Add Async API Call Wrapper

**Location:** Create new file `GeminiApiAsync.h` / `GeminiApiAsync.cpp` or add to existing files

**Implementation:**
- Wrap `GeneratePathPatternFromDescription()` in an async call using `std::async` or `std::thread`
- Return a `std::future<GeminiApiResult>` for non-blocking execution
- Handle timeout and cancellation

**Example Structure:**
```cpp
namespace gemini_api_utils {
  std::future<GeminiApiResult> GeneratePathPatternAsync(
      std::string_view user_description,
      std::string_view api_key = "",
      int timeout_seconds = 30);
}
```

**Files to Create/Modify:**
- `GeminiApiAsync.h` (new)
- `GeminiApiAsync.cpp` (new)
- Or add to `GeminiApiUtils.h` / `GeminiApiUtils.cpp`

### 1.3 Integrate with Search Flow

**Location:** `SearchController.cpp` or `UIRenderer.cpp`

**Flow:**
1. User enters description and clicks "Generate Pattern"
2. Show loading indicator (spinner, "Generating pattern...")
3. Call `GeneratePathPatternAsync()` in background
4. On completion:
   - If success: Populate `state.pathInput` with `result.path_pattern`
   - If error: Show error message (tooltip, popup, or status bar)
5. Optionally: Auto-trigger search after successful pattern generation

**Files to Modify:**
- `UIRenderer.cpp`: Handle button click, manage async call state
- `GuiState.h`: Add state for async call tracking
- `SearchController.cpp`: Optionally auto-trigger search

---

## Phase 2: Error Handling & User Feedback (Priority: High)

### 2.1 Error Display

**Implementation:**
- Show error messages in a user-friendly way:
  - **API key missing:** "Please set GEMINI_API_KEY environment variable"
  - **Network error:** "Failed to connect to Gemini API. Check your internet connection."
  - **Timeout:** "Request timed out. Please try again."
  - **Invalid response:** "Could not parse API response. Please try a different description."
  - **API error:** Show API error message if available

**UI Options:**
1. **Status bar message** (temporary, auto-dismiss after 5 seconds)
2. **Tooltip on error icon** (hover to see details)
3. **Modal popup** (for critical errors)
4. **Inline text** (below input field, red text)

**Recommendation:** Use status bar for transient errors, tooltip for details.

**Files to Modify:**
- `UIRenderer.cpp`: Add error display logic
- `GuiState.h`: Add `std::string geminiErrorMessage = "";` and `std::chrono::steady_clock::time_point errorDisplayTime;`

### 2.2 Loading States

**Implementation:**
- Show spinner/loading indicator while API call is in progress
- Disable "Generate Pattern" button during call
- Show "Generating pattern..." text

**Files to Modify:**
- `UIRenderer.cpp`: Add loading UI
- `GuiState.h`: Add `bool geminiApiCallInProgress = false;`

### 2.3 Input Validation

**Implementation:**
- Validate description is not empty before calling API
- Warn if description is too long (e.g., > 500 characters)
- Validate API key is set before allowing API call

**Files to Modify:**
- `UIRenderer.cpp`: Add validation before API call

---

## Phase 3: Settings & Configuration (Priority: Medium)

### 3.1 API Key Settings UI

**Location:** Settings window (if exists) or new settings section

**Implementation:**
- Add input field for API key in settings
- Store in environment variable or application settings file
- Show masked input (password-style) for security
- Add "Test API Key" button to validate

**Files to Modify:**
- Settings UI file (if exists)
- Or create new settings section in `UIRenderer.cpp`

### 3.2 Timeout Configuration

**Implementation:**
- Add timeout setting (default: 30 seconds)
- Allow user to adjust in settings

**Files to Modify:**
- Settings UI
- `AppSettings.h` (if exists)

---

## Phase 4: Performance & Optimization (Priority: Medium)

### 4.1 Caching

**Implementation:**
- Cache API responses: `description → path_pattern`
- Use `std::unordered_map<std::string, std::string>` for simple in-memory cache
- Cache size limit (e.g., 100 entries, LRU eviction)
- Persist cache to disk (optional, for app restarts)

**Benefits:**
- Avoid redundant API calls for same descriptions
- Faster response for repeated queries
- Reduce API usage/costs

**Files to Create:**
- `GeminiApiCache.h` / `GeminiApiCache.cpp` (new)

### 4.2 Rate Limiting

**Implementation:**
- Track API call frequency
- Limit calls per minute (e.g., max 10 calls/minute)
- Show message if rate limit exceeded: "Too many requests. Please wait a moment."

**Files to Modify:**
- `GeminiApiUtils.cpp`: Add rate limiting logic
- `GuiState.h`: Add rate limit tracking state

### 4.3 Request Debouncing

**Implementation:**
- If user types in description field, debounce API calls
- Only call API after user stops typing for 1-2 seconds (if auto-detect is enabled)
- Or only call on explicit button click (simpler, recommended)

**Files to Modify:**
- `UIRenderer.cpp`: Add debouncing logic (if auto-detect is implemented)

---

## Phase 5: Advanced Features (Priority: Low)

### 5.1 Pattern Preview

**Implementation:**
- Show generated pattern in a preview box before applying
- Allow user to edit pattern before using it
- Show explanation of what the pattern matches

**Files to Modify:**
- `UIRenderer.cpp`: Add preview UI

### 5.2 History

**Implementation:**
- Save recent descriptions and generated patterns
- Show dropdown/history menu for quick reuse
- Persist to settings file

**Files to Modify:**
- `AppSettings.h`: Add history storage
- `UIRenderer.cpp`: Add history UI

### 5.3 Multiple Pattern Suggestions

**Implementation:**
- If API returns multiple patterns, show all options
- Let user choose which pattern to use
- Or combine patterns with OR logic

**Files to Modify:**
- `GeminiApiUtils.cpp`: Parse multiple patterns from response
- `UIRenderer.cpp`: Show pattern selection UI

---

## Implementation Order

### Sprint 1: Basic Integration (Week 1)
1. ✅ Add natural language input field to UI
2. ✅ Implement async API call wrapper
3. ✅ Integrate with search flow (populate Path Search field)
4. ✅ Basic error handling (show error messages)

### Sprint 2: Polish & Settings (Week 2)
5. ✅ Loading states and user feedback
6. ✅ API key settings UI
7. ✅ Input validation
8. ✅ Timeout configuration

### Sprint 3: Optimization (Week 3)
9. ✅ Caching implementation
10. ✅ Rate limiting
11. ✅ Performance testing

### Sprint 4: Advanced Features (Week 4, Optional)
12. ✅ Pattern preview
13. ✅ History
14. ✅ Multiple pattern suggestions

---

## Technical Considerations

### Thread Safety
- Ensure UI updates happen on main thread
- Use `std::future` or `std::async` for background API calls
- Use mutex or atomic flags for state synchronization

### Memory Management
- Cache should have size limits to prevent memory bloat
- Clear error messages after display timeout
- Clean up async call futures when done

### Error Recovery
- Retry logic for transient network errors (optional)
- Fallback to manual pattern entry if API fails
- Graceful degradation if API key is not set

### Testing
- Add integration tests for UI flow
- Test error scenarios (network failure, invalid API key, etc.)
- Test async call cancellation
- Test cache behavior

---

## Files Summary

### New Files to Create:
- `GeminiApiAsync.h` / `GeminiApiAsync.cpp` (async wrapper)
- `GeminiApiCache.h` / `GeminiApiCache.cpp` (caching, optional)

### Files to Modify:
- `GuiState.h`: Add state variables for Gemini API UI
- `UIRenderer.cpp`: Add UI components and async call handling
- `SearchController.cpp`: Optionally auto-trigger search
- `AppSettings.h` / Settings UI: Add API key configuration

### Files Already Complete:
- `GeminiApiUtils.h` / `GeminiApiUtils.cpp`: ✅ Ready to use
- `tests/GeminiApiUtilsTests.cpp`: ✅ Comprehensive tests

---

## Success Criteria

✅ **Phase 1 Complete When:**
- User can enter natural language description
- Clicking "Generate Pattern" calls API asynchronously
- Generated pattern populates Path Search field
- Basic error messages are shown

✅ **Phase 2 Complete When:**
- All error scenarios show user-friendly messages
- Loading indicators work correctly
- Input validation prevents invalid API calls

✅ **Phase 3 Complete When:**
- API key can be configured in settings
- Timeout can be adjusted
- Settings persist across app restarts

✅ **Phase 4 Complete When:**
- Cache reduces redundant API calls
- Rate limiting prevents API abuse
- Performance is acceptable (< 1 second for cached responses)

---

## Questions to Resolve

1. **UI Placement:** Where should the natural language input field be placed? (Recommendation: Below Path Search field, indented)
2. **Auto-trigger Search:** Should search auto-trigger after pattern generation? (Recommendation: No, let user review pattern first)
3. **Cache Persistence:** Should cache persist to disk? (Recommendation: Yes, for better UX)
4. **Rate Limiting:** What should the rate limit be? (Recommendation: 10 calls/minute, configurable)
5. **Error Recovery:** Should we implement retry logic? (Recommendation: No for v1, add later if needed)

---

## Notes

- All helper functions are ready and tested
- Focus on UI integration first, then optimize
- Keep async calls non-blocking to maintain UI responsiveness
- Consider user experience: make it easy to use, but also allow manual pattern entry
- Follow existing code patterns and naming conventions




