# Gemini API Clipboard Workflow Plan

## Overview

When `GEMINI_API_KEY` environment variable is not set, the application will use a clipboard-based workflow that allows users to interact with their favorite AI assistant (Copilot, ChatGPT, etc.) instead of calling the Gemini API directly.

## Workflow

1. **User enters natural language description** in the Gemini input field
2. **User clicks "Generate Search Config" button**
3. **If API key is not set:**
   - Copy the prompt to clipboard (cross-platform)
   - Show message to user with instructions
4. **User pastes prompt into their AI assistant** (Copilot, ChatGPT, etc.)
5. **User copies AI response to clipboard**
6. **User clicks "Paste from Clipboard" button** (or similar)
7. **Application reads clipboard and parses JSON**
8. **Application applies configuration to GuiState**

## Clipboard Access Options

### Option 1: GLFW Clipboard Functions (RECOMMENDED) ✅

**Pros:**
- ✅ Already in the project (no new dependencies)
- ✅ Cross-platform (Windows, macOS, Linux)
- ✅ Simple API: `glfwSetClipboardString()` and `glfwGetClipboardString()`
- ✅ No platform-specific code needed
- ✅ Thread-safe (must be called from main thread, which matches our UI thread)

**Cons:**
- ⚠️ Requires GLFW window handle (but we already have it)

**API:**
```cpp
// Set clipboard
glfwSetClipboardString(GLFWwindow* window, const char* string);

// Get clipboard
const char* text = glfwGetClipboardString(GLFWwindow* window);
// Note: Returned string is valid until next clipboard call or library termination
```

**Implementation:**
- Access GLFW window from `Application` class (already available)
- Pass window handle to UI components that need clipboard access
- Create helper functions in `FileOperations` or new `ClipboardUtils` module

### Option 2: Platform-Specific APIs

**Windows:**
- `OpenClipboard()`, `GetClipboardData()`, `CloseClipboard()`
- Already used in `FileOperations.cpp` for `CopyPathToClipboard()`

**macOS:**
- `NSPasteboard` (Objective-C/Cocoa)
- Already used in `FileOperations_mac.mm`

**Linux:**
- X11: `xclip`/`xsel` via shell commands
- Wayland: `wl-clipboard` via shell commands
- Already used in `FileOperations_linux.cpp`

**Pros:**
- ✅ Already implemented for copy operations
- ✅ Could reuse existing code

**Cons:**
- ❌ Requires platform-specific code
- ❌ More complex to maintain
- ❌ Linux uses shell commands (less reliable)

**Verdict:** GLFW is better - simpler and already available.

### Option 3: ImGui Clipboard

ImGui has clipboard functions, but they're platform-specific under the hood and require ImGui context.

**Verdict:** GLFW is better - more direct and we already have GLFW window.

## Recommended Implementation

### 1. Create Clipboard Helper Functions

**Location:** New file `ClipboardUtils.h` and `ClipboardUtils.cpp` (or add to `FileOperations.h`)

```cpp
namespace clipboard_utils {
  // Set clipboard text (cross-platform via GLFW)
  // Returns true on success, false on failure
  bool SetClipboardText(GLFWwindow* window, const std::string& text);
  
  // Get clipboard text (cross-platform via GLFW)
  // Returns empty string on failure or if clipboard is empty
  std::string GetClipboardText(GLFWwindow* window);
}
```

### 2. Update UI Workflow

**Location:** `ui/SearchInputs.cpp`

**New UI Elements:**
- Button: "Generate Prompt" (when API key not set)
- Button: "Paste from Clipboard" (to read AI response)
- Status message area (to show instructions)

**Flow:**
1. Check if `GEMINI_API_KEY` is set
2. If not set:
   - Show "Generate Prompt" button
   - On click: Build prompt, copy to clipboard, show message
3. Show "Paste from Clipboard" button (always visible when in clipboard mode)
4. On click: Read clipboard, parse JSON, apply config

### 3. Message to Show User

When prompt is copied to clipboard, show:

```
✅ Prompt copied to clipboard!

Next steps:
1. Paste the prompt into your AI assistant (Copilot, ChatGPT, etc.)
2. Copy the AI's JSON response
3. Click "Paste from Clipboard" to apply the configuration
```

### 4. Error Handling

- **Clipboard write failure:** Show error message
- **Clipboard read failure:** Show error message
- **Invalid JSON:** Show parsing error with helpful message
- **Missing fields:** Use defaults (as per design)

## Implementation Steps

1. ✅ **Create ClipboardUtils module** with GLFW-based functions
2. ✅ **Update UI** to check for API key and show appropriate buttons
3. ✅ **Add "Generate Prompt" button** that copies prompt to clipboard
4. ✅ **Add "Paste from Clipboard" button** that reads and parses JSON
5. ✅ **Add status message area** to show instructions
6. ✅ **Test workflow** on all platforms

## Code Structure

```
ClipboardUtils.h/cpp
  - SetClipboardText(GLFWwindow*, string) -> bool
  - GetClipboardText(GLFWwindow*) -> string

ui/SearchInputs.cpp
  - Check GEMINI_API_KEY
  - Show "Generate Prompt" or "Generate Config" button
  - Show "Paste from Clipboard" button
  - Handle clipboard operations
  - Show status messages

Application.cpp
  - Pass GLFW window to UI components (already available)
```

## Testing Checklist

- [ ] Copy prompt to clipboard works (Windows)
- [ ] Copy prompt to clipboard works (macOS)
- [ ] Copy prompt to clipboard works (Linux)
- [ ] Read from clipboard works (Windows)
- [ ] Read from clipboard works (macOS)
- [ ] Read from clipboard works (Linux)
- [ ] JSON parsing works with valid responses
- [ ] Error handling for invalid JSON
- [ ] Error handling for clipboard failures
- [ ] UI messages are clear and helpful

## Future Enhancements

- **Auto-detect clipboard changes:** Poll clipboard and auto-parse when JSON is detected (optional)
- **Clipboard history:** Remember last N clipboard contents
- **Validation feedback:** Show preview of config before applying
- **Edit before apply:** Allow user to edit JSON before applying



