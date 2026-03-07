#pragma once

/**
 * @file LoggingUtils.h
 * @brief Utilities for consistent error logging across the codebase
 *
 * This module provides helper functions to standardize error logging,
 * ensuring all errors include required context (function name, error codes,
 * relevant parameters) for easier debugging in production.
 *
 * DESIGN:
 * - Centralized logging helpers to enforce consistent format
 * - Automatic Windows error code to string conversion
 * - Context-aware error messages
 * - Reduces boilerplate and ensures all errors are logged with full context
 *
 * USAGE:
 * ```cpp
 * if (!SomeWindowsApiCall()) {
 *   DWORD err = GetLastError();
 *   logging_utils::LogWindowsApiError("SomeWindowsApiCall", 
 *                                      "Context: " + context_value, 
 *                                      err);
 *   return false;
 * }
 * ```
 */

#include <string>

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem

namespace logging_utils {
  /**
   * @brief Get human-readable Windows error string from error code
   * 
   * Uses FormatMessageA to convert Windows error code to readable string.
   * 
   * @param error_code Windows error code (from GetLastError())
   * @return Human-readable error message, or "Unknown error N" if conversion fails
   */
  std::string GetWindowsErrorString(DWORD error_code);
  
  /**
   * @brief Log Windows API error with standardized format
   * 
   * Logs error with format: "[operation] failed: [error message]. Context: [context], Error code: [code]"
   * 
   * @param operation Name of the operation that failed (e.g., "DeviceIoControl", "CreateFile")
   * @param context Additional context (e.g., file path, volume path, thread ID)
   * @param error_code Windows error code (from GetLastError())
   */
  void LogWindowsApiError(std::string_view operation,
                          std::string_view context,
                          DWORD error_code);
  
  /**
   * @brief Log exception with standardized format
   * 
   * Logs exception with format: "[operation] failed: Exception: [message]. Context: [context]"
   * 
   * @param operation Name of the operation that failed
   * @param context Additional context (e.g., file ID, thread index, range)
   * @param e Exception that was caught
   */
  void LogException(std::string_view operation,
                   std::string_view context,
                   const std::exception& e);
  
  /**
   * @brief Log unknown exception with standardized format
   * 
   * Logs unknown exception with format: "[operation] failed: Unknown exception. Context: [context]"
   * 
   * @param operation Name of the operation that failed
   * @param context Additional context (e.g., file ID, thread index, range)
   */
  void LogUnknownException(std::string_view operation,
                          std::string_view context);
  
  /**
   * @brief Log HRESULT error (COM/ActiveX errors) with standardized format
   * 
   * Logs HRESULT error with format: "[operation] failed: [error message] (HRESULT: 0x[hex]). Context: [context]"
   * 
   * @param operation Name of the operation that failed (e.g., "CoCreateInstance", "SHGetDesktopFolder")
   * @param context Additional context (e.g., CLSID, path, interface name)
   * @param hr HRESULT error code from COM/ActiveX API
   */
  void LogHResultError(std::string_view operation,
                      std::string_view context,
                      HRESULT hr);
} // namespace logging_utils

#endif  // _WIN32

