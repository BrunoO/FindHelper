/**
 * @file ClipboardUtils.cpp
 * @brief Implementation of cross-platform clipboard utilities using GLFW
 */

#include "utils/ClipboardUtils.h"
#include "utils/Logger.h"

#include <GLFW/glfw3.h>

namespace clipboard_utils {

bool SetClipboardText(GLFWwindow* window, std::string_view text) {
  if (text.empty()) {
    LOG_WARNING("SetClipboardText called with empty text");
    return false;
  }

  // GLFW handles clipboard operations cross-platform (convert to string for c_str())
  std::string text_str(text);
  glfwSetClipboardString(window, text_str.c_str());
  
  // GLFW doesn't return error codes, but we can verify by reading back
  // (though this is not 100% reliable, it's the best we can do)
  if (const char* verify = glfwGetClipboardString(window); 
      verify != nullptr && text_str == verify) {
    LOG_INFO("Clipboard text set successfully");
    return true;
  }
  
  // If verification fails, it might still have worked (timing issue)
  // Log warning but don't fail - GLFW clipboard operations are generally reliable
  LOG_WARNING("Clipboard text verification failed (may still have succeeded)");
  return true; // Assume success since GLFW doesn't provide error codes
}

std::string GetClipboardText(GLFWwindow* window) {
  const char* clipboard_text = glfwGetClipboardString(window);
  
  if (clipboard_text == nullptr) {
    // Clipboard is empty, contains non-text data, or error occurred
    // GLFW doesn't distinguish between these cases, so we return empty string
    return {};
  }
  
  // Copy the string since GLFW's pointer is only valid until next clipboard operation
  std::string result(clipboard_text);
  return result;
}

} // namespace clipboard_utils



