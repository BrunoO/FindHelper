#pragma once

#include <string>

// Forward declaration
struct GLFWwindow;

/**
 * @file ClipboardUtils.h
 * @brief Cross-platform clipboard utilities using GLFW
 *
 * This module provides simple clipboard operations using GLFW's built-in
 * clipboard functions, which work cross-platform (Windows, macOS, Linux).
 *
 * THREADING:
 * - Must be called from the main thread (GLFW requirement)
 * - All clipboard operations are synchronous
 *
 * LIFETIME:
 * - Returned strings from GetClipboardText are valid until the next
 *   clipboard operation or until GLFW is terminated
 */
namespace clipboard_utils {

/**
 * @brief Set clipboard text (cross-platform via GLFW)
 *
 * Copies the given text to the system clipboard. This works on all
 * platforms where GLFW is available (Windows, macOS, Linux).
 *
 * @param window GLFW window handle (can be nullptr on some platforms)
 * @param text Text to copy to clipboard (UTF-8 encoded)
 * @return true on success, false on failure
 *
 * @note The window parameter is required on Windows but can be nullptr
 *       on macOS/Linux. However, passing the window is recommended for
 *       consistency across platforms.
 */
bool SetClipboardText(GLFWwindow* window, std::string_view text);

/**
 * @brief Get clipboard text (cross-platform via GLFW)
 *
 * Reads text from the system clipboard. Returns empty string if clipboard
 * is empty, contains non-text data, or if an error occurs.
 *
 * @param window GLFW window handle (can be nullptr on some platforms)
 * @return Clipboard text as UTF-8 string, or empty string on failure
 *
 * @note The returned string is valid until the next clipboard operation
 *       or until GLFW is terminated. Copy the string if you need to
 *       keep it longer.
 *
 * @note The window parameter is required on Windows but can be nullptr
 *       on macOS/Linux. However, passing the window is recommended for
 *       consistency across platforms.
 */
std::string GetClipboardText(GLFWwindow* window);

} // namespace clipboard_utils



