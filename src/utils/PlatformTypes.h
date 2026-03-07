#pragma once

/**
 * @file PlatformTypes.h
 * @brief Platform-specific type aliases for cross-platform compatibility
 *
 * This header provides type aliases that abstract platform differences,
 * allowing headers to use unified APIs without conditional compilation.
 */

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
using NativeWindowHandle = HWND;
#else
// On macOS/Linux, native window handle is not needed for most operations
// NOSONAR - void* used for cross-platform abstraction (always nullptr, ignored)
// This allows API compatibility while indicating "no handle needed" on non-Windows
using NativeWindowHandle = void*;  // NOSONAR(cpp:S5008) - Opaque handle type for platform-agnostic window representation
#endif  // _WIN32

